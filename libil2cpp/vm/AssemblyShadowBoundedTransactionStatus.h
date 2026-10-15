#pragma once
// P1 limited LIVE source acquisition output: bounded transaction STATUS only.
// This is neither a complete general/exec diagnostic census nor a Player wire.
// Caller constructs this object while healthy and gives one thread exclusive
// access. The VM source adapter must hold the transaction mutex while Begin()
// copies strings and release it BEFORE ObserveFirstFailure/Seal.
//
// No string/view/pointer from the VM survives in this object. Failure is
// permanent: no truncation, retry, partial success, or implicit epoch claim.
// Independent assembly/usage/class/R02 source generations remain unverified.
#include "AssemblyShadowTypes.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_transaction {

using assembly_shadow_diagnostic::QuotedText;
enum class Error : uint8_t
{
    None, WrongPhase, MissingInput, AliasedInput,
    DetailLimit, BuildIdLimit, PatchIdLimit, SourceChanged
};

struct Input
{
    AssemblyShadowState state=AssemblyShadowState::Disabled;
    AssemblyShadowError lastError=AssemblyShadowError::Success;
    QuotedText detail{}, baselineBuildId{}, patchId{};
    uint64_t generation=0, retainedBytes=0;
    size_t expected=0, staged=0;
};

struct View
{
    AssemblyShadowState state=AssemblyShadowState::Disabled;
    AssemblyShadowError lastError=AssemblyShadowError::Success;
    QuotedText detail{}, baselineBuildId{}, patchId{};
    uint64_t generation=0, retainedBytes=0;
    size_t expected=0, staged=0;
};

class Capture final
{
public:
    static constexpr size_t kMaxDetail=32768;
    static constexpr size_t kMaxBuildId=8192;
    static constexpr size_t kMaxPatchId=8192;
    static constexpr size_t kMaxObjectBytes=kMaxDetail+kMaxBuildId+kMaxPatchId+128;
private:
    enum class Phase:uint8_t { New, Base, FailureObserved, Sealed, Failed };
    char detail_[kMaxDetail]{};
    char build_[kMaxBuildId]{};
    char patch_[kMaxPatchId]{};
    uint32_t detailLength_=0, buildLength_=0, patchLength_=0;
    AssemblyShadowState state_=AssemblyShadowState::Disabled;
    AssemblyShadowError lastError_=AssemblyShadowError::Success;
    uint64_t generation_=0, retainedBytes_=0;
    size_t expected_=0, staged_=0;
    Phase phase_=Phase::New;
    Error error_=Error::None;
    bool Fail(Error why) noexcept
    {
        if (error_==Error::None) error_=why;
        phase_=Phase::Failed;
        return false;
    }
    bool Copy(char* dst,size_t max,uint32_t& length,QuotedText value,Error over) noexcept
    {
        if (!value.data) return Fail(Error::MissingInput);
        const uintptr_t p=reinterpret_cast<uintptr_t>(value.data);
        const uintptr_t a=reinterpret_cast<uintptr_t>(detail_);
        if (value.bytes>UINTPTR_MAX-p || kMaxObjectBytes>UINTPTR_MAX-a)
            return Fail(Error::MissingInput);
        if (value.bytes && p<a+kMaxObjectBytes && a<p+value.bytes)
            return Fail(Error::AliasedInput);
        if (value.bytes>max) return Fail(over);
        if (value.bytes) std::memcpy(dst,value.data,value.bytes);
        length=static_cast<uint32_t>(value.bytes);
        return true;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    Error FirstError() const noexcept {return error_;}
    bool Complete() const noexcept {return phase_==Phase::Sealed && error_==Error::None;}

    // Call under the SAME existing transaction lock as the old status copy.
    // The adapter supplies precedence-resolved, already sampled status fields.
    bool Begin(const Input& value) noexcept
    {
        if (phase_!=Phase::New || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if (!Copy(detail_,kMaxDetail,detailLength_,value.detail,Error::DetailLimit) ||
            !Copy(build_,kMaxBuildId,buildLength_,value.baselineBuildId,Error::BuildIdLimit) ||
            !Copy(patch_,kMaxPatchId,patchLength_,value.patchId,Error::PatchIdLimit))
            return false;
        state_=value.state;lastError_=value.lastError;
        generation_=value.generation;retainedBytes_=value.retainedBytes;
        expected_=value.expected;staged_=value.staged;
        phase_=Phase::Base;
        return true;
    }
    // Must be called EXACTLY once AFTER transaction lock release, with the
    // independently acquired immutable TypeFailure pointer (when present).
    // Presence of a type failure overrides the transaction's own error/detail
    // and chooses Failed/FailedAfterCommit from current physical active state.
    // A changed source condition must fail closed, never be hidden.
    bool ObserveFirstFailure(bool sourceStable,bool present,
        AssemblyShadowError code,QuotedText detail,bool active) noexcept
    {
        if (phase_!=Phase::Base || error_!=Error::None) return Fail(Error::WrongPhase);
        if (!sourceStable) return Fail(Error::SourceChanged);
        if (present)
        {
            if (!Copy(detail_,kMaxDetail,detailLength_,detail,Error::DetailLimit))
                return false;
            state_=active ? AssemblyShadowState::FailedAfterCommit : AssemblyShadowState::Failed;
            lastError_=code;
        }
        phase_=Phase::FailureObserved;
        return true;
    }
    bool Seal() noexcept
    {
        if (phase_!=Phase::FailureObserved || error_!=Error::None)
            return Fail(Error::WrongPhase);
        phase_=Phase::Sealed;
        return true;
    }
    bool Get(View& output) const noexcept
    {
        if (!Complete()) return false;
        output.state=state_;output.lastError=lastError_;
        output.detail={detail_,detailLength_};
        output.baselineBuildId={build_,buildLength_};
        output.patchId={patch_,patchLength_};
        output.generation=generation_;output.retainedBytes=retainedBytes_;
        output.expected=expected_;output.staged=staged_;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaxObjectBytes,
              "bounded source status storage unexpectedly grew");
}}} // il2cpp::vm::assembly_shadow_bounded_transaction
