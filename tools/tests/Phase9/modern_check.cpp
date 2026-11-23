#include "game/modern_dynasty.cpp"
#include <iostream>
#include <vector>
namespace {std::vector<uint8_t> memory(4<<20);bool modern=false;}
namespace kernel {uint8_t* g_base=memory.data();uint32_t CallGuest(X86Context&,uint32_t,const uint32_t*,size_t){return 0;}}
namespace phase9 {bool Enabled(Feature f){return modern&&f==Feature::ModernDynasty;}}
namespace mem {uint32_t AllocSystem(uint32_t n,uint32_t){static uint32_t next=0x100000;auto at=next;next+=n;return at;}}
extern "C" void __imp__sub_00081110(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D2150(X86Context&,uint8_t*){}
extern "C" void __imp__sub_00083B00(X86Context& c,uint8_t*){c.eax=0;c.esp+=8;}
int main(){
    static_assert(phase9::kRecruitProspects==966 && phase9::kRecruitPositionQuota==580 && phase9::ScaleRecruiting(400,128)==400);
    constexpr uint32_t owner=0x1000,data=0x2000,roster=0x4000;
    kernel::W32(owner+0x20,1);kernel::W32(owner+0x24,data);
    for(unsigned year=0;year<4;++year){
        uint32_t original=0x80000a35u|(year<<12);kernel::W32(data+4,original);
        modern=false;phase9::NormalizeClass(owner,0,false);if(kernel::R32(data+4)!=original)throw std::runtime_error("disabled class changed");
        modern=true;phase9::NormalizeClass(owner,0,false);auto converted=kernel::R32(data+4);
        if(((converted>>12)&7)!=year+1||(converted&0x80000000u)||(converted&0xfff)!=(original&0xfff))throw std::runtime_error("legacy eligibility conversion");
        phase9::NormalizeClass(owner,0,false);if(kernel::R32(data+4)!=converted)throw std::runtime_error("conversion not idempotent");
    }
    for(unsigned slot=0;slot<30;++slot){kernel::W32(roster+0xC8+slot*8,0x1ffffu|(3u<<18));phase9::NormalizeStatus(roster,slot);if(kernel::R32(roster+0xC8+slot*8)!=0x1ffffu)throw std::runtime_error("redshirt status leaked");}
    for(unsigned year=0;year<5;++year){if(phase9::ClassLabel(year,false)!=9045+year||phase9::ClassLabel(year,true)!=9050+year)throw std::runtime_error("five class labels");}
    auto text=phase9::ClassText(9054);if(kernel::R16(text)!='G'||kernel::R16(text+2)!='R')throw std::runtime_error("native GR text storage");
    kernel::W32(data+4,0x80004000u);bool refused=false;try{phase9::NormalizeClass(owner,0,false);}catch(const std::exception&){refused=true;}if(!refused)throw std::runtime_error("invalid eligibility accepted");
    std::cout<<"PASS optional-off preservation, five classes, idempotent legacy redshirt conversion, 30 status slots, native GR text storage and eligibility rejection\n";
}
