// Included exactly once inside the AssemblyShadow.cpp feature-enabled VM TU.
// INACTIVE: no original GetExecutionDiagnosticsJson or Player caller.
bool AssemblyShadow::TryCaptureBoundedExecutionHeader(
    assembly_shadow_bounded_execution_header::Capture& capture)
{
    namespace bounded=assembly_shadow_bounded_execution_header;
    namespace serial=assembly_shadow_execution_diagnostic;
    namespace r02=assembly_shadow_r02;
    // This ticket covers the ActiveSnapshot pointer lifetime only. It does
    // not impose a global epoch, quiesce counter writers or own class metadata.
    assembly_shadow_metadata_lifetime::Gate::Reader reader(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!reader)return capture.RejectOwnerUnavailable();

    auto readHeader=[]()->serial::Header
    {
        serial::Header h{};
        const ActiveSnapshot* active=s_active.load(std::memory_order_acquire);
        const AssemblyShadowState state=s_state.load(std::memory_order_acquire);
        h.stateCode=static_cast<int32_t>(state);
        h.state=AssemblyShadowDiagnostics::StateName(state);
        h.generation=active?active->generation:0;

        // Same publication order and acquire-before-relaxed sequencing as
        // GetExecutionDiagnosticsJson. Recheck every field below and refuse a
        // witnessed change; these reads are NOT one atomic VM snapshot.
        h.shadowMethodChecks=s_shadowMethodChecks.load(std::memory_order_acquire);
        h.rejectedBaselineMethods=s_rejectedBaselineMethods.load(std::memory_order_acquire);
        h.methodChecks=s_methodChecks.load(std::memory_order_relaxed);
        h.shadowInterpreterTransformations=
            s_shadowInterpreterTransformations.load(std::memory_order_acquire);
        h.interpreterTransformations=
            s_interpreterTransformations.load(std::memory_order_relaxed);
        h.baselineClassCctorStarted=s_baselineClassCctorStarted.load(std::memory_order_relaxed);
        h.shadowClassCctorStarted=s_shadowClassCctorStarted.load(std::memory_order_relaxed);
        h.droppedClassObservations=s_droppedClassObservations.load(std::memory_order_relaxed);
        return h;
    };
    const ActiveSnapshot* activeBefore=s_active.load(std::memory_order_acquire);
    const serial::Header before=readHeader();
    const uint64_t droppedBefore=r02::ObservationCounters::DroppedThreads();
    const bool saturatedBefore=r02::ObservationCounters::Saturated();
    if(!capture.Begin(before,droppedBefore,saturatedBefore))return false;

    const serial::Header after=readHeader();
    const uint64_t droppedAfter=r02::ObservationCounters::DroppedThreads();
    const bool saturatedAfter=r02::ObservationCounters::Saturated();
    const ActiveSnapshot* activeAfter=s_active.load(std::memory_order_acquire);
    // Fail if active pointer is replaced, even if generation values match.
    // Equal samples do not exclude ABA, an unobserved intermediate update,
    // or any class/R02/GC/counter activity after the second sample.
    return capture.Seal(after,droppedAfter,saturatedAfter,
                        activeBefore==activeAfter);
}
