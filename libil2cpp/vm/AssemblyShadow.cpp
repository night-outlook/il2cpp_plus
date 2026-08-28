#include "AssemblyShadow.h"
#include "AssemblyShadowDiagnostics.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadowName.h"
#include "AssemblyShadowVisibility.h"
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "vm/Assembly.h"
#include "vm/Exception.h"
#include "vm/Image.h"
#include "vm/MetadataCache.h"
#include "hybridclr/metadata/Assembly.h"
#include "hybridclr/metadata/AssemblyShadowBridge.h"
#include "hybridclr/metadata/MetadataUtil.h"
#include "hybridclr/metadata/StagedAssembly.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <stdexcept>

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
        uint64_t retainedBytes = 0;
        uint64_t validatedUsageGeneration = 0;
        AssemblyShadowError lastError = AssemblyShadowError::Success;
        std::string detail;
        std::vector<ShadowEventDiagnostic> events;
        std::vector<std::string> commitOrder;
    };

    std::atomic<CandidateRegistry*> s_candidates{nullptr};
    std::atomic<const ActiveSnapshot*> s_active{nullptr};
    std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Disabled};
    std::atomic<bool> s_lateBaselineUse{false};
    std::atomic<bool> s_unexpectedFailure{false};
    std::atomic_flag s_usageLock = ATOMIC_FLAG_INIT;
    uint64_t s_usageGeneration = 0;

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

    bool Interpreter(const Il2CppAssembly* assembly)
    {
        return assembly && assembly->image && hybridclr::metadata::IsInterpreterImage(assembly->image);
    }

    AssemblyShadowError Result(Transaction& transaction, AssemblyShadowError error, const std::string& detail = "")
    {
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
        return registry->stableByName.Find(name);
    }

    bool CanResolvePrivateFacade(const char* name, const CandidateRegistry* registry)
    {
        return hybridclr::metadata::Image::CanUseLogicalNetStandardFacade(name,
            registry->byName.Find(name) != nullptr,
            MetadataCache::GetAotAssemblyByNamePhysical(name) != nullptr,
            registry->netstandardProviders.size());
    }

    bool ResolvePrivateFacade(const char* name, std::vector<const Il2CppAssembly*>& providers, void*)
    {
        const CandidateRegistry* registry = s_candidates.load(std::memory_order_acquire);
        if (!registry || !CanResolvePrivateFacade(name, registry)) return false;
        providers = registry->netstandardProviders;
        return true;
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
                else if (!registry->stableByName.Find(reference.c_str()) && !CanResolvePrivateFacade(reference.c_str(), registry))
                {
                    return Result(transaction,
                        MetadataCache::GetAotAssemblyByNamePhysical(reference.c_str()) ?
                            AssemblyShadowError::ReferenceEscapesClosure : AssemblyShadowError::ReferenceResolutionFailed,
                        "Unapproved or missing stable AOT reference: " + reference);
                }
            }
        }
        return AssemblyShadowError::Success;
    }

    struct Publication
    {
        Transaction* transaction;
        ActiveSnapshot* snapshot;
        Candidate* used = nullptr;
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

    void PublishActive(void* context)
    {
        auto& publication = *static_cast<Publication*>(context);
        for (const auto& member : publication.transaction->closure)
            InterpreterAssembly::PublishStagedImage(member.staged);
        s_active.store(publication.snapshot, std::memory_order_release);
        UnlockUsage();
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
}

AssemblyShadowError AssemblyShadow::ConfigureCandidates(const char* baselineBuildId,
    const std::vector<std::string>& names, const std::vector<std::string>& stableAotNames)
{
    auto& transaction = Current();
    std::lock_guard<std::mutex> lock(transaction.mutex);
    if (s_state.load() != AssemblyShadowState::Disabled || s_candidates.load()) return WrongState(transaction);
    if (!baselineBuildId || !*baselineBuildId || names.empty())
        return Result(transaction, AssemblyShadowError::InvalidArgument, "A baseline ID and candidates are required.");
    std::unique_ptr<CandidateRegistry> registry(new CandidateRegistry());
    registry->byName.Reserve(names.size());
    registry->byAssembly.reserve(names.size());
    registry->entries.reserve(names.size());
    for (const auto& name : names)
    {
        std::string canonical;
        if (!CanonicalName(name.c_str(), canonical)) return Result(transaction, AssemblyShadowError::InvalidArgument, name);
        if (registry->byName.Find(canonical.c_str())) return Result(transaction, AssemblyShadowError::DuplicateAssemblyName, name);
        const Il2CppAssembly* baseline = MetadataCache::GetAotAssemblyByNamePhysical(canonical.c_str());
        if (!baseline) return Result(transaction, AssemblyShadowError::BaselineAssemblyNotFound, name);
        if (Interpreter(baseline)) return Result(transaction, AssemblyShadowError::UnsupportedAssembly, name);
        std::unique_ptr<Candidate> candidate(new Candidate());
        candidate->name = baseline->aname.name;
        candidate->baseline = baseline;
        registry->byName.Add(candidate->name, candidate.get());
        registry->byAssembly.emplace(baseline, candidate.get());
        registry->entries.push_back(std::move(candidate));
    }
    registry->stableByName.Reserve(stableAotNames.size());
    for (const auto& name : stableAotNames)
    {
        std::string canonical;
        if (!CanonicalName(name.c_str(), canonical)) return Result(transaction, AssemblyShadowError::InvalidArgument, name);
        if (registry->byName.Find(canonical.c_str()) || registry->stableByName.Find(canonical.c_str()))
            return Result(transaction, AssemblyShadowError::DuplicateAssemblyName, name);
        const Il2CppAssembly* assembly = MetadataCache::GetAotAssemblyByNamePhysical(canonical.c_str());
        if (!assembly) return Result(transaction, AssemblyShadowError::BaselineAssemblyNotFound, name);
        if (Interpreter(assembly)) return Result(transaction, AssemblyShadowError::UnsupportedAssembly, name);
        registry->stableByName.Add(assembly->aname.name, assembly);
        registry->stableNames.push_back(assembly->aname.name);
    }
    // Facade providers are the intersection of upstream's finite framework
    // provider list and explicitly approved physical stable AOT assemblies.
    // Candidates cannot enter stableByName, even when named after a provider.
    for (const char* const* name = hybridclr::metadata::Image::GetNetStandardProviderNames(); *name; ++name)
        if (const Il2CppAssembly* provider = registry->stableByName.Find(*name))
            registry->netstandardProviders.push_back(provider);
    transaction.owner = std::this_thread::get_id();
    transaction.baselineBuildId = baselineBuildId;
    s_candidates.store(registry.release(), std::memory_order_release);
    s_state.store(AssemblyShadowState::CandidatesRegistered, std::memory_order_release);
    Event(transaction, "candidates-registered");
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
    error = InterpreterAssembly::CreateStagedSkeleton(dll, dllLength, pdb, pdbLength, staged, detail);
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
    s_state.store(AssemblyShadowState::Committing, std::memory_order_release);
    if (!MetadataCache::PublishInterpreterAssembliesBatch(assemblies, TryBeginPublication, PublishActive, &publication))
    {
        s_state.store(AssemblyShadowState::Validated, std::memory_order_release);
        return Result(transaction, AssemblyShadowError::BaselineAlreadyUsed,
            publication.used ? publication.used->name : "Candidate use changed before publication.");
    }
    snapshot.release(); // Immutable active mapping is process-lifetime.
    Event(transaction, "active-published");
    // This is the managed-execution boundary. Do not move initializers above it.
    lock.unlock();
    for (const auto& member : transaction.closure)
    {
        if (s_lateBaselineUse.load(std::memory_order_acquire))
        {
            lock.lock();
            s_state.store(AssemblyShadowState::FailedAfterCommit, std::memory_order_release);
            return Result(transaction, AssemblyShadowError::BaselineAlreadyUsed, "Baseline use raced activation; restart is required.");
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
                return Result(transaction, AssemblyShadowError::ModuleInitializerFailed, detail);
            }
            transaction.commitOrder.push_back(member.candidate->name);
            Event(transaction, "initializer-complete", member.candidate->name);
        }
    }
    lock.lock();
    AssemblyShadowState expected = AssemblyShadowState::Committing;
    if (!s_state.compare_exchange_strong(expected, AssemblyShadowState::Committed, std::memory_order_acq_rel))
        return Result(transaction, AssemblyShadowError::BaselineAlreadyUsed, "Baseline use raced initializers; restart is required.");
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
    if (!registry || !registry->byName.Find(name)) return AssemblyShadowError::CandidateNotRegistered;
    if (const ActiveSnapshot* active = s_active.load(std::memory_order_acquire))
        if (active->byName.Find(name)) mode = AssemblyExecutionMode::InterpreterShadow;
    return AssemblyShadowError::Success;
}

