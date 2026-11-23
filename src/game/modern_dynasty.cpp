#include "common/game_hooks.h"
#include "game/phase9.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include <array>
#include <mutex>
#include <stdexcept>

void phase9::NormalizeClass(uint32_t owner,uint32_t ordinal,bool assigning){
    if(!Enabled(Feature::ModernDynasty)||ordinal>=kernel::R32(owner+0x20))return;
    auto at=kernel::R32(owner+0x24)+84*ordinal+4;auto value=kernel::R32(at);
    if(!(value&0x80000000u))return;
    auto year=(value>>12)&7;if(!assigning)++year;
    if(year>4)throw std::runtime_error("Modern dynasty: eligibility exceeds five classes");
    kernel::W32(at,(value&~0x80007000u)|(year<<12));
}
void phase9::NormalizeStatus(uint32_t roster,uint32_t slot){
    if(!Enabled(Feature::ModernDynasty))return;
    if(slot>=30)throw std::runtime_error("Modern dynasty: invalid roster status slot");
    auto at=roster+0xC8+slot*8,value=kernel::R32(at);
    if(((value>>18)&7)==3)kernel::W32(at,value&~0x1C0000u);
}
uint32_t phase9::ClassLabel(uint32_t year,bool abbreviated){if(year>4)throw std::runtime_error("Modern dynasty: invalid class label");return (abbreviated?9050:9045)+year;}
void phase9::AddGraduateOption(X86Context& ctx){
    if(!Enabled(Feature::ModernDynasty))return;
    auto saved=ctx;ctx.ecx=kernel::R32(0x6C6560);auto text=kernel::CallGuest(ctx,0x83B00,{9049});
    ctx.ecx=kernel::R32(saved.esi+0x78C);auto method=kernel::R32(kernel::R32(ctx.ecx)+0xC4);
    kernel::CallGuest(ctx,method,{text,4});ctx=saved;
}
X86_EXTERN_FUNC(__imp__sub_00081110);
X86_FUNC(sub_00081110){auto field=kernel::R32(ctx.esp+4);if(field==7||field==13)phase9::NormalizeClass(ctx.ecx,kernel::R32(ctx.esp+8),false);__imp__sub_00081110(ctx,base);}
X86_EXTERN_FUNC(__imp__sub_004D2150);
X86_FUNC(sub_004D2150){phase9::NormalizeStatus(ctx.ecx,uint8_t(kernel::R32(ctx.esp+4)));__imp__sub_004D2150(ctx,base);}

// Native text lookups return borrowed UTF-16 pointers (83B00 ret4). Supply only
// the class labels here so localization mods can coexist in either load order.
uint32_t phase9::ClassText(uint32_t id){
    if(!Enabled(Feature::ModernDynasty)||id<9045||id>9054)return 0;
    static std::mutex mutex;static std::array<uint32_t,10> pointers{};
    static const char16_t* labels[]={u"Freshman",u"Sophomore",u"Junior",u"Senior",u"Graduate",u"FR",u"SO",u"JR",u"SR",u"GR"};
    std::lock_guard guard(mutex);auto index=id-9045;
    if(!pointers[index]){size_t bytes=(std::char_traits<char16_t>::length(labels[index])+1)*2;pointers[index]=mem::AllocSystem(uint32_t(bytes));if(!pointers[index])throw std::runtime_error("Modern dynasty: class text allocation failed");std::memcpy(kernel::HostPtr(pointers[index]),labels[index],bytes);}
    return pointers[index];
}
