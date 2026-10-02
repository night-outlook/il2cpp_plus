#include "vm/AssemblyShadowRuntimeProbe.h"
bool OtherTranslationUnitBegin(const void* target, uint64_t generation)
{
#if HYBRIDCLR_R03_RUNTIME_PROBE
    return il2cpp::vm::assembly_shadow_r03::RuntimeProbe::Begin(target, generation);
#else
    (void)target; (void)generation; return false;
#endif
}
