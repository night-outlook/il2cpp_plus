#pragma once

#include "il2cpp-config.h"

namespace il2cpp { namespace vm {

class AssemblyShadowStartup
{
public:
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    // Called only at the complete Runtime::Init tail, still under its init lock.
    static void MarkCoreReady() noexcept;
    // Called after Runtime::Init returns and releases that lock. No lock spans
    // the stable AOT callback. A failed gateway never permits host startup.
    static bool AfterRuntimeInit(bool initialized) noexcept;
#else
    static void MarkCoreReady() noexcept {}
    static bool AfterRuntimeInit(bool initialized) noexcept { return initialized; }
#endif
};

}}
