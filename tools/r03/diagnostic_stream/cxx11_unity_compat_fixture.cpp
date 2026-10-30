// Header-only source check for the actual C++11 language level used by Unity
// 2022.3.62f2. Not an SDK integration test or evidence of runtime acceptance.
#include "AssemblyShadowBoundedGeneralComposer.h"
#include "AssemblyShadowFrozenExecutionRecord.h"
#include "AssemblyShadowExecutionRegistrationWitness.h"
#include "AssemblyShadowOwnedPhysicalNames.h"
#include "AssemblyShadowBoundedGeneralLive.h"
#include "AssemblyShadowBoundedExecutionEmptyLive.h"
#include <type_traits>

namespace dw=il2cpp::vm::assembly_shadow_diagnostic;
namespace general=il2cpp::vm::assembly_shadow_frozen_general;
namespace exec=il2cpp::vm::assembly_shadow_frozen_execution_record;
namespace registry=il2cpp::vm::assembly_shadow_execution_witness;
namespace names=il2cpp::vm::assembly_shadow_owned_physical_names;

static bool Receive(void*,const char*,size_t) noexcept { return true; }
static_assert(std::is_same<decltype(dw::WriteResult{}.bytesAccepted),size_t>::value,
              "Existing bounded writer result contract changed");
static_assert(sizeof(general::Transaction)>sizeof(uint64_t),
              "Typed transaction source removed");
static_assert(exec::Record::kMaximumObjectBytes<=4u*1024u*1024u,
              "Execution record budget changed");
static_assert(registry::kSlots==2048,"Registration witness source changed");
static_assert(names::kMaximumRows==1024,"Owned physical name budget changed");

void CompileActualCxx11TypesOnly()
{
    dw::Sink sink{nullptr,&Receive};
    dw::Writer writer(sink,64);
    writer<<"ok";
    const dw::WriteResult result=writer.Finish();
    (void)result;
    const dw::WriteResult failure{dw::WriteError::InvalidInput,0,false};
    (void)failure;
    general::Transaction status{};
    status.generation=1;
    general::AssemblyRow assembly{};
    assembly.moduleInitializerRan=false;
    general::EventRow event{};
    event.sequence=1;
    general::UseRow use{};
    use.thread=2;
    general::OrdinaryAssemblyRow physical{};
    physical.isInterpreter=false;
    general::OrdinaryClassRow klass{};
    klass.isConstructedGeneric=false;
    (void)status;(void)assembly;(void)event;(void)use;(void)physical;(void)klass;
}
