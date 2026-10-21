// P1 INACTIVE: actual matched physical classes -> copied primitive facts ->
// pointer-free nine-category census -> independently owned noncanonical JSON.
// This is NOT the original V1 getter, a coherent VM epoch, or a Player API.
// Caller allocates exclusive scratch/capture/output outside VM locks while
// healthy. Pure formatting begins only after the original Reader and metadata
// plus registry locks have been released by the prior source method.
bool AssemblyShadow::TryCaptureObservedTypeShapeJson(
    assembly_shadow_physical_membership::Scratch& membershipScratch,
    assembly_shadow_owned_physical_names::Capture& names,
    assembly_shadow_owned_physical_names::Receipt& namesReceipt,
    assembly_shadow_observed_type_shape_census::Result& censusResult,
    assembly_shadow_observed_type_shape_json::Record& jsonOutput,
    assembly_shadow_observed_type_shape_json::Receipt& jsonReceipt,
    size_t maximumBytes)
{
    namespace census=assembly_shadow_observed_type_shape_census;
    namespace json=assembly_shadow_observed_type_shape_json;
    jsonReceipt=json::Receipt{};
    // An early failure must never expose a previously successful names receipt.
    namesReceipt=assembly_shadow_owned_physical_names::Receipt{};
    // Fail stale output BEFORE entering any VM iterator/metadata lifetime gate.
    if(!jsonOutput.Fresh())
    {
        names.Revoke();
        membershipScratch.Scrub();
        census::Fail(censusResult,census::Error::ReusedCensus);
        return json::Fail(jsonOutput,jsonReceipt,json::Error::ReusedOutput);
    }
    if(!census::Fresh(censusResult))
    {
        names.Revoke();
        membershipScratch.Scrub();
        census::Fail(censusResult,census::Error::ReusedCensus);
        return json::Fail(jsonOutput,jsonReceipt,json::Error::IncompleteCensus);
    }
    if(maximumBytes>json::Record::kMaximumBytes)
    {
        names.Revoke();
        membershipScratch.Scrub();
        census::Fail(censusResult,census::Error::InvalidShapeCensus);
        return json::Fail(jsonOutput,jsonReceipt,json::Error::InvalidOutputLimit);
    }
    // Actual existing implementation: initial-session Reader, short registry
    // locks, first-match physical callback under g_MetadataLock, source/revision
    // fence, owned primitive bits, and SCRUB of every borrowed pointer.
    // Its return releases all original locks/readers and revokes source names.
    if(!TryCaptureObservedTypeShapeCensus(
           membershipScratch,names,namesReceipt,censusResult))
        return json::Fail(jsonOutput,jsonReceipt,json::Error::IncompleteCensus);
    // Only primitive counts/revision survive. No VM pointer or name is read.
    // The pure writer cannot touch the VM, class allocation or metadata.
    return json::Compose(censusResult,jsonOutput,jsonReceipt,maximumBytes);
}
