// P1 native source, included ONCE in AssemblyShadow.cpp inside il2cpp::vm.
// INACTIVE, no Player or original getter invokes this method.
bool AssemblyShadow::TryCaptureBoundedEmptyExecutionLive(
    assembly_shadow_frozen_execution_record::Record& output,
    assembly_shadow_bounded_execution_empty_live::Receipt& receipt)
{
    namespace live=assembly_shadow_bounded_execution_empty_live;
    namespace r02=assembly_shadow_r02;
    namespace frozen=assembly_shadow_frozen_execution_record;
    receipt=live::Receipt{};
    if(output.Complete() || output.FirstError()!=frozen::RecordError::None)
        return live::Fail(receipt,live::Error::ReusedOutput,output);

    // Outer initial-source metadata ticket extends through the final output
    // and readback. No source mutex is nested with another mutex here.
    assembly_shadow_metadata_lifetime::Gate::Reader admission(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!admission)
        return live::Fail(receipt,live::Error::MetadataAdmissionUnavailable,output);

    struct Stamp
    {
        const ActiveSnapshot* active=nullptr;
        AssemblyShadowState state=AssemblyShadowState::Disabled;
        uint64_t generation=0;
        size_t count=0;
        bool classCountValid=false;
        bool Equal(const Stamp& other) const noexcept
        {
            return active==other.active && state==other.state &&
                   generation==other.generation && count==other.count &&
                   classCountValid && other.classCountValid;
        }
    };
    auto sample=[]()->Stamp
    {
        Stamp value;
        value.active=s_active.load(std::memory_order_acquire);
        value.state=s_state.load(std::memory_order_acquire);
        value.generation=value.active ? value.active->generation : 0;
        size_t actual=0;
        {
            ExecutionObservationLock lock; // Never combined with g_MetadataLock.
            value.count=s_executionClassCount;
            for(const Il2CppClass* klass:s_executionClasses)
                if(klass)++actual; // Opaque only; NO pointer dereference.
        }
        value.classCountValid=(value.count<=kMaximumExecutionClasses &&
                               actual==value.count);
        return value;
    };
    const Stamp before=sample();
    receipt.observedClassesBefore=before.count;
    receipt.activeGenerationBefore=before.generation;
    if(!before.classCountValid)
        return live::Fail(receipt,live::Error::InvalidClassObservation,output);
    // A single nonempty class requires physical metadata provenance/type keys.
    // Never silently emit a partial classes[] as if it were complete.
    if(before.count!=0)
        return live::Fail(receipt,live::Error::ClassRowsNotSupported,output);

    r02::Snapshot observed{};
    if(!r02::Snapshot::TryCaptureCurrent(observed,receipt.countersBefore))
        return live::Fail(receipt,live::Error::CounterCaptureFailed,output);
    if(!live::CompleteCoverage(observed))
        return live::Fail(receipt,live::Error::IncompleteCoverage,output);
    const Stamp collected=sample();
    receipt.observedClassesAfter=collected.count;
    receipt.activeGenerationAfter=collected.generation;
    if(!before.Equal(collected))
        return live::Fail(receipt,live::Error::ObservedSourceChange,output);
    if(!output.Begin({static_cast<int32_t>(before.state),
                      assembly_shadow_diagnostic::Quoted(
                          AssemblyShadowDiagnostics::StateName(before.state)),
                      before.generation},observed,0))
        return live::Fail(receipt,live::Error::BeginFailed,output);
    if(!output.Freeze())
        return live::Fail(receipt,live::Error::FreezeFailed,output);

    // Formatting may execute without any metadata mutex. Recheck ALL
    // captured R02 values and the monotonic class inventory afterward.
    r02::Snapshot finishedCounters{};
    if(!r02::Snapshot::TryCaptureCurrent(finishedCounters,receipt.countersAfter))
        return live::Fail(receipt,live::Error::CounterCaptureFailed,output);
    const Stamp finished=sample();
    receipt.observedClassesAfter=finished.count;
    receipt.activeGenerationAfter=finished.generation;
    if(!before.Equal(finished) ||
       !live::Equivalent(observed,finishedCounters))
        return live::Fail(receipt,live::Error::ObservedSourceChange,output);
    receipt.zeroClassSubsetComplete=true;
    receipt.observedSourceFenceStable=true;
    // Never set classPointerOriginProven, coherentVmEpochProven or acceptance.
    return true;
}
