#pragma once
// R03 P1 INACTIVE: complete execution V1 JSON from immutable caller-owned
// identity/class views and one owned R02 sample. It reads NO class/VM metadata.
// Preallocate while healthy; one owner; no concurrent mutation of input spans.
// The older AssemblyShadowFrozenExecutionCapture owned-row API is unchanged.
// This field's 3,932,160-byte cap DOES NOT replace the separate 4 MiB V2
// aggregate/frame budget or authorize the original getters/Player transport.
#include "AssemblyShadowExecutionSerializer.h"
#include "AssemblyShadowFrozenR02Serializer.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <locale>

namespace il2cpp { namespace vm { namespace assembly_shadow_frozen_execution_record {
namespace diagnostic=assembly_shadow_diagnostic;
namespace execution=assembly_shadow_execution_diagnostic;
namespace r02=assembly_shadow_r02;

struct Identity
{
    int32_t stateCode=0;
    diagnostic::QuotedText state{};
    uint64_t generation=0;
};
enum class RecordError : uint8_t
{
    None, WrongStage, InvalidInput, AliasedInput, InvalidUtf8, InvalidCounters,
    RowCount, Capacity, SinkFailure, UnsupportedLocale
};

class Record final
{
public:
    static constexpr size_t kMaxBytes=3932160;
    static constexpr size_t kMaxRows=1024; // Existing execution-observation cap.
    static constexpr size_t kMaxStateBytes=64;
    static constexpr size_t kMaximumObjectBytes=kMaxBytes+768;
private:
    enum class Stage : uint8_t { New, Rows, Complete, Failed };
    char bytes_[kMaxBytes]{};
    size_t used_=0, expectedRows_=0, rows_=0;
    r02::Snapshot counters_{};
    RecordError error_=RecordError::None;
    Stage stage_=Stage::New;
    mutable bool emitting_=false, emissionReentered_=false;

    static bool Collect(void* context,const char* data,size_t bytes) noexcept
    {
        auto& self=*static_cast<Record*>(context);
        if((!data && bytes) || bytes>kMaxBytes-self.used_)return false;
        if(bytes)std::memcpy(self.bytes_+self.used_,data,bytes);
        self.used_+=bytes;
        return true;
    }
    bool Fail(RecordError error) noexcept
    {
        if(error_==RecordError::None)error_=error;
        stage_=Stage::Failed;
        return false;
    }
    bool Check(Stage stage) noexcept
    { return stage_==stage && error_==RecordError::None ? true : Fail(RecordError::WrongStage); }
    bool CheckWrite(diagnostic::WriteResult result) noexcept
    {
        return result.complete ? true : Fail(
            result.error==diagnostic::WriteError::ByteLimit ? RecordError::Capacity : RecordError::SinkFailure);
    }
    static bool Utf8(const char* input,size_t bytes) noexcept
    {
        const auto* p=reinterpret_cast<const uint8_t*>(input);
        for(size_t i=0;i<bytes;)
        {
            uint32_t cp=p[i++];unsigned extra=0;uint32_t minimum=0;
            if(cp<128)continue;
            if(cp>=0xc2 && cp<=0xdf){extra=1;cp&=31;minimum=128;}
            else if(cp>=0xe0 && cp<=0xef){extra=2;cp&=15;minimum=2048;}
            else if(cp>=0xf0 && cp<=0xf4){extra=3;cp&=7;minimum=65536;}
            else return false;
            if(extra>bytes-i)return false;
            while(extra--)
            {
                const uint8_t next=p[i++];
                if((next&0xc0)!=0x80)return false;
                cp=(cp<<6)|(next&63);
            }
            if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
        }
        return true;
    }
    bool Valid(diagnostic::QuotedText text,size_t limit=kMaxBytes) noexcept
    {
        if(!text.data)return Fail(RecordError::InvalidInput);
        if(text.bytes>limit)return Fail(RecordError::Capacity);
        const uintptr_t start=reinterpret_cast<uintptr_t>(text.data);
        const uintptr_t self=reinterpret_cast<uintptr_t>(this);
        if(text.bytes>UINTPTR_MAX-start || sizeof(*this)>UINTPTR_MAX-self)
            return Fail(RecordError::InvalidInput);
        const uintptr_t end=self+sizeof(*this);
        // Reject overlap with the ENTIRE mutable capture, including counters
        // and writer state, before scanning or copying any source byte.
        if((start>=self && start<end) || (start<end && self<start+text.bytes))
            return Fail(RecordError::AliasedInput);
        return Utf8(text.data,text.bytes) ? true : Fail(RecordError::InvalidUtf8);
    }
public:
    Record() noexcept=default;
    Record(const Record&)=delete;
    Record& operator=(const Record&)=delete;
    Record(Record&&)=delete;
    Record& operator=(Record&&)=delete;
    RecordError FirstError() const noexcept { return error_; }
    bool Complete() const noexcept { return stage_==Stage::Complete && error_==RecordError::None; }
    size_t Bytes() const noexcept { return Complete() ? used_ : 0; }
    const char* Data() const noexcept { return Complete() ? bytes_ : nullptr; }
    // Diagnostic lower bound only; never use this as the exact failed length.
    size_t BytesWrittenLowerBound() const noexcept { return used_; }
    size_t RowsWritten() const noexcept { return rows_; }
    bool RejectInvalidSource() noexcept { return Fail(RecordError::InvalidInput); }

