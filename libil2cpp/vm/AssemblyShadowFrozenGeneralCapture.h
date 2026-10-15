#pragma once
// P1 INACTIVE bounded general-diagnostics record. Builds the EXACT existing
// general JSON schema, stage by stage, into caller-preallocated owned storage.
// Input string views are consumed synchronously; no view/VM pointer is retained.
// This does NOT read VM metadata, obtain a VM/transaction/usage lock, establish
// a coherent cross-domain snapshot, open files, or prove post-terminal safety.
//
// Construct while healthy and give one thread exclusive lifetime ownership.
// Every producer stage must be called in the V1 field order; on first error,
// fail permanently without returning a valid prefix. A complete record must
// still pass the separate authenticated V2 aggregate-size/identity/semantic
// verifier. kMaxBytes is a GENERAL FIELD bound, not a combined-wire limit.
#include "AssemblyShadowDiagnostics.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <locale>

namespace il2cpp { namespace vm { namespace assembly_shadow_frozen_general {

using assembly_shadow_diagnostic::QuotedText;
using assembly_shadow_diagnostic::Sink;
using assembly_shadow_diagnostic::WriteError;
using assembly_shadow_diagnostic::WriteResult;
using assembly_shadow_diagnostic::Writer;

struct Header
{
    bool enabled = false;
    int32_t startupCandidateSchemaVersion = 0;
    QuotedText startupObservationMode{};
};
struct Transaction
{
    AssemblyShadowState state = AssemblyShadowState::Disabled;
    AssemblyShadowError lastError = AssemblyShadowError::Success;
    QuotedText detail{}, baselineBuildId{}, patchId{};
    uint64_t generation = 0, retainedBytes = 0;
    size_t expected = 0, staged = 0;
};
struct AssemblyRow
{
    QuotedText name{}, mvid{};
    bool skeletonBuilt=false, runtimeMetadataInitialized=false, published=false;
    bool moduleInitializerAttempted=false, moduleInitializerRan=false;
};
struct EventRow
{
    uint64_t sequence=0, generation=0;
    size_t stagedCount=0;
    QuotedText kind{}, name{};
};
struct UseRow
{
    QuotedText name{}, detail{}, type{};
    BaselineUseKind kind=BaselineUseKind::AssemblyReflection;
    uint64_t thread=0, timestamp=0;
};
struct OrdinaryAssemblyRow { QuotedText name{}; bool isInterpreter=false; };
struct OrdinaryClassRow
{
    QuotedText assemblyName{}, typeName{};
    bool isInterpreter=false, isConstructedGeneric=false, usesStagedMetadata=false;
};
enum class CaptureError : uint8_t
{
    None, WrongStage, InvalidInput, AliasedInput, Capacity,
    InvalidUtf8, SinkFailure, UnsupportedLocale
};

class Capture final
{
public:
    // Existing V2 artifact capacity, NOT an allowance to consume the entire
    // artifact on this field: the companion execution field and frame overhead
    // must fit the same separate aggregate budget at Encode.
    static constexpr size_t kMaxBytes = 3932160;
    static constexpr size_t kMaximumObjectBytes = kMaxBytes + 128;
private:
    enum class Stage : uint8_t
    {
        New, StartupNames, Closure, StableAot, CommitOrder,
        Assemblies, Events, BaselineUses, OrdinaryAssemblies,
        OrdinaryClasses, Complete, Failed
    };
    char bytes_[kMaxBytes]{};
    size_t used_ = 0;
    CaptureError error_ = CaptureError::None;
    Stage stage_ = Stage::New;
    bool complete_ = false;
    size_t arrayCount_ = 0;
    Writer writer_;

