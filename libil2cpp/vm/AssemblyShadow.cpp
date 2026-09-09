#include "AssemblyShadow.h"
#include "AssemblyShadowDiagnostics.h"
#include "AssemblyShadowRecovery.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadowName.h"
#include "AssemblyShadowVisibility.h"
#include "AssemblyShadowTypeResolver.h"
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "vm/Assembly.h"
#include "vm/Exception.h"
#include "vm/Image.h"
#include "vm/MetadataCache.h"
#include "vm/MetadataLock.h"
#include "os/Atomic.h"
#include "hybridclr/metadata/Assembly.h"
#include "hybridclr/metadata/AssemblyShadowBridge.h"
#include "hybridclr/metadata/MetadataUtil.h"
#include "hybridclr/metadata/InterpreterImage.h"
#include "hybridclr/metadata/StagedAssembly.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <stdexcept>
#include <array>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <limits>

namespace hybridclr {
    extern const uint32_t g_assemblyShadowStartupCandidateSchemaVersion;
    extern const char* g_assemblyShadowStartupCandidates[];
}

namespace il2cpp { namespace vm {
namespace {
    using namespace assembly_shadow_detail;
    using hybridclr::metadata::StagedAssembly;
    using InterpreterAssembly = hybridclr::metadata::Assembly;
    using StagingBridge = hybridclr::metadata::AssemblyShadowBridge;

    struct UseRecord
    {
        bool present = false;
        BaselineUseKind kind = BaselineUseKind::AssemblyReflection;
        char detail[160] = {};
        const Il2CppClass* klass = nullptr;
        uint64_t thread = 0;
        uint64_t timestamp = 0;
        uint64_t sequence = 0;
    };

    struct Candidate
    {
        std::string name;
        const Il2CppAssembly* baseline = nullptr;
        UseRecord firstUse; // Accessed only under the short usage lock.
    };

    struct CandidateRegistry
    {
        std::vector<std::unique_ptr<Candidate>> entries;
        NameIndex<Candidate*> byName;
        std::unordered_map<const Il2CppAssembly*, Candidate*> byAssembly;
        // Raw definition handles only; Configure never materializes classes.
        // Captured generic arguments cannot be identified by an AOT method's
        // declaring assembly, nor may their handles be logically remapped.
        std::unordered_map<Il2CppMetadataTypeHandle, Candidate*> byTypeHandle;
    };

    struct StableAotConfiguration
    {
        NameIndex<const Il2CppAssembly*> stableByName;
        std::vector<std::string> stableNames;
        std::vector<const Il2CppAssembly*> netstandardProviders;
    };

    struct ActiveSnapshot
    {
        uint64_t generation = 1;
        NameIndex<const Il2CppAssembly*> byName;
        std::unordered_map<const Il2CppAssembly*, const Il2CppAssembly*> byAssembly;
        std::unordered_map<const Il2CppImage*, const Il2CppImage*> byImage;
        std::unordered_map<const Il2CppAssembly*, const Il2CppAssembly*> shadowToBaseline;
    };

    struct ClosureMember
    {
        Candidate* candidate;
        StagedAssembly* staged;
    };

    struct Transaction
    {
        std::mutex mutex;
        std::thread::id owner;
        std::string baselineBuildId;
        std::string patchId;
        std::string phase;
        std::vector<ClosureMember> closure;
        std::unordered_map<Candidate*, size_t> positions;
        std::vector<StagedAssembly*> retained;
        std::vector<uint64_t> budgetSizes;
        std::vector<uint32_t> reservedImageIndices;
        bool budgetReserved = false;
        uint64_t retainedBytes = 0;
        uint64_t validatedUsageGeneration = 0;
        AssemblyShadowError lastError = AssemblyShadowError::Success;
        AssemblyShadowError recoveryTerminalError = AssemblyShadowError::Success;
        std::string recoveryTerminalDetail;
        bool recoveryBaselineRejection = false;
        std::string detail;
        std::vector<ShadowEventDiagnostic> events;
        std::vector<std::string> commitOrder;
    };

    std::atomic<CandidateRegistry*> s_candidates{nullptr};
    std::atomic<const StableAotConfiguration*> s_configuration{nullptr};
    // No heap-backed singleton or transaction is needed at metadata startup.
    // Both successful and failed attempts forbid metadata replacement/reload.
    enum class StartupAttempt { Unattempted, Initializing, Ready, Failed };
    std::atomic<StartupAttempt> s_startupAttempt{StartupAttempt::Unattempted};
    std::atomic<bool> s_earlyTracking{false};
    std::atomic<const ActiveSnapshot*> s_active{nullptr};
    std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Disabled};
    std::atomic<bool> s_lateBaselineUse{false};
    std::atomic<bool> s_unexpectedFailure{false};
    std::atomic<bool> s_referenceViolation{false};
    std::atomic<const std::string*> s_referenceViolationDetail{nullptr}; // One immutable process-lifetime failure.
    struct TypeFailure { AssemblyShadowError error; std::string detail; };
    std::atomic<const TypeFailure*> s_typeFailure{nullptr};
    std::atomic_flag s_usageLock = ATOMIC_FLAG_INIT;
    uint64_t s_usageGeneration = 0;
    std::atomic<uint64_t> s_methodChecks{0};
    std::atomic<uint64_t> s_shadowMethodChecks{0};
    std::atomic<uint64_t> s_rejectedBaselineMethods{0};
    std::atomic<uint64_t> s_baselineClassCctorStarted{0};
    std::atomic<uint64_t> s_shadowClassCctorStarted{0};
    std::atomic<uint64_t> s_interpreterTransformations{0};
    std::atomic<uint64_t> s_shadowInterpreterTransformations{0};
    std::atomic<uint64_t> s_droppedClassObservations{0};
    // Process-lifetime physical identities only. Never allocate on an execution
    // observation or acquire metadata/transaction locks from this short lock.
    const size_t kMaximumExecutionClasses = 1024;
    std::array<Il2CppClass*, kMaximumExecutionClasses * 2> s_executionClasses{};
    size_t s_executionClassCount = 0;
    std::atomic_flag s_executionObservationLock = ATOMIC_FLAG_INIT;

    struct ExecutionObservationLock
    {
        ExecutionObservationLock()
        {
            while (s_executionObservationLock.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
        }
        ~ExecutionObservationLock() { s_executionObservationLock.clear(std::memory_order_release); }
    };

    const Il2CppAssembly* PhysicalMethodAssembly(const MethodInfo* method)
    {
        return method && method->klass && method->klass->image ? method->klass->image->assembly : nullptr;
    }

    void ObserveExecutionClass(Il2CppClass* klass)
    {
        if (!klass || !klass->image || !klass->image->assembly) return;
        const CandidateRegistry* candidates = s_candidates.load(std::memory_order_acquire);
        if (!candidates) return; // Startup execution policy is a separate proof.
        const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
        if (!candidates->byAssembly.count(klass->image->assembly) &&
            !(active && active->shadowToBaseline.count(klass->image->assembly))) return;
        ExecutionObservationLock lock;
        size_t slot = (reinterpret_cast<uintptr_t>(klass) >> 3) % s_executionClasses.size();
        for (size_t probe = 0; probe < s_executionClasses.size(); ++probe)
        {
            Il2CppClass*& entry = s_executionClasses[slot];
            if (entry == klass) return;
            if (!entry)
            {
                if (s_executionClassCount == kMaximumExecutionClasses) break;
                entry = klass;
                ++s_executionClassCount;
                return;
            }
            slot = (slot + 1) % s_executionClasses.size();
        }
        s_droppedClassObservations.fetch_add(1, std::memory_order_relaxed);
    }

    void RecordFirstGuardFailure(AssemblyShadowError error, const std::string& detail)
    {
        if (s_typeFailure.load(std::memory_order_acquire)) return;
        std::unique_ptr<TypeFailure> failure(new TypeFailure{error, detail});
        const TypeFailure* expected = nullptr;
        if (s_typeFailure.compare_exchange_strong(expected, failure.get(), std::memory_order_acq_rel)) failure.release();
    }

    Transaction& Current()
    {
        // Successful snapshots and aborted interpreter metadata have process
        // lifetime. Avoid static-destructor ordering with VM shutdown.
        static Transaction* transaction = new Transaction();
        return *transaction;
    }

