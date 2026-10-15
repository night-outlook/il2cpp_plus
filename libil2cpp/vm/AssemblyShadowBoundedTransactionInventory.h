#pragma once
// P1 INACTIVE native transaction-only snapshot. The real producer copies the
// existing transaction-owned inventory under its existing mutex, together
// with status. There is NO type/class lookup, business method, file I/O,
// managed callback or dynamic allocation in Capture. A caller must prewarm
// the transaction and preallocate this object while healthy.
//
// A successful Seal means one owned transaction-subset observation with a
// stable first-TypeFailure/status admission. It does NOT imply an all-VM epoch,
// safe post-terminal scheduling, class lifetime or complete diagnostics.
// Overruns fail closed: never silently cap an existing source array.
#include "AssemblyShadowBoundedTransactionStatus.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_inventory {
using assembly_shadow_diagnostic::QuotedText;
namespace status = assembly_shadow_bounded_transaction;

enum class Error : uint8_t
{
    None, WrongStage, MissingInput, Overlap, TextCapacity,
    StableLimit, ClosureLimit, CommitLimit, EventLimit,
    StatusFailure, SourceChanged
};
struct AssemblyInput
{
    QuotedText name{}, mvid{};
    bool skeletonBuilt=false, runtimeMetadataInitialized=false, published=false,
         moduleInitializerAttempted=false, moduleInitializerRan=false;
};
struct EventInput
{
    uint64_t sequence=0, generation=0;
    size_t stagedCount=0;
    QuotedText kind{}, name{};
};
struct AssemblyView
{
    QuotedText name{}, mvid{};
    bool skeletonBuilt=false, runtimeMetadataInitialized=false, published=false,
         moduleInitializerAttempted=false, moduleInitializerRan=false;
};
struct EventView
{
    uint64_t sequence=0, generation=0;
    size_t stagedCount=0;
    QuotedText kind{}, name{};
};

class Capture final
{
public:
    static constexpr size_t kTextCapacity=1024u*1024u;
    static constexpr size_t kMaximumStableNames=4096u;
    static constexpr size_t kMaximumClosure=4096u;
    static constexpr size_t kMaximumCommitNames=4096u;
    static constexpr size_t kMaximumEvents=4096u;
    static constexpr size_t kMaximumObjectBytes=kTextCapacity+600000u;
private:
    struct Span { uint32_t offset=0, length=0; };
    struct AssemblyRow
    {
        Span name{}, mvid{};
        uint8_t flags=0;
    };
    struct EventRow
    {
        Span kind{},name{};
        uint64_t sequence=0,generation=0;
        size_t stagedCount=0;
    };
    enum : uint8_t
    {
        Skeleton=1, Metadata=2, Published=4,
        InitializerAttempted=8, InitializerRan=16
    };
    enum class Stage : uint8_t
    {
        New, StableNames, Closure, CommitNames, Events,
        LockedComplete, Sealed, Failed
    };
    status::Capture status_{};
    char text_[kTextCapacity]{};
    Span stableNames_[kMaximumStableNames]{};
    AssemblyRow closure_[kMaximumClosure]{};
    Span commitNames_[kMaximumCommitNames]{};
    EventRow events_[kMaximumEvents]{};
    uint32_t used_=0, stableCount_=0, closureCount_=0, commitCount_=0, eventCount_=0;
    Error error_=Error::None;
    Stage stage_=Stage::New;

    bool Fail(Error why) noexcept
    {
        if (error_==Error::None) error_=why;
        stage_=Stage::Failed;
        return false;
    }
    bool Check(Stage expected) noexcept
    {
        if (stage_!=expected || error_!=Error::None)
            return Fail(Error::WrongStage);
        return true;
    }
    bool Copy(QuotedText source,Span& destination) noexcept
    {
        if (!source.data) return Fail(Error::MissingInput);
        const uintptr_t sourceStart=reinterpret_cast<uintptr_t>(source.data);
        const uintptr_t outputStart=reinterpret_cast<uintptr_t>(this);
        if (source.bytes>UINTPTR_MAX-sourceStart ||
            sizeof(*this)>UINTPTR_MAX-outputStart) return Fail(Error::MissingInput);
        // Disallow an input from *any* owned portion of this object. The
        // comparison stops at the exact end of this object, never at a budget
        // upper bound plus bookkeeping headroom (past P1 alias-guard bug).
        if (source.bytes && sourceStart<outputStart+sizeof(*this) &&
            outputStart<sourceStart+source.bytes)
            return Fail(Error::Overlap);
        if (source.bytes>kTextCapacity-used_) return Fail(Error::TextCapacity);
        destination.offset=used_;
        destination.length=static_cast<uint32_t>(source.bytes);
        if (source.bytes) std::memcpy(text_+used_,source.data,source.bytes);
        used_+=static_cast<uint32_t>(source.bytes);
        return true;
    }
    QuotedText Text(Span s) const noexcept { return {text_+s.offset,s.length}; }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;

    Error FirstError() const noexcept { return error_; }
    status::Error StatusFirstError() const noexcept { return status_.FirstError(); }
    bool Complete() const noexcept
    { return stage_==Stage::Sealed && error_==Error::None && status_.Complete(); }
    size_t TextBytesCaptured() const noexcept {return used_;}
    size_t ObservedStableNames() const noexcept{return stableCount_;}
    size_t ObservedClosure() const noexcept{return closureCount_;}
    size_t ObservedCommitNames() const noexcept{return commitCount_;}
    size_t ObservedEvents() const noexcept{return eventCount_;}

