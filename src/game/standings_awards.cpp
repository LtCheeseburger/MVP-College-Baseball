#include "common/game_hooks.h"
#include "game/expansion.h"
#include "game/phase9.h"
#include "core/dev_tools.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include <set>
#include <vector>
#include <stdexcept>

namespace {
void Require(bool ok,const char* why){if(!ok){LOGE("standings/awards: %s",why);throw std::runtime_error(why);}}
struct Collector;
thread_local Collector* collecting=nullptr;
struct Collector {
    Collector* previous=collecting;
    std::vector<std::pair<uint32_t,uint32_t>> lists;
    std::set<uint32_t> seen;
    Collector(){collecting=this;}
    ~Collector(){for(auto [owner,buffer]:lists){(void)owner;mem::FreeSystem(buffer);}collecting=previous;}
    bool Owns(uint32_t self)const{for(auto [owner,buffer]:lists){(void)buffer;if(owner==self)return true;}return false;}
};
void Compact(uint32_t self){
    if(!collecting||!collecting->Owns(self))return;
    auto data=kernel::R32(self+4),count=kernel::R32(self+8);
    Require(data&&kernel::R32(self+12)==512&&count<=512,"award collector allocation/count differs");
    unsigned kept=0;
    for(unsigned i=0;i<count;++i){auto id=kernel::R32(data+4*i);if(!expansion::SchoolActive(id))continue;kernel::W32(data+4*kept++,id);collecting->seen.insert(id);}
    kernel::W32(self+8,kept);
}
}

// The native collector already reads a pointer/capacity header. Give only this
// short-lived collector external scratch; its original stack, rank/award owner,
// 45 award rows, SEH, and all serialized layouts stay unchanged.
X86_EXTERN_FUNC(__imp__sub_000984D0);
X86_FUNC(sub_000984D0){
    auto owner=ctx.ecx,caller=kernel::R32(ctx.esp);
    __imp__sub_000984D0(ctx,base);
    if(!collecting||caller!=0x4f87a9)return;
    auto buffer=mem::AllocSystem(512*4);Require(buffer!=0,"award collector scratch allocation failed");
    collecting->lists.emplace_back(owner,buffer);std::memset(kernel::HostPtr(buffer),0,512*4);
    kernel::W32(owner+4,buffer);kernel::W32(owner+12,512);
}
X86_EXTERN_FUNC(__imp__sub_004E5AE0);
X86_FUNC(sub_004E5AE0){auto owner=ctx.ecx;__imp__sub_004E5AE0(ctx,base);Compact(owner);}
X86_EXTERN_FUNC(__imp__sub_004E5C30);
X86_FUNC(sub_004E5C30){auto owner=ctx.ecx;__imp__sub_004E5C30(ctx,base);Compact(owner);}
X86_EXTERN_FUNC(__imp__sub_004F8750);
X86_FUNC(sub_004F8750){
    if(!phase9::Enabled(phase9::Feature::StandingsAwards))return __imp__sub_004F8750(ctx,base);
    Collector scope;__imp__sub_004F8750(ctx,base);
    if(dev::Env("MVP06_TEST_AWARDS"))LOGR("standings/awards probe: native collector considered %zu active schools, highest ID %u",scope.seen.size(),scope.seen.empty()?0:*scope.seen.rbegin());
}

void phase9::StandingsBegin(X86Context& ctx){
    if(!Enabled(Feature::StandingsAwards))return;
    Require(kernel::R32(ctx.esi)==0x645c50&&ctx.eax<=expansion::kStandingsRows,"standings owner or conference count differs");
    std::memset(kernel::HostPtr(ctx.esi+0x62c),0,4*expansion::kStandingsRows);
}
void phase9::StandingsStore(X86Context& ctx){
    if(!Enabled(Feature::StandingsAwards))return;
    Require(kernel::R32(ctx.esi)==0x645c50&&ctx.edi>=1&&ctx.edi<=expansion::kStandingsRows&&ctx.eax==ctx.esi+0x62c+4*(ctx.edi-1)&&expansion::SchoolActive(ctx.ebp),"standings display index or active high-ID identity differs");
}
void phase9::StandingsClear(X86Context& ctx){
    if(!Enabled(Feature::StandingsAwards))return;
    Require(kernel::R32(ctx.esi)==0x645c50,"standings initializer owner differs");
    std::memset(kernel::HostPtr(ctx.esi+0x62c+12*4),0,(expansion::kStandingsRows-12)*4);
}

void phase9::ProbeAwards(X86Context& ctx){
    if(!dev::Env("MVP06_TEST_AWARDS"))return;
    static bool complete=false;if(complete||!DynastyMenuStable())return;
    Require(Enabled(Feature::StandingsAwards),"award probe requires its optional mod");
    auto saved=ctx;struct Restore{X86Context& c;X86Context old;~Restore(){c=old;}} restore{ctx,saved};
    auto owner=mem::AllocSystem(0x634);Require(owner!=0,"award probe allocation failed");
    struct Free{uint32_t at;~Free(){mem::FreeSystem(at);}} free{owner};
    ctx.esp=saved.esp-0x400;ctx.ecx=owner;kernel::CallGuest(ctx,0x4ff100,{});
    ctx.esp=saved.esp-0x400;ctx.ecx=owner;kernel::CallGuest(ctx,0x4f8750,{0,0});
    complete=true;game::WorkshopAwardsVerified();
}