    void LockUsage()
    {
        while (s_usageLock.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    }
    void UnlockUsage() { s_usageLock.clear(std::memory_order_release); }
    struct UsageLock
    {
        UsageLock() { LockUsage(); }
        ~UsageLock() { UnlockUsage(); }
    };

    void CopyDetail(char* destination, size_t capacity, const char* source);

    struct CapturedArgumentFailure
    {
        AssemblyShadowError error = AssemblyShadowError::Success;
        Candidate* candidate = nullptr;
        const char* context = "";
        const char* reason = "";
        char path[512] = {};
    };

    // Runs only over already-existing, runtime-owned metadata, under UsageLock.
    // No Class::FromIl2CppType, inflation, managed exception, or cache lock is
    // permitted here: the boolean guard is also used at reverse callbacks.
    struct CapturedArgumentVisitor
    {
        const CandidateRegistry* candidates;
        const ActiveSnapshot* active;
        const char* site;
        CapturedArgumentFailure& failure;
        size_t remaining = 1024;
        char path[512] = {};

        CapturedArgumentVisitor(const CandidateRegistry* registry, const ActiveSnapshot* snapshot,
            const char* useSite, CapturedArgumentFailure& result)
            : candidates(registry), active(snapshot), site(useSite), failure(result) {}

        bool Fail(const char* reason, Candidate* candidate = nullptr)
        {
            failure.error = candidate ? AssemblyShadowError::BaselineAlreadyUsed : AssemblyShadowError::InvalidArgument;
            failure.candidate = candidate;
            failure.reason = reason;
            CopyDetail(failure.path, sizeof(failure.path), path);
            return false;
        }

        bool Child(const Il2CppType* type, const char* edge, uint32_t index, size_t depth)
        {
            size_t length = std::strlen(path);
            int added = std::snprintf(path + length, sizeof(path) - length, "/%s[%u]", edge, index);
            if (added < 0 || static_cast<size_t>(added) >= sizeof(path) - length) return Fail("PathLimit");
            bool valid = Type(type, depth);
            path[length] = 0;
            return valid;
        }

        bool Instance(const Il2CppGenericInst* instance, size_t depth)
        {
            if (!instance || !instance->type_argc || !instance->type_argv) return Fail("EmptyGenericInstance");
            if (instance->type_argc > remaining) return Fail("ArgumentLimit");
            for (uint32_t index = 0; index < instance->type_argc; ++index)
                if (!Child(instance->type_argv[index], "arg", index, depth)) return false;
            return true;
        }

        bool Type(const Il2CppType* type, size_t depth)
        {
            if (!type) return Fail("NullType");
            if (depth > 64 || !remaining) return Fail("TraversalLimit");
            --remaining;
            switch (type->type)
            {
                case IL2CPP_TYPE_CLASS:
                case IL2CPP_TYPE_VALUETYPE:
                {
                    if (!type->data.typeHandle) return Fail("NullDefinition");
                    if (!candidates) return true;
                    auto found = candidates->byTypeHandle.find(type->data.typeHandle);
                    if (found == candidates->byTypeHandle.end()) return true;
                    Candidate* candidate = found->second;
                    UseRecord& use = candidate->firstUse;
                    if (!use.present)
                    {
                        use.present = true;
                        use.kind = BaselineUseKind::MethodExecution;
                        std::snprintf(use.detail, sizeof(use.detail), "%s Context=%s Path=%s",
                            site ? site : "", failure.context, path);
                        // No class exists necessarily for a captured argument.
                        use.thread = static_cast<uint64_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
                        use.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                        use.sequence = ++s_usageGeneration;
                    }
                    return active && active->byAssembly.count(candidate->baseline) ? Fail("CapturedBaselineType", candidate) : true;
                }
                case IL2CPP_TYPE_GENERICINST:
                {
                    const Il2CppGenericClass* generic = type->data.generic_class;
                    if (!generic || !generic->type || generic->context.method_inst) return Fail("MalformedGenericClass");
                    if (generic->type->type != IL2CPP_TYPE_CLASS && generic->type->type != IL2CPP_TYPE_VALUETYPE)
                        return Fail("MalformedGenericDefinition");
                    return Child(generic->type, "definition", 0, depth + 1) && Instance(generic->context.class_inst, depth + 1);
                }
                case IL2CPP_TYPE_SZARRAY:
                case IL2CPP_TYPE_PTR:
                    return Child(type->data.type, "element", 0, depth + 1);
                case IL2CPP_TYPE_ARRAY:
                    if (!type->data.array || !type->data.array->rank ||
                        type->data.array->numsizes > type->data.array->rank ||
                        type->data.array->numlobounds > type->data.array->rank ||
                        (type->data.array->numsizes && !type->data.array->sizes) ||
                        (type->data.array->numlobounds && !type->data.array->lobounds)) return Fail("MalformedArray");
                    return Child(type->data.array->etype, "element", 0, depth + 1);
                case IL2CPP_TYPE_VAR:
                case IL2CPP_TYPE_MVAR:
                    return Fail("OpenGenericArgument");
                case IL2CPP_TYPE_BOOLEAN: case IL2CPP_TYPE_CHAR:
                case IL2CPP_TYPE_I1: case IL2CPP_TYPE_U1: case IL2CPP_TYPE_I2: case IL2CPP_TYPE_U2:
                case IL2CPP_TYPE_I4: case IL2CPP_TYPE_U4: case IL2CPP_TYPE_I8: case IL2CPP_TYPE_U8:
                case IL2CPP_TYPE_R4: case IL2CPP_TYPE_R8: case IL2CPP_TYPE_I: case IL2CPP_TYPE_U:
                case IL2CPP_TYPE_OBJECT: case IL2CPP_TYPE_STRING:
                    return true;
                default:
                    return Fail("UnsupportedCapturedType");
            }
        }

        bool Context(const Il2CppGenericContext& context, const char* classLabel, const char* methodLabel)
        {
            failure.context = classLabel;
            if (context.class_inst && !Instance(context.class_inst, 0)) return false;
            failure.context = methodLabel;
            return !context.method_inst || Instance(context.method_inst, 0);
        }
    };

    bool Interpreter(const Il2CppAssembly* assembly)
    {
        return assembly && assembly->image && hybridclr::metadata::IsInterpreterImage(assembly->image);
    }

    // Physical tables and raw TypeDef handles only; never materialize a class.
    AssemblyShadowError BuildCandidateRegistry(CandidateRegistry* registry, const std::vector<std::string>& names)
    {
        registry->byName.Reserve(names.size());
        registry->byAssembly.reserve(names.size());
        registry->entries.reserve(names.size());
        for (const auto& name : names)
        {
            std::string canonical;
            if (!CanonicalName(name.c_str(), canonical)) return AssemblyShadowError::InvalidArgument;
            if (registry->byName.Find(canonical.c_str())) return AssemblyShadowError::DuplicateAssemblyName;
            const Il2CppAssembly* baseline = MetadataCache::GetAotAssemblyByNamePhysical(canonical.c_str());
            if (!baseline) return AssemblyShadowError::BaselineAssemblyNotFound;
            if (!baseline->image || !baseline->aname.name) return AssemblyShadowError::InternalError;
            if (Interpreter(baseline)) return AssemblyShadowError::UnsupportedAssembly;
            std::unique_ptr<Candidate> candidate(new Candidate());
            candidate->name = baseline->aname.name;
            candidate->baseline = baseline;
            registry->byName.Add(candidate->name, candidate.get());
            if (!registry->byAssembly.emplace(baseline, candidate.get()).second)
                return AssemblyShadowError::DuplicateAssemblyName;
            for (uint32_t index = 0; index < baseline->image->typeCount; ++index)
            {
                Il2CppMetadataTypeHandle handle = MetadataCache::GetAssemblyTypeHandle(baseline->image, index);
                if (!handle || !registry->byTypeHandle.emplace(handle, candidate.get()).second)
                    return AssemblyShadowError::InternalError;
            }
            registry->entries.push_back(std::move(candidate));
        }
        return AssemblyShadowError::Success;
    }

    AssemblyShadowError Result(Transaction& transaction, AssemblyShadowError error, const std::string& detail = "")
    {
        const auto state = s_state.load(std::memory_order_acquire);
        const int32_t code = static_cast<int32_t>(error);
        if (transaction.recoveryTerminalError == AssemblyShadowError::Success &&
            (state == AssemblyShadowState::Failed || state == AssemblyShadowState::FailedAfterCommit ||
             error == AssemblyShadowError::InternalError || code < 0 || code > 24))
        {
            transaction.recoveryTerminalError = error == AssemblyShadowError::Success ? AssemblyShadowError::InternalError : error;
            transaction.recoveryTerminalDetail = detail;
        }
        if (error == AssemblyShadowError::BaselineAlreadyUsed && !s_active.load(std::memory_order_acquire))
            transaction.recoveryBaselineRejection = true;
        transaction.lastError = error;
        transaction.detail = detail;
        return error;
    }

    AssemblyShadowError WrongState(Transaction& transaction)
    {
        return Result(transaction, s_active.load(std::memory_order_acquire) ?
            AssemblyShadowError::AlreadyCommitted : AssemblyShadowError::InvalidState,
            "Operation is not allowed in the current transaction state.");
    }

    bool Owner(const Transaction& transaction)
    {
        return transaction.owner == std::this_thread::get_id();
    }

    size_t StagedCount(const Transaction& transaction)
    {
        size_t count = 0;
        for (const auto& member : transaction.closure) if (member.staged) ++count;
        return count;
    }

    void Event(Transaction& transaction, const char* kind, const std::string& name = "")
    {
        // Bounded evidence. Rejected/repeated API calls do not append events.
        if (transaction.events.size() >= 4096) return;
        ShadowEventDiagnostic event;
        event.sequence = transaction.events.size() + 1;
        event.kind = kind;
        event.name = name;
        const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
        event.generation = active ? active->generation : 0;
        event.stagedCount = StagedCount(transaction);
        transaction.events.push_back(event);
    }

    Candidate* UsedClosureMember(const Transaction& transaction)
    {
        // Caller holds usage lock, never transaction acquisition from a VM hook.
        for (const auto& member : transaction.closure)
            if (member.candidate->firstUse.present) return member.candidate;
        return nullptr;
    }

    const Il2CppAssembly* ResolvePrivate(const char* name, void* context)
    {
        const auto& transaction = *static_cast<Transaction*>(context);
        const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
        Candidate* candidate = registry->byName.Find(name);
        if (candidate)
        {
            auto found = transaction.positions.find(candidate);
            if (found != transaction.positions.end())
            {
                StagedAssembly* staged = transaction.closure[found->second].staged;
                return staged ? staged->assembly : nullptr; // Never baseline fallback.
            }
            return candidate->baseline; // An unchanged, registered candidate.
        }
        return s_configuration.load(std::memory_order_acquire)->stableByName.Find(name);
    }

    bool CanResolvePrivateFacade(const char* name, const CandidateRegistry* registry)
    {
        return hybridclr::metadata::Image::CanUseLogicalNetStandardFacade(name,
            registry->byName.Find(name) != nullptr,
            MetadataCache::GetAotAssemblyByNamePhysical(name) != nullptr,
            s_configuration.load(std::memory_order_acquire)->netstandardProviders.size());
    }

    bool ResolvePrivateFacade(const char* name, std::vector<const Il2CppAssembly*>& providers, void*)
    {
        const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
        if (!registry || !CanResolvePrivateFacade(name, registry)) return false;
        providers = s_configuration.load(std::memory_order_acquire)->netstandardProviders;
        return true;
    }

    std::string ReferenceDetail(const Il2CppAssembly* requester, const char* provider, int32_t index, const char* site)
    {
        const char* name = requester ? requester->aname.name : "<unknown>";
        return std::string("ShadowClosureViolation Requester=") + name + " Provider=" +
            (provider ? provider : "<unknown>") + " ReferenceIndex=" + std::to_string(index) +
            " Path=" + name + " -> " + (provider ? provider : "<unknown>") + " Site=" + site;
    }

    AssemblyShadowError CheckExternalReferences(Transaction& transaction, const CandidateRegistry* registry)
    {
        AssemblyVector physical;
        Assembly::GetAllPhysicalAssemblies(physical);
        for (const Il2CppAssembly* requester : physical)
        {
            if (Interpreter(requester)) continue;
            auto candidate = registry->byAssembly.find(requester);
            if (candidate != registry->byAssembly.end() && transaction.positions.count(candidate->second)) continue;
            for (int32_t index = 0; index < requester->referencedAssemblyCount; ++index)
            {
                const Il2CppAssembly* provider = MetadataCache::GetReferencedAssemblyPhysical(requester, index);
                Candidate* changed = provider ? registry->byName.Find(provider->aname.name) : nullptr;
                if (changed && transaction.positions.count(changed))
                    return Result(transaction, AssemblyShadowError::ReferenceEscapesClosure,
                        ReferenceDetail(requester, provider->aname.name, index, "Validate.PhysicalAotAssemblyRef"));
            }
        }
        return AssemblyShadowError::Success;
    }

    AssemblyShadowError CheckClosure(Transaction& transaction)
    {
        const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
        for (size_t index = 0; index < transaction.closure.size(); ++index)
        {
            const auto& member = transaction.closure[index];
            StagedAssembly* staged = member.staged;
            if (!staged) return Result(transaction, AssemblyShadowError::ClosureMemberMissing, member.candidate->name);
            if (!staged->skeletonBuilt || !staged->assembly || !staged->image)
                return Result(transaction, AssemblyShadowError::BadImage, member.candidate->name);
            if (!utils::VmStringUtils::CaseInsensitiveComparer()(staged->assembly->aname.name, member.candidate->baseline->aname.name))
                return Result(transaction, AssemblyShadowError::AssemblyNameMismatch, member.candidate->name);
            if (staged->published || staged->moduleInitializerAttempted.load())
                return Result(transaction, AssemblyShadowError::InvalidState, "Staging was published or executed before commit.");
            for (const auto& reference : staged->references)
            {
                Candidate* provider = registry->byName.Find(reference.c_str());
                if (provider)
                {
                    auto position = transaction.positions.find(provider);
                    if (position != transaction.positions.end())
                    {
                        if (!transaction.closure[position->second].staged)
                            return Result(transaction, AssemblyShadowError::ClosureMemberMissing, reference);
                        if (position->second >= index)
                            return Result(transaction, AssemblyShadowError::ReferenceResolutionFailed,
                                "Manifest load order is not provider-before-consumer: " + reference + " -> " + member.candidate->name);
                    }
                }
                else if (!s_configuration.load(std::memory_order_acquire)->stableByName.Find(reference.c_str()) && !CanResolvePrivateFacade(reference.c_str(), registry))
                {
                    return Result(transaction,
                        MetadataCache::GetAotAssemblyByNamePhysical(reference.c_str()) ?
                            AssemblyShadowError::ReferenceEscapesClosure : AssemblyShadowError::ReferenceResolutionFailed,
                        "Unapproved or missing stable AOT reference: " + reference);
                }
            }
        }
        return CheckExternalReferences(transaction, registry);
    }

    struct Publication
    {
        Transaction* transaction;
        ActiveSnapshot* snapshot;
        std::vector<uint32_t> imageIndices;
        Candidate* used = nullptr;
        bool runtimePublicationFailed = false;
    };

    bool TryBeginPublication(void* context)
    {
        auto& publication = *static_cast<Publication*>(context);
        // Called with transaction -> metadata -> assembly locks held. No hook
        // takes those locks after taking usage, so there is no reverse edge.
        LockUsage();
        publication.used = UsedClosureMember(*publication.transaction);
        if (publication.used) { UnlockUsage(); return false; }
        return true; // Keep the short lock through the release-store below.
    }

    bool PublishActive(void* context)
    {
        auto& publication = *static_cast<Publication*>(context);
        if (!InterpreterAssembly::PublishStagedImagesBatch(publication.imageIndices))
        {
            publication.runtimePublicationFailed = true;
            UnlockUsage();
            return false;
        }
        for (const auto& member : publication.transaction->closure)
            member.staged->published = true;
        s_active.store(publication.snapshot, std::memory_order_release);
        UnlockUsage();
        return true;
    }

    void CopyDetail(char* destination, size_t capacity, const char* source)
    {
        if (!source) source = "";
        size_t length = strlen(source);
        if (length >= capacity)
        {
            length = capacity - 1;
            while (length && (static_cast<unsigned char>(source[length]) & 0xc0) == 0x80) --length;
        }
        memcpy(destination, source, length);
        destination[length] = '\0';
    }

    void FailReference(const Il2CppAssembly* requester, const char* provider, int32_t index, const char* site)
    {
        std::string detail = ReferenceDetail(requester, provider, index, site);
        // One process-lifetime first failure across reference and type guards.
        // A later guard must not replace the diagnosis that sealed this process.
        RecordFirstGuardFailure(AssemblyShadowError::ReferenceEscapesClosure, detail);
        // Allocate only on failure, outside VM/usage locks. Retain one complete
        // record, not a per-lookup event stream or a truncated dependency path.
        std::unique_ptr<std::string> retained(new std::string(detail));
        const std::string* expected = nullptr;
        if (s_referenceViolationDetail.compare_exchange_strong(expected, retained.get(), std::memory_order_acq_rel))
            retained.release();
        s_referenceViolation.store(true, std::memory_order_release);
        s_state.store(AssemblyShadowState::FailedAfterCommit, std::memory_order_release);
        // The sealed state and first-failure detail survive a managed catch.
        Exception::Raise(Exception::GetInvalidOperationException(detail.c_str()));
    }
}

bool AssemblyShadow::BeginStartupTrackingInitialization() noexcept
{
    StartupAttempt expected = StartupAttempt::Unattempted;
    if (s_startupAttempt.compare_exchange_strong(expected, StartupAttempt::Initializing, std::memory_order_acq_rel)) return true;
    // A process-lifetime registry cannot survive replacement of physical tables.
    FailStartupTrackingInitialization();
    return false;
}

void AssemblyShadow::FailStartupTrackingInitialization() noexcept
{
    s_startupAttempt.store(StartupAttempt::Failed, std::memory_order_release);
    s_unexpectedFailure.store(true, std::memory_order_release);
    s_state.store(s_active.load(std::memory_order_acquire) ? AssemblyShadowState::FailedAfterCommit :
        AssemblyShadowState::Failed, std::memory_order_release);
}

bool AssemblyShadow::InitializeStartupCandidates() noexcept
{
    if (s_startupAttempt.load(std::memory_order_acquire) != StartupAttempt::Initializing)
    {
        FailStartupTrackingInitialization();
        return false;
    }
    try
    {
        if (hybridclr::g_assemblyShadowStartupCandidateSchemaVersion != 1)
        {
            FailStartupTrackingInitialization();
            return false;
        }
        std::vector<std::string> names;
        for (const char** name = hybridclr::g_assemblyShadowStartupCandidates; *name; ++name) names.emplace_back(*name);
        if (!names.empty())
        {
            std::unique_ptr<CandidateRegistry> registry(new CandidateRegistry());
            if (BuildCandidateRegistry(registry.get(), names) != AssemblyShadowError::Success)
            {
                FailStartupTrackingInitialization();
                return false;
            }
            s_candidates.store(registry.release(), std::memory_order_release);
            s_earlyTracking.store(true, std::memory_order_release);
        }
        StartupAttempt expected = StartupAttempt::Initializing;
        if (!s_startupAttempt.compare_exchange_strong(expected, StartupAttempt::Ready, std::memory_order_acq_rel))
        {
            FailStartupTrackingInitialization();
            return false;
        }
        return true;
    }
    catch (...)
    {
        FailStartupTrackingInitialization();
        return false;
    }
}

AssemblyShadowError AssemblyShadow::ConfigureCandidates(const char* baselineBuildId,
    const std::vector<std::string>& names, const std::vector<std::string>& stableAotNames)
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    if (s_state.load() != AssemblyShadowState::Disabled || s_configuration.load()) return WrongState(transaction);
    if (!baselineBuildId || !*baselineBuildId || names.empty())
        return Result(transaction, AssemblyShadowError::InvalidArgument, "A baseline ID and candidates are required.");
    CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
    std::unique_ptr<CandidateRegistry> legacyRegistry;
    if (registry)
    {
        // Compare normalized sets without replacing identities or their records.
        NameIndex<bool> provided;
        provided.Reserve(names.size());
        for (const auto& name : names)
        {
            std::string canonical;
            if (!CanonicalName(name.c_str(), canonical)) return Result(transaction, AssemblyShadowError::InvalidArgument, name);
            if (provided.Find(canonical.c_str())) return Result(transaction, AssemblyShadowError::DuplicateAssemblyName, name);
            provided.Add(canonical, true);
            if (!registry->byName.Find(canonical.c_str()))
                return Result(transaction, AssemblyShadowError::CandidateNotRegistered, name);
        }
        if (names.size() != registry->entries.size())
            return Result(transaction, AssemblyShadowError::InvalidArgument, "Configure must match the embedded candidate set.");
    }
    else
    {
        // Empty generated lists retain the legacy ConfigureOnly contract.
        if (hybridclr::g_assemblyShadowStartupCandidates[0])
            return Result(transaction, AssemblyShadowError::InvalidState, "Embedded startup candidates have not been initialized.");
        legacyRegistry.reset(new CandidateRegistry());
        registry = legacyRegistry.get();
        AssemblyShadowError error = BuildCandidateRegistry(registry, names);
        if (error != AssemblyShadowError::Success) return Result(transaction, error, "Candidate physical identity validation failed.");
    }
    std::unique_ptr<StableAotConfiguration> configuration(new StableAotConfiguration());
    configuration->stableByName.Reserve(stableAotNames.size());
    for (const auto& name : stableAotNames)
    {
        std::string canonical;
        if (!CanonicalName(name.c_str(), canonical)) return Result(transaction, AssemblyShadowError::InvalidArgument, name);
        if (registry->byName.Find(canonical.c_str()) || configuration->stableByName.Find(canonical.c_str()))
            return Result(transaction, AssemblyShadowError::DuplicateAssemblyName, name);
        const Il2CppAssembly* assembly = MetadataCache::GetAotAssemblyByNamePhysical(canonical.c_str());
        if (!assembly) return Result(transaction, AssemblyShadowError::BaselineAssemblyNotFound, name);
        if (Interpreter(assembly)) return Result(transaction, AssemblyShadowError::UnsupportedAssembly, name);
        configuration->stableByName.Add(assembly->aname.name, assembly);
        configuration->stableNames.push_back(assembly->aname.name);
    }
    // Facade providers are the intersection of upstream's finite framework
    // provider list and explicitly approved physical stable AOT assemblies.
    // Candidates cannot enter stableByName, even when named after a provider.
    for (const char* const* name = hybridclr::metadata::Image::GetNetStandardProviderNames(); *name; ++name)
        if (const Il2CppAssembly* provider = configuration->stableByName.Find(*name))
            configuration->netstandardProviders.push_back(provider);
    // Complete allocations before committing ownership/configuration.
    std::string buildId(baselineBuildId);
    Event(transaction, "candidates-registered");
    transaction.baselineBuildId.swap(buildId);
    transaction.owner = std::this_thread::get_id();
    if (legacyRegistry) s_candidates.store(legacyRegistry.release(), std::memory_order_release);
    s_configuration.store(configuration.release(), std::memory_order_release);
    s_state.store(AssemblyShadowState::CandidatesRegistered, std::memory_order_release);
    return Result(transaction, AssemblyShadowError::Success);
}

AssemblyShadowError AssemblyShadow::BeginTransaction(const char* patchId,
    const char* expectedBaselineBuildId, const std::vector<std::string>& closureLoadOrder, int32_t runtimeAbiVersion)
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    if (s_state.load() != AssemblyShadowState::CandidatesRegistered || !Owner(transaction)) return WrongState(transaction);
    if (!patchId || !*patchId || !expectedBaselineBuildId || closureLoadOrder.empty())
        return Result(transaction, AssemblyShadowError::InvalidArgument, "Patch ID, baseline ID and closure are required.");
    if (transaction.baselineBuildId != expectedBaselineBuildId)
        return Result(transaction, AssemblyShadowError::BaselineBuildMismatch, "Expected baseline build ID differs from the configured Player.");
    if (runtimeAbiVersion != kAssemblyShadowRuntimeAbiVersion)
        return Result(transaction, AssemblyShadowError::RuntimeAbiMismatch, "Native runtime ABI version differs.");
    const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
    std::vector<ClosureMember> members;
    std::unordered_map<Candidate*, size_t> positions;
    for (const auto& name : closureLoadOrder)
    {
        Candidate* candidate = registry->byName.Find(name.c_str());
        if (!candidate) return Result(transaction, AssemblyShadowError::CandidateNotRegistered, name);
        if (positions.count(candidate)) return Result(transaction, AssemblyShadowError::DuplicateAssemblyName, name);
        positions.emplace(candidate, members.size());
        members.push_back({candidate, nullptr});
    }
    transaction.patchId = patchId;
    transaction.closure.swap(members);
    transaction.positions.swap(positions);
    transaction.retained.reserve(transaction.closure.size() + 1);
    s_state.store(AssemblyShadowState::Staging, std::memory_order_release);
    Event(transaction, "transaction-begun");
    return Result(transaction, AssemblyShadowError::Success);
}

AssemblyShadowError AssemblyShadow::StageAssembly(const uint8_t* dll, size_t dllLength,
    const uint8_t* pdb, size_t pdbLength)
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    AssemblyShadowState state = s_state.load();
    if ((state != AssemblyShadowState::Staging && state != AssemblyShadowState::Staged) || !Owner(transaction))
        return WrongState(transaction);
    if (!dll || !dllLength || (!pdb && pdbLength))
        return Result(transaction, AssemblyShadowError::InvalidArgument, "DLL bytes are required; PDB pointer and size must agree.");
    std::string name, detail;
    AssemblyShadowError error = InterpreterAssembly::ReadStagedAssemblyIdentity(dll, dllLength, name, detail);
    if (error != AssemblyShadowError::Success)
    {
        if (error == AssemblyShadowError::InternalError) s_state.store(AssemblyShadowState::Failed, std::memory_order_release);
        return Result(transaction, error, detail);
    }
    Candidate* candidate = s_candidates.load(std::memory_order_acquire)->byName.Find(name.c_str());
    auto position = transaction.positions.find(candidate);
    if (!candidate || position == transaction.positions.end())
        return Result(transaction, AssemblyShadowError::UnexpectedClosureMember, name);
    if (!utils::VmStringUtils::CaseInsensitiveComparer()(name.c_str(), candidate->baseline->aname.name))
        return Result(transaction, AssemblyShadowError::AssemblyNameMismatch, name);
    auto& member = transaction.closure[position->second];
    if (member.staged) return Result(transaction, AssemblyShadowError::DuplicateAssemblyName, name);
    StagedAssembly* staged = nullptr;
    if (transaction.budgetReserved && transaction.budgetSizes[position->second] != dllLength)
        return Result(transaction, AssemblyShadowError::MetadataBudgetMismatch, "DLL size differs from the reserved closure input.");
    error = InterpreterAssembly::CreateStagedSkeleton(dll, dllLength, pdb, pdbLength, staged, detail,
        transaction.budgetReserved ? transaction.reservedImageIndices[position->second] : 0);
    if (staged)
    {
        transaction.retained.push_back(staged);
        transaction.retainedBytes += staged->dllSize + staged->pdbSize;
    }
    if (error != AssemblyShadowError::Success)
    {
        // Invalid supplied symbols are checked before creating a private owner.
        // Correctable input rejection is distinct from a retained/unknown failure.
        if (staged || error == AssemblyShadowError::InternalError)
            s_state.store(AssemblyShadowState::Failed, std::memory_order_release);
        return Result(transaction, error, detail);
    }
    // Register private identity before runtime metadata can populate shared
    // generic/array caches. Public class walkers must not expose those entries.
    AssemblyShadowVisibility::RegisterPrivateImage(staged->image);
    member.staged = staged;
    Event(transaction, "skeleton-created", candidate->name);
    if (StagedCount(transaction) == transaction.closure.size()) s_state.store(AssemblyShadowState::Staged, std::memory_order_release);
    return Result(transaction, AssemblyShadowError::Success);
}

