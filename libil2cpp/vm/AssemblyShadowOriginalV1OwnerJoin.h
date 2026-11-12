#pragma once
// R03 B2 INACTIVE: reconcile one physically authenticated AOT TypeDef handle
// against the ORIGINAL execution V1 metadata map's first-emplace image.
// Original V1 GetExecutionDiagnosticsJson builds MetadataTypeImages by:
//   CaptureShadowEnumeration (CopyValidAssemblies, token!=0), in that
//   physical order, across ALL images including interpreter; for each
//   image type definition index, images.emplace(handle,image).
// Thus the FIRST image whose indexed TypeDef == handle owns this key in V1.
// The caller must separately authenticate the expected AOT owner and borrow
// each image under g_MetadataLock + initial MetadataLifetimeGate Reader.
// This pure helper performs bounded opaque handle equality before ANY TypeDef
// dereference, retains no borrowed pointer, creates no classes/caches and
// never claims whole-VM atomicity, complete field15 or terminal V2 readiness.
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_original_v1_owner_join {

enum class Error : uint8_t {
    None, InvalidInput, MalformedImage, ImageBudget, LookupBudget,
    OwnerNotFound, FirstOwnerDifferent, Reused
};
struct Receipt {
    Error error=Error::None;
    size_t examinedImages=0, examinedHandles=0;
    size_t firstOwnerIndex=0;
    bool firstEmplaceOwnerMatched=false;
    bool entireOriginalV1Field15Proven=false;
    bool coherentVmEpochProven=false;
    bool terminalSafe=false;
    bool nativeV2TransportImplemented=false;
    bool runtimeAcceptance=false;
};
template<class Inventory,class Image,class Handle,class Read>
bool CheckFirstOwner(const Inventory& originalImages,
                     const Image* expectedAotImage,Handle handle,
                     Read read,size_t& aggregateLookups,
                     size_t maximumLookups,size_t maximumTypesPerImage,
                     Receipt& receipt) noexcept {
    receipt=Receipt{};
    if(!expectedAotImage || !handle || !originalImages.Healthy() ||
       !originalImages.Count() || !maximumLookups ||
       !maximumTypesPerImage || aggregateLookups>maximumLookups) {
        receipt.error=Error::InvalidInput;return false;
    }
    // Visit images in ORIGINAL CopyValidAssemblies order. A duplicate
    // metadata handle in a later image does not override an earlier emplace.
    for(size_t i=0;i<originalImages.Count();++i) {
        const Image* image=originalImages.template Get<Image>(i);
        if(!image || !image->assembly || !image->assembly->image ||
           image->assembly->image!=image) {
            receipt.error=Error::MalformedImage;return false;
        }
        if(image->typeCount>maximumTypesPerImage) {
            receipt.error=Error::ImageBudget;return false;
        }
        ++receipt.examinedImages;
        for(uint32_t j=0;j<image->typeCount;++j) {
            if(aggregateLookups==maximumLookups) {
                receipt.error=Error::LookupBudget;return false;
            }
            ++aggregateLookups;
            ++receipt.examinedHandles;
            if(read(image,j)==handle) {
                receipt.firstOwnerIndex=i;
                if(image!=expectedAotImage) {
                    receipt.error=Error::FirstOwnerDifferent;return false;
                }
                receipt.firstEmplaceOwnerMatched=true;
                return true;
            }
        }
    }
    receipt.error=Error::OwnerNotFound;return false;
}
}}} // namespaces
