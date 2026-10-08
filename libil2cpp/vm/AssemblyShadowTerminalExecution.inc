// Internal implementation fragment included after the transaction/registry definitions.
// Kept separate so host contracts execute exactly these production predicates.
    // A valid physical method is not permission to execute a failed transaction.
    // The terminal path is read-only, bounded and metadata-only. In particular,
    // it must not poison unrelated fixed diagnostics while constructing the
    // managed exception which reports an already-latched business failure.
    bool TerminalExecutionObserved() noexcept
    {
        const AssemblyShadowState state = s_state.load(std::memory_order_acquire);
        return state == AssemblyShadowState::Failed || state == AssemblyShadowState::FailedAfterCommit ||
            s_typeFailure.load(std::memory_order_acquire) || s_unexpectedFailure.load(std::memory_order_acquire) ||
            s_lateBaselineUse.load(std::memory_order_acquire) || s_referenceViolation.load(std::memory_order_acquire);
    }

    struct TerminalMethodInspection
    {
        const CandidateRegistry* candidates;
        const ActiveSnapshot* active;
        size_t remaining = 1024;

        bool Owner(const Il2CppAssembly* owner) const noexcept
        {
            return !owner || (candidates && candidates->byAssembly.count(owner)) ||
                (active && (active->byAssembly.count(owner) || active->shadowToBaseline.count(owner)));
        }

        bool Instance(const Il2CppGenericInst* instance, size_t depth) noexcept
        {
            if (!instance || !instance->type_argc || !instance->type_argv ||
                instance->type_argc > remaining) return true;
            for (uint32_t i = 0; i < instance->type_argc; ++i)
                if (Type(instance->type_argv[i], depth)) return true;
            return false;
        }

        bool Type(const Il2CppType* type, size_t depth) noexcept
        {
            if (!type || depth > 64 || !remaining) return true;
            --remaining;
            switch (type->type)
            {
                case IL2CPP_TYPE_CLASS:
                case IL2CPP_TYPE_VALUETYPE:
                    // generation zero deliberately includes every registered
                    // private image, even one now published or retained failed.
                    return !type->data.typeHandle ||
                        (candidates && candidates->byTypeHandle.count(type->data.typeHandle)) ||
                        !AssemblyShadowVisibility::IsTypeVisible(type, 0);
                case IL2CPP_TYPE_GENERICINST:
                {
                    const Il2CppGenericClass* generic = type->data.generic_class;
                    return !generic || !generic->type || generic->context.method_inst ||
                        Type(generic->type, depth + 1) || Instance(generic->context.class_inst, depth + 1);
                }
                case IL2CPP_TYPE_SZARRAY:
                case IL2CPP_TYPE_PTR:
                case IL2CPP_TYPE_BYREF:
                    return Type(type->data.type, depth + 1);
                case IL2CPP_TYPE_ARRAY:
                    return !type->data.array || !type->data.array->rank ||
                        type->data.array->numsizes > type->data.array->rank ||
                        type->data.array->numlobounds > type->data.array->rank ||
                        (type->data.array->numsizes && !type->data.array->sizes) ||
                        (type->data.array->numlobounds && !type->data.array->lobounds) ||
                        Type(type->data.array->etype, depth + 1);
                case IL2CPP_TYPE_BOOLEAN: case IL2CPP_TYPE_CHAR:
                case IL2CPP_TYPE_I1: case IL2CPP_TYPE_U1: case IL2CPP_TYPE_I2: case IL2CPP_TYPE_U2:
                case IL2CPP_TYPE_I4: case IL2CPP_TYPE_U4: case IL2CPP_TYPE_I8: case IL2CPP_TYPE_U8:
                case IL2CPP_TYPE_R4: case IL2CPP_TYPE_R8: case IL2CPP_TYPE_I: case IL2CPP_TYPE_U:
                case IL2CPP_TYPE_OBJECT: case IL2CPP_TYPE_STRING:
                    return false;
                default:
                    return true; // Unknown/open captured identity is not diagnostic authority.
            }
        }

        bool Context(const Il2CppGenericContext& context) noexcept
        {
            return (context.class_inst && Instance(context.class_inst, 0)) ||
                (context.method_inst && Instance(context.method_inst, 0));
        }

        bool Class(const Il2CppClass* klass) noexcept
        {
            return !klass || !klass->image || Owner(klass->image->assembly) ||
                Type(&klass->byval_arg, 0) ||
                (klass->generic_class && (Type(klass->generic_class->type, 0) ||
                    !klass->generic_class->context.class_inst || Context(klass->generic_class->context)));
        }
    };

    bool RejectTerminalMethod(const MethodInfo* method) noexcept
    {
        if (!TerminalExecutionObserved()) return false;
        TerminalMethodInspection inspection;
        inspection.candidates = s_candidates.load(std::memory_order_acquire);
        inspection.active = s_active.load(std::memory_order_acquire);
        if (!method || inspection.Class(method->klass)) return true;
        if (!method->is_inflated) return false;
        const auto* generic = method->genericMethod;
        const MethodInfo* definition = generic ? generic->methodDefinition : nullptr;
        return !definition || definition == method || definition->is_inflated ||
            inspection.Class(definition->klass) || inspection.Context(generic->context);
    }

    void RaiseTerminalMethodFailure()
    {
        AssemblyShadowError error = AssemblyShadowError::InternalError;
        std::string detail;
        if (const TypeFailure* failure = s_typeFailure.load(std::memory_order_acquire))
        {
            error = failure->error;
            detail = failure->detail;
        }
        else if (StagingBridge::IsStaging() || AssemblyShadow::IsResolvingTypeMetadata())
        {
            // A transaction lock may already be owned by metadata preparation.
            // Preserve that transaction's original failure without recursively
            // locking it or constructing a managed exception inside this scope.
            throw ShadowTypeResolutionFailure(error,
                "Assembly Shadow terminal failure forbids execution in private metadata scope.");
        }
        else
        {
            // This is the throwing path, never the boolean callback guard.
            // Initializer/Stage errors live in the transaction, not TypeFailure.
            auto& transaction = Current();
            std::lock_guard<std::mutex> lock(transaction.mutex);
            error = transaction.recoveryTerminalError;
            detail = transaction.recoveryTerminalDetail;
        }
        if (error == AssemblyShadowError::Success) error = AssemblyShadowError::InternalError;
        if (detail.empty()) detail = "Assembly Shadow terminal failure; restart is required.";
        // Do not call FailTypeResolution: reporting existing poison must not
        // replace an initializer/Stage first error or mutate the terminal state.
        if (StagingBridge::IsStaging() || AssemblyShadow::IsResolvingTypeMetadata())
            throw ShadowTypeResolutionFailure(error, detail);
        Exception::Raise(Exception::GetInvalidOperationException(detail.c_str()));
        throw std::runtime_error(detail); // Fail closed if an exception adapter returns.
    }