AssemblyShadowError AssemblyShadow::ReserveMetadataBudget(const std::vector<uint64_t>& sizes, int32_t profileVersion)
{
    using Admission = hybridclr::metadata::InterpreterImageAdmission;
    using IndexRuntime = hybridclr::metadata::InterpreterMetadataIndexRuntime;
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    if (s_state.load() != AssemblyShadowState::Staging || !Owner(transaction) ||
        StagedCount(transaction) != 0 || transaction.budgetReserved)
        return WrongState(transaction);
    if (profileVersion != Admission::kProfileVersion)
        return Result(transaction, AssemblyShadowError::CapabilityUnavailable, "Metadata encoding profile is unsupported.");
    if (sizes.size() != transaction.closure.size() || sizes.empty())
        return Result(transaction, AssemblyShadowError::MetadataBudgetMismatch, "One exact DLL size per ordered closure member is required.");
    // Prepare all transaction storage before committing global monotonic cursors.
    std::vector<uint64_t> capturedSizes(sizes);
    std::vector<uint32_t> indices;
    IndexRuntime::Error runtimeError = IndexRuntime::Error::None;
    const auto report = hybridclr::metadata::InterpreterImage::ReserveImageBudget(sizes, indices, runtimeError);
    if (!report.IsSuccess() || runtimeError != IndexRuntime::Error::None)
    {
        const size_t index = report.firstFailureIndex;
        return Result(transaction, AssemblyShadowError::MetadataCapacityExceeded,
            "Metadata capacity rejected before Stage: " +
            (index < transaction.closure.size() ? transaction.closure[index].candidate->name : std::string("invalid budget state")));
    }
    transaction.budgetSizes.swap(capturedSizes);
    transaction.reservedImageIndices.swap(indices);
    transaction.budgetReserved = true;
    Event(transaction, "metadata-budget-reserved");
    return Result(transaction, AssemblyShadowError::Success);
}

