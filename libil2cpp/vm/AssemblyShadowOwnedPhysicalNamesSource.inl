// P1 INACTIVE. Included once by AssemblyShadow.cpp within the VM namespace.
// No original diagnostic getter, managed method or Player code invokes this.
bool AssemblyShadow::TryCaptureOwnedObservedPhysicalNames(
    assembly_shadow_physical_membership::Scratch& membershipScratch,
    assembly_shadow_owned_physical_names::Capture& names,
    assembly_shadow_owned_physical_names::Receipt& receipt)
{
    namespace physical=assembly_shadow_physical_membership;
    namespace owned=assembly_shadow_owned_physical_names;
    namespace shape=assembly_shadow_observed_type_shape;
    // Source-owned typed constants must agree with the actual installed ABI.
    static_assert(static_cast<uint8_t>(IL2CPP_TYPE_CLASS)==shape::kClassTypeCode &&
                  static_cast<uint8_t>(IL2CPP_TYPE_VALUETYPE)==shape::kValueTypeCode,
                  "Physical shape type codes differ from the pinned IL2CPP ABI");
    receipt=owned::Receipt{};
    auto reject=[&](owned::Error why)->bool {
        if(receipt.error==owned::Error::None)receipt.error=why;
        receipt.namesCopiedFromMatchedEnumeratorCallbacks=false;
        receipt.allRowsBoundToObservedRegistry=false;
        receipt.allObservedTypeShapeRowsFromMatchedCallbacks=false;
        names.Revoke();
        membershipScratch.Scrub();
        return false;
    };
    if(!membershipScratch.Consume() || !names.Begin())
        return reject(owned::Error::ReusedCapture);
    assembly_shadow_metadata_lifetime::Gate::Reader admission(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!admission)return reject(owned::Error::AdmissionUnavailable);
    static_assert(std::tuple_size<decltype(s_executionClasses)>::value==physical::kSlots,
                  "Observed registry and owned name source slot limits changed");
    const ActiveSnapshot* activeBefore=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState stateBefore=s_state.load(std::memory_order_acquire);
    {
        ExecutionObservationLock lock;
        if(!membershipScratch.Capture(s_executionClasses.data(),s_executionClasses.size(),
            s_executionClassCount,s_executionRegistryRevision.Current(),
            s_executionRegistrationWitnesses))
            return reject(owned::Error::RegistryInvalid);
    }
    if(!membershipScratch.Unique())return reject(owned::Error::RegistryInvalid);
    receipt.observedClasses=membershipScratch.Observed();
    receipt.registryRevisionBefore=membershipScratch.Revision();
    if(receipt.observedClasses)
    {
        struct Context {physical::Scratch* membership;owned::Capture* names;};
        Context context{&membershipScratch,&names};
        // Snapshot is prepared BEFORE taking g_MetadataLock. The callback
        // borrows physical fields ONLY for an exact observed iterator match.
        // It writes caller-owned fixed text and retains no Il2CppClass pointer.
        os::FastAutoLock metadataLock(&g_MetadataLock);
        il2cpp_class_for_each([](Il2CppClass* klass,void* raw) {
            Context& context=*static_cast<Context*>(raw);
            const auto event=context.membership->ObserveEnumeratedOwned(klass);
            if(event!=physical::ObservationEvent::FirstMatch ||
               !context.names->CollectingHealthy())return;
            if(!klass->image || !klass->image->assembly ||
               !klass->image->assembly->aname.name || !klass->name)
            {
                context.names->RejectInvalidSource();
                return;
            }
            // Bind copied bytes to the short-lived observed registry slot and
            // original registration source, never to a retained class pointer.
            size_t registeredSlot=physical::kSlots;
            auto registeredSource=assembly_shadow_execution_witness::Source::None;
            if(!context.membership->TryObservedRegistrationBinding(
                   klass,registeredSlot,registeredSource))
            {
                context.names->RejectInvalidBinding();
                return;
            }
            // All these Il2CppClass fields are in its always-valid header.
            // Read them ONLY at the matched callback while metadata lock
            // and initial-session lifetime admission are both held. Copy
            // primitive bits; never retain metadata/generic/class pointers.
            shape::RawEvidence rawShape{};
            rawShape.byvalTypeCode=static_cast<uint8_t>(klass->byval_arg.type);
            rawShape.arrayRank=klass->rank;
            rawShape.hasTypeMetadataHandle=klass->typeMetadataHandle!=nullptr;
            rawShape.hasDeclaringType=klass->declaringType!=nullptr;
            rawShape.hasConstructedGenericClass=klass->generic_class!=nullptr;
            rawShape.isGenericDefinition=klass->is_generic!=0;
            rawShape.observedFromMatchedPhysicalCallback=true;
            context.names->Add(registeredSlot,registeredSource,
                               klass->image->assembly->aname.name,
                               klass->namespaze,klass->name,rawShape);
        },&context);
    }
    receipt.matchedClasses=membershipScratch.Matched();
    if(!membershipScratch.EnumeratorHealthy())
        return reject(owned::Error::EnumeratorIncomplete);
    if(!names.CollectingHealthy())return reject(names.FirstError());
    bool sameRegistry=false;
    {
        ExecutionObservationLock lock;
        sameRegistry=membershipScratch.SameRegistry(
            s_executionClasses.data(),s_executionClasses.size(),
            s_executionClassCount,s_executionRegistryRevision.Current(),
            s_executionRegistrationWitnesses);
        receipt.registryRevisionAfter=s_executionRegistryRevision.Current();
    }
    const ActiveSnapshot* activeAfter=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState stateAfter=s_state.load(std::memory_order_acquire);
    if(!sameRegistry || activeBefore!=activeAfter || stateBefore!=stateAfter)
        return reject(owned::Error::SourceChanged);
    if(!membershipScratch.FullyMatched() ||
       !names.Seal(membershipScratch.Observed()))
        return reject(owned::Error::CountMismatch);
    if(!names.AllObservedShapesFromMatchedCallbacks())
        return reject(owned::Error::NameCaptureFailed);
    membershipScratch.Scrub();
    receipt.allObservedTypeShapeRowsFromMatchedCallbacks=true;
    receipt.ownedRows=names.Count();
    receipt.ownedTextBytes=names.TextBytes();
    receipt.boundRegistrationRows=names.Count();
    receipt.allRowsBoundToObservedRegistry=true;
    // This is an owned *physical display name* subset, not an execution TypeKey.
    receipt.namesCopiedFromMatchedEnumeratorCallbacks=true;
    return true;
}
