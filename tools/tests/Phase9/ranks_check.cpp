#include "game/ranks.cpp"
#include <iostream>
namespace {std::vector<uint8_t> memory(8<<20);bool enabled=true;unsigned first=1;}
namespace kernel {uint8_t* g_base=memory.data();}
namespace phase9 {bool Enabled(Feature){return enabled;}}
namespace expansion {bool Active(){return true;}bool SchoolActive(uint32_t id){return id>0&&id<=310&&id!=102;}}
extern "C" void __imp__sub_004FD2A0(X86Context& ctx,uint8_t*){kernel::W32(ctx.ecx,first);kernel::W32(ctx.ecx+0x410,130);kernel::g_base[ctx.ecx+0x414]=1;ctx.esp+=8;}
extern "C" void __imp__sub_004FD1B0(X86Context& ctx,uint8_t*){kernel::W32(ctx.ecx,first);ctx.esp+=8;}
extern "C" void __imp__sub_004DAEC0(X86Context& ctx,uint8_t*){ctx.eax=0;ctx.esp+=8;}
extern "C" void __imp__sub_004E4FE0(X86Context& ctx,uint8_t*){ctx.eax=0;ctx.esp+=4;}
int main(){
    using kernel::W32;X86Context ctx{};constexpr unsigned list=0x10000,stack=0x20000;
    W32(kLeagueDb,0x30000);W32(0x30000+0x50,0x31000);W32(0x31008,0x40000);W32(0x40000+310*4,250);
    auto args=[&](unsigned value){ctx.ecx=list;ctx.esp=stack;W32(stack+4,value);};
    args(0);sub_004FD2A0(ctx,memory.data());if(!IsNational(list))throw std::runtime_error("National list untracked");
    first=2;args(0);sub_004FD1B0(ctx,memory.data());if(!IsNational(list))throw std::runtime_error("Weekly leader change invalidated national identity");
    args(310);sub_004DAEC0(ctx,memory.data());if(ctx.eax!=250)throw std::runtime_error("High-ID rank fallback lost after weekly refresh");
    args(7);sub_004FD1B0(ctx,memory.data());if(IsNational(list))throw std::runtime_error("Conference list mistaken for national");
    enabled=false;args(0);sub_004FD2A0(ctx,memory.data());first=3;args(0);sub_004FD1B0(ctx,memory.data());if(IsNational(list))throw std::runtime_error("Disabled refresh behavior changed");
    std::cout<<"PASS weekly national leader refresh, high-ID rank250 fallback, conference reuse exclusion and disabled preservation\n";
}
