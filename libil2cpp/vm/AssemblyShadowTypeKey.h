#pragma once
#include "il2cpp-config.h"
#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadowTypes.h"
#include "il2cpp-class-internals.h"
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

namespace il2cpp { namespace vm {

struct ShadowTypeDeclaration
{
    std::string namespaze;
    std::string name;
    uint32_t genericArity;
};

struct ShadowTypeKey
{
    std::string assemblyName;
    std::vector<ShadowTypeDeclaration> declarations; // Outermost to innermost.
    std::string ToString() const;
    bool operator==(const ShadowTypeKey& other) const;
};

class ShadowTypeResolutionFailure : public std::runtime_error
{
public:
    ShadowTypeResolutionFailure(AssemblyShadowError code, const std::string& detail) : std::runtime_error(detail), error(code) {}
    AssemblyShadowError error;
};

// RAII guard owned by the core. It only prevents recursive semantic redirection
// while reading physical metadata. Business-use guards never ignore this flag.
class AssemblyShadowTypeMetadataScope
{
public:
    AssemblyShadowTypeMetadataScope();
    ~AssemblyShadowTypeMetadataScope();
    AssemblyShadowTypeMetadataScope(const AssemblyShadowTypeMetadataScope&) = delete;
    AssemblyShadowTypeMetadataScope& operator=(const AssemblyShadowTypeMetadataScope&) = delete;
};

class AssemblyShadowTypeKey
{
public:
    typedef std::unordered_map<Il2CppMetadataTypeHandle, const Il2CppImage*> MetadataTypeImages;
    static ShadowTypeKey Make(const Il2CppClass* definition);
    static uint32_t GenericArity(const Il2CppClass* definition);
    static Il2CppClass* RawDefinition(const Il2CppType* type);
    static std::string Format(const Il2CppType* type);
    // Diagnostic-only raw metadata path: never creates/initializes a class.
    // The caller supplies a physical image inventory, not a logical name map.
    static std::string FormatMetadataOnly(const Il2CppType* type, const MetadataTypeImages& images);
};

}}
#endif
