// R03 P1: source-bound host fixture, NOT official Unity/IL2CPP runtime.
// The real AssemblyShadowVisibility bounded class callback + collector are
// mechanically inserted verbatim. All class/image/visibility metadata below
// are explicit mocks; teardown and generics lifetime are NOT proven.
#include "AssemblyShadowBoundedOrdinaryClasses.h"
#include "AssemblyShadowFrozenGeneralCapture.h"
#include "AssemblyShadowDiagnostics.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <locale>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern bool watch;
extern size_t newCalls;

struct Il2CppAssembly
{
    struct { const char* name=nullptr; } aname;
};
struct Il2CppImage
{
    const Il2CppAssembly* assembly=nullptr;
    bool interpreter=false;
};
struct Il2CppClass
{
    const Il2CppImage* image=nullptr;
    const char* name=nullptr;
    const char* namespaze=nullptr;
    void* generic_class=nullptr;
    bool staged=false, visible=true, initialized=true;
};
namespace hybridclr { namespace metadata {
bool IsInterpreterImage(const Il2CppImage* image)
{return image->interpreter;}
}}

namespace il2cpp { namespace os {
class FastAutoLock
{
    std::unique_lock<std::mutex> lock_;
public:
    explicit FastAutoLock(std::mutex* ptr):lock_(*ptr){}
};
}}
namespace il2cpp { namespace vm {
namespace {
std::mutex g_MetadataLock;
struct PrivateImage {unsigned token=0;};
PrivateImage privateA{1},privateB{2};
std::atomic<const PrivateImage*> s_privateImages{&privateA};
bool flipPrivateVisibility=false;
uint64_t currentGeneration=7;
size_t generationCalls=0, visibilityCalls=0;
bool flipGeneration=false;
std::vector<Il2CppClass*> sourceClassPointers;
}
class AssemblyShadow
{
public:
    static uint64_t ActiveGeneration()
    {
        ++generationCalls;
        if(flipGeneration && generationCalls==2) ++currentGeneration;
        return currentGeneration;
    }
};
class AssemblyShadowVisibility
{
public:
    static bool ClassUsesStagedMetadata(const Il2CppClass* klass)
    {
        ++visibilityCalls;
        return klass->staged;
    }
    static bool TryCaptureBoundedOrdinaryClasses(
        assembly_shadow_bounded_classes::Capture&);
};
// This host mock includes only initialized, visible classes. The real
// MemoryInformation::ReportIL2CppClasses implementation enumerates multiple
// other image/array/generic/pointer registries and may allocate internally.
void il2cpp_class_for_each(void (*callback)(Il2CppClass*,void*),void* ctx)
{
    size_t seen=0;
    for(auto* klass:sourceClassPointers)
    {
        if(!klass || (klass->initialized && klass->visible))
        {
            callback(klass,ctx);
            ++seen;
            if(flipPrivateVisibility && seen==1)
                s_privateImages.store(&privateB,std::memory_order_release);
        }
    }
}

// __EXACT_CALLBACK_SOURCE__
// __EXACT_NATIVE_METHOD_SOURCE__

namespace cl=assembly_shadow_bounded_classes;
namespace fg=assembly_shadow_frozen_general;
namespace dg=assembly_shadow_diagnostic;

class OriginalDiagnostics
{
public:
    static std::string Serialize(const ShadowDiagnosticSnapshot&);
};
struct Source
{
    std::vector<std::string> assemblies, names, namespaces;
    std::vector<Il2CppAssembly> assemblyRecords;
    std::vector<Il2CppImage> images;
    std::vector<Il2CppClass> classes;
    void Prepare(size_t count)
    {
        sourceClassPointers.clear();
        assemblies.clear();names.clear();namespaces.clear();
        assemblyRecords.clear();images.clear();classes.clear();
        assemblies.reserve(count);names.reserve(count);
        namespaces.reserve(count);assemblyRecords.reserve(count);
        images.reserve(count);classes.reserve(count);
        sourceClassPointers.reserve(count);
        for(size_t i=0;i<count;++i)
        {
            assemblies.push_back("Module.Physical."+std::to_string(i%7));
            names.push_back("Class_"+std::to_string(i)+"_0123456789ABCDEFGHIJKLMN");
            namespaces.push_back(i%3==0 ? "Game.UI.中文" :
                i%3==1 ? "" : "Game.Core");
            assemblyRecords.push_back({{assemblies.back().c_str()}});
            images.push_back({&assemblyRecords.back(),i%2==0});
            classes.push_back({&images.back(),names.back().c_str(),
                namespaces.back().c_str(),(i%4==0?reinterpret_cast<void*>(uintptr_t(1)):nullptr),
                i%5==0,true,true});
            sourceClassPointers.push_back(&classes.back());
        }
        generationCalls=0;visibilityCalls=0;
        currentGeneration=7;flipGeneration=false;
        flipPrivateVisibility=false;
        s_privateImages.store(&privateA,std::memory_order_release);
    }
};
static std::string Text(const char* text,size_t count)
{
    assert(text);return std::string(text,count);
}
static std::unique_ptr<cl::Capture> Collect()
{
    auto capture=std::make_unique<cl::Capture>();
    generationCalls=0;
    newCalls=0;watch=true;
    const bool ok=AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*capture);
    const auto allocations=newCalls;
    watch=false;
    assert(ok && capture->Complete() && capture->FirstError()==cl::Error::None);
    assert(allocations==0);
    assert(generationCalls==2);
    return capture;
}
static void VerifySimple()
{
    Source source;source.Prepare(0);
    auto empty=Collect();
    assert(empty->Count()==0 && empty->ClassEnumerationGeneration()==7);
    source.Prepare(6);
    source.names[1]="Name\n\\\"";source.classes[1].name=source.names[1].c_str();
    source.namespaces[2]="";source.classes[2].namespaze=nullptr;
    const auto owned=Collect();
    assert(owned->Count()==6 && owned->ClassEnumerationGeneration()==7);
    assert(visibilityCalls==6);
    cl::ClassView row;
    assert(owned->Get(0,row));
    assert(Text(row.assemblyName,row.assemblyNameBytes)=="Module.Physical.0");
    assert(Text(row.typeName,row.typeNameBytes)=="Game.UI.中文.Class_0_0123456789ABCDEFGHIJKLMN");
    assert(row.isInterpreter && row.isConstructedGeneric && row.usesStagedMetadata);
    assert(owned->Get(1,row));
    assert(Text(row.typeName,row.typeNameBytes)=="Name\n\\\"");
    assert(!row.isInterpreter && !row.isConstructedGeneric && !row.usesStagedMetadata);
    assert(owned->Get(2,row) && Text(row.typeName,row.typeNameBytes)=="Class_2_0123456789ABCDEFGHIJKLMN");
    assert(!owned->Get(6,row));
    const auto saved=Text(row.typeName,row.typeNameBytes);
    source.names[2]="mutated";
    source.assemblies[2]="mutated";
    source.namespaces[2]="mutated";
    assert(owned->Get(2,row) && Text(row.typeName,row.typeNameBytes)==saved);
    std::cout<<"owned class text, flags, namespaces, order and lifetime passed\n";
}
static void Boundary()
{
    Source source;source.Prepare(cl::Capture::kMaximumClasses);
    const auto owned=Collect();
    assert(owned->Count()==cl::Capture::kMaximumClasses);
    assert(owned->OwnedTextBytes()>262144);
    cl::ClassView last;
    assert(owned->Get(8191,last));
    assert(Text(last.typeName,last.typeNameBytes).find("Class_8191")!=std::string::npos);
    source.Prepare(cl::Capture::kMaximumClasses+1);
    auto denied=std::make_unique<cl::Capture>();
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*denied));
    assert(denied->FirstError()==cl::Error::TooManyClasses && !denied->Complete());
    assert(!denied->Get(0,last));
    std::cout<<"8192 exact class capacity and 8193 fail-closed passed\n";
}
static void Negative()
{
    Source source;source.Prepare(3);
    flipGeneration=true;
    auto changed=std::make_unique<cl::Capture>();
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*changed));
    assert(changed->FirstError()==cl::Error::SourceChanged);
    source.Prepare(3);
    flipPrivateVisibility=true;
    auto privateChanged=std::make_unique<cl::Capture>();
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*privateChanged));
    assert(privateChanged->FirstError()==cl::Error::SourceChanged);
    assert(!privateChanged->Complete()); // no earlier rows leak as valid
    source.Prepare(3);
    source.classes[0].name=nullptr;
    auto nullname=std::make_unique<cl::Capture>();
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*nullname));
    assert(nullname->FirstError()==cl::Error::NullName);
    source.Prepare(3);
    source.classes[0].image=nullptr;
    auto nullimage=std::make_unique<cl::Capture>();
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*nullimage));
    assert(nullimage->FirstError()==cl::Error::NullName);
    source.Prepare(3);
    source.classes[0].image=&source.images[0];
    source.assemblies[0].assign(cl::Capture::kTextCapacity+1,'Z');
    source.assemblyRecords[0].aname.name=source.assemblies[0].c_str();
    auto oversize=std::make_unique<cl::Capture>();
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(*oversize));
    assert(oversize->FirstError()==cl::Error::TextCapacity);
    {
        auto owned=std::make_unique<cl::Capture>();
        assert(!owned->Seal(0,true));
        assert(owned->FirstError()==cl::Error::WrongPhase);
        assert(!owned->Begin());
    }
    {
        auto owned=std::make_unique<cl::Capture>();
        assert(owned->Begin());
        auto* ptr=reinterpret_cast<const char*>(owned.get());
        assert(!owned->Add(ptr,"N","C",false,false,false));
        assert(owned->FirstError()==cl::Error::AliasedInput);
    }
    {
        struct Adjacent {cl::Capture capture; char name[24];};
        auto adjacent=std::make_unique<Adjacent>();
        std::strcpy(adjacent->name,"SeparateName");
        assert(adjacent->capture.Begin());
        assert(adjacent->capture.Add(adjacent->name,"","C",true,true,false));
        assert(adjacent->capture.Seal(11,true));
        cl::ClassView row;
        assert(adjacent->capture.Get(0,row));
        assert(Text(row.assemblyName,row.assemblyNameBytes)=="SeparateName");
        assert(Text(row.typeName,row.typeNameBytes)=="C");
    }
    // The public class callback returns only initialized and visible classes
    // in this HOST FIXTURE; source production semantics are audited separately.
    source.Prepare(3);
    source.classes[1].visible=false;
    source.classes[2].initialized=false;
    const auto filtered=Collect();
    assert(filtered->Count()==1);
    std::cout<<"null/capacity/ordering/alias/visibility/generation controls passed\n";
}
static void CompleteOriginalV1Parity()
{
    std::locale::global(std::locale::classic());
    Source source;source.Prepare(8192);
    const auto capture=Collect();
    ShadowDiagnosticSnapshot old;
    old.enabled=true;
    old.startupCandidateSchemaVersion=1;
    old.startupObservationMode="EarlyTracking";
    old.state=AssemblyShadowState::FailedAfterCommit;
    old.detail="synthetic-host";
    old.classEnumerationGeneration=capture->ClassEnumerationGeneration();
    for(size_t i=0;i<capture->Count();++i)
    {
        cl::ClassView row;
        assert(capture->Get(i,row));
        old.ordinaryClasses.push_back({Text(row.assemblyName,row.assemblyNameBytes),
            Text(row.typeName,row.typeNameBytes),
            row.isInterpreter,row.isConstructedGeneric,row.usesStagedMetadata});
    }
    const std::string golden=OriginalDiagnostics::Serialize(old);
    const std::string current=AssemblyShadowDiagnostics::Serialize(old);
    assert(golden==current);
    auto output=std::make_unique<fg::Capture>();
    assert(output->Begin({old.enabled,old.startupCandidateSchemaVersion,
                          dg::Quoted(old.startupObservationMode)}));
    assert(output->EndStartupCandidates({old.state,old.lastError,
        dg::Quoted(old.detail),dg::Quoted(old.baselineBuildId),
        dg::Quoted(old.patchId),old.generation,old.retainedBytes,
        old.expected,old.staged}));
    assert(output->EndClosure() && output->EndStableAot() &&
           output->EndCommitOrder() && output->EndAssemblies() &&
           output->EndEvents() && output->EndBaselineUses(old.enumerationGeneration));
    assert(output->EndOrdinaryAssemblies(old.classEnumerationGeneration));
    for(size_t i=0;i<capture->Count();++i)
    {
        cl::ClassView row;assert(capture->Get(i,row));
        assert(output->AddOrdinaryClass({
            {row.assemblyName,row.assemblyNameBytes},
            {row.typeName,row.typeNameBytes},
            row.isInterpreter,row.isConstructedGeneric,row.usesStagedMetadata}));
    }
    assert(output->Freeze());
    assert(output->Bytes()==golden.size());
    assert(std::memcmp(output->Data(),golden.data(),golden.size())==0);
    assert(golden.size()>262144);
    std::cout<<"8192-class host-mock three-way exact V1 bytes="
             <<golden.size()<<" (other VM domains MOCKED) passed\n";
}
}} // il2cpp::vm

int main()
{
    il2cpp::vm::VerifySimple();
    il2cpp::vm::Boundary();
    il2cpp::vm::Negative();
    il2cpp::vm::CompleteOriginalV1Parity();
    std::cout<<"R03 P1 fixed ordinary classes V1 exact method host contract PASS\n";
}
