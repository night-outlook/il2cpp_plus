#pragma once

#include "il2cpp-config.h"
#include <stdint.h>

namespace il2cpp { namespace vm {

// The managed definitions are an ABI. Never renumber existing values.
enum class AssemblyShadowError : int32_t
{
    Success = 0,
    FeatureDisabled = 1,
    InvalidState = 2,
    InvalidArgument = 3,
    CandidateNotRegistered = 4,
    DuplicateAssemblyName = 5,
    BaselineAssemblyNotFound = 6,
    BaselineBuildMismatch = 7,
    AssemblyNameMismatch = 8,
    BadImage = 9,
    UnsupportedAssembly = 10,
    ClosureMemberMissing = 11,
    UnexpectedClosureMember = 12,
    ReferenceResolutionFailed = 13,
    ReferenceEscapesClosure = 14,
    BaselineAlreadyUsed = 15,
    ResourceAbiMismatch = 16,
    RuntimeAbiMismatch = 17,
    AlreadyCommitted = 18,
    ModuleInitializerFailed = 19,
    InternalError = 20,
    BaselineMethodExecution = 21,
    CapabilityUnavailable = 22,
    MetadataCapacityExceeded = 23,
    MetadataBudgetMismatch = 24,
};

enum class AssemblyShadowState : int32_t
{
    Disabled = 0,
    CandidatesRegistered = 1,
    Staging = 2,
    Staged = 3,
    Validated = 4,
    Committing = 5,
    Committed = 6,
    Aborted = 7,
    Failed = 8,
    FailedAfterCommit = 9,
};

enum class AssemblyExecutionMode : int32_t
{
    AotBaseline = 0,
    InterpreterShadow = 1,
};

enum class BaselineUseKind : int32_t
{
    AssemblyReflection = 0,
    TypeReflection = 1,
    ClassInit = 2,
    ObjectAllocation = 3,
    StaticField = 4,
    VTable = 5,
    MonoScript = 6,
    ModuleReflection = 7,
    MethodExecution = 8,
};

static const int32_t kAssemblyShadowRuntimeAbiVersion = 2;
// Exact wire contracts, shared by full diagnostics and live compact queries.
static const int32_t kAssemblyShadowMetadataBudgetCapabilityVersion = 2;
static const int32_t kAssemblyShadowRecoveryCapabilityVersion = 1;

}}
