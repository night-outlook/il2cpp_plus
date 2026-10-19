// R03 P1 INACTIVE; included once by AssemblyShadow.cpp under feature ON.
// Identity-only source census. Never dereference, retain or publish a klass,
// metadata type pointer or its raw address. No change to the original getter.
bool AssemblyShadow::TryCaptureBoundedExecutionCensus(
    assembly_shadow_bounded_execution_census::Capture& capture)
{
    namespace bounded=assembly_shadow_bounded_execution_census;
    namespace r02=assembly_shadow_r02;

    assembly_shadow_metadata_lifetime::Gate::Reader reader(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!reader)return capture.RejectOwnerUnavailable();

    auto sample=[]()->bounded::Sample
    {
        bounded::Sample result{};
        result.diagnosticsLevel=r02::ObservationCounters::Level;
        {
            // Only the existing process-lifetime execution-identity table is
            // protected by this short lock. No metadata lock, type formatter,
            // transaction mutex, managed execution or heap collection here.
            ExecutionObservationLock lock;
            result.declaredCount=s_executionClassCount;
            result.tableCapacity=s_executionClasses.size();
            uint64_t digest=UINT64_C(14695981039346656037);
            for(size_t index=0;index<s_executionClasses.size();++index)
            {
                const Il2CppClass* klass=s_executionClasses[index];
                if(!klass)continue;
                ++result.presentCount;
                // The digest is solely an in-process *change witness*,
                // not a cryptographic or public class identity. The pointer
                // is NOT dereferenced or copied into the Capture.
                const uint64_t bits=static_cast<uint64_t>(
                    reinterpret_cast<uintptr_t>(klass));
                digest^=bits;
                digest*=UINT64_C(1099511628211);
                digest^=static_cast<uint64_t>(index);
                digest*=UINT64_C(1099511628211);
            }
            result.fingerprint=digest;
        }
        // Read these separately AFTER releasing ExecutionObservationLock.
        // Both sources are monotonic within the initial runtime lifetime.
        // Even two identical read-fence samples are not an all-VM epoch.
        result.droppedObservations=s_droppedClassObservations.load(
            std::memory_order_relaxed);
        result.droppedCounterThreads=r02::ObservationCounters::DroppedThreads();
        result.countersSaturated=r02::ObservationCounters::Saturated();
        return result;
    };

    const bounded::Sample before=sample();
    if(!capture.Begin(before))return false;
    const bounded::Sample after=sample();
    return capture.Seal(after);
}
