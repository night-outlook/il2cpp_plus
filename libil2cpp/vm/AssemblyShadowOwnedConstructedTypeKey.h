#pragma once
// R03 B2 INACTIVE pure, fixed-storage canonical generic-instantiation key
// composer. It accepts only previously completed *owned* physical metadata
// definition keys and writes the exact old FormatType GENERICINST grammar:
// generic(<definition>,<byteLength>:<argument>,...)
// Source/owner/lifetime proof and type-shape admission belong to the VM caller.
// There is no heap, VM metadata, pointer retention, terminal or V2 authority.
#include "AssemblyShadowOwnedDefinitionTypeKey.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace r03_b2_owned_constructed_type_key {

enum class Error : uint8_t {
    None, Reused, InvalidDefinition, InvalidArity, InvalidArgument,
    TooManyArguments, WrongCount, Capacity, WrongStage, Revoked
};

class Capture final {
public:
    static constexpr size_t kCapacity=4096;
    static constexpr size_t kMaximumArguments=16;
private:
    enum class Stage : uint8_t { Fresh, Arguments, Complete, Failed };
    char bytes_[kCapacity+1]{};
    size_t used_=0, count_=0, expected_=0;
    Stage stage_=Stage::Fresh;
    Error first_=Error::None;

    bool Fail(Error e) noexcept {
        if(first_==Error::None)first_=e;
        stage_=Stage::Failed;
        std::memset(bytes_,0,sizeof bytes_);
        used_=count_=expected_=0;
        return false;
    }
    bool Put(const char* p,size_t n) noexcept {
        if(!p && n)return Fail(Error::InvalidArgument);
        if(n>kCapacity-used_)return Fail(Error::Capacity);
        if(n)std::memcpy(bytes_+used_,p,n);
        used_+=n;bytes_[used_]=0;return true;
    }
    bool Put(char c) noexcept{return Put(&c,1);}
    bool Decimal(size_t value) noexcept {
        char temp[3*sizeof(size_t)+1]{};
        size_t count=0;
        do {
            temp[count++]=static_cast<char>('0'+value%10);
            value/=10;
        } while(value);
        while(count)if(!Put(temp[--count]))return false;
        return true;
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Capture(Capture&&)=delete;
    Capture& operator=(Capture&&)=delete;
    Error FirstError() const noexcept{return first_;}
    bool Complete() const noexcept {
        return stage_==Stage::Complete && first_==Error::None;
    }
    size_t Bytes() const noexcept {return Complete()?used_:0;}
    const char* Data() const noexcept {return Complete()?bytes_:nullptr;}
    size_t Arguments() const noexcept {return Complete()?count_:0;}
    bool Begin(const r03_b2_owned_definition_key::Capture& definition,
               size_t count) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(!definition.Complete()||!definition.Data()||!definition.Bytes())
            return Fail(Error::InvalidDefinition);
        if(count==0||count>kMaximumArguments)return Fail(Error::InvalidArity);
        if(!Put("generic(",8)||!Put(definition.Data(),definition.Bytes()))
            return false;
        expected_=count;stage_=Stage::Arguments;return true;
    }
    bool AddArgument(const r03_b2_owned_definition_key::Capture& argument) noexcept {
        if(stage_!=Stage::Arguments)return Fail(Error::WrongStage);
        if(count_>=expected_)return Fail(Error::TooManyArguments);
        if(!argument.Complete()||!argument.Data()||!argument.Bytes())
            return Fail(Error::InvalidArgument);
        if(!Put(',')||!Decimal(argument.Bytes())||!Put(':')||
           !Put(argument.Data(),argument.Bytes()))return false;
        ++count_;return true;
    }
    bool Seal() noexcept {
        if(stage_!=Stage::Arguments)return Fail(Error::WrongStage);
        if(count_!=expected_)return Fail(Error::WrongCount);
        if(!Put(')'))return false;
        stage_=Stage::Complete;return true;
    }
    bool Revoke() noexcept{return Fail(Error::Revoked);}
};
static_assert(sizeof(Capture)<=4200,
              "R03 B2 generic type key exceeded the fixed-source budget");
} // namespace r03_b2_owned_constructed_type_key