    bool Begin(const Identity& identity,const r02::Snapshot& counters,size_t expectedRows) noexcept
    {
        if(!Check(Stage::New))return false;
        if(!counters.Complete())return Fail(RecordError::InvalidCounters);
        if(expectedRows>kMaxRows || (counters.DiagnosticsLevel()<2 && expectedRows))
            return Fail(RecordError::RowCount);
        if(!Valid(identity.state,kMaxStateBytes))return false;
        if(std::locale()!=std::locale::classic())return Fail(RecordError::UnsupportedLocale);
        if(!identity.state.bytes || std::memchr(identity.state.data,0,identity.state.bytes))
            return Fail(RecordError::InvalidInput);
        // Preserve the original C-string formatter without an unbounded input
        // scan. This bounded stack token remains alive throughout Begin.
        char state[kMaxStateBytes+1]{};
        std::memcpy(state,identity.state.data,identity.state.bytes);
        counters_=counters; // Own values, not the caller's snapshot or a VM pointer.
        expectedRows_=expectedRows;
        execution::Header header;
        header.stateCode=identity.stateCode;header.generation=identity.generation;header.state=state;
        header.methodChecks=counters_.Read(r02::Metric::MethodChecks);
        header.shadowMethodChecks=counters_.Read(r02::Metric::ShadowMethodChecks);
        header.rejectedBaselineMethods=counters_.Read(r02::Metric::RejectedBaselineMethods);
        header.baselineClassCctorStarted=counters_.Read(r02::Metric::BaselineCctors);
        header.shadowClassCctorStarted=counters_.Read(r02::Metric::ShadowCctors);
        header.interpreterTransformations=counters_.Read(r02::Metric::Transformations);
        header.shadowInterpreterTransformations=counters_.Read(r02::Metric::ShadowTransformations);
        header.droppedClassObservations=counters_.Read(r02::Metric::DroppedClasses);
        diagnostic::Writer writer({this,Collect},kMaxBytes-used_);
        execution::Begin(writer,header);
        if(!CheckWrite(writer.Finish()))return false;
        stage_=Stage::Rows;
        return true;
    }
    bool AddClass(const execution::ClassRow& row) noexcept
    {
        if(!Check(Stage::Rows))return false;
        if(rows_>=expectedRows_)return Fail(RecordError::RowCount);
        if(!Valid(row.logicalAssembly) || !Valid(row.typeKey) || !Valid(row.staticStoragePointer))return false;
        diagnostic::Writer writer({this,Collect},kMaxBytes-used_);
        execution::AppendClass(writer,row,rows_!=0);
        if(!CheckWrite(writer.Finish()))return false;
        ++rows_;
        return true;
    }
    bool Freeze() noexcept
    {
        if(!Check(Stage::Rows))return false;
        if(rows_!=expectedRows_)return Fail(RecordError::RowCount);
        diagnostic::Writer writer({this,Collect},kMaxBytes-used_);
        execution::EndClasses(writer);
        // Typed suffix, never an arbitrary caller-supplied JSON fragment.
        if(!r02::AppendFrozenDiagnostics(writer,counters_))return Fail(RecordError::InvalidCounters);
        writer<<'}';
        if(!CheckWrite(writer.Finish()))return false;
        stage_=Stage::Complete;
        return true;
    }
    diagnostic::WriteResult EmitTo(diagnostic::Sink sink,size_t maximumBytes) const noexcept
    {
        using diagnostic::WriteError;using diagnostic::WriteResult;
        if(emitting_){emissionReentered_=true;return {WriteError::Reentered,0,false};}
        if(!Complete())return {WriteError::InvalidInput,0,false};
        if(maximumBytes<used_)return {WriteError::ByteLimit,0,false};
        emitting_=true;emissionReentered_=false;
        struct Guard { const Record& self;~Guard(){self.emitting_=false;} } guard{*this};
        size_t accepted=0;
        for(size_t offset=0;offset<used_;)
        {
            const size_t left=used_-offset, step=left<65536 ? left : 65536;
            // One complete checked Writer per chunk. This preserves the
            // original Writer API and stops immediately on an I/O failure.
            diagnostic::Writer output(sink,step);
            output.write(bytes_+offset,static_cast<std::streamsize>(step));
            const auto result=output.Finish();
            accepted+=result.bytesAccepted;
            if(emissionReentered_ || !Complete())
                return {emissionReentered_ ? WriteError::Reentered : WriteError::InvalidInput,accepted,false};
            if(!result.complete)return {result.error,accepted,false};
            offset+=step;
        }
        return {WriteError::None,accepted,true};
    }
};
static_assert(sizeof(Record)<=Record::kMaximumObjectBytes,"P1 execution frozen storage bound changed");
}}} // il2cpp::vm::assembly_shadow_frozen_execution_record
