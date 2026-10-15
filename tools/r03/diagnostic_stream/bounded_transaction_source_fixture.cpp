// TEST ONLY: this file is mechanically combined with the *current production*
// TryCaptureBoundedTransactionStatus method in run_bounded_transaction_status.py.
// The same function body is compiled, but VM source objects are explicit mocks.
// This does not simulate Unity's scheduler or prove metadata/cache lifetimes.
#include "AssemblyShadowBoundedTransactionStatus.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern bool watch;
extern size_t newCalls;

namespace il2cpp { namespace vm {
namespace {
struct Staged {};
struct Closure { Staged* staged=nullptr; };
struct Transaction
{
    std::mutex mutex;
    AssemblyShadowError lastError=AssemblyShadowError::Success;
    std::string detail,baselineBuildId,patchId;
    std::vector<Closure> closure;
    uint64_t retainedBytes=0;
};
struct TypeFailure { AssemblyShadowError error; std::string detail; };
struct ActiveSnapshot { uint64_t generation=1; };

Transaction transactionSource;
TypeFailure firstFailureSource{AssemblyShadowError::ReferenceResolutionFailed,"first guard error"};
ActiveSnapshot activeSource{};
std::atomic<const TypeFailure*> s_typeFailure{nullptr};
std::atomic<const ActiveSnapshot*> s_active{nullptr};
std::atomic<AssemblyShadowState> s_state{AssemblyShadowState::Committed};
std::atomic<bool> s_unexpectedFailure{false};
std::atomic<bool> s_lateBaselineUse{false};
int mutationMode=0,generationCalls=0;

Transaction& Current(){return transactionSource;}
size_t StagedCount(const Transaction& tx)
{
    size_t result=0;
    for (const auto& item:tx.closure) if(item.staged) ++result;
    return result;
}
} // deliberately matches the production anonymous-namespace source

class AssemblyShadow
{
public:
    static bool TryCaptureBoundedTransactionStatus(assembly_shadow_bounded_transaction::Capture&);
    static uint64_t ActiveGeneration()
    {
        ++generationCalls;
        if (generationCalls==2)
        {
            if (mutationMode==1)
                s_typeFailure.store(&firstFailureSource,std::memory_order_release);
            if (mutationMode==2)
                s_state.store(AssemblyShadowState::FailedAfterCommit,std::memory_order_release);
            if (mutationMode==3)
                s_active.store(&activeSource,std::memory_order_release);
            if (mutationMode==4)
                s_lateBaselineUse.store(true,std::memory_order_release);
        }
        const ActiveSnapshot* active=s_active.load(std::memory_order_acquire);
        return active ? active->generation:0;
    }
};

// __PRODUCTION_CAPTURE_FUNCTION_IS_INSERTED_HERE__

static void Setup()
{
    watch=false;
    mutationMode=0;generationCalls=0;
    s_state.store(AssemblyShadowState::Committed);
    s_typeFailure.store(nullptr);
    s_active.store(nullptr);
    s_unexpectedFailure.store(false);
    s_lateBaselineUse.store(false);
    transactionSource.lastError=AssemblyShadowError::MetadataCapacityExceeded;
    transactionSource.detail=std::string("error \0 with control\n中文",27);
    transactionSource.baselineBuildId="base-123";
    transactionSource.patchId="patch-456";
    transactionSource.closure.clear();
    transactionSource.closure.resize(3);
    static Staged staged;
    transactionSource.closure[0].staged=&staged;
    transactionSource.closure[2].staged=&staged;
    transactionSource.retainedBytes=UINT64_MAX;
    activeSource.generation=31;
    firstFailureSource.error=AssemblyShadowError::ReferenceResolutionFailed;
    firstFailureSource.detail="immutable type failure";
}
using B=assembly_shadow_bounded_transaction::Capture;
using E=assembly_shadow_bounded_transaction::Error;
using V=assembly_shadow_bounded_transaction::View;

static std::unique_ptr<B> Acquire()
{
    auto output=std::make_unique<B>();
    newCalls=0; watch=true;
    bool success=AssemblyShadow::TryCaptureBoundedTransactionStatus(*output);
    watch=false;
    assert(success && output->Complete() && output->FirstError()==E::None);
    assert(newCalls==0); // excludes allocator internals and real VM lifecycle
    return output;
}
static std::string ToString(assembly_shadow_diagnostic::QuotedText p)
{
    assert(p.data);return {p.data,p.bytes};
}
static void AssertSourceUntouched(const V& p)
{
    assert(ToString(p.baselineBuildId)=="base-123");
    assert(ToString(p.patchId)=="patch-456");
    assert(p.expected==3 && p.staged==2 && p.retainedBytes==UINT64_MAX);
    assert(p.generation==0);
}
static void Baseline()
{
    Setup();
    const auto frozen=Acquire();
    V p;assert(frozen->Get(p));
    assert(p.state==AssemblyShadowState::Committed);
    assert(p.lastError==AssemblyShadowError::MetadataCapacityExceeded);
    AssertSourceUntouched(p);
    const std::string original=ToString(p.detail);
    assert(original==transactionSource.detail);
    transactionSource.detail.assign("destroyed");
    transactionSource.baselineBuildId.assign("destroyed");
    transactionSource.patchId.assign("destroyed");
    assert(ToString(p.detail)==original && ToString(p.baselineBuildId)=="base-123");
    assert(ToString(p.patchId)=="patch-456");
}
static void Priority()
{
    Setup();s_lateBaselineUse.store(true);
    {
        const auto c=Acquire();V p;assert(c->Get(p));
        assert(p.lastError==AssemblyShadowError::BaselineAlreadyUsed);
        assert(ToString(p.detail)=="Physical baseline use after publication; restart required.");
    }
    Setup();s_lateBaselineUse.store(true);s_unexpectedFailure.store(true);
    {
        const auto c=Acquire();V p;assert(c->Get(p));
        assert(p.lastError==AssemblyShadowError::InternalError);
        assert(ToString(p.detail)=="Unexpected native failure sealed the transaction; restart required.");
    }
    Setup();s_unexpectedFailure.store(true);s_typeFailure.store(&firstFailureSource);
    {
        const auto c=Acquire();V p;assert(c->Get(p));
        assert(p.state==AssemblyShadowState::Failed);
        assert(p.lastError==AssemblyShadowError::ReferenceResolutionFailed);
        assert(ToString(p.detail)=="immutable type failure");
        AssertSourceUntouched(p);
        firstFailureSource.detail="mutated original after capture";
        assert(ToString(p.detail)=="immutable type failure");
    }
    Setup();s_typeFailure.store(&firstFailureSource);s_active.store(&activeSource);
    {
        const auto c=Acquire();V p;assert(c->Get(p));
        assert(p.state==AssemblyShadowState::FailedAfterCommit);
        assert(p.generation==31);
        assert(p.lastError==AssemblyShadowError::ReferenceResolutionFailed);
        assert(ToString(p.detail)=="immutable type failure");
    }
}
static void EpochAndLifetimeFailures()
{
    for(int mode=1;mode<=4;++mode)
    {
        Setup();mutationMode=mode;
        auto c=std::make_unique<B>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionStatus(*c));
        if (c->FirstError()!=E::SourceChanged || c->Complete())
            std::cerr<<"unexpected capture error, mutationMode="<<mode
                     <<" error="<<unsigned(c->FirstError())
                     <<" completed="<<c->Complete()
                     <<" active="<<(s_active.load()!=nullptr)
                     <<" typeFailure="<<(s_typeFailure.load()!=nullptr)
                     <<" state="<<int(s_state.load())
                     <<" generationCalls="<<generationCalls<<'\n';
        assert(c->FirstError()==E::SourceChanged && !c->Complete());
        V v;assert(!c->Get(v));
    }
    Setup();
    transactionSource.detail.assign(B::kMaxDetail+1,'X');
    {
        auto c=std::make_unique<B>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionStatus(*c));
        assert(c->FirstError()==E::DetailLimit && !c->Complete());
    }
    Setup();
    transactionSource.baselineBuildId.assign(B::kMaxBuildId+1,'X');
    {
        auto c=std::make_unique<B>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionStatus(*c));
        assert(c->FirstError()==E::BuildIdLimit && !c->Complete());
    }
    Setup();
    transactionSource.patchId.assign(B::kMaxPatchId+1,'X');
    {
        auto c=std::make_unique<B>();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionStatus(*c));
        assert(c->FirstError()==E::PatchIdLimit && !c->Complete());
    }
    Setup();
    {
        auto c=Acquire();
        assert(!AssemblyShadow::TryCaptureBoundedTransactionStatus(*c));
        assert(c->FirstError()==E::WrongPhase && !c->Complete());
    }
}
static void DirectPhaseControl()
{
    Setup();
    auto c=std::make_unique<B>();
    V v;assert(!c->Get(v));assert(!c->Seal());
    assert(c->FirstError()==E::WrongPhase);
    B second;
    assembly_shadow_bounded_transaction::Input i;
    i.detail=assembly_shadow_diagnostic::Quoted("d");
    i.baselineBuildId=assembly_shadow_diagnostic::Quoted("b");
    i.patchId=assembly_shadow_diagnostic::Quoted("p");
    assert(second.Begin(i));
    assert(!second.Seal()); // first-failure observation is mandatory
    assert(second.FirstError()==E::WrongPhase && !second.Complete());
}
}} // il2cpp::vm

int main()
{
    il2cpp::vm::Baseline();
    il2cpp::vm::Priority();
    il2cpp::vm::EpochAndLifetimeFailures();
    il2cpp::vm::DirectPhaseControl();
    std::cout<<"P1 actual VM status method under mocks: precedence/owned bytes/"
             <<"source flips/limits/one-shot/allocation controls passed\n";
}
