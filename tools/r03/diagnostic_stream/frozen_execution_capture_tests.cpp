// R03 P1: source-bound owned native execution capture contract.
// No live VM, Player, engine scheduler or semantic acceptance is exercised.
#include "AssemblyShadowFrozenExecutionCapture.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

extern bool watch;        // allocation_probe.cpp, source-bound test only
extern size_t newCalls;   // operator-new accounting excludes malloc/GC/RSS
using namespace il2cpp::vm;
namespace ex=assembly_shadow_execution_diagnostic;
namespace fx=assembly_shadow_frozen_execution;
namespace dg=assembly_shadow_diagnostic;

struct Buffer final
{
    std::vector<char> data;
    size_t used=0, calls=0, failAt=0;
    explicit Buffer(size_t capacity) : data(capacity, '\0') {}
    static bool Write(void* user,const char* p,size_t n) noexcept
    {
        auto& b=*static_cast<Buffer*>(user);
        if (++b.calls==b.failAt || n>b.data.size()-b.used) return false;
        if (n) std::memcpy(b.data.data()+b.used,p,n);
        b.used+=n;
        return true;
    }
    dg::Sink Sink() { return {this,Write}; }
    std::string Str() const { return {data.data(),used}; }
};

static ex::Header HeaderForN()
{
    ex::Header h;
    h.stateCode=9;
    h.state="FailedAfterCommit";
    h.generation=1;
    h.methodChecks=6;
    h.shadowMethodChecks=2;
    h.rejectedBaselineMethods=3;
    h.interpreterTransformations=2;
    h.shadowInterpreterTransformations=2;
    return h;
}

static ex::ClassRow NRow()
{
    ex::ClassRow row;
    row.logicalAssembly=dg::Quoted("mscorlib");
    row.typeKey=dg::Quoted("Synthetic.ShortType");
    row.staticStoragePointer=dg::Quoted("");
    row.isActive=true;
    row.cctorFinished=true;
    return row;
}

static std::string DirectLegacy(const ex::Header& header,
                                const std::vector<ex::ClassRow>& rows,
                                const std::string& suffix)
{
    std::ostringstream out;
    ex::Begin(out,header);
    for(size_t i=0;i<rows.size();++i) ex::AppendClass(out,rows[i],i!=0);
    ex::EndClasses(out);
    out<<suffix<<'}';
    return out.str();
}

static void FixtureToStdout()
{
    auto capture=std::make_unique<fx::Capture>();
    assert(capture->Begin(HeaderForN()));
    ex::ClassRow row=NRow();
    for(size_t i=0;i<fx::Capture::kMaxRows;++i)
        assert(capture->Add(row));
    assert(capture->SetR02Suffix(dg::Quoted("")));
    assert(capture->Freeze(true)); // historical N VM stub has no r02 suffix
    Buffer output(4u*1024u*1024u);
    const auto receipt=capture->SerializeTo(output.Sink(),output.data.size());
    assert(receipt.complete && receipt.bytesAccepted==331078);
    assert(output.used==331078);
    std::cout.write(output.data.data(),static_cast<std::streamsize>(output.used));
    assert(std::cout);
}

static void BasicAndLifetime()
{
    auto capture=std::make_unique<fx::Capture>();
    auto header=HeaderForN();
    std::string mutableState="FailedAfterCommit";
    std::string mutableAssembly="A\"\\n";
    std::string mutableTypeKey("ab\0C\\x01\\xF0\\x9F\\x90\\xA6",10);
    std::string mutablePointer="0x1234";
    std::string suffix=",\"r02\":{\"schemaVersion\":1,\"classesCoverage\":\"BoundedComplete\"}";
    header.state=mutableState.c_str();
    ex::ClassRow row=NRow();
    row.logicalAssembly=dg::Quoted(mutableAssembly);
    row.typeKey=dg::Quoted(mutableTypeKey);
    row.staticStoragePointer=dg::Quoted(mutablePointer);
    row.pointerDetailsAvailable=true;
    row.staticStorageAvailable=true;
    row.interpreterShadow=true;
    row.physicalInterpreter=true;
    row.isActive=false;
    row.cctorStarted=true;
    row.cctorFinished=false;
    row.hasInitializationException=true;
    std::vector<ex::ClassRow> original{row};
    const std::string golden=DirectLegacy(header,original,suffix);
    Buffer output(golden.size()+64);
    newCalls=0;watch=true;
    assert(capture->Begin(header));
    assert(capture->Add(row));
    assert(capture->SetR02Suffix(dg::Quoted(suffix)));
    assert(capture->Freeze());
    const size_t capturedNewCalls=newCalls;
    // Mutate/destroy the caller's strings AFTER capture: owned bytes must stay
    // unchanged even if the originating temporary VM names are no longer valid.
    watch=false;
    mutableState.assign("Unavailable");
    mutableAssembly.assign("other");
    mutableTypeKey.assign("destroyed");
    mutablePointer.assign("gone");
    suffix.assign("replaced");
    watch=true;
    const dg::WriteResult result=capture->SerializeTo(output.Sink(),golden.size());
    const size_t formattedNewCalls=newCalls;
    watch=false;
    assert(capturedNewCalls==0 && formattedNewCalls==0);
    assert(result.complete && result.error==dg::WriteError::None);
    assert(result.bytesAccepted==golden.size());
    assert(output.Str()==golden);
    assert(capture->CapturedClasses()==1 && capture->CapturedTextBytes()>0);
    assert(capture->Frozen() && capture->FirstError()==fx::CaptureError::None);
    // Tests exact output boundary and fail-closed partial sink; all other
    // input and source data remain private to this capture.
    Buffer shortOutput(golden.size());
    const auto shortReceipt=capture->SerializeTo(shortOutput.Sink(),golden.size()-1);
    assert(!shortReceipt.complete && shortReceipt.error==dg::WriteError::ByteLimit);
    assert(shortReceipt.bytesAccepted<golden.size());
    Buffer failOutput(golden.size());
    failOutput.failAt=2;
    const auto failReceipt=capture->SerializeTo(failOutput.Sink(),golden.size());
    assert(!failReceipt.complete && failReceipt.error==dg::WriteError::SinkFailure);
    assert(failReceipt.bytesAccepted<golden.size());
    assert(!capture->Add(row)); // immutable after Freeze: poison on violation
    assert(capture->FirstError()==fx::CaptureError::WrongState);
    assert(!capture->Frozen());
    assert(!capture->SerializeTo(output.Sink(),golden.size()).complete);
}

