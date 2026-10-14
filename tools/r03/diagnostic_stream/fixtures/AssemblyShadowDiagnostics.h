#pragma once

#include "AssemblyShadowTypes.h"
#include <string>
#include <vector>

namespace il2cpp { namespace vm {

struct ShadowAssemblyDiagnostic
{
    std::string name;
    std::string mvid;
    bool skeletonBuilt = false;
    bool runtimeMetadataInitialized = false;
    bool published = false;
    bool moduleInitializerAttempted = false;
    bool moduleInitializerRan = false;
};

struct ShadowEventDiagnostic
{
    uint64_t sequence = 0;
    std::string kind;
    std::string name;
    uint64_t generation = 0;
    size_t stagedCount = 0;
};

struct ShadowUseDiagnostic
{
    std::string name;
    BaselineUseKind kind = BaselineUseKind::AssemblyReflection;
    std::string detail;
    std::string type;
    uint64_t thread = 0;
    uint64_t timestamp = 0;
};

struct ShadowDiagnosticSnapshot
{
    bool enabled = false;
    int32_t startupCandidateSchemaVersion = 0;
    std::vector<std::string> startupCandidateNames;
    std::string startupObservationMode = "Unavailable";
    AssemblyShadowState state = AssemblyShadowState::Disabled;
    AssemblyShadowError lastError = AssemblyShadowError::Success;
    std::string detail;
    std::string baselineBuildId;
    std::string patchId;
    uint64_t generation = 0;
    size_t expected = 0;
    size_t staged = 0;
    uint64_t retainedBytes = 0;
    uint64_t enumerationGeneration = 0;
    struct OrdinaryAssembly { std::string name; bool isInterpreter = false; };
    std::vector<OrdinaryAssembly> ordinaryAssemblies;
    uint64_t classEnumerationGeneration = 0;
    struct OrdinaryClass
    {
        std::string assemblyName;
        std::string typeName;
        bool isInterpreter = false;
        bool isConstructedGeneric = false;
        bool usesStagedMetadata = false;
    };
    std::vector<OrdinaryClass> ordinaryClasses;
    std::vector<std::string> closureLoadOrder;
    std::vector<std::string> stableAotNames;
    std::vector<std::string> commitOrder;
    std::vector<ShadowAssemblyDiagnostic> assemblies;
    std::vector<ShadowEventDiagnostic> events;
    std::vector<ShadowUseDiagnostic> baselineUses;
};

class AssemblyShadowDiagnostics
{
public:
    static const char* StateName(AssemblyShadowState state);
    static const char* UseKindName(BaselineUseKind kind);
    static std::string Quote(const std::string& text);
    // The caller must release VM and transaction locks before serialization.
    static std::string Serialize(const ShadowDiagnosticSnapshot& snapshot);
};

}}
