#pragma once
// R03 P1 INACTIVE: bounded observed-pointer membership in the physical class
// enumerator. No registered class address is dereferenced or returned.
// Enumerator membership is NOT allocation provenance, metadata lifetime,
// a coherent VM epoch, or authorization to read class fields/type keys.
#include "AssemblyShadowExecutionRegistrationWitness.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_physical_membership {
static constexpr size_t kSlots=2048;
static constexpr size_t kMaximumObserved=1024;
static constexpr size_t kMaximumEnumerated=65536;
enum class Error:uint8_t {
    None, ReusedScratch, AdmissionUnavailable, InvalidRegistry,
    DuplicateObservedIdentity, EnumeratorIncomplete, UnmatchedObservedIdentity,
    SourceChanged
};
enum class ObservationEvent:uint8_t {Ignored,FirstMatch,DuplicateMatch,Invalid};
class Scratch final
{
    std::array<const void*,kSlots> identities_{};
    std::array<uint8_t,kSlots> roles_{};
    std::array<uint8_t,kSlots> seen_{};
    size_t count_=0, matched_=0, enumerated_=0;
    uint64_t revision_=0;
    bool consumed_=false, captured_=false, badEnumeration_=false;
public:
    Scratch() noexcept=default;
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
    bool Consume() noexcept {if(consumed_)return false;consumed_=true;return true;}
    bool Consumed() const noexcept{return consumed_;}
    template<class Pointer,class Witnesses>
    bool Capture(const Pointer* slots,size_t length,size_t claimed,
                 uint64_t revision,const Witnesses& witnesses) noexcept
    {
        if(!consumed_||captured_||!slots||length!=kSlots||
           claimed>kMaximumObserved||
           revision==std::numeric_limits<uint64_t>::max())return false;
        captured_=true;
        revision_=revision;
        for(size_t i=0;i<kSlots;++i)
        {
            const void* address=slots[i];  // copy opaque value ONLY
            assembly_shadow_execution_witness::Source role{};
            if(!witnesses.Matches(i,address,role))return false;
            identities_[i]=address;
            roles_[i]=static_cast<uint8_t>(role);
            if(address)++count_;
        }
        return count_==claimed;
    }
    bool Unique() const noexcept
    {
        if(!captured_)return false;
        for(size_t i=0;i<kSlots;++i)
        {
            const void* p=identities_[i];
            if(!p)continue;
            for(size_t j=i+1;j<kSlots;++j)
                if(identities_[j]==p)return false;
        }
        return true;
    }
    // Classify a synchronous iterator callback without reading any class
    // metadata. FirstMatch is the sole point at which a separate owned-name
    // consumer may borrow fields while g_MetadataLock is held.
    ObservationEvent ObserveEnumeratedOwned(const void* address) noexcept
    {
        if(badEnumeration_)return ObservationEvent::Invalid;
        if(!address||enumerated_==kMaximumEnumerated)
        {
            badEnumeration_=true;return ObservationEvent::Invalid;
        }
        ++enumerated_;
        size_t slot=(reinterpret_cast<uintptr_t>(address)>>3)%kSlots;
        for(size_t probe=0;probe<kSlots;++probe)
        {
            const void* current=identities_[slot];
            if(!current)return ObservationEvent::Ignored;
            if(current==address)
            {
                if(!seen_[slot])
                {
                    seen_[slot]=1;++matched_;
                    return ObservationEvent::FirstMatch;
                }
                return ObservationEvent::DuplicateMatch;
            }
            slot=(slot+1)%kSlots;
        }
        badEnumeration_=true;
        return ObservationEvent::Invalid;
    }
    // Preserve the original opaque-membership API and counting semantics.
    void ObserveEnumerated(const void* address) noexcept
    {
        (void)ObserveEnumeratedOwned(address);
    }
    // Read only the opaque slot and registered source of a class ALREADY
    // identified as FirstMatch in the synchronous physical enumeration.
    // No class metadata is touched and no pointer is returned. The slot is
    // scoped to this registry snapshot, not a persistent class identity.
    bool TryObservedRegistrationBinding(const void* address,size_t& slotOut,
        assembly_shadow_execution_witness::Source& sourceOut) const noexcept
    {
        slotOut=kSlots;
        sourceOut=assembly_shadow_execution_witness::Source::None;
        if(!captured_||badEnumeration_||!address)return false;
        size_t slot=(reinterpret_cast<uintptr_t>(address)>>3)%kSlots;
        for(size_t probe=0;probe<kSlots;++probe)
        {
            const void* current=identities_[slot];
            if(!current)return false;
            if(current==address)
            {
                if(!seen_[slot])return false;
                const auto role=static_cast<assembly_shadow_execution_witness::Source>(roles_[slot]);
                if(role!=assembly_shadow_execution_witness::Source::Candidate &&
                   role!=assembly_shadow_execution_witness::Source::ActiveShadow)
                    return false;
                slotOut=slot;
                sourceOut=role;
                return true;
            }
            slot=(slot+1)%kSlots;
        }
        return false;
    }
    template<class Pointer,class Witnesses>
    bool SameRegistry(const Pointer* slots,size_t length,size_t claimed,
                      uint64_t revision,const Witnesses& witnesses) const noexcept
    {
        if(!captured_||!slots||length!=kSlots||claimed!=count_||
           revision!=revision_||revision==std::numeric_limits<uint64_t>::max())return false;
        for(size_t i=0;i<kSlots;++i)
        {
            const void* address=slots[i];
            assembly_shadow_execution_witness::Source role{};
            if(identities_[i]!=address||!witnesses.Matches(i,address,role)||
               roles_[i]!=static_cast<uint8_t>(role))return false;
        }
        return true;
    }
    bool EnumeratorHealthy() const noexcept{return !badEnumeration_;}
    bool FullyMatched() const noexcept{return captured_&&!badEnumeration_&&matched_==count_;}
    size_t Observed() const noexcept{return captured_?count_:0;}
    size_t Matched() const noexcept{return matched_;}
    size_t Enumerated() const noexcept{return enumerated_;}
    uint64_t Revision() const noexcept{return captured_?revision_:0;}
    void Scrub() noexcept
    {
        for(auto& p:identities_)p=nullptr;
        roles_.fill(0);seen_.fill(0);
        count_=matched_=enumerated_=0;revision_=0;
        captured_=false;badEnumeration_=false;
    }
};
struct Receipt
{
    Error error=Error::None;
    size_t observedClasses=0,enumeratedClasses=0,matchedClasses=0;
    uint64_t registryRevisionBefore=0,registryRevisionAfter=0;
    bool enumeratorInvoked=false,enumeratedMembershipComplete=false;
    // ALWAYS false, including after complete enumeration.
    bool physicalClassPointerOriginProven=false;
    bool metadataPointerLifetimeProven=false;
    bool coherentVmEpochProven=false;
    bool executionJsonComplete=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
inline bool Fail(Scratch& scratch,Receipt& receipt,Error reason) noexcept
{
    if(receipt.error==Error::None)receipt.error=reason;
    receipt.enumeratedMembershipComplete=false;
    scratch.Scrub();return false;
}
static_assert(sizeof(Scratch)<=24576u,"Physical membership scratch bound changed");
}}} // namespaces
