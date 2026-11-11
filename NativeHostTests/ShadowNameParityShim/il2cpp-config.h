#pragma once
// Host-only ABI shim used ONLY by AssemblyShadowNameReferenceParityHost.cpp.
// Prevent inclusion or copying into the Unity/IL2CPP production overlay.
#include <cstdint>
typedef uint16_t Il2CppChar;
struct Il2CppString;
#define HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW 1
#define LIBIL2CPP_CODEGEN_API
