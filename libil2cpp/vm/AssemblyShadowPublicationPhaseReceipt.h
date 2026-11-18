#pragma once

// R03 B2 / D1: inert publication phase-receipt contract.
// NOT wired to AssemblyShadow::CommitTransaction, the HybridCLR Runtime, or
// Player. D2-D4 must separately review native lock/lifetime and failure policy.
//
// ONE receipt per publication ATTEMPT, not one reusable receipt per transaction.
// A proved pre-codec rejection may leave the transaction Validated, but a
// later attempt must construct a FRESH receipt. No Reset() is intentional.
// One writer owns each attempt; readers must finish before it is destroyed.
// This source contains no dynamic allocation, managed VM call, logging, or
// exception. Atomic reads publish one coherent {phase, outcome, reason} word.
// It does NOT independently authenticate caller-provided rollback evidence.

#include <atomic>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_publication {

enum class Phase : uint8_t
{
    Prepared = 0,
    UsageAdmitted = 1,
    RegistriesAppended = 2,
    CodecAttempted = 3,
    RuntimeGroupPublished = 4,
    ActiveSnapshotPublished = 5,
    InitializersRunning = 6,
};

enum class Outcome : uint8_t
{
    InProgress = 0,
    RejectedBeforeCodec = 1,
    TerminalRestartRequired = 2,
    Complete = 3,
};

enum class Reason : uint8_t
{
    None = 0,
    ProvenPreCodecRejection = 1,
    MissingCleanupProof = 2,
    InvalidPhaseTransition = 3,
    CodecOutcomeUnknown = 4,
    NativePublicationFailure = 5,
    InitializerFailure = 6,
    SnapshotOwnershipUnverified = 7,
};

// The producer must prove every property at its actual native call site.
// Setting all booleans is NOT itself proof of registry or Codec lifetime.
struct PreCodecCleanupProof
{
    bool codecNotAttempted;
    bool physicalRegistryRestored;
    bool cliRegistryRestored;
    bool usageLockReleased;
};

struct Snapshot
{
    Phase phase;
    Outcome outcome;
    Reason reason;
};

class PublicationPhaseReceipt final
{
public:
    PublicationPhaseReceipt() noexcept
        : word_(Pack(Phase::Prepared, Outcome::InProgress, Reason::None))
    {
    }

    PublicationPhaseReceipt(const PublicationPhaseReceipt&) = delete;
    PublicationPhaseReceipt& operator=(const PublicationPhaseReceipt&) = delete;

    // Coherent single-word snapshot; no string allocation or VM calls.
    Snapshot Read() const noexcept
    {
        const uint32_t word = word_.load(std::memory_order_acquire);
        Snapshot result = {
            static_cast<Phase>(word & kByteMask),
            static_cast<Outcome>((word >> kOutcomeShift) & kByteMask),
            static_cast<Reason>((word >> kReasonShift) & kByteMask),
        };
        return result;
    }

