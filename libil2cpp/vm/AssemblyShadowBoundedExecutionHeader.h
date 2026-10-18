#pragma once
// R03 P1 INACTIVE: bounded execution-diagnostic SCALARS only.
// A caller must preallocate this object while healthy and single-own it.
// This captures no execution class list, type key, R02 metric set, VM mutex,
// GC handles, file, Player call or coherent all-VM snapshot.
#include "AssemblyShadowExecutionSerializer.h"
#include "AssemblyShadowDiagnostics.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_execution_header {

enum class Error : uint8_t
{
    None, InvalidInput, CounterOrder, CoverageUnavailable,
    SourceChanged, WrongPhase
};

class Capture final
{
    enum class Stage : uint8_t { New, Sampled, Sealed, Failed };
    assembly_shadow_execution_diagnostic::Header owned_{};
    uint64_t droppedThreads_=0;
    bool saturated_=false;
    Stage stage_=Stage::New;
    Error error_=Error::None;
    bool Fail(Error reason) noexcept
    {
        if(error_==Error::None)error_=reason;
        stage_=Stage::Failed;
        return false;
    }
    static bool ValidStateCode(int32_t state) noexcept
    {
        return state>=static_cast<int32_t>(AssemblyShadowState::Disabled) &&
               state<=static_cast<int32_t>(AssemblyShadowState::FailedAfterCommit);
    }
    static bool Ordered(const assembly_shadow_execution_diagnostic::Header& h) noexcept
    {
        // These subset increments release-publish before their totals.
        // A raced reader can still see a partial cross-counter observation.
        return h.shadowMethodChecks<=h.methodChecks &&
               h.rejectedBaselineMethods<=h.methodChecks &&
               h.shadowInterpreterTransformations<=h.interpreterTransformations;
    }
    static bool Equal(const assembly_shadow_execution_diagnostic::Header& a,
                      const assembly_shadow_execution_diagnostic::Header& b) noexcept
    {
        // Never compare struct bytes or padding, nor mutable external pointers.
        return a.stateCode==b.stateCode && a.generation==b.generation &&
           a.methodChecks==b.methodChecks &&
           a.shadowMethodChecks==b.shadowMethodChecks &&
           a.rejectedBaselineMethods==b.rejectedBaselineMethods &&
           a.baselineClassCctorStarted==b.baselineClassCctorStarted &&
           a.shadowClassCctorStarted==b.shadowClassCctorStarted &&
           a.interpreterTransformations==b.interpreterTransformations &&
           a.shadowInterpreterTransformations==b.shadowInterpreterTransformations &&
           a.droppedClassObservations==b.droppedClassObservations;
    }
public:
    static constexpr size_t kMaximumObjectBytes=256;
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    Error FirstError() const noexcept {return error_;}
    bool Complete() const noexcept
    {return stage_==Stage::Sealed && error_==Error::None;}
    bool RejectOwnerUnavailable() noexcept {return Fail(Error::CoverageUnavailable);}
    bool RejectSourceChanged() noexcept {return Fail(Error::SourceChanged);}
    bool Begin(const assembly_shadow_execution_diagnostic::Header& sample,
               uint64_t droppedThreads,bool saturated) noexcept
    {
        if(stage_!=Stage::New || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!sample.state || !ValidStateCode(sample.stateCode))
            return Fail(Error::InvalidInput);
        if(!Ordered(sample))return Fail(Error::CounterOrder);
        if(droppedThreads || saturated)return Fail(Error::CoverageUnavailable);
        owned_=sample;
        // Never retain caller state pointer; all output uses static enum names.
        owned_.state=nullptr;
        droppedThreads_=droppedThreads;
        saturated_=saturated;
        stage_=Stage::Sampled;
        return true;
    }
    bool Seal(const assembly_shadow_execution_diagnostic::Header& after,
              uint64_t droppedThreads,bool saturated,bool sameActiveSnapshot) noexcept
    {
        if(stage_!=Stage::Sampled || error_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!after.state || !ValidStateCode(after.stateCode))
            return Fail(Error::InvalidInput);
        if(!Ordered(after))return Fail(Error::CounterOrder);
        if(droppedThreads || saturated || droppedThreads_ || saturated_)
            return Fail(Error::CoverageUnavailable);
        if(!sameActiveSnapshot || !Equal(owned_,after))
            return Fail(Error::SourceChanged);
        stage_=Stage::Sealed;
        return true;
    }
    bool Get(assembly_shadow_execution_diagnostic::Header& value) const noexcept
    {
        if(!Complete())return false;
        value=owned_;
        value.state=AssemblyShadowDiagnostics::StateName(
              static_cast<AssemblyShadowState>(owned_.stateCode));
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 execution scalar capture object budget changed");
}}} // il2cpp::vm::assembly_shadow_bounded_execution_header