static void FailClosedCases()
{
    const auto h=HeaderForN();
    const auto row=NRow();
    const std::string suffix=",\"r02\":{\"schemaVersion\":1}";
    {
        auto c=std::make_unique<fx::Capture>();
        Buffer b(256);
        assert(!c->SerializeTo(b.Sink(),256).complete);
        assert(!c->Freeze());
        assert(c->FirstError()==fx::CaptureError::WrongState);
        assert(!c->Begin(h));
    }
    {
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        assert(!c->Begin(h));
        assert(c->FirstError()==fx::CaptureError::WrongState);
        assert(!c->Freeze());
    }
    {
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        ex::ClassRow invalid=row;
        invalid.logicalAssembly={nullptr,0};
        assert(!c->Add(invalid));
        assert(c->FirstError()==fx::CaptureError::InvalidInput);
    }
    {
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        assert(!c->SetR02Suffix(dg::Quoted("not-r02")));
        assert(c->FirstError()==fx::CaptureError::InvalidInput);
    }
    {
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        assert(c->SetR02Suffix(dg::Quoted("")));
        assert(!c->Freeze());
        assert(c->FirstError()==fx::CaptureError::MissingR02);
        assert(!c->Freeze(true)); // no retry after semantic failure
    }
    {
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        assert(c->SetR02Suffix(dg::Quoted(suffix)));
        assert(!c->Add(row)); // cannot add rows after finishing the suffix
        assert(c->FirstError()==fx::CaptureError::WrongState);
    }
    {
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        for(size_t i=0;i<fx::Capture::kMaxRows;++i) assert(c->Add(row));
        assert(!c->Add(row)); // no omitted 1025th class
        assert(c->CapturedClasses()==fx::Capture::kMaxRows);
        assert(c->FirstError()==fx::CaptureError::RowLimit);
        assert(!c->Freeze());
    }
    {
        // The only allowed behavior for a too-large type key is rejection.
        // No prefix can ever be emitted as a complete capture.
        auto c=std::make_unique<fx::Capture>();
        std::string huge(fx::Capture::kMaxTextBytes+1,'x');
        ex::ClassRow excessive=row;
        excessive.typeKey=dg::Quoted(huge);
        assert(c->Begin(h));
        assert(!c->Add(excessive));
        assert(c->FirstError()==fx::CaptureError::TextLimit);
        Buffer sink(1024);
        assert(!c->SerializeTo(sink.Sink(),1024).complete && sink.used==0);
    }
    {
        auto c=std::make_unique<fx::Capture>();
        ex::Header invalid=h;
        invalid.state=nullptr;
        assert(!c->Begin(invalid));
        assert(c->FirstError()==fx::CaptureError::InvalidInput);
    }
    {
        // Duplicate R02 suffix and post-freeze mutation cannot be accepted.
        auto c=std::make_unique<fx::Capture>();
        assert(c->Begin(h));
        assert(c->SetR02Suffix(dg::Quoted(suffix)));
        assert(!c->SetR02Suffix(dg::Quoted(suffix)));
        assert(!c->Freeze());
    }
}

static void ZeroAndFullRows()
{
    auto h=HeaderForN();
    const std::string suffix=",\"r02\":{\"schemaVersion\":1}";
    {
        auto c=std::make_unique<fx::Capture>();
        std::vector<ex::ClassRow> rows;
        const auto expected=DirectLegacy(h,rows,suffix);
        assert(c->Begin(h) && c->SetR02Suffix(dg::Quoted(suffix)) && c->Freeze());
        Buffer out(expected.size());
        auto r=c->SerializeTo(out.Sink(),expected.size());
        assert(r.complete && out.Str()==expected && c->CapturedClasses()==0);
    }
    {
        auto c=std::make_unique<fx::Capture>();
        const auto row=NRow();
        std::vector<ex::ClassRow> rows(fx::Capture::kMaxRows,row);
        const auto expected=DirectLegacy(h,rows,suffix);
        assert(c->Begin(h));
        for(const auto& item:rows) assert(c->Add(item));
        assert(c->SetR02Suffix(dg::Quoted(suffix)) && c->Freeze());
        Buffer out(expected.size());
        auto r=c->SerializeTo(out.Sink(),expected.size());
        assert(r.complete && out.Str()==expected && c->CapturedClasses()==fx::Capture::kMaxRows);
        assert(expected.size()>262144);
    }
}

int main(int argc,char** argv)
{
    if(argc==2 && std::string(argv[1])=="n-large")
    {
        FixtureToStdout();
        return 0;
    }
    assert(argc==1);
    BasicAndLifetime();
    FailClosedCases();
    ZeroAndFullRows();
    std::cout<<"P1 owned execution capture: legacy bytes/1024 rows/deep-copy/bounds/failures passed\n";
    return 0;
}
