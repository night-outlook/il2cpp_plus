#include "il2cpp-config.h"
#include "AssemblyShadowRuntimeProbe.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW && HYBRIDCLR_R03_RUNTIME_PROBE
#include "AssemblyShadowTypeKey.h"

namespace il2cpp { namespace vm { namespace assembly_shadow_r03 {
namespace {
void CaptureDefinition(ProbeTypeSnapshot& identity, const void* physical)
{
    const auto* klass = static_cast<const Il2CppClass*>(physical);
    // RecordLayout is called only by the staged pair screen under the metadata
    // lock/owner scope. It has already materialized these physical definitions.
    // Make uses their declaration chain; unlike Format(&byval_arg), it does not
    // round-trip through a private image/token lookup or construct a class.
    try
    {
        if (!klass || !klass->image || !klass->image->assembly || klass->rank || klass->generic_class)
        { identity.Capture(physical, nullptr, nullptr, nullptr, nullptr); return; }
        const std::string key = AssemblyShadowTypeKey::Make(klass).ToString();
        identity.Capture(physical, klass->image->assembly->aname.name,
            klass->namespaze ? klass->namespaze : "", klass->name, key.c_str());
    }
    catch (...)
    {
        // Diagnostic unavailability is explicit. Never install a new runtime
        // error or turn an incomplete/truncated identity into a valid witness.
        identity.CaptureFailed(physical);
    }
}
}
void RuntimeProbe::RecordLayout(const LayoutRow& row)
{
    LayoutRow owned = row;
    CaptureDefinition(owned.baselineIdentity, row.baseline);
    CaptureDefinition(owned.targetIdentity, row.target);
    auto& s = Data(); std::lock_guard<std::mutex> lock(s.mutex);
    if (s.report.layouts == kLayouts) { s.report.overflow = true; return; }
    if (!owned.baselineIdentity.Complete() || !owned.targetIdentity.Complete())
        s.report.overflow = true;
    s.report.layout[s.report.layouts++] = owned;
}
}}}
#endif
