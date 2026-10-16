// Scoped R03 P1 source-owner composition test against current production headers.
// All input capture instances are REAL bounded C++ classes, initialized under
// explicitly modeled source provenance. There is NO real VM owner, global
// snapshot epoch, Unity SDK, terminal scheduler, filesystem or Player here.
#include "AssemblyShadowBoundedGeneralComposer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <locale>
#include <memory>
#include <string>
#include <vector>
extern bool watch;
extern size_t newCalls;

namespace il2cpp { namespace vm { namespace {
namespace bc=assembly_shadow_bounded_general;
namespace fi=assembly_shadow_bounded_first_use;
namespace inv=assembly_shadow_bounded_inventory;
namespace phy=assembly_shadow_bounded_physical;
namespace cls=assembly_shadow_bounded_classes;
namespace fr=assembly_shadow_frozen_general;
namespace tx=assembly_shadow_bounded_transaction;
namespace dw=assembly_shadow_diagnostic;

struct Sources
{
    std::unique_ptr<inv::Capture> inventory{new inv::Capture()};
    std::unique_ptr<fi::Capture> uses{new fi::Capture()};
    std::unique_ptr<phy::Capture> ordinary{new phy::Capture()};
    std::unique_ptr<cls::Capture> types{new cls::Capture()};
    int classIdentity=17;
    std::string copiedStable="Stable";
    std::string copiedClassName="Class";
    uint64_t classGeneration=55;

    explicit Sources(uint64_t generation=55) :classGeneration(generation)
    {
        tx::Input status{};
        status.state=AssemblyShadowState::FailedAfterCommit;
        status.lastError=AssemblyShadowError::BaselineAlreadyUsed;
        status.detail=dw::Quoted("Original detail");
        status.baselineBuildId=dw::Quoted("baseline-id");
        status.patchId=dw::Quoted("patch-id");
        status.generation=55;
        status.retainedBytes=42;
        status.expected=2;
        status.staged=2;
        assert(inventory->Begin(status));
        assert(inventory->AddStable(dw::Quoted(copiedStable)));
        assert(inventory->EndStable());
        assert(inventory->AddClosure({dw::Quoted("Closure"),dw::Quoted("mvid"),
             true,true,false,true,false}));
        assert(inventory->EndClosure());
        assert(inventory->AddCommit(dw::Quoted("Commit")));
        assert(inventory->EndCommit());
        assert(inventory->AddEvent({9,55,2,dw::Quoted("Committed"),dw::Quoted("Closure")}));
        assert(inventory->EndLocked());
        assert(inventory->Seal(true,false,AssemblyShadowError::Success,
                               dw::Quoted(""),true));

        assert(uses->Begin(3));
        fi::Raw first{};
        first.present=true;
        first.kind=BaselineUseKind::MethodExecution;
        std::memcpy(first.detail,"call",5);
        first.thread=11; first.timestamp=12; first.sequence=7;
        assert(uses->AddRaw(first));
        fi::Raw absent{};
        assert(uses->AddRaw(absent));
        fi::Raw withClass{};
        withClass.present=true;
        withClass.kind=BaselineUseKind::ClassInit;
        std::memcpy(withClass.detail,"init",5);
        withClass.opaqueClass=&classIdentity;
        withClass.thread=22;withClass.timestamp=23;withClass.sequence=8;
        assert(uses->AddRaw(withClass));
        assert(uses->EndRaw(19));
        assert(uses->AddCandidateName(0,dw::Quoted("CandidateA")));
        assert(uses->AddCandidateName(1,dw::Quoted("NoUse")));
        assert(uses->AddCandidateName(2,dw::Quoted("CandidateB")));
        assert(uses->EndNames());
        assert(uses->ResolvePhysicalClassCString(2,&classIdentity,"",
                                                 copiedClassName.c_str()));
        assert(uses->Seal(19,true));

        assert(ordinary->Begin());
        assert(ordinary->Add("Physical.C",true));
        assert(ordinary->Add("Physical.D",false));
        assert(ordinary->Seal(55,true));

        assert(types->Begin());
        assert(types->Add("Physical.C","Ns","Class",true,false,true));
        assert(types->Add("Physical.D",nullptr,"Solo",false,true,false));
        assert(types->Seal(classGeneration,true));

        // The input strings no longer own the captured names.
        copiedStable.assign("destroyed stable");
        copiedClassName.assign("destroyed class");
    }

