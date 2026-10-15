// R03 P1 host-only: owned execution snapshot -> same V2 memory Capture/Encoder
// used by the pinned demo. All VM fields are fixture data, NOT a live census.
#include "AssemblyShadowFrozenExecutionCapture.h"
#include "BoundedDiagnosticsV2.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
extern bool watch;
extern size_t newCalls;

namespace fx=il2cpp::vm::assembly_shadow_frozen_execution;
namespace ex=il2cpp::vm::assembly_shadow_execution_diagnostic;
namespace dg=il2cpp::vm::assembly_shadow_diagnostic;
namespace v2=r03_diag_v2;

struct Appender
{
    v2::Capture* sink=nullptr;
    static bool Write(void* user,const char* p,size_t count) noexcept
    {
        auto& self=*static_cast<Appender*>(user);
        return self.sink->Append(reinterpret_cast<const uint8_t*>(p),count);
    }
};
struct Bytes
{
    std::vector<uint8_t> value;
    size_t used=0,callCount=0,failAt=0;
    explicit Bytes(size_t length):value(length) {}
    static bool Write(void* user,const uint8_t* p,size_t n) noexcept
    {
        auto& out=*static_cast<Bytes*>(user);
        ++out.callCount;
        if(out.callCount==out.failAt || n>out.value.size()-out.used)return false;
        if(n)std::memcpy(out.value.data()+out.used,p,n);
        out.used+=n;
        return true;
    }
};
static v2::Digest DigestOf(const char* hex)
{
    v2::Digest result{};
    if(!hex || std::strlen(hex)!=64) std::abort();
    for(size_t i=0;i<32;++i)
    {
        auto digit=[](char c)->uint8_t {
            if(c>='0'&&c<='9')return uint8_t(c-'0');
            if(c>='a'&&c<='f')return uint8_t(c-'a'+10);
            std::abort();
        };
        result[i]=uint8_t(16*digit(hex[2*i])+digit(hex[2*i+1]));
    }
    return result;
}
static void Save(const std::string& path,const uint8_t* bytes,size_t size)
{
    std::ofstream out(path,std::ios::binary);
    assert(out);
    out.write(reinterpret_cast<const char*>(bytes),static_cast<std::streamsize>(size));
    out.close();
    assert(out);
}
static void PrintError(const char* mode,v2::Capture& raw,fx::Capture& frozen,
                       bool successful,const v2::Receipt& receipt,size_t calls)
{
    std::cout << "{\"mode\":\"" << mode << "\",\"complete\":"
        << (successful?"true":"false")
        << ",\"frozenState\":" << unsigned(frozen.FirstError())
        << ",\"captureError\":" << unsigned(raw.error())
        << ",\"capturedClasses\":" << frozen.CapturedClasses()
        << ",\"acceptedRawBytes\":" << raw.bytes()
        << ",\"wireBytes\":" << receipt.bytes
        << ",\"wireComplete\":" << (receipt.complete?"true":"false")
        << ",\"sinkCalls\":" << calls << "}\n";
}

int main(int argc,char**argv)
{
    if(argc!=8)return 40;
    const std::string mode(argv[1]), root(argv[2]);
    const auto context=DigestOf(argv[3]);
    const auto inlineHash=DigestOf(argv[4]);
    const size_t inlineBytes=std::stoul(argv[5]);
    const size_t generalCount=std::stoul(argv[6]);
    const size_t rowCount=std::stoul(argv[7]);
    if ((mode!="complete" && mode!="short" && mode!="sink-fail") ||
        rowCount!=1024 || generalCount!=300000) return 41;

    auto frozen=std::make_unique<fx::Capture>();
    ex::Header header;
    header.stateCode=9;header.state="FailedAfterCommit";header.generation=1;
    header.methodChecks=6;header.shadowMethodChecks=2;header.rejectedBaselineMethods=3;
    header.interpreterTransformations=2;header.shadowInterpreterTransformations=2;
    ex::ClassRow row;
    row.logicalAssembly=dg::Quoted("mscorlib");
    row.typeKey=dg::Quoted("Synthetic.ShortType");
    row.isActive=true;row.cctorFinished=true;row.staticStoragePointer=dg::Quoted("");
    assert(frozen->Begin(header));
    for(size_t i=0;i<rowCount;++i)assert(frozen->Add(row));
    assert(frozen->SetR02Suffix(dg::Quoted("")));
    assert(frozen->Freeze(true)); // exact immutable N mock omits R02 metrics
    // The general field exceeds original V1 individual slot limit, but is a
    // complete JSON text. It is NOT the actual SNAP-01 class inventory.
    const std::string general="{\"schemaVersion\":1,\"payload\":\""+
        std::string(generalCount,'g')+"\"}";
    std::vector<uint8_t> captureBytes(v2::kArtifactMax);
    std::vector<uint8_t> scratch(v2::kFrame);
    Bytes wire(v2::kArtifactMax);
    // Only the deliberately invalid short storage case reduces the bound.
    const size_t capacity=mode=="short"?100000:captureBytes.size();
    v2::Capture capture(captureBytes.data(),capacity);
    Appender appender{&capture};
    v2::Encoder encoder;
    v2::Receipt receipt{};
    bool okay=false;
    bool generalComplete=false;
    bool executionComplete=false;
    newCalls=0;
    watch=true;
    if(capture.Begin(14,false) && capture.Append(
        reinterpret_cast<const uint8_t*>(general.data()),general.size()) && capture.End())
    {
        generalComplete=true;
        if(capture.Begin(15,false))
        {
            auto result=frozen->SerializeTo({&appender,Appender::Write},v2::kArtifactMax);
            executionComplete=result.complete && capture.End();
        }
    }
    if(generalComplete && executionComplete && capture.Freeze())
    {
        v2::FieldView fields[2]{};
        if(capture.View(14,fields[0]) && capture.View(15,fields[1]))
        {
            if(mode=="sink-fail")wire.failAt=2;
            okay=encoder.Encode(context,inlineHash,inlineBytes,fields,
                                scratch.data(),scratch.size(),
                                Bytes::Write,&wire,receipt);
        }
    }
    watch=false;
    assert(newCalls==0);
    if(mode=="complete")
    {
        assert(okay && receipt.complete && receipt.bytes==wire.used);
        v2::FieldView fields[2]{};
        assert(capture.View(14,fields[0]) && capture.View(15,fields[1]));
        assert(fields[0].size==general.size() && fields[1].size==331078);
        Save(root+"/general.json",fields[0].data,fields[0].size);
        Save(root+"/execution.json",fields[1].data,fields[1].size);
        Save(root+"/wire.bin",wire.value.data(),wire.used);
        PrintError(mode.c_str(),capture,*frozen,true,receipt,wire.callCount);
        return 0;
    }
    assert(!okay && !receipt.complete && receipt.bytes==0);
    if(mode=="short")
    {
        assert(!generalComplete && capture.error()!=v2::Error::None);
        assert(wire.used==0);
    }
    else
    {
        assert(generalComplete && executionComplete);
        assert(encoder.failure().error==v2::Error::SinkFailure);
        assert(wire.used>0); // deliberately incomplete wire MUST be rejected
        Save(root+"/partial.wire.bin",wire.value.data(),wire.used);
    }
    PrintError(mode.c_str(),capture,*frozen,false,receipt,wire.callCount);
    return 0;
}
