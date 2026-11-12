// R03 B2 pure-byte observational parity tests. No fake VM and no Player.
// Actual header is compiled unchanged; both inputs must remain caller-owned.
// Equality here NEVER authenticates the original getter, metadata lifetime,
// terminal V2 completeness, or a coherent VM epoch.
#include "vm/AssemblyShadowExecutionV1ByteParity.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <string>

namespace p=il2cpp::vm::assembly_shadow_execution_v1_parity;
static unsigned cases=0,assertions=0;
static void Check(bool value,int line){
    ++assertions;
    if(!value){std::fprintf(stderr,"R03 observational V1 parity host case=%u line=%d\n",cases,line);std::abort();}
}
#define CHECK(v) Check(!!(v),__LINE__)

static p::View View(const char* str){return {str,std::strlen(str)};}
static void Equal(){
    ++cases;
    p::Probe probe;
    const char* observed="{\"schemaVersion\":1,\"classes\":[{\"typeKey\":\"type(3:abc/0:/1:A@0)\"}]}";
    CHECK(probe.Fresh());
    CHECK(probe.Compare(View(observed),View(observed)));
    const p::Receipt& r=probe.Result();
    CHECK(r.error==p::Error::None);
    CHECK(r.exactBytesObserved && r.twoIndependentObservationsOnly);
    CHECK(r.legacyBytes==r.boundedBytes && r.legacyBytes==std::strlen(observed));
    CHECK(!r.coherentVmEpochProven && !r.fullCanonicalTypeKeysProven &&
          !r.terminalSafeFinalExecutionDiagnostics &&
          !r.nativeV2TransportImplemented && !r.runtimeAcceptance &&
          !r.R03Accepted && !r.H2Passed);
    CHECK(!probe.Fresh());
    CHECK(!probe.Compare(View(observed),View(observed)));
    CHECK(probe.Result().error==p::Error::Reused);
    CHECK(!probe.Result().exactBytesObserved);
}
static void Different(){
    ++cases;
    p::Probe probe;
    const char* a="{\"classes\":[1,2]}";
    const char* b="{\"classes\":[1,3]}";
    CHECK(!probe.Compare(View(a),View(b)));
    CHECK(probe.Result().error==p::Error::DifferentByte);
    CHECK(probe.Result().firstDifferentOffset==14);
    CHECK(!probe.Result().exactBytesObserved);
    CHECK(probe.Result().legacyBytes==std::strlen(a));
}
static void DifferentLength(){
    ++cases;
    p::Probe probe;
    CHECK(!probe.Compare(View("ab"),View("abc")));
    CHECK(probe.Result().error==p::Error::DifferentLength);
    CHECK(probe.Result().firstDifferentOffset==2);
    CHECK(probe.Result().legacyBytes==2 && probe.Result().boundedBytes==3);
    CHECK(!probe.Result().twoIndependentObservationsOnly);
}
static void Missing(){
    ++cases;
    p::Probe probe;
    CHECK(!probe.Compare({nullptr,1},View("x")));
    CHECK(probe.Result().error==p::Error::MissingInput);
    CHECK(!probe.Result().exactBytesObserved);
    CHECK(!probe.RejectLegacy());
    CHECK(probe.Result().error==p::Error::Reused);
}
static void Empty(){
    ++cases;
    p::Probe probe;
    CHECK(!probe.Compare(View("x"),View("")));
    CHECK(probe.Result().error==p::Error::MissingInput);
}
static void OutsideBudget(){
    ++cases;
    p::Probe probe;
    CHECK(!probe.Compare({"x",p::Probe::kMaximumBytes+1},View("x")));
    CHECK(probe.Result().error==p::Error::OutsideBudget);
}
static void MissingLegacy(){
    ++cases;
    p::Probe probe;
    CHECK(!probe.RejectLegacy());
    CHECK(probe.Result().error==p::Error::UnavailableLegacy);
    CHECK(!probe.Result().exactBytesObserved);
}
static void MissingBounded(){
    ++cases;
    p::Probe probe;
    CHECK(!probe.RejectBounded());
    CHECK(probe.Result().error==p::Error::UnavailableBounded);
    CHECK(!probe.Result().exactBytesObserved);
}
static void LastByte(){
    ++cases;
    p::Probe probe;
    char a[]="long-original-json-a";
    char b[]="long-original-json-b";
    CHECK(!probe.Compare({a,sizeof a-1},{b,sizeof b-1}));
    CHECK(probe.Result().firstDifferentOffset==sizeof a-2);
    CHECK(probe.Result().error==p::Error::DifferentByte);
}
static void DistinctBuffers(){
    ++cases;
    p::Probe probe;
    std::string a="testing-original-JSON-row";
    std::string b=a;
    CHECK(probe.Compare({a.data(),a.size()},{b.data(),b.size()}));
    CHECK(probe.Result().exactBytesObserved);
    b[0]='X'; // Previously compared views are not retained.
    CHECK(probe.Result().exactBytesObserved);
    CHECK(!probe.Result().coherentVmEpochProven);
}
int main(){
    static_assert(!std::is_copy_constructible<p::Probe>::value,
                  "One owner and one comparison only");
    Equal();Different();DifferentLength();Missing();Empty();
    OutsideBudget();MissingLegacy();MissingBounded();LastByte();
    DistinctBuffers();
    std::printf("{\"kind\":\"R03B2ObservedExecutionV1ByteParityHost\","
                "\"result\":\"PassedPureNonAuthorityOnly\","
                "\"cases\":%u,\"assertions\":%u,\"realLegacyGetter\":false,"
                "\"vmEpochProven\":false,\"fullV1Field15Parity\":false,"
                "\"nativeV2TransportImplemented\":false,\"runtimeAcceptance\":false}\n",
                cases,assertions);
}
