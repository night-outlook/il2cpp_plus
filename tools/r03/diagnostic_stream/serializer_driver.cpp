// Host integration: actual current and original general serializers. VM capture
// is synthetic/prebuilt; no claim of a no-allocation live metadata census.
#include "AssemblyShadowDiagnostics.h"
#include "BoundedDiagnosticsV2.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <vector>

extern bool watch;
extern size_t newCalls;

namespace il2cpp { namespace vm {
class OriginalDiagnostics { public: static std::string Serialize(const ShadowDiagnosticSnapshot&); };
}}
using namespace il2cpp::vm;
namespace fmt = il2cpp::vm::assembly_shadow_diagnostic;
struct Memory {
    std::vector<char> bytes; size_t used=0, calls=0, fail=0; bool partial=false;
    explicit Memory(size_t n):bytes(n+32, '\x5a'){}
    static bool Write(void* ctx,const char* p,size_t n) noexcept {
        auto& s=*static_cast<Memory*>(ctx);++s.calls;
        if(s.calls==s.fail){if(s.partial&&n){s.bytes[s.used]=p[0];++s.used;}return false;}
        if(n>s.bytes.size()-32-s.used)return false;
        if(n){std::memcpy(s.bytes.data()+s.used,p,n);}s.used+=n;return true;
    }
    fmt::Sink sink(){return {this,Write};}
    bool intact()const{return std::all_of(bytes.end()-32,bytes.end(),[](char c){return c=='\x5a';});}
    std::string text()const{return {bytes.data(),used};}
};
static ShadowDiagnosticSnapshot Snapshot(size_t count,int state,int variant) {
    ShadowDiagnosticSnapshot s;s.enabled=state!=0;s.state=static_cast<AssemblyShadowState>(state);
    s.startupCandidateSchemaVersion=1;s.startupObservationMode="EarlyTracking";
    s.startupCandidateNames={"Methods.R03","A\"B\\C"};s.lastError=AssemblyShadowError::ReferenceResolutionFailed;
    s.detail="exact\noriginal\tmetadata";s.baselineBuildId="build\r001";s.patchId="shadow";
    s.generation=18446744073709551615ULL;s.expected=3;s.staged=2;s.retainedBytes=0x7fffffffffffffffULL;
    s.enumerationGeneration=7;s.classEnumerationGeneration=8;s.closureLoadOrder={"A","B"};
    s.stableAotNames={"System"};s.commitOrder={"B","A"};
    s.assemblies.push_back({"A","a-mvid",true,true,false,true,false});
    s.events.push_back({23,"prepare","A",6,3});
    s.baselineUses.push_back({"A",BaselineUseKind::MethodExecution,"detail","T",0,UINT64_MAX});
    s.ordinaryAssemblies.push_back({"A",false});s.ordinaryAssemblies.push_back({"B",true});
    s.ordinaryClasses.reserve(count);
    for(size_t i=0;i<count;++i)s.ordinaryClasses.push_back({"Methods.R03",std::string("Namespace.Module.ObservedType_")+std::to_string(i),i%2!=0,i%3==0,i%5==0});
    if(variant==1){for(int i=0;i<32;++i)s.detail.push_back(static_cast<char>(i));s.detail += "\\\"\x7f\xc3\xa9\xe4\xb8\xad\xf0\x9f\x90\xa6";}
    if(variant==2)s.detail.assign(300000,'d');
    if(variant==3)s.detail.assign(r03_diag_v2::kArtifactMax,'d');
    if(variant==4)s.detail="\xc0\xaf"; // Legacy preserves bytes; V2 UTF-8 must reject them.
    if(variant==5){s.startupCandidateSchemaVersion=INT32_MIN;s.lastError=static_cast<AssemblyShadowError>(INT32_MIN);}
    if(variant==6)s=ShadowDiagnosticSnapshot{};
    return s;
}
static std::string Read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static void File(const std::string& p,const std::string& b){std::ofstream f(p,std::ios::binary);assert(f);f.write(b.data(),static_cast<std::streamsize>(b.size()));f.close();assert(f);}
static r03_diag_v2::Digest Parse(const char* p){r03_diag_v2::Digest d{};assert(std::strlen(p)==64);for(size_t i=0;i<32;++i){auto h=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};int a=h(p[2*i]),b=h(p[2*i+1]);assert(a>=0&&b>=0);d[i]=static_cast<uint8_t>(a*16+b);}return d;}
struct Capturer {r03_diag_v2::Capture* p;static bool Write(void* c,const char* d,size_t n)noexcept{return static_cast<Capturer*>(c)->p->Append(reinterpret_cast<const uint8_t*>(d),n);}};
struct Bytes {std::vector<uint8_t> data;size_t used=0;explicit Bytes(size_t n):data(n){};static bool Write(void* c,const uint8_t* p,size_t n)noexcept{auto&s=*static_cast<Bytes*>(c);if(n>s.data.size()-s.used)return false;if(n){std::memcpy(s.data.data()+s.used,p,n);}s.used+=n;return true;}};
static void RoundTrip(int argc,char**argv){
    assert(argc==8);size_t count=std::stoull(argv[2]);int state=std::stoi(argv[3]),variant=std::stoi(argv[4]);std::string dir=argv[5];
    auto s=Snapshot(count,state,variant);
    std::string old=OriginalDiagnostics::Serialize(s),current=AssemblyShadowDiagnostics::Serialize(s);assert(old==current);
    Memory target(old.size());newCalls=0;watch=true;
    auto result=AssemblyShadowDiagnostics::SerializeTo(s,target.sink(),old.size());watch=false;
    assert(result.complete&&result.error==fmt::WriteError::None&&result.bytesAccepted==old.size()&&newCalls==0);
    assert(target.intact()&&target.text()==old);File(dir+"/original.json",old);File(dir+"/legacy.json",current);File(dir+"/streamed.json",target.text());
    // Encoder input identity hashes are independently computed by the Python caller.
    std::string execution=Read((dir+"/execution.json").c_str());
    std::vector<uint8_t> captureBytes(r03_diag_v2::kArtifactMax),scratch(r03_diag_v2::kFrame);
    r03_diag_v2::Capture capture(captureBytes.data(),captureBytes.size());Capturer ctx{&capture};
    Bytes wire(r03_diag_v2::kArtifactMax);r03_diag_v2::Encoder encoder;r03_diag_v2::Receipt receipt;
    auto identity=Parse(argv[6]),inlineHash=Parse(argv[7]);bool ok=false;
    newCalls=0;watch=true;
    bool begin=capture.Begin(14,false);auto streamed=AssemblyShadowDiagnostics::SerializeTo(s,{&ctx,Capturer::Write},r03_diag_v2::kArtifactMax);
    bool end=streamed.complete && capture.End();
    if(begin&&end&&capture.Begin(15,false)&&capture.Append(reinterpret_cast<const uint8_t*>(execution.data()),execution.size())&&capture.End()&&capture.Freeze()){
        r03_diag_v2::FieldView fields[2]{};assert(capture.View(14,fields[0])&&capture.View(15,fields[1]));
        ok=encoder.Encode(identity,inlineHash,128,fields,scratch.data(),scratch.size(),Bytes::Write,&wire,receipt);
    }
    watch=false;assert(newCalls==0);
    if(variant==3||variant==4){assert(!ok);assert(!receipt.complete);assert(wire.used==0);}else{assert(ok&&receipt.complete);assert(receipt.bytes==wire.used);}
    File(dir+"/wire.bin",{reinterpret_cast<const char*>(wire.data.data()),wire.used});
    std::cout<<"{\"originalBytes\":"<<old.size()<<",\"classCount\":"<<count<<",\"outputMatches\":true,\"formatNewCalls\":0,\"encodeNewCalls\":"<<newCalls<<",\"formatComplete\":"<<(streamed.complete?"true":"false")<<",\"formatError\":"<<static_cast<int>(streamed.error)<<",\"captureError\":"<<static_cast<int>(capture.error())<<",\"wireComplete\":"<<(ok?"true":"false")<<",\"wireBytes\":"<<wire.used<<"}\n";
}
static void Negative(int argc,char**argv){
    assert(argc==4);std::string name=argv[2];size_t limit=std::stoull(argv[3]);auto s=Snapshot(1,9,1);
    std::string expected=OriginalDiagnostics::Serialize(s);Memory target(expected.size());
    if(name=="limit"){
        if(limit==999999)limit=expected.size()-1;
        auto r=AssemblyShadowDiagnostics::SerializeTo(s,target.sink(),limit);
        assert(!r.complete&&r.error==fmt::WriteError::ByteLimit&&r.bytesAccepted<=limit&&target.intact());
    }else if(name=="all-sink-failures"){
        auto ok=AssemblyShadowDiagnostics::SerializeTo(s,target.sink(),expected.size());assert(ok.complete);size_t writes=target.calls;
        for(size_t n=1;n<=writes;++n){for(int partial=0;partial<2;++partial){Memory t(expected.size());t.fail=n;t.partial=partial!=0;
            auto r=AssemblyShadowDiagnostics::SerializeTo(s,t.sink(),expected.size());
            assert(!r.complete&&r.error==fmt::WriteError::SinkFailure&&t.calls==n&&t.intact());}}
        std::cout<<"{\"sinkWritePositions\":"<<writes<<",\"partialAndNoWriteFailures\":"<<2*writes<<",\"allRejected\":true}\n";return;
    }else if(name=="null-sink"){
        auto r=AssemblyShadowDiagnostics::SerializeTo(s,{nullptr,nullptr},expected.size());assert(!r.complete&&r.error==fmt::WriteError::InvalidInput&&r.bytesAccepted==0);
    }else assert(false);
    std::cout<<"{\"rejected\":true}\n";
}
struct Reenter {fmt::Writer* writer=nullptr;int calls=0;bool finish=false;static bool Write(void*c,const char*,size_t)noexcept{auto&s=*static_cast<Reenter*>(c);++s.calls;if(s.finish)s.writer->Finish();else *s.writer<<"nested";return true;}};
struct GroupedDigits : std::numpunct<char> {
    char do_thousands_sep()const override{return '_';}
    std::string do_grouping()const override{return "\3";}
};
static void Primitive(const std::string& mode){
    Memory m(2048);fmt::Writer w(m.sink(),2048);
    if(mode=="nonclassic"){
        auto sample=Snapshot(1,9,0);
        std::locale::global(std::locale(std::locale::classic(),new GroupedDigits));
        auto original=OriginalDiagnostics::Serialize(sample);
        assert(original==AssemblyShadowDiagnostics::Serialize(sample));
        auto result=AssemblyShadowDiagnostics::SerializeTo(sample,m.sink(),2048);
        assert(!result.complete&&result.error==fmt::WriteError::UnsupportedLocale&&m.calls==0);
        std::locale::global(std::locale::classic());
    }else if(mode=="integers"){
        std::ostringstream expected;expected<<std::boolalpha<<INT64_MIN<<'|'<<UINT64_MAX<<'|'<<INT32_MIN<<'|'<<true<<'|'<<false<<'|'<<0;
        w<<std::boolalpha<<INT64_MIN<<'|'<<UINT64_MAX<<'|'<<INT32_MIN<<'|'<<true<<'|'<<false<<'|'<<0;
        assert(w.Finish().complete&&m.text()==expected.str());
    }else if(mode=="repeat-finish"){
        w<<"one";assert(w.Finish().complete);auto r=w.Finish();assert(!r.complete&&r.error==fmt::WriteError::Finished);
    }else if(mode=="write-after-finish"){
        w<<"one";assert(w.Finish().complete);size_t before=m.calls;w<<"two";auto r=w.Finish();assert(!r.complete&&r.error==fmt::WriteError::Finished&&m.calls==before);
    }else if(mode=="bad-input"){
        w.write(nullptr,2);w<<"ignored";auto r=w.Finish();assert(!r.complete&&r.error==fmt::WriteError::InvalidInput&&m.used==0);
    }else if(mode=="negative-length"){
        w.write("x",-1);auto r=w.Finish();assert(!r.complete&&r.error==fmt::WriteError::InvalidInput&&m.used==0);
    }else if(mode=="bad-manipulator"){
        w<<std::hex;auto r=w.Finish();assert(!r.complete&&r.error==fmt::WriteError::InvalidInput);
    }else if(mode=="reenter"||mode=="reenter-finish"){
        Reenter ctx;fmt::Writer nested({&ctx,Reenter::Write},100);ctx.writer=&nested;ctx.finish=mode=="reenter-finish";
        nested<<"first";nested<<"next";auto r=nested.Finish();assert(!r.complete&&r.error==fmt::WriteError::Reentered&&ctx.calls==1);
    }else assert(false);
    std::cout<<"{\"primitivePassed\":true,\"sizeofWriter\":"<<sizeof(w)<<"}\n";
}
int main(int argc,char**argv){assert(argc>=2);std::locale::global(std::locale::classic());std::string mode=argv[1];if(mode=="roundtrip")RoundTrip(argc,argv);else if(mode=="negative")Negative(argc,argv);else Primitive(mode);}
