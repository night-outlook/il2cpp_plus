#pragma once
// R03 B2 INACTIVE. Pure C++11 bounded, allocation-free canonical TypeKey
// composition for owned TypeDef leaves, GENERICINST, SZARRAY, PTR, ARRAY
// and the exact pinned Il2CppType qualifier syntax. No VM type is read here.
// Source ownership, recursion/metadata budgets and lifetime belong to caller.
// Completing a key does NOT prove complete V1 diagnostics or V2 field15.
#include "AssemblyShadowOwnedDefinitionTypeKey.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace r03_b2_owned_composite_type_key {
namespace definition = ::r03_b2_owned_definition_key;

enum class Error : uint8_t {
    None, Reused, InvalidChild, InvalidArgumentCount, TooManyArguments,
    WrongCount, InvalidArray, Capacity, WrongStage, AliasedInput, Revoked
};

class Capture final {
public:
    static constexpr size_t kCapacity=4096;
    static constexpr size_t kMaximumArguments=16;
    static constexpr size_t kMaximumArrayDimensions=32;
private:
    enum class Stage : uint8_t { Fresh, Arguments, Complete, Failed };
    char bytes_[kCapacity+1]{};
    size_t used_=0,expected_=0,received_=0;
    Stage stage_=Stage::Fresh;
    Error first_=Error::None;

    bool Fail(Error error) noexcept {
        if(first_==Error::None)first_=error;
        stage_=Stage::Failed;
        std::memset(bytes_,0,sizeof bytes_);
        used_=expected_=received_=0;
        return false;
    }
    bool Put(const char* p,size_t n) noexcept {
        if(!p && n)return Fail(Error::InvalidChild);
        if(n>kCapacity-used_)return Fail(Error::Capacity);
        if(n)std::memcpy(bytes_+used_,p,n);
        used_+=n;bytes_[used_]=0;
        return true;
    }
    bool Put(char c) noexcept { return Put(&c,1); }
    bool Number(uint64_t n) noexcept {
        char digits[24]{};size_t count=0;
        do {digits[count++]=static_cast<char>('0'+n%10);n/=10;}while(n);
        while(count)if(!Put(digits[--count]))return false;
        return true;
    }
    bool Signed(int64_t n) noexcept {
        if(n<0) {
            // Negating a negative signed value must not overflow.
            return Put('-')&&Number(static_cast<uint64_t>(-(n+1))+1);
        }
        return Number(static_cast<uint64_t>(n));
    }
    bool Child(const Capture& value) noexcept {
        if(&value==this)return Fail(Error::AliasedInput);
        if(!value.Complete()||!value.Data()||!value.Bytes())
            return Fail(Error::InvalidChild);
        return Put(value.Data(),value.Bytes());
    }
    bool Definition(const definition::Capture& value) noexcept {
        if(!value.Complete()||!value.Data()||!value.Bytes())
            return Fail(Error::InvalidChild);
        return Put(value.Data(),value.Bytes());
    }
    bool Unary(const char* prefix,size_t prefixBytes,
               const Capture& child) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(&child==this)return Fail(Error::AliasedInput);
        if(!Put(prefix,prefixBytes)||!Child(child)||!Put(')'))return false;
        stage_=Stage::Complete;
        return true;
    }
    bool ValidateArray(const Capture& element,const int* sizes,size_t ns,
                       const int* bounds,size_t nb,uint8_t rank) noexcept {
        if(&element==this)return Fail(Error::AliasedInput);
        if(!element.Complete())return Fail(Error::InvalidChild);
        if(!rank||ns>kMaximumArrayDimensions||
           nb>kMaximumArrayDimensions||(ns&&!sizes)||(nb&&!bounds))
            return Fail(Error::InvalidArray);
        return true;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;

    Error FirstError() const noexcept {return first_;}
    bool Complete() const noexcept {
        return stage_==Stage::Complete&&first_==Error::None;
    }
    const char* Data() const noexcept {return Complete()?bytes_:nullptr;}
    size_t Bytes() const noexcept {return Complete()?used_:0;}
    size_t Arguments() const noexcept {return Complete()?received_:0;}

    bool FromDefinition(const definition::Capture& key) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(!Definition(key))return false;
        stage_=Stage::Complete;return true;
    }
    bool BeginGeneric(const definition::Capture& genericDefinition,
                      size_t arguments) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(!arguments||arguments>kMaximumArguments)
            return Fail(Error::InvalidArgumentCount);
        if(!Put("generic(",8)||!Definition(genericDefinition))return false;
        expected_=arguments;stage_=Stage::Arguments;
        return true;
    }
    bool AddGenericArgument(const Capture& key) noexcept {
        if(stage_!=Stage::Arguments)return Fail(Error::WrongStage);
        if(received_>=expected_)return Fail(Error::TooManyArguments);
        if(&key==this)return Fail(Error::AliasedInput);
        if(!key.Complete()||!key.Bytes())return Fail(Error::InvalidChild);
        if(!Put(',')||!Number(key.Bytes())||!Put(':')||!Child(key))
            return false;
        ++received_;return true;
    }
    bool SealGeneric() noexcept {
        if(stage_!=Stage::Arguments)return Fail(Error::WrongStage);
        if(received_!=expected_)return Fail(Error::WrongCount);
        if(!Put(')'))return false;
        stage_=Stage::Complete;return true;
    }
    bool FromPointer(const Capture& target) noexcept {
        return Unary("ptr(",4,target);
    }
    bool FromSzArray(const Capture& element) noexcept {
        return Unary("szarray(",8,element);
    }
    // ARRAY matches the original FormatType spelling exactly, including the
    // comma after each dimension and "sizes=" followed by "bounds=".
    bool FromArray(const Capture& element,uint8_t rank,
                   const int* sizes,size_t numSizes,
                   const int* lowerBounds,size_t numLowerBounds) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(!ValidateArray(element,sizes,numSizes,
                          lowerBounds,numLowerBounds,rank))return false;
        if(!Put("array(",6)||!Number(rank)||!Put(',')||
           !Number(element.Bytes())||!Put(':')||!Child(element)||
           !Put(",sizes=",7))return false;
        for(size_t i=0;i<numSizes;++i)
            if(!Signed(sizes[i])||!Put(','))return false;
        if(!Put("bounds=",7))return false;
        for(size_t i=0;i<numLowerBounds;++i)
            if(!Signed(lowerBounds[i])||!Put(','))return false;
        if(!Put(')'))return false;
        stage_=Stage::Complete;return true;
    }
    bool FromQualified(const Capture& value,uint32_t attrs,
                       uint32_t numMods,uint32_t byref,
                       uint32_t pinned) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(&value==this)return Fail(Error::AliasedInput);
        if(!Put("qualified(",10)||!Number(attrs)||!Put(',')||
           !Number(numMods)||!Put(',')||!Number(byref)||!Put(',')||
           !Number(pinned)||!Put(',')||!Child(value)||!Put(')'))
            return false;
        stage_=Stage::Complete;return true;
    }
    bool Revoke() noexcept {return Fail(Error::Revoked);}
};
static_assert(sizeof(Capture)<=4200,
              "R03 B2 owned composite TypeKey exceeded fixed-source budget");
} // namespace r03_b2_owned_composite_type_key
