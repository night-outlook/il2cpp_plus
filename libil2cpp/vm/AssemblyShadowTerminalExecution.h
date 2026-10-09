#pragma once
// R03 IR-R03-02: lock-free terminal business-entry decision.
// No runtime locks, allocation, metadata queries, managed exception or user callbacks.
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_terminal {

constexpr int32_t kDisabled = 0;
constexpr int32_t kFailed = 8;
constexpr int32_t kFailedAfterCommit = 9;

// Terminal facts can become observable before a concurrent failure writer
// publishes s_state. Disabled/OFF without a transaction is not poisoned.
inline bool TerminalForBusiness(int32_t state, bool durableFailure) noexcept
{
    return state == kFailed || state == kFailedAfterCommit ||
        (state != kDisabled && durableFailure);
}

// A diagnostic-construction scope never permits arbitrary business execution.
// Only a separately verified physical corlib exception .ctor can be admitted.
inline bool RejectMethod(int32_t state, bool durableFailure,
    bool constructingDiagnostic, bool verifiedFixedExceptionCtor) noexcept
{
    return TerminalForBusiness(state, durableFailure) &&
        !(constructingDiagnostic && verifiedFixedExceptionCtor);
}

}}} // namespace il2cpp::vm::assembly_shadow_terminal
