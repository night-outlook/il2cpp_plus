// Included exactly once by AssemblyShadow.cpp INSIDE the native VM namespace.
// This code is production source but INACTIVE: no getter/Player calls it.
bool AssemblyShadow::TryCaptureBoundedGeneralLive(
    const assembly_shadow_bounded_general_live::Sources& sources,
    assembly_shadow_bounded_general_live::Scratch& scratch,
    assembly_shadow_frozen_general::Capture& output,
    assembly_shadow_bounded_general_live::Receipt& receipt)
{
    // All Capture/Receipt/Scratch objects must already be constructed while
    // healthy, owned by one thread, and never reused. Current() and its
    // transaction must also have been prewarmed.
    namespace live=assembly_shadow_bounded_general_live;
    namespace composer=assembly_shadow_bounded_general;
    receipt=live::Receipt{};
    if(!sources.transaction || !sources.firstUses ||
       !sources.ordinaryAssemblies || !sources.ordinaryClasses ||
       scratch.sealed || scratch.count || scratch.scannedBytes)
        return live::Fail(receipt,live::Error::MissingSource,output);

    // Hold one teardown admission for the WHOLE operation, not just for each
    // nested bounded metadata/assembly/first-use reader. Runtime::Shutdown
    // must drain this reader before MetadataAllocCleanup, but a VM deadlock
    // or the origin/lifetime of stored Il2CppClass* still requires real proof.
    assembly_shadow_metadata_lifetime::Gate::Reader reader(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!reader)
        return live::Fail(receipt,live::Error::MetadataAdmissionUnavailable,output);

    // A conservatively compared read fence observes independently published
    // controls. It is NOT an atomic all-VM epoch, and equal samples may hide
    // ABA or unobserved metadata/R02 counter changes.
    struct Fence
    {
        const ActiveSnapshot* active=nullptr;
        const CandidateRegistry* candidates=nullptr;
        const StableAotConfiguration* configuration=nullptr;
        const TypeFailure* failure=nullptr;
        const std::string* referenceDetail=nullptr;
        StartupAttempt startup=StartupAttempt::Unattempted;
        AssemblyShadowState state=AssemblyShadowState::Disabled;
        uint64_t usage=0,activeGeneration=0;
        bool unexpected=false,late=false,referenceViolation=false,early=false;
        bool SameAs(const Fence& b) const noexcept
        {
            return active==b.active && candidates==b.candidates &&
                configuration==b.configuration && failure==b.failure &&
                referenceDetail==b.referenceDetail && startup==b.startup &&
                state==b.state && usage==b.usage &&
                activeGeneration==b.activeGeneration &&
                unexpected==b.unexpected && late==b.late &&
                referenceViolation==b.referenceViolation && early==b.early;
        }
    };
    auto sample=[]()->Fence
    {
        Fence f;
        f.active=s_active.load(std::memory_order_acquire);
        f.candidates=s_candidates.load(std::memory_order_acquire);
        f.configuration=s_configuration.load(std::memory_order_acquire);
        f.failure=s_typeFailure.load(std::memory_order_acquire);
        f.referenceDetail=s_referenceViolationDetail.load(std::memory_order_acquire);
        f.startup=s_startupAttempt.load(std::memory_order_acquire);
        f.state=s_state.load(std::memory_order_acquire);
        f.unexpected=s_unexpectedFailure.load(std::memory_order_acquire);
        f.late=s_lateBaselineUse.load(std::memory_order_acquire);
        f.referenceViolation=s_referenceViolation.load(std::memory_order_acquire);
        f.early=s_earlyTracking.load(std::memory_order_acquire);
        f.activeGeneration=f.active ? f.active->generation : 0;
        {
            UsageLock lock; // ordinary s_usageGeneration is protected here.
            f.usage=s_usageGeneration;
        }
        return f;
    };
    const Fence before=sample();
    receipt.usageGenerationBefore=before.usage;
    receipt.activeGenerationBefore=before.activeGeneration;

    // Take component source locks serially; NEVER nest the transaction mutex,
    // UsageLock, s_assemblyLock and g_MetadataLock. Each reader owns its own
    // fixed storage, original ordering and distinct fail-closed contract.
    if(!AssemblyShadow::TryCaptureBoundedTransactionInventory(*sources.transaction))
        return live::Fail(receipt,live::Error::TransactionFailed,output);
    if(!AssemblyShadow::TryCaptureBoundedFirstUses(*sources.firstUses))
        return live::Fail(receipt,live::Error::FirstUseFailed,output);
    if(!Assembly::TryCaptureBoundedPhysicalAssemblies(*sources.ordinaryAssemblies))
        return live::Fail(receipt,live::Error::PhysicalFailed,output);
    if(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*sources.ordinaryClasses))
        return live::Fail(receipt,live::Error::ClassesFailed,output);

    // Generated startup names are process-lifetime static literals. Keep
    // bounded borrowed spans only until Compose has copied them synchronously.
    // No heap vector/string and no unbounded strlen or silent truncation.
    for(const char** name=hybridclr::g_assemblyShadowStartupCandidates; *name; ++name)
    {
        if(!scratch.AddStartup(*name))
            return live::Fail(receipt,
                scratch.count==live::Scratch::kMaximumStartupNames ?
                live::Error::StartupLimit : live::Error::MalformedStartup,output);
    }
    if(!scratch.Seal())
        return live::Fail(receipt,live::Error::MalformedStartup,output);
    receipt.startupCandidates=scratch.count;
    receipt.startupTextBytes=scratch.scannedBytes;

    const Fence collected=sample();
    receipt.usageGenerationAfter=collected.usage;
    receipt.activeGenerationAfter=collected.activeGeneration;
    if(!before.SameAs(collected))
        return live::Fail(receipt,live::Error::ObservedSourceChange,output);

    composer::Inputs input{};
    input.header={true,
        static_cast<int32_t>(hybridclr::g_assemblyShadowStartupCandidateSchemaVersion),
        assembly_shadow_diagnostic::Quoted(before.early?"EarlyTracking":"ConfigureOnly")};
    input.startupCandidateNames=scratch.names.data();
    input.startupCandidateCount=scratch.count;
    input.transaction=sources.transaction;
    input.firstUses=sources.firstUses;
    input.ordinaryAssemblies=sources.ordinaryAssemblies;
    input.ordinaryClasses=sources.ordinaryClasses;
    if(!composer::Compose(input,output,receipt.composed))
        return live::Fail(receipt,live::Error::ComposeFailed,output);

    // A source change during the formatter invalidates even a fully closed
    // legacy-compatible byte record. No retry, fallback or partial success.
    const Fence finished=sample();
    receipt.usageGenerationAfter=finished.usage;
    receipt.activeGenerationAfter=finished.activeGeneration;
    if(!collected.SameAs(finished))
        return live::Fail(receipt,live::Error::ObservedSourceChange,output);
    receipt.componentSourcesComplete=true;
    receipt.observableSourceFenceStable=true;
    // Deliberately no global epoch/race-free/GC/RSS/Player acceptance flag.
    return true;
}
