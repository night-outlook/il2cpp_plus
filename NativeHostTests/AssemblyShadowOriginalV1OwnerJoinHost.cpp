// R03 B2 pure C++11 source test. Compiles actual original-V1 physical
// token-valid visitor and original V1 first-emplace owner join. Synthetic
// registration records only; NOT Unity's full Assembly.cpp or live VM.
// Do not install this standalone host/global allocation guard in GameAssembly.
#include "vm/AssemblyShadowOriginalV1ImageScan.h"
#include "vm/AssemblyShadowOriginalV1OwnerJoin.h"
#include "vm/AssemblyShadowObservedPhysicalImages.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <cstdint>

struct Image;
struct Assembly {
    int token=1;
    Image* image=nullptr;
    struct {const char* name="A";} aname;
};
struct Image {
    Assembly* assembly=nullptr;
    uint32_t typeCount=0;
    const void* handles[8]{};
};
using namespace il2cpp::vm;
namespace v1=assembly_shadow_original_v1_image_scan;
namespace join=assembly_shadow_original_v1_owner_join;
namespace inventory=assembly_shadow_observed_images;
static bool guarded=false;
static unsigned cases=0,assertions=0,protectedAllocations=0;
void* operator new(std::size_t n) {
    if(guarded){++protectedAllocations;std::abort();}
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void Check(bool b,int line) {
    ++assertions;
    if(!b){std::fprintf(stderr,"R03 original V1 join case=%u line=%d\n",cases,line);
        std::abort();}
}
#define CHECK(x) Check(!!(x),__LINE__)
struct Fixture {
    int handleA=100,handleB=200;
    Assembly a{},interpreter{},invalid{};
    Image aot{},hot{},empty{};
    const Assembly* originals[3]{};
    inventory::Inventory<8> snapshots;
    Fixture() {
        a.image=&aot;aot.assembly=&a;aot.typeCount=1;
        aot.handles[0]=&handleA;
        interpreter.aname.name="Hot";
        interpreter.image=&hot;hot.assembly=&interpreter;
        hot.typeCount=1;hot.handles[0]=&handleB;
        invalid.token=0;invalid.image=&empty;
        empty.assembly=&invalid;empty.typeCount=0;
    }
    bool Capture(size_t count) {
        bool ok=v1::Scan(originals,originals+count,[&](const Image* img)->bool {
            return snapshots.AddFromRegisteredAssembly(img);
        });
        return ok&&snapshots.Healthy();
    }
};
static auto Read=[](const Image* image,uint32_t idx)->const void*{
    return image->handles[idx];
};
static bool Try(Fixture& f,const Image* expected,const void* handle,
                join::Receipt& receipt,size_t& budget,size_t max=100) {
    return join::CheckFirstOwner(f.snapshots,expected,handle,Read,budget,max,
                                 8192,receipt);
}
static void SingleAot() {
    ++cases;Fixture f;f.originals[0]=&f.a;
    guarded=true;CHECK(f.Capture(1));
    join::Receipt receipt{};size_t budget=0;
    CHECK(Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(receipt.firstEmplaceOwnerMatched&&receipt.firstOwnerIndex==0);
    CHECK(receipt.examinedImages==1&&receipt.examinedHandles==1&&budget==1);
    CHECK(!receipt.entireOriginalV1Field15Proven&&!receipt.coherentVmEpochProven);
}
static void InterpreterFirstConflict() {
    ++cases;Fixture f;f.hot.handles[0]=&f.handleA;
    f.originals[0]=&f.interpreter;f.originals[1]=&f.a;
    guarded=true;CHECK(f.Capture(2));
    join::Receipt receipt{};size_t budget=0;
    CHECK(!Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(receipt.error==join::Error::FirstOwnerDifferent);
    CHECK(receipt.firstOwnerIndex==0&&!receipt.firstEmplaceOwnerMatched);
    CHECK(budget==1);
}
static void LaterInterpreterAlias() {
    ++cases;Fixture f;f.hot.handles[0]=&f.handleA;
    f.originals[0]=&f.a;f.originals[1]=&f.interpreter;
    guarded=true;CHECK(f.Capture(2));
    join::Receipt receipt{};size_t budget=0;
    CHECK(Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(receipt.error==join::Error::None&&receipt.firstOwnerIndex==0);
    CHECK(receipt.examinedImages==1);
}
static void ZeroTypeDefFirst() {
    ++cases;Fixture f;f.hot.typeCount=0;
    f.originals[0]=&f.interpreter;f.originals[1]=&f.a;
    guarded=true;CHECK(f.Capture(2));
    join::Receipt receipt{};size_t budget=0;
    CHECK(Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(receipt.firstOwnerIndex==1&&receipt.examinedImages==2);
    CHECK(receipt.examinedHandles==1&&budget==1);
}
static void TokenZeroSkipped() {
    ++cases;Fixture f;
    f.originals[0]=&f.invalid;f.originals[1]=&f.a;
    guarded=true;CHECK(f.Capture(2));
    join::Receipt receipt{};size_t budget=0;
    CHECK(Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(f.snapshots.Count()==1&&receipt.firstOwnerIndex==0);
}
static void MissingOwner() {
    ++cases;Fixture f;f.originals[0]=&f.interpreter;
    guarded=true;CHECK(f.Capture(1));
    join::Receipt receipt{};size_t budget=0;
    CHECK(!Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(receipt.error==join::Error::OwnerNotFound&&budget==1);
}
static void LookupBudget() {
    ++cases;Fixture f;f.originals[0]=&f.interpreter;f.originals[1]=&f.a;
    guarded=true;CHECK(f.Capture(2));
    join::Receipt receipt{};size_t budget=0;
    CHECK(!Try(f,&f.aot,&f.handleA,receipt,budget,1));
    guarded=false;
    CHECK(receipt.error==join::Error::LookupBudget&&budget==1);
}
static void ImageBudget() {
    ++cases;Fixture f;f.originals[0]=&f.interpreter;
    f.hot.typeCount=8193;
    guarded=true;CHECK(f.Capture(1));
    join::Receipt receipt{};size_t budget=0;
    CHECK(!Try(f,&f.aot,&f.handleA,receipt,budget));
    guarded=false;
    CHECK(receipt.error==join::Error::ImageBudget);
    CHECK(budget==0);
}
static void InvalidImage() {
    ++cases;Fixture f;f.originals[0]=&f.interpreter;
    f.interpreter.image=nullptr;
    guarded=true;CHECK(!f.Capture(1));guarded=false;
    CHECK(f.snapshots.Count()==0);
}
static void MismatchedImageParent() {
    ++cases;Fixture f;f.originals[0]=&f.interpreter;
    f.hot.assembly=&f.a;
    guarded=true;CHECK(!f.Capture(1));guarded=false;
    CHECK(f.snapshots.Count()==0);
}
static void EmptyInput() {
    ++cases;Fixture f;join::Receipt receipt{};size_t budget=0;
    guarded=true;CHECK(!Try(f,&f.aot,&f.handleA,receipt,budget));guarded=false;
    CHECK(receipt.error==join::Error::InvalidInput);
}
static void DuplicateSameImage() {
    ++cases;Fixture f;f.originals[0]=&f.a;f.originals[1]=&f.a;
    guarded=true;CHECK(f.Capture(2));
    join::Receipt receipt{};size_t budget=0;
    CHECK(Try(f,&f.aot,&f.handleA,receipt,budget));guarded=false;
    CHECK(receipt.firstOwnerIndex==0 && f.snapshots.Count()==1);
}
int main() {
    static_assert(!std::is_copy_constructible<inventory::Inventory<8>>::value,
                  "Metadata image inventory must not be copied");
    SingleAot();InterpreterFirstConflict();LaterInterpreterAlias();
    ZeroTypeDefFirst();TokenZeroSkipped();MissingOwner();LookupBudget();
    ImageBudget();InvalidImage();MismatchedImageParent();EmptyInput();
    DuplicateSameImage();
    CHECK(protectedAllocations==0);
    std::printf("{\"kind\":\"R03B2OriginalV1FirstOwnerJoinHost\","
                "\"result\":\"PassedSourceSemanticsOnly\","
                "\"cases\":%u,\"assertions\":%u,"
                "\"protectedAllocations\":%u,"
                "\"actualUnityAssemblyCppCompiled\":false,"
                "\"fullLegacyField15Parity\":false,"
                "\"coherentVmEpochProven\":false,"
                "\"runtimeAcceptance\":false}\n",cases,assertions,protectedAllocations);
}
