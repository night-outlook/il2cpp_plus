// R03 B2 INACTIVE: restricted, actual VM physical TypeDef -> owned class
// rows -> original frozen execution/R02 record. Included ONCE by
// AssemblyShadow.cpp inside il2cpp::vm; NEVER called by a Player or V1 getter.
// This is deliberately fail-closed for nested/constructed/non-AOT/non-ASCII
// assembly/type-qualified cases. Neither successful source capture nor an
// unchanged R02/registry read fence proves a global VM epoch or full V2.
bool AssemblyShadow::TryCaptureBoundedDefinitionExecutionLive(
    assembly_shadow_physical_membership::Scratch& membership,
    ::r03_b2_owned_execution_rows::Rows& rows,
    assembly_shadow_frozen_execution_record::Record& record,
    assembly_shadow_bounded_definition_execution::Receipt& receipt)
{
    namespace live=assembly_shadow_bounded_definition_execution;
    namespace physical=assembly_shadow_physical_membership;
    namespace owned=::r03_b2_owned_execution_rows;
    namespace key=::r03_b2_owned_definition_key;
    namespace r02=assembly_shadow_r02;
    namespace frozen=assembly_shadow_frozen_execution_record;
    receipt=live::Receipt{};
    auto reject=[&](live::Error error)->bool {
        return live::Reject(receipt,error,membership,rows,record);
    };
    // Do not resurrect earlier failed/complete objects or emit partial bytes.
    if(!membership.Consume() || record.Complete() ||
       record.FirstError()!=frozen::RecordError::None)
        return reject(live::Error::ReusedSource);

    assembly_shadow_metadata_lifetime::Gate::Reader admission(
        assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate);
    if(!admission)return reject(live::Error::NoMetadataAdmission);
    receipt.admittedMetadataEpoch=admission.Epoch();

    static_assert(std::tuple_size<decltype(s_executionClasses)>::value==physical::kSlots,
                  "B2 physical registry slot table changed");
    const ActiveSnapshot* activeBefore=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState stateBefore=s_state.load(std::memory_order_acquire);
    // This restricted healthy-time prototype does not claim postterminal
    // metadata readability or first-error snapshot coherence.
    if(stateBefore==AssemblyShadowState::Failed ||
       stateBefore==AssemblyShadowState::FailedAfterCommit ||
       s_firstFailureV2.status()!=assembly_shadow_first_failure_v2::State::Empty)
        return reject(live::Error::UnsupportedTypeShape);
    receipt.activeGenerationBefore=activeBefore?activeBefore->generation:0;

    {
        ExecutionObservationLock lock;
        if(!membership.Capture(s_executionClasses.data(),s_executionClasses.size(),
             s_executionClassCount,s_executionRegistryRevision.Current(),
             s_executionRegistrationWitnesses))
            return reject(live::Error::InvalidRegistry);
    }
    if(!membership.Unique())return reject(live::Error::DuplicateRegistration);
    receipt.registered=membership.Observed();
    receipt.registryRevisionBefore=membership.Revision();
    if(!receipt.registered)return reject(live::Error::InvalidRegistry);
    if(!rows.Begin(receipt.registered))return reject(live::Error::RowsFailed);

    r02::Snapshot counters{};
    r02::SnapshotReceipt beforeR02{};
    if(!r02::Snapshot::TryCaptureCurrent(counters,beforeR02))
        return reject(live::Error::CounterUnavailable);
    if(!assembly_shadow_bounded_execution_empty_live::CompleteCoverage(counters))
        return reject(live::Error::CounterUnavailable);

    struct Context {
        physical::Scratch* membership;
        owned::Rows* rows;
        const ActiveSnapshot* active;
        live::Error error=live::Error::None;
        size_t lookups=0, rejected=0;
    } context{&membership,&rows,activeBefore};

    // Physically enumerate with the original metadata lock. Only a
    // FirstMatch may be dereferenced, and then only inside this callback.
    // il2cpp_class_for_each is an existing VM traversal and may allocate;
    // no global no-allocation claim is made for the source collection.
    {
        os::FastAutoLock metadataLock(&g_MetadataLock);
        il2cpp_class_for_each([](Il2CppClass* klass,void* raw) {
            Context& c=*static_cast<Context*>(raw);
            if(c.error!=live::Error::None)return;
            const auto event=c.membership->ObserveEnumeratedOwned(klass);
            if(event==physical::ObservationEvent::DuplicateMatch ||
               event==physical::ObservationEvent::Invalid) {
                c.error=live::Error::BadPhysicalEnumeration;return;
            }
            if(event!=physical::ObservationEvent::FirstMatch)return;

            size_t registration=physical::kSlots;
            assembly_shadow_execution_witness::Source witness=
                assembly_shadow_execution_witness::Source::None;
            if(!c.membership->TryObservedRegistrationBinding(klass,registration,witness)) {
                c.error=live::Error::InvalidRegistry;return;
            }
            if(!klass || !klass->image || !klass->image->assembly ||
               !klass->image->assembly->aname.name ||
               !klass->typeMetadataHandle || !klass->name) {
                c.error=live::Error::InvalidTypeIdentity;return;
            }
            const Il2CppImage* image=klass->image;
            const Il2CppAssembly* owner=image->assembly;
            const Il2CppType& type=klass->byval_arg;
            // This prototype supports original *physical AOT TypeDef*
            // definitions only. No constructed generic, arrays, nested
            // declaration, qualifiers, or interpreter-owned metadata.
            if(hybridclr::metadata::IsInterpreterIndex(image->token) ||
               image->typeCount==0 ||
               image->typeCount>live::kMaximumAotImageTypes) {
                ++c.rejected;c.error=live::Error::UnsupportedImage;return;
            }
            if(klass->rank || klass->generic_class || klass->declaringType ||
               (type.type!=IL2CPP_TYPE_CLASS &&
                type.type!=IL2CPP_TYPE_VALUETYPE) ||
               type.data.typeHandle!=klass->typeMetadataHandle ||
               type.attrs || type.num_mods || type.byref || type.pinned) {
                ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
            }
            // Explicit owner inventory: do not trust an unproven raw
            // Il2CppMetadataTypeHandle (even from a class header) as a
            // pointer to a type definition until its physical image proves
            // it is one of its own registered TypeDefs.
            bool belongs=false;
            for(uint32_t i=0;i<image->typeCount;++i) {
                if(c.lookups==live::kMaximumTotalMetadataLookups) {
                    c.error=live::Error::SourceOutsideBudget;return;
                }
                ++c.lookups;
                if(MetadataCache::GetAssemblyTypeHandle(image,i)==
                   klass->typeMetadataHandle) {
                    belongs=true;break;
                }
            }
            if(!belongs) {
                c.error=live::Error::InvalidTypeIdentity;return;
            }
            const auto* def=reinterpret_cast<const Il2CppTypeDefinition*>(
                klass->typeMetadataHandle);
            if(def->declaringTypeIndex!=kTypeIndexInvalid) {
                ++c.rejected;c.error=live::Error::UnsupportedMetadataAncestry;
                return;
            }
            // Original MetadataDefinitionKey obtains this exact pair and
            // definition generic arity. The callback retains neither string
            // nor metadata pointer after it returns.
            const auto name=GlobalMetadata::GetTypeNamespaceAndName(
                klass->typeMetadataHandle);
            if(!name.second) {
                c.error=live::Error::InvalidTypeIdentity;return;
            }
            const char* ns=name.first?name.first:"";
            // Bound every borrowed null-terminated metadata string, including
            // the original assembly name, before forwarding its byte view.
            auto count=[](const char* p,size_t& size)->bool {
                if(!p)return false;
                size=0;
                while(size<=key::Capture::kCapacity && p[size])++size;
                return size<=key::Capture::kCapacity;
            };
            size_t assemblyBytes=0,nsBytes=0,nameBytes=0;
            if(!count(owner->aname.name,assemblyBytes) ||
               !count(ns,nsBytes) || !count(name.second,nameBytes)) {
                c.error=live::Error::TypeKeyFailed;return;
            }
            const auto container=GlobalMetadata::GetGenericContainerFromIndex(
                def->genericContainerIndex);
            const uint32_t arity=GlobalMetadata::GetGenericContainerCount(container);
            key::Capture typeKey;
            const bool keyOk=typeKey.Begin(
                    {owner->aname.name,assemblyBytes},true,false,false) &&
                typeKey.AddDeclaration({ns,nsBytes},{name.second,nameBytes},arity) &&
                typeKey.Seal();
            if(!keyOk) {
                ++c.rejected;c.error=live::Error::TypeKeyFailed;return;
            }

            owned::Flags flags{};
            const bool baseline=c.active && c.active->byAssembly.count(owner);
            const bool shadow=c.active && c.active->shadowToBaseline.count(owner);
            flags.interpreterShadow=baseline||shadow;
            flags.physicalInterpreter=Interpreter(owner);
            flags.isActive=!baseline;
            flags.cctorStarted=os::Atomic::LoadRelaxed(
                reinterpret_cast<const int32_t*>(&klass->cctor_started))!=0;
            flags.cctorFinished=os::Atomic::LoadRelaxed(
                reinterpret_cast<const int32_t*>(&klass->cctor_finished_or_no_cctor))!=0;
            flags.hasInitializationException=os::Atomic::LoadRelaxed(
                reinterpret_cast<const int32_t*>(&klass->initializationExceptionGCHandle))!=0;
            const void* storage=klass->static_fields;
            flags.staticStorageAvailable=storage!=nullptr;
            char pointer[2+sizeof(uintptr_t)*2+1]{};
            size_t pointerBytes=0;
#if IL2CPP_DEBUG
            flags.pointerDetailsAvailable=true;
            if(storage) {
                const int n=std::snprintf(pointer,sizeof pointer,"0x%llx",
                    static_cast<unsigned long long>(
                        reinterpret_cast<uintptr_t>(storage)));
                if(n<0 || static_cast<size_t>(n)>=sizeof pointer) {
                    c.error=live::Error::RowsFailed;return;
                }
                pointerBytes=static_cast<size_t>(n);
            }
#endif
            const auto role=witness==assembly_shadow_execution_witness::Source::Candidate
                ? owned::Source::Candidate : owned::Source::ActiveShadow;
            if(!c.rows->Add(registration,role,
                 {owner->aname.name,assemblyBytes},typeKey,
                 {pointer,pointerBytes},flags)) {
                c.error=live::Error::RowsFailed;return;
            }
        },&context);
    }

    receipt.metadataLookups=context.lookups;
    receipt.rejectedUnsupported=context.rejected;
    receipt.physicallyMatched=membership.Matched();
    if(context.error!=live::Error::None)return reject(context.error);
    if(!membership.EnumeratorHealthy() || !membership.FullyMatched() ||
       receipt.physicallyMatched!=receipt.registered)
        return reject(live::Error::UnmatchedClass);
    if(!rows.Seal() || rows.Count()!=receipt.registered)
        return reject(live::Error::RowsFailed);

    // This is a bounded observational fence, not an all-VM linearization.
    bool sameRegistry=false;
    {
        ExecutionObservationLock lock;
        sameRegistry=membership.SameRegistry(
            s_executionClasses.data(),s_executionClasses.size(),
            s_executionClassCount,s_executionRegistryRevision.Current(),
            s_executionRegistrationWitnesses);
        receipt.registryRevisionAfter=s_executionRegistryRevision.Current();
    }
    if(!sameRegistry ||
       activeBefore!=s_active.load(std::memory_order_acquire) ||
       stateBefore!=s_state.load(std::memory_order_acquire))
        return reject(live::Error::SourceChanged);

    frozen::Identity identity{};
    identity.stateCode=static_cast<int32_t>(stateBefore);
    identity.state=assembly_shadow_diagnostic::Quoted(
        AssemblyShadowDiagnostics::StateName(stateBefore));
    identity.generation=receipt.activeGenerationBefore;
    if(!record.Begin(identity,counters,rows.Count()))
        return reject(live::Error::FrozenRecordFailed);
    // Input rows and typekeys are now owned; no VM pointer survives.
    for(size_t i=0;i<rows.Count();++i) {
        owned::RowView source{};
        if(!rows.Get(i,source))
            return reject(live::Error::RowsFailed);
        assembly_shadow_execution_diagnostic::ClassRow row{};
        row.logicalAssembly.data=source.logicalAssembly.data;
        row.logicalAssembly.bytes=source.logicalAssembly.size;
        row.typeKey.data=source.typeKey.data;
        row.typeKey.bytes=source.typeKey.size;
        row.interpreterShadow=source.fields.interpreterShadow;
        row.physicalInterpreter=source.fields.physicalInterpreter;
        row.isActive=source.fields.isActive;
        row.cctorStarted=source.fields.cctorStarted;
        row.cctorFinished=source.fields.cctorFinished;
        row.hasInitializationException=source.fields.hasInitializationException;
        row.staticStoragePointer.data=source.staticStoragePointer.data;
        row.staticStoragePointer.bytes=source.staticStoragePointer.size;
        row.pointerDetailsAvailable=source.fields.pointerDetailsAvailable;
        row.staticStorageAvailable=source.fields.staticStorageAvailable;
        if(!record.AddClass(row))
            return reject(live::Error::FrozenRecordFailed);
    }
    if(!record.Freeze())
        return reject(live::Error::FrozenRecordFailed);

    r02::Snapshot finished{};
    r02::SnapshotReceipt afterR02{};
    if(!r02::Snapshot::TryCaptureCurrent(finished,afterR02))
        return reject(live::Error::CounterUnavailable);
    if(!assembly_shadow_bounded_execution_empty_live::Equivalent(counters,finished))
        return reject(live::Error::CounterChanged);
    {
        ExecutionObservationLock lock;
        sameRegistry=membership.SameRegistry(
            s_executionClasses.data(),s_executionClasses.size(),
            s_executionClassCount,s_executionRegistryRevision.Current(),
            s_executionRegistrationWitnesses);
        receipt.registryRevisionAfter=s_executionRegistryRevision.Current();
    }
    const ActiveSnapshot* activeAfter=s_active.load(std::memory_order_acquire);
    const AssemblyShadowState stateAfter=s_state.load(std::memory_order_acquire);
    receipt.activeGenerationAfter=activeAfter?activeAfter->generation:0;
    if(!sameRegistry || activeAfter!=activeBefore ||
       stateAfter!=stateBefore || !record.Complete())
        return reject(live::Error::SourceChanged);

    membership.Scrub();
    receipt.serializedRows=rows.Count();
    receipt.ownedBytes=rows.TextBytes();
    receipt.callbackPhysicalMembershipProven=true;
    receipt.restrictedTypeDefSubsetProven=true;
    receipt.sourceFenceStable=true;
    // Deliberately NOT a complete arbitrary TypeKey/field15/VM-epoch claim.
    return true;
}
