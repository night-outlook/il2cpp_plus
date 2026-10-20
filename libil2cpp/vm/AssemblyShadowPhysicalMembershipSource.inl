// P1 INACTIVE / VM namespace: no public getter or Player caller.
// The actual class iterator is called under g_MetadataLock, but none of the
// registered pointer values is dereferenced or exposed by this method.
bool AssemblyShadow::TryAuditObservedPhysicalClassMembership(
    assembly_shadow_physical_membership::Scratch& scratch,
    assembly_shadow_physical_membership::Receipt& receipt)
{
    namespace membership=assembly_shadow_physical_membership;
    receipt=membership::Receipt{};
    if(!scratch.Consume())
        return membership::Fail(scratch,receipt,membership::Error::ReusedScratch);
    // Source gate is initial-session-only, never a proof of VM pointer origin.
    assembly_shadow_metadata_lifetime::Gate::Reader admission(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!admission)
        return membership::Fail(scratch,receipt,membership::Error::AdmissionUnavailable);
    static_assert(std::tuple_size<decltype(s_executionClasses)>::value==membership::kSlots,
                  "Physical membership and execution registry slot count changed");
    const ActiveSnapshot* activeBefore=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState stateBefore=s_state.load(std::memory_order_acquire);
    {
        ExecutionObservationLock lock;
        if(!scratch.Capture(s_executionClasses.data(),s_executionClasses.size(),
            s_executionClassCount,s_executionRegistryRevision.Current(),
            s_executionRegistrationWitnesses))
            return membership::Fail(scratch,receipt,membership::Error::InvalidRegistry);
    }
    if(!scratch.Unique())
        return membership::Fail(scratch,receipt,membership::Error::DuplicateObservedIdentity);
    receipt.observedClasses=scratch.Observed();
    receipt.registryRevisionBefore=scratch.Revision();
    // Do NOT nest ExecutionObservationLock with g_MetadataLock. The callback
    // compares opaque addresses only. The underlying VM iterator may allocate
    // or perform additional work; no process RSS or latency bound is claimed.
    if(receipt.observedClasses)
    {
        os::FastAutoLock metadataLock(&g_MetadataLock);
        receipt.enumeratorInvoked=true;
        il2cpp_class_for_each([](Il2CppClass* klass,void* ctx) {
            static_cast<membership::Scratch*>(ctx)->ObserveEnumerated(klass);
        },&scratch);
    }
    receipt.enumeratedClasses=scratch.Enumerated();
    receipt.matchedClasses=scratch.Matched();
    if(!scratch.EnumeratorHealthy())
        return membership::Fail(scratch,receipt,membership::Error::EnumeratorIncomplete);
    bool sameRegistry=false;
    {
        ExecutionObservationLock lock;
        sameRegistry=scratch.SameRegistry(s_executionClasses.data(),s_executionClasses.size(),
            s_executionClassCount,s_executionRegistryRevision.Current(),
            s_executionRegistrationWitnesses);
        receipt.registryRevisionAfter=s_executionRegistryRevision.Current();
    }
    const ActiveSnapshot* activeAfter=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState stateAfter=s_state.load(std::memory_order_acquire);
    if(!sameRegistry||activeBefore!=activeAfter||stateBefore!=stateAfter)
        return membership::Fail(scratch,receipt,membership::Error::SourceChanged);
    if(!scratch.FullyMatched())
        return membership::Fail(scratch,receipt,membership::Error::UnmatchedObservedIdentity);
    scratch.Scrub();
    // ONLY an observed same-session physical iterator membership subset.
    receipt.enumeratedMembershipComplete=true;
    return true;
}
