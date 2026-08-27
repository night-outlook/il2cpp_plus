#pragma once

#include "il2cpp-config.h"
#include "AssemblyShadowTypes.h"

#include <stddef.h>
#include <string>
#include <vector>

struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppObject;

namespace il2cpp { namespace vm {

class AssemblyShadow
{
public:
    // Bootstrap-only mutation APIs. Lock order is transaction -> metadata ->
    // assembly -> usage/cache. No managed execution is permitted under these locks.
    static AssemblyShadowError ConfigureCandidates(const char* baselineBuildId,
        const std::vector<std::string>& names,
        const std::vector<std::string>& stableAotNames);
    // closureLoadOrder is the verified manifest's provider-before-consumer order,
    // including declared ModuleContract edges absent from AssemblyRef metadata.
    static AssemblyShadowError BeginTransaction(const char* patchId,
        const char* expectedBaselineBuildId,
        const std::vector<std::string>& closureLoadOrder, int32_t runtimeAbiVersion);
    static AssemblyShadowError StageAssembly(const uint8_t* dll, size_t dllLength,
        const uint8_t* pdb, size_t pdbLength);
    static AssemblyShadowError ValidateTransaction();
    static AssemblyShadowError CommitTransaction();
    static AssemblyShadowError AbortTransaction();
    static AssemblyShadowError GetState(AssemblyShadowState& state);
    static AssemblyShadowError GetAssemblyExecutionMode(const char* name, AssemblyExecutionMode& mode);
    static AssemblyShadowError GetDiagnosticsJson(std::string& json);
    // Icall exception boundary: seal a mutating operation that unwound through
    // an unexpected failure. This path does not allocate or acquire VM locks.
    static AssemblyShadowError ReportUnexpectedFailure();

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    // Resolver hooks do not take the transaction lock. The immutable active
    // snapshot is release-published once and retained for process lifetime.
    static const Il2CppAssembly* ResolveName(const char* name, const char* site);
    static const Il2CppAssembly* ResolveAssembly(const Il2CppAssembly* assembly);
    static const Il2CppImage* ResolveImage(const Il2CppImage* image);
    static bool IsShadowedBaseline(const Il2CppAssembly* assembly);
    static bool IsActiveShadow(const Il2CppAssembly* assembly);
    static bool IsCandidate(const Il2CppAssembly* assembly);
    static uint64_t ActiveGeneration();
    static void RecordBaselineUse(const Il2CppAssembly* assembly, BaselineUseKind kind,
        const char* detail, const Il2CppClass* klass = nullptr);
    static void RequireUserCodeAllowed();

    // Temporary narrow Unity query-target mapping retained from the accepted
    // feasibility proof. General type/cast/cache resolution is M05-M07 work.
    static Il2CppClass* ResolveUnityComparisonTarget(const Il2CppClass* actual, Il2CppClass* expected);
    static void TraceImage(const char* site, const Il2CppImage* image);
    static void TraceClass(const char* site, const Il2CppClass* klass);
    static void TraceTypeCheck(const char* site, const Il2CppClass* actual,
        const Il2CppClass* expected, bool checkInterfaces, bool matches);
    static void SetPhase(const char* phase);
    static std::string InspectObject(const Il2CppObject* object);
    static std::string InspectAssembly(const Il2CppAssembly* assembly);
#endif
};

}}
