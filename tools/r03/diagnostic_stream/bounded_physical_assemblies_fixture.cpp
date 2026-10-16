// P1 HOST ONLY: the exact current Assembly::TryCaptureBoundedPhysicalAssemblies
// body is inserted below, under explicit assembly-registry and generation
// mocks. Physical name and interpreter bit are copied while s_assemblyLock
// is held. No Unity runtime shutdown/metadata-lifetime claim is made.
#include "AssemblyShadowBoundedPhysicalAssemblies.h"
#include "AssemblyShadowFrozenGeneralCapture.h"
#include "AssemblyShadowDiagnostics.h"
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

struct Il2CppImage { bool interpreter=false; };
struct Il2CppAssemblyName { const char* name=nullptr; };
struct Il2CppAssembly {
    uint32_t token=0;
    const Il2CppImage* image=nullptr;
    Il2CppAssemblyName aname{};
};
namespace hybridclr { namespace metadata {
inline bool IsInterpreterImage(const Il2CppImage* image) noexcept
{return image->interpreter;}
}}
namespace il2cpp {
namespace os {
class FastAutoLock final {
    std::lock_guard<std::mutex> lock_;
public:
    explicit FastAutoLock(std::mutex* mutex):lock_(*mutex){}
};
}
namespace vm {
namespace {
std::mutex s_assemblyLock;
std::vector<const Il2CppAssembly*> s_Assemblies;
uint64_t generation=11;
int generationReads=0;
bool flipGeneration=false;
}
class AssemblyShadow {
public:
    static uint64_t ActiveGeneration()
    {
        ++generationReads;
        if(flipGeneration && generationReads==2) ++generation;
        return generation;
    }
};
class Assembly {
public:
    static bool TryCaptureBoundedPhysicalAssemblies(
        assembly_shadow_bounded_physical::Capture& capture) noexcept;
};
// __CURRENT_PRODUCTION_PHYSICAL_METHOD__

namespace phys=assembly_shadow_bounded_physical;
namespace frozen=assembly_shadow_frozen_general;
namespace dg=assembly_shadow_diagnostic;

class OriginalDiagnostics {
public:static std::string Serialize(const ShadowDiagnosticSnapshot&);
};
struct Source {
    std::vector<std::string> names;
    std::vector<Il2CppImage> images;
    std::vector<Il2CppAssembly> assemblies;

