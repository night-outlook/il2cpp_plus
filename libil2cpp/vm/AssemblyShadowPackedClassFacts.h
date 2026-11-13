#pragma once
// R03 B2 INACTIVE: exact class-fact storage for a sparse 2048-slot registry
// with at most 1024 physical observed identities. Caller owns this scratch.
// One-based slot indices map to densely stored copied primitive facts.
// No class pointers escape, no allocation, no metadata lifetime/epoch proof.
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace il2cpp { namespace vm { namespace assembly_shadow_packed_class_facts {
template<class Fact,size_t Slots,size_t Capacity>
class Scratch final {
    static_assert(Slots>0 && Capacity>0 && Capacity<=Slots &&
                  Capacity<=65534u,"Invalid B2 packed class bounds");
    static_assert(std::is_trivially_copyable<Fact>::value,
                  "Packed class facts must be trivially copyable");
    std::array<Fact,Capacity> facts_{};
    std::array<uint16_t,Slots> slotToIndexPlusOne_{};
    size_t used_=0;
public:
    Scratch() noexcept=default;
    ~Scratch() noexcept {Scrub();} // Also scrub every early source rejection.
    Scratch(const Scratch&)=delete;
    Scratch& operator=(const Scratch&)=delete;
    Scratch(Scratch&&)=delete;
    Scratch& operator=(Scratch&&)=delete;
    bool Insert(size_t slot,const Fact& fact) noexcept {
        if(slot>=Slots || slotToIndexPlusOne_[slot] ||
           used_==Capacity)return false;
        facts_[used_]=fact;
        slotToIndexPlusOne_[slot]=static_cast<uint16_t>(used_+1);
        ++used_;
        return true;
    }
    const Fact* Find(size_t slot) const noexcept {
        if(slot>=Slots)return nullptr;
        const uint16_t index=slotToIndexPlusOne_[slot];
        return index && static_cast<size_t>(index)<=used_
            ? &facts_[static_cast<size_t>(index)-1] : nullptr;
    }
    bool Update(size_t slot,const Fact& fact) noexcept {
        if(slot>=Slots)return false;
        const uint16_t index=slotToIndexPlusOne_[slot];
        if(!index || static_cast<size_t>(index)>used_)return false;
        facts_[static_cast<size_t>(index)-1]=fact;
        return true;
    }
    size_t Count() const noexcept {return used_;}
    void Scrub() noexcept {
        facts_.fill(Fact{});
        slotToIndexPlusOne_.fill(0);
        used_=0;
    }
};
}}} // il2cpp::vm::assembly_shadow_packed_class_facts