    bc::Inputs Input(const dw::QuotedText* candidates,size_t count) const
    {
        bc::Inputs input{};
        input.header={true,1,dw::Quoted("EarlyTracking")};
        input.startupCandidateNames=candidates;
        input.startupCandidateCount=count;
        input.transaction=inventory.get();
        input.firstUses=uses.get();
        input.ordinaryAssemblies=ordinary.get();
        input.ordinaryClasses=types.get();
        return input;
    }
};

static ShadowDiagnosticSnapshot Reference(const std::string& controlName)
{
    ShadowDiagnosticSnapshot expected;
    expected.enabled=true;
    expected.startupCandidateSchemaVersion=1;
    expected.startupObservationMode="EarlyTracking";
    expected.startupCandidateNames={controlName,"CandidateB"};
    expected.state=AssemblyShadowState::FailedAfterCommit;
    expected.lastError=AssemblyShadowError::BaselineAlreadyUsed;
    expected.detail="Original detail";
    expected.baselineBuildId="baseline-id";
    expected.patchId="patch-id";
    expected.generation=55;
    expected.retainedBytes=42;
    expected.expected=2;
    expected.staged=2;
    expected.closureLoadOrder={"Closure"};
    expected.stableAotNames={"Stable"};
    expected.commitOrder={"Commit"};
    ShadowAssemblyDiagnostic a{};
    a.name="Closure";
    a.mvid="mvid";
    a.skeletonBuilt=true;
    a.runtimeMetadataInitialized=true;
    a.published=false;
    a.moduleInitializerAttempted=true;
    a.moduleInitializerRan=false;
    expected.assemblies.push_back(a);
    ShadowEventDiagnostic event{};
    event.sequence=9;event.kind="Committed";event.name="Closure";
    event.generation=55;event.stagedCount=2;
    expected.events.push_back(event);
    ShadowUseDiagnostic use{};
    use.name="CandidateA";use.kind=BaselineUseKind::MethodExecution;
    use.detail="call FirstUseSequence=7";use.type="";
    use.thread=11;use.timestamp=12;
    expected.baselineUses.push_back(use);
    ShadowUseDiagnostic klassUse{};
    klassUse.name="CandidateB";klassUse.kind=BaselineUseKind::ClassInit;
    klassUse.detail="init FirstUseSequence=8";klassUse.type=".Class";
    klassUse.thread=22;klassUse.timestamp=23;
    expected.baselineUses.push_back(klassUse);
    expected.enumerationGeneration=55;
    expected.ordinaryAssemblies.push_back({"Physical.C",true});
    expected.ordinaryAssemblies.push_back({"Physical.D",false});
    expected.classEnumerationGeneration=55;
    ShadowDiagnosticSnapshot::OrdinaryClass c{};
    c.assemblyName="Physical.C";c.typeName="Ns.Class";
    c.isInterpreter=true;c.isConstructedGeneric=false;c.usesStagedMetadata=true;
    expected.ordinaryClasses.push_back(c);
    ShadowDiagnosticSnapshot::OrdinaryClass d{};
    d.assemblyName="Physical.D";d.typeName="Solo";
    d.isInterpreter=false;d.isConstructedGeneric=true;d.usesStagedMetadata=false;
    expected.ordinaryClasses.push_back(d);
    return expected;
}
static void BaseParity()
{
    Sources source;
    std::string initial("CandidateA\0embedded",19);
    const auto a=dw::Quoted(initial);
    const dw::QuotedText names[]={a,dw::Quoted("CandidateB")};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(bc::Compose(source.Input(names,2),*out,receipt));
    const auto reference=Reference(initial);
    const std::string expected=AssemblyShadowDiagnostics::Serialize(reference);
    assert(out->Complete() && out->Bytes()==expected.size());
    assert(std::memcmp(out->Data(),expected.data(),expected.size())==0);
    assert(receipt.componentSourcesComplete);
    assert(receipt.startupNames==2 && receipt.transactionStable==1);
    assert(receipt.transactionClosure==1 && receipt.transactionCommit==1);
    assert(receipt.transactionEvents==1 && receipt.baselineUses==2);
    assert(receipt.ordinaryAssemblies==2 && receipt.ordinaryClasses==2);
    assert(receipt.ordinaryGeneration==55 && receipt.usageGeneration==19);
    assert(receipt.sourceTextBytes>0 && receipt.serializedBytes==expected.size());
    assert(!receipt.coherentVmEpochProven && !receipt.runtimeAcceptance);
    // All source bytes have been released from the original mutable text.
    initial.assign("source changed");
    assert(std::memcmp(out->Data(),expected.data(),expected.size())==0);
    std::cout<<"GROUP:byte-exact-general-owned-parity\n";
}
static void AllocationAfterPrewarm()
{
    Sources source;
    dw::QuotedText startup[]={dw::Quoted("CandidateA"),dw::Quoted("CandidateB")};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    // Account only synchronous C++ capture/formatting, NOT preparation or
    // source-class iterator/GC cost. std::locale may have process setup cost.
    (void)std::locale();
    newCalls=0;watch=true;
    const bool ok=bc::Compose(source.Input(startup,2),*out,receipt);
    const size_t allocations=newCalls;
    watch=false;
    assert(ok && out->Complete());
    std::cout<<"ALLOCATIONS_AFTER_PREWARM:"<<allocations<<"\n";
    // The formatter used in prior bounded source CI is allocation-free
    // after locale initialization. An observed new cost must be reviewed.
    assert(allocations==0);
    std::cout<<"GROUP:composition-no-new-allocation\n";
}
static void MissingSourcesFailBeforeAnyPrefix()
{
    Sources source;
    dw::QuotedText startup[]={dw::Quoted("CandidateA")};
    bc::Inputs input=source.Input(startup,1);
    input.transaction=nullptr;
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(!bc::Compose(input,*out,receipt));
    assert(receipt.error==bc::Error::InvalidInput);
    assert(!out->Complete() && out->Data()==nullptr && out->Bytes()==0);
    assert(out->FirstError()==fr::CaptureError::InvalidInput);
    assert(!receipt.componentSourcesComplete);
    std::cout<<"GROUP:missing-source-fail-closed\n";
}
static void IncompleteSourcesFailClosed()
{
    Sources source;
    fi::Capture virgin;
    dw::QuotedText startup[]={dw::Quoted("CandidateA")};
    auto input=source.Input(startup,1);
    input.firstUses=&virgin;
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(!bc::Compose(input,*out,receipt));
    assert(receipt.error==bc::Error::IncompleteSource);
    assert(!out->Complete() && out->Bytes()==0);
    std::cout<<"GROUP:incomplete-source-fail-closed\n";
}
static void GenerationMismatchRefused()
{
    Sources source(56);
    dw::QuotedText startup[]={dw::Quoted("CandidateA")};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(!bc::Compose(source.Input(startup,1),*out,receipt));
    assert(receipt.error==bc::Error::GenerationMismatch);
    assert(!out->Complete() && out->Bytes()==0);
    std::cout<<"GROUP:cross-reader-generation-mismatch\n";
}
static void StartupInputValidation()
{
    Sources source;
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    auto input=source.Input(nullptr,1);
    assert(!bc::Compose(input,*out,receipt));
    assert(receipt.error==bc::Error::InvalidInput && !out->Complete());
    auto second=std::make_unique<fr::Capture>();
    input=source.Input(nullptr,bc::kMaxStartupNames+1);
    assert(!bc::Compose(input,*second,receipt));
    assert(receipt.error==bc::Error::InvalidInput && !second->Complete());
    std::cout<<"GROUP:startup-source-bounds\n";
}
static void InvalidBorrowedNameFailsClosed()
{
    Sources source;
    dw::QuotedText candidates[]={{nullptr,0}};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(!bc::Compose(source.Input(candidates,1),*out,receipt));
    assert(receipt.error==bc::Error::OutputFailed);
    assert(!out->Complete() && out->Bytes()==0);
    std::cout<<"GROUP:invalid-borrowed-startup-refusal\n";
}
static void TotalByteLimitFailsClosed()
{
    Sources source;
    std::string huge(1200,'x');
    std::vector<dw::QuotedText> names(bc::kMaxStartupNames,dw::Quoted(huge));
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(!bc::Compose(source.Input(names.data(),names.size()),*out,receipt));
    assert(receipt.error==bc::Error::OutputFailed);
    assert(!out->Complete() && out->Data()==nullptr && out->Bytes()==0);
    std::cout<<"GROUP:complete-output-budget-refusal\n";
}
static void InvalidUtf8FailsClosed()
{
    Sources source;
    const char invalid[]={char(0xff)};
    dw::QuotedText names[]={{invalid,1}};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(!bc::Compose(source.Input(names,1),*out,receipt));
    assert(receipt.error==bc::Error::OutputFailed);
    assert(!out->Complete() && out->Bytes()==0);
    std::cout<<"GROUP:invalid-utf8-refusal\n";
}
static void ReentrantUseInvalidatesOldCompleteRecord()
{
    Sources source;
    dw::QuotedText names[]={dw::Quoted("A")};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt first,second;
    auto input=source.Input(names,1);
    assert(bc::Compose(input,*out,first));
    assert(out->Complete());
    assert(!bc::Compose(input,*out,second));
    assert(second.error==bc::Error::OutputFailed);
    assert(!out->Complete() && out->Bytes()==0);
    std::cout<<"GROUP:single-owner-single-freeze\n";
}
static void ExplicitNoGlobalEpochProof()
{
    Sources source;
    dw::QuotedText names[]={dw::Quoted("A")};
    auto out=std::make_unique<fr::Capture>();
    bc::Receipt receipt;
    assert(bc::Compose(source.Input(names,1),*out,receipt));
    assert(receipt.componentSourcesComplete);
    assert(!receipt.coherentVmEpochProven);
    assert(!receipt.runtimeAcceptance);
    std::cout<<"GROUP:no-coherent-vm-epoch-or-player-claim\n";
}
}}} // namespaces

int main()
{
    il2cpp::vm::BaseParity();
    il2cpp::vm::AllocationAfterPrewarm();
    il2cpp::vm::MissingSourcesFailBeforeAnyPrefix();
    il2cpp::vm::IncompleteSourcesFailClosed();
    il2cpp::vm::GenerationMismatchRefused();
    il2cpp::vm::StartupInputValidation();
    il2cpp::vm::InvalidBorrowedNameFailsClosed();
    il2cpp::vm::TotalByteLimitFailsClosed();
    il2cpp::vm::InvalidUtf8FailsClosed();
    il2cpp::vm::ReentrantUseInvalidatesOldCompleteRecord();
    il2cpp::vm::ExplicitNoGlobalEpochProof();
    std::cout<<"P1 BOUNDED GENERAL COMPOSER HOST PASS\n";
}
