// Test-only engine-originated reporting witness. Not a production recovery API.
// The validation build explicitly sets HYBRIDCLR_R03_RUNTIME_PROBE=1. A native
// observer persists evidence after an uncaught managed startup exception, then
// terminates this disposable test process without invoking managed shutdown.
#include "il2cpp-config.h"
#if HYBRIDCLR_R03_RUNTIME_PROBE
#include "vm/AssemblyShadow.h"
#include "vm/AssemblyShadowTerminalReporting.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

namespace {
std::atomic<int32_t> s_engineLoggerCalls{0};
std::atomic<bool> s_engineWitnessArmed{false};
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
        initial != AssemblyShadowState::Committed || s_engineWitnessArmed.exchange(true)) return -2;
    // No overwrite/following an output symlink. Parent/source isolation is also
    // verified by the host before the disposable Player is launched.
    const int fd = ::open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return -3;
    FILE* stream = ::fdopen(fd, "w");
    if (!stream) { ::close(fd); return -4; }
    char runCopy[33], caseCopy[96], shaCopy[65];
    std::memcpy(runCopy, run, 33);
    std::strcpy(caseCopy, caseId);
    std::memcpy(shaCopy, requestSha, 65);
    const uint64_t before = assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire);
    try
    {
        std::thread([stream, fd, before, runCopy, caseCopy, shaCopy]() noexcept {
            try
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire) == before &&
                       std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const bool observed = assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire) > before;
                // Detect immediate engine re-entry/stack failure after reporting;
                // this bounded observation is not a production-duration SLA.
                if (observed) std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const uint64_t completed = assembly_shadow_reporting::CompletedCounter().load(std::memory_order_acquire) - before;
                const int32_t calls = s_engineLoggerCalls.load(std::memory_order_acquire);
                AssemblyShadowState state;
                AssemblyShadow::GetState(state);
                std::string recovery;
                const auto recoveryCode = AssemblyShadow::GetRecoveryInfoJson(recovery);
                const bool valid = observed && completed <= 64 && calls == 1 &&
                    state == AssemblyShadowState::FailedAfterCommit && recoveryCode == AssemblyShadowError::Success;
                const int written = std::fprintf(stream,
                    "{\"schemaVersion\":1,\"kind\":\"R03IREngineReportingV1\",\"result\":\"ObservedNotAccepted\","
                    "\"runId\":\"%s\",\"caseId\":\"%s\",\"requestSha256\":\"%s\",\"processId\":%d,"
                    "\"initialState\":6,\"finalState\":%d,\"completedReports\":%llu,"
                    "\"preManagedLoggerCalls\":1,\"finalManagedLoggerCalls\":%d,\"timedOut\":%s,"
                    "\"observationMilliseconds\":500,\"recovery\":%s,\"runtimeAcceptance\":false}\n",
                    runCopy, caseCopy, shaCopy, static_cast<int>(::getpid()), static_cast<int32_t>(state),
                    static_cast<unsigned long long>(completed), calls, observed ? "false" : "true",
                    recovery.empty() ? "null" : recovery.c_str());
                const bool flushed = written > 0 && std::fflush(stream) == 0 && ::fsync(fd) == 0;
                const bool closed = std::fclose(stream) == 0;
                std::_Exit(valid && flushed && closed ? 0 : 71);
            }
            catch (...) { std::fputs("[R03EngineWitness] native observer failed\n", stderr); std::fflush(stderr); std::_Exit(72); }
        }).detach();
        return 1;
    }
    catch (...) { std::fclose(stream); return -5; }
#else
    (void)output; (void)run; (void)caseId; (void)requestSha;
    return -6;
#endif
}
#endif // HYBRIDCLR_R03_RUNTIME_PROBE
