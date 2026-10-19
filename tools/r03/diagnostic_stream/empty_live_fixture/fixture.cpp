#include "MockRuntime.h"
#include "AssemblyShadowBoundedExecutionEmptyLive.h"
namespace il2cpp { namespace vm {
class AssemblyShadow {
public:
 static bool TryCaptureBoundedEmptyExecutionLive(
  assembly_shadow_frozen_execution_record::Record&,
  assembly_shadow_bounded_execution_empty_live::Receipt&);
};
#include "AssemblyShadowBoundedExecutionEmptyLiveSource.inl"
}}
#include <iostream>
#include <stdexcept>
using namespace il2cpp::vm;
namespace live=il2cpp::vm::assembly_shadow_bounded_execution_empty_live;
namespace r02=il2cpp::vm::assembly_shadow_r02;
namespace frozen=il2cpp::vm::assembly_shadow_frozen_execution_record;
void require(bool b,const char* cause) {if(!b)throw std::runtime_error(cause);}
void reset() {
 s_active.store(&s_mainActive);s_state.store(AssemblyShadowState::Committed);
 s_executionClasses.fill(nullptr);s_executionClassCount=0;
 g_classSamples=0;g_lockDepth=0;g_onClassSample=nullptr;g_onFreeze=nullptr;
 r02::g_level=2;r02::g_captureCalls=0;r02::g_failCaptureAt=0;
 r02::g_dropped=0;r02::g_methodChecks=8;r02::g_droppedClasses=0;
 r02::g_baselineCctors=1;r02::g_saturated=false;r02::g_onCounterSample=nullptr;
 assembly_shadow_metadata_lifetime::g_admissionOpen=true;
 require(assembly_shadow_metadata_lifetime::g_readers==0,"metadata admission leak");
}
void run(const char* name,live::Error failure,std::function<void(frozen::Record&)> setup={}) {
 reset();frozen::Record out;live::Receipt receipt;
 if(setup)setup(out);
 const bool result=AssemblyShadow::TryCaptureBoundedEmptyExecutionLive(out,receipt);
 require(result==(failure==live::Error::None),"unexpected return");
 require(receipt.error==failure,"wrong error classification");
 require(assembly_shadow_metadata_lifetime::g_readers==0,"metadata reader leak");
 require(g_lockDepth==0,"execution lock leak");
 require(receipt.classPointerOriginProven==false && receipt.coherentVmEpochProven==false &&
  receipt.originalLiveGettersReplaced==false && receipt.playerExecution==false &&
  receipt.runtimeAcceptance==false,"false acceptance flags");
 if(result){
  require(out.Complete() && receipt.observedSourceFenceStable && receipt.zeroClassSubsetComplete,"missing complete output");
  require(out.recordedState=="Committed" && out.recordedGeneration==4,"wrong identity");
  require(g_classSamples==3 && r02::g_captureCalls==2,"unbounded/under-sampled success");
 } else require(!out.Complete() && !receipt.zeroClassSubsetComplete,"partial success");
 std::cout<<"PASS "<<name<<"\n";
}
int main(){
 try {
  run("level2-zero-class",live::Error::None);
  run("level1-zero-class",live::Error::None,[](auto&){r02::g_level=1;});
  run("level0-zero-class",live::Error::None,[](auto&){r02::g_level=0;r02::g_methodChecks=0;r02::g_baselineCctors=0;});
  run("reused-frozen-output",live::Error::ReusedOutput,[](auto& r){r.RejectInvalidSource();});
  run("metadata-closed",live::Error::MetadataAdmissionUnavailable,[](auto&){assembly_shadow_metadata_lifetime::g_admissionOpen=false;});
  run("nonempty-class-inventory",live::Error::ClassRowsNotSupported,[](auto&){
    s_executionClasses[0]=reinterpret_cast<Il2CppClass*>(uintptr_t(3));s_executionClassCount=1;});
  run("inconsistent-class-inventory",live::Error::InvalidClassObservation,[](auto&){s_executionClassCount=1;});
  run("counter-first-failure",live::Error::CounterCaptureFailed,[](auto&){r02::g_failCaptureAt=1;});
  run("counter-second-failure",live::Error::CounterCaptureFailed,[](auto&){r02::g_failCaptureAt=2;});
  run("dropped-threads-refused",live::Error::IncompleteCoverage,[](auto&){r02::g_dropped=1;});
  run("saturated-refused",live::Error::IncompleteCoverage,[](auto&){r02::g_saturated=true;});
  run("dropped-class-rows-refused",live::Error::IncompleteCoverage,[](auto&){r02::g_droppedClasses=1;});
  run("state-changed-mid-source",live::Error::ObservedSourceChange,[](auto&){
    r02::g_onCounterSample=[](){s_state.store(AssemblyShadowState::Failed);r02::g_onCounterSample=nullptr;};});
  run("class-added-mid-source",live::Error::ObservedSourceChange,[](auto&){
    r02::g_onCounterSample=[](){s_executionClasses[0]=reinterpret_cast<Il2CppClass*>(uintptr_t(3));s_executionClassCount=1;r02::g_onCounterSample=nullptr;};});
  run("state-changed-during-format",live::Error::ObservedSourceChange,[](auto&){
    g_onFreeze=[](){s_state.store(AssemblyShadowState::Failed);};});
  run("counter-changed-during-format",live::Error::ObservedSourceChange,[](auto&){
    g_onFreeze=[](){++r02::g_methodChecks;};});
  run("generation-changed-during-format",live::Error::ObservedSourceChange,[](auto&){
    g_onFreeze=[](){s_active.store(&s_altActive);};});
  run("class-added-during-format",live::Error::ObservedSourceChange,[](auto&){
    g_onFreeze=[](){s_executionClasses[0]=reinterpret_cast<Il2CppClass*>(uintptr_t(3));s_executionClassCount=1;};});
  run("begin-refusal",live::Error::BeginFailed,[](auto& r){r.failBegin=true;});
  run("freeze-refusal",live::Error::FreezeFailed,[](auto& r){r.failFreeze=true;});
  std::cout<<"P1 EMPTY LIVE EXECUTION SOURCE HOST PASS\n";
 } catch(const std::exception& e) {std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