    // Only sequential P0 -> P1 -> ... -> P6 transitions are legal. This
    // receipt does not imply that the corresponding native action occurred.
    // The owner must record CodecAttempted BEFORE entering the real Codec.
    bool Advance(Phase next) noexcept
    {
        uint32_t observed = word_.load(std::memory_order_acquire);
        if (ExtractOutcome(observed) != Outcome::InProgress)
            return false;
        const uint8_t current = static_cast<uint8_t>(ExtractPhase(observed));
        const uint8_t requested = static_cast<uint8_t>(next);
        if (requested != static_cast<unsigned>(current) + 1u ||
            requested > static_cast<uint8_t>(Phase::InitializersRunning))
        {
            MarkTerminal(Reason::InvalidPhaseTransition);
            return false;
        }
        const uint32_t desired = Pack(next, Outcome::InProgress, Reason::None);
        if (word_.compare_exchange_strong(observed, desired,
                std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
        // Concurrent writer misuse is terminal. Do not silently reorder phases.
        MarkTerminal(Reason::InvalidPhaseTransition);
        return false;
    }

    // The only disposition permitting an existing Validated state to survive.
    // An unverified pre-codec cleanup is *terminal*, never a retry permission.
    // This call must never be used after marking CodecAttempted.
    bool RejectBeforeCodec(const PreCodecCleanupProof& proof) noexcept
    {
        uint32_t observed = word_.load(std::memory_order_acquire);
        if (ExtractOutcome(observed) != Outcome::InProgress)
            return false;
        if (static_cast<uint8_t>(ExtractPhase(observed)) >=
            static_cast<uint8_t>(Phase::CodecAttempted))
        {
            MarkTerminal(Reason::CodecOutcomeUnknown);
            return false;
        }
        if (!proof.codecNotAttempted || !proof.physicalRegistryRestored ||
            !proof.cliRegistryRestored || !proof.usageLockReleased)
        {
            MarkTerminal(Reason::MissingCleanupProof);
            return false;
        }
        const uint32_t desired = Pack(ExtractPhase(observed),
            Outcome::RejectedBeforeCodec, Reason::ProvenPreCodecRejection);
        if (word_.compare_exchange_strong(observed, desired,
                std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
        MarkTerminal(Reason::InvalidPhaseTransition);
        return false;
    }

    // Unknown outcomes, including exceptions after entering CodecAttempted,
    // remain terminal. The first terminal reason is preserved *within the
    // attempt*. Complete and RejectedBeforeCodec are absorbing; post-Complete
    // process-wide fatal failures require the existing separate terminal guard.
    void MarkTerminal(Reason reason) noexcept
    {
        if (reason == Reason::None || reason == Reason::ProvenPreCodecRejection)
            reason = Reason::InvalidPhaseTransition;
        uint32_t observed = word_.load(std::memory_order_acquire);
        while (ExtractOutcome(observed) == Outcome::InProgress)
        {
            const uint32_t desired = Pack(ExtractPhase(observed),
                Outcome::TerminalRestartRequired, reason);
            if (word_.compare_exchange_weak(observed, desired,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return;
        }
    }

    // Only a completed native publication followed by initializer phase may
    // seal a success receipt. This does NOT run initializers or enable Shadow.
    bool Complete() noexcept
    {
        uint32_t observed = word_.load(std::memory_order_acquire);
        if (ExtractOutcome(observed) != Outcome::InProgress)
            return false;
        if (ExtractPhase(observed) != Phase::InitializersRunning)
        {
            MarkTerminal(Reason::InvalidPhaseTransition);
            return false;
        }
        const uint32_t desired = Pack(Phase::InitializersRunning,
            Outcome::Complete, Reason::None);
        if (word_.compare_exchange_strong(observed, desired,
                std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
        MarkTerminal(Reason::InvalidPhaseTransition);
        return false;
    }

    bool MayReturnValidated() const noexcept
    {
        return Read().outcome == Outcome::RejectedBeforeCodec;
    }
    bool RestartRequired() const noexcept
    {
        return Read().outcome == Outcome::TerminalRestartRequired;
    }
    bool IsComplete() const noexcept
    {
        return Read().outcome == Outcome::Complete;
    }

    // Exposes the target-specific atomic implementation property; noexcept
    // alone does NOT imply lock-free or bounded-time metadata publication.
    // D2/D3 must explicitly admit or reject a non-lock-free native target.
    bool IsLockFreeOnThisTarget() const noexcept
    {
        return word_.is_lock_free();
    }

private:
    static constexpr uint32_t kByteMask = 0xffu;
    static constexpr uint32_t kOutcomeShift = 8;
    static constexpr uint32_t kReasonShift = 16;

    static constexpr uint32_t Pack(Phase phase, Outcome outcome, Reason reason) noexcept
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(phase)) |
               (static_cast<uint32_t>(static_cast<uint8_t>(outcome)) << kOutcomeShift) |
               (static_cast<uint32_t>(static_cast<uint8_t>(reason)) << kReasonShift);
    }
    static Phase ExtractPhase(uint32_t word) noexcept
    {
        return static_cast<Phase>(word & kByteMask);
    }
    static Outcome ExtractOutcome(uint32_t word) noexcept
    {
        return static_cast<Outcome>((word >> kOutcomeShift) & kByteMask);
    }

    std::atomic<uint32_t> word_;
};

}}} // il2cpp::vm::assembly_shadow_publication
