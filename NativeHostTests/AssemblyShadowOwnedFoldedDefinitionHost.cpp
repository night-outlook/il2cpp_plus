// Standalone R03 B2 host-only Unicode canonical assembly fold/TypeKey tests.
// It compiles the REAL bounded owned-key header with a known-value mock
// of original NextFolded; only the real VM adapter binds to VmStringUtils.
// Global allocator overrides MUST stay out of libil2cpp and GameAssembly.
#include "vm/AssemblyShadowOwnedDefinitionTypeKey.h"
#include "AssemblyShadowFoldedNameMock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

namespace key=r03_b2_owned_definition_key;
namespace names=il2cpp::vm::assembly_shadow_detail;
static bool guardAlloc=false;
static unsigned assertions=0,cases=0,guardedAllocations=0;
void* operator new(std::size_t bytes){
    if(guardAlloc){++guardedAllocations;std::abort();}
    if(void* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void Check(bool ok,int line){
    ++assertions;
    if(!ok){std::fprintf(stderr,"Folded TypeKey test case=%u line=%d\n",cases,line);
        std::abort();}
}
#define CHECK(ok) Check(!!(ok),__LINE__)
static key::ByteView View(const char* s){return {s,std::strlen(s)};}
static const auto Next=[](const char*& p,const char* e,uint16_t& first,
                          uint16_t& second)->bool{
    return names::NextFolded(p,e,first,second);
};
static bool BeginName(key::Capture& capture,const char* raw){
    const names::NameView slice=names::ViewName(raw);
    if(!slice.begin||slice.begin==slice.end)return false;
    size_t hash=0;
    if(!names::NameHash(slice,hash))return false;
    return capture.BeginCanonicalFolded(
        {slice.begin,static_cast<size_t>(slice.end-slice.begin)},
        true,false,false,Next);
}
static void Positive(const char* raw,const char* canonical){
    ++cases;
    key::Capture capture;
    guardAlloc=true;
    const bool ok=BeginName(capture,raw)&&
        capture.AddDeclaration(View("Demo"),View("Hero"),0)&&
        capture.Seal();
    guardAlloc=false;
    CHECK(ok && capture.Complete());
    const std::string original(canonical);
    const std::string expected="type("+std::to_string(original.size())+":"+
        original+"/4:Demo/4:Hero@0)";
    CHECK(std::string(capture.Data(),capture.Bytes())==expected);
    CHECK(capture.Declarations()==1);
    CHECK(!capture.Revoke()&&!capture.Complete()&&capture.Data()==nullptr);
}
static void TestSupported(){
    Positive("METHODS","methods");
    Positive("C:\\Path\\M\xC3\x89THODS.DLL, Version=2","m\xC3\xA9thods");
    Positive("B\xCE\xA3","b\xCF\x83"); // BΣ -> bσ
    Positive("X\xF0\x9F\x98\x80","x\xF0\x9F\x98\x80");
    Positive("C:/TOOLS/Fancy.exe, Culture=neutral","fancy");
    Positive("A\xC3\x84","a\xC3\xA4"); // AÄ -> aä
}
static void Negative(const char* value,key::Error expected){
    ++cases;
    key::Capture capture;
    const names::NameView slice=names::ViewName(value);
    const bool ok=capture.BeginCanonicalFolded(
        {slice.begin,static_cast<size_t>(slice.end-slice.begin)},
        true,false,false,Next);
    CHECK(!ok&&!capture.Complete()&&capture.Data()==nullptr);
    CHECK(capture.FirstError()==expected);
}
static void TestMalformed(){
    Negative("B\xC0\xAF",key::Error::InvalidUtf8);
    Negative("B\xED\xA0\x80",key::Error::InvalidUtf8);
    Negative("B\xF4\x90\x80\x80",key::Error::InvalidUtf8);
    Negative("B\x80",key::Error::InvalidUtf8);
    Negative("B\x01",key::Error::InvalidUtf8);
    Negative("",key::Error::MissingSource);
}
static void TestUntrustedFoldResults(){
    ++cases;
    {
        key::Capture capture;
        const auto noProgress=[](const char*& /*p*/,const char*,uint16_t& f,
                                  uint16_t& s)->bool {f='a';s=0;return true;};
        CHECK(!capture.BeginCanonicalFolded(View("A"),true,false,false,noProgress));
        CHECK(capture.FirstError()==key::Error::InvalidUtf8);
    }
    {
        key::Capture capture;
        const auto danglingHigh=[](const char*& p,const char*,uint16_t& f,
                                    uint16_t& s)->bool{++p;f=0xd800;s=0;return true;};
        CHECK(!capture.BeginCanonicalFolded(View("A"),true,false,false,danglingHigh));
        CHECK(capture.FirstError()==key::Error::InvalidUtf8);
    }
    {
        key::Capture capture;
        const auto strayLow=[](const char*& p,const char*,uint16_t& f,
                               uint16_t& s)->bool{++p;f=0xdc00;s=0;return true;};
        CHECK(!capture.BeginCanonicalFolded(View("A"),true,false,false,strayLow));
        CHECK(capture.FirstError()==key::Error::InvalidUtf8);
    }
    {
        key::Capture capture;
        const auto extraSecond=[](const char*& p,const char*,uint16_t& f,
                                  uint16_t& s)->bool{++p;f='a';s='b';return true;};
        CHECK(!capture.BeginCanonicalFolded(View("A"),true,false,false,extraSecond));
        CHECK(capture.FirstError()==key::Error::InvalidUtf8);
    }
    {
        key::Capture capture;
        const auto wrongPair=[](const char*& p,const char*,uint16_t& f,
                                uint16_t& s)->bool{++p;f=0xd800;s=0xe000;return true;};
        CHECK(!capture.BeginCanonicalFolded(View("A"),true,false,false,wrongPair));
        CHECK(capture.FirstError()==key::Error::InvalidUtf8);
    }
}
static void TestLimitsAndReuse(){
    ++cases;
    static char name[4096]{};
    std::memset(name,'Z',sizeof name-1);
    {
        key::Capture key;
        CHECK(!key.BeginCanonicalFolded({name,sizeof name-1},true,false,false,Next));
        CHECK(key.FirstError()==r03_b2_owned_definition_key::Error::Capacity);
        CHECK(key.Data()==nullptr);
    }
    {
        key::Capture key;
        CHECK(!key.BeginCanonicalFolded(View("A"),false,false,false,Next));
        CHECK(key.FirstError()==r03_b2_owned_definition_key::Error::UnsupportedShape);
    }
    {
        key::Capture key;
        CHECK(!key.BeginCanonicalFolded(View("A"),true,true,false,Next));
        CHECK(key.FirstError()==r03_b2_owned_definition_key::Error::UnsupportedShape);
    }
    {
        key::Capture key;
        CHECK(BeginName(key,"METHODS"));
        CHECK(!key.BeginCanonicalFolded(View("B"),true,false,false,Next));
        CHECK(key.FirstError()==r03_b2_owned_definition_key::Error::Reused);
    }
    {
        key::Capture key;
        CHECK(!key.BeginCanonicalFolded(
              {reinterpret_cast<const char*>(&key),1},true,false,false,Next));
        CHECK(key.FirstError()==r03_b2_owned_definition_key::Error::Alias);
    }
    {
        // The old ASCII-only entry is retained but must reject non-ASCII.
        key::Capture key;
        CHECK(!key.Begin(View("M\xC3\x89THODS"),true,false,false));
        CHECK(key.FirstError()==r03_b2_owned_definition_key::Error::NonAsciiAssembly);
    }
}
int main(){
    static_assert(!std::is_copy_constructible<key::Capture>::value,
                  "Folded TypeKey is single-owner");
    TestSupported();
    TestMalformed();
    TestUntrustedFoldResults();
    TestLimitsAndReuse();
    CHECK(guardedAllocations==0);
    std::printf("{\"kind\":\"R03B2CanonicalFoldedAssemblyHost\","
                "\"result\":\"PassedOwnedFoldedBytesOnly\","
                "\"cases\":%u,\"assertions\":%u,"
                "\"guardedAllocations\":%u,"
                "\"actualVmUnicodeTable\":false,"
                "\"fullLegacyFieldParity\":false,"
                "\"realUnity\":false,\"nativeV2TransportImplemented\":false,"
                "\"runtimeAcceptance\":false}\n",
                cases,assertions,guardedAllocations);
}
