// HOST ONLY: exact current native entry methods are inserted at the markers.
// The Gate is production code. Registries, metadata locks, capture destinations
// and the class callback are explicit mocks. Existing class/physical suites
// separately test the real capture buffers and V1 serializers.
#include "AssemblyShadowMetadataLifetime.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

struct Il2CppImage { bool interpreter=false; };
struct Il2CppAssembly
{
    uint32_t token=1;
    const Il2CppImage* image=nullptr;
    struct {const char* name=nullptr;} aname;
};
struct Il2CppClass {};
namespace il2cpp { namespace vm {
namespace assembly_shadow_metadata_lifetime { Gate g_MetadataLifetimeGate; }
static bool sourceMustNotRun=true;
static unsigned sourceCallbacks=0;
struct MockLock { unsigned entries=0; bool held=false; };
static MockLock s_assemblyLock,g_MetadataLock;
struct MockCapture
{
    bool ownerDenied=false,started=false,complete=false;
    unsigned rows=0;
    bool Begin() {started=true;return true;}
    bool Add(const char*,bool) {++rows;return true;}
    bool Seal(uint64_t,bool stable) {complete=stable;return stable;}
    bool RejectSource() {complete=false;return false;}
    bool RejectOwnerUnavailable() {ownerDenied=true;complete=false;return false;}
};
namespace assembly_shadow_bounded_classes {using Capture=MockCapture;}
namespace assembly_shadow_bounded_physical {using Capture=MockCapture;}
static std::vector<const Il2CppAssembly*> s_Assemblies;
struct PrivateImage {};
static std::atomic<const PrivateImage*> s_privateImages{nullptr};
class AssemblyShadow
{
public: static uint64_t ActiveGeneration() {assert(!sourceMustNotRun);return 7;}
};
class Assembly
{
public: static bool TryCaptureBoundedPhysicalAssemblies(
    assembly_shadow_bounded_physical::Capture&) noexcept;
};
class AssemblyShadowVisibility
{
public: static bool TryCaptureBoundedOrdinaryClasses(
    assembly_shadow_bounded_classes::Capture&);
};
static void CaptureOwnedOrdinaryClass(Il2CppClass*,void* context)
{
    assert(!sourceMustNotRun && g_MetadataLock.held);
    ++static_cast<MockCapture*>(context)->rows;
}
static void il2cpp_class_for_each(void (*callback)(Il2CppClass*,void*),void* context)
{
    assert(!sourceMustNotRun && g_MetadataLock.held);
    ++sourceCallbacks;
    Il2CppClass klass;
    callback(&klass,context);
}
}}
namespace il2cpp { namespace os {
class FastAutoLock
{
    vm::MockLock* lock_;
public:
    explicit FastAutoLock(vm::MockLock* lock):lock_(lock)
    {
        assert(!vm::sourceMustNotRun && !lock_->held);
        assert(vm::assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==1);
        ++lock_->entries;lock_->held=true;
    }
    ~FastAutoLock()
    {
        assert(vm::assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate.ReaderCount()==1);
        lock_->held=false;
    }
};
}}
namespace hybridclr { namespace metadata {
static bool IsInterpreterImage(const Il2CppImage* image)
{
    assert(!il2cpp::vm::sourceMustNotRun && il2cpp::vm::s_assemblyLock.held);
    return image->interpreter;
}
}}
namespace il2cpp { namespace vm {
// __EXACT_PHYSICAL_METHOD__
// __EXACT_CLASS_METHOD__

static void BothDenied()
{
    auto& gate=assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate;
    const auto readers=gate.ReaderCount();
    const auto classLocks=g_MetadataLock.entries,assemblyLocks=s_assemblyLock.entries;
    const auto callbacks=sourceCallbacks;
    MockCapture physical,classes;
    assert(!Assembly::TryCaptureBoundedPhysicalAssemblies(physical));
    assert(!AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(classes));
    assert(physical.ownerDenied && !physical.started && !physical.complete && physical.rows==0);
    assert(classes.ownerDenied && !classes.started && !classes.complete && classes.rows==0);
    assert(g_MetadataLock.entries==classLocks && s_assemblyLock.entries==assemblyLocks);
    assert(sourceCallbacks==callbacks && gate.ReaderCount()==readers);
}
static void ExerciseActualEntryMethods()
{
    auto& gate=assembly_shadow_metadata_lifetime::g_MetadataLifetimeGate;
    BothDenied(); // cold: no metadata source or lock is entered
    assert(gate.OpenAfterInit());
    sourceMustNotRun=false;
    Il2CppImage image{true};
    Il2CppAssembly assembly{1,&image,{"Physical"}};
    s_Assemblies.push_back(&assembly);
    MockCapture physical,classes;
    assert(Assembly::TryCaptureBoundedPhysicalAssemblies(physical));
    assert(AssemblyShadowVisibility::TryCaptureBoundedOrdinaryClasses(classes));
    assert(physical.complete && classes.complete && physical.rows==1 && classes.rows==1);
    assert(g_MetadataLock.entries==1 && s_assemblyLock.entries==1 && sourceCallbacks==1);
    assert(gate.ReaderCount()==0);
    gate.CloseAndDrain();
    sourceMustNotRun=true;
    // An unqualified retained pointer would be invalid after source teardown.
    // The reopened admission check must fail BEFORE even the source locks.
    s_Assemblies[0]=reinterpret_cast<const Il2CppAssembly*>(uintptr_t(1));
    BothDenied();
    assert(gate.OpenAfterInit()); // runtime epoch 2 is open, not merely closed
    BothDenied();
    {
        assembly_shadow_metadata_lifetime::Gate::Reader separatelyStamped(gate,2);
        assert(separatelyStamped && separatelyStamped.Epoch()==2);
        BothDenied(); // a different active ticket cannot lend source provenance
    }
    gate.CloseAndDrain();
    assert(gate.OpenAfterInit());
    BothDenied(); // epoch 3 is also not provenance for retained first-run data
    gate.CloseAndDrain();
    std::cout<<"actual bounded entry methods HOST PASS: initial epoch only; cold/closed/"
                "reopened sources denied before locks and callbacks; no ambient-ticket borrowing\n";
}
}}
int main() {il2cpp::vm::ExerciseActualEntryMethods();}
