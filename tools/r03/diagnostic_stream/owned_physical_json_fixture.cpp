// R03 P1 host fixture: ACTUAL production owned-name capture + pure owned JSON.
// No VM classes, metadata heap, unity SDK, file transport or Player execution.
#include "AssemblyShadowOwnedPhysicalNames.h"
#include "AssemblyShadowOwnedPhysicalNamesJson.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace names=il2cpp::vm::assembly_shadow_owned_physical_names;
namespace output=il2cpp::vm::assembly_shadow_owned_physical_json;
namespace witness=il2cpp::vm::assembly_shadow_execution_witness;

static void require(bool value,const char* why){
    if(!value)throw std::runtime_error(why);
}
struct Session {
    names::Capture source;
    names::Receipt sourceReceipt{};
    output::Record record;
    output::Receipt result{};
    size_t addedRows=0;
    void Begin(){ require(source.Begin(),"source begin"); }
    void Add(size_t slot,witness::Source role,const char* assembly,
             const char* ns,const char* simple){
        require(source.Add(slot,role,assembly,ns,simple),"source add");
        ++addedRows;
    }
    void Seal(uint64_t revision=7) {
        require(source.Seal(addedRows),"source seal");
        sourceReceipt.observedClasses=source.Count();
        sourceReceipt.matchedClasses=source.Count();
        sourceReceipt.ownedRows=source.Count();
        sourceReceipt.boundRegistrationRows=source.Count();
        sourceReceipt.ownedTextBytes=source.TextBytes();
        sourceReceipt.registryRevisionBefore=revision;
        sourceReceipt.registryRevisionAfter=revision;
        sourceReceipt.namesCopiedFromMatchedEnumeratorCallbacks=true;
        sourceReceipt.allRowsBoundToObservedRegistry=true;
    }
    bool Write(size_t limit=output::Record::kMaximumBytes){
        return output::Compose(source,sourceReceipt,record,result,limit);
    }
    std::string Json() const{
        require(record.Complete()&&record.Data()&&record.Bytes(),"no completed JSON");
        return std::string(record.Data(),record.Bytes());
    }
};
static std::unique_ptr<Session> make(){
    return std::unique_ptr<Session>(new Session);
}
static void check(const char* label,const std::function<void()>& fn){
    fn();std::cout<<"PASS "<<label<<"\n";
}
int main(){
 try {
 check("empty-owned-json",[](){
    auto p=make();p->Begin();p->Seal(0);require(p->Write(),"empty compose");
    const auto json=p->Json();
    require(json.find("\"observedRows\":0")!=std::string::npos,"empty count");
    require(json.find("\"rows\":[]}")!=std::string::npos,"empty rows");
    require(p->result.rows==0&&p->result.candidateRows==0&&
            p->result.activeShadowRows==0,"empty census");
 });
 check("one-candidate-exact-json",[](){
    auto p=make();p->Begin();
    p->Add(17,witness::Source::Candidate,"Physical","N","C");p->Seal();
    require(p->Write(),"one compose");
    const std::string expected=
       "{\"schemaVersion\":1,\"kind\":\"R03P1ObservedPhysicalDisplayNames\","
       "\"scope\":\"OwnedSnapshotLocalDisplayNamesOnly\",\"registryRevision\":7,"
       "\"observedRows\":1,\"candidateRows\":1,\"activeShadowRows\":0,"
       "\"canonicalExecutionTypeKeys\":false,\"physicalClassPointerOriginProven\":false,"
       "\"coherentVmEpochProven\":false,\"runtimeAcceptance\":false,\"rows\":["
       "{\"registrySlot\":17,\"registrationSource\":\"Candidate\","
       "\"physicalAssemblyName\":\"Physical\",\"namespace\":\"N\",\"simpleName\":\"C\"}]}";
    require(p->Json()==expected,"noncanonical owned JSON changed bytes/schema/order");
    require(p->result.rows==1&&p->result.serializedBytes==expected.size()&&
            p->result.ownedNonCanonicalJsonComplete&&
            p->result.allRowsBoundToObservedRegistry,"census incomplete");
    require(!p->result.canonicalExecutionTypeKeys&&!p->result.coherentVmEpochProven&&
            !p->result.physicalClassPointerOriginProven&&!p->result.runtimeAcceptance,
            "false completeness claims");
 });
 check("reverse-source-order-and-partition",[](){
    auto p=make();p->Begin();
    p->Add(200,witness::Source::ActiveShadow,"A","Ns","Active");
    p->Add(4,witness::Source::Candidate,"B","","Candidate");p->Seal(8);
    require(p->Write(),"roles compose");
    auto j=p->Json();
    require(j.find("\"candidateRows\":1,\"activeShadowRows\":1")!=std::string::npos,
            "role totals");
    require(j.find("\"registrySlot\":200")<j.find("\"registrySlot\":4"),"source order lost");
    require(j.find("\"registrationSource\":\"ActiveShadow\"")!=std::string::npos,
            "source role not emitted");
 });
 check("same-display-name-distinct-registered-slots",[](){
    auto p=make();p->Begin();
    p->Add(1,witness::Source::Candidate,"A","N","Same");
    p->Add(2047,witness::Source::ActiveShadow,"A","N","Same");p->Seal();
    require(p->Write(),"duplicate display names must be distinct bound rows");
    require(p->result.rows==2&&p->result.activeShadowRows==1,
            "name collision merged registration identities");
    require(p->Json().find("\"registrySlot\":2047")!=std::string::npos,
            "last bound slot not reported");
 });
 check("escaping-quotes-backslash-and-controls",[](){
    auto p=make();p->Begin();
    p->Add(4,witness::Source::Candidate,"A\"\\","A\n\t","C\r");p->Seal();
    require(p->Write(),"quoted compose");
    const auto j=p->Json();
    require(j.find("A\\\"\\\\")!=std::string::npos,"quote/backslash not escaped");
    require(j.find("A\\u000a\\u0009")!=std::string::npos,"control chars not escaped");
    require(j.find("C\\u000d")!=std::string::npos,"carriage return not escaped");
 });
 check("valid-utf8-retained-unchanged",[](){
    auto p=make();p->Begin();
    p->Add(3,witness::Source::Candidate,"F\xC3\xA4","N","M\xE2\x82\xAC");p->Seal();
    require(p->Write(),"valid UTF8 refused");
    const auto j=p->Json();
    require(j.find("F\xC3\xA4")!=std::string::npos&&
            j.find("M\xE2\x82\xAC")!=std::string::npos,"UTF8 altered");
 });
 check("invalid-utf8-continuation-refused",[](){
    const char invalid[]={'N',static_cast<char>(0x80),0};
    auto p=make();p->Begin();
    p->Add(4,witness::Source::Candidate,"A",invalid,"T");p->Seal();
    require(!p->Write(),"invalid UTF8 accepted");
    require(p->result.error==output::Error::InvalidUtf8 &&
            !p->record.Complete()&&p->record.Data()==nullptr&&p->record.Bytes()==0,
            "failed invalid UTF8 exposed bytes");
 });
 check("overlong-and-surrogate-utf8-refused",[](){
    const char overlong[]={static_cast<char>(0xc0),static_cast<char>(0xaf),0};
    const char surrogate[]={static_cast<char>(0xed),static_cast<char>(0xa0),
                           static_cast<char>(0x80),0};
    for(const char* name:{overlong,surrogate}){
        auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N",name);p->Seal();
        require(!p->Write()&&p->result.error==output::Error::InvalidUtf8,"bad scalar accepted");
        require(p->record.Data()==nullptr,"invalid scalar leaked output");
    }
 });
 check("empty-required-physical-assembly-refused",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"","N","C");p->Seal();
    require(!p->Write()&&p->result.error==output::Error::InvalidName,"empty assembly accepted");
 });
 check("empty-required-simple-name-refused",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","");p->Seal();
    require(!p->Write()&&p->result.error==output::Error::InvalidName,"empty type accepted");
 });
 check("invalid-source-capture-refused",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");
    // Capture is not sealed, regardless of forged successful receipt values.
    p->sourceReceipt.namesCopiedFromMatchedEnumeratorCallbacks=true;
    p->sourceReceipt.allRowsBoundToObservedRegistry=true;
    require(!p->Write()&&p->result.error==output::Error::IncompleteSource,
            "unfinished capture accepted");
 });
 check("missing-source-witness-flag-refused",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");p->Seal();
    p->sourceReceipt.allRowsBoundToObservedRegistry=false;
    require(!p->Write()&&p->result.error==output::Error::InvalidReceipt,
            "missing binding proof accepted");
 });
 check("mismatched-row-count-and-text-byte-receipts-refused",[](){
    for(int mode=0;mode<2;++mode){
        auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");p->Seal();
        if(mode==0)++p->sourceReceipt.boundRegistrationRows;
        else ++p->sourceReceipt.ownedTextBytes;
        require(!p->Write()&&p->result.error==output::Error::InvalidReceipt,
                "inconsistent source receipt promoted");
    }
 });
 check("revision-and-saturated-revision-refused",[](){
    for(int mode=0;mode<2;++mode){
        auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");p->Seal();
        if(mode==0)++p->sourceReceipt.registryRevisionAfter;
        else p->sourceReceipt.registryRevisionBefore=
                 p->sourceReceipt.registryRevisionAfter=UINT64_MAX;
        require(!p->Write()&&p->result.error==output::Error::InvalidReceipt,
                "invalid revision promoted");
    }
 });
 check("forbidden-success-scope-refused",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");p->Seal();
    p->sourceReceipt.completeExecutionTypeKeys=true;
    require(!p->Write()&&p->result.error==output::Error::InvalidReceipt,
            "canonical TypeKey falsely authorized");
 });
 check("small-byte-budget-atomically-refused",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");p->Seal();
    require(!p->Write(200),"too-small output accepted");
    require(p->result.error==output::Error::OutputCapacity &&
            p->record.FirstError()==output::Error::OutputCapacity &&
            p->record.Data()==nullptr&&p->record.Bytes()==0&&
            !p->result.ownedNonCanonicalJsonComplete,
            "partial output exposed after budget refusal");
 });
 check("output-limit-above-fixed-cap-refused",[](){
    auto p=make();p->Begin();p->Seal();
    require(!p->Write(output::Record::kMaximumBytes+1),"unbounded output accepted");
    require(p->result.error==output::Error::InvalidOutputLimit &&
            !p->record.Complete(),"invalid limit record accepted");
 });
 check("single-use-output-revokes-on-reuse",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","C");p->Seal();
    require(p->Write(),"first compose");
    require(!p->Write()&&p->result.error==output::Error::ReusedOutput,
            "repeated writer accepted");
    require(p->record.Data()==nullptr&&p->record.Bytes()==0,"old success retained after reuse");
 });
 check("owned-json-survives-source-revoke",[](){
    auto p=make();p->Begin();p->Add(8,witness::Source::Candidate,"A","N","Before");p->Seal();
    require(p->Write(),"compose before source revoke");
    const auto old=p->Json();p->source.Revoke();
    require(!p->source.Complete()&&p->Json()==old,"owned bytes depend on mutable source");
 });
 check("source-string-mutation-before-compose-independent",[](){
    char sourceName[32]="MutableBefore";
    auto p=make();p->Begin();
    p->Add(8,witness::Source::Candidate,sourceName,"N","C");p->Seal();
    std::strcpy(sourceName,"After");
    require(p->Write(),"compose after original metadata text mutation");
    require(p->Json().find("MutableBefore")!=std::string::npos&&
            p->Json().find("\"After\"")==std::string::npos,
            "borrowed mutable text retained");
 });
 check("worst-case-escaped-text-under-fixed-budget",[](){
    auto p=make();p->Begin();
    std::string text(4000,'\n');
    for(size_t i=0;i<60;++i)
        p->Add(i+1,witness::Source::Candidate,"A","N",text.c_str());
    p->Seal();
    require(p->Write(),"bounded worst-case escape exceeds fixed record");
    require(p->record.Bytes()>p->source.TextBytes()*5 &&
            p->record.Bytes()<output::Record::kMaximumBytes,
            "escape expansion budget not respected");
    require(p->result.rows==60&&p->result.candidateRows==60,
            "max escaped row census");
 });
 std::cout<<"P1 OWNED PHYSICAL JSON HOST PASS\n";
 return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;
 }
}