    static bool Collect(void* opaque,const char* bytes,size_t count) noexcept
    {
        auto& self=*static_cast<Capture*>(opaque);
        if ((!bytes && count) || count>kMaxBytes-self.used_)
        {
            if (self.error_==CaptureError::None)
                self.error_=count>kMaxBytes-self.used_ ? CaptureError::Capacity : CaptureError::InvalidInput;
            return false;
        }
        if (count) std::memcpy(self.bytes_+self.used_,bytes,count);
        self.used_+=count;
        return true;
    }
    bool Fail(CaptureError error) noexcept
    {
        if (error_==CaptureError::None) error_=error;
        stage_=Stage::Failed;
        complete_=false;
        return false;
    }
    bool Check(Stage stage) noexcept
    {
        if (stage_==stage && error_==CaptureError::None) return true;
        return Fail(CaptureError::WrongStage);
    }
    bool Valid(QuotedText text) noexcept
    {
        if (!text.data) return Fail(CaptureError::InvalidInput);
        const uintptr_t p=reinterpret_cast<uintptr_t>(text.data);
        const uintptr_t first=reinterpret_cast<uintptr_t>(bytes_);
        if (text.bytes>UINTPTR_MAX-p || kMaxBytes>UINTPTR_MAX-first)
            return Fail(CaptureError::InvalidInput);
        if (text.bytes && p<first+kMaxBytes && first<p+text.bytes)
            return Fail(CaptureError::AliasedInput);
        return true;
    }
    void Text(QuotedText text) noexcept { writer_ << text; }
    bool AddString(Stage stage,QuotedText text) noexcept
    {
        if (!Check(stage) || !Valid(text)) return false;
        if (arrayCount_++) writer_<<',';
        Text(text);
        return true;
    }
    bool Close(Stage current,Stage next,const char* suffix) noexcept
    {
        if (!Check(current)) return false;
        writer_<<suffix;
        stage_=next;
        arrayCount_=0;
        return true;
    }
    static bool Utf8(const char* input,size_t bytes) noexcept
    {
        const auto* p=reinterpret_cast<const uint8_t*>(input);
        for (size_t i=0;i<bytes;)
        {
            uint32_t cp=p[i++]; unsigned extra=0; uint32_t minimum=0;
            if (cp<128) continue;
            if (cp>=0xc2 && cp<=0xdf) { extra=1;cp&=31;minimum=128; }
            else if (cp>=0xe0 && cp<=0xef) { extra=2;cp&=15;minimum=2048; }
            else if (cp>=0xf0 && cp<=0xf4) { extra=3;cp&=7;minimum=65536; }
            else return false;
            if (extra>bytes-i) return false;
            while (extra--)
            {
                const uint8_t next=p[i++];
                if ((next&0xc0)!=0x80) return false;
                cp=(cp<<6)|(next&63);
            }
            if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
        }
        return true;
    }
public:
    Capture() noexcept : writer_({this,Collect},kMaxBytes) {}
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    CaptureError FirstError() const noexcept { return error_; }
    bool Complete() const noexcept { return complete_ && error_==CaptureError::None && stage_==Stage::Complete; }
    size_t Bytes() const noexcept { return Complete() ? used_ : 0; }
    const char* Data() const noexcept { return Complete() ? bytes_ : nullptr; }

