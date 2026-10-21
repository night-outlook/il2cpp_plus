// P1 INACTIVE: composed source path from the genuine already-implemented
// class enumerator to a wholly owned, noncanonical diagnostic JSON buffer.
// Neither the original execution V1 getter nor any Player call invokes this.
// Caller must preallocate Scratch, Capture and Record while healthy; the
// output is 2 MiB and MUST NOT be placed on a small VM/Player thread stack.
bool AssemblyShadow::TryCaptureOwnedObservedPhysicalJson(
    assembly_shadow_physical_membership::Scratch& membershipScratch,
    assembly_shadow_owned_physical_names::Capture& names,
    assembly_shadow_owned_physical_names::Receipt& namesReceipt,
    assembly_shadow_owned_physical_json::Record& jsonOutput,
    assembly_shadow_owned_physical_json::Receipt& jsonReceipt,
    size_t maximumBytes)
{
    namespace json=assembly_shadow_owned_physical_json;
    jsonReceipt=json::Receipt{};
    // Refuse unusable output before an expensive physical VM traversal.
    // Do not allow previous complete JSON or partial output to survive a
    // rejected attempt. The other one-use input objects also fail closed.
    if(!jsonOutput.Fresh())
    {
        names.Revoke();
        membershipScratch.Scrub();
        return json::Fail(jsonOutput,jsonReceipt,json::Error::ReusedOutput);
    }
    if(maximumBytes>json::Record::kMaximumBytes)
    {
        names.Revoke();
        membershipScratch.Scrub();
        return json::Fail(jsonOutput,jsonReceipt,json::Error::InvalidOutputLimit);
    }
    // This real VM source acquires its own initial-session Reader, captures
    // under the short registry lock, and copies names inside g_MetadataLock;
    // all VM locks and Reader are released when it returns.
    if(!TryCaptureOwnedObservedPhysicalNames(membershipScratch,names,namesReceipt))
        return json::Fail(jsonOutput,jsonReceipt,json::Error::IncompleteSource);

    // This pure formatting step may allocate no VM class/type metadata and
    // requires no metadata or registry locks. It owns every emitted byte.
    const bool composed=json::Compose(names,namesReceipt,jsonOutput,jsonReceipt,
                                     maximumBytes);
    // Source names are no longer needed, even when the JSON writer failed.
    // No borrowed class/image/assembly/name pointer escapes the prior pass.
    names.Revoke();
    return composed;
}
