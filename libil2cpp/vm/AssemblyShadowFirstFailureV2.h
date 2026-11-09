#pragma once
// R03 B1: bounded, process-lifetime originating native first-error record.
// The VM calls TryFreeze only at an existing failure producer. The record is
// NOT a substitute for the existing recovery policy and does not prove a
// coherent whole-VM epoch or authorize a Player/release.
// All storage is inline; capture/read require C++11 only, no locks, heap,
// managed API, logging, metadata resolution, formatting, or file access.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_first_failure_v2 {

static constexpr std::size_t kReasonLimit = 4096;

enum class Origin : std::uint8_t {
    Guard = 1,
    InterpreterInitializer = 2,
    LateBaselineUse = 3,
    ReferenceViolation = 4,
    UnexpectedFailure = 5,
    TransactionTerminal = 6
};
enum class State : std::uint8_t { Empty = 0, Writing = 1, Frozen = 2, Invalid = 3 };

struct Snapshot final {
    std::int32_t code = 0;
    Origin origin = Origin::Guard;
    std::size_t length = 0;
    char reason[kReasonLimit + 1] = {};
};

class FirstErrorV2 final {
    std::atomic<State> state_{State::Empty};
    std::int32_t code_ = 0;
    Origin origin_ = Origin::Guard;
    std::size_t length_ = 0;
    char reason_[kReasonLimit + 1] = {};

    static bool ValidUtf8(const char* raw, std::size_t count) noexcept {
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(raw);
        for (std::size_t i = 0; i < count;) {
            std::uint32_t c = p[i++];
            if (c < 0x80) { if (!c) return false; continue; }
            std::uint32_t minimum = 0;
            unsigned n = 0;
            if (c >= 0xc2 && c <= 0xdf) { n = 1; minimum = 0x80; c &= 0x1f; }
            else if (c >= 0xe0 && c <= 0xef) { n = 2; minimum = 0x800; c &= 0x0f; }
            else if (c >= 0xf0 && c <= 0xf4) { n = 3; minimum = 0x10000; c &= 0x07; }
            else return false;
            if (count - i < n) return false;
            for (unsigned j = 0; j < n; ++j) {
                const std::uint8_t b = p[i++];
                if ((b & 0xc0) != 0x80) return false;
                c = (c << 6) | (b & 0x3f);
            }
            if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
                return false;
        }
        return true;
    }

    static bool ValidOrigin(Origin origin) noexcept {
        return origin >= Origin::Guard && origin <= Origin::TransactionTerminal;
    }

public:
    FirstErrorV2() noexcept = default;
    FirstErrorV2(const FirstErrorV2&) = delete;
    FirstErrorV2& operator=(const FirstErrorV2&) = delete;

    State status() const noexcept { return state_.load(std::memory_order_acquire); }

    bool TryFreeze(std::int32_t code, Origin origin, const char* raw,
                   std::size_t count) noexcept {
        State expected = State::Empty;
        if (!state_.compare_exchange_strong(expected, State::Writing,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            return false;
        // A malformed original first event permanently invalidates this
        // diagnostic record; never replace it with a later convenient error.
        if (code <= 0 || !ValidOrigin(origin) || !raw || !count ||
            count > kReasonLimit || !ValidUtf8(raw, count)) {
            state_.store(State::Invalid, std::memory_order_release);
            return false;
        }
        code_ = code;
        origin_ = origin;
        length_ = count;
        std::memcpy(reason_, raw, count);
        reason_[count] = '\0';
        state_.store(State::Frozen, std::memory_order_release);
        return true;
    }

    bool Read(Snapshot& out) const noexcept {
        if (state_.load(std::memory_order_acquire) != State::Frozen)
            return false;
        // Writer release-publishes immutable fields. No shared writes follow.
        out.code = code_;
        out.origin = origin_;
        out.length = length_;
        std::memcpy(out.reason, reason_, length_ + 1);
        return true;
    }
};
static_assert(sizeof(FirstErrorV2) < 4608, "B1 fixed first-error storage bound");

}}} // namespace il2cpp::vm::assembly_shadow_first_failure_v2
