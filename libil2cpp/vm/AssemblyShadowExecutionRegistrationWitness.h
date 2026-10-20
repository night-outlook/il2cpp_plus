#pragma once
// R03 P1 INACTIVE source witness. Entries are recorded only by the existing
// ObserveExecutionClass registration path while ExecutionObservationLock is held.
// These are pointer VALUE identities, not safe-to-dereference metadata owners.
// A witness proves registration-path admission only, not pointer lifetime,
// shutdown quiescence, global epoch or Player/runtime acceptance.
#include <array>
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_execution_witness {
constexpr size_t kSlots=2048;
enum class Source : uint8_t { None=0, Candidate=1, ActiveShadow=2 };
class Table final
{
    struct Entry { const void* identity=nullptr; Source source=Source::None; };
    std::array<Entry,kSlots> slots_{};
public:
    Table() noexcept=default;
    bool Register(size_t slot,const void* identity,Source source) noexcept
    {
        if(slot>=kSlots || !identity ||
           (source!=Source::Candidate && source!=Source::ActiveShadow) ||
           slots_[slot].identity || slots_[slot].source!=Source::None)
            return false;
        slots_[slot]={identity,source};
        return true;
    }
    bool Matches(size_t slot,const void* identity,Source& result) const noexcept
    {
        result=Source::None;
        if(slot>=kSlots)return false;
        const Entry& current=slots_[slot];
        if(current.identity!=identity)return false;
        if(!identity)return current.source==Source::None;
        if(current.source!=Source::Candidate &&
           current.source!=Source::ActiveShadow)return false;
        result=current.source;
        return true;
    }
};
static_assert(sizeof(Table)<=40960u,"P1 witness table memory cap changed");
}}} // il2cpp::vm::assembly_shadow_execution_witness
