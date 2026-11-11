#pragma once
// R03 B2: test-only pure bounded TypeKey byte builder, C++11.
// Matches ShadowTypeKey::ToString grammar for supported owned metadata
// definitions. Physical source provenance MUST be verified by a separate
// real VM reader. This formatter retains no metadata pointer or string view.
// It is not a nonempty execution diagnostics producer or V2 transport.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace r03_b2_owned_definition_key {
enum class Error : uint8_t {
    None, Reused, MissingSource, InvalidAssembly, NonAsciiAssembly,
    InvalidUtf8, InvalidDeclaration, Alias, TooDeep, Capacity,
    UnsupportedShape, NotComplete
};
struct ByteView { const char* data; size_t size; };

class Capture final {
public:
    static constexpr size_t kCapacity = 4096;
    static constexpr size_t kMaximumDeclarations = 128;
private:
    enum class State : uint8_t { Fresh, Open, Complete, Failed };
    char bytes_[kCapacity+1]{};
    size_t used_=0, declarations_=0;
    State state_=State::Fresh;
    Error first_=Error::None;
    bool Fail(Error why) noexcept {
        if(first_==Error::None)first_=why;
        state_=State::Failed;
        std::memset(bytes_,0,sizeof bytes_);
        used_=declarations_=0;
        return false;
    }
    bool Aliases(ByteView x) const noexcept {
        if(!x.data || !x.size)return false;
        const uintptr_t p=reinterpret_cast<uintptr_t>(x.data);
        const uintptr_t b=reinterpret_cast<uintptr_t>(this);
        return x.size>UINTPTR_MAX-p || sizeof(*this)>UINTPTR_MAX-b ||
            (p<b+sizeof(*this) && b<p+x.size);
    }
    static bool Utf8(ByteView x) noexcept {
        if(!x.data && x.size)return false;
        for(size_t i=0;i<x.size;) {
            uint32_t cp=static_cast<unsigned char>(x.data[i++]);
            uint32_t min=0;unsigned n=0;
            if(cp<0x20 || cp==0x7f)return false;
            if(cp<0x80)continue;
            if(cp>=0xc2 && cp<=0xdf){n=1;min=0x80;cp&=31;}
            else if(cp>=0xe0 && cp<=0xef){n=2;min=0x800;cp&=15;}
            else if(cp>=0xf0 && cp<=0xf4){n=3;min=0x10000;cp&=7;}
            else return false;
            if(x.size-i<n)return false;
            for(unsigned j=0;j<n;++j){
                const uint8_t c=static_cast<uint8_t>(x.data[i++]);
                if((c&0xc0)!=0x80)return false;
                cp=(cp<<6)|(c&63);
            }
            if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
        }
        return true;
    }
    bool Put(const char* p,size_t n) noexcept {
        if(!p && n)return Fail(Error::MissingSource);
        if(n>kCapacity-used_)return Fail(Error::Capacity);
        if(n)std::memcpy(bytes_+used_,p,n);
        used_+=n;bytes_[used_]=0;return true;
    }
    bool Put(char c) noexcept{return Put(&c,1);}
    bool Number(uint64_t n) noexcept {
        char ds[24]{};size_t k=0;
        do{ds[k++]=static_cast<char>('0'+n%10);n/=10;}while(n);
        while(k)if(!Put(ds[--k]))return false;
        return true;
    }
    bool Part(ByteView x) noexcept {
        return Number(x.size)&&Put(':')&&Put(x.data,x.size);
    }
    static char Lower(char c) noexcept {
        return c>='A'&&c<='Z'?static_cast<char>(c+('a'-'A')):c;
    }
    static bool Extension(const char* p,size_t n) noexcept {
        if(n<4 || p[n-4]!='.')return false;
        const char a=Lower(p[n-3]),b=Lower(p[n-2]),c=Lower(p[n-1]);
        return (a=='d'&&b=='l'&&c=='l')||(a=='e'&&b=='x'&&c=='e');
    }
public:
    Capture() noexcept=default;
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Error FirstError() const noexcept{return first_;}
    bool Complete() const noexcept{return state_==State::Complete&&first_==Error::None;}
    size_t Bytes() const noexcept{return Complete()?used_:0;}
    const char* Data() const noexcept{return Complete()?bytes_:nullptr;}
    size_t Declarations() const noexcept{return Complete()?declarations_:0;}

    // True flags are caller assertions, not authentication. The VM adapter
    // must bind them to real physical TypeDef origin and metadata Reader.
    // Only ASCII assemblies are supported; the real Unicode UTF16 case fold
    // is required before non-ASCII could be claimed byte-equivalent.
    bool Begin(ByteView rawAssembly,bool physicalMetadataDefinition,
               bool hasTypeQualifiers,bool isConstructedGeneric) noexcept {
        if(state_!=State::Fresh)return Fail(Error::Reused);
        if(!physicalMetadataDefinition||hasTypeQualifiers||isConstructedGeneric)
            return Fail(Error::UnsupportedShape);
        if(!rawAssembly.data||!rawAssembly.size)return Fail(Error::MissingSource);
        if(Aliases(rawAssembly))return Fail(Error::Alias);
        size_t end=0;
        while(end<rawAssembly.size&&rawAssembly.data[end]!=',')++end;
        size_t start=0;
        for(size_t i=0;i<end;++i)
            if(rawAssembly.data[i]=='/'||rawAssembly.data[i]=='\\')start=i+1;
        if(end<rawAssembly.size)
            while(end>start&&rawAssembly.data[end-1]==' ')--end;
        size_t n=end-start;
        if(Extension(rawAssembly.data+start,n))n-=4;
        if(!n)return Fail(Error::InvalidAssembly);
        for(size_t i=0;i<n;++i){
            const uint8_t c=static_cast<uint8_t>(rawAssembly.data[start+i]);
            if(c>=0x80)return Fail(Error::NonAsciiAssembly);
            if(c<0x20||c==0x7f)return Fail(Error::InvalidAssembly);
        }
        if(!Put("type(",5)||!Number(n)||!Put(':'))return false;
        for(size_t i=0;i<n;++i)
            if(!Put(Lower(rawAssembly.data[start+i])))return false;
        state_=State::Open;return true;
    }
    // Outermost to innermost definition. Arity is the verified metadata
    // definition container count, not a constructed generic argument.
    bool AddDeclaration(ByteView nameSpace,ByteView name,uint32_t arity) noexcept {
        if(state_!=State::Open)return Fail(Error::NotComplete);
        if(declarations_==kMaximumDeclarations)return Fail(Error::TooDeep);
        if(Aliases(nameSpace)||Aliases(name))return Fail(Error::Alias);
        if(!name.data||!name.size)return Fail(Error::InvalidDeclaration);
        if((!nameSpace.data&&nameSpace.size)||!Utf8(nameSpace)||!Utf8(name))
            return Fail(Error::InvalidUtf8);
        if(!Put('/')||!Part(nameSpace)||!Put('/')||
           !Part(name)||!Put('@')||!Number(arity))return false;
        ++declarations_;return true;
    }
    bool Seal() noexcept {
        if(state_!=State::Open||!declarations_)return Fail(Error::NotComplete);
        if(!Put(')'))return false;
        state_=State::Complete;return true;
    }
    // Caller must revoke the record if subsequent source admission fails.
    bool Revoke() noexcept{return Fail(Error::UnsupportedShape);}
};
static_assert(sizeof(Capture)<=4200,"Bounded owned TypeKey storage grew");
} // namespace r03_b2_owned_definition_key
