#include "AssemblyShadowDiagnostics.h"
#include <cstdio>
#include <sstream>

namespace il2cpp { namespace vm {

const char* AssemblyShadowDiagnostics::StateName(AssemblyShadowState state)
{
    switch (state)
    {
        case AssemblyShadowState::Disabled: return "Disabled";
        case AssemblyShadowState::CandidatesRegistered: return "CandidatesRegistered";
        case AssemblyShadowState::Staging: return "Staging";
        case AssemblyShadowState::Staged: return "Staged";
        case AssemblyShadowState::Validated: return "Validated";
        case AssemblyShadowState::Committing: return "Committing";
        case AssemblyShadowState::Committed: return "Committed";
        case AssemblyShadowState::Aborted: return "Aborted";
        case AssemblyShadowState::Failed: return "Failed";
        case AssemblyShadowState::FailedAfterCommit: return "FailedAfterCommit";
    }
    return "Unknown";
}

const char* AssemblyShadowDiagnostics::UseKindName(BaselineUseKind kind)
{
    switch (kind)
    {
        case BaselineUseKind::AssemblyReflection: return "AssemblyReflection";
        case BaselineUseKind::TypeReflection: return "TypeReflection";
        case BaselineUseKind::ClassInit: return "ClassInit";
        case BaselineUseKind::ObjectAllocation: return "ObjectAllocation";
        case BaselineUseKind::StaticField: return "StaticField";
        case BaselineUseKind::VTable: return "VTable";
        case BaselineUseKind::MonoScript: return "MonoScript";
        case BaselineUseKind::ModuleReflection: return "ModuleReflection";
        case BaselineUseKind::MethodExecution: return "MethodExecution";
    }
    return "Unknown";
}

std::string AssemblyShadowDiagnostics::Quote(const std::string& text)
{
    std::string result = "\"";
    for (unsigned char value : text)
    {
        if (value == '"' || value == '\\') { result += '\\'; result += static_cast<char>(value); }
        else if (value < 32)
        {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", value);
            result += escaped;
        }
        else result += static_cast<char>(value);
    }
    return result + '"';
}

static void StringArray(std::ostringstream& output, const std::vector<std::string>& values)
{
    output << '[';
    for (size_t index = 0; index < values.size(); ++index)
    {
        if (index) output << ',';
        output << AssemblyShadowDiagnostics::Quote(values[index]);
    }
    output << ']';
}

std::string AssemblyShadowDiagnostics::Serialize(const ShadowDiagnosticSnapshot& value)
{
    std::ostringstream output;
    output << std::boolalpha << "{\"schemaVersion\":1,\"enabled\":" << value.enabled
        << ",\"runtimeAbiVersion\":" << kAssemblyShadowRuntimeAbiVersion
        << ",\"metadataBudgetCapabilityVersion\":" << (value.enabled ? kAssemblyShadowMetadataBudgetCapabilityVersion : 0)
        << ",\"recoveryCapabilityVersion\":" << (value.enabled ? kAssemblyShadowRecoveryCapabilityVersion : 0)
        << ",\"startupCandidateSchemaVersion\":" << value.startupCandidateSchemaVersion
        << ",\"startupObservationMode\":" << Quote(value.startupObservationMode)
        << ",\"startupCandidateNames\":";
    StringArray(output, value.startupCandidateNames);
    output
        << ",\"state\":" << Quote(StateName(value.state))
        << ",\"stateCode\":" << static_cast<int>(value.state)
        << ",\"lastError\":" << static_cast<int>(value.lastError)
        << ",\"detail\":" << Quote(value.detail)
        << ",\"baselineBuildId\":" << Quote(value.baselineBuildId)
        << ",\"patchId\":" << Quote(value.patchId)
        << ",\"generation\":" << value.generation
        << ",\"expected\":" << value.expected << ",\"staged\":" << value.staged
        << ",\"retainedBytes\":" << value.retainedBytes << ",\"closureLoadOrder\":";
    StringArray(output, value.closureLoadOrder);
    output << ",\"stableAotNames\":";
    StringArray(output, value.stableAotNames);
    output << ",\"commitOrder\":";
    StringArray(output, value.commitOrder);
    output << ",\"assemblies\":[";
    for (size_t index = 0; index < value.assemblies.size(); ++index)
    {
        if (index) output << ',';
        const auto& assembly = value.assemblies[index];
        output << "{\"name\":" << Quote(assembly.name) << ",\"mvid\":" << Quote(assembly.mvid)
            << ",\"skeletonBuilt\":" << assembly.skeletonBuilt
            << ",\"runtimeMetadataInitialized\":" << assembly.runtimeMetadataInitialized
            << ",\"published\":" << assembly.published
            << ",\"moduleInitializerAttempted\":" << assembly.moduleInitializerAttempted
            << ",\"moduleInitializerRan\":" << assembly.moduleInitializerRan << '}';
    }
    output << "],\"events\":[";
    for (size_t index = 0; index < value.events.size(); ++index)
    {
        if (index) output << ',';
        const auto& event = value.events[index];
        output << "{\"sequence\":" << event.sequence << ",\"kind\":" << Quote(event.kind)
            << ",\"name\":" << Quote(event.name) << ",\"generation\":" << event.generation
            << ",\"stagedCount\":" << event.stagedCount << '}';
    }
    output << "],\"baselineUses\":[";
    for (size_t index = 0; index < value.baselineUses.size(); ++index)
    {
        if (index) output << ',';
        const auto& use = value.baselineUses[index];
        output << "{\"name\":" << Quote(use.name) << ",\"kind\":" << Quote(UseKindName(use.kind))
            << ",\"detail\":" << Quote(use.detail) << ",\"type\":" << Quote(use.type)
            << ",\"thread\":" << use.thread << ",\"timestamp\":" << use.timestamp << '}';
    }
    output << "],\"enumerationGeneration\":" << value.enumerationGeneration << ",\"ordinaryAssemblies\":[";
    for (size_t index = 0; index < value.ordinaryAssemblies.size(); ++index)
    {
        if (index) output << ',';
        const auto& assembly = value.ordinaryAssemblies[index];
        output << "{\"name\":" << Quote(assembly.name) << ",\"isInterpreter\":" << assembly.isInterpreter << '}';
    }
    output << "],\"classEnumerationGeneration\":" << value.classEnumerationGeneration << ",\"ordinaryClasses\":[";
    for (size_t index = 0; index < value.ordinaryClasses.size(); ++index)
    {
        if (index) output << ',';
        const auto& klass = value.ordinaryClasses[index];
        output << "{\"assemblyName\":" << Quote(klass.assemblyName) << ",\"typeName\":" << Quote(klass.typeName)
            << ",\"isInterpreter\":" << klass.isInterpreter << ",\"isConstructedGeneric\":" << klass.isConstructedGeneric
            << ",\"usesStagedMetadata\":" << klass.usesStagedMetadata << '}';
    }
    output << "]}";
    return output.str();
}

}}