    // Begin is called under the transaction's mutex, at the same source
    // boundary as original status/closure/event enumeration. The caller must
    // later validate first TypeFailure and active/state publications.
    bool Begin(const status::Input& sample) noexcept
    {
        if (!Check(Stage::New)) return false;
        if (!status_.Begin(sample)) return Fail(Error::StatusFailure);
        stage_=Stage::StableNames;
        return true;
    }
    bool AddStable(QuotedText text) noexcept
    {
        if (!Check(Stage::StableNames)) return false;
        if (stableCount_==kMaximumStableNames) return Fail(Error::StableLimit);
        Span span;
        if (!Copy(text,span)) return false;
        stableNames_[stableCount_++]=span;
        return true;
    }
    bool EndStable() noexcept
    {
        if (!Check(Stage::StableNames)) return false;
        stage_=Stage::Closure;
        return true;
    }
    bool AddClosure(const AssemblyInput& row) noexcept
    {
        if (!Check(Stage::Closure)) return false;
        if (closureCount_==kMaximumClosure) return Fail(Error::ClosureLimit);
        AssemblyRow& owned=closure_[closureCount_];
        if (!Copy(row.name,owned.name) || !Copy(row.mvid,owned.mvid)) return false;
        owned.flags=(row.skeletonBuilt?Skeleton:0)|(row.runtimeMetadataInitialized?Metadata:0)|
            (row.published?Published:0)|(row.moduleInitializerAttempted?InitializerAttempted:0)|
            (row.moduleInitializerRan?InitializerRan:0);
        ++closureCount_;
        return true;
    }
    bool EndClosure() noexcept
    {
        if (!Check(Stage::Closure)) return false;
        stage_=Stage::CommitNames;
        return true;
    }
    bool AddCommit(QuotedText text) noexcept
    {
        if (!Check(Stage::CommitNames)) return false;
        if (commitCount_==kMaximumCommitNames) return Fail(Error::CommitLimit);
        Span span;
        if (!Copy(text,span)) return false;
        commitNames_[commitCount_++]=span;
        return true;
    }
    bool EndCommit() noexcept
    {
        if (!Check(Stage::CommitNames)) return false;
        stage_=Stage::Events;
        return true;
    }
    bool AddEvent(const EventInput& row) noexcept
    {
        if (!Check(Stage::Events)) return false;
        if (eventCount_==kMaximumEvents) return Fail(Error::EventLimit);
        EventRow& owned=events_[eventCount_];
        if (!Copy(row.kind,owned.kind) || !Copy(row.name,owned.name)) return false;
        owned.sequence=row.sequence;
        owned.generation=row.generation;
        owned.stagedCount=row.stagedCount;
        ++eventCount_;
        return true;
    }
    bool EndLocked() noexcept
    {
        if (!Check(Stage::Events)) return false;
        stage_=Stage::LockedComplete;
        return true;
    }
    // After mutex release, verify independently published values have not
    // visibly changed. A stable pair of atomic reads is NOT a global epoch.
    bool Seal(bool sourceStable,bool typeFailurePresent,AssemblyShadowError typeError,
              QuotedText typeDetail,bool active) noexcept
    {
        if (!Check(Stage::LockedComplete)) return false;
        if (!sourceStable) return Fail(Error::SourceChanged);
        if (!status_.ObserveFirstFailure(true,typeFailurePresent,typeError,typeDetail,active) ||
            !status_.Seal()) return Fail(Error::StatusFailure);
        stage_=Stage::Sealed;
        return true;
    }

    bool GetStatus(status::View& value) const noexcept
    { return Complete() && status_.Get(value); }
    bool GetStable(size_t index,QuotedText& value) const noexcept
    {
        if (!Complete() || index>=stableCount_) return false;
        value=Text(stableNames_[index]);return true;
    }
    bool GetClosure(size_t index,AssemblyView& value) const noexcept
    {
        if (!Complete() || index>=closureCount_) return false;
        const auto& row=closure_[index];
        value.name=Text(row.name);
        value.mvid=Text(row.mvid);
        value.skeletonBuilt=(row.flags&Skeleton)!=0;
        value.runtimeMetadataInitialized=(row.flags&Metadata)!=0;
        value.published=(row.flags&Published)!=0;
        value.moduleInitializerAttempted=(row.flags&InitializerAttempted)!=0;
        value.moduleInitializerRan=(row.flags&InitializerRan)!=0;
        return true;
    }
    bool GetCommit(size_t index,QuotedText& value) const noexcept
    {
        if (!Complete() || index>=commitCount_) return false;
        value=Text(commitNames_[index]);return true;
    }
    bool GetEvent(size_t index,EventView& value) const noexcept
    {
        if (!Complete() || index>=eventCount_) return false;
        const auto& row=events_[index];
        value.kind=Text(row.kind);value.name=Text(row.name);
        value.sequence=row.sequence;value.generation=row.generation;
        value.stagedCount=row.stagedCount;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 transaction inventory exceeded review preallocation limit");
static_assert(Capture::kTextCapacity<=UINT32_MAX,"P1 inventory offset representation overflow");
}}} // il2cpp::vm::assembly_shadow_bounded_inventory
