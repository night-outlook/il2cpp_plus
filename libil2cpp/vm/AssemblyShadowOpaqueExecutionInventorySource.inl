// INACTIVE, included once by AssemblyShadow.cpp within the VM namespace.
bool AssemblyShadow::TryCaptureOpaqueExecutionInventory(
    assembly_shadow_opaque_execution::Scratch& scratch,
    assembly_shadow_opaque_execution::Receipt& receipt)
{
    namespace opaque=assembly_shadow_opaque_execution;
    receipt=opaque::Receipt{};
    if(!scratch.Consume())
        return opaque::Fail(scratch,receipt,opaque::Error::ReusedScratch);
    // Metadata admission remains active over both source samples. The ticket
    // is initial-runtime-only; it does not authenticate pointer provenance.
    assembly_shadow_metadata_lifetime::Gate::Reader admission(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!admission)
        return opaque::Fail(scratch,receipt,opaque::Error::AdmissionUnavailable);
    static_assert(std::tuple_size<decltype(s_executionClasses)>::value==opaque::kSlotCount,
                  "Opaque execution slot count changed");
    auto sample=[](opaque::Snapshot& output)->bool {
        // Only the existing observation mutex is held, never MetadataLock,
        // UsageLock or the transaction mutex. No metadata dereference.
        ExecutionObservationLock lock;
        return output.Populate(s_executionClasses.data(),
                               s_executionClasses.size(),s_executionClassCount);
    };
    const ActiveSnapshot* beforeActive=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState beforeState=s_state.load(std::memory_order_acquire);
    receipt.generationBefore=beforeActive?beforeActive->generation:0;
    if(!sample(scratch.Before()))
        return opaque::Fail(scratch,receipt,opaque::Error::InvalidRegistry);
    receipt.completedSourceSamples=1;
    receipt.countBefore=scratch.Before().Count();
    if(!scratch.Before().Unique())
        return opaque::Fail(scratch,receipt,opaque::Error::DuplicateIdentity);

    if(!sample(scratch.After()))
        return opaque::Fail(scratch,receipt,opaque::Error::InvalidRegistry);
    receipt.completedSourceSamples=2;
    receipt.countAfter=scratch.After().Count();
    if(!scratch.After().Unique())
        return opaque::Fail(scratch,receipt,opaque::Error::DuplicateIdentity);
    const ActiveSnapshot* afterActive=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState afterState=s_state.load(std::memory_order_acquire);
    receipt.generationAfter=afterActive?afterActive->generation:0;
    if(beforeActive!=afterActive||beforeState!=afterState||
       receipt.generationBefore!=receipt.generationAfter||
       !scratch.Before().SameAs(scratch.After()))
        return opaque::Fail(scratch,receipt,opaque::Error::SourceChanged);
    // No opaque address is returned. Neither stability nor membership
    // establishes physical origin, lifetime, ABA freedom or VM coherence.
    scratch.Scrub();
    receipt.opaqueInventoryObserved=true;
    receipt.exactOpaqueSlotSamplesEqual=true;
    return true;
}
