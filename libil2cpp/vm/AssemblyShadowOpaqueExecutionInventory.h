#pragma once
// R03 P1 INACTIVE: bounded opaque execution-observation census only.
// No class metadata is read; no raw class pointer escapes. This is not a
// pointer provenance proof, coherent VM epoch or complete execution JSON.
#include <array>
#include <cstddef>
#include <cstdint>
namespace il2cpp { namespace vm { namespace assembly_shadow_opaque_execution {
static constexpr size_t kSlotCount=2048;
static constexpr size_t kMaximumClasses=1024;
enum class Error:uint8_t { None,ReusedScratch,AdmissionUnavailable,
    InvalidRegistry,DuplicateIdentity,SourceChanged };
class Snapshot final {
    std::array<const void*,kSlotCount> slots_{};
    size_t count_=0;
    bool populated_=false;
public:
    Snapshot() noexcept=default;
    Snapshot(const Snapshot&)=delete;
    Snapshot& operator=(const Snapshot&)=delete;
    template<class Pointer>
    bool Populate(const Pointer* table,size_t length,size_t claimed) noexcept {
        if(populated_||!table||length!=kSlotCount||claimed>kMaximumClasses)return false;
        populated_=true;
        for(size_t i=0;i<kSlotCount;++i){
            slots_[i]=table[i];
            if(slots_[i])++count_;
        }
        return count_==claimed;
    }
    bool Unique() const noexcept {
        if(!populated_)return false;
        // Bounded worst-case comparisons after releasing source lock.
        for(size_t i=0;i<kSlotCount;++i) {
            const void* current=slots_[i];
            if(!current)continue;
            for(size_t j=i+1;j<kSlotCount;++j)
                if(slots_[j]==current)return false;
        }
        return true;
    }
    bool SameAs(const Snapshot& b) const noexcept {
        if(!populated_||!b.populated_||count_!=b.count_)return false;
        for(size_t i=0;i<kSlotCount;++i)
            if(slots_[i]!=b.slots_[i])return false;
        return true;
    }
    size_t Count() const noexcept{return populated_?count_:0;}
    bool Populated() const noexcept{return populated_;}
    void Clear() noexcept {
        for(auto& slot:slots_)slot=nullptr;
        count_=0;populated_=false;
    }
};
class Scratch final {
    Snapshot before_,after_;
    bool consumed_=false;
public:
    Scratch() noexcept=default;
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
    bool Consume() noexcept{if(consumed_)return false;consumed_=true;return true;}
    Snapshot& Before() noexcept{return before_;}
    Snapshot& After() noexcept{return after_;}
    void Scrub() noexcept{before_.Clear();after_.Clear();}
    bool Consumed() const noexcept{return consumed_;}
};
struct Receipt {
    Error error=Error::None;
    size_t countBefore=0,countAfter=0,completedSourceSamples=0;
    uint64_t generationBefore=0,generationAfter=0;
    bool opaqueInventoryObserved=false;
    bool exactOpaqueSlotSamplesEqual=false;
    bool physicalClassPointerOriginProven=false;
    bool coherentVmEpochProven=false;
    bool executionJsonComplete=false;
    bool playerExecution=false;
    bool runtimeAcceptance=false;
};
inline bool Fail(Scratch& scratch,Receipt& receipt,Error error) noexcept {
    if(receipt.error==Error::None)receipt.error=error;
    receipt.opaqueInventoryObserved=false;
    receipt.exactOpaqueSlotSamplesEqual=false;
    scratch.Scrub();
    return false;
}
static_assert(sizeof(Scratch)<=33000u,"R03 opaque scratch bound changed");
}}} // namespaces