AssemblyShadowError AssemblyShadow::GetMetadataCapacityJson(const std::vector<uint64_t>& sizes, std::string& json)
{
    using Admission = hybridclr::metadata::InterpreterImageAdmission;
    using IndexRuntime = hybridclr::metadata::InterpreterMetadataIndexRuntime;
    using Codec = IndexRuntime::Codec;
    Codec::Stats stats{};
    uint64_t ordinary = 0;
    uint64_t shadow = 0;
    uint64_t reserved = 0;
    if (hybridclr::metadata::InterpreterImage::GetMetadataCapacitySnapshot(
        stats, ordinary, shadow, reserved) != IndexRuntime::Error::None)
    {
        json.clear();
        return AssemblyShadowError::InternalError;
    }
    const auto report = Admission::Evaluate(stats.reservationCount,
        sizes.empty() ? nullptr : sizes.data(), sizes.size());
    uint64_t aggregateInputBytes = 0;
    for (uint64_t size : sizes)
        aggregateInputBytes = size > std::numeric_limits<uint64_t>::max() - aggregateInputBytes
            ? std::numeric_limits<uint64_t>::max() : aggregateInputBytes + size;
    std::ostringstream out;
    const char* failure = "None";
    switch (report.error)
    {
    case Admission::Error::None: break;
    case Admission::Error::EmptyDll: failure = "EmptyDll"; break;
    case Admission::Error::DllTooLarge: failure = "DllTooLarge"; break;
    case Admission::Error::ImageLimit: failure = "ImageLimit"; break;
    case Admission::Error::InvalidInput: failure = "InvalidInput"; break;
    default: failure = "InvalidState"; break;
    }
    out << "{\"schemaVersion\":2,\"enabled\":true,\"profileVersion\":" << Admission::kProfileVersion
        << ",\"maximumImageCount\":" << Admission::kMaximumImages
        << ",\"maximumDllBytes\":" << Admission::kMaximumDllBytes
        << ",\"usablePageCapacity\":" << Codec::kUsablePageCount
        << ",\"chargedPageCeiling\":" << Codec::kMaxChargedPages
        << ",\"minimumFreePageMargin\":" << (Codec::kUsablePageCount - Codec::kMaxChargedPages)
        << ",\"reservedPages\":" << stats.reservedPages
        << ",\"mappedPages\":" << stats.mappedPages
        << ",\"lifetimeReservedImageCount\":" << stats.reservationCount
        << ",\"remainingImageCount\":" << (Admission::kMaximumImages - stats.reservationCount)
        << ",\"requiredImages\":" << sizes.size()
        // Admission is all-or-nothing. firstFailingIndex identifies the
        // rejected input, but no prefix is accepted when any member fails.
        << ",\"acceptedImages\":" << (report.IsSuccess() ? sizes.size() : 0)
        << ",\"firstFailingIndex\":" << (report.IsSuccess() ? -1 : static_cast<int64_t>(report.firstFailureIndex))
        << ",\"firstFailingSize\":" << (report.IsSuccess() || report.firstFailureIndex >= sizes.size() ? 0 : sizes[report.firstFailureIndex])
        << ",\"failureReason\":" << AssemblyShadowDiagnostics::Quote(failure)
        << ",\"fitsPreliminary\":" << (report.IsSuccess() ? "true" : "false")
        << ",\"runtimeFinalizationRequired\":true"
        << ",\"aggregateInputDllBytes\":" << aggregateInputBytes
        << ",\"aggregateInputDllBytesInformational\":true"
        << ",\"ordinaryAllocatedCount\":" << ordinary
        << ",\"shadowAllocatedCount\":" << shadow
        << ",\"reservedShadowImageCount\":" << reserved << '}';
    json = out.str();
    return AssemblyShadowError::Success;
}

