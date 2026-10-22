// R03 P1: compile the ACTUAL production admitted-execution diagnostic .inl
// and ACTUAL metadata lifetime gate, NOT a copied implementation.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include "AssemblyShadowMetadataLifetime.h"

namespace il2cpp {namespace vm {
enum class AssemblyShadowError:int32_t {
 Success=0,InvalidState=2,ReferenceResolutionFailed=13
};
namespace assembly_shadow_metadata_lifetime {Gate g_MetadataLifetimeGate;}
namespace {
std::atomic<int> calls{0};
std::atomic<int> admissionAtCall{0};
std::atomic<bool> failInner{false},throwInner{false};
std::atomic<bool> holdInner{false},innerEntered{false},releaseInner{false};
}
class AssemblyShadow {
public:
 static AssemblyShadowError GetExecutionDiagnosticsJson(std::string& json);
 static AssemblyShadowError TryGetExecutionDiagnosticsWithMetadataAdmission(std::string& json);
};
AssemblyShadowError AssemblyShadow::GetExecutionDiagnosticsJson(std::string& json)
{
    calls.fetch_add(1,std::memory_order_relaxed);
    if (assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==0)
        throw std::runtime_error("called original getter without Reader");
    admissionAtCall.fetch_add(1,std::memory_order_relaxed);
    if(holdInner.load(std::memory_order_acquire)){
        innerEntered.store(true,std::memory_order_release);
        while(!releaseInner.load(std::memory_order_acquire))std::this_thread::yield();
    }
    if(throwInner.load(std::memory_order_relaxed))
        throw std::runtime_error("original getter deliberately failed");
    if(failInner.load(std::memory_order_relaxed))
        return AssemblyShadowError::ReferenceResolutionFailed;
    json="{\"existingLegacyV1\":true}";
    return AssemblyShadowError::Success;
}
#include "AssemblyShadowAdmittedExecutionDiagnosticsSource.inl"
}} // namespace vm
using namespace il2cpp::vm;
static void require(bool value,const char*message){if(!value)throw std::runtime_error(message);}
static void test(const char*name,void(*action)()){
    action();std::cout<<"PASS ADMITTED EXECUTION "<<name<<"\n";
}
int main(){
try{
 namespace life=assembly_shadow_metadata_lifetime;
 life::Gate& gate=life::g_MetadataLifetimeGate;
 test("closed-before-first-init",[] {
    std::string out="stale legacy V1";
    require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
      AssemblyShadowError::InvalidState && out.empty() && calls.load()==0,
      "closed metadata gate delegated to old getter or retained stale JSON");
 });
 test("original-reader-opens-once",[] {
    require(gate.OpenAfterInit() && !gate.Closed() && gate.ReaderCount()==0,
            "native metadata lifetime gate failed initial init");
 });
 test("actual-old-getter-inside-reader",[] {
    std::string out="stale";
    require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
            AssemblyShadowError::Success &&
            out=="{\"existingLegacyV1\":true}" &&
            calls.load()==1 && admissionAtCall.load()==1 &&
            gate.ReaderCount()==0,
            "legacy getter call not guarded from metadata teardown");
 });
 test("separate-output-owned-after-admission",[] {
    std::string out="different";
    require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
            AssemblyShadowError::Success &&
            out=="{\"existingLegacyV1\":true}" && gate.ReaderCount()==0,
            "output lost or retained a VM reader");
 });
 test("original-error-code-propagated",[] {
    failInner=true;std::string out="stale";
    require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
            AssemblyShadowError::ReferenceResolutionFailed &&
            out.empty() && gate.ReaderCount()==0,
            "inner failure code swallowed or stale output exposed");
    failInner=false;
 });
 test("original-exception-unwinds-reader",[] {
    throwInner=true;std::string out="stale";bool caught=false;
    try{(void)AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out);}
    catch(const std::runtime_error& e){caught=std::string(e.what())=="original getter deliberately failed";}
    throwInner=false;
    require(caught&&out.empty()&&gate.ReaderCount()==0,
            "getter exception leaked metadata Reader or was falsely accepted");
 });
 test("shutdown-blocked-until-reader-exits",[] {
    holdInner=true;innerEntered=false;releaseInner=false;
    std::atomic<bool> drained{false};
    std::thread reader([]{
        std::string out;
        require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
                AssemblyShadowError::Success && !out.empty(),
                "long old getter call unexpectedly refused");
    });
    for(int i=0;!innerEntered.load(std::memory_order_acquire)&&i<1000000;++i)
        std::this_thread::yield();
    require(innerEntered.load(std::memory_order_acquire),
            "reader did not enter actual delegated getter");
    std::thread shutdown([&]{gate.CloseAndDrain();drained=true;});
    for(int i=0;!gate.Closed()&&i<1000000;++i)std::this_thread::yield();
    require(gate.Closed()&&!drained.load(std::memory_order_acquire) &&
            gate.ReaderCount()==1,
            "shutdown reached metadata teardown before delegated getter left");
    releaseInner=true;
    reader.join();shutdown.join();
    holdInner=false;
    require(drained.load() && gate.ReaderCount()==0 && gate.Closed(),
            "metadata reader did not drain");
 });
 test("shutdown-refuses-without-calling-getter",[] {
    int before=calls.load();std::string out="stale";
    require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
            AssemblyShadowError::InvalidState &&
            out.empty() && calls.load()==before,
            "post-shutdown reader admitted or stale JSON escaped");
 });
 test("reopen-epoch-two-refuses-unproven-source",[] {
    require(gate.OpenAfterInit()&&!gate.Closed(),
            "new gate epoch unable to reopen after reader drain");
    int before=calls.load();std::string out="stale";
    require(AssemblyShadow::TryGetExecutionDiagnosticsWithMetadataAdmission(out)==
            AssemblyShadowError::InvalidState &&
            calls.load()==before&&gate.ReaderCount()==0&&out.empty(),
            "missing physical source epoch was replaced with current epoch");
    gate.CloseAndDrain();
 });
 std::cout<<"P1 INACTIVE EXECUTION METADATA ADMISSION HOST PASS\n";
 return 0;
}catch(const std::exception& e){
 std::cerr<<"FAIL admitted-execution: "<<e.what()<<"\n";return 1;
}
}
