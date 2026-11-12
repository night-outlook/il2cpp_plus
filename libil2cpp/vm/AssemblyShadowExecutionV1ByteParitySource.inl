// R03 B2 INACTIVE. Intentionally non-noexcept healthy-time diagnostic only.
// Calls the ORIGINAL allocating V1 execution getter under a metadata Reader,
// then separately calls the authenticated bounded physical source. These are
// two observations, not an atomic linearization of all Class/GC/VM state.
// The original getter is UNMODIFIED. No current Player or transport calls us.
bool AssemblyShadow::TryProbeBoundedExecutionV1ByteParity(
    assembly_shadow_physical_membership::Scratch& membership,
    ::r03_b2_owned_execution_rows::Rows& rows,
    assembly_shadow_frozen_execution_record::Record& record,
    assembly_shadow_bounded_definition_execution::Receipt& sourceReceipt,
    assembly_shadow_execution_v1_parity::Probe& parity,
    std::string& originalV1)
{
    namespace live=assembly_shadow_bounded_definition_execution;
    namespace compare=assembly_shadow_execution_v1_parity;
    originalV1.clear();
    sourceReceipt=live::Receipt{};
    auto reject=[&](live::Error reason)->bool {
        originalV1.clear();
        return live::Reject(sourceReceipt,reason,membership,rows,record);
    };
    if(!parity.Fresh()) {
        parity.RejectLegacy(); // One-shot: cannot revive a previous result.
        return reject(live::Error::ReusedSource);
    }
    // Initial-runtime admission covers BOTH invocations (and allocation and
    // comparison). Neither invocation can bypass Runtime::Shutdown teardown
    // or substitute its CURRENT epoch for missing class provenance.
    assembly_shadow_metadata_lifetime::Gate::Reader admission(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!admission) {
        parity.RejectLegacy();
        return reject(live::Error::NoMetadataAdmission);
    }
    // The original getter may allocate, query logical assemblies and throw.
    // This probe must run while healthy; it is NOT terminal-safe and is not
    // an immutable pre-error V1 capture.
    AssemblyShadowError legacyStatus=AssemblyShadowError::InvalidState;
    try {
        legacyStatus=TryGetExecutionDiagnosticsWithMetadataAdmission(originalV1);
    } catch(...) {
        parity.RejectLegacy();
        return reject(live::Error::SourceChanged);
    }
    if(legacyStatus!=AssemblyShadowError::Success ||
       originalV1.empty() ||
       originalV1.size()>compare::Probe::kMaximumBytes) {
        parity.RejectLegacy();
        return reject(live::Error::SourceChanged);
    }
    if(!TryCaptureBoundedDefinitionExecutionLive(
        membership,rows,record,sourceReceipt)) {
        parity.RejectBounded();
        originalV1.clear();
        return false; // Native source has already revoked its positive flags.
    }
    // The restricted source receipt is checked independently; a successful
    // record alone does NOT prove source origin, complete original field15,
    // one coherent VM epoch, or a usable terminal V2 descriptor.
    if(!sourceReceipt.callbackPhysicalMembershipProven ||
       !sourceReceipt.restrictedTypeDefSubsetProven ||
       !sourceReceipt.observedRegistryAndCounterFenceStable ||
       !record.Complete() || !record.Data() || !record.Bytes()) {
        parity.RejectBounded();
        return reject(live::Error::SourceChanged);
    }
    const bool equal=parity.Compare(
        {originalV1.data(),originalV1.size()},
        {record.Data(),record.Bytes()});
    if(!equal) {
        // Keep first-difference offset and lengths in parity.Result(), but
        // never return either stale original bytes or a complete frozen record
        // on a mismatch. A changed timer/cctor/observation between reads may
        // produce this result without implying broken source formatting.
        return reject(live::Error::SourceChanged);
    }
    // Matched bytes prove only that two independently timed observations
    // produced exactly the same UTF-8 JSON; explicitly no VM epoch or V2
    // readiness claim. Caller can inspect originalV1 while it owns it.
    return true;
}
