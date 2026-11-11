// Standalone B2 host-only canonical GENERICINST grammar and owned row checks.
// This driver, its global allocator guard and mocked definitions MUST NEVER
// enter production libil2cpp source. No metadata/VM source acceptance.
#include "vm/AssemblyShadowOwnedExecutionRows.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

static bool s_guard=false;
static unsigned s_allocations=0,s_assertions=0;
void* operator new(std::size_t n) {
    if(s_guard){++s_allocations;std::abort();}
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void Check(bool value){++s_assertions;if(!value)std::abort();}
namespace key=r03_b2_owned_definition_key;
namespace generic=r03_b2_owned_constructed_type_key;
namespace rows=r03_b2_owned_execution_rows;
static key::ByteView View(const char* s){return {s,std::strlen(s)};}
static void Definition(key::Capture& out,const char* assembly,
                       const char* ns,const char* name,uint32_t arity=0){
    Check(out.Begin(View(assembly),true,false,false));
    Check(out.AddDeclaration(View(ns),View(name),arity));
    Check(out.Seal());
}
static std::string Text(const key::Capture& key){
    return std::string(key.Data(),key.Bytes());
}
static void TestPositive(){
    static_assert(!std::is_copy_constructible<generic::Capture>::value,
                  "One owner only");
    key::Capture base,argA,argB;
    Definition(base,"Methods","Demo","Pair",2);
    Definition(argA,"Methods","Demo","Hero");
    Definition(argB,"Methods","Demo","Monster");
    generic::Capture composed;
    s_guard=true;
    const bool ok=composed.Begin(base,2) &&
        composed.AddArgument(argA) &&
        composed.AddArgument(argB) &&
        composed.Seal();
    s_guard=false;
    Check(ok&&composed.Complete());
    const std::string expected=std::string("generic(")+Text(base)+","+
        std::to_string(argA.Bytes())+":"+Text(argA)+","+
        std::to_string(argB.Bytes())+":"+Text(argB)+")";
    Check(std::string(composed.Data(),composed.Bytes())==expected);
    Check(composed.Arguments()==2);
    rows::Rows store;rows::Flags f{};f.interpreterShadow=true;
    s_guard=true;
    const bool stored=store.Begin(1)&&
        store.Add(8,rows::Source::ActiveShadow,View("Methods"),
                  composed,View(""),f)&&store.Seal();
    s_guard=false;
    Check(stored&&store.Complete()&&store.Count()==1);
    Check(!base.Revoke()&&!argA.Revoke()&&!argB.Revoke());
    Check(!composed.Revoke());
    rows::RowView view{};
    Check(store.Get(0,view));
    Check(view.typeKey.data!=nullptr&&view.typeKey.size==expected.size());
    Check(std::memcmp(view.typeKey.data,expected.data(),expected.size())==0);
    Check(view.fields.interpreterShadow);
}
static void TestWrongArguments(){
    key::Capture base,arg;
    Definition(base,"Methods","Demo","Box",1);
    Definition(arg,"Methods","Demo","Thing");
    {
        generic::Capture g;
        Check(!g.Begin(base,0)&&!g.Data());
        Check(g.FirstError()==generic::Error::InvalidArity);
    }
    {
        generic::Capture g;
        Check(!g.Begin(base,generic::Capture::kMaximumArguments+1));
        Check(g.FirstError()==generic::Error::InvalidArity);
    }
    {
        generic::Capture g;
        Check(g.Begin(base,2));
        Check(g.AddArgument(arg));
        Check(!g.Seal()&&!g.Data());
        Check(g.FirstError()==generic::Error::WrongCount);
    }
    {
        generic::Capture g;
        Check(g.Begin(base,1));
        Check(g.AddArgument(arg));
        Check(!g.AddArgument(arg)&&!g.Complete());
        Check(g.FirstError()==generic::Error::TooManyArguments);
    }
    {
        key::Capture unfinished;
        generic::Capture g;
        Check(!g.Begin(unfinished,1)&&!g.Data());
        Check(g.FirstError()==generic::Error::InvalidDefinition);
    }
    {
        key::Capture unfinished;
        generic::Capture g;
        Check(g.Begin(base,1));
        Check(!g.AddArgument(unfinished)&&!g.Data());
        Check(g.FirstError()==generic::Error::InvalidArgument);
    }
    {
        generic::Capture g;
        Check(g.Begin(base,1));
        Check(g.AddArgument(arg)&&g.Seal());
        Check(!g.Begin(base,1)&&!g.Complete());
        Check(g.FirstError()==generic::Error::Reused);
    }
    {
        generic::Capture g;
        Check(g.Begin(base,1)&&g.AddArgument(arg)&&g.Seal());
        Check(!g.Revoke()&&!g.Data());
        Check(g.FirstError()==generic::Error::Revoked);
    }
}
static void TestBounds(){
    key::Capture base,arg;
    Definition(base,"Methods","Demo","Box",1);
    static char huge[3800];
    std::memset(huge,'Z',sizeof huge);
    Check(arg.Begin(View("Methods"),true,false,false));
    Check(arg.AddDeclaration(View("Demo"),{huge,sizeof huge},0));
    Check(arg.Seal());
    generic::Capture g;
    Check(g.Begin(base,2));
    Check(g.AddArgument(arg));
    Check(!g.AddArgument(arg));
    Check(g.FirstError()==generic::Error::Capacity && !g.Data());
    // Zero/saturated type arguments are not silently accepted.
    generic::Capture many;
    Check(many.Begin(base,generic::Capture::kMaximumArguments));
    for(size_t i=0;i<generic::Capture::kMaximumArguments;++i)
        Check(many.AddArgument(base));
    Check(many.Seal()&&many.Arguments()==generic::Capture::kMaximumArguments);
}
int main(){
    TestPositive();
    TestWrongArguments();
    TestBounds();
    Check(s_allocations==0);
    std::printf("{\"kind\":\"R03B2ConstructedTypeKeyHost\","
                "\"result\":\"PassedSameImageDefinitionArgumentsOnly\","
                "\"assertions\":%u,\"protectedAllocations\":%u,"
                "\"realVmSource\":false,\"completeCanonicalTypeKeys\":false,"
                "\"nativeV2TransportImplemented\":false,\"runtimeAcceptance\":false}\n",
                s_assertions,s_allocations);
}
