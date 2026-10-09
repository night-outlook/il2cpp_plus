#include "vm/AssemblyShadowTerminalExecution.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <initializer_list>
#include <cstdlib>
#include <thread>

using namespace il2cpp::vm::assembly_shadow_terminal;

static int checks = 0;
static void Check(bool condition) { ++checks; assert(condition); if (!condition) std::abort(); }

int main()
{
    // Ordinary feature OFF, no transaction. Diagnostic constructor scope alone
    // never changes the ordinary execution policy.
    for (bool durable : {false, true})
    {
        Check(!TerminalForBusiness(kDisabled, durable));
        Check(!RejectMethod(kDisabled, durable, false, false));
        Check(!RejectMethod(kDisabled, durable, true, false));
    }
    for (int32_t state : {1, 2, 3, 4, 5, 6, 7})
    {
        Check(!TerminalForBusiness(state, false));
        Check(TerminalForBusiness(state, true)); // races ahead of state store
        Check(RejectMethod(state, true, false, false));
        Check(RejectMethod(state, true, true, false));
        Check(!RejectMethod(state, true, true, true));
    }
    for (int32_t terminal : {kFailed, kFailedAfterCommit})
    {
        for (bool durable : {false, true})
        {
            Check(TerminalForBusiness(terminal, durable));
            Check(RejectMethod(terminal, durable, false, false));
            Check(RejectMethod(terminal, durable, true, false));
            Check(RejectMethod(terminal, durable, false, true));
            Check(!RejectMethod(terminal, durable, true, true));
        }
    }
    std::atomic<int> state{kDisabled};
    std::atomic<bool> durable{false};
    std::atomic<int> sideEffects{0};
    auto business = [&]() {
        if (RejectMethod(state.load(std::memory_order_acquire),
                durable.load(std::memory_order_acquire), false, false)) return false;
        ++sideEffects;
        return true;
    };
    Check(business()); // positive control before terminal
    Check(sideEffects == 1);
    durable.store(true, std::memory_order_release);
    state.store(kFailedAfterCommit, std::memory_order_release);
    Check(!business()); // poison cannot continue a valid method body
    Check(sideEffects == 1);
    Check(RejectMethod(state.load(), true, true, false)); // no blanket diagnostic scope
    Check(!RejectMethod(state.load(), true, true, true)); // only fixed constructor
    std::thread other([&]() {
        Check(!business()); // process-wide, not thread-local poison
    });
    other.join();
    Check(sideEffects == 1);
    std::cout << "{\"kind\":\"R03TerminalExecutionPolicyHeaderTest\",\"result\":\"Passed\",\"checks\":"
              << checks << ",\"unityRun\":false,\"runtimeAcceptance\":false}" << std::endl;
}
