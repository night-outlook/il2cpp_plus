#pragma once
// R03 B2 INACTIVE: bounded borrowed-image inventory, populated only from
// physically matched observed-class callbacks under the metadata lock and
// initial-session Reader. It does NOT authenticate an arbitrary image address.
// Lifetime: the entire object must remain inside the same metadata-lock scope.
// The destructor scrubs all image pointers; never expose this inventory to a
// Player, a transaction thread, or a postterminal source.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace il2cpp { namespace vm { namespace assembly_shadow_observed_images {
enum class Error : uint8_t { None, MissingImage, Capacity, AlreadyFailed };
template<size_t Capacity=64>
class Inventory final {
    static_assert(Capacity>0 && Capacity<=1024, "Bounded observed images only");
    const void* entries_[Capacity]{};
    size_t used_=0;
    Error first_=Error::None;
    bool Fail(Error e) noexcept {
        if(first_==Error::None)first_=e;
        Scrub();return false;
    }
public:
    Inventory() noexcept=default;
    ~Inventory() noexcept{Scrub();}
    Inventory(const Inventory&)=delete;
    Inventory& operator=(const Inventory&)=delete;
    bool AddFromMatchedCallback(const void* image) noexcept {
        if(first_!=Error::None)return false;
        if(!image)return Fail(Error::MissingImage);
        for(size_t i=0;i<used_;++i)
            if(entries_[i]==image)return true;
        if(used_==Capacity)return Fail(Error::Capacity);
        entries_[used_++]=image;return true;
    }
    template<class Image>
    const Image* Get(size_t index) const noexcept {
        return first_==Error::None && index<used_
            ? static_cast<const Image*>(entries_[index]):nullptr;
    }
    size_t Count() const noexcept{return first_==Error::None?used_:0;}
    Error FirstError() const noexcept{return first_;}
    bool Healthy() const noexcept{return first_==Error::None;}
    void Scrub() noexcept {
        std::memset(entries_,0,sizeof entries_);used_=0;
    }
};
static_assert(sizeof(Inventory<64>)<=600,"Physical image inventory size grew");
}}} // namespaces