AssemblyShadowError AssemblyShadow::GetRecoveryInfoJson(std::string& json)
{
    using namespace assembly_shadow_recovery;
    static_assert(static_cast<int32_t>(AssemblyShadowState::Disabled) == StateDisabled, "Recovery Disabled ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::CandidatesRegistered) == StateCandidatesRegistered, "Recovery CandidatesRegistered ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Staging) == StateStaging, "Recovery Staging ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Staged) == StateStaged, "Recovery Staged ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Validated) == StateValidated, "Recovery Validated ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Committing) == StateCommitting, "Recovery Committing ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Committed) == StateCommitted, "Recovery Committed ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Aborted) == StateAborted, "Recovery Aborted ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::Failed) == StateFailed, "Recovery Failed ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowState::FailedAfterCommit) == StateFailedAfterCommit, "Recovery FailedAfterCommit ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::Success) == ErrorSuccess, "Recovery Success ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::FeatureDisabled) == ErrorFeatureDisabled, "Recovery FeatureDisabled ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::InvalidState) == ErrorInvalidState, "Recovery InvalidState ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::InvalidArgument) == ErrorInvalidArgument, "Recovery InvalidArgument ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::CandidateNotRegistered) == ErrorCandidateNotRegistered, "Recovery CandidateNotRegistered ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::DuplicateAssemblyName) == ErrorDuplicateAssemblyName, "Recovery DuplicateAssemblyName ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::BaselineAssemblyNotFound) == ErrorBaselineAssemblyNotFound, "Recovery BaselineAssemblyNotFound ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::BaselineBuildMismatch) == ErrorBaselineBuildMismatch, "Recovery BaselineBuildMismatch ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::AssemblyNameMismatch) == ErrorAssemblyNameMismatch, "Recovery AssemblyNameMismatch ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::BadImage) == ErrorBadImage, "Recovery BadImage ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::UnsupportedAssembly) == ErrorUnsupportedAssembly, "Recovery UnsupportedAssembly ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::ClosureMemberMissing) == ErrorClosureMemberMissing, "Recovery ClosureMemberMissing ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::UnexpectedClosureMember) == ErrorUnexpectedClosureMember, "Recovery UnexpectedClosureMember ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::ReferenceResolutionFailed) == ErrorReferenceResolutionFailed, "Recovery ReferenceResolutionFailed ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::ReferenceEscapesClosure) == ErrorReferenceEscapesClosure, "Recovery ReferenceEscapesClosure ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::BaselineAlreadyUsed) == ErrorBaselineAlreadyUsed, "Recovery BaselineAlreadyUsed ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::ResourceAbiMismatch) == ErrorResourceAbiMismatch, "Recovery ResourceAbiMismatch ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::RuntimeAbiMismatch) == ErrorRuntimeAbiMismatch, "Recovery RuntimeAbiMismatch ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::AlreadyCommitted) == ErrorAlreadyCommitted, "Recovery AlreadyCommitted ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::ModuleInitializerFailed) == ErrorModuleInitializerFailed, "Recovery ModuleInitializerFailed ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::InternalError) == ErrorInternal, "Recovery InternalError ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::BaselineMethodExecution) == ErrorBaselineMethodExecution, "Recovery BaselineMethodExecution ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::CapabilityUnavailable) == ErrorCapabilityUnavailable, "Recovery CapabilityUnavailable ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::MetadataCapacityExceeded) == ErrorCapacityExceeded, "Recovery MetadataCapacityExceeded ABI changed");
    static_assert(static_cast<int32_t>(AssemblyShadowError::MetadataBudgetMismatch) == ErrorBudgetMismatch, "Recovery MetadataBudgetMismatch ABI changed");
    auto& transaction = Current();
    RecoveryInput input{};
    AssemblyShadowError terminal = AssemblyShadowError::Success;
    std::string reason;
    uint64_t retainedBytes;
    {
        std::lock_guard<std::mutex> lock(transaction.mutex);
        input.state = static_cast<int32_t>(s_state.load(std::memory_order_acquire));
        input.published = s_active.load(std::memory_order_acquire) != nullptr;
        input.knownBaselineUseRejection = transaction.recoveryBaselineRejection;
        input.lastError = static_cast<int32_t>(transaction.lastError);
        terminal = transaction.recoveryTerminalError;
        reason = terminal == AssemblyShadowError::Success ? transaction.detail : transaction.recoveryTerminalDetail;
        retainedBytes = transaction.retainedBytes;
    }
    // Durable lock-free failure facts dominate later mutable API results.
    const TypeFailure* guard = s_typeFailure.load(std::memory_order_acquire);
    const bool unexpected = s_unexpectedFailure.load(std::memory_order_acquire);
    const bool lateUse = s_lateBaselineUse.load(std::memory_order_acquire);
    const bool reference = s_referenceViolation.load(std::memory_order_acquire);
    input.poisoned = guard || unexpected || lateUse || reference;
    if (guard) { terminal = guard->error; reason = guard->detail; }
    else if (unexpected) { terminal = AssemblyShadowError::InternalError; reason = "Unexpected native mutation failure requires restart."; }
    else if (lateUse) { terminal = AssemblyShadowError::BaselineAlreadyUsed; reason = "Physical baseline use after publication requires restart."; }
    else if (reference) { terminal = AssemblyShadowError::ReferenceEscapesClosure; reason = "Reference guard failure requires restart."; }
    if (terminal == AssemblyShadowError::Success &&
        (input.state == StateFailed || input.state == StateFailedAfterCommit))
        terminal = AssemblyShadowError::InternalError;
    input.terminalFailure = terminal != AssemblyShadowError::Success;
    const auto decision = Classify(input);
    const char* names[] = {"RestartRequired", "CorrectInputOrAbort", "AbortRequired", "BaselineEligibleAfterAbort", "ActiveShadow", "BaselineUnselected"};
    std::ostringstream out;
    out << std::boolalpha << "{\"schemaVersion\":1,\"enabled\":true,\"capabilityVersion\":1,\"stateCode\":" << input.state
        << ",\"state\":" << AssemblyShadowDiagnostics::Quote(AssemblyShadowDiagnostics::StateName(static_cast<AssemblyShadowState>(input.state)))
        << ",\"published\":" << input.published << ",\"abortAllowed\":" << decision.abortAllowed
        << ",\"dispositionCode\":" << static_cast<int32_t>(decision.disposition)
        << ",\"disposition\":" << AssemblyShadowDiagnostics::Quote(names[static_cast<int32_t>(decision.disposition)])
        << ",\"terminalFailureCode\":" << static_cast<int32_t>(terminal)
        << ",\"reason\":" << AssemblyShadowDiagnostics::Quote(reason)
        << ",\"retainedBytes\":" << retainedBytes
        << ",\"baselineEligibilityRequiresStartupValidation\":" << decision.requireStartupValidation << '}';
    json = out.str();
    return AssemblyShadowError::Success;
}

AssemblyShadowError AssemblyShadow::ValidateTransaction()
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    AssemblyShadowState state = s_state.load();
    if ((state != AssemblyShadowState::Staging && state != AssemblyShadowState::Staged) || !Owner(transaction))
        return WrongState(transaction);
    AssemblyShadowError error = CheckClosure(transaction);
    if (error != AssemblyShadowError::Success) return error;
    Candidate* used;
    {
        UsageLock usage;
        used = UsedClosureMember(transaction);
    }
    if (used) return Result(transaction, AssemblyShadowError::BaselineAlreadyUsed, used->name);
    std::vector<StagedAssembly*> images;
    for (const auto& member : transaction.closure) images.push_back(member.staged);
    {
        hybridclr::metadata::ScopedStagingResolver scope(images, ResolvePrivate, &transaction, ResolvePrivateFacade);
        for (const auto& member : transaction.closure)
        {
            Event(transaction, "metadata-begin", member.candidate->name);
            std::string detail;
            error = InterpreterAssembly::InitializeStagedRuntimeMetadata(member.staged, detail);
            if (error != AssemblyShadowError::Success)
            {
                s_state.store(AssemblyShadowState::Failed, std::memory_order_release);
                return Result(transaction, error, detail);
            }
            Event(transaction, "metadata-ready", member.candidate->name);
        }
    }
    {
        UsageLock usage;
        used = UsedClosureMember(transaction);
        transaction.validatedUsageGeneration = s_usageGeneration;
    }
    if (used) return Result(transaction, AssemblyShadowError::BaselineAlreadyUsed, used->name);
    s_state.store(AssemblyShadowState::Validated, std::memory_order_release);
    Event(transaction, "transaction-validated");
    return Result(transaction, AssemblyShadowError::Success);
}

AssemblyShadowError AssemblyShadow::CommitTransaction()
{
    auto& transaction = Current();
    std::unique_lock<std::mutex> lock(transaction.mutex);
    if (s_state.load() != AssemblyShadowState::Validated || !Owner(transaction)) return WrongState(transaction);
    AssemblyShadowError error = CheckClosure(transaction);
    if (error != AssemblyShadowError::Success) return error;
    std::unique_ptr<ActiveSnapshot> snapshot(new ActiveSnapshot());
    snapshot->byName.Reserve(transaction.closure.size());
    snapshot->byAssembly.reserve(transaction.closure.size());
    snapshot->byImage.reserve(transaction.closure.size());
    snapshot->shadowToBaseline.reserve(transaction.closure.size());
    std::vector<Il2CppAssembly*> assemblies;
    assemblies.reserve(transaction.closure.size());
    for (const auto& member : transaction.closure)
    {
        if (!member.staged->runtimeMetadataInitialized)
            return Result(transaction, AssemblyShadowError::InvalidState, "Runtime metadata was not initialized.");
        const Il2CppAssembly* baseline = member.candidate->baseline;
        Il2CppAssembly* shadow = member.staged->assembly;
        snapshot->byName.Add(member.candidate->name, shadow);
        snapshot->byAssembly.emplace(baseline, shadow);
        snapshot->byImage.emplace(baseline->image, shadow->image);
        snapshot->shadowToBaseline.emplace(shadow, baseline);
        assemblies.push_back(shadow);
    }
    Publication publication;
    publication.transaction = &transaction;
    publication.snapshot = snapshot.get();
    publication.imageIndices.reserve(transaction.closure.size());
    for (const auto& member : transaction.closure)
        publication.imageIndices.push_back(member.staged->interpreterImage
            ? member.staged->interpreterImage->GetIndex() : 0);
    s_state.store(AssemblyShadowState::Committing, std::memory_order_release);
    if (!MetadataCache::PublishInterpreterAssembliesBatch(assemblies, TryBeginPublication, PublishActive, &publication))
    {
        s_state.store(AssemblyShadowState::Validated, std::memory_order_release);
        return publication.runtimePublicationFailed
            ? Result(transaction, AssemblyShadowError::InternalError, "Sparse metadata publication failed before activation.")
            : Result(transaction, AssemblyShadowError::BaselineAlreadyUsed,
                publication.used ? publication.used->name : "Candidate use changed before publication.");
    }
    snapshot.release(); // Immutable active mapping is process-lifetime.
    Event(transaction, "active-published");
    // This is the managed-execution boundary. Do not move initializers above it.
    lock.unlock();
    for (const auto& member : transaction.closure)
    {
        if (s_lateBaselineUse.load(std::memory_order_acquire) || s_referenceViolation.load(std::memory_order_acquire) ||
            s_typeFailure.load(std::memory_order_acquire))
        {
            lock.lock();
            s_state.store(AssemblyShadowState::FailedAfterCommit, std::memory_order_release);
            const TypeFailure* failure = s_typeFailure.load(std::memory_order_acquire);
            return Result(transaction, failure ? failure->error : (s_referenceViolation.load() ?
                AssemblyShadowError::ReferenceEscapesClosure : AssemblyShadowError::BaselineAlreadyUsed),
                failure ? failure->detail : "A guarded failure raced activation; restart is required.");
        }
        {
            std::lock_guard<std::mutex> record(transaction.mutex);
            Event(transaction, "initializer-begin", member.candidate->name);
        }
        std::string detail;
        error = InterpreterAssembly::RunStagedModuleInitializer(member.staged, detail);
        {
            std::lock_guard<std::mutex> record(transaction.mutex);
            if (error != AssemblyShadowError::Success)
            {
                s_state.store(AssemblyShadowState::FailedAfterCommit, std::memory_order_release);
                Event(transaction, "initializer-failed", member.candidate->name);
                const TypeFailure* failure = s_typeFailure.load(std::memory_order_acquire);
                return Result(transaction, failure ? failure->error : (s_referenceViolation.load(std::memory_order_acquire) ?
                    AssemblyShadowError::ReferenceEscapesClosure : AssemblyShadowError::ModuleInitializerFailed),
                    failure ? failure->detail : detail);
            }
            transaction.commitOrder.push_back(member.candidate->name);
            Event(transaction, "initializer-complete", member.candidate->name);
        }
    }
    lock.lock();
    AssemblyShadowState expected = AssemblyShadowState::Committing;
    if (!s_state.compare_exchange_strong(expected, AssemblyShadowState::Committed, std::memory_order_acq_rel))
    {
        const TypeFailure* failure = s_typeFailure.load(std::memory_order_acquire);
        return Result(transaction, failure ? failure->error : (s_referenceViolation.load(std::memory_order_acquire) ?
            AssemblyShadowError::ReferenceEscapesClosure : AssemblyShadowError::BaselineAlreadyUsed),
            failure ? failure->detail : "A guarded failure raced initializers; restart is required.");
    }
    Event(transaction, "transaction-committed");
    return Result(transaction, AssemblyShadowError::Success);
}

