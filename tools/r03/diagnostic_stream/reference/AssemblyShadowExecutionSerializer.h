#pragma once
// Pure execution-diagnostic JSON formatting over caller-owned immutable views.
// This is deliberately separate from VM/metadata capture. It neither acquires
// locks nor makes its input lifetime safe. Writer output is allocation-free;
// callers must check Finish() before accepting any bounded-sink result.
#include "AssemblyShadowDiagnosticWriter.h"
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_execution_diagnostic {
using assembly_shadow_diagnostic::QuotedText;
using assembly_shadow_diagnostic::Quoted;

struct Header
{
    int32_t stateCode = 0;
    const char* state = nullptr;
    uint64_t generation = 0;
    uint64_t methodChecks = 0;
    uint64_t shadowMethodChecks = 0;
    uint64_t rejectedBaselineMethods = 0;
    uint64_t baselineClassCctorStarted = 0;
    uint64_t shadowClassCctorStarted = 0;
    uint64_t interpreterTransformations = 0;
    uint64_t shadowInterpreterTransformations = 0;
    uint64_t droppedClassObservations = 0;
};
struct ClassRow
{
    QuotedText logicalAssembly{};
    QuotedText typeKey{};
    bool interpreterShadow = false;
    bool physicalInterpreter = false;
    bool isActive = false;
    bool cctorStarted = false;
    bool cctorFinished = false;
    bool hasInitializationException = false;
    QuotedText staticStoragePointer{};
    bool pointerDetailsAvailable = false;
    bool staticStorageAvailable = false;
};

template<class Output>
inline void Begin(Output& output, const Header& h)
{
    output << std::boolalpha << "{\"schemaVersion\":1,\"enabled\":true,\"stateCode\":" << h.stateCode
        << ",\"state\":" << Quoted(h.state)
        << ",\"generation\":" << h.generation
        << ",\"methodChecks\":" << h.methodChecks
        << ",\"shadowMethodChecks\":" << h.shadowMethodChecks
        << ",\"rejectedBaselineMethods\":" << h.rejectedBaselineMethods
        << ",\"baselineClassCctorStarted\":" << h.baselineClassCctorStarted
        << ",\"shadowClassCctorStarted\":" << h.shadowClassCctorStarted
        << ",\"interpreterTransformations\":" << h.interpreterTransformations
        << ",\"shadowInterpreterTransformations\":" << h.shadowInterpreterTransformations
        << ",\"droppedClassObservations\":" << h.droppedClassObservations
        << ",\"classes\":[";
}

template<class Output>
inline void AppendClass(Output& output, const ClassRow& row, bool hasPrevious)
{
    if (hasPrevious) output << ',';
    output << "{\"logicalAssembly\":" << row.logicalAssembly
        << ",\"typeKey\":" << row.typeKey
        << ",\"executionModeCode\":" << (row.interpreterShadow ? 1 : 0)
        << ",\"executionMode\":" << Quoted(row.interpreterShadow ? "InterpreterShadow" : "AotBaseline")
        << ",\"physicalImageKind\":" << Quoted(row.physicalInterpreter ? "Interpreter" : "Aot")
        << ",\"isActive\":" << row.isActive
        << ",\"cctorStarted\":" << row.cctorStarted
        << ",\"cctorFinished\":" << row.cctorFinished
        << ",\"hasInitializationException\":" << row.hasInitializationException
        << ",\"staticStoragePointer\":" << row.staticStoragePointer
        << ",\"pointerDetailsAvailable\":" << row.pointerDetailsAvailable
        << ",\"staticStorageAvailable\":" << row.staticStorageAvailable << '}';
}

template<class Output>
inline void EndClasses(Output& output) { output << ']'; }

}}} // il2cpp::vm::assembly_shadow_execution_diagnostic
