#pragma once
// R03 B2 D3: fail-closed disposition after the native batch callback returns
// false. Does not inspect or reverse Codec/Runtime publication. C++11, no heap,
// no managed code, no locks. Call only AFTER D2 native rollback scopes unwind.
//
// The sole permitted retryable refusal is a USED-CLOSURE rejection before
// either registry append. Caller must own and verify those source facts.
// Runtime pre-validation errors are deliberately terminal without such proof.
#include "AssemblyShadowPublicationPhaseReceipt.h"

namespace il2cpp { namespace vm { namespace assembly_shadow_publication {

enum class D3Disposition : uint8_t
{
    ProvenUsedRejection = 1,
    TerminalRestartRequired = 2,
};

inline D3Disposition ClassifyFailedNativeBatch(
    PublicationPhaseReceipt& attempt,
    bool usedClosureDenied,
    bool usageHeld,
    bool runtimePublicationFailed) noexcept
{
    const Snapshot observed = attempt.Read();
    if (usedClosureDenied &&
        observed.phase == Phase::Prepared &&
        observed.outcome == Outcome::InProgress &&
        !usageHeld && !runtimePublicationFailed)
    {
        // Actual TryBeginPublication refuses before either append and releases
        // its usage lease. Physical/CLI scopes unwind without appended values.
        const PreCodecCleanupProof proof = {true, true, true, true};
        if (attempt.RejectBeforeCodec(proof))
            return D3Disposition::ProvenUsedRejection;
    }

    // Distinguish already-entered Codec from otherwise missing proof. BOTH
    // dispositions are terminal: no unknown Codec outcome may retry.
    attempt.MarkTerminal(
        static_cast<uint8_t>(observed.phase) >=
            static_cast<uint8_t>(Phase::CodecAttempted)
            ? Reason::CodecOutcomeUnknown
            : Reason::MissingCleanupProof);
    return D3Disposition::TerminalRestartRequired;
}

}}} // il2cpp::vm::assembly_shadow_publication
