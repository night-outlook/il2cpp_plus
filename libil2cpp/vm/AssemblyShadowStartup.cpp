#include "AssemblyShadowStartup.h"

#if HYBRIDCLR_ENABLE_ASSEMBLY_SHADOW
#include "AssemblyShadowStartupGate.h"
#include "AssemblyShadow.h"
#include "Class.h"
#include "Image.h"
#include "MetadataCache.h"
#include "Object.h"
#include "Runtime.h"
#include "Thread.h"
#include "il2cpp-class-internals.h"
#include "il2cpp-object-internals.h"
#include "il2cpp-tabledefs.h"
#include "hybridclr/metadata/MetadataUtil.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hybridclr {
    extern const uint32_t g_assemblyShadowStartupBootstrapSchemaVersion;
    extern const char* g_assemblyShadowStartupBootstrapAssembly;
    extern const char* g_assemblyShadowStartupBootstrapNamespace;
    extern const char* g_assemblyShadowStartupBootstrapType;
    extern const char* g_assemblyShadowStartupBootstrapMethod;
}

namespace il2cpp { namespace vm {
namespace {
    using namespace assembly_shadow_startup;
    Gate s_gate;
    std::atomic<bool> s_failureLogged { false };
    std::atomic<bool> s_unexpectedSealed { false };

    // Generated identities are bounded simple metadata names, not paths,
    // assembly-qualified names, nested types or generic specifications.
    bool ValidName(const char* value)
    {
        if (!value) return false;
        for (size_t index = 0; index <= 512; ++index)
        {
            unsigned char c = static_cast<unsigned char>(value[index]);
            if (!c) return true;
            if (c <= 0x20 || c == 0x7f || c == '/' || c == '\\' || c == '+' ||
                c == '`' || c == ',' || c == '[' || c == ']') return false;
        }
        return false;
    }

    const char* Reason(Failure failure)
    {
        switch (failure)
        {
            case Failure::Configuration: return "Invalid generated bootstrap configuration";
            case Failure::CoreInitialization: return "Core runtime initialization failed";
            case Failure::CoreNotReady: return "Core runtime initialization is incomplete";
            case Failure::WrongThread: return "Bootstrap dispatch requires the attached main thread";
            case Failure::PendingEntry: return "Concurrent or recursive initialization during bootstrap";
            case Failure::Assembly: return "Bootstrap assembly is not an exact physical noncandidate AOT assembly";
            case Failure::Type: return "Bootstrap type is not an exact nonnested nongeneric definition";
            case Failure::Method: return "Bootstrap requires one callable static nongeneric parameterless Int32 method";
            case Failure::NullResult: return "Bootstrap returned no boxed result";
            case Failure::WrongResultType: return "Bootstrap returned a non-Int32 result";
            case Failure::CallbackRefused: return "Bootstrap explicitly refused startup";
            case Failure::CallbackThrew: return "Bootstrap raised an unexpected exception";
            default: return "Bootstrap gateway failed";
        }
    }

    NORETURN void TerminateBeforeHostContinuation() noexcept
    {
        // The host may ignore a false runtime-init result. Do not permit any
        // host continuation, managed shutdown, or process-exit callbacks after
        // refusing this process's startup contract.
        std::fprintf(stderr, "[AssemblyShadowStartup] Terminating process before host continuation (exit=1)\n");
        std::fflush(stderr);
        std::_Exit(1);
    }

    NORETURN bool FinishFailure() noexcept
    {
        const Failure reason = s_gate.Reason();
        // Explicit refusal preserves the callback's transaction/recovery error.
        // Reentry or an unwind has no trustworthy completion contract and must
        // seal even an already-published transaction as FailedAfterCommit.
        if ((reason == Failure::PendingEntry || reason == Failure::CallbackThrew) &&
            !s_unexpectedSealed.exchange(true, std::memory_order_acq_rel))
            AssemblyShadow::ReportUnexpectedFailure();
        if (!s_failureLogged.exchange(true, std::memory_order_acq_rel))
            std::fprintf(stderr, "[AssemblyShadowStartup] Failed: %s\n", Reason(reason));
        TerminateBeforeHostContinuation();
    }

