// Test-only engine-originated reporting witness. Not a production recovery API.
// The validation build explicitly sets HYBRIDCLR_R03_RUNTIME_PROBE=1. A native
// observer persists evidence after an uncaught managed startup exception, then
// terminates this disposable test process without invoking managed shutdown.
#include "il2cpp-config.h"
#if HYBRIDCLR_R03_RUNTIME_PROBE
#include "vm/AssemblyShadow.h"
// Complete Il2CppException/Il2CppString types are required in the OFF build too.
// TerminalReporting.h includes this only when the feature is enabled.
#include "il2cpp-object-internals.h"
#include "vm/AssemblyShadowTerminalReporting.h"
#include "vm/InternalCalls.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace {
std::atomic<int32_t> s_engineLoggerCalls{0};
std::atomic<bool> s_engineWitnessArmed{false};
// Prepared from the real Unity native icall while healthy. This is NOT an
// exception to the post-terminal managed method guard, and not a production
// replacement for Unity owner lifecycle policy.
using OwnerQuit = void (*)(int32_t);
OwnerQuit s_ownerQuit = nullptr;
char s_expectedSentinel[64] = {};
std::atomic<bool> s_ownerQuitRequested{false};
std::atomic<int32_t> s_ownerQuitCalls{0};

std::atomic<bool> s_ownerQuitReturned{false};
std::atomic<uint64_t> s_reportsBefore{0};
std::atomic<int64_t> s_firstSentinelNs{0};
std::atomic<bool> s_recoveryReady{false};
std::atomic<int32_t> s_terminalState{0};
std::string s_originalRecovery;
std::atomic<int> s_reportWriter{0};
FILE* s_reportStream = nullptr;
int s_reportFd = -1;
char s_runCopy[33] = {};
char s_caseCopy[96] = {};
char s_shaCopy[65] = {};
int64_t NowNs() noexcept
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
void CaptureOriginalFailure() noexcept
{
    try
    {
        il2cpp::vm::AssemblyShadowState state;
        std::string recovery;
        const auto stateResult = il2cpp::vm::AssemblyShadow::GetState(state);
        const auto recoveryResult = il2cpp::vm::AssemblyShadow::GetRecoveryInfoJson(recovery);
        if (stateResult != il2cpp::vm::AssemblyShadowError::Success ||
            recoveryResult != il2cpp::vm::AssemblyShadowError::Success ||
            state != il2cpp::vm::AssemblyShadowState::FailedAfterCommit ||
            recovery.empty()) return;
        s_originalRecovery = recovery;
        s_terminalState.store(static_cast<int32_t>(state), std::memory_order_release);
        s_recoveryReady.store(true, std::memory_order_release);
    }
    catch (...) { /* An unavailable witness cannot authenticate the exit. */ }
}
// A normal C runtime exit invokes this registered callback; std::_Exit does
// not. It is evidence of process termination, not proof of full Unity teardown.
bool WriteReport(bool normalExit) noexcept
{
    int expected = 0;
    if (!s_reportWriter.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
        return false;
    if (!s_reportStream || s_reportFd < 0) return false;
    const int64_t first = s_firstSentinelNs.load(std::memory_order_acquire);
    const int64_t elapsed = first > 0 ? (NowNs() - first) / 1000000 : -1;
    const uint64_t completed =
        il2cpp::vm::assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire) -
        s_reportsBefore.load(std::memory_order_acquire);
    const bool requested = s_ownerQuitRequested.load(std::memory_order_acquire);
    const int32_t calls = s_ownerQuitCalls.load(std::memory_order_acquire);
    const int32_t logCalls = s_engineLoggerCalls.load(std::memory_order_acquire);
    const bool recoveryReady = s_recoveryReady.load(std::memory_order_acquire);
    const bool inTime = first > 0 && elapsed >= 0 && elapsed <= 500;
    const bool timedOut = !normalExit || !inTime;
    const int written = std::fprintf(s_reportStream,
        "{\"schemaVersion\":2,\"kind\":\"R03IREngineReportingV2\",\"result\":\"ObservedNotAccepted\","
        "\"runId\":\"%s\",\"caseId\":\"%s\",\"requestSha256\":\"%s\",\"processId\":%d,"
        "\"initialState\":6,\"finalState\":%d,\"completedReports\":%llu,"
        "\"ownerQuitRequested\":%s,\"ownerQuitNativeCalls\":%d,\"ownerQuitReturned\":%s,"
        "\"preManagedLoggerCalls\":1,\"finalManagedLoggerCalls\":%d,"
        "\"normalExitHookObserved\":%s,\"witnessExitMode\":\"%s\","
        "\"observationLimitMilliseconds\":500,\"observationElapsedMilliseconds\":%lld,"
        "\"timedOut\":%s,\"recovery\":%s,\"runtimeAcceptance\":false}\n",
        s_runCopy, s_caseCopy, s_shaCopy, static_cast<int>(::getpid()),
        s_terminalState.load(std::memory_order_acquire),
        static_cast<unsigned long long>(completed), requested ? "true" : "false",
        calls, s_ownerQuitReturned.load(std::memory_order_acquire) ? "true" : "false",
        logCalls, normalExit ? "true" : "false",
        normalExit ? "NativeAtexit" : "WatchdogForcedExit",
        static_cast<long long>(elapsed), timedOut ? "true" : "false",
        recoveryReady ? s_originalRecovery.c_str() : "null");
    const bool flushed = written > 0 && std::fflush(s_reportStream) == 0 &&
                         ::fsync(s_reportFd) == 0;
    const bool closed = std::fclose(s_reportStream) == 0;
    s_reportStream = nullptr;
    s_reportFd = -1;
    s_reportWriter.store(2, std::memory_order_release);
    return flushed && closed;
}
void NormalExitReport() noexcept
{
    if (!s_engineWitnessArmed.load(std::memory_order_acquire))
        return;
    const bool written = WriteReport(true);
    std::fprintf(stderr, "[R03EngineOwnerExit] nativeAtexit=true rawFlushed=%s\n",
                 written ? "true" : "false");
    std::fflush(stderr);
}
#endif // HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
bool Hex(const char* text, size_t size)
{
    if (!text || std::strlen(text) != size) return false;
    for (size_t i = 0; i < size; ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return false;
    return true;
}
bool Identifier(const char* text)
{
    if (!text || !*text || std::strlen(text) >= 96) return false;
    for (; *text; ++text)
        if (!( (*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') ||
               (*text >= '0' && *text <= '9') || *text == '-' || *text == '_')) return false;
    return true;
}
}

// Called from the native exact Unity unhandled-report path only after its
// completed count advances. All ordinary managed callbacks remain denied.
extern "C" IL2CPP_EXPORT void R03_IR_OnNativeEngineReportCompleted(
    const Il2CppException* error)
{
    if (!s_engineWitnessArmed.load(std::memory_order_acquire) ||
        !error || !error->message || !s_ownerQuit)
        return;
    const Il2CppString* message = error->message;
    const size_t expected = std::strlen(s_expectedSentinel);
    if (message->length != static_cast<int32_t>(expected)) return;
    for (size_t index = 0; index < expected; ++index)
        if (static_cast<uint16_t>(message->chars[index]) !=
            static_cast<unsigned char>(s_expectedSentinel[index]))
            return;
    if (s_ownerQuitRequested.exchange(true, std::memory_order_acq_rel))
        return; // The underlying report counter is never capped.
    s_firstSentinelNs.store(NowNs(), std::memory_order_release);
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    CaptureOriginalFailure();
#endif
    s_ownerQuitCalls.fetch_add(1, std::memory_order_release);
    std::fprintf(stderr, "[R03EngineOwnerQuit] source=UnityNativeIcall once=1 sentinelMatched=true\n");
    std::fflush(stderr);
    try
    {
        // Resolve while healthy, call only the native Unity owner request.
        // An icall return is NOT process-exit acknowledgement.
        s_ownerQuit(0);
        s_ownerQuitReturned.store(true, std::memory_order_release);
    }
    catch (...)
    {
        std::fputs("[R03EngineOwnerQuit] nativeQuitThrew=true\n", stderr);
        std::fflush(stderr);
    }
}

extern "C" IL2CPP_EXPORT void R03_IR_EngineLoggerCalled()
{
    s_engineLoggerCalls.fetch_add(1, std::memory_order_relaxed);
}

extern "C" IL2CPP_EXPORT void R03_IR_EngineFatal(int32_t phase)
{
    std::fprintf(stderr, "[R03EngineWitness] fatalPhase=%d\n", static_cast<int>(phase));
    std::fflush(stderr);
    std::_Exit(70); // Test process only; never an acceptance result.
}

extern "C" IL2CPP_EXPORT int32_t R03_IR_ArmEngineWitness(
    const char* output, const char* run, const char* caseId, const char* requestSha)
{
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
    using namespace il2cpp::vm;
    if (!output || output[0] != '/' || !Hex(run, 32) || !Hex(requestSha, 64) ||
        !Identifier(caseId) || s_engineLoggerCalls.load() != 1) return -1;
    AssemblyShadowState initial;
    if (AssemblyShadow::GetState(initial) != AssemblyShadowError::Success ||
        initial != AssemblyShadowState::Committed ||
        s_engineWitnessArmed.load(std::memory_order_acquire)) return -2;
    // Resolve the exact Player icall while healthy, not after terminal failure.
    const auto nativeQuit = il2cpp::vm::InternalCalls::Resolve(
        "UnityEngine.Application::Quit(System.Int32)");
    if (!nativeQuit) return -7;
    s_ownerQuit = reinterpret_cast<OwnerQuit>(nativeQuit);
    if (std::snprintf(s_expectedSentinel, sizeof(s_expectedSentinel),
                      "R03_ENGINE_UNHANDLED_%s", run) != 53) return -8;
    const int fd = ::open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return -3;
    FILE* stream = ::fdopen(fd, "w");
    if (!stream) { ::close(fd); return -4; }
    s_reportStream = stream;
    s_reportFd = fd;
    std::memcpy(s_runCopy, run, 33);
    std::strcpy(s_caseCopy, caseId);
    std::memcpy(s_shaCopy, requestSha, 65);
    s_reportsBefore.store(assembly_shadow_reporting::CompletedCounter().load(
        std::memory_order_acquire), std::memory_order_release);
    // Unlike the former observer's std::_Exit(0), only a normal process exit
    // can publish a positive exit-provenance signal. The native watchdog is
    // failure-only and keeps a complete raw diagnostic even on timeout.
    if (std::atexit(NormalExitReport) != 0)
    {
        std::fclose(stream);
        s_reportStream = nullptr;
        s_reportFd = -1;
        return -9;
    }
    s_engineWitnessArmed.store(true, std::memory_order_release);
    try
    {
        std::thread([]() noexcept {
            try
            {
                const uint64_t before = s_reportsBefore.load(std::memory_order_acquire);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire) == before &&
                       std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const bool observed =
                    assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire) > before;
                if (observed)
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                // A normal Unity-owner shutdown would already have reached
                // NormalExitReport. A watchdog must NEVER award success.
                if (s_reportWriter.load(std::memory_order_acquire) == 0)
                    WriteReport(false);
                std::_Exit(71);
            }
            catch (...)
            {
                std::fputs("[R03EngineWitness] native observer failed\n", stderr);
                std::fflush(stderr);
                std::_Exit(72);
            }
        }).detach();
        return 1;
    }
    catch (...)
    {
        s_engineWitnessArmed.store(false, std::memory_order_release);
        std::fclose(stream);
        s_reportStream = nullptr;
        s_reportFd = -1;
        return -5;
    }
#else
    (void)output; (void)run; (void)caseId; (void)requestSha;
    return -6;
#endif
}
#endif // HYBRIDCLR_R03_RUNTIME_PROBE
