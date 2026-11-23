#include "game/standings_awards.cpp"
#include <iostream>
namespace {std::vector<uint8_t> memory(8<<20);bool enabled=true;unsigned allocations=0,frees=0,calls=0;}
namespace kernel {uint8_t* g_base=memory.data();uint32_t CallGuest(X86Context&,uint32_t,const uint32_t*,size_t){throw std::runtime_error("Unexpected probe");}}
namespace phase9 {bool Enabled(Feature){return enabled;}bool DynastyMenuStable(){return false;}}
namespace expansion {bool SchoolActive(uint32_t id){return id>0&&id<=310&&id!=102;}}
namespace mem {uint32_t AllocSystem(uint32_t n,uint32_t){if(n!=2048)throw std::runtime_error("Collector capacity");++allocations;return 0x40000;}void FreeSystem(uint32_t at){if(at!=0x40000)throw std::runtime_error("Foreign scratch free");++frees;}}
namespace game {void WorkshopAwardsVerified(){}}
namespace logx {void Write(Level,const char*,...) {}}
extern "C" void __imp__sub_000984D0(X86Context& ctx,uint8_t*){kernel::W32(ctx.ecx+4,ctx.ecx+24);kernel::W32(ctx.ecx+8,0);kernel::W32(ctx.ecx+12,130);ctx.esp+=4;}
extern "C" void __imp__sub_004E5AE0(X86Context& ctx,uint8_t*){auto data=kernel::R32(ctx.ecx+4);for(unsigned i=0;i<311;++i)kernel::W32(data+4*i,i);kernel::W32(ctx.ecx+8,311);ctx.esp+=8;}
extern "C" void __imp__sub_004E5C30(X86Context& ctx,uint8_t* base){__imp__sub_004E5AE0(ctx,base);}
extern "C" void __imp__sub_004F8750(X86Context& ctx,uint8_t* base){
    ++calls;if(!enabled)return;
    auto saved=ctx;ctx.ecx=0x20000;ctx.esp=0x70000;kernel::W32(ctx.esp,0x4f87a9);sub_000984D0(ctx,base);
    if(kernel::R32(ctx.ecx+12)!=512||kernel::R32(ctx.ecx+4)!=0x40000)throw std::runtime_error("Collector retained stock capacity");
    sub_004E5AE0(ctx,base);
    if(kernel::R32(ctx.ecx+8)!=309||kernel::R32(0x40000+308*4)!=310)throw std::runtime_error("High-ID school omitted");
    for(unsigned i=0;i<309;++i)if(!expansion::SchoolActive(kernel::R32(0x40000+4*i)))throw std::runtime_error("Retired/null school remained");
    ctx=saved;
}
int main(){
    X86Context ctx{};ctx.ecx=0x10000;ctx.esp=0x60000;
    sub_004F8750(ctx,memory.data());if(allocations!=1||frees!=1||collecting)throw std::runtime_error("Collector scratch lifetime leaked");
    enabled=false;sub_004F8750(ctx,memory.data());if(calls!=2||allocations!=1)throw std::runtime_error("Disabled award collector changed");enabled=true;
    ctx.esi=0x50000;kernel::W32(ctx.esi,0x645c50);ctx.eax=16;
    std::memset(memory.data()+ctx.esi+0x62c,0xa5,32*4);phase9::StandingsBegin(ctx);
    for(unsigned i=0;i<32;++i)if(kernel::R32(ctx.esi+0x62c+4*i))throw std::runtime_error("Stale scrolling row retained");
    ctx.edi=16;ctx.eax=ctx.esi+0x62c+15*4;ctx.ebp=310;phase9::StandingsStore(ctx);
    ctx.ebp=102;bool rejected=false;try{phase9::StandingsStore(ctx);}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("Retired standings row accepted");
    ctx.eax=33;rejected=false;try{phase9::StandingsBegin(ctx);}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("Oversized conference accepted");
    enabled=false;phase9::StandingsBegin(ctx);
    std::cout<<"PASS scoped all-309 award collector, high ID310, retired/null exclusion, scratch lifetime, disabled delegation, scrolling-row invalidation and bounds\n";
}
