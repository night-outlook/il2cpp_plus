// P1 INACTIVE: actual physical class iterator -> primitive shape-only census.
// This method is never called by the legacy execution JSON getters or Player.
// Scratch and bounded capture MUST be allocated while healthy. The new census
// contains only fixed counters/flags, no borrowed VM class or metadata handle.
bool AssemblyShadow::TryCaptureObservedTypeShapeCensus(
    assembly_shadow_physical_membership::Scratch& membershipScratch,
    assembly_shadow_owned_physical_names::Capture& names,
    assembly_shadow_owned_physical_names::Receipt& namesReceipt,
    assembly_shadow_observed_type_shape_census::Result& censusResult)
{
    namespace shapes=assembly_shadow_observed_type_shape_census;
    // A rejected early call must not leave an older successful source receipt.
    namesReceipt=assembly_shadow_owned_physical_names::Receipt{};
    // Refuse stale or partly populated output before running the VM iterator.
    // Existing one-use capture and scratch will also fail their own admission.
    if(!shapes::Fresh(censusResult))
    {
        names.Revoke();
        membershipScratch.Scrub();
        return shapes::Fail(censusResult,shapes::Error::ReusedCensus);
    }
    // This method owns its initial-session Reader, first-match physical
    // enumeration under g_MetadataLock, separate short registry locks and
    // revision/state fences. Its return releases every reader/lock.
    if(!TryCaptureOwnedObservedPhysicalNames(membershipScratch,names,namesReceipt))
        return shapes::Fail(censusResult,shapes::Error::IncompleteCapture);

    // Build only over copied primitive shape data and copied registry roles;
    // no class pointer, metadata lock, or live VM operation remains.
    const bool completed=shapes::Build(names,namesReceipt,censusResult);
    names.Revoke(); // Even a failed census leaves no temporary copied rows.
    return completed;
}
