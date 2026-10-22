// R03 P1 INACTIVE: whole-call metadata teardown admission for a diagnostic
// read of the UNMODIFIED original GetExecutionDiagnosticsJson(). Not wired to
// normal managed/native getters, policy, V1/V2 transport or the Unity Player.
//
// The original getter may inspect Il2CppClass/metadata and allocate while
// formatting. This gate only ensures Runtime::Shutdown cannot reach metadata
// teardown during the delegated call in the initial admitted session. It
// does NOT prove that its recorded Il2CppClass pointers came from a valid
// allocator, have a coherent VM epoch, or satisfy execution TypeKeys.
// Never open a new gate epoch here or substitute the current epoch for the
// caller's missing physical source provenance.
AssemblyShadowError AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(
    std::string& json)
{
    json.clear(); // Also on failed/closed admission; no stale prior output.
    assembly_shadow_metadata_lifetime::Gate::Reader reader(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if (!reader)
        return AssemblyShadowError::InvalidState;
    // Reader stays alive over the entire original getter, including
    // metadata enumeration and JSON formatting. Its destructor also
    // releases admission if the original getter raises an exception.
    return GetExecutionDiagnosticsJson(json);
}
