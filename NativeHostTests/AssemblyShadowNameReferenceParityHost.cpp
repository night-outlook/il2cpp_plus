// Host-only reference Unicode folding tests.
// Compiles ORIGINAL vm/AssemblyShadowName.h and char-conversions.cpp tables,
// with a tiny test-local il2cpp-config ABI shim, and compiles the REAL bounded
// OwnedDefinitionTypeKey.h. This is not a Unity Player or live VM ABI test.
// The Utf16ToLower body below mirrors the pinned VmStringUtils.cpp 1:1.
#include "vm/AssemblyShadowName.h"
#include "vm/AssemblyShadowOwnedDefinitionTypeKey.h"
#include "char-conversions.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

namespace il2cpp { namespace utils {
Il2CppChar VmStringUtils::Utf16ToLower(Il2CppChar c){
    const Il2CppChar kDataLowThreshold=9423;
    const Il2CppChar kDataHighThreshold=65313;
    if(c<=kDataLowThreshold)c=ToLowerDataLow[c];
    else if(c>=kDataHighThreshold)
        c=ToLowerDataHigh[c-kDataHighThreshold];
    return c;
}
}}
namespace names=il2cpp::vm::assembly_shadow_detail;
namespace owned=r03_b2_owned_definition_key;
static bool guarded=false;
static unsigned asserts=0,cases=0,allocations=0;
void* operator new(std::size_t bytes){
    if(guarded){++allocations;std::abort();}
    if(void* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void Check(bool truth,int line){
    ++asserts;
    if(!truth){std::fprintf(stderr,"B2 real folding parity case=%u line=%d\n",cases,line);
        std::abort();}
}
#define CHECK(statement) Check(!!(statement),__LINE__)
static owned::ByteView View(const char* str){return {str,std::strlen(str)};}
static bool Begin(owned::Capture& key,const char* raw) {
    const names::NameView name=names::ViewName(raw);
    size_t hash=0;
    if(!names::NameHash(name,hash))return false;
    return key.BeginCanonicalFolded(
        {name.begin,static_cast<size_t>(name.end-name.begin)},
        true,false,false,
        [](const char*& cursor,const char* end,uint16_t& first,
           uint16_t& second)->bool {
            return names::NextFolded(cursor,end,first,second);
        });
}
static void Positive(const char* raw,const char* expectedAssembly){
    ++cases;
    owned::Capture key;
    guarded=true;
    const bool success=Begin(key,raw)&&
        key.AddDeclaration(View("Demo"),View("Hero"),0)&&
        key.Seal();
    guarded=false;
    CHECK(success&&key.Complete());
    const std::string folded(expectedAssembly);
    const std::string legacyBytes="type("+std::to_string(folded.size())+
        ":"+folded+"/4:Demo/4:Hero@0)";
    CHECK(std::string(key.Data(),key.Bytes())==legacyBytes);
    const names::NameView canonical=names::ViewName(raw);
    const names::NameView normalized=names::ViewName(expectedAssembly);
    CHECK(names::NameEquals(canonical,normalized));
    size_t h1=0,h2=0;
    CHECK(names::NameHash(canonical,h1));
    CHECK(names::NameHash(normalized,h2));
    CHECK(h1==h2);
}
static void TestUnicodeReference(){
    Positive("ASSEMBLY-CSHARP","assembly-csharp");
    Positive("C:\\Plugins\\M\xC3\x89THODS.DLL, Version=1","m\xC3\xA9thods");
    Positive("X\xF0\x9F\x98\x80","x\xF0\x9F\x98\x80");
    Positive("B\xCE\xA3","b\xCF\x83");
    Positive("A\xC3\x84","a\xC3\xA4");
    Positive("B\xD0\x98","b\xD0\xB8");
    Positive("C:/Dir/Fancy.EXE, Version=3","fancy");
}
static void Invalid(const char* bytes){
    ++cases;
    owned::Capture capture;
    const names::NameView view=names::ViewName(bytes);
    guarded=true;
    const bool accepted=capture.BeginCanonicalFolded(
        {view.begin,static_cast<size_t>(view.end-view.begin)},
        true,false,false,names::NextFolded);
    guarded=false;
    CHECK(!accepted&&!capture.Complete()&&capture.Data()==nullptr);
    CHECK(capture.FirstError()==owned::Error::InvalidUtf8);
    size_t unused=0;
    CHECK(!names::NameHash(view,unused));
}
static void TestInvalid(){
    Invalid("BAD\xC0\xAF");
    Invalid("BAD\xED\xA0\x80");
    Invalid("BAD\xF4\x90\x80\x80");
    Invalid("BAD\x80");
    Invalid("BAD\x01");
}
static void TestBounds(){
    ++cases;
    static char huge[4096]{};
    std::memset(huge,'A',sizeof huge-1);
    owned::Capture capture;
    const names::NameView normalized=names::ViewName(huge);
    guarded=true;
    const bool accepted=capture.BeginCanonicalFolded(
        {normalized.begin,static_cast<size_t>(normalized.end-normalized.begin)},
        true,false,false,names::NextFolded);
    guarded=false;
    CHECK(!accepted);
    CHECK(capture.FirstError()==owned::Error::Capacity&&
          capture.Data()==nullptr);
    owned::Capture noSource;
    CHECK(!noSource.BeginCanonicalFolded(
        {nullptr,0},true,false,false,names::NextFolded));
    CHECK(noSource.FirstError()==owned::Error::MissingSource);
}
int main(){
    static_assert(!std::is_copy_constructible<owned::Capture>::value,
                  "Original fold source has a single owned output");
    TestUnicodeReference();
    TestInvalid();
    TestBounds();
    CHECK(allocations==0);
    std::printf("{\"kind\":\"R03B2OriginalNameUnicodeParityHost\","
                "\"result\":\"PassedOriginalNameTableAndOwnedBytes\","
                "\"cases\":%u,\"assertions\":%u,\"protectedAllocations\":%u,"
                "\"originalNameHeader\":true,\"originalUnicodeTables\":true,"
                "\"actualVmMetadata\":false,\"completeV1Parity\":false,"
                "\"realUnity\":false,\"nativeV2TransportImplemented\":false,"
                "\"runtimeAcceptance\":false}\n",
                cases,asserts,allocations);
}
