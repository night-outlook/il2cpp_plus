#pragma once
// R03 B2: test-only, owned, fixed-budget execution-row staging.
// No VM pointer, borrowed source string or metadata handle is retained.
// A complete Rows object proves owned bytes and ordered rows ONLY. It does
// NOT prove the data came from an authentic, live Il2CppClass, an actual
// register witness or a correct canonical metadata TypeKey.
#include "AssemblyShadowOwnedDefinitionTypeKey.h"
#include "AssemblyShadowOwnedConstructedTypeKey.h"
#include "AssemblyShadowOwnedCompositeTypeKey.h"
#include <type_traits>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace r03_b2_owned_execution_rows {
using r03_b2_owned_definition_key::ByteView;
using r03_b2_owned_definition_key::Capture;

static constexpr size_t kMaximumRows=1024;
static constexpr size_t kMaximumRegistrationSlots=2048;
static constexpr size_t kMaximumTextBytes=262144;
enum class Error:uint8_t {
    None, Reused, InvalidCount, InvalidSlot, DuplicateSlot,
    IncompleteDefinitionKey, MissingText, InvalidUtf8, AliasedInput,
    TextCapacity, CountMismatch, NotComplete, UnsupportedRole
};
enum class Source:uint8_t { Candidate=1, ActiveShadow=2 };

struct Flags {
    bool interpreterShadow=false;
    bool physicalInterpreter=false;
    bool isActive=false;
    bool cctorStarted=false;
    bool cctorFinished=false;
    bool hasInitializationException=false;
    bool pointerDetailsAvailable=false;
    bool staticStorageAvailable=false;
};
struct RowView {
    ByteView logicalAssembly{}, typeKey{}, staticStoragePointer{};
    size_t registrySlot=0;
    Source source=Source::Candidate;
    Flags fields{};
};

class Rows final {
    struct Span {uint32_t offset=0,length=0;};
    struct Entry {
        Span assembly{}, typeKey{}, pointer{};
        uint16_t slot=0;
        Source source=Source::Candidate;
        Flags fields{};
    };
    enum class Stage:uint8_t { Fresh, Collecting, Frozen, Failed };
    std::array<Entry,kMaximumRows> entries_{};
    std::array<uint8_t,kMaximumRegistrationSlots> slots_{};
    std::array<char,kMaximumTextBytes> text_{};
    size_t textUsed_=0, count_=0, expected_=0;
    Error first_=Error::None;
    Stage stage_=Stage::Fresh;

