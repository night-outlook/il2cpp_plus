#pragma once

// This header intentionally has no IL2CPP include dependencies.  The recovery
// decision is a small, pure boundary contract shared by production code and
// native tests.  The integer values below are the ABI values of
// AssemblyShadowState and AssemblyShadowError; the integration point must
// static_assert that its native enums still agree with them.

#include <stdint.h>

namespace il2cpp { namespace vm {

namespace assembly_shadow_recovery
{

enum RecoveryState : int32_t
{
    StateDisabled = 0,
    StateCandidatesRegistered = 1,
    StateStaging = 2,
    StateStaged = 3,
    StateValidated = 4,
    StateCommitting = 5,
    StateCommitted = 6,
    StateAborted = 7,
    StateFailed = 8,
    StateFailedAfterCommit = 9,
};

enum RecoveryDisposition : int32_t
{
    RestartRequired = 0,
    CorrectInputOrAbort = 1,
    AbortRequired = 2,
    BaselineEligibleAfterAbort = 3,
    ActiveShadow = 4,
    BaselineUnselected = 5,
};

// AssemblyShadowError values.  Values above 24 and negative values are
// intentionally outside this domain: an unknown value must require a restart.
enum RecoveryError : int32_t
{
    ErrorSuccess = 0,
    ErrorFeatureDisabled = 1,
    ErrorInvalidState = 2,
    ErrorInvalidArgument = 3,
    ErrorCandidateNotRegistered = 4,
    ErrorDuplicateAssemblyName = 5,
    ErrorBaselineAssemblyNotFound = 6,
    ErrorBaselineBuildMismatch = 7,
    ErrorAssemblyNameMismatch = 8,
    ErrorBadImage = 9,
    ErrorUnsupportedAssembly = 10,
    ErrorClosureMemberMissing = 11,
    ErrorUnexpectedClosureMember = 12,
    ErrorReferenceResolutionFailed = 13,
    ErrorReferenceEscapesClosure = 14,
    ErrorBaselineAlreadyUsed = 15,
    ErrorResourceAbiMismatch = 16,
    ErrorRuntimeAbiMismatch = 17,
    ErrorAlreadyCommitted = 18,
    ErrorModuleInitializerFailed = 19,
    ErrorInternal = 20,
    ErrorBaselineMethodExecution = 21,
    ErrorCapabilityUnavailable = 22,
    // R01 budget contract values; these are intentionally outside the current
    // legacy enum until the owning runtime integration adds them.
    ErrorCapacityExceeded = 23,
    ErrorBudgetMismatch = 24,
};

struct RecoveryInput
{
    // State values are interpreted as the integer domain above so this helper
    // can be compiled without AssemblyShadowTypes.h.
    int32_t state;
    bool published;
    bool terminalFailure;
    bool poisoned;
    bool knownBaselineUseRejection;
    // This is the mutable last-error slot.  terminalFailure and
    // knownBaselineUseRejection represent durable facts; state-specific
    // recovery rules decide whether each durable fact is fatal or merely the
    // reason an Abort path was required.
    int32_t lastError;
};

struct RecoveryDecision
{
    RecoveryDisposition disposition;
    bool abortAllowed;
    bool requireStartupValidation;
};

inline bool IsKnownCorrectableError(int32_t error)
{
    switch (error)
    {
        case ErrorInvalidArgument:
        case ErrorDuplicateAssemblyName:
        case ErrorBadImage:
        case ErrorUnsupportedAssembly:
        case ErrorClosureMemberMissing:
        case ErrorUnexpectedClosureMember:
        case ErrorReferenceResolutionFailed:
        case ErrorReferenceEscapesClosure:
        case ErrorCapabilityUnavailable:
        case ErrorCapacityExceeded:
        case ErrorBudgetMismatch:
            return true;
        default:
            return false;
    }
}

inline bool IsKnownError(int32_t error)
{
    return error >= ErrorSuccess && error <= ErrorBudgetMismatch;
}

inline RecoveryDecision Restart()
{
    RecoveryDecision result = { RestartRequired, false, true };
    return result;
}

inline RecoveryDecision BaselineUnselectedResult()
{
    RecoveryDecision result = { BaselineUnselected, false, true };
    return result;
}

inline RecoveryDecision CorrectInputResult()
{
    RecoveryDecision result = { CorrectInputOrAbort, true, true };
    return result;
}

inline RecoveryDecision AbortResult()
{
    RecoveryDecision result = { AbortRequired, true, true };
    return result;
}

inline RecoveryDecision ActiveResult()
{
    RecoveryDecision result = { ActiveShadow, false, false };
    return result;
}

inline RecoveryDecision BaselineAfterAbortResult()
{
    RecoveryDecision result = { BaselineEligibleAfterAbort, false, true };
    return result;
}

// Classify the state of one activation attempt.  The function has no side
// effects and never elects a baseline by itself.  CorrectInputOrAbort only
// describes a caller action: a coordinator must correct the input or call
// AbortTransaction and then perform its own startup validation.
inline RecoveryDecision Classify(const RecoveryInput& input)
{
    // Durable terminal failure cannot be erased by AbortTransaction,
    // CommitTransaction, or a later success written to lastError.
    if (input.terminalFailure || input.poisoned)
        return Restart();

    // Publication is a one-way boundary.  Any state/publication combination
    // other than a published Committed snapshot is inconsistent and must be
    // treated as a restart condition.
    if ((input.published && input.state != StateCommitted) ||
        (!input.published && input.state == StateCommitted))
        return Restart();

    // An unknown error is itself an unknown native failure.  Check it before
    // state-specific handling so a future enum value cannot accidentally
    // inherit a permissive recovery path.
    if (!IsKnownError(input.lastError) || input.lastError == ErrorInternal)
        return Restart();

    switch (input.state)
    {
        case StateDisabled:
        case StateCandidatesRegistered:
            // No transaction needs Abort before Begin.  The coordinator still
            // has to validate and select the complete startup baseline.
            return BaselineUnselectedResult();

        case StateStaging:
        case StateStaged:
        case StateValidated:
            // BaselineAlreadyUsed is a safety rejection.  It may not be
            // silently treated as an ordinary bad input.
            if (input.knownBaselineUseRejection || input.lastError == ErrorBaselineAlreadyUsed)
                return AbortResult();
            if (IsKnownCorrectableError(input.lastError) || input.lastError == ErrorSuccess)
                return CorrectInputResult();
            // InternalError and every future non-correctable error fail closed.
            return Restart();

        case StateCommitting:
            // The publication boundary cannot be inferred from this mutable
            // state.  Never offer an intermediate baseline while committing.
            return Restart();

        case StateCommitted:
            // The publication checks above establish the healthy snapshot
            // shape.  Mutable lastError may contain a rejected Abort/Commit
            // result; it cannot downgrade a healthy active world.  A durable
            // baseline-use observation is different and remains fatal.
            return !input.knownBaselineUseRejection ? ActiveResult() : Restart();

        case StateAborted:
            // A clean abort has no active published world; baseline remains a
            // coordinator decision and needs startup validation.  A rejected
            // pre-commit baseline use is the reason Abort was required and
            // does not invalidate a later clean Aborted state.
            return BaselineAfterAbortResult();

        case StateFailed:
        case StateFailedAfterCommit:
            return Restart();

        default:
            return Restart();
    }
}

} // namespace assembly_shadow_recovery

}} // namespace il2cpp::vm