    bool Begin(const Header& header) noexcept
    {
        if (!Check(Stage::New) || !Valid(header.startupObservationMode)) return false;
        // Equivalent to AssemblyShadowDiagnostics::SerializeFields through
        // startupCandidateNames. This emits immediately, not after locks end.
        if (std::locale()!=std::locale::classic()) return Fail(CaptureError::UnsupportedLocale);
        writer_<<std::boolalpha<<"{\"schemaVersion\":1,\"enabled\":"<<header.enabled
            <<",\"runtimeAbiVersion\":"<<kAssemblyShadowRuntimeAbiVersion
            <<",\"metadataBudgetCapabilityVersion\":"<<(header.enabled?kAssemblyShadowMetadataBudgetCapabilityVersion:0)
            <<",\"recoveryCapabilityVersion\":"<<(header.enabled?kAssemblyShadowRecoveryCapabilityVersion:0)
            <<",\"startupCandidateSchemaVersion\":"<<header.startupCandidateSchemaVersion
            <<",\"startupObservationMode\":";
        Text(header.startupObservationMode);
        writer_<<",\"startupCandidateNames\":[";
        stage_=Stage::StartupNames;
        return true;
    }
    bool AddStartupCandidate(QuotedText text) noexcept { return AddString(Stage::StartupNames,text); }
    bool EndStartupCandidates(const Transaction& status) noexcept
    {
        if (!Check(Stage::StartupNames) || !Valid(status.detail) ||
            !Valid(status.baselineBuildId) || !Valid(status.patchId)) return false;
        writer_<<"],\"state\":";
        Text(assembly_shadow_diagnostic::Quoted(AssemblyShadowDiagnostics::StateName(status.state)));
        writer_<<",\"stateCode\":"<<static_cast<int>(status.state)
            <<",\"lastError\":"<<static_cast<int>(status.lastError)
            <<",\"detail\":";
        Text(status.detail);
        writer_<<",\"baselineBuildId\":";
        Text(status.baselineBuildId);
        writer_<<",\"patchId\":";
        Text(status.patchId);
        writer_<<",\"generation\":"<<status.generation
            <<",\"expected\":"<<status.expected
            <<",\"staged\":"<<status.staged
            <<",\"retainedBytes\":"<<status.retainedBytes
            <<",\"closureLoadOrder\":[";
        stage_=Stage::Closure;arrayCount_=0;
        return true;
    }
    bool AddClosureName(QuotedText value) noexcept { return AddString(Stage::Closure,value); }
    bool EndClosure() noexcept { return Close(Stage::Closure,Stage::StableAot,"],\"stableAotNames\":["); }
    bool AddStableAotName(QuotedText value) noexcept { return AddString(Stage::StableAot,value); }
    bool EndStableAot() noexcept { return Close(Stage::StableAot,Stage::CommitOrder,"],\"commitOrder\":["); }
    bool AddCommitOrderName(QuotedText value) noexcept { return AddString(Stage::CommitOrder,value); }
    bool EndCommitOrder() noexcept { return Close(Stage::CommitOrder,Stage::Assemblies,"],\"assemblies\":["); }
    bool AddAssembly(const AssemblyRow& row) noexcept
    {
        if (!Check(Stage::Assemblies) || !Valid(row.name) || !Valid(row.mvid)) return false;
        if (arrayCount_++) writer_<<',';
        writer_<<"{\"name\":";Text(row.name);
        writer_<<",\"mvid\":";Text(row.mvid);
        writer_<<",\"skeletonBuilt\":"<<row.skeletonBuilt
            <<",\"runtimeMetadataInitialized\":"<<row.runtimeMetadataInitialized
            <<",\"published\":"<<row.published
            <<",\"moduleInitializerAttempted\":"<<row.moduleInitializerAttempted
            <<",\"moduleInitializerRan\":"<<row.moduleInitializerRan<<'}';
        return true;
    }
    bool EndAssemblies() noexcept { return Close(Stage::Assemblies,Stage::Events,"],\"events\":["); }
    bool AddEvent(const EventRow& row) noexcept
    {
        if (!Check(Stage::Events) || !Valid(row.kind) || !Valid(row.name)) return false;
        if (arrayCount_++) writer_<<',';
        writer_<<"{\"sequence\":"<<row.sequence<<",\"kind\":";
        Text(row.kind);
        writer_<<",\"name\":";
        Text(row.name);
        writer_<<",\"generation\":"<<row.generation
            <<",\"stagedCount\":"<<row.stagedCount<<'}';
        return true;
    }
    bool EndEvents() noexcept { return Close(Stage::Events,Stage::BaselineUses,"],\"baselineUses\":["); }
    bool AddBaselineUse(const UseRow& row) noexcept
    {
        if (!Check(Stage::BaselineUses) || !Valid(row.name) || !Valid(row.detail) || !Valid(row.type)) return false;
        if (arrayCount_++) writer_<<',';
        writer_<<"{\"name\":";Text(row.name);
        writer_<<",\"kind\":";
        Text(assembly_shadow_diagnostic::Quoted(AssemblyShadowDiagnostics::UseKindName(row.kind)));
        writer_<<",\"detail\":";Text(row.detail);
        writer_<<",\"type\":";Text(row.type);
        writer_<<",\"thread\":"<<row.thread
            <<",\"timestamp\":"<<row.timestamp<<'}';
        return true;
    }
    bool EndBaselineUses(uint64_t generation) noexcept
    {
        if (!Check(Stage::BaselineUses)) return false;
        writer_<<"],\"enumerationGeneration\":"<<generation<<",\"ordinaryAssemblies\":[";
        stage_=Stage::OrdinaryAssemblies;arrayCount_=0;
        return true;
    }
    bool AddOrdinaryAssembly(const OrdinaryAssemblyRow& row) noexcept
    {
        if (!Check(Stage::OrdinaryAssemblies) || !Valid(row.name)) return false;
        if (arrayCount_++) writer_<<',';
        writer_<<"{\"name\":";Text(row.name);
        writer_<<",\"isInterpreter\":"<<row.isInterpreter<<'}';
        return true;
    }
    bool EndOrdinaryAssemblies(uint64_t classGeneration) noexcept
    {
        if (!Check(Stage::OrdinaryAssemblies)) return false;
        writer_<<"],\"classEnumerationGeneration\":"<<classGeneration<<",\"ordinaryClasses\":[";
        stage_=Stage::OrdinaryClasses;arrayCount_=0;
        return true;
    }
    bool AddOrdinaryClass(const OrdinaryClassRow& row) noexcept
    {
        if (!Check(Stage::OrdinaryClasses) || !Valid(row.assemblyName) || !Valid(row.typeName)) return false;
        if (arrayCount_++) writer_<<',';
        writer_<<"{\"assemblyName\":";Text(row.assemblyName);
        writer_<<",\"typeName\":";Text(row.typeName);
        writer_<<",\"isInterpreter\":"<<row.isInterpreter
            <<",\"isConstructedGeneric\":"<<row.isConstructedGeneric
            <<",\"usesStagedMetadata\":"<<row.usesStagedMetadata<<'}';
        return true;
    }
    bool Freeze() noexcept
    {
        if (!Check(Stage::OrdinaryClasses)) return false;
        writer_<<"]}";
        const auto result=writer_.Finish();
        if (!result.complete || result.bytesAccepted!=used_ || error_!=CaptureError::None)
            return Fail(result.error==WriteError::ByteLimit || error_==CaptureError::Capacity
                ? CaptureError::Capacity : CaptureError::SinkFailure);
        if (!Utf8(bytes_,used_)) return Fail(CaptureError::InvalidUtf8);
        stage_=Stage::Complete;complete_=true;
        return true;
    }
    WriteResult EmitTo(Sink sink,size_t maximumBytes) const noexcept
    {
        if (!Complete())
        {
            WriteResult result;
            result.error=WriteError::InvalidInput;
            return result;
        }
        // Reject a short destination BEFORE any callback, to avoid emitting a
        // prefix when the complete byte length is already known. A sink I/O
        // failure may still leave a physical prefix; the receipt remains FAIL.
        if (maximumBytes<used_)
        {
            WriteResult result;
            result.error=WriteError::ByteLimit;
            return result;
        }
        Writer writer(sink,maximumBytes);
        static constexpr size_t kMaximumCallbackBytes=65536;
        for (size_t offset=0;offset<used_;)
        {
            const size_t left=used_-offset;
            const size_t step=left<kMaximumCallbackBytes?left:kMaximumCallbackBytes;
            writer.write(bytes_+offset,static_cast<std::streamsize>(step));
            offset+=step;
        }
        return writer.Finish();
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 general fixed record exceeded reviewed object capacity");
static_assert(Capture::kMaxBytes<=UINT32_MAX,"general byte count representation");

}}} // il2cpp::vm::assembly_shadow_frozen_general