    bool InvokeBootstrap()
    {
        const char* assemblyName = hybridclr::g_assemblyShadowStartupBootstrapAssembly;
        const char* namespaze = hybridclr::g_assemblyShadowStartupBootstrapNamespace;
        const char* typeName = hybridclr::g_assemblyShadowStartupBootstrapType;
        const char* methodName = hybridclr::g_assemblyShadowStartupBootstrapMethod;
        const Il2CppAssembly* assembly = MetadataCache::GetAotAssemblyByNamePhysical(assemblyName);
        if (!assembly || !assembly->aname.name || std::strcmp(assembly->aname.name, assemblyName) ||
            !assembly->image || assembly->image->assembly != assembly || assembly->image->dynamic ||
            hybridclr::metadata::IsInterpreterImage(assembly->image) || AssemblyShadow::IsCandidate(assembly))
            return s_gate.Fail(Failure::Assembly);
        Il2CppClass* klass = Image::ClassFromNameDefinedInImage(assembly->image, namespaze, typeName);
        if (!klass || klass->image != assembly->image || klass->declaringType || klass->is_generic ||
            klass->generic_class || !klass->name || !klass->namespaze ||
            std::strcmp(klass->name, typeName) || std::strcmp(klass->namespaze, namespaze))
            return s_gate.Fail(Failure::Type);
        const MethodInfo* selected = nullptr;
        void* iterator = nullptr;
        while (const MethodInfo* method = Class::GetMethods(klass, &iterator))
        {
            if (!method->name || std::strcmp(method->name, methodName) || method->parameters_count) continue;
            if (selected) return s_gate.Fail(Failure::Method);
            selected = method;
        }
        if (!selected || selected->klass != klass || !(selected->flags & METHOD_ATTRIBUTE_STATIC) ||
            (selected->flags & METHOD_ATTRIBUTE_ABSTRACT) || selected->is_generic || selected->is_inflated ||
            !selected->methodPointer || !selected->invoker_method || !selected->return_type ||
            selected->return_type->type != IL2CPP_TYPE_I4 || selected->return_type->byref)
            return s_gate.Fail(Failure::Method);
        Il2CppException* exception = nullptr;
        Il2CppObject* result = Runtime::Invoke(selected, nullptr, nullptr, &exception);
        if (exception) return s_gate.Fail(Failure::CallbackThrew);
        if (!result) return s_gate.Fail(Failure::NullResult);
        if (!il2cpp_defaults.int32_class || result->klass != il2cpp_defaults.int32_class)
            return s_gate.Fail(Failure::WrongResultType);
        int32_t code;
        std::memcpy(&code, Object::Unbox(result), sizeof(code));
        return code == 0;
    }
}

void AssemblyShadowStartup::MarkCoreReady() noexcept { s_gate.MarkCoreReady(); }

bool AssemblyShadowStartup::AfterRuntimeInit(bool initialized) noexcept
{
    const char* assembly = hybridclr::g_assemblyShadowStartupBootstrapAssembly;
    const char* namespaze = hybridclr::g_assemblyShadowStartupBootstrapNamespace;
    const char* type = hybridclr::g_assemblyShadowStartupBootstrapType;
    const char* method = hybridclr::g_assemblyShadowStartupBootstrapMethod;
    if (hybridclr::g_assemblyShadowStartupBootstrapSchemaVersion != 1 ||
        !ValidName(assembly) || !ValidName(namespaze) || !ValidName(type) || !ValidName(method))
    {
        s_gate.Fail(Failure::Configuration);
        return FinishFailure();
    }
    // An empty namespace is valid. Only the complete empty tuple disables this
    // optional gateway; it preserves legacy initialization, including reentry.
    if (!*assembly && !*namespaze && !*type && !*method) return initialized;
    if (!*assembly || !*type || !*method)
    {
        s_gate.Fail(Failure::Configuration);
        return FinishFailure();
    }
    if (!initialized) s_gate.Fail(Failure::CoreInitialization);
    else if (!s_gate.CoreReady()) s_gate.Fail(Failure::CoreNotReady);
    else
    {
        Il2CppThread* current = Thread::Current();
        if (s_gate.Run(true, current && current == Thread::Main(), InvokeBootstrap)) return true;
    }
    return FinishFailure();
}

}}
#endif
