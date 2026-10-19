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
struct Il2CppType;
struct MethodInfo;

namespace il2cpp { namespace vm {
namespace assembly_shadow_bounded_transaction { class Capture; }
namespace assembly_shadow_bounded_inventory { class Capture; }
namespace assembly_shadow_bounded_first_use { class Capture; }
namespace assembly_shadow_frozen_general { class Capture; }
namespace assembly_shadow_bounded_general_live { struct Sources; class Scratch; struct Receipt; }
namespace assembly_shadow_frozen_execution_record { class Record; }
namespace assembly_shadow_bounded_execution_empty_live { struct Receipt; }
namespace assembly_shadow_opaque_execution { class Scratch; struct Receipt; }

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
enum class AssemblyResolveContext { Normal, Staging, DiagnosticsPhysical };
#endif

class AssemblyShadow
{
public:
    // Runtime startup only. Begin must precede every metadata initialization
    // attempt; physical metadata replacement/reload is unsupported when ON.
    // Initialize runs once after physical assembly registration and before any
    // class/signature startup. Native-only, noexcept, and no transaction owner.
    static bool BeginStartupTrackingInitialization() noexcept;
    static bool InitializeStartupCandidates() noexcept;
    static void FailStartupTrackingInitialization() noexcept;
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
    // Capability schema 1. Reserve the whole ordered closure before any Stage.
    // Legacy transactions retain their original per-image admission contract.
    static AssemblyShadowError ReserveMetadataBudget(const std::vector<uint64_t>& sizes, int32_t profileVersion);
    static AssemblyShadowError GetMetadataCapacityJson(const std::vector<uint64_t>& sizes, std::string& json);
    static AssemblyShadowError GetRecoveryInfoJson(std::string& json);
    static AssemblyShadowError ValidateTransaction();
    static AssemblyShadowError CommitTransaction();
    static AssemblyShadowError AbortTransaction();
    static AssemblyShadowError GetState(AssemblyShadowState& state);
    static AssemblyShadowError GetAssemblyExecutionMode(const char* name, AssemblyExecutionMode& mode);
    static AssemblyShadowError GetDiagnosticsJson(std::string& json);
    static AssemblyShadowError GetTypeResolutionInfo(const Il2CppType* type, std::string& json);
    static AssemblyShadowError GetExecutionDiagnosticsJson(std::string& json);
    // Icall exception boundary: seal a mutating operation that unwound through
    // an unexpected failure. This path does not allocate or acquire VM locks.
    static AssemblyShadowError ReportUnexpectedFailure();

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    // P1 limited native status source capture. Copies only transaction status
    // fields under the existing lock and checks first-failure publication
    // afterward; it does NOT capture other diagnostics or authorize Player use.
    // Caller preconstructs and exclusively owns a healthy-time fixed Capture.
    static bool TryCaptureBoundedTransactionStatus(
        assembly_shadow_bounded_transaction::Capture& capture);
    // P1 additive inventory and status snapshot under ONE transaction mutex.
    // No metadata, first-use, class, R02 or Player validation is included.
    static bool TryCaptureBoundedTransactionInventory(
        assembly_shadow_bounded_inventory::Capture& capture);
    // P1 additive, NO Player caller: copy raw first-use records under short
    // UsageLock, then immutable candidate names after unlock. Never dereference
    // physical Il2CppClass*; rows requiring class metadata fail closed until
    // a separately reviewed metadata-lifetime name resolver exists.
    static bool TryCaptureBoundedFirstUses(
        assembly_shadow_bounded_first_use::Capture& capture);
    // P1 INACTIVE live-source orchestrator: collect preallocated bounded
    // component captures while one metadata admission ticket is held. Reject
    // observed publication/usage changes across the collection AND composition.
    // Equal fence samples are NOT a coherent all-VM epoch. No Player caller.
    static bool TryCaptureBoundedGeneralLive(
        const assembly_shadow_bounded_general_live::Sources& sources,
        assembly_shadow_bounded_general_live::Scratch& scratch,
        assembly_shadow_frozen_general::Capture& output,
        assembly_shadow_bounded_general_live::Receipt& receipt);
    // P1 INACTIVE: actual R02 live source + zero observed class subset only.
    // Nonempty classes and incomplete counters are refused before formatting.
    // This does NOT establish full execution capture, class pointer provenance,
    // VM-wide atomicity, or original Player getter acceptance.
    static bool TryCaptureBoundedEmptyExecutionLive(
        assembly_shadow_frozen_execution_record::Record& output,
        assembly_shadow_bounded_execution_empty_live::Receipt& receipt);
    // P1 INACTIVE: observable opaque class identities without metadata dereference.
    // Never returns pointers, type keys, JSON, or a VM-coherence claim.
    static bool TryCaptureOpaqueExecutionInventory(
        assembly_shadow_opaque_execution::Scratch& scratch,
        assembly_shadow_opaque_execution::Receipt& receipt);
    // Resolver hooks do not take the transaction lock. The immutable active
    // snapshot is release-published once and retained for process lifetime.
    static AssemblyResolveContext CurrentResolveContext();
    static const Il2CppAssembly* ResolveByName(const char* name, AssemblyResolveContext context);
    static const Il2CppAssembly* ResolveName(const char* name, const char* site);
    static const Il2CppAssembly* ResolveAssembly(const Il2CppAssembly* assembly);
    static const Il2CppAssembly* ResolveReferencedAssembly(const Il2CppAssembly* requester,
        const Il2CppAssembly* physicalProvider, const char* referencedName,
        int32_t referenceIndex = -1, const char* site = "AssemblyRef");
    static const Il2CppImage* ResolveImage(const Il2CppImage* image);
    // Exported image identity stays at the startup-registered baseline image.
    // Metadata queries still use ResolveImage; physical VM getters stay physical.
    static const Il2CppImage* ResolvePublicImageIdentity(const Il2CppImage* image);
    static Il2CppClass* ResolveClassDefinition(Il2CppClass* klass);
    static Il2CppClass* ResolveClass(Il2CppClass* klass);
    static const Il2CppType* ResolveType(const Il2CppType* type);
    // Reflection-only canonicalization. Managed execution continues to use
    // AssertMethodIsActive/RequireActiveMethod and never remaps a MethodInfo.
    static const MethodInfo* ResolveReflectionMethod(const MethodInfo* method);
    static Il2CppClass* ResolveAllocationClass(Il2CppClass* klass, const char* site);
    static void RequireActiveClass(Il2CppClass* klass, BaselineUseKind kind, const char* site);
    static void RecordTypeUse(const Il2CppType* type, BaselineUseKind kind, const char* site);
    // Physical metadata recursion only. Never an exemption for initialization,
    // static storage, vtable, managed reflection exposure or object allocation.
    static bool IsResolvingTypeMetadata();
    static void FailTypeResolution(AssemblyShadowError error, const std::string& detail);
    static bool IsShadowedBaseline(const Il2CppAssembly* assembly);
    static bool IsActiveShadow(const Il2CppAssembly* assembly);
    static bool IsCandidate(const Il2CppAssembly* assembly);
    static uint64_t ActiveGeneration();
    static void RecordBaselineUse(const Il2CppAssembly* assembly, BaselineUseKind kind,
        const char* detail, const Il2CppClass* klass = nullptr);
    static void RequireUserCodeAllowed();
    // Checks physical method/definition ownership, never remaps a MethodInfo.
    // The boolean boundary records/seals failure without raising a managed exception.
    static bool AssertMethodIsActive(const MethodInfo* method, const char* site) noexcept;
    static void RequireActiveMethod(const MethodInfo* method, const char* site);
    // Observation hooks never initialize classes or resolve metadata. Counters
    // have process lifetime; class observations begin when candidate identities are published.
    static void ObserveClassCctorStarted(Il2CppClass* klass);
    static void ObserveInterpreterTransformation(const MethodInfo* method);

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
