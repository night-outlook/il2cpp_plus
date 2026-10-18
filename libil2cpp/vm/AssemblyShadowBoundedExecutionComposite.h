#pragma once
// R03 P1 INACTIVE — classless execution JSON from TWO owned, completed sources.
// This is a bounded header+R02 schema-parity fragment, NOT the full execution
// diagnostic: zero class rows are TEST INPUT only, NOT real class coverage.
// No VM metadata, R02 counters, file, Player or managed callback is read here.
// Class collection, global epoch, type keys, GC/RSS and native teardown remain
// absent. A fragment must NEVER be supplied to the original semantic verifier.
#include "AssemblyShadowBoundedExecutionHeader.h"
#include "AssemblyShadowBoundedR02Metrics.h"
#include "AssemblyShadowDiagnosticWriter.h"
#include <cstddef>
#include <cstdint>
namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_execution_composite {
namespace header=assembly_shadow_bounded_execution_header;
namespace r02=assembly_shadow_bounded_r02;
namespace serial=assembly_shadow_execution_diagnostic;
namespace writer=assembly_shadow_diagnostic;

enum class Error : uint8_t {
    None, MissingSource, IncompleteSource, InvalidLimit, OutputLimit,
    WriterFailure, WrongPhase
};
class Capture final
{
public:
    static constexpr size_t kClasslessMaxBytes=32768;
    static constexpr size_t kMaximumObjectBytes=kClasslessMaxBytes+128;
private:
    enum class Phase:uint8_t { New, Complete, Failed };
    char bytes_[kClasslessMaxBytes]{};
    size_t used_=0;
    Phase phase_=Phase::New;
    Error first_=Error::None;
    bool Fail(Error error) noexcept
    {
        if(first_==Error::None)first_=error;
        used_=0;
        phase_=Phase::Failed;
        return false;
    }
    static bool Push(void* context,const char* data,size_t n) noexcept
    {
        auto* self=static_cast<Capture*>(context);
        if(n>kClasslessMaxBytes-self->used_)return false;
        for(size_t i=0;i<n;++i)
            self->bytes_[self->used_+i]=data[i];
        self->used_+=n;
        return true;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    bool Complete() const noexcept
    {return phase_==Phase::Complete && first_==Error::None;}
    Error FirstError() const noexcept {return first_;}
    const char* Data() const noexcept {return Complete()?bytes_:nullptr;}
    size_t Bytes() const noexcept {return Complete()?used_:0;}
    // A short destination fails CLOSED; callers cannot use any emitted prefix.
    bool Build(const header::Capture* scalar,const r02::Capture* metrics,
               size_t maximumBytes=kClasslessMaxBytes) noexcept
    {
        if(phase_!=Phase::New || first_!=Error::None)
            return Fail(Error::WrongPhase);
        if(!scalar || !metrics)return Fail(Error::MissingSource);
        if(!scalar->Complete() || !metrics->Complete())
            return Fail(Error::IncompleteSource);
        if(maximumBytes==0 || maximumBytes>kClasslessMaxBytes)
            return Fail(Error::InvalidLimit);
        serial::Header h{};
        if(!scalar->Get(h))return Fail(Error::IncompleteSource);
        writer::Writer output({this,&Push},maximumBytes);
        serial::Begin(output,h);
        serial::EndClasses(output); // CLASSLESS TEST FRAGMENT, not full VM!
        if(!metrics->Append(output))return Fail(Error::IncompleteSource);
        output<<'}';
        const auto result=output.Finish();
        if(!result.complete || result.bytesAccepted!=used_)
            return Fail(result.error==writer::WriteError::ByteLimit
                        ? Error::OutputLimit : Error::WriterFailure);
        phase_=Phase::Complete;
        return true;
    }
};
static_assert(sizeof(Capture)<=Capture::kMaximumObjectBytes,
              "P1 classless execution fragment allocated storage grew");
}}} // il2cpp::vm::assembly_shadow_bounded_execution_composite
