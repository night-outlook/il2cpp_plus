// Standalone exact-byte contract for the VM execution-diagnostic formatter.
// No Unity/Player/VM capture is simulated or accepted by this test.
#include "AssemblyShadowExecutionSerializer.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <iostream>
using namespace il2cpp::vm;
using namespace assembly_shadow_execution_diagnostic;
using assembly_shadow_diagnostic::Writer;
using assembly_shadow_diagnostic::WriteError;

static bool Accept(void* context,const char* bytes,size_t length) noexcept
{
    try { static_cast<std::string*>(context)->append(bytes,length);return true; }
    catch (...) { return false; }
}
static bool Deny(void*,const char*,size_t) noexcept { return false; }

int main()
{
    Header h;h.stateCode=9;h.state="FailedAfterCommit";h.generation=std::numeric_limits<uint64_t>::max();
    h.methodChecks=9223372036854775808ull;h.shadowMethodChecks=51;h.rejectedBaselineMethods=3;
    h.baselineClassCctorStarted=4;h.shadowClassCctorStarted=5;h.interpreterTransformations=6;
    h.shadowInterpreterTransformations=7;h.droppedClassObservations=8;
    const std::string type="type(2:A\"\n\t\\\x01)";
    ClassRow first; first.logicalAssembly=Quoted("A");first.typeKey=Quoted(type);
    first.interpreterShadow=true;first.physicalInterpreter=true;first.isActive=true;first.cctorStarted=true;
    first.staticStoragePointer=Quoted("0xff");first.pointerDetailsAvailable=true;first.staticStorageAvailable=true;
    ClassRow second=first;second.typeKey=Quoted("type(primitive)");second.interpreterShadow=false;
    second.physicalInterpreter=false;second.isActive=false;second.staticStoragePointer=Quoted("");
    second.pointerDetailsAvailable=false;second.staticStorageAvailable=false;
    const auto format = [&](auto& output) {
        Begin(output,h);AppendClass(output,first,false);AppendClass(output,second,true);
        EndClasses(output);output << ",\"r02\":{\"schemaVersion\":1}}";
    };
    // Independent literal V1 field order and precise control-byte escaping.
    const std::string golden=R"golden({"schemaVersion":1,"enabled":true,"stateCode":9,"state":"FailedAfterCommit","generation":18446744073709551615,"methodChecks":9223372036854775808,"shadowMethodChecks":51,"rejectedBaselineMethods":3,"baselineClassCctorStarted":4,"shadowClassCctorStarted":5,"interpreterTransformations":6,"shadowInterpreterTransformations":7,"droppedClassObservations":8,"classes":[{"logicalAssembly":"A","typeKey":"type(2:A\"\u000a\u0009\\\u0001)","executionModeCode":1,"executionMode":"InterpreterShadow","physicalImageKind":"Interpreter","isActive":true,"cctorStarted":true,"cctorFinished":false,"hasInitializationException":false,"staticStoragePointer":"0xff","pointerDetailsAvailable":true,"staticStorageAvailable":true},{"logicalAssembly":"A","typeKey":"type(primitive)","executionModeCode":0,"executionMode":"AotBaseline","physicalImageKind":"Aot","isActive":false,"cctorStarted":true,"cctorFinished":false,"hasInitializationException":false,"staticStoragePointer":"","pointerDetailsAvailable":false,"staticStorageAvailable":false}],"r02":{"schemaVersion":1}})golden";
    std::ostringstream legacy;format(legacy);
    assert(legacy.str()==golden);
    std::string streamed;Writer exact({&streamed,Accept},golden.size());format(exact);
    const auto receipt=exact.Finish();
    assert(receipt.complete && receipt.error==WriteError::None && receipt.bytesAccepted==golden.size());
    assert(streamed==golden);
    std::string shortOutput;Writer shortWriter({&shortOutput,Accept},golden.size()-1);format(shortWriter);
    const auto shortResult=shortWriter.Finish();
    assert(!shortResult.complete && shortResult.error==WriteError::ByteLimit && shortResult.bytesAccepted<golden.size());
    Writer denied({nullptr,Deny},golden.size());format(denied);
    const auto deniedResult=denied.Finish();
    assert(!deniedResult.complete && deniedResult.error==WriteError::SinkFailure && deniedResult.bytesAccepted==0);
    const std::string nulKey("X\0Y",3);first.typeKey=Quoted(nulKey);
    std::ostringstream withNul;format(withNul);
    assert(withNul.str().find("X\\u0000Y")!=std::string::npos);
    std::cout << "R03 execution formatter: legacy-golden, bounded byte parity, overlimit, sink-denial, embedded-NUL passed\n";
}