AssemblyShadowError AssemblyShadow::AbortTransaction()
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    AssemblyShadowState state = s_state.load();
    if ((state != AssemblyShadowState::Staging && state != AssemblyShadowState::Staged && state != AssemblyShadowState::Validated) || !Owner(transaction))
        return WrongState(transaction);
    if (transaction.budgetReserved)
    {
        for (uint32_t imageIndex : transaction.reservedImageIndices)
            if (hybridclr::metadata::InterpreterImage::AbortImage(imageIndex) !=
                hybridclr::metadata::InterpreterMetadataIndexRuntime::Error::None)
            {
                s_state.store(AssemblyShadowState::Failed, std::memory_order_release);
                return Result(transaction, AssemblyShadowError::InternalError,
                    "Failed to seal a reserved sparse metadata identity during abort.");
            }
    }
    else
    {
        for (const auto& member : transaction.closure)
            if (member.staged && member.staged->interpreterImage &&
                hybridclr::metadata::InterpreterImage::AbortImage(
                member.staged->interpreterImage->GetIndex()) !=
                hybridclr::metadata::InterpreterMetadataIndexRuntime::Error::None)
            {
                s_state.store(AssemblyShadowState::Failed, std::memory_order_release);
                return Result(transaction, AssemblyShadowError::InternalError,
                    "Failed to seal a staged sparse metadata identity during abort.");
            }
    }
    // Metadata does not have a proven destructor. Retain privately and seal this
    // process; never reset image indices or advertise this as an unload/retry.
    s_state.store(AssemblyShadowState::Aborted, std::memory_order_release);
    Event(transaction, "transaction-aborted");
    return Result(transaction, AssemblyShadowError::Success, "Private metadata retained; a second transaction is forbidden.");
}

AssemblyShadowError AssemblyShadow::GetState(AssemblyShadowState& state)
{
    state = s_state.load(std::memory_order_acquire);
    return AssemblyShadowError::Success;
}

AssemblyShadowError AssemblyShadow::GetAssemblyExecutionMode(const char* name, AssemblyExecutionMode& mode)
{
    mode = AssemblyExecutionMode::AotBaseline;
    const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
    if (!name || !*name) return AssemblyShadowError::InvalidArgument;
    if (!s_configuration.load(std::memory_order_acquire) || !registry || !registry->byName.Find(name))
        return AssemblyShadowError::CandidateNotRegistered;
    if (const ActiveSnapshot* active = s_active.load(std::memory_order_acquire))
        if (active->byName.Find(name)) mode = AssemblyExecutionMode::InterpreterShadow;
    return AssemblyShadowError::Success;
}

AssemblyShadowError AssemblyShadow::GetDiagnosticsJson(std::string& json)
{
    ShadowDiagnosticSnapshot snapshot;
    snapshot.enabled = true;
    snapshot.startupCandidateSchemaVersion = hybridclr::g_assemblyShadowStartupCandidateSchemaVersion;
    snapshot.startupObservationMode = s_earlyTracking.load(std::memory_order_acquire) ? "EarlyTracking" : "ConfigureOnly";
    for (const char** name = hybridclr::g_assemblyShadowStartupCandidates; *name; ++name)
        snapshot.startupCandidateNames.push_back(*name);
    auto& transaction = Current();
    {
        std::lock_guard<std::mutex> lock(transaction.mutex);
        snapshot.state = s_state.load(std::memory_order_acquire);
        if (const StableAotConfiguration* configuration = s_configuration.load(std::memory_order_acquire))
            snapshot.stableAotNames = configuration->stableNames;
        snapshot.lastError = s_unexpectedFailure.load() ? AssemblyShadowError::InternalError :
            (s_lateBaselineUse.load() ? AssemblyShadowError::BaselineAlreadyUsed : transaction.lastError);
        snapshot.detail = s_unexpectedFailure.load() ? "Unexpected native failure sealed the transaction; restart required." :
            (s_lateBaselineUse.load() ? "Physical baseline use after publication; restart required." : transaction.detail);
        snapshot.baselineBuildId = transaction.baselineBuildId;
        snapshot.patchId = transaction.patchId;
        snapshot.generation = ActiveGeneration();
        snapshot.expected = transaction.closure.size();
        snapshot.staged = StagedCount(transaction);
        snapshot.retainedBytes = transaction.retainedBytes;
        snapshot.events = transaction.events;
        snapshot.commitOrder = transaction.commitOrder;
        for (const auto& member : transaction.closure)
        {
            snapshot.closureLoadOrder.push_back(member.candidate->name);
            ShadowAssemblyDiagnostic assembly;
            assembly.name = member.candidate->name;
            if (StagedAssembly* staged = member.staged)
            {
                assembly.mvid = staged->mvid;
                assembly.skeletonBuilt = staged->skeletonBuilt;
                assembly.runtimeMetadataInitialized = staged->runtimeMetadataInitialized;
                assembly.published = staged->published;
                assembly.moduleInitializerAttempted = staged->moduleInitializerAttempted.load();
                assembly.moduleInitializerRan = staged->moduleInitializerRan.load();
            }
            snapshot.assemblies.push_back(assembly);
        }
    }
    const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
    if (const TypeFailure* failure = s_typeFailure.load(std::memory_order_acquire))
    {
        snapshot.lastError = failure->error;
        snapshot.detail = failure->detail;
        snapshot.state = s_active.load(std::memory_order_acquire) ? AssemblyShadowState::FailedAfterCommit : AssemblyShadowState::Failed;
    }
    if (registry)
    {
        std::vector<UseRecord> uses(registry->entries.size());
        {
            UsageLock usage;
            for (size_t index = 0; index < uses.size(); ++index) uses[index] = registry->entries[index]->firstUse;
        }
        for (size_t index = 0; index < uses.size(); ++index)
        {
            const auto& use = uses[index];
            if (!use.present) continue;
            ShadowUseDiagnostic diagnostic;
            diagnostic.name = registry->entries[index]->name;
            diagnostic.kind = use.kind;
            diagnostic.detail = std::string(use.detail) + " FirstUseSequence=" + std::to_string(use.sequence);
            if (use.klass) diagnostic.type = std::string(use.klass->namespaze) + "." + use.klass->name;
            diagnostic.thread = use.thread;
            diagnostic.timestamp = use.timestamp;
            snapshot.baselineUses.push_back(diagnostic);
        }
    }
    AssemblyVector ordinary;
    // This independent snapshot is coherent with its own generation, even if a
    // commit starts after the transaction-status snapshot above was captured.
    snapshot.enumerationGeneration = Assembly::CaptureShadowEnumeration(ordinary);
    for (const Il2CppAssembly* assembly : ordinary)
    {
        ShadowDiagnosticSnapshot::OrdinaryAssembly observation;
        observation.name = assembly->aname.name;
        observation.isInterpreter = Interpreter(assembly);
        snapshot.ordinaryAssemblies.push_back(observation);
    }
    std::vector<Il2CppClass*> ordinaryClasses;
    AssemblyShadowVisibility::CollectOrdinaryClasses(ordinaryClasses, snapshot.classEnumerationGeneration);
    for (const Il2CppClass* klass : ordinaryClasses)
    {
        ShadowDiagnosticSnapshot::OrdinaryClass observation;
        observation.assemblyName = klass->image->assembly->aname.name;
        // Formatting a generic type through Type::GetName could resolve more
        // metadata and record baseline use. Report only existing raw identity.
        if (klass->namespaze && klass->namespaze[0])
            observation.typeName = std::string(klass->namespaze) + ".";
        observation.typeName += klass->name;
        observation.isInterpreter = Interpreter(klass->image->assembly);
        observation.isConstructedGeneric = klass->generic_class != nullptr;
        observation.usesStagedMetadata = AssemblyShadowVisibility::ClassUsesStagedMetadata(klass);
        snapshot.ordinaryClasses.push_back(observation);
    }
    json = AssemblyShadowDiagnostics::Serialize(snapshot);
    return AssemblyShadowError::Success;
}

AssemblyResolveContext AssemblyShadow::CurrentResolveContext()
{
    return StagingBridge::IsStaging() ? AssemblyResolveContext::Staging : AssemblyResolveContext::Normal;
}

const Il2CppAssembly* AssemblyShadow::ResolveByName(const char* name, AssemblyResolveContext context)
{
    if (context == AssemblyResolveContext::DiagnosticsPhysical)
        return MetadataCache::GetAssemblyByNameOriginal(name);
    if (context == AssemblyResolveContext::Staging)
    {
        const Il2CppAssembly* staged = nullptr;
        if (!StagingBridge::TryResolveForCurrentThread(name, staged) || !staged)
            throw std::runtime_error(std::string("Unresolved or unapproved assembly in private staging resolver: ") + (name ? name : "<null>"));
        return staged;
    }
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    return active ? active->byName.Find(name) : nullptr;
}

const Il2CppAssembly* AssemblyShadow::ResolveName(const char* name, const char*)
{
    return ResolveByName(name, CurrentResolveContext());
}

const Il2CppAssembly* AssemblyShadow::ResolveReferencedAssembly(const Il2CppAssembly* requester,
    const Il2CppAssembly* physicalProvider, const char* referencedName, int32_t referenceIndex, const char* site)
{
    if (StagingBridge::IsStaging())
    {
        const Il2CppAssembly* staged = nullptr;
        if (!StagingBridge::TryResolveForCurrentThread(referencedName, staged) || !staged)
            throw std::runtime_error(ReferenceDetail(requester, referencedName, referenceIndex, site));
        return staged;
    }
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    if (!active) return physicalProvider;
    bool closureRequester = active->byAssembly.count(requester) || active->shadowToBaseline.count(requester);
    const Il2CppAssembly* provider = active->byName.Find(referencedName);
    if (provider)
    {
        if (!requester || (!Interpreter(requester) && !closureRequester))
        {
            FailReference(requester, referencedName, referenceIndex, site);
            return nullptr;
        }
        return provider;
    }
    if (closureRequester)
    {
        const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
        Candidate* unchanged = registry->byName.Find(referencedName);
        const Il2CppAssembly* approved = unchanged ? unchanged->baseline : s_configuration.load(std::memory_order_acquire)->stableByName.Find(referencedName);
        if (!approved || (physicalProvider && physicalProvider != approved))
        {
            FailReference(requester, referencedName, referenceIndex, site);
            return nullptr;
        }
        return approved;
    }
    return physicalProvider;
}

AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)
{
    json.clear();
    std::array<Il2CppClass*, kMaximumExecutionClasses> classes{};
    size_t count = 0;
    {
        ExecutionObservationLock lock;
        for (Il2CppClass* klass : s_executionClasses)
            if (klass) classes[count++] = klass;
    }
    // No observation lock remains held while reading physical metadata. The
    // inventory creates neither classes nor generic instances, including AOT.
    AssemblyVector assemblies;
    Assembly::CaptureShadowEnumeration(assemblies);
    AssemblyShadowTypeKey::MetadataTypeImages images;
    for (const Il2CppAssembly* assembly : assemblies)
        for (uint32_t index = 0; index < assembly->image->typeCount; ++index)
            images.emplace(MetadataCache::GetAssemblyTypeHandle(assembly->image, index), assembly->image);

    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    const AssemblyShadowState state = s_state.load(std::memory_order_acquire);
    // Subset increments release-publish their preceding total increments. Read
    // them with acquire before totals: no impossible subset > total snapshot.
    const uint64_t shadowChecks = s_shadowMethodChecks.load(std::memory_order_acquire);
    const uint64_t rejectedChecks = s_rejectedBaselineMethods.load(std::memory_order_acquire);
    const uint64_t methodChecks = s_methodChecks.load(std::memory_order_relaxed);
    const uint64_t shadowTransforms = s_shadowInterpreterTransformations.load(std::memory_order_acquire);
    const uint64_t transforms = s_interpreterTransformations.load(std::memory_order_relaxed);
    std::ostringstream output;
    output << std::boolalpha << "{\"schemaVersion\":1,\"enabled\":true,\"stateCode\":" << static_cast<int>(state)
        << ",\"state\":" << AssemblyShadowDiagnostics::Quote(AssemblyShadowDiagnostics::StateName(state))
        << ",\"generation\":" << (active ? active->generation : 0)
        << ",\"methodChecks\":" << methodChecks << ",\"shadowMethodChecks\":" << shadowChecks
        << ",\"rejectedBaselineMethods\":" << rejectedChecks
        << ",\"baselineClassCctorStarted\":" << s_baselineClassCctorStarted.load(std::memory_order_relaxed)
        << ",\"shadowClassCctorStarted\":" << s_shadowClassCctorStarted.load(std::memory_order_relaxed)
        << ",\"interpreterTransformations\":" << transforms
        << ",\"shadowInterpreterTransformations\":" << shadowTransforms
        << ",\"droppedClassObservations\":" << s_droppedClassObservations.load(std::memory_order_relaxed)
        << ",\"classes\":[";
    for (size_t index = 0; index < count; ++index)
    {
        Il2CppClass* klass = classes[index];
        const Il2CppAssembly* owner = klass->image->assembly;
        const bool baseline = active && active->byAssembly.count(owner);
        const bool shadow = active && active->shadowToBaseline.count(owner);
        std::string typeKey;
        try { typeKey = AssemblyShadowTypeKey::FormatMetadataOnly(&klass->byval_arg, images); }
        catch (const ShadowTypeResolutionFailure& failure) { return failure.error; }
        void* storage;
        {
            il2cpp::os::FastAutoLock metadataLock(&g_MetadataLock);
            storage = klass->static_fields; // Only already-existing storage.
        }
        const bool started = os::Atomic::LoadRelaxed(reinterpret_cast<const int32_t*>(&klass->cctor_started)) != 0;
        const bool finished = os::Atomic::LoadRelaxed(reinterpret_cast<const int32_t*>(&klass->cctor_finished_or_no_cctor)) != 0;
        const bool exception = os::Atomic::LoadRelaxed(reinterpret_cast<const int32_t*>(&klass->initializationExceptionGCHandle)) != 0;
        std::string pointer;
#if IL2CPP_DEBUG
        const bool details = true;
        if (storage)
        {
            std::ostringstream address;
            address << "0x" << std::hex << reinterpret_cast<uintptr_t>(storage);
            pointer = address.str();
        }
#else
        const bool details = false;
#endif
        if (index) output << ',';
        output << "{\"logicalAssembly\":" << AssemblyShadowDiagnostics::Quote(owner->aname.name)
            << ",\"typeKey\":" << AssemblyShadowDiagnostics::Quote(typeKey)
            << ",\"executionModeCode\":" << (baseline || shadow ? 1 : 0)
            << ",\"executionMode\":" << AssemblyShadowDiagnostics::Quote(baseline || shadow ? "InterpreterShadow" : "AotBaseline")
            << ",\"physicalImageKind\":" << AssemblyShadowDiagnostics::Quote(Interpreter(owner) ? "Interpreter" : "Aot")
            << ",\"isActive\":" << !baseline
            << ",\"cctorStarted\":" << started << ",\"cctorFinished\":" << finished
            << ",\"hasInitializationException\":" << exception
            << ",\"staticStoragePointer\":" << AssemblyShadowDiagnostics::Quote(pointer)
            << ",\"pointerDetailsAvailable\":" << details << ",\"staticStorageAvailable\":" << (storage != nullptr) << '}';
    }
    output << "]}";
    json = output.str();
    return AssemblyShadowError::Success;
}

const Il2CppAssembly* AssemblyShadow::ResolveAssembly(const Il2CppAssembly* assembly)
{
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    if (active)
    {
        auto found = active->byAssembly.find(assembly);
        if (found != active->byAssembly.end()) return found->second;
    }
    return assembly;
}

const Il2CppImage* AssemblyShadow::ResolveImage(const Il2CppImage* image)
{
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    if (active)
    {
        auto found = active->byImage.find(image);
        if (found != active->byImage.end()) return found->second;
    }
    return image;
}

const Il2CppImage* AssemblyShadow::ResolvePublicImageIdentity(const Il2CppImage* image)
{
    if (!image) return image;
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    if (active)
    {
        auto found = active->shadowToBaseline.find(image->assembly);
        // Only the exact published physical image receives the stable identity.
        // Private images and physical metadata ownership are never rewritten.
        if (found != active->shadowToBaseline.end() && found->first->image == image)
            return found->second->image;
    }
    return image;
}

bool AssemblyShadow::IsShadowedBaseline(const Il2CppAssembly* assembly)
{
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    return active && active->byAssembly.count(assembly);
}

bool AssemblyShadow::IsActiveShadow(const Il2CppAssembly* assembly)
{
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    return active && active->shadowToBaseline.count(assembly);
}

bool AssemblyShadow::IsCandidate(const Il2CppAssembly* assembly)
{
    const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
    return registry && registry->byAssembly.count(assembly);
}

uint64_t AssemblyShadow::ActiveGeneration()
{
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    return active ? active->generation : 0;
}

void AssemblyShadow::RecordBaselineUse(const Il2CppAssembly* assembly, BaselineUseKind kind,
    const char* detail, const Il2CppClass* klass)
{
    // Private interpreter identities are absent from this physical AOT map.
    // A thread-local staging scope does not authorize baseline execution/use.
    if (!assembly) return;
    const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
    if (!registry) return;
    auto found = registry->byAssembly.find(assembly);
    if (found == registry->byAssembly.end()) return;
    bool late;
    {
        UsageLock lock;
        auto& use = found->second->firstUse;
        if (!use.present)
        {
            use.present = true;
            use.kind = kind;
            CopyDetail(use.detail, sizeof(use.detail), detail);
            use.klass = klass;
            use.thread = static_cast<uint64_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
            use.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            ++s_usageGeneration;
            use.sequence = s_usageGeneration;
        }
        late = IsShadowedBaseline(assembly);
        if (late)
        {
            s_lateBaselineUse.store(true, std::memory_order_release);
            s_state.store(AssemblyShadowState::FailedAfterCommit, std::memory_order_release);
        }
    }
    if (late)
        FailTypeResolution(AssemblyShadowError::BaselineAlreadyUsed, std::string("ShadowBaselineUse Assembly=") + assembly->aname.name +
            " Type=" + (klass ? AssemblyShadowTypeKey::Format(&klass->byval_arg) : "<assembly>") +
            " Kind=" + AssemblyShadowDiagnostics::UseKindName(kind) + " Site=" + (detail ? detail : ""));
}

bool AssemblyShadow::AssertMethodIsActive(const MethodInfo* method, const char* site) noexcept
{
    s_methodChecks.fetch_add(1, std::memory_order_relaxed);
    const MethodInfo* definition = method && method->is_inflated && method->genericMethod ?
        method->genericMethod->methodDefinition : method;
    const Il2CppAssembly* owners[2] = {PhysicalMethodAssembly(method), PhysicalMethodAssembly(definition)};
    AssemblyShadowError error = AssemblyShadowError::Success;
    const MethodInfo* rejected = nullptr;
    CapturedArgumentFailure argumentFailure;
    if (!owners[0] || !owners[1] || (method->is_inflated && (!method->genericMethod || definition == method || definition->is_inflated)))
        error = AssemblyShadowError::InvalidArgument;
    else
    {
        ObserveExecutionClass(method->klass);
        const CandidateRegistry* candidates = s_candidates.load(std::memory_order_acquire);
        Candidate* uses[2] = {};
        if (candidates)
        {
            for (size_t index = 0; index < 2; ++index)
            {
                auto found = candidates->byAssembly.find(owners[index]);
                if (found != candidates->byAssembly.end()) uses[index] = found->second;
            }
        }
        // Publication uses this same short lock. Either the use prevents the
        // publish, or the published exact baseline identity is rejected here.
        if (uses[0] || uses[1])
        {
            UsageLock lock;
            const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
            for (size_t index = 0; index < 2; ++index)
            {
                if (active && active->byAssembly.count(owners[index]))
                    rejected = index == 0 ? method : definition;
                if (!uses[index] || uses[index]->firstUse.present) continue;
                UseRecord& use = uses[index]->firstUse;
                use.present = true;
                use.kind = BaselineUseKind::MethodExecution;
                CopyDetail(use.detail, sizeof(use.detail), site);
                use.klass = index == 0 ? method->klass : definition->klass;
                use.thread = static_cast<uint64_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
                use.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                use.sequence = ++s_usageGeneration;
            }
        }
        const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
        if (active && active->byAssembly.count(owners[0])) rejected = method;
        else if (active && active->byAssembly.count(owners[1])) rejected = definition;
        if (active && (active->shadowToBaseline.count(owners[0]) || active->shadowToBaseline.count(owners[1])))
            s_shadowMethodChecks.fetch_add(1, std::memory_order_release);
        if (rejected)
        {
            error = AssemblyShadowError::BaselineMethodExecution;
            s_rejectedBaselineMethods.fetch_add(1, std::memory_order_release);
        }
        else if (method->is_inflated || method->klass->generic_class)
        {
            UsageLock lock;
            CapturedArgumentVisitor visitor(candidates, s_active.load(std::memory_order_acquire), site, argumentFailure);
            bool valid = !method->is_inflated || visitor.Context(method->genericMethod->context, "class_inst", "method_inst");
            if (valid && method->klass->generic_class)
            {
                if (!method->klass->generic_class->context.class_inst)
                    valid = visitor.Fail("EmptyDeclaringClassInstance");
                else if (method->klass->generic_class->context.method_inst)
                    valid = visitor.Fail("MalformedDeclaringClassContext");
                else valid = visitor.Context(method->klass->generic_class->context, "declaring_class_inst", "declaring_method_inst");
            }
            if (!valid)
            {
                error = argumentFailure.error;
                if (error == AssemblyShadowError::BaselineAlreadyUsed)
                    s_lateBaselineUse.store(true, std::memory_order_release);
            }
        }
    }
    if (error == AssemblyShadowError::Success) return true;

    // No managed exception or metadata query is allowed in this boolean hook.
    // Failure strings allocate only on rejection, outside usage/observation locks.
    try
    {
        const MethodInfo* detailMethod = rejected ? rejected : method;
        const Il2CppAssembly* owner = PhysicalMethodAssembly(detailMethod);
        std::string detail = std::string(error == AssemblyShadowError::BaselineMethodExecution ?
            "ShadowBaselineMethodExecution" : error == AssemblyShadowError::BaselineAlreadyUsed ?
            "ShadowCapturedBaselineType" : "ShadowInvalidMethod") + " Assembly=" +
            (owner && owner->aname.name ? owner->aname.name : "<null>") + " Type=" +
            (detailMethod && detailMethod->klass && detailMethod->klass->namespaze ? detailMethod->klass->namespaze : "") + "." +
            (detailMethod && detailMethod->klass && detailMethod->klass->name ? detailMethod->klass->name : "<null>") +
            " Method=" + (detailMethod && detailMethod->name ? detailMethod->name : "<null>") +
            " Site=" + (site ? site : "");
        if (argumentFailure.error != AssemblyShadowError::Success)
            detail += std::string(" CapturedArgumentAssembly=") +
                (argumentFailure.candidate ? argumentFailure.candidate->name : "<none>") +
                " Context=" + argumentFailure.context + " TypePath=" + argumentFailure.path + " Reason=" + argumentFailure.reason;
        RecordFirstGuardFailure(error, detail);
    }
    catch (...)
    {
        // Even allocation failure cannot permit the caller to execute.
        s_unexpectedFailure.store(true, std::memory_order_release);
    }
    s_state.store(s_active.load(std::memory_order_acquire) ? AssemblyShadowState::FailedAfterCommit : AssemblyShadowState::Failed,
        std::memory_order_release);
    return false;
}

