#pragma once
// HOST TEST ONLY. Intentionally partial case-folding table for known Unicode
// examples. This is NOT the VM's VmStringUtils table or production name API.
// The actual .inl binds to ORIGINAL AssemblyShadowName.h in Unity.
// Use this shim solely to compile and exercise real .inl in a mock VM.
#include <cstddef>
#include <cstdint>

namespace il2cpp { namespace vm { namespace assembly_shadow_detail {
struct NameView { const char* begin; const char* end; };
inline char LowerAscii(char c) noexcept {
    return c>='A'&&c<='Z'?static_cast<char>(c+('a'-'A')):c;
}
inline NameView ViewName(const char* input) noexcept {
    if(!input)return {nullptr,nullptr};
    const char* begin=input;
    const char* end=input;
    for(;*end&&*end!=',';++end)
        if(*end=='/'||*end=='\\')begin=end+1;
    if(*end==',')
        while(end>begin&&end[-1]==' ')--end;
    if(end-begin>=4&&end[-4]=='.') {
        const char a=LowerAscii(end[-3]),b=LowerAscii(end[-2]),
                   c=LowerAscii(end[-1]);
        if((a=='d'&&b=='l'&&c=='l')||(a=='e'&&b=='x'&&c=='e'))
            end-=4;
    }
    return {begin,end};
}
inline uint16_t SampleLower(uint16_t unit) noexcept {
    if(unit>='A'&&unit<='Z')return static_cast<uint16_t>(unit+32);
    if(unit==0x00c9u)return 0x00e9u; // É -> é
    if(unit==0x00c4u)return 0x00e4u; // Ä -> ä
    if(unit==0x03a3u)return 0x03c3u; // Σ -> σ
    return unit;
}
inline bool NextFolded(const char*& p,const char* end,uint16_t& first,
                       uint16_t& second) noexcept {
    if(!p||p==end)return false;
    uint32_t value=static_cast<unsigned char>(*p++);
    unsigned additional=0;uint32_t minimum=0;
    if(value>=0xc2u&&value<=0xdfu){
        value&=0x1fu;additional=1;minimum=0x80u;
    }else if(value>=0xe0u&&value<=0xefu){
        value&=0x0fu;additional=2;minimum=0x800u;
    }else if(value>=0xf0u&&value<=0xf4u){
        value&=7u;additional=3;minimum=0x10000u;
    }else if(value>=0x80u||value<0x20u||value==0x7fu)return false;
    for(unsigned i=0;i<additional;++i){
        if(p==end)return false;
        const unsigned char byte=static_cast<unsigned char>(*p++);
        if((byte&0xc0u)!=0x80u)return false;
        value=(value<<6)|(byte&0x3fu);
    }
    if(value<minimum||value>0x10ffffu||
       (value>=0xd800u&&value<=0xdfffu))return false;
    first=value>0xffffu
        ?static_cast<uint16_t>((value>>10)+55232u)
        :static_cast<uint16_t>(value);
    second=value>0xffffu
        ?static_cast<uint16_t>((value&0x3ffu)+56320u):0u;
    first=SampleLower(first);
    second=SampleLower(second);
    return true;
}
inline bool NameHash(NameView view,size_t& result) noexcept {
    if(!view.begin||view.begin==view.end)return false;
    uint64_t hash=UINT64_C(14695981039346656037);
    const char* cur=view.begin;
    while(cur!=view.end){
        uint16_t first=0,second=0;
        if(!NextFolded(cur,view.end,first,second))return false;
        hash=(hash^first)*UINT64_C(1099511628211);
        hash=(hash^second)*UINT64_C(1099511628211);
    }
    result=static_cast<size_t>(hash);return true;
}
}}} // fake VM name shim
