// R03 B2 INACTIVE: restricted, actual VM physical TypeDef -> owned class
// rows -> original frozen execution/R02 record. Included ONCE by
// AssemblyShadow.cpp inside il2cpp::vm; NEVER called by a Player or V1 getter.
// Nested physical metadata definitions require an owner-checked ancestry.
// Limited GENERICINST AOT argument owners now come from the raw physical
// assembly table (registered AOT images), even when no observed class exists
// in that argument's image. Both physical registry visits and the two class
// enumerations run under the initial-session metadata-lock scope. Unregistered,
// recursive/open/qualified/array and non-ASCII assembly forms reject.
// Neither two enumerations nor unchanged R02/registry fences prove an
// atomic VM epoch or complete V2 execution diagnostics.
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
    namespace constructed_key=::r03_b2_owned_constructed_type_key;
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

    // Two source-pinned independent membership scans: the first discovers
    // physical images only from matched observed class headers. The second
    // copies class rows after that complete owner inventory is available.
    physical::Scratch discovery;
    if(!discovery.Consume())return reject(live::Error::InvalidRegistry);
    struct DiscoveryCleanup {
        physical::Scratch& scratch;
        ~DiscoveryCleanup() noexcept{scratch.Scrub();}
    } discoveryCleanup{discovery};
    bool capturedRegistry=false, capturedDiscovery=false;
    {
        ExecutionObservationLock lock;
        capturedRegistry=membership.Capture(s_executionClasses.data(),s_executionClasses.size(),
             s_executionClassCount,s_executionRegistryRevision.Current(),
             s_executionRegistrationWitnesses);
        capturedDiscovery=discovery.Capture(s_executionClasses.data(),s_executionClasses.size(),
             s_executionClassCount,s_executionRegistryRevision.Current(),
             s_executionRegistrationWitnesses);
    }
    if(!capturedRegistry||!capturedDiscovery)
        return reject(live::Error::InvalidRegistry);
    if(!membership.Unique()||!discovery.Unique())
        return reject(live::Error::DuplicateRegistration);
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
        assembly_shadow_observed_images::Inventory<live::kMaximumRegisteredAotImages>* images=nullptr;
        assembly_shadow_observed_images::Inventory<64>* observedImages=nullptr;
        bool usedCrossImageArgument=false;
        bool usedUnobservedAotArgumentImage=false;
    };
    Context context{};
    context.membership=&membership;
    context.rows=&rows;
    context.active=activeBefore;

    // Physically enumerate with the original metadata lock. Only a
    // FirstMatch may be dereferenced, and then only inside this callback.
    // il2cpp_class_for_each is an existing VM traversal and may allocate;
    // no global no-allocation claim is made for the source collection.
    {
        os::FastAutoLock metadataLock(&g_MetadataLock);
        assembly_shadow_observed_images::Inventory<64> imageInventory;
        assembly_shadow_observed_images::Inventory<
            live::kMaximumRegisteredAotImages> registeredAotImages;
        // This is the *raw physical* assembly table, NOT the logical
        // Assembly::GetAllAssemblies visibility-filtered snapshot. The
        // original s_assemblyLock is held during each synchronous visit.
        struct AotRegistrySink {
            assembly_shadow_observed_images::Inventory<
                live::kMaximumRegisteredAotImages>* inventory;
            live::Error error=live::Error::None;
        } registrySink{};
        registrySink.inventory=&registeredAotImages;
        auto readAotRegistry=[](const Il2CppImage* img,void* raw)->bool {
            AotRegistrySink& sink=*static_cast<AotRegistrySink*>(raw);
            if(!img || !img->typeCount ||
               img->typeCount>live::kMaximumAotImageTypes) {
                sink.error=live::Error::UnsupportedImage;
                return false;
            }
            if(!sink.inventory->AddFromRegisteredAssembly(img)) {
                sink.error=live::Error::PhysicalImageLimit;
                return false;
            }
            return true;
        };
        if(!Assembly::TryVisitRegisteredAotImages(readAotRegistry,&registrySink))
            return reject(registrySink.error==live::Error::None?
                live::Error::UnadmittedMetadataOwner:registrySink.error);
        if(!registeredAotImages.Healthy() || !registeredAotImages.Count())
            return reject(live::Error::UnadmittedMetadataOwner);
        receipt.registeredPhysicalAotImages=registeredAotImages.Count();
        context.images=&registeredAotImages;
        context.observedImages=&imageInventory;
        // Physical iterator #1 is observation-only: no output rows and no
        // metadata handles are retained. A class is dereferenced only after
        // it matches one of the authenticated observed registry identities.
        // The inventory is destroyed before this metadata lock is released.
        struct DiscoveryContext {
            physical::Scratch* matched;
            assembly_shadow_observed_images::Inventory<64>* images;
            const assembly_shadow_observed_images::Inventory<
                live::kMaximumRegisteredAotImages>* registry;
            live::Error error=live::Error::None;
        } inventoryContext{};
        inventoryContext.matched=&discovery;
        inventoryContext.images=&imageInventory;
        inventoryContext.registry=&registeredAotImages;
        il2cpp_class_for_each([](Il2CppClass* klass,void* raw) {
            DiscoveryContext& d=*static_cast<DiscoveryContext*>(raw);
            if(d.error!=live::Error::None)return;
            const auto event=d.matched->ObserveEnumeratedOwned(klass);
            if(event==physical::ObservationEvent::DuplicateMatch ||
               event==physical::ObservationEvent::Invalid) {
                d.error=live::Error::BadPhysicalEnumeration;return;
            }
            if(event!=physical::ObservationEvent::FirstMatch)return;
            if(!klass || !klass->image) {
                d.error=live::Error::InvalidTypeIdentity;return;
            }
            const Il2CppImage* img=klass->image;
            if(!img->assembly || !img->assembly->aname.name ||
               hybridclr::metadata::IsInterpreterIndex(img->token) ||
               !img->typeCount || img->typeCount>live::kMaximumAotImageTypes) {
                d.error=live::Error::UnsupportedImage;return;
            }
            if(!d.registry->Contains(img)) {
                d.error=live::Error::UnadmittedMetadataOwner;return;
            }
            if(!d.images->AddFromMatchedCallback(img))
                d.error=live::Error::PhysicalImageLimit;
        },&inventoryContext);
        if(inventoryContext.error!=live::Error::None)
            return reject(inventoryContext.error);
        if(!discovery.EnumeratorHealthy() || !discovery.FullyMatched() ||
           !imageInventory.Healthy() || !imageInventory.Count())
            return reject(live::Error::UnmatchedClass);
        receipt.admittedPhysicalImages=imageInventory.Count();
        // Physical iterator #2 consumes *the same* matched registry under
        // the same metadata lock. It can use the complete bounded image list
        // even if the corresponding physical class appears later in order.
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
            // Only physical AOT definitions and a narrowly owned constructed
            // generic whose definition AND each concrete TypeDef argument
            // belong to this same physical image. Generic arguments from
            // another image, recursively constructed types, arrays and
            // qualifiers fail closed. The existing legacy getter is unchanged.
            if(hybridclr::metadata::IsInterpreterIndex(image->token) ||
               image->typeCount==0 ||
               image->typeCount>live::kMaximumAotImageTypes) {
                ++c.rejected;c.error=live::Error::UnsupportedImage;return;
            }
            const bool constructed=type.type==IL2CPP_TYPE_GENERICINST;
            const Il2CppGenericClass* generic=constructed?
                type.data.generic_class:nullptr;
            if(klass->rank || type.attrs || type.num_mods ||
               type.byref || type.pinned ||
               (!constructed && klass->generic_class) ||
               (!constructed && type.type!=IL2CPP_TYPE_CLASS &&
                type.type!=IL2CPP_TYPE_VALUETYPE)) {
                ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
            }
            const Il2CppGenericInst* instance=nullptr;
            auto definition=klass->typeMetadataHandle;
            if(constructed) {
                if(!generic || generic!=klass->generic_class ||
                   !generic->type || !generic->context.class_inst ||
                   generic->context.method_inst ||
                   (generic->type->type!=IL2CPP_TYPE_CLASS &&
                    generic->type->type!=IL2CPP_TYPE_VALUETYPE) ||
                   generic->type->attrs || generic->type->num_mods ||
                   generic->type->byref || generic->type->pinned ||
                   generic->type->data.typeHandle!=definition) {
                    ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
                }
                instance=generic->context.class_inst;
                if(!instance->type_argv || instance->type_argc==0 ||
                   instance->type_argc>constructed_key::Capture::kMaximumArguments) {
                    ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
                }
            } else if(type.data.typeHandle!=definition) {
                c.error=live::Error::InvalidTypeIdentity;return;
            }

            auto count=[](const char* p,size_t& size)->bool {
                if(!p)return false;
                size=0;
                while(size<=key::Capture::kCapacity && p[size])++size;
                return size<=key::Capture::kCapacity;
            };
            size_t assemblyBytes=0;
            if(!count(owner->aname.name,assemblyBytes)) {
                c.error=live::Error::TypeKeyFailed;return;
            }
            // A complete owner inventory is available from the first
            // physical pass, and only those matched/initial-session AOT images
            // may supply TypeDef handles. Compare opaque handle identity first:
            // never dereference a borrowed metadata handle before owner proof.
            auto resolveOwner=[&](decltype(klass->typeMetadataHandle) handle,
                                  const Il2CppImage* constrained,
                                  const Il2CppImage*& resolved)->bool {
                resolved=nullptr;
                if(!handle || !c.images || !c.images->Healthy()) {
                    c.error=live::Error::UnadmittedMetadataOwner;return false;
                }
                for(size_t sourceIndex=0;sourceIndex<c.images->Count();++sourceIndex) {
                    const auto* ownerImage=c.images->Get<Il2CppImage>(sourceIndex);
                    if(!ownerImage || !ownerImage->assembly ||
                       !ownerImage->assembly->aname.name ||
                       hybridclr::metadata::IsInterpreterIndex(ownerImage->token) ||
                       !ownerImage->typeCount ||
                       ownerImage->typeCount>live::kMaximumAotImageTypes) {
                        c.error=live::Error::UnsupportedImage;return false;
                    }
                    for(uint32_t i=0;i<ownerImage->typeCount;++i) {
                        if(c.lookups==live::kMaximumTotalMetadataLookups) {
                            c.error=live::Error::SourceOutsideBudget;return false;
                        }
                        ++c.lookups;
                        if(MetadataCache::GetAssemblyTypeHandle(ownerImage,i)==handle) {
                            if(resolved && resolved!=ownerImage) {
                                c.error=live::Error::AmbiguousMetadataOwner;
                                return false;
                            }
                            resolved=ownerImage;
                            break;
                        }
                    }
                }
                if(!resolved) {
                    c.error=live::Error::UnadmittedMetadataOwner;return false;
                }
                if(constrained && constrained!=resolved) {
                    c.error=live::Error::InvalidTypeIdentity;return false;
                }
                return true;
            };
            auto buildDefinition=[&](decltype(klass->typeMetadataHandle) handle,
                                     const Il2CppImage* requiredOwner,
                                     key::Capture& result,uint32_t& leafArity,
                                     const Il2CppImage*& provenOwner)->bool {
                struct Component {
                    key::ByteView nameSpace{};
                    key::ByteView simpleName{};
                    uint32_t arity=0;
                };
                Component parts[key::Capture::kMaximumDeclarations]{};
                decltype(klass->typeMetadataHandle)
                    verifiedHandles[key::Capture::kMaximumDeclarations]{};
                size_t depth=0;
                bool ancestryComplete=false;
                leafArity=0;
                provenOwner=nullptr;
                for(;depth<key::Capture::kMaximumDeclarations;++depth) {
                    if(!handle) {
                        c.error=live::Error::UnsupportedMetadataAncestry;return false;
                    }
                    for(size_t j=0;j<depth;++j)
                        if(verifiedHandles[j]==handle) {
                            c.error=live::Error::UnsupportedMetadataAncestry;
                            return false;
                        }
                    const Il2CppImage* currentOwner=nullptr;
                    if(!resolveOwner(handle,depth?provenOwner:requiredOwner,
                                     currentOwner))return false;
                    if(!depth)provenOwner=currentOwner;
                    verifiedHandles[depth]=handle;
                    // The only dereference of this TypeDef occurs after the
                    // full physical-image ownership scan and under the same
                    // metadata lock + initial epoch Reader.
                    const auto* def=reinterpret_cast<const Il2CppTypeDefinition*>(handle);
                    const auto name=GlobalMetadata::GetTypeNamespaceAndName(handle);
                    if(!name.second) {
                        c.error=live::Error::InvalidTypeIdentity;return false;
                    }
                    const char* ns=name.first?name.first:"";
                    size_t nsBytes=0,nameBytes=0;
                    if(!count(ns,nsBytes) || !count(name.second,nameBytes)) {
                        c.error=live::Error::TypeKeyFailed;return false;
                    }
                    if(def->genericContainerIndex!=kGenericContainerIndexInvalid &&
                       hybridclr::metadata::IsInterpreterIndex(
                           def->genericContainerIndex)) {
                        c.error=live::Error::UnsupportedMetadataAncestry;return false;
                    }
                    const auto container=GlobalMetadata::GetGenericContainerFromIndex(
                        def->genericContainerIndex);
                    const uint32_t arity=GlobalMetadata::GetGenericContainerCount(container);
                    parts[depth].nameSpace={ns,nsBytes};
                    parts[depth].simpleName={name.second,nameBytes};
                    parts[depth].arity=arity;
                    if(depth==0)leafArity=arity;
                    if(def->declaringTypeIndex==kTypeIndexInvalid) {
                        ancestryComplete=true;++depth;break;
                    }
                    if(hybridclr::metadata::IsInterpreterIndex(def->declaringTypeIndex)) {
                        c.error=live::Error::UnsupportedMetadataAncestry;return false;
                    }
                    const Il2CppType* declaring=GlobalMetadata::GetIl2CppTypeFromIndex(
                        def->declaringTypeIndex);
                    if(!declaring ||
                       (declaring->type!=IL2CPP_TYPE_CLASS &&
                        declaring->type!=IL2CPP_TYPE_VALUETYPE)) {
                        c.error=live::Error::UnsupportedMetadataAncestry;return false;
                    }
                    handle=declaring->data.typeHandle;
                }
                if(!ancestryComplete || !provenOwner ||
                   !provenOwner->assembly ||
                   !provenOwner->assembly->aname.name) {
                    c.error=live::Error::UnsupportedMetadataAncestry;return false;
                }
                size_t sourceAssemblyBytes=0;
                if(!count(provenOwner->assembly->aname.name,sourceAssemblyBytes)) {
                    c.error=live::Error::TypeKeyFailed;return false;
                }
                bool ok=result.Begin(
                    {provenOwner->assembly->aname.name,sourceAssemblyBytes},
                    true,false,false);
                for(size_t i=depth;i>0&&ok;--i)
                    ok=result.AddDeclaration(parts[i-1].nameSpace,
                        parts[i-1].simpleName,parts[i-1].arity);
                if(ok)ok=result.Seal();
                if(!ok)c.error=live::Error::TypeKeyFailed;
                return ok;
            };

            key::Capture definitionKey;
            uint32_t definitionArity=0;
            const Il2CppImage* definitionOwner=nullptr;
            if(!buildDefinition(definition,image,definitionKey,
                                definitionArity,definitionOwner)) {
                ++c.rejected;return;
            }
            constructed_key::Capture genericKey;
            if(constructed) {
                // The original GENERICINST key uses the definition key plus
                // length-prefixed canonical keys of each exact class argument.
                // At most 16 TypeDef arguments; no open/primitive/array or
                // cross-image argument is silently accepted.
                if(definitionArity!=instance->type_argc ||
                   !genericKey.Begin(definitionKey,instance->type_argc)) {
                    ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
                }
                for(uint32_t i=0;i<instance->type_argc;++i) {
                    const Il2CppType* arg=instance->type_argv[i];
                    if(!arg ||
                       (arg->type!=IL2CPP_TYPE_CLASS &&
                        arg->type!=IL2CPP_TYPE_VALUETYPE) ||
                       arg->attrs || arg->num_mods || arg->byref || arg->pinned ||
                       !arg->data.typeHandle) {
                        ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
                    }
                    key::Capture argumentKey;
                    uint32_t ignoredArity=0;
                    const Il2CppImage* argumentOwner=nullptr;
                    if(!buildDefinition(arg->data.typeHandle,nullptr,argumentKey,
                                        ignoredArity,argumentOwner)) {
                        ++c.rejected;return;
                    }
                    if(argumentOwner!=image)c.usedCrossImageArgument=true;
                    if(!c.observedImages->Contains(argumentOwner))
                        c.usedUnobservedAotArgumentImage=true;
                    if(!genericKey.AddArgument(argumentKey)) {
                        c.error=live::Error::TypeKeyFailed;return;
                    }
                }
                if(!genericKey.Seal()) {
                    c.error=live::Error::TypeKeyFailed;return;
                }
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
            const bool stored=constructed ?
                c.rows->Add(registration,role,
                    {owner->aname.name,assemblyBytes},genericKey,
                    {pointer,pointerBytes},flags) :
                c.rows->Add(registration,role,
                    {owner->aname.name,assemblyBytes},definitionKey,
                    {pointer,pointerBytes},flags);
            if(!stored) {
                c.error=live::Error::RowsFailed;return;
            }
        },&context);
        // Revisit the SAME raw physical registry under the assembly lock.
        // Ordered identity equality is an observational source fence only,
        // not a VM-wide epoch, ABA barrier or class-cctor mutation lock.
        assembly_shadow_observed_images::Inventory<
            live::kMaximumRegisteredAotImages> registeredAfter;
        AotRegistrySink afterSink{};
        afterSink.inventory=&registeredAfter;
        if(!Assembly::TryVisitRegisteredAotImages(readAotRegistry,&afterSink) ||
           !registeredAfter.SameOrderedImageSet(registeredAotImages))
            return reject(live::Error::SourceChanged);
        receipt.registeredImageSetStable=true;
        // Borrowed metadata pointers are scrubbed while g_MetadataLock is
        // still held. Nothing escapes to the frozen writer after this scope.
        context.images=nullptr;
        context.observedImages=nullptr;
    }

    receipt.metadataLookups=context.lookups;
    receipt.rejectedUnsupported=context.rejected;
    receipt.usedCrossImageArgument=context.usedCrossImageArgument;
    receipt.usedUnobservedAotArgumentImage=context.usedUnobservedAotArgumentImage;
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
       stateBefore!=s_state.load(std::memory_order_acquire) ||
       // Terminal publication may race the physical traversal independently
       // of s_state and of the opaque execution-registry revision.
       s_firstFailureV2.status()!=assembly_shadow_first_failure_v2::State::Empty)
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
       stateAfter!=stateBefore ||
       s_firstFailureV2.status()!=assembly_shadow_first_failure_v2::State::Empty ||
       !record.Complete())
        return reject(live::Error::SourceChanged);

    membership.Scrub();
    receipt.serializedRows=rows.Count();
    receipt.ownedBytes=rows.TextBytes();
    receipt.callbackPhysicalMembershipProven=true;
    receipt.restrictedTypeDefSubsetProven=true;
    receipt.observedRegistryAndCounterFenceStable=true;
    // Deliberately NOT a complete arbitrary TypeKey/field15/VM-epoch claim.
    return true;
}