void AssemblyShadow::RequireActiveMethod(const MethodInfo* method, const char* site)
{
    if (!AssertMethodIsActive(method, site))
    {
        const TypeFailure* failure = s_typeFailure.load(std::memory_order_acquire);
        FailTypeResolution(failure ? failure->error : AssemblyShadowError::InternalError,
            failure ? failure->detail : "Assembly Shadow method execution rejected; diagnostic allocation failed.");
        throw std::runtime_error("Assembly Shadow method execution rejected.");
    }
    RequireUserCodeAllowed();
}

void AssemblyShadow::ObserveClassCctorStarted(Il2CppClass* klass)
{
    ObserveExecutionClass(klass);
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    if (!active || !klass || !klass->image) return;
    const Il2CppAssembly* owner = klass->image->assembly;
    if (active->byAssembly.count(owner)) s_baselineClassCctorStarted.fetch_add(1, std::memory_order_relaxed);
    else if (active->shadowToBaseline.count(owner)) s_shadowClassCctorStarted.fetch_add(1, std::memory_order_relaxed);
}

void AssemblyShadow::ObserveInterpreterTransformation(const MethodInfo* method)
{
    s_interpreterTransformations.fetch_add(1, std::memory_order_relaxed);
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    if (active && active->shadowToBaseline.count(PhysicalMethodAssembly(method)))
        s_shadowInterpreterTransformations.fetch_add(1, std::memory_order_release);
    if (method) ObserveExecutionClass(method->klass);
}

void AssemblyShadow::RequireUserCodeAllowed()
{
    if (StagingBridge::IsStaging() || IsResolvingTypeMetadata())
        // Do not allocate a managed exception while guarding managed execution:
        // constructing that exception could itself require a class initializer.
        throw std::runtime_error("Managed execution is forbidden during Assembly Shadow physical/private metadata resolution.");
}

void AssemblyShadow::FailTypeResolution(AssemblyShadowError error, const std::string& detail)
{
    AssemblyShadowTypeResolver::CountGuardFailure();
    RecordFirstGuardFailure(error, detail);
    s_state.store(s_active.load(std::memory_order_acquire) ? AssemblyShadowState::FailedAfterCommit : AssemblyShadowState::Failed,
        std::memory_order_release);
    // Allocating a managed exception inside the physical metadata scope would
    // itself be an allocation exposure. Unwind that scope natively first.
    if (IsResolvingTypeMetadata() || StagingBridge::IsStaging()) throw ShadowTypeResolutionFailure(error, detail);
    Exception::Raise(Exception::GetInvalidOperationException(detail.c_str()));
}

Il2CppClass* AssemblyShadow::ResolveClassDefinition(Il2CppClass* klass)
{
    try { return AssemblyShadowTypeResolver::ResolveDefinition(klass); }
    catch (const ShadowTypeResolutionFailure& error) { FailTypeResolution(error.error, error.what()); return nullptr; }
}

Il2CppClass* AssemblyShadow::ResolveClass(Il2CppClass* klass)
{
    try { return AssemblyShadowTypeResolver::ResolveClass(klass); }
    catch (const ShadowTypeResolutionFailure& error) { FailTypeResolution(error.error, error.what()); return nullptr; }
}

const Il2CppType* AssemblyShadow::ResolveType(const Il2CppType* type)
{
    try { return AssemblyShadowTypeResolver::Resolve(type); }
    catch (const ShadowTypeResolutionFailure& error) { FailTypeResolution(error.error, error.what()); return nullptr; }
}

const MethodInfo* AssemblyShadow::ResolveReflectionMethod(const MethodInfo* method)
{
    try { return AssemblyShadowTypeResolver::ResolveReflectionMethod(method); }
    catch (const ShadowTypeResolutionFailure& error) { FailTypeResolution(error.error, error.what()); return nullptr; }
}

Il2CppClass* AssemblyShadow::ResolveAllocationClass(Il2CppClass* klass, const char* site)
{
    try
    {
        Il2CppClass* result = AssemblyShadowTypeResolver::ResolveAllocation(klass, site);
        AssemblyShadowTypeResolver::RecordUse(result ? &result->byval_arg : nullptr, BaselineUseKind::ObjectAllocation, site);
        return result;
    }
    catch (const ShadowTypeResolutionFailure& error) { FailTypeResolution(error.error, error.what()); return nullptr; }
}

void AssemblyShadow::RequireActiveClass(Il2CppClass* klass, BaselineUseKind kind, const char* site)
{
    RecordTypeUse(klass ? &klass->byval_arg : nullptr, kind, site);
}

void AssemblyShadow::RecordTypeUse(const Il2CppType* type, BaselineUseKind kind, const char* site)
{
    try { AssemblyShadowTypeResolver::RecordUse(type, kind, site); }
    catch (const ShadowTypeResolutionFailure& error) { FailTypeResolution(error.error, error.what()); }
}

AssemblyShadowError AssemblyShadow::GetTypeResolutionInfo(const Il2CppType* type, std::string& json)
{
    return AssemblyShadowTypeResolver::GetInfo(type, json);
}

AssemblyShadowError AssemblyShadow::ReportUnexpectedFailure()
{
    s_unexpectedFailure.store(true, std::memory_order_release);
    s_state.store(s_active.load(std::memory_order_acquire) ? AssemblyShadowState::FailedAfterCommit : AssemblyShadowState::Failed,
        std::memory_order_release);
    return AssemblyShadowError::InternalError;
}

Il2CppClass* AssemblyShadow::ResolveUnityComparisonTarget(const Il2CppClass* actual, Il2CppClass* expected)
{
    if (!actual || !expected || !IsActiveShadow(actual->image->assembly) || !IsShadowedBaseline(expected->image->assembly) ||
        ResolveImage(expected->image) != actual->image) return expected;
    return ResolveClass(expected); // Resolve only the comparison handle, never actual->klass.
}

void AssemblyShadow::TraceImage(const char* site, const Il2CppImage* image)
{
    if (image) RecordBaselineUse(image->assembly, BaselineUseKind::AssemblyReflection, site);
}

void AssemblyShadow::TraceClass(const char* site, const Il2CppClass* klass)
{
    if (!klass || !klass->image) return;
    BaselineUseKind kind = BaselineUseKind::TypeReflection;
    if (strncmp(site, "Object::", 8) == 0) kind = BaselineUseKind::ObjectAllocation;
    else if (strcmp(site, "Class::Init") == 0 || strcmp(site, "Runtime::ClassInit") == 0) kind = BaselineUseKind::ClassInit;
    RecordBaselineUse(klass->image->assembly, kind, site, klass);
}

void AssemblyShadow::TraceTypeCheck(const char* site, const Il2CppClass* actual,
    const Il2CppClass*, bool, bool)
{
    // The expected Unity query target may be a cached baseline class that is
    // normalized immediately afterward. Never mutate an actual object's class.
    TraceClass(site, actual);
}

void AssemblyShadow::SetPhase(const char* phase)
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    transaction.phase = phase ? phase : "";
}

std::string AssemblyShadow::InspectAssembly(const Il2CppAssembly* assembly)
{
    // Physical inspection is intentionally resolver-free and pointer-free in
    // both configurations. It does not create/cache a Reflection handle.
    return "{\"name\":" + AssemblyShadowDiagnostics::Quote(assembly ? assembly->aname.name : "") +
        ",\"isInterpreter\":" + (Interpreter(assembly) ? "true" : "false") +
        ",\"matchesShadow\":" + (IsActiveShadow(assembly) ? "true" : "false") + "}";
}

std::string AssemblyShadow::InspectObject(const Il2CppObject* object)
{
    const Il2CppClass* klass = object ? object->klass : nullptr;
    return "{\"type\":" + AssemblyShadowDiagnostics::Quote(klass ? std::string(klass->namespaze) + "." + klass->name : "") +
        ",\"instanceSize\":" + std::to_string(klass ? klass->instance_size : 0) +
        ",\"physicalAssembly\":" + InspectAssembly(klass ? klass->image->assembly : nullptr) + "}";
}

}}
#else
namespace il2cpp { namespace vm {
bool AssemblyShadow::BeginStartupTrackingInitialization() noexcept { return true; }
bool AssemblyShadow::InitializeStartupCandidates() noexcept { return true; }
void AssemblyShadow::FailStartupTrackingInitialization() noexcept {}
AssemblyShadowError AssemblyShadow::ConfigureCandidates(const char*, const std::vector<std::string>&, const std::vector<std::string>&) { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::BeginTransaction(const char*, const char*, const std::vector<std::string>&, int32_t) { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::StageAssembly(const uint8_t*, size_t, const uint8_t*, size_t) { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::ReserveMetadataBudget(const std::vector<uint64_t>&, int32_t) { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::GetMetadataCapacityJson(const std::vector<uint64_t>&, std::string& json) { json = "{\"schemaVersion\":2,\"enabled\":false,\"profileVersion\":0}"; return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::GetRecoveryInfoJson(std::string& json) { json = "{\"schemaVersion\":1,\"enabled\":false,\"capabilityVersion\":0}"; return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::ValidateTransaction() { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::CommitTransaction() { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::AbortTransaction() { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::ReportUnexpectedFailure() { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::GetState(AssemblyShadowState& state) { state = AssemblyShadowState::Disabled; return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::GetAssemblyExecutionMode(const char*, AssemblyExecutionMode& mode) { mode = AssemblyExecutionMode::AotBaseline; return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::GetDiagnosticsJson(std::string& json)
{
    ShadowDiagnosticSnapshot snapshot;
    snapshot.lastError = AssemblyShadowError::FeatureDisabled;
    json = AssemblyShadowDiagnostics::Serialize(snapshot);
    return AssemblyShadowError::FeatureDisabled;
}
AssemblyShadowError AssemblyShadow::GetTypeResolutionInfo(const Il2CppType*, std::string& json)
{
    json.clear();
    return AssemblyShadowError::FeatureDisabled;
}
AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)
{
    json.clear();
    return AssemblyShadowError::FeatureDisabled;
}
}}
#endif
