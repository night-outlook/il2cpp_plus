#pragma once
// R03 P1: process-local execution observation registry revision.
// Mutations and reads must be protected by ExecutionObservationLock.
// NOT a coherent VM-wide epoch or proof of class pointer lifetime.
#include <cstdint>
#include <limits>
namespace il2cpp { namespace vm { namespace assembly_shadow_execution_registry {
class Revision final
{
    uint64_t value_=0;
public:
    Revision() noexcept=default;
    explicit constexpr Revision(uint64_t initial) noexcept:value_(initial){}
    uint64_t Current() const noexcept{return value_;}
    // Saturates permanently. An opaque capture must reject MAX.
    bool Advance() noexcept
    {
        if(value_==std::numeric_limits<uint64_t>::max())return false;
        ++value_;
        return true;
    }
};
}}} // namespaces