    void Create(size_t count)
    {
        names.clear();images.clear();assemblies.clear();s_Assemblies.clear();
        names.reserve(count);images.reserve(count);assemblies.reserve(count);
        s_Assemblies.reserve(count);
        for(size_t i=0;i<count;++i)
        {
            names.push_back("Assembly.Physical."+std::to_string(i)+
                ".0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ_中文");
            images.push_back({i%2!=0});
            assemblies.push_back({uint32_t(i+1),&images.back(),{names.back().c_str()}});
            s_Assemblies.push_back(&assemblies.back());
        }
    }
};

static std::string Text(phys::Row row)
{
    assert(row.name);return {row.name,row.bytes};
}
static std::unique_ptr<phys::Capture> Collect()
{
    auto c=std::make_unique<phys::Capture>();
    generationReads=0;
    newCalls=0;watch=true;
    const bool result=Assembly::TryCaptureBoundedPhysicalAssemblies(*c);
    const size_t allocations=newCalls;
    watch=false;
    assert(result && c->Complete() && c->FirstError()==phys::Error::None);
    assert(allocations==0);
    assert(generationReads==2);
    return c;
}
static void VerifyBasic()
{
    generation=11;flipGeneration=false;
    Source source;source.Create(0);
    {
        auto capture=Collect();
        assert(capture->Count()==0 && capture->EnumerationGeneration()==11);
    }
    source.Create(4);
    source.names[1]="A\n\\\""; // rebind the pointer held by registry after change
    source.assemblies[1].aname.name=source.names[1].c_str();
    source.names[2]="Shared.Physical.Identity";
    source.assemblies[2].aname.name=source.names[2].c_str();
    source.names[3]="Shared.Physical.Identity";
    source.assemblies[3].aname.name=source.names[3].c_str();
    source.assemblies[0].token=0; // exactly this physical row is filtered
    const auto captured=Collect();
    assert(captured->Count()==3 && captured->EnumerationGeneration()==11);
    phys::Row row{};
    assert(captured->Get(0,row) && Text(row)=="A\n\\\"" && row.interpreter);
    assert(captured->Get(1,row) && Text(row)=="Shared.Physical.Identity" && !row.interpreter);
    assert(captured->Get(2,row) && Text(row)=="Shared.Physical.Identity" && row.interpreter);
    assert(!captured->Get(3,row));
    const size_t bytes=captured->OwnedTextBytes();
    assert(bytes==source.names[1].size()+2*source.names[2].size());
    for(auto& name:source.names)name.assign("overwritten");
    assert(captured->Get(0,row) && Text(row)=="A\n\\\"" && row.interpreter);
    assert(captured->Get(2,row) && Text(row)=="Shared.Physical.Identity");
    std::cout<<"physical name/order/interpreter/duplicate/unpublished/source-mutation passed\n";
}
static void ExactlyFullAndOverfull()
{
    generation=11;flipGeneration=false;
    Source source;source.Create(phys::Capture::kMaxEntries);
    auto cap=Collect();
    assert(cap->Count()==phys::Capture::kMaxEntries);
    phys::Row row{};
    assert(cap->Get(phys::Capture::kMaxEntries-1,row));
    assert(Text(row).find("Assembly.Physical.4095")==0 && row.interpreter);
    assert(cap->OwnedTextBytes()>262144);
    source.Create(phys::Capture::kMaxEntries+1);
    auto denied=std::make_unique<phys::Capture>();
    assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(*denied));
    assert(denied->FirstError()==phys::Error::TooMany && !denied->Complete());
    assert(!denied->Get(0,row) && denied->Count()==0);
    std::cout<<"physical 4096 accepted, 4097 denied, >262144 synthetic name bytes passed\n";
}
static void FailClosed()
{
    Source source;generation=10;flipGeneration=false;
    source.Create(3);
    flipGeneration=true;generationReads=0;
    {
        auto denied=std::make_unique<phys::Capture>();
        assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(*denied));
        assert(denied->FirstError()==phys::Error::SourceChanged);
    }
    flipGeneration=false;generation=10;generationReads=0;
    s_Assemblies[1]=nullptr;
    {
        auto denied=std::make_unique<phys::Capture>();
        assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(*denied));
        assert(denied->FirstError()==phys::Error::SourceInvalid);
    }
    source.Create(3);
    source.assemblies[1].image=nullptr;
    {
        auto denied=std::make_unique<phys::Capture>();
        assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(*denied));
        assert(denied->FirstError()==phys::Error::SourceInvalid);
    }
    source.Create(3);
    source.assemblies[1].aname.name=nullptr;
    {
        auto denied=std::make_unique<phys::Capture>();
        assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(*denied));
        assert(denied->FirstError()==phys::Error::SourceInvalid);
    }
    source.Create(1);
    source.names[0].assign(phys::Capture::kMaxText+1,'Q');
    source.assemblies[0].aname.name=source.names[0].c_str();
    {
        auto denied=std::make_unique<phys::Capture>();
        assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(*denied));
        assert(denied->FirstError()==phys::Error::TextLimit);
    }
    {
        auto cap=std::make_unique<phys::Capture>();
        assert(!cap->Seal(0,true));
        assert(cap->FirstError()==phys::Error::WrongPhase);
        assert(!cap->Begin());
    }
    {
        auto cap=std::make_unique<phys::Capture>();
        assert(cap->Begin());
        assert(!cap->Add(reinterpret_cast<const char*>(cap.get()),false));
        assert(cap->FirstError()==phys::Error::AliasedSource);
        phys::Row v;assert(!cap->Get(0,v));
    }
    {
        struct Adjacent { phys::Capture capture; char name[32]; };
        auto owned=std::make_unique<Adjacent>();
        std::memcpy(owned->name,"separate-physical",18);
        owned->name[17]='\0';
        assert(owned->capture.Begin());
        assert(owned->capture.Add(owned->name,true));
        assert(owned->capture.Seal(9,true));
        phys::Row v;assert(owned->capture.Get(0,v));
        assert(Text(v)=="separate-physical" && v.interpreter);
    }
    std::cout<<"physical source invalid/generation change/budget/order/alias negatives passed\n";
}
static void OriginalV1Parity()
{
    std::locale::global(std::locale::classic());
    Source source;generation=77;flipGeneration=false;
    source.Create(4096);
    auto owned=Collect();
    ShadowDiagnosticSnapshot snapshot;
    snapshot.enabled=true;
    snapshot.startupCandidateSchemaVersion=3;
    snapshot.startupObservationMode="EarlyTracking";
    snapshot.state=AssemblyShadowState::FailedAfterCommit;
    snapshot.detail="host-synthetic-nonassembly";
    snapshot.enumerationGeneration=owned->EnumerationGeneration();
    for(size_t i=0;i<owned->Count();++i)
    {
        phys::Row row;assert(owned->Get(i,row));
        snapshot.ordinaryAssemblies.push_back({Text(row),row.interpreter});
    }
    const std::string original=OriginalDiagnostics::Serialize(snapshot);
    const std::string current=AssemblyShadowDiagnostics::Serialize(snapshot);
    assert(original==current);
    auto output=std::make_unique<frozen::Capture>();
    assert(output->Begin({snapshot.enabled,snapshot.startupCandidateSchemaVersion,
                          dg::Quoted(snapshot.startupObservationMode)}));
    assert(output->EndStartupCandidates({snapshot.state,snapshot.lastError,
        dg::Quoted(snapshot.detail),dg::Quoted(snapshot.baselineBuildId),
        dg::Quoted(snapshot.patchId),snapshot.generation,
        snapshot.retainedBytes,snapshot.expected,snapshot.staged}));
    assert(output->EndClosure() && output->EndStableAot() &&
           output->EndCommitOrder() && output->EndAssemblies() &&
           output->EndEvents() && output->EndBaselineUses(snapshot.enumerationGeneration));
    for(size_t i=0;i<owned->Count();++i)
    {
        phys::Row row;assert(owned->Get(i,row));
        assert(output->AddOrdinaryAssembly({{row.name,row.bytes},row.interpreter}));
    }
    assert(output->EndOrdinaryAssemblies(snapshot.classEnumerationGeneration));
    assert(output->Freeze());
    assert(output->Bytes()==original.size());
    assert(std::memcmp(output->Data(),original.data(),original.size())==0);
    assert(original.size()>262144);
    std::cout<<"P1 actual physical source reader + MOCK other VM fields: original/current/"
             <<"frozen V1 exact bytes="<<original.size()<<" passed\n";
}
}} // il2cpp::vm

int main()
{
    il2cpp::vm::VerifyBasic();
    il2cpp::vm::ExactlyFullAndOverfull();
    il2cpp::vm::FailClosed();
    il2cpp::vm::OriginalV1Parity();
    std::cout<<"P1 physical assembly exact source method, fixed owned inventory and V1 parity PASS\n";
}
