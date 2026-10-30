#pragma once
// R03 P1 INACTIVE: pure owned-source -> frozen general-V1 byte composition.
// This does not sample VM state, establish an all-VM epoch, hold a lock, make a
// managed call, open a file, or make a terminal/Player admission decision.
//
// Inputs MUST already have completed their individual bounded source captures
// under their respective owners. The startup names must remain valid only
// during Compose; they are copied synchronously into the frozen destination.
// Caller preallocates destination and every source capture while healthy.
// No input pointer is retained in the output. Scope: source-component parity,
// NOT authenticated global coherence or live post-terminal scheduling.
#include "AssemblyShadowFrozenGeneralCapture.h"
#include "AssemblyShadowBoundedTransactionInventory.h"
#include "AssemblyShadowBoundedFirstUseCapture.h"
#include "AssemblyShadowBoundedPhysicalAssemblies.h"
#include "AssemblyShadowBoundedOrdinaryClasses.h"
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_bounded_general {

namespace frozen=assembly_shadow_frozen_general;
namespace inventory=assembly_shadow_bounded_inventory;
namespace first_use=assembly_shadow_bounded_first_use;
namespace physical=assembly_shadow_bounded_physical;
namespace classes=assembly_shadow_bounded_classes;
using assembly_shadow_diagnostic::QuotedText;

enum class Error : uint8_t
{
    None, InvalidInput, IncompleteSource, GenerationMismatch,
    MissingOwnedRow, OutputFailed
};

struct Inputs
{
    frozen::Header header{};
    const QuotedText* startupCandidateNames=nullptr;
    size_t startupCandidateCount=0;
    const inventory::Capture* transaction=nullptr;
    const first_use::Capture* firstUses=nullptr;
    const physical::Capture* ordinaryAssemblies=nullptr;
    const classes::Capture* ordinaryClasses=nullptr;
};
struct Receipt
{
    Error error=Error::None;
    size_t serializedBytes=0, sourceTextBytes=0;
    size_t startupNames=0, transactionStable=0, transactionClosure=0;
    size_t transactionCommit=0, transactionEvents=0;
    size_t baselineUses=0, ordinaryAssemblies=0, ordinaryClasses=0;
    uint64_t ordinaryGeneration=0, usageGeneration=0;
    // Immutable scope markers: the sources were read under independent locks.
    // Matching ActiveGeneration is NOT proof of a coherent all-VM epoch.
    bool componentSourcesComplete=false;
    bool coherentVmEpochProven=false;
    bool runtimeAcceptance=false;
};

static constexpr size_t kMaxStartupNames=4096;
static constexpr size_t kMaxTotalOwnedObjectBytes=
    frozen::Capture::kMaximumObjectBytes +
    inventory::Capture::kMaximumObjectBytes +
    first_use::Capture::kMaximumObjectBytes +
    physical::Capture::kMaxObjectBytes +
    classes::Capture::kMaximumObjectBytes;
static_assert(kMaxTotalOwnedObjectBytes<=16u*1024u*1024u,
              "P1 general composition fixed owned-memory budget changed");

inline bool Fail(Receipt& receipt,Error error,frozen::Capture* destination=nullptr) noexcept
{
    if(receipt.error==Error::None)receipt.error=error;
    receipt.serializedBytes=0;
    receipt.componentSourcesComplete=false;
    if(destination)destination->RejectInvalidSource();
    return false;
}

// Operates on an already owned source collection only. The V1 JSON field
// sequence and values must remain byte-identical to
// AssemblyShadowDiagnostics::Serialize(ShadowDiagnosticSnapshot).
// No failure may leave a completed/usable destination or success receipt.
inline bool Compose(const Inputs& input,frozen::Capture& output,Receipt& receipt) noexcept
{
    receipt=Receipt{};
    if(!input.transaction||!input.firstUses||!input.ordinaryAssemblies||
       !input.ordinaryClasses||
       (input.startupCandidateCount && !input.startupCandidateNames)||
       input.startupCandidateCount>kMaxStartupNames)
        return Fail(receipt,Error::InvalidInput,&output);

    const auto& transaction=*input.transaction;
    const auto& uses=*input.firstUses;
    const auto& assemblies=*input.ordinaryAssemblies;
    const auto& typeRows=*input.ordinaryClasses;
    if(!transaction.Complete() || !uses.Complete() ||
       !assemblies.Complete() || !typeRows.Complete())
        return Fail(receipt,Error::IncompleteSource,&output);
    // Both real bounded enumeration readers sample AssemblyShadow::ActiveGeneration.
    // A mismatch is a witnessed inconsistency, not an opportunity to format
    // one legacy-compatible but mixed-generation record.
    if(assemblies.EnumerationGeneration()!=typeRows.ClassEnumerationGeneration())
        return Fail(receipt,Error::GenerationMismatch,&output);
    assembly_shadow_bounded_transaction::View status{};
    if(!transaction.GetStatus(status))
        return Fail(receipt,Error::MissingOwnedRow,&output);
    receipt.sourceTextBytes=transaction.TextBytesCaptured() +
        uses.CapturedTextBytes() + assemblies.OwnedTextBytes() +
        typeRows.OwnedTextBytes();

    if(!output.Begin(input.header))
        return Fail(receipt,Error::OutputFailed,&output);
    for(size_t i=0;i<input.startupCandidateCount;++i)
        if(!output.AddStartupCandidate(input.startupCandidateNames[i]))
            return Fail(receipt,Error::OutputFailed,&output);
    // The destination types use default member initializers; construct then
    // populate explicitly for the Unity C++11 native compiler.
    frozen::Transaction frozenStatus{};
    frozenStatus.state=status.state;
    frozenStatus.lastError=status.lastError;
    frozenStatus.detail=status.detail;
    frozenStatus.baselineBuildId=status.baselineBuildId;
    frozenStatus.patchId=status.patchId;
    frozenStatus.generation=status.generation;
    frozenStatus.retainedBytes=status.retainedBytes;
    frozenStatus.expected=status.expected;
    frozenStatus.staged=status.staged;
    if(!output.EndStartupCandidates(frozenStatus))
        return Fail(receipt,Error::OutputFailed,&output);

    // Legacy serializer outputs closureLoadOrder BEFORE stableAotNames.
    for(size_t i=0;i<transaction.ObservedClosure();++i)
    {
        inventory::AssemblyView row{};
        if(!transaction.GetClosure(i,row))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        if(!output.AddClosureName(row.name))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.EndClosure())
        return Fail(receipt,Error::OutputFailed,&output);
    for(size_t i=0;i<transaction.ObservedStableNames();++i)
    {
        QuotedText name{};
        if(!transaction.GetStable(i,name))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        if(!output.AddStableAotName(name))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.EndStableAot())
        return Fail(receipt,Error::OutputFailed,&output);
    for(size_t i=0;i<transaction.ObservedCommitNames();++i)
    {
        QuotedText name{};
        if(!transaction.GetCommit(i,name))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        if(!output.AddCommitOrderName(name))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.EndCommitOrder())
        return Fail(receipt,Error::OutputFailed,&output);

    for(size_t i=0;i<transaction.ObservedClosure();++i)
    {
        inventory::AssemblyView row{};
        if(!transaction.GetClosure(i,row))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        frozen::AssemblyRow owned{};
        owned.name=row.name;
        owned.mvid=row.mvid;
        owned.skeletonBuilt=row.skeletonBuilt;
        owned.runtimeMetadataInitialized=row.runtimeMetadataInitialized;
        owned.published=row.published;
        owned.moduleInitializerAttempted=row.moduleInitializerAttempted;
        owned.moduleInitializerRan=row.moduleInitializerRan;
        if(!output.AddAssembly(owned))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.EndAssemblies())
        return Fail(receipt,Error::OutputFailed,&output);
    for(size_t i=0;i<transaction.ObservedEvents();++i)
    {
        inventory::EventView row{};
        if(!transaction.GetEvent(i,row))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        frozen::EventRow owned{};
        owned.sequence=row.sequence;
        owned.generation=row.generation;
        owned.stagedCount=row.stagedCount;
        owned.kind=row.kind;
        owned.name=row.name;
        if(!output.AddEvent(owned))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.EndEvents())
        return Fail(receipt,Error::OutputFailed,&output);

    size_t emittedUses=0;
    for(size_t i=0;i<uses.SourceCandidateCount();++i)
    {
        first_use::Row row{};
        if(!uses.Get(i,row))continue; // Missing first-use is no JSON row.
        frozen::UseRow owned{};
        owned.name=row.name;
        owned.detail=row.detail;
        owned.type=row.type;
        owned.kind=row.kind;
        owned.thread=row.thread;
        owned.timestamp=row.timestamp;
        if(!output.AddBaselineUse(owned))
            return Fail(receipt,Error::OutputFailed,&output);
        ++emittedUses;
    }
    // A complete source must neither invent nor silently drop any used row.
    if(emittedUses!=uses.PresentCount())
        return Fail(receipt,Error::MissingOwnedRow,&output);
    if(!output.EndBaselineUses(assemblies.EnumerationGeneration()))
        return Fail(receipt,Error::OutputFailed,&output);

    for(size_t i=0;i<assemblies.Count();++i)
    {
        physical::Row row{};
        if(!assemblies.Get(i,row))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        frozen::OrdinaryAssemblyRow owned{};
        owned.name=QuotedText{row.name,row.bytes};
        owned.isInterpreter=row.interpreter;
        if(!output.AddOrdinaryAssembly(owned))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.EndOrdinaryAssemblies(typeRows.ClassEnumerationGeneration()))
        return Fail(receipt,Error::OutputFailed,&output);
    for(size_t i=0;i<typeRows.Count();++i)
    {
        classes::ClassView row{};
        if(!typeRows.Get(i,row))
            return Fail(receipt,Error::MissingOwnedRow,&output);
        frozen::OrdinaryClassRow owned{};
        owned.assemblyName=QuotedText{row.assemblyName,row.assemblyNameBytes};
        owned.typeName=QuotedText{row.typeName,row.typeNameBytes};
        owned.isInterpreter=row.isInterpreter;
        owned.isConstructedGeneric=row.isConstructedGeneric;
        owned.usesStagedMetadata=row.usesStagedMetadata;
        if(!output.AddOrdinaryClass(owned))
            return Fail(receipt,Error::OutputFailed,&output);
    }
    if(!output.Freeze())
        return Fail(receipt,Error::OutputFailed,&output);

    receipt.serializedBytes=output.Bytes();
    receipt.startupNames=input.startupCandidateCount;
    receipt.transactionStable=transaction.ObservedStableNames();
    receipt.transactionClosure=transaction.ObservedClosure();
    receipt.transactionCommit=transaction.ObservedCommitNames();
    receipt.transactionEvents=transaction.ObservedEvents();
    receipt.baselineUses=emittedUses;
    receipt.ordinaryAssemblies=assemblies.Count();
    receipt.ordinaryClasses=typeRows.Count();
    receipt.ordinaryGeneration=assemblies.EnumerationGeneration();
    receipt.usageGeneration=uses.CapturedUsageGeneration();
    receipt.componentSourcesComplete=true;
    return true;
}
}}} // il2cpp::vm::assembly_shadow_bounded_general