    bool Fail(Error reason) noexcept {
        if(first_==Error::None)first_=reason;
        stage_=Stage::Failed;
        // Poison and scrub every owned byte on an incomplete source.
        entries_.fill(Entry{});
        slots_.fill(0);text_.fill(0);
        textUsed_=count_=expected_=0;
        return false;
    }
    bool Aliases(ByteView view) const noexcept {
        if(!view.data||!view.size)return false;
        const uintptr_t b=reinterpret_cast<uintptr_t>(this);
        const uintptr_t p=reinterpret_cast<uintptr_t>(view.data);
        return sizeof(*this)>UINTPTR_MAX-b ||
            view.size>UINTPTR_MAX-p ||
            (p<b+sizeof(*this) && b<p+view.size);
    }
    static bool Utf8(ByteView x) noexcept {
        if(!x.data && x.size)return false;
        for(size_t i=0;i<x.size;) {
            uint32_t cp=static_cast<uint8_t>(x.data[i++]);unsigned n=0;
            uint32_t minimum=0;
            if(cp<0x20||cp==0x7f)return false;
            if(cp<0x80)continue;
            if(cp>=0xc2&&cp<=0xdf){n=1;minimum=0x80;cp&=31;}
            else if(cp>=0xe0&&cp<=0xef){n=2;minimum=0x800;cp&=15;}
            else if(cp>=0xf0&&cp<=0xf4){n=3;minimum=0x10000;cp&=7;}
            else return false;
            if(x.size-i<n)return false;
            for(unsigned j=0;j<n;++j){
                const uint8_t ch=static_cast<uint8_t>(x.data[i++]);
                if((ch&0xc0)!=0x80)return false;
                cp=(cp<<6)|(ch&63);
            }
            if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
        }
        return true;
    }
    bool Copy(ByteView src,Span& dst) noexcept {
        if((!src.data&&src.size))return Fail(Error::MissingText);
        if(Aliases(src))return Fail(Error::AliasedInput);
        if(!Utf8(src))return Fail(Error::InvalidUtf8);
        if(src.size>kMaximumTextBytes-textUsed_)return Fail(Error::TextCapacity);
        dst.offset=static_cast<uint32_t>(textUsed_);
        dst.length=static_cast<uint32_t>(src.size);
        if(src.size)std::memcpy(text_.data()+textUsed_,src.data,src.size);
        textUsed_+=src.size;
        return true;
    }
    ByteView SpanView(Span span) const noexcept {
        return ByteView{text_.data()+span.offset,span.length};
    }
public:
    Rows() noexcept=default;
    Rows(const Rows&)=delete;
    Rows& operator=(const Rows&)=delete;
    bool Complete() const noexcept {return stage_==Stage::Frozen&&first_==Error::None;}
    Error FirstError() const noexcept{return first_;}
    size_t Count() const noexcept{return Complete()?count_:0;}
    size_t TextBytes() const noexcept{return Complete()?textUsed_:0;}
    bool Begin(size_t expected) noexcept {
        if(stage_!=Stage::Fresh)return Fail(Error::Reused);
        if(expected>kMaximumRows)return Fail(Error::InvalidCount);
        expected_=expected;stage_=Stage::Collecting;return true;
    }
    // Must be called synchronously while each authenticated owned TypeKey
    // and original class-field primitive is still valid. Registration Source
    // here is a copied role; external VM proof is mandatory.
    template<class TrustedKey>
    bool Add(size_t registrationSlot,Source source,ByteView logicalAssembly,
             const TrustedKey& immutableTypeKey,ByteView staticPointer,
             const Flags& fields) noexcept {
        static_assert(
            std::is_same<TrustedKey,::r03_b2_owned_definition_key::Capture>::value ||
            std::is_same<TrustedKey,::r03_b2_owned_constructed_type_key::Capture>::value,
            std::is_same<TrustedKey,::r03_b2_owned_composite_type_key::Capture>::value ||
            "Only canonical owned definition/constructed TypeKeys may enter rows");
        if(stage_!=Stage::Collecting)return Fail(Error::NotComplete);
        if(count_>=expected_)return Fail(Error::CountMismatch);
        if(registrationSlot>=kMaximumRegistrationSlots)return Fail(Error::InvalidSlot);
        if(slots_[registrationSlot])return Fail(Error::DuplicateSlot);
        if(source!=Source::Candidate && source!=Source::ActiveShadow)
            return Fail(Error::UnsupportedRole);
        if(!logicalAssembly.data||!logicalAssembly.size)return Fail(Error::MissingText);
        if(!immutableTypeKey.Complete()||!immutableTypeKey.Data()||
           !immutableTypeKey.Bytes())return Fail(Error::IncompleteDefinitionKey);
        Entry& item=entries_[count_];
        if(!Copy(logicalAssembly,item.assembly) ||
           !Copy(ByteView{immutableTypeKey.Data(),immutableTypeKey.Bytes()},item.typeKey)||
           !Copy(staticPointer,item.pointer))return false;
        item.slot=static_cast<uint16_t>(registrationSlot);
        item.source=source;
        item.fields=fields;
        slots_[registrationSlot]=1;++count_;return true;
    }
    bool Seal() noexcept {
        if(stage_!=Stage::Collecting || count_!=expected_)
            return Fail(Error::CountMismatch);
        // Original execution getter iterates registry slots in ascending
        // order. Physical callback discovery may arrive in arbitrary order.
        // Sort only owned primitives after all source accesses have ended.
        for(size_t i=1;i<count_;++i) {
            Entry row=entries_[i];size_t j=i;
            while(j>0 && entries_[j-1].slot>row.slot){
                entries_[j]=entries_[j-1];--j;
            }
            entries_[j]=row;
        }
        stage_=Stage::Frozen;return true;
    }
    bool Get(size_t index,RowView& out) const noexcept {
        out=RowView{};
        if(!Complete()||index>=count_)return false;
        const Entry& row=entries_[index];
        out.logicalAssembly=SpanView(row.assembly);
        out.typeKey=SpanView(row.typeKey);
        out.staticStoragePointer=SpanView(row.pointer);
        out.registrySlot=row.slot;
        out.source=row.source;
        out.fields=row.fields;
        return true;
    }
    bool Revoke() noexcept{return Fail(Error::NotComplete);}
};
static_assert(sizeof(Rows)<=320u*1024u,"Bounded owned execution rows memory increased");
} // namespace r03_b2_owned_execution_rows
