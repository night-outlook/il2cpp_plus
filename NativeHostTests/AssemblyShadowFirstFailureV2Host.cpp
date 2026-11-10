// Real host compile and execution of the exact B1 native header.
// Deliberately does not mock AssemblyShadow.cpp or claim Unity/VM evidence.
#include "../libil2cpp/vm/AssemblyShadowFirstFailureV2.h"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <cstdio>

using namespace il2cpp::vm::assembly_shadow_first_failure_v2;
static std::atomic<bool> counting(false);
static std::atomic<int> allocations(0);
void* operator new(std::size_t n) {
    if (counting.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    if (counting.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
#if __cplusplus >= 201402L
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

static bool Normal() {
    FirstErrorV2 owner;
    Snapshot out{};
    if (owner.status() != State::Empty || owner.Read(out)) return false;
    char reason[] = "ShadowCapturedBaselineType Context=method_inst TypePath=/arg[0]";
    const std::size_t n = sizeof(reason) - 1;
    if (!owner.TryFreeze(15, Origin::Guard, reason, n) ||
        owner.status() != State::Frozen || !owner.Read(out) ||
        out.origin != Origin::Guard || out.code != 15 || out.length != n)
        return false;
    reason[0] = 'Z'; // Snapshot owns an exact copy of mutable source.
    if (std::strcmp(out.reason,
                    "ShadowCapturedBaselineType Context=method_inst TypePath=/arg[0]"))
        return false;
    if (owner.TryFreeze(19, Origin::InterpreterInitializer, "replacement", 11) ||
        !owner.Read(out) || out.code != 15 || out.origin != Origin::Guard)
        return false;
    return true;
}

static bool Origins() {
    struct Case {
        std::int32_t code;
        Origin origin;
        const char* reason;
    };
    const Case scenarios[] = {
        {19, Origin::InterpreterInitializer, "R03IR.InitializerFailure.AfterPublication.V1"},
        {15, Origin::LateBaselineUse, "Physical baseline use after publication"},
        {14, Origin::ReferenceViolation, "ReferenceEscapesClosure Site=probe"},
        {20, Origin::UnexpectedFailure, "Unexpected native mutation failure requires restart."},
        {20, Origin::TransactionTerminal, "Original terminal transaction detail"}
    };
    for (const auto& c : scenarios) {
        FirstErrorV2 record;
        Snapshot out{};
        if (!record.TryFreeze(c.code, c.origin, c.reason, std::strlen(c.reason)) ||
            !record.Read(out) || out.code != c.code || out.origin != c.origin ||
            std::strcmp(out.reason, c.reason))
            return false;
    }
    return true;
}

static bool InvalidFirst() {
    const char invalidUtf8[] = {char(0xe2), char(0x82)};
    const char embeddedNul[] = {'a','\0','b'};
    const char overlong[] = {char(0xc0),char(0xaf)};
    const char surrogate[] = {char(0xed),char(0xa0),char(0x80)};
    const char* bad[] = {invalidUtf8, embeddedNul, overlong, surrogate};
    const std::size_t counts[] = {sizeof invalidUtf8,sizeof embeddedNul,
                                   sizeof overlong,sizeof surrogate};
    for (unsigned i = 0; i < 4; ++i) {
        FirstErrorV2 slot;
        Snapshot out{};
        if (slot.TryFreeze(15, Origin::Guard, bad[i], counts[i]) ||
            slot.status() != State::Invalid || slot.Read(out) ||
            slot.TryFreeze(19, Origin::InterpreterInitializer, "later", 5))
            return false;
    }
    FirstErrorV2 noReason, noCode, noOrigin;
    if (noReason.TryFreeze(19, Origin::Guard, nullptr, 5) ||
        noCode.TryFreeze(0, Origin::Guard, "raw", 3) ||
        noOrigin.TryFreeze(15, static_cast<Origin>(0), "raw", 3) ||
        noReason.status() != State::Invalid ||
        noCode.status() != State::Invalid ||
        noOrigin.status() != State::Invalid)
        return false;
    char tooLong[kReasonLimit + 1];
    std::memset(tooLong, 'q', sizeof tooLong);
    FirstErrorV2 over;
    Snapshot out{};
    if (over.TryFreeze(15, Origin::Guard, tooLong, sizeof tooLong) ||
        over.status() != State::Invalid || over.Read(out))
        return false;
    FirstErrorV2 maximum;
    if (!maximum.TryFreeze(15, Origin::Guard, tooLong, kReasonLimit) ||
        !maximum.Read(out) || out.length != kReasonLimit ||
        out.reason[kReasonLimit] != '\0') return false;
    return true;
}

static bool RacingWriters() {
    FirstErrorV2 latch;
    static const unsigned kWriters = 16;
    std::atomic<unsigned> winners(0);
    std::thread threads[kWriters];
    for (unsigned i = 0; i < kWriters; ++i) {
        threads[i] = std::thread([&latch,&winners,i] {
            char buffer[] = "writer0";
            buffer[6] = static_cast<char>('A' + i);
            if (latch.TryFreeze(100 + static_cast<std::int32_t>(i),
                                Origin::Guard, buffer, sizeof(buffer)-1))
                winners.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (unsigned i = 0; i < kWriters; ++i) threads[i].join();
    Snapshot out{};
    if (winners.load() != 1 || !latch.Read(out) ||
        out.code < 100 || out.code >= 116 || out.length != 7 ||
        std::memcmp(out.reason, "writer", 6))
        return false;
    return out.reason[6] == static_cast<char>('A' + out.code - 100);
}

static bool NoAlloc() {
    FirstErrorV2 latch;
    Snapshot out{};
    static const char reason[] = "R03 first native failure without heap allocation";
    allocations.store(0, std::memory_order_relaxed);
    counting.store(true, std::memory_order_release);
    const bool yes = latch.TryFreeze(19, Origin::InterpreterInitializer,
                                     reason, sizeof reason - 1);
    const bool read = latch.Read(out);
    const bool rejected = !latch.TryFreeze(20, Origin::UnexpectedFailure,
                                            reason, sizeof reason - 1);
    counting.store(false, std::memory_order_release);
    return yes && read && rejected && out.code == 19 &&
        allocations.load(std::memory_order_acquire) == 0;
}

static bool ReadingWhileWriting() {
    FirstErrorV2 latch;
    char raw[kReasonLimit];
    std::memset(raw, 'z', sizeof raw);
    std::atomic<bool> start(false);
    std::atomic<bool> ready(false);
    std::thread reader([&] {
        start.store(true, std::memory_order_release);
        for (int i = 0; i < 100000 && latch.status() != State::Frozen; ++i) {
            Snapshot value{};
            if (latch.Read(value) && (value.length != kReasonLimit ||
                value.reason[kReasonLimit] != '\0'))
                return;
            std::this_thread::yield();
        }
        ready.store(true, std::memory_order_release);
    });
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    const bool success = latch.TryFreeze(15, Origin::Guard, raw, sizeof raw);
    reader.join();
    Snapshot out{};
    return success && ready.load(std::memory_order_acquire) &&
        latch.Read(out) && out.length == kReasonLimit &&
        out.reason[kReasonLimit-1] == 'z';
}

int main() {
    if (!Normal()) return 11;
    if (!Origins()) return 12;
    if (!InvalidFirst()) return 13;
    if (!RacingWriters()) return 14;
    if (!NoAlloc()) return 15;
    if (!ReadingWhileWriting()) return 16;
    static const char result[] =
        "{\"kind\":\"R03NativeB1FirstFailureSourceHost\","
        "\"result\":\"PassedCxx11HostSource_NotVmOrUnity\","
        "\"nativeOriginIntegratedSource\":true,"
        "\"nativeOriginObservedInPlayer\":false,"
        "\"nativeV2TransportImplemented\":false,"
        "\"wholeVmEpochAuthenticated\":false,"
        "\"runtimeAcceptance\":false,\"R03Accepted\":false,\"H2Passed\":false}\n";
    return std::fwrite(result, 1, sizeof(result)-1, stdout) ==
        sizeof(result)-1 ? 0 : 17;
}