AssemblyShadowError AssemblyShadow::GetDiagnosticsJson(std::string& json)
{
    ShadowDiagnosticSnapshot snapshot;
    snapshot.enabled = true;
    auto& transaction = Current();
    {
        std::lock_guard<std::mutex> lock(transaction.mutex);
        snapshot.state = s_state.load(std::memory_order_acquire);
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
    if (registry)
    {
        snapshot.stableAotNames = registry->stableNames;
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
            diagnostic.detail = use.detail;
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

const Il2CppAssembly* AssemblyShadow::ResolveName(const char* name, const char*)
{
    const Il2CppAssembly* staged = nullptr;
    if (StagingBridge::TryResolveForCurrentThread(name, staged))
    {
        if (!staged) throw std::runtime_error("Unresolved or unapproved assembly in private staging resolver.");
        return staged;
    }
    const ActiveSnapshot* active = s_active.load(std::memory_order_acquire);
    return active ? active->byName.Find(name) : nullptr;
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
    if (!assembly || StagingBridge::IsStaging()) return;
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
        }
        late = IsShadowedBaseline(assembly);
        if (late)
        {
            s_lateBaselineUse.store(true, std::memory_order_release);
            s_state.store(AssemblyShadowState::FailedAfterCommit, std::memory_order_release);
        }
    }
    if (late)
        Exception::Raise(Exception::GetInvalidOperationException(
            "Assembly Shadow detected physical baseline use after activation. Business startup must terminate; restart is required."));
}

void AssemblyShadow::RequireUserCodeAllowed()
{
    if (StagingBridge::IsStaging())
        // Do not allocate a managed exception while guarding managed execution:
        // constructing that exception could itself require a class initializer.
        throw std::runtime_error("Managed execution is forbidden during private Assembly Shadow metadata staging.");
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
        ResolveImage(expected->image) != actual->image || actual->is_generic || actual->generic_class || actual->declaringType ||
        expected->is_generic || expected->generic_class || expected->declaringType) return expected;
    Il2CppClass* mapped = Image::ClassFromName(actual->image, expected->namespaze, expected->name);
    return mapped ? mapped : expected;
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
AssemblyShadowError AssemblyShadow::ConfigureCandidates(const char*, const std::vector<std::string>&, const std::vector<std::string>&) { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::BeginTransaction(const char*, const char*, const std::vector<std::string>&, int32_t) { return AssemblyShadowError::FeatureDisabled; }
AssemblyShadowError AssemblyShadow::StageAssembly(const uint8_t*, size_t, const uint8_t*, size_t) { return AssemblyShadowError::FeatureDisabled; }
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
}}
#endif
