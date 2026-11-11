#pragma once
// R03 B2 INACTIVE. Pure C++11 bounded physical assembly-table visitor.
// The REAL Assembly.cpp call holds s_assemblyLock. Callers of that API must
// also hold g_MetadataLock + the initial metadata Reader. This visitor may
// pass a borrowed Il2CppImage* only to a synchronous, nonthrowing consumer.
// It never materializes the logical/Shadow assembly snapshot, calls Load,
// initializes a class, allocates, retains a pointer, or claims a VM epoch.
#include <cstddef>

namespace il2cpp { namespace vm { namespace assembly_shadow_registered_aot_scan {

template<class Iterator,class IsInterpreter,class Visit>
bool Scan(Iterator first,Iterator last,IsInterpreter isInterpreter,
          Visit visit) noexcept {
    for(;first!=last;++first) {
        const auto* assembly=*first;
        if(!assembly)return false;
        if(!assembly->token)continue; // Same validity rule as physical enumeration.
        if(!assembly->image || !assembly->aname.name ||
           assembly->image->assembly!=assembly)return false;
        const auto* image=assembly->image;
        if(isInterpreter(image))continue;
        if(!image->typeCount || !visit(image))return false;
    }
    return true;
}

}}} // namespaces
