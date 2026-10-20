#pragma once
// R03 P1: execution-registry publisher, called only under ExecutionObservationLock.
// Register an opaque pointer, source witness, count and registry revision as
// one critical-section publication. No class dereference, heap or new lock.
// This establishes only registry insertion provenance, NOT VM pointer lifetime.
#include "AssemblyShadowExecutionRegistrationWitness.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace il2cpp { namespace vm { namespace assembly_shadow_execution_publisher {
enum class Result : uint8_t { Inserted, Existing, Full, WitnessRejected, RevisionExhausted, InvalidInput };

template<class Class, size_t Capacity, class Witnesses, class Revision>
inline Result Insert(std::array<Class*,Capacity>& slots, size_t& count,
                     size_t maximum, Witnesses& witnesses, Revision& revision,
                     Class* identity, assembly_shadow_execution_witness::Source source) noexcept
{
    static_assert(Capacity==assembly_shadow_execution_witness::kSlots,
                  "Registry and witness slot counts differ");
    if(!identity || (source!=assembly_shadow_execution_witness::Source::Candidate &&
                     source!=assembly_shadow_execution_witness::Source::ActiveShadow))
        return Result::InvalidInput;
    if(maximum==0 || maximum>Capacity || count>maximum)
        return Result::Full;
    size_t slot=(reinterpret_cast<uintptr_t>(identity)>>3)%Capacity;
    for(size_t probe=0; probe<Capacity; ++probe)
    {
        Class*& entry=slots[slot];
        if(entry==identity)return Result::Existing;
        if(!entry)
        {
            if(count==maximum)return Result::Full;
            // Saturated revisions cannot certify another insertion. Refuse
            // BEFORE writing the otherwise immutable source witness.
            if(revision.Current()==std::numeric_limits<uint64_t>::max())
                return Result::RevisionExhausted;
            if(!witnesses.Register(slot,identity,source))
                return Result::WitnessRejected;
            entry=identity;
            ++count;
            // The precheck above and exclusive lock make this advance succeed.
            revision.Advance();
            return Result::Inserted;
        }
        slot=(slot+1)%Capacity;
    }
    return Result::Full;
}
}}} // namespaces
