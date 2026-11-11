// R03 B2: compile the actual pure composite TypeKey implementation.
// This host driver overrides global allocators and MUST NOT be put in
// libil2cpp/vm or an IL2CPP Player source roster. No real VM is simulated.
#include "vm/AssemblyShadowOwnedExecutionRows.h"
#include "vm/AssemblyShadowOwnedCompositeTypeKey.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

namespace leaf=r03_b2_owned_definition_key;
namespace composite=r03_b2_owned_composite_type_key;
namespace rows=r03_b2_owned_execution_rows;
static bool allocationGuard=false;
static unsigned assertions=0,protectedAllocations=0,cases=0;
void* operator new(std::size_t n) {
    if(allocationGuard){++protectedAllocations;std::abort();}
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void Check(bool b,int line){
    ++assertions;
    if(!b){std::fprintf(stderr,"R03 composite TypeKey case=%u assertion=%u line=%d\n",
                        cases,assertions,line);std::abort();}
}
#define CHECK(expression) Check(!!(expression),__LINE__)
static leaf::ByteView View(const char* s){return{s,std::strlen(s)};}
static void Define(leaf::Capture& dst,const char* name,uint32_t arity=0){
    CHECK(dst.Begin(View("Assembly-CSharp"),true,false,false));
    CHECK(dst.AddDeclaration(View("Demo"),View(name),arity));
    CHECK(dst.Seal());
}
static std::string Text(const composite::Capture& key){
    return key.Data()?std::string(key.Data(),key.Bytes()):std::string();
}
static std::string Text(const leaf::Capture& key){
    return key.Data()?std::string(key.Data(),key.Bytes()):std::string();
}

static void TestCompositeAndOriginalGrammar(){
    ++cases;
    leaf::Capture box,child;
    Define(box,"Box",2);
    Define(child,"A");
    composite::Capture a,ptr,szarr,arr,qualified,outer;
    const int sizes[]={4,5},lowerBounds[]={-1,0};
    allocationGuard=true;
    const bool success=a.FromDefinition(child) && ptr.FromPointer(a) &&
        szarr.FromSzArray(ptr) &&
        arr.FromArray(szarr,2,sizes,2,lowerBounds,2) &&
        qualified.FromQualified(arr,7,2,1,0) &&
        outer.BeginGeneric(box,2) && outer.AddGenericArgument(qualified) &&
        outer.AddGenericArgument(a) && outer.SealGeneric();
    allocationGuard=false;
    CHECK(success);
    const std::string originalLeaf=Text(child);
    const std::string pointer=std::string("ptr(")+originalLeaf+")";
    const std::string szarray=std::string("szarray(")+pointer+")";
    const std::string array="array(2,"+std::to_string(szarray.size())+
                            ":"+szarray+",sizes=4,5,bounds=-1,0,)";
    const std::string full="qualified(7,2,1,0,"+array+")";
    const std::string expected="generic("+Text(box)+","+
        std::to_string(full.size())+":"+full+","+
        std::to_string(originalLeaf.size())+":"+originalLeaf+")";
    CHECK(Text(a)==originalLeaf);
    CHECK(Text(ptr)==pointer);
    CHECK(Text(szarr)==szarray);
    CHECK(Text(arr)==array);
    CHECK(Text(qualified)==full);
    CHECK(Text(outer)==expected);
    CHECK(outer.Arguments()==2 && outer.Complete());
    CHECK(std::strstr(outer.Data(),"sizes=4,5,bounds=-1,0,")!=nullptr);

    // Existing real bounded execution-row serializer must preserve all
    // immutable composite bytes without retaining input Capture lifetime.
    rows::Rows staging;
    rows::Flags f{};f.interpreterShadow=true;
    allocationGuard=true;
    const bool written=staging.Begin(1)&&
        staging.Add(37,rows::Source::Candidate,View("Assembly-CSharp"),
                    outer,View(""),f)&&staging.Seal();
    allocationGuard=false;
    CHECK(written&&staging.Complete()&&staging.Count()==1);
    CHECK(!outer.Revoke());
    CHECK(staging.Count()==1);
    rows::RowView row{};
    CHECK(staging.Get(0,row));
    CHECK(row.registrySlot==37 && row.fields.interpreterShadow);
    CHECK(row.typeKey.size==expected.size());
    CHECK(std::memcmp(row.typeKey.data,expected.data(),expected.size())==0);
}
static void TestFailuresAndNoPartialOutput(){
    ++cases;
    leaf::Capture box,child,huge;
    Define(box,"Box",1);
    Define(child,"Child");
    composite::Capture simple;
    CHECK(simple.FromDefinition(child));
    {
        composite::Capture x;
        CHECK(!x.BeginGeneric(box,0)&&
              x.FirstError()==composite::Error::InvalidArgumentCount);
        CHECK(x.Data()==nullptr && !x.Complete());
    }
    {
        composite::Capture x;
        CHECK(!x.BeginGeneric(box,composite::Capture::kMaximumArguments+1));
        CHECK(x.Data()==nullptr && !x.Complete());
    }
    {
        composite::Capture x;
        CHECK(x.BeginGeneric(box,2));
        CHECK(x.AddGenericArgument(simple));
        CHECK(!x.SealGeneric()&&x.FirstError()==composite::Error::WrongCount);
        CHECK(x.Data()==nullptr && x.Bytes()==0);
    }
    {
        composite::Capture x;
        CHECK(x.BeginGeneric(box,1));
        CHECK(x.AddGenericArgument(simple));
        CHECK(!x.AddGenericArgument(simple));
        CHECK(x.FirstError()==composite::Error::TooManyArguments);
    }
    {
        composite::Capture x,unfinished;
        CHECK(x.BeginGeneric(box,1));
        CHECK(!x.AddGenericArgument(unfinished));
        CHECK(x.FirstError()==composite::Error::InvalidChild);
    }
    {
        composite::Capture x;
        CHECK(!x.FromPointer(x));
        CHECK(x.FirstError()==composite::Error::AliasedInput);
    }
    {
        composite::Capture x;
        CHECK(!x.FromQualified(x,0,0,0,0));
        CHECK(x.FirstError()==composite::Error::AliasedInput);
    }
    {
        composite::Capture x;
        CHECK(!x.FromArray(simple,2,nullptr,1,nullptr,0));
        CHECK(x.FirstError()==composite::Error::InvalidArray);
    }
    {
        composite::Capture x;
        CHECK(!x.FromArray(simple,0,nullptr,0,nullptr,0));
        CHECK(x.FirstError()==composite::Error::InvalidArray);
    }
    {
        composite::Capture x;
        int dims[composite::Capture::kMaximumArrayDimensions+1]{};
        CHECK(!x.FromArray(simple,1,dims,composite::Capture::kMaximumArrayDimensions+1,nullptr,0));
        CHECK(x.FirstError()==composite::Error::InvalidArray);
    }
    {
        composite::Capture x;
        CHECK(x.FromDefinition(child));
        CHECK(!x.FromDefinition(child));
        CHECK(x.FirstError()==composite::Error::Reused);
        CHECK(x.Data()==nullptr && !x.Complete());
    }
    {
        composite::Capture x;
        CHECK(!x.FromArray(x,1,nullptr,0,nullptr,0));
        CHECK(x.FirstError()==composite::Error::AliasedInput);
    }
    {
        composite::Capture x;
        CHECK(x.FromSzArray(simple));
        CHECK(!x.Revoke() && x.Data()==nullptr);
        CHECK(x.FirstError()==composite::Error::Revoked);
    }
    {
        // A large but valid leaf key cannot produce an unbounded array key.
        static char chars[4050];
        std::memset(chars,'A',sizeof chars);
        CHECK(huge.Begin(View("Assembly-CSharp"),true,false,false));
        CHECK(huge.AddDeclaration(View(""),{chars,sizeof chars},0));
        CHECK(huge.Seal());
        composite::Capture big,oversized;
        CHECK(big.FromDefinition(huge));
        CHECK(!oversized.FromQualified(big,65535,31,1,1));
        CHECK(oversized.FirstError()==composite::Error::Capacity);
        CHECK(oversized.Data()==nullptr && !oversized.Complete());
    }
}
static void TestMaximumArgumentsAndSignedBounds(){
    ++cases;
    leaf::Capture base,thing;
    Define(base,"Many",16);
    Define(thing,"Thing");
    composite::Capture arg,outer,arr;
    const int widths[]={0,-1,2147483647};
    const int bounds[]={-2147483647-1};
    allocationGuard=true;
    bool okay=arg.FromDefinition(thing)&&
        outer.BeginGeneric(base,composite::Capture::kMaximumArguments);
    for(size_t i=0;i<composite::Capture::kMaximumArguments&&okay;++i)
        okay=outer.AddGenericArgument(arg);
    okay=okay&&outer.SealGeneric()&&
        arr.FromArray(arg,3,widths,3,bounds,1);
    allocationGuard=false;
    CHECK(okay);
    CHECK(outer.Arguments()==16);
    CHECK(Text(arr).find("sizes=0,-1,2147483647,bounds=-2147483648,")!=std::string::npos);
    CHECK(!outer.Revoke() && outer.Bytes()==0);
}
int main(){
    static_assert(!std::is_copy_constructible<composite::Capture>::value,
                  "A composite key has exactly one owner");
    static_assert(!std::is_move_constructible<composite::Capture>::value,
                  "A composite key is never moved");
    TestCompositeAndOriginalGrammar();
    TestFailuresAndNoPartialOutput();
    TestMaximumArgumentsAndSignedBounds();
    CHECK(protectedAllocations==0);
    std::printf("{\"kind\":\"R03B2OwnedCompositeTypeKeyHost\","
                "\"result\":\"PassedPureComposer_NotVmSource\","
                "\"cases\":%u,\"assertions\":%u,"
                "\"protectedAllocations\":%u,\"realUnity\":false,"
                "\"sourceLifetimeProven\":false,"
                "\"fullCanonicalTypeKeys\":false,"
                "\"nativeV2TransportImplemented\":false,"
                "\"runtimeAcceptance\":false}\n",
                cases,assertions,protectedAllocations);
}
