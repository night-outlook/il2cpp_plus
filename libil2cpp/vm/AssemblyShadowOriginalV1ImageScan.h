#pragma once
// R03 B2 INACTIVE. Mirror the ORIGINAL Assembly.cpp CopyValidAssemblies
// *membership and physical registration order*, not GetAllAssemblies' logical
// visibility remapping. The original V1 execution getter calls
// CaptureShadowEnumeration -> CopyValidAssemblies -> token != 0,
// then inserts every physical image's TypeDef handles into a first-wins map.
// Interpreter images and zero-TypeDef images MUST NOT be silently filtered.
// This visitor is bounded by the caller. It holds no source pointers, invokes
// no managed code and performs no heap allocations. Calling Assembly.cpp owns
// s_assemblyLock; the caller owns g_MetadataLock and initial Reader.
// Missing image, mismatched parent or invalid assembly fails closed, whereas
// the original allocating getter may crash on malformed source.
#include <cstddef>

namespace il2cpp { namespace vm { namespace assembly_shadow_original_v1_image_scan {

template<class Iterator,class Visit>
bool Scan(Iterator first,Iterator last,Visit visit) noexcept
{
    for(;first!=last;++first)
    {
        const auto* assembly=*first;
        if(!assembly)return false;
        if(!assembly->token)continue; // Exact CopyValidAssemblies eligibility.
        if(!assembly->image || !assembly->aname.name ||
           assembly->image->assembly!=assembly)return false;
        // Deliberately does not reject interpreter or typeCount==0.
        if(!visit(assembly->image))return false;
    }
    return true;
}

}}} // namespaces
