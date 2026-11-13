// R03 B2 INACTIVE: restricted, actual VM physical TypeDef -> owned class
// rows -> original frozen execution/R02 record. Included ONCE by
// AssemblyShadow.cpp inside il2cpp::vm; NEVER called by a Player or V1 getter.
// Nested physical metadata definitions require an owner-checked ancestry.
// Restricted AOT TypeDefs and bounded composite/Unicode/primitive arguments
// use registered PHYSICAL AOT owners, including unobserved argument images.
// Plain nongeneric/nonnested interpreter TypeDefs can use their ORIGINAL V1
// physical owner; interpreter composites/VAR/MVAR still fail closed.
// A second visitor mirrors the ORIGINAL V1 CopyValidAssemblies token-valid
// PHYSICAL registration order, including interpreter and zero-TypeDef images,
// and verifies every admitted key handle has the same FIRST-emplace owner.
// Both V1/registered image inventories are sampled twice while an initial
// metadata Reader and g_MetadataLock remain active. Unsupported/unowned,
// conflicting, open-generic and over-budget source always fails closed.
// These independently sampled inventories, original cctor/static fields and
// R02/registry fences do NOT establish a VM-wide epoch, full original V1
// execution field15 or terminal-safe native V2 transport.
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
    namespace composite_key=::r03_b2_owned_composite_type_key;
    namespace original_join=assembly_shadow_original_v1_owner_join;
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

    // This exact primitive snapshot covers the fields printed by the
    // ORIGINAL execution V1 per-class serializer, not a class-initialization
    // lifecycle/GC epoch. It owns value bits and an address value ONLY;
    // no class pointer is saved after a callback. The initial-session Reader
    // and g_MetadataLock cover both authenticated class enumerations.
    // V1 source includes image/TypeDef/byval root identity, not only
    // per-class cctor/static facts. Snapshot raw pointer VALUES only under
    // authenticated physical callbacks + g_MetadataLock/initial Reader.
    // Exact root comparisons detect observed mutation but not GC provenance,
    // ABA, changing metadata bytes or a coherent whole-VM epoch.
    struct ClassFacts {
        uintptr_t storage=0;
        uintptr_t image=0,definition=0,typeData=0,generic=0;
        uint32_t typeCode=0;
        uint8_t bits=0; // started=1, finished=2, exception=4
        bool observed=false;
        static ClassFacts Read(const Il2CppClass* klass) noexcept {
            ClassFacts out{};
            if(!klass)return out;
            if(os::Atomic::LoadRelaxed(
                  reinterpret_cast<const int32_t*>(&klass->cctor_started))!=0)
                out.bits|=1;
            if(os::Atomic::LoadRelaxed(
                  reinterpret_cast<const int32_t*>(&klass->cctor_finished_or_no_cctor))!=0)
                out.bits|=2;
            if(os::Atomic::LoadRelaxed(
                  reinterpret_cast<const int32_t*>(&klass->initializationExceptionGCHandle))!=0)
                out.bits|=4;
            out.storage=reinterpret_cast<uintptr_t>(klass->static_fields);
            out.image=reinterpret_cast<uintptr_t>(klass->image);
            out.definition=reinterpret_cast<uintptr_t>(klass->typeMetadataHandle);
            out.generic=reinterpret_cast<uintptr_t>(klass->generic_class);
            const Il2CppType& type=klass->byval_arg;
            out.typeCode=static_cast<uint32_t>(type.type);
            if(type.type==IL2CPP_TYPE_CLASS ||
               type.type==IL2CPP_TYPE_VALUETYPE)
                out.typeData=reinterpret_cast<uintptr_t>(type.data.typeHandle);
            else if(type.type==IL2CPP_TYPE_GENERICINST)
                out.typeData=reinterpret_cast<uintptr_t>(type.data.generic_class);
            // Other type roots fail the restricted source's shape check:
            // do not read an inactive type-data union member here.
            out.observed=true;
            return out;
        }
        bool SameClassState(const ClassFacts& other) const noexcept {
            return observed && other.observed &&
                   bits==other.bits && storage==other.storage;
        }
        bool SameRoot(const ClassFacts& other) const noexcept {
            return observed && other.observed &&
                   image==other.image && definition==other.definition &&
                   typeData==other.typeData && typeCode==other.typeCode &&
                   generic==other.generic;
        }
    };
    // Each fact is <=48 bytes; PackedFacts below holds at most 1024 facts
    // plus a 2048-slot uint16 index (<=53,264 bytes). Actual Unity thread-
    // stack consumption and native ABI remain unverified.
    static_assert(sizeof(ClassFacts)<=48,
                  "B2 original class source facts stack bound changed");
    using PackedFacts=assembly_shadow_packed_class_facts::Scratch<
        ClassFacts,physical::kSlots,physical::kMaximumObserved>;
    static_assert(sizeof(PackedFacts)<=53264,
                  "B2 packed class facts exceeds 52-KiB bound");

    // Capture every distinct non-root TypeDef owner admitted by resolveOwner
    // while formatting nested declarations, composite generic arguments or
    // primitive defaults. The separate third class pass covers class roots.
    // This finite inventory does not cover arbitrary VM TypeDef maps.
    // Existing V1 physical-owner admission is necessary but not sufficient
    // for declaration consistency: the same owned handle may later expose
    // different namespace/name bytes or declaration ancestry/arity.
    // Bounded EXACT copies for admitted non-root TypeDefs only. Oversized
    // names fail closed, not truncated or hashed; no metadata char* escapes.
    struct NonRootOwnerAudit {
        using Handle=decltype(static_cast<Il2CppClass*>(nullptr)->typeMetadataHandle);
        enum { kNameBytes=live::kMaximumNonRootTypeDefNameBytes }; // C++11 local type
        struct Pair {
            Handle handle=nullptr;
            const Il2CppImage* image=nullptr;
            char nameSpace[kNameBytes]{};
            char simpleName[kNameBytes]{};
            uint32_t namespaceBytes=0,nameBytes=0;
            int32_t genericIndex=0,declaringIndex=0;
            uint32_t arity=0;
            bool captured=false;
            bool Same(const char* ns,size_t nsLength,const char* name,
                      size_t nameLength,int32_t generic,int32_t declaring,
                      uint32_t expectedArity) const noexcept {
                if(!captured || !ns || !name || namespaceBytes!=nsLength ||
                   nameBytes!=nameLength || genericIndex!=generic ||
                   declaringIndex!=declaring || arity!=expectedArity)
                    return false;
                for(size_t i=0;i<nsLength;++i)
                    if(nameSpace[i]!=ns[i])return false;
                for(size_t i=0;i<nameLength;++i)
                    if(simpleName[i]!=name[i])return false;
                return true;
            }
        };
        std::array<Pair,live::kMaximumNonRootTypeDefOwnerPairs> pairs{};
        size_t used=0;
        enum class NoteResult : uint8_t {Accepted,Conflict,Overflow};
        NoteResult Note(Handle handle,const Il2CppImage* image) noexcept {
            if(!handle || !image)return NoteResult::Conflict;
            for(size_t i=0;i<used;++i)if(pairs[i].handle==handle)
                return pairs[i].image==image ?
                    NoteResult::Accepted:NoteResult::Conflict;
            if(used==pairs.size())return NoteResult::Overflow;
            pairs[used].handle=handle;
            pairs[used].image=image;
            ++used;
            return NoteResult::Accepted;
        }
        bool Capture(Handle handle,const char* ns,size_t nsLength,
                     const char* name,size_t nameLength,int32_t generic,
                     int32_t declaring,uint32_t arity) noexcept {
            if(!handle || !ns || !name || nsLength>kNameBytes ||
               nameLength>kNameBytes)return false;
            for(size_t i=0;i<used;++i)if(pairs[i].handle==handle) {
                Pair& entry=pairs[i];
                if(entry.captured)
                    return entry.Same(ns,nsLength,name,nameLength,
                                      generic,declaring,arity);
                entry.namespaceBytes=static_cast<uint32_t>(nsLength);
                entry.nameBytes=static_cast<uint32_t>(nameLength);
                entry.genericIndex=generic;
                entry.declaringIndex=declaring;
                entry.arity=arity;
                for(size_t j=0;j<nsLength;++j)entry.nameSpace[j]=ns[j];
                for(size_t j=0;j<nameLength;++j)entry.simpleName[j]=name[j];
                entry.captured=true;
                return true;
            }
            return false;
        }
        static bool CountBounded(const char* p,size_t& length) noexcept {
            if(!p)return false;
            length=0;
            while(length<=kNameBytes && p[length])++length;
            return length<=kNameBytes;
        }
        void Scrub() noexcept {pairs.fill(Pair{});used=0;}
        ~NonRootOwnerAudit() noexcept {Scrub();}
    };
    // At most 128 exact copies, two <=64-byte fields per entry.
    // Test the native ABI/stack profile before ever activating this source.
    static_assert(sizeof(NonRootOwnerAudit)<=24576,
                  "B2 non-root declaration snapshot stack bound changed");
    struct Context {
        physical::Scratch* membership;
        owned::Rows* rows;
        const ActiveSnapshot* active;
        live::Error error=live::Error::None;
        size_t lookups=0, rejected=0;
        size_t originalV1Lookups=0, originalV1Matches=0;
        size_t classFactsMatched=0, classRootFactsMatched=0;
        PackedFacts* firstPassFacts=nullptr;
        NonRootOwnerAudit* nonRootOwners=nullptr;
        assembly_shadow_observed_images::Inventory<live::kMaximumRegisteredAotImages>* images=nullptr;
        assembly_shadow_observed_images::Inventory<64>* observedImages=nullptr;
        assembly_shadow_observed_images::Inventory<
            live::kMaximumOriginalV1Images>* originalV1Images=nullptr;
        bool usedCrossImageArgument=false;
        bool usedUnobservedAotArgumentImage=false;
        bool usedUnicodeAssemblyFold=false;
        bool usedExistingPrimitiveDefault=false;
        bool usedSimpleInterpreterDefinition=false;
        size_t admittedPrimitiveArguments=0;
        bool usedNestedGenericArgument=false;
        bool usedPointerArgument=false;
        bool usedSzArrayArgument=false;
        bool usedMdArrayArgument=false;
        bool usedQualifiedArgument=false;
        size_t compositeNodesVisited=0,maximumCompositeDepth=0;
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
        // One 16-byte observed primitive fact per 2048 registry slots, no
        // metadata address or class pointer escapes this lock scope.
        // At most 1024 physical observed classes fit in 2048 registry slots.
        // Dense facts with a one-based slot map cost about 52 KiB vs 96 KiB.
        PackedFacts firstPassFacts{};
        NonRootOwnerAudit nonRootOwners{};
        context.nonRootOwners=&nonRootOwners;
        assembly_shadow_observed_images::Inventory<
            live::kMaximumOriginalV1Images> originalV1Images;
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
        // The original V1 physical enumeration can contain only interpreter
        // TypeDefs. Do NOT require an unrelated AOT TypeDef merely to admit
        // a plain interpreter row. Every AOT root/argument still requires
        // registeredAotImages.Contains() during its own owner resolution.
        if(!registeredAotImages.Healthy())
            return reject(live::Error::UnadmittedMetadataOwner);
        receipt.registeredPhysicalAotImages=registeredAotImages.Count();
        // Exact borrowed original-V1 image primitives, not an all-VM epoch.
        // The existing second physical image visit checks these without
        // retaining images/TypeDef handles outside the metadata-lock scope.
        // Bounded additional stack: 12 bytes x 1024 image visits = 12 KiB.
        struct OriginalV1ImageFacts {
            uint32_t typeCount=0,imageToken=0,assemblyToken=0;
            static OriginalV1ImageFacts Read(const Il2CppImage* image) noexcept {
                OriginalV1ImageFacts value{};
                value.typeCount=static_cast<uint32_t>(image->typeCount);
                value.imageToken=static_cast<uint32_t>(image->token);
                value.assemblyToken=static_cast<uint32_t>(image->assembly->token);
                return value;
            }
            bool Same(const OriginalV1ImageFacts& other) const noexcept {
                return typeCount==other.typeCount &&
                       imageToken==other.imageToken &&
                       assemblyToken==other.assemblyToken;
            }
        };
        static_assert(sizeof(OriginalV1ImageFacts)==12,
                      "B2 original V1 image primitive stack bound changed");
        std::array<OriginalV1ImageFacts,live::kMaximumOriginalV1Images>
            firstOriginalV1Facts{};
        // The ORIGINAL GetExecutionDiagnosticsJson uses
        // Assembly::CaptureShadowEnumeration->CopyValidAssemblies, which
        // visits token-valid PHYSICAL images including interpreter images.
        // GetAllAssemblies' logical/shadow mapping is NOT this V1 source.
        // Borrow exactly this original V1 image order, without allocating
        // or retaining it outside the initial metadata-lock/Reader scope.
        struct OriginalV1Sink {
            assembly_shadow_observed_images::Inventory<
                live::kMaximumOriginalV1Images>* images;
            OriginalV1ImageFacts* firstFacts=nullptr;
            size_t visits=0;
            bool compareFacts=false;
            live::Error error=live::Error::None;
        } originalSink{};
        originalSink.images=&originalV1Images;
        originalSink.firstFacts=firstOriginalV1Facts.data();
        auto readOriginalV1=[](const Il2CppImage* img,void* raw)->bool {
            OriginalV1Sink& sink=*static_cast<OriginalV1Sink*>(raw);
            if(!img || !img->assembly || !img->assembly->image ||
               img->assembly->image!=img || !img->assembly->token ||
               !img->assembly->aname.name ||
               img->typeCount>live::kMaximumAotImageTypes) {
                sink.error=live::Error::OriginalV1ImageSourceUnavailable;
                return false;
            }
            if(!sink.firstFacts ||
               sink.visits==live::kMaximumOriginalV1Images) {
                sink.error=live::Error::PhysicalImageLimit;return false;
            }
            const size_t slot=sink.visits++;
            const OriginalV1ImageFacts current=OriginalV1ImageFacts::Read(img);
            if(sink.compareFacts) {
                if(!sink.firstFacts[slot].Same(current)) {
                    sink.error=live::Error::OriginalV1ImageFactsChanged;
                    return false;
                }
            } else sink.firstFacts[slot]=current;
            const size_t before=sink.images->Count();
            if(!sink.images->AddFromRegisteredAssembly(img)) {
                sink.error=live::Error::PhysicalImageLimit;return false;
            }
            // Original V1 enumerates physical registrations, not a deduped
            // subset. A duplicate would otherwise silently collapse here.
            if(sink.images->Count()!=before+1) {
                sink.error=live::Error::OriginalV1ImageSourceUnavailable;
                return false;
            }
            return true;
        };
        if(!Assembly::TryVisitOriginalV1ExecutionImages(
               readOriginalV1,&originalSink) ||
           !originalV1Images.Healthy() || !originalV1Images.Count())
            return reject(originalSink.error==live::Error::None ?
                live::Error::OriginalV1ImageSourceUnavailable:
                originalSink.error);
        receipt.originalV1Images=originalV1Images.Count();
        context.originalV1Images=&originalV1Images;
        context.images=&registeredAotImages;
        context.observedImages=&imageInventory;
        context.firstPassFacts=&firstPassFacts;
        // Physical iterator #1 is observation-only: no output rows and no
        // metadata handles are retained. A class is dereferenced only after
        // it matches one of the authenticated observed registry identities.
        // The inventory is destroyed before this metadata lock is released.
        struct DiscoveryContext {
            physical::Scratch* matched;
            assembly_shadow_observed_images::Inventory<64>* images;
            const assembly_shadow_observed_images::Inventory<
                live::kMaximumRegisteredAotImages>* registry;
            const assembly_shadow_observed_images::Inventory<
                live::kMaximumOriginalV1Images>* originalV1Images=nullptr;
            PackedFacts* firstPassFacts=nullptr;
            live::Error error=live::Error::None;
        } inventoryContext{};
        inventoryContext.matched=&discovery;
        inventoryContext.images=&imageInventory;
        inventoryContext.registry=&registeredAotImages;
        inventoryContext.originalV1Images=&originalV1Images;
        inventoryContext.firstPassFacts=&firstPassFacts;
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
            if(!img->assembly || img->assembly->image!=img ||
               !img->assembly->aname.name ||
               !img->typeCount || img->typeCount>live::kMaximumAotImageTypes) {
                d.error=live::Error::UnsupportedImage;return;
            }
            const bool interpreter=
                hybridclr::metadata::IsInterpreterIndex(img->token);
            // No arbitrary image source: AOT rows must be in the registered
            // AOT table; interpreter rows must have a token-valid physical
            // original V1 image in the separately authenticated image table.
            if(interpreter ?
               !d.originalV1Images || !d.originalV1Images->Contains(img) :
               !d.registry->Contains(img)) {
                d.error=live::Error::UnadmittedMetadataOwner;return;
            }
            if(!d.images->AddFromMatchedCallback(img)) {
                d.error=live::Error::PhysicalImageLimit;return;
            }
            // The first actual physical callback samples original V1
            // cctor/static primitives only after registered-image and
            // opaque execution-registry identity proof. These are value
            // snapshots, not a retained class address or lifetime ticket.
            size_t slot=physical::kSlots;
            assembly_shadow_execution_witness::Source source=
                assembly_shadow_execution_witness::Source::None;
            if(!d.matched->TryObservedRegistrationBinding(
                    klass,slot,source) ||
               !d.firstPassFacts || slot>=physical::kSlots ||
               d.firstPassFacts->Find(slot)) {
                d.error=live::Error::InvalidRegistry;return;
            }
            const ClassFacts sampled=ClassFacts::Read(klass);
            if(!sampled.observed || !d.firstPassFacts->Insert(slot,sampled)) {
                d.error=live::Error::InvalidRegistry;return;
            }
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
            const ClassFacts* previous=c.firstPassFacts ?
                c.firstPassFacts->Find(registration):nullptr;
            if(!previous || !previous->observed) {
                c.error=live::Error::InvalidRegistry;return;
            }
            const ClassFacts secondFacts=ClassFacts::Read(klass);
            if(!previous->SameRoot(secondFacts)) {
                ++c.rejected;
                c.error=live::Error::ObservedClassRootChanged;return;
            }
            if(!previous->SameClassState(secondFacts)) {
                ++c.rejected;
                c.error=live::Error::ObservedClassFactsChanged;return;
            }
            if(!klass || !klass->image || !klass->image->assembly ||
               !klass->image->assembly->aname.name ||
               !klass->typeMetadataHandle || !klass->name) {
                c.error=live::Error::InvalidTypeIdentity;return;
            }
            const Il2CppImage* image=klass->image;
            const Il2CppAssembly* owner=image->assembly;
            const Il2CppType& type=klass->byval_arg;
            // Original V1 enumerates token-valid PHYSICAL interpreter images.
            // Accept only their plain CLASS/VALUETYPE TypeDefs, not constructed
            // or open-generic shapes, with a separate first-emplace owner proof.
            if(image->typeCount==0 ||
               image->typeCount>live::kMaximumAotImageTypes) {
                ++c.rejected;c.error=live::Error::UnsupportedImage;return;
            }
            const bool interpreterDefinition=
                hybridclr::metadata::IsInterpreterIndex(image->token);
            const bool constructed=type.type==IL2CPP_TYPE_GENERICINST;
            if(interpreterDefinition && (constructed ||
               klass->generic_class || klass->genericContainerHandle ||
               klass->declaringType || !c.originalV1Images ||
               !c.originalV1Images->Contains(image))) {
                ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
            }
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
                // A simple interpreter root may only resolve to its own
                // physical original-V1 registered image. Unconstrained generic
                // arguments stay on the prior registered-AOT owner path.
                const bool interpreterRoot=constrained &&
                    hybridclr::metadata::IsInterpreterIndex(constrained->token);
                if(interpreterRoot) {
                    if(!c.originalV1Images ||
                       !c.originalV1Images->Contains(constrained) ||
                       !constrained->typeCount ||
                       constrained->typeCount>live::kMaximumAotImageTypes) {
                        c.error=live::Error::UnadmittedMetadataOwner;return false;
                    }
                    for(uint32_t i=0;i<constrained->typeCount;++i) {
                        if(c.lookups==live::kMaximumTotalMetadataLookups) {
                            c.error=live::Error::SourceOutsideBudget;return false;
                        }
                        ++c.lookups;
                        if(MetadataCache::GetAssemblyTypeHandle(constrained,i)==handle) {
                            resolved=constrained;break;
                        }
                    }
                    if(!resolved) {
                        c.error=live::Error::UnadmittedMetadataOwner;return false;
                    }
                } else for(size_t sourceIndex=0;sourceIndex<c.images->Count();++sourceIndex) {
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
                // The legacy unordered_map::emplace is FIRST-owner-wins
                // across ALL token-valid physical images (interpreter too).
                // Verify that independent original V1 first owner matches
                // the already authenticated registered physical AOT owner.
                // No TypeDef handle is dereferenced by this comparison.
                original_join::Receipt joined{};
                if(!c.originalV1Images ||
                   !original_join::CheckFirstOwner(
                       *c.originalV1Images,resolved,handle,
                       [](const Il2CppImage* img,uint32_t index) {
                           return MetadataCache::GetAssemblyTypeHandle(img,index);
                       },
                       c.originalV1Lookups,
                       live::kMaximumTotalMetadataLookups,
                       live::kMaximumAotImageTypes,joined)) {
                    if(joined.error==original_join::Error::FirstOwnerDifferent)
                        c.error=live::Error::OriginalV1FirstOwnerConflict;
                    else if(joined.error==original_join::Error::LookupBudget)
                        c.error=live::Error::OriginalV1LookupBudget;
                    else if(joined.error==original_join::Error::OwnerNotFound)
                        c.error=live::Error::OriginalV1OwnerMissing;
                    else
                        c.error=live::Error::OriginalV1ImageSourceUnavailable;
                    return false;
                }
                ++c.originalV1Matches;
                // The final class traversal separately checks each registered
                // class root. Remember all OTHER proven TypeDef handles used
                // by this row so a late metadata-index owner change cannot
                // evade the root-only end pass. Never retain a live TypeDef.
                if(handle!=klass->typeMetadataHandle) {
                    if(!c.nonRootOwners) {
                        c.error=live::Error::InvalidRegistry;return false;
                    }
                    const auto noted=c.nonRootOwners->Note(handle,resolved);
                    if(noted!=NonRootOwnerAudit::NoteResult::Accepted) {
                        c.error=noted==NonRootOwnerAudit::NoteResult::Overflow ?
                            live::Error::SourceOutsideBudget:
                            live::Error::AmbiguousMetadataOwner;
                        return false;
                    }
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
                    // Avoid staged container/declaring-type lookup for now.
                    // Only the plain interpreter TypeDef itself is admitted.
                    if(hybridclr::metadata::IsInterpreterIndex(currentOwner->token) &&
                       (depth!=0 ||
                        def->genericContainerIndex!=kGenericContainerIndexInvalid ||
                        def->declaringTypeIndex!=kTypeIndexInvalid)) {
                        c.error=live::Error::UnsupportedTypeShape;return false;
                    }
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
                    if(handle!=klass->typeMetadataHandle) {
                        // The original first-emplace owner is already proven.
                        // Make a BYTE-EXACT declaration copy for the final
                        // bounded non-root metadata-content recheck.
                        if(!c.nonRootOwners || !c.nonRootOwners->Capture(
                               handle,ns,nsBytes,name.second,nameBytes,
                               static_cast<int32_t>(def->genericContainerIndex),
                               static_cast<int32_t>(def->declaringTypeIndex),arity)) {
                            c.error=nsBytes>NonRootOwnerAudit::kNameBytes ||
                                nameBytes>NonRootOwnerAudit::kNameBytes ?
                                live::Error::SourceOutsideBudget :
                                live::Error::OriginalV1TypeDefContentChanged;
                            return false;
                        }
                    }
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
                // V1 canonical assembly normalization is source-of-truth:
                // ViewName strips path/version/extension and NextFolded uses
                // original VmStringUtils UTF16 ordinal case folding.
                const auto canonical=assembly_shadow_detail::ViewName(
                    provenOwner->assembly->aname.name);
                if(!canonical.begin||canonical.begin==canonical.end) {
                    c.error=live::Error::TypeKeyFailed;return false;
                }
                size_t canonicalHash=0;
                if(!assembly_shadow_detail::NameHash(canonical,canonicalHash)) {
                    c.error=live::Error::TypeKeyFailed;return false;
                }
                bool ok=result.BeginCanonicalFolded(
                    {canonical.begin,static_cast<size_t>(canonical.end-canonical.begin)},
                    true,false,false,
                    [](const char*& next,const char* end,uint16_t& first,
                       uint16_t& second)->bool {
                        return assembly_shadow_detail::NextFolded(
                            next,end,first,second);
                    });
                for(size_t i=depth;i>0&&ok;--i)
                    ok=result.AddDeclaration(parts[i-1].nameSpace,
                        parts[i-1].simpleName,parts[i-1].arity);
                if(ok)ok=result.Seal();
                if(ok) {
                    for(const char* pos=canonical.begin;pos!=canonical.end;++pos)
                        if(static_cast<unsigned char>(*pos)>=0x80u) {
                            c.usedUnicodeAssemblyFold=true;break;
                        }
                }
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
            // Preserve the pinned native FormatType spelling while owning
            // every nested leaf. All TypeDef dereferences go through the
            // already proven registered-AOT owner map above.
            // This is a deliberately bounded SUBSET: primitive, open VAR/MVAR,
            // unregistered, non-ASCII-folded assemblies and postterminal source
            // still fail closed. A composite key is not a whole-VM epoch.
            struct Composer {
                decltype(buildDefinition)& makeDefinition;
                Context& context;
                const Il2CppImage* sourceImage;
                enum { MaximumDepth=8 };

                bool Fail() {
                    ++context.rejected;
                    if(context.error==live::Error::None)
                        context.error=live::Error::UnsupportedTypeShape;
                    return false;
                }
                bool Format(const Il2CppType* value,
                            composite_key::Capture& destination,
                            size_t depth) {
                    if(!value||depth>MaximumDepth)return Fail();
                    ++context.compositeNodesVisited;
                    if(depth>context.maximumCompositeDepth)
                        context.maximumCompositeDepth=depth;
                    if(value->attrs||value->num_mods||value->byref||value->pinned) {
                        context.usedQualifiedArgument=true;
                        composite_key::Capture core;
                        // Qualifiers are wrapped around this same original
                        // metadata node, not appended to its leaf.
                        if(!Raw(value,core,depth))return false;
                        if(!destination.FromQualified(
                            core,value->attrs,value->num_mods,
                            value->byref,value->pinned))
                            return Fail();
                        return true;
                    }
                    return Raw(value,destination,depth);
                }
                bool Raw(const Il2CppType* value,
                         composite_key::Capture& destination,
                         size_t depth) {
                    if(!value)return Fail();
                    switch(value->type) {
                        case IL2CPP_TYPE_CLASS:
                        case IL2CPP_TYPE_VALUETYPE: {
                            if(!value->data.typeHandle)return Fail();
                            key::Capture definition;
                            uint32_t arity=0;
                            const Il2CppImage* resolved=nullptr;
                            if(!makeDefinition(value->data.typeHandle,nullptr,
                                               definition,arity,resolved))
                                return false;
                            if(resolved!=sourceImage)
                                context.usedCrossImageArgument=true;
                            if(!context.observedImages->Contains(resolved))
                                context.usedUnobservedAotArgumentImage=true;
                            if(!destination.FromDefinition(definition))
                                return Fail();
                            return true;
                        }
                        case IL2CPP_TYPE_GENERICINST: {
                            context.usedNestedGenericArgument=true;
                            const Il2CppGenericClass* g=value->data.generic_class;
                            if(!g||!g->type||
                               (g->type->type!=IL2CPP_TYPE_CLASS &&
                                g->type->type!=IL2CPP_TYPE_VALUETYPE) ||
                               g->type->attrs||g->type->num_mods||
                               g->type->byref||g->type->pinned||
                               !g->type->data.typeHandle||
                               !g->context.class_inst||
                               g->context.method_inst)return Fail();
                            const Il2CppGenericInst* args=g->context.class_inst;
                            if(!args->type_argv||args->type_argc==0||
                               args->type_argc>composite_key::Capture::kMaximumArguments)
                                return Fail();
                            key::Capture definition;
                            uint32_t arity=0;
                            const Il2CppImage* resolved=nullptr;
                            if(!makeDefinition(g->type->data.typeHandle,nullptr,
                                               definition,arity,resolved))
                                return false;
                            if(arity!=args->type_argc)return Fail();
                            if(resolved!=sourceImage)
                                context.usedCrossImageArgument=true;
                            if(!context.observedImages->Contains(resolved))
                                context.usedUnobservedAotArgumentImage=true;
                            if(!destination.BeginGeneric(definition,args->type_argc))
                                return Fail();
                            for(uint32_t i=0;i<args->type_argc;++i) {
                                composite_key::Capture child;
                                if(!Format(args->type_argv[i],child,depth+1)||
                                   !destination.AddGenericArgument(child))
                                    return Fail();
                            }
                            if(!destination.SealGeneric())return Fail();
                            return true;
                        }
                        case IL2CPP_TYPE_PTR:
                        case IL2CPP_TYPE_SZARRAY: {
                            if(value->type==IL2CPP_TYPE_PTR)
                                context.usedPointerArgument=true;
                            else
                                context.usedSzArrayArgument=true;
                            composite_key::Capture child;
                            if(!Format(value->data.type,child,depth+1))
                                return false;
                            const bool ok=value->type==IL2CPP_TYPE_PTR
                                ?destination.FromPointer(child)
                                :destination.FromSzArray(child);
                            if(!ok)return Fail();
                            return true;
                        }
                        case IL2CPP_TYPE_ARRAY: {
                            context.usedMdArrayArgument=true;
                            const Il2CppArrayType* array=value->data.array;
                            if(!array||!array->rank ||
                               (array->numsizes&&!array->sizes) ||
                               (array->numlobounds&&!array->lobounds))
                                return Fail();
                            composite_key::Capture child;
                            if(!Format(array->etype,child,depth+1))return false;
                            if(!destination.FromArray(
                                child,array->rank,array->sizes,array->numsizes,
                                array->lobounds,array->numlobounds))
                                return Fail();
                            return true;
                        }
                        default: {
                            // Mirror the original Class.cpp primitive
                            // default switch without its ASSERT. If a
                            // default has not been initialized, fail closed
                            // instead of aborting under IL2CPP_DEBUG.
                            // Exact mapping is compared against original
                            // Class.cpp by the four-fork source gate.
                            // No class creation; VAR/MVAR return null.
                            const Il2CppClass* primitive=
                                assembly_shadow_existing_primitive_defaults::Peek(
                                    value->type);
                            if(!primitive || !primitive->image ||
                               !primitive->image->assembly ||
                               !primitive->typeMetadataHandle ||
                               primitive->rank || primitive->generic_class ||
                               primitive->genericContainerHandle)
                                return Fail();
                            key::Capture existingDefault;
                            uint32_t arity=0;
                            const Il2CppImage* resolved=nullptr;
                            if(!makeDefinition(primitive->typeMetadataHandle,
                                               primitive->image,
                                               existingDefault,arity,resolved))
                                return false;
                            if(!resolved || resolved!=primitive->image ||
                               arity!=0 || primitive->declaringType)
                                return Fail();
                            // The original V1 Make(primitive) uses *class*
                            // declaration names. Our owned key uses physical
                            // TypeDef names. Require equality before claiming
                            // primitive V1 grammar parity in this subset.
                            const auto names=GlobalMetadata::GetTypeNamespaceAndName(
                                primitive->typeMetadataHandle);
                            if(!primitive->name || !names.second ||
                               std::strcmp(primitive->name,names.second)!=0 ||
                               std::strcmp(primitive->namespaze?
                                   primitive->namespaze:"",names.first?
                                   names.first:"")!=0)
                                return Fail();
                            if(!destination.FromDefinition(existingDefault))
                                return Fail();
                            if(resolved!=sourceImage)
                                context.usedCrossImageArgument=true;
                            if(!context.observedImages->Contains(resolved))
                                context.usedUnobservedAotArgumentImage=true;
                            context.usedExistingPrimitiveDefault=true;
                            ++context.admittedPrimitiveArguments;
                            return true;
                        }
                    }
                }
            };
            Composer composer{buildDefinition,c,image};
            composite_key::Capture genericKey;
            if(constructed) {
                if(definitionArity!=instance->type_argc ||
                   !genericKey.BeginGeneric(definitionKey,instance->type_argc)) {
                    ++c.rejected;c.error=live::Error::UnsupportedTypeShape;return;
                }
                for(uint32_t i=0;i<instance->type_argc;++i) {
                    composite_key::Capture argument;
                    if(!composer.Format(instance->type_argv[i],argument,0))
                        return;
                    if(!genericKey.AddGenericArgument(argument)) {
                        ++c.rejected;c.error=live::Error::TypeKeyFailed;return;
                    }
                }
                if(!genericKey.SealGeneric()) {
                    c.error=live::Error::TypeKeyFailed;return;
                }
            }

            owned::Flags flags{};
            const bool baseline=c.active && c.active->byAssembly.count(owner);
            const bool shadow=c.active && c.active->shadowToBaseline.count(owner);
            flags.interpreterShadow=baseline||shadow;
            flags.physicalInterpreter=Interpreter(owner);
            flags.isActive=!baseline;
            // Third point-in-time check, still during this matched class
            // callback and under g_MetadataLock, after TypeKey formatting.
            // If a cctor or static field address visibly changed while the
            // bounded source was executing, revoke the entire class record.
            const ClassFacts finalFacts=ClassFacts::Read(klass);
            if(!secondFacts.SameRoot(finalFacts)) {
                ++c.rejected;
                c.error=live::Error::ObservedClassRootChanged;return;
            }
            if(!secondFacts.SameClassState(finalFacts)) {
                ++c.rejected;
                c.error=live::Error::ObservedClassFactsChanged;return;
            }
            ++c.classFactsMatched;
            ++c.classRootFactsMatched;
            flags.cctorStarted=(finalFacts.bits&1)!=0;
            flags.cctorFinished=(finalFacts.bits&2)!=0;
            flags.hasInitializationException=(finalFacts.bits&4)!=0;
            const void* storage=reinterpret_cast<const void*>(finalFacts.storage);
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
            if(interpreterDefinition)
                c.usedSimpleInterpreterDefinition=true;
            // Save the last observed primitive AFTER the owned row has been
            // recorded. A later row's TypeKey source may mutate an earlier
            // class; a final per-row read cannot detect that late change.
            if(!c.firstPassFacts->Update(registration,finalFacts)) {
                c.error=live::Error::InvalidRegistry;return;
            }
        },&context);
        // Third, end-of-collection physical traversal rechecks EVERY
        // previously admitted class, including early rows that a later
        // TypeKey formatter could mutate after their row was copied.
        // The second pass updated firstPassFacts with its FINAL primitive
        // values only after successful owned-row publication. Each third-pass
        // callback must be independently unique; missing, duplicated or
        // changed sources reject the entire record before any positive
        // receipt can be emitted. The end pass repeats the ORIGINAL physical
        // first-owner lookup of each root metadata handle, using only bounded
        // indexed metadata API reads (not direct TypeDef dereferences).
        // This is still an observation under initial Reader/g_MetadataLock,
        // not an atomic VM epoch or complete metadata handle-table proof.
        if(context.error==live::Error::None &&
           membership.EnumeratorHealthy() && membership.FullyMatched()) {
            struct EndPassFacts {
                physical::Scratch* membership=nullptr;
                const PackedFacts* facts=nullptr;
                const assembly_shadow_observed_images::Inventory<
                    live::kMaximumOriginalV1Images>* originalV1Images=nullptr;
                std::array<uint8_t,physical::kSlots> seen{};
                size_t enumerated=0,matched=0;
                size_t rootOwnerLookups=0,rootOwnerMatched=0;
                live::Error error=live::Error::None;
            } endPass{};
            endPass.membership=&membership;
            endPass.facts=&firstPassFacts;
            endPass.originalV1Images=&originalV1Images;
            il2cpp_class_for_each([](Il2CppClass* klass,void* raw) {
                EndPassFacts& end=*static_cast<EndPassFacts*>(raw);
                if(end.error!=live::Error::None)return;
                if(!klass || end.enumerated==physical::kMaximumEnumerated) {
                    end.error=live::Error::BadPhysicalEnumeration;return;
                }
                ++end.enumerated;
                size_t slot=physical::kSlots;
                assembly_shadow_execution_witness::Source witness=
                    assembly_shadow_execution_witness::Source::None;
                if(!end.membership->TryObservedRegistrationBinding(
                       klass,slot,witness))
                    return; // Other physical VM classes are not in V1 registry.
                if(slot>=physical::kSlots || end.seen[slot]) {
                    end.error=live::Error::BadPhysicalEnumeration;return;
                }
                end.seen[slot]=1;
                ++end.matched;
                const ClassFacts* previous=end.facts ?
                    end.facts->Find(slot):nullptr;
                if(!previous) {
                    end.error=live::Error::InvalidRegistry;return;
                }
                const ClassFacts observed=ClassFacts::Read(klass);
                if(!previous->SameRoot(observed)) {
                    end.error=live::Error::ObservedClassRootChanged;return;
                }
                if(!previous->SameClassState(observed)) {
                    end.error=live::Error::ObservedClassFactsChanged;return;
                }
                if(!klass->image || !klass->typeMetadataHandle ||
                   !end.originalV1Images) {
                    end.error=live::Error::InvalidTypeIdentity;return;
                }
                // An indexed TypeDef handle can change after we copied the
                // owned row, while the class pointer, typeCount and image
                // tokens remain unchanged. Recheck its ORIGINAL first owner.
                original_join::Receipt root{};
                if(!original_join::CheckFirstOwner(
                       *end.originalV1Images,klass->image,
                       klass->typeMetadataHandle,
                       [](const Il2CppImage* image,uint32_t index) {
                           return MetadataCache::GetAssemblyTypeHandle(image,index);
                       },end.rootOwnerLookups,
                       live::kMaximumTotalMetadataLookups,
                       live::kMaximumAotImageTypes,root)) {
                    if(root.error==original_join::Error::FirstOwnerDifferent)
                        end.error=live::Error::OriginalV1FirstOwnerConflict;
                    else if(root.error==original_join::Error::OwnerNotFound)
                        end.error=live::Error::OriginalV1OwnerMissing;
                    else if(root.error==original_join::Error::LookupBudget)
                        end.error=live::Error::OriginalV1LookupBudget;
                    else
                        end.error=live::Error::OriginalV1ImageSourceUnavailable;
                    return;
                }
                ++end.rootOwnerMatched;
            },&endPass);
            if(endPass.error!=live::Error::None)
                return reject(endPass.error);
            if(endPass.matched!=receipt.registered)
                return reject(live::Error::UnmatchedClass);
            receipt.observedClassFactsEndPassMatched=endPass.matched;
            receipt.originalV1RootOwnerEndPassMatched=endPass.rootOwnerMatched;
            receipt.originalV1RootOwnerEndPassLookups=endPass.rootOwnerLookups;
        }
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
        // Second original-V1 image-source snapshot, in its own assembly
        // lock pass. Only image *identities/order* are compared: no ABA,
        // metadata rewrite or class cctor/static transaction is proved.
        assembly_shadow_observed_images::Inventory<
            live::kMaximumOriginalV1Images> originalAfter;
        OriginalV1Sink originalAfterSink{};
        originalAfterSink.images=&originalAfter;
        originalAfterSink.firstFacts=firstOriginalV1Facts.data();
        originalAfterSink.compareFacts=true;
        if(!Assembly::TryVisitOriginalV1ExecutionImages(
               readOriginalV1,&originalAfterSink) ||
           originalAfterSink.visits!=originalSink.visits ||
           !originalAfter.SameOrderedImageSet(originalV1Images))
            return reject(originalAfterSink.error==live::Error::None ?
                live::Error::SourceChanged:originalAfterSink.error);
        receipt.originalV1ImageSetStable=true;
        receipt.originalV1ImageFactsMatched=originalAfterSink.visits;

        // No additional physical class traversal. AFTER the second original
        // image order/token observation, revisit all distinct non-root
        // TypeDef handles actually used to build the owned TypeKeys. The
        // previous third class pass only checked roots. Each pair's expected
        // owner is the independently authenticated ORIGINAL first owner.
        // A separate 65536 indexed-lookup limit fails closed on large source.
        size_t nonRootLookups=0,nonRootChecked=0,nonRootContentsChecked=0;
        for(size_t i=0;i<nonRootOwners.used;++i) {
            const auto& entry=nonRootOwners.pairs[i];
            original_join::Receipt checked{};
            if(!original_join::CheckFirstOwner(
                   originalV1Images,entry.image,entry.handle,
                   [](const Il2CppImage* image,uint32_t index) {
                       return MetadataCache::GetAssemblyTypeHandle(image,index);
                   },nonRootLookups,live::kMaximumTotalMetadataLookups,
                   live::kMaximumAotImageTypes,checked)) {
                if(checked.error==original_join::Error::FirstOwnerDifferent)
                    return reject(live::Error::OriginalV1FirstOwnerConflict);
                if(checked.error==original_join::Error::OwnerNotFound)
                    return reject(live::Error::OriginalV1OwnerMissing);
                if(checked.error==original_join::Error::LookupBudget)
                    return reject(live::Error::OriginalV1LookupBudget);
                return reject(live::Error::OriginalV1ImageSourceUnavailable);
            }
            ++nonRootChecked;
            // First-owner equality alone does not freeze a TypeDef's name,
            // generic arity or declaring-type metadata. Recheck every
            // previously copied non-root declaration before source release.
            const auto* def=reinterpret_cast<const Il2CppTypeDefinition*>(
                entry.handle);
            if(!entry.captured || !def ||
               static_cast<int32_t>(def->genericContainerIndex)!=entry.genericIndex ||
               static_cast<int32_t>(def->declaringTypeIndex)!=entry.declaringIndex)
                return reject(live::Error::OriginalV1TypeDefContentChanged);
            const auto name=GlobalMetadata::GetTypeNamespaceAndName(entry.handle);
            const char* ns=name.first?name.first:"";
            size_t nsBytes=0,nameBytes=0;
            if(!NonRootOwnerAudit::CountBounded(ns,nsBytes) ||
               !NonRootOwnerAudit::CountBounded(name.second,nameBytes))
                return reject(live::Error::OriginalV1TypeDefContentChanged);
            // Generic index is already verified BEFORE resolving container
            // count; never feed a newly changed/staged index to the API.
            const auto container=GlobalMetadata::GetGenericContainerFromIndex(
                def->genericContainerIndex);
            const uint32_t arity=GlobalMetadata::GetGenericContainerCount(container);
            if(!entry.Same(ns,nsBytes,name.second,nameBytes,
                           entry.genericIndex,entry.declaringIndex,arity))
                return reject(live::Error::OriginalV1TypeDefContentChanged);
            ++nonRootContentsChecked;
        }
        receipt.originalV1NonRootTypeDefContentMatched=nonRootContentsChecked;
        receipt.originalV1NonRootTypeDefOwnersRecorded=nonRootOwners.used;
        receipt.originalV1NonRootTypeDefOwnersRechecked=nonRootChecked;
        receipt.originalV1NonRootTypeDefOwnerLookups=nonRootLookups;
        nonRootOwners.Scrub();
        context.nonRootOwners=nullptr;
        firstOriginalV1Facts.fill(OriginalV1ImageFacts{});
        // Borrowed metadata pointers are scrubbed while g_MetadataLock is
        // still held. Nothing escapes to the frozen writer after this scope.
        context.images=nullptr;
        context.observedImages=nullptr;
        context.originalV1Images=nullptr;
        context.firstPassFacts=nullptr;
        // The first-pass per-class address VALUES are destroyed/scrubbed
        // before releasing original metadata/assembly source admission.
        firstPassFacts.Scrub();
    }

    receipt.metadataLookups=context.lookups;
    receipt.originalV1MetadataLookups=context.originalV1Lookups;
    receipt.originalV1HandlesMatched=context.originalV1Matches;
    receipt.observedClassFactsMatched=context.classFactsMatched;
    receipt.observedClassRootFactsMatched=context.classRootFactsMatched;
    receipt.rejectedUnsupported=context.rejected;
    receipt.usedCrossImageArgument=context.usedCrossImageArgument;
    receipt.usedUnobservedAotArgumentImage=context.usedUnobservedAotArgumentImage;
    receipt.usedUnicodeAssemblyFold=context.usedUnicodeAssemblyFold;
    receipt.usedExistingPrimitiveDefault=context.usedExistingPrimitiveDefault;
    receipt.usedSimpleInterpreterDefinition=context.usedSimpleInterpreterDefinition;
    receipt.admittedPrimitiveArguments=context.admittedPrimitiveArguments;
    receipt.usedNestedGenericArgument=context.usedNestedGenericArgument;
    receipt.usedPointerArgument=context.usedPointerArgument;
    receipt.usedSzArrayArgument=context.usedSzArrayArgument;
    receipt.usedMdArrayArgument=context.usedMdArrayArgument;
    receipt.usedQualifiedArgument=context.usedQualifiedArgument;
    receipt.compositeNodesVisited=context.compositeNodesVisited;
    receipt.maximumCompositeDepth=context.maximumCompositeDepth;
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
    receipt.observedClassFactsTwoPassStable=
        receipt.registered>0 &&
        receipt.observedClassFactsMatched==receipt.registered;
    receipt.observedClassFactsEndPassStable=
        receipt.observedClassFactsTwoPassStable &&
        receipt.observedClassFactsEndPassMatched==receipt.registered;
    receipt.observedClassRootFactsThreePassStable=
        receipt.observedClassFactsEndPassStable &&
        receipt.observedClassRootFactsMatched==receipt.registered;
    // Exact class root pointer/tag equality and cctor/static observations
    // across three passes are NOT a VM, GC/class lifetime or TypeDef epoch.
    receipt.originalV1ImageFactsTwoPassStable=
        receipt.originalV1ImageSetStable &&
        receipt.originalV1ImageFactsMatched==receipt.originalV1Images &&
        receipt.originalV1ImageFactsMatched>0;
    receipt.originalV1RootOwnerEndPassStable=
        receipt.originalV1ImageFactsTwoPassStable &&
        receipt.originalV1RootOwnerEndPassMatched==receipt.registered &&
        receipt.originalV1RootOwnerEndPassMatched>0 &&
        receipt.originalV1RootOwnerEndPassLookups>=
            receipt.originalV1RootOwnerEndPassMatched;
    receipt.originalV1NonRootTypeDefOwnersEndPassStable=
        receipt.originalV1ImageFactsTwoPassStable &&
        receipt.originalV1NonRootTypeDefOwnersRecorded==
            receipt.originalV1NonRootTypeDefOwnersRechecked &&
        receipt.originalV1NonRootTypeDefOwnersRecorded<=
            live::kMaximumNonRootTypeDefOwnerPairs &&
        receipt.originalV1NonRootTypeDefOwnerLookups>=
            receipt.originalV1NonRootTypeDefOwnersRechecked;
    receipt.originalV1NonRootTypeDefContentEndPassStable=
        receipt.originalV1NonRootTypeDefOwnersEndPassStable &&
        receipt.originalV1NonRootTypeDefContentMatched==
            receipt.originalV1NonRootTypeDefOwnersRecorded;
    receipt.originalV1FirstEmplaceOwnerMatched=
        receipt.originalV1RootOwnerEndPassStable &&
        receipt.originalV1NonRootTypeDefOwnersEndPassStable &&
        receipt.originalV1NonRootTypeDefContentEndPassStable &&
        receipt.originalV1HandlesMatched>0;
    // No claim of complete legacy execution field15, only those handles
    // actually used in the restricted rows. Other original V1 source rows,
    // cctor/static and global VM consistency remain unproven.
    receipt.observedRegistryAndCounterFenceStable=true;
    // Deliberately NOT a complete arbitrary TypeKey/field15/VM-epoch claim.
    return true;
}
