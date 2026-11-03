// Exercise the production poll against a synthetic guest database; no game code.
#include "game/poll.cpp"
#include <bit>
#include <iostream>
#include <set>
#include <stdexcept>
namespace { std::vector<uint8_t> memory(8<<20); bool expanded=false; int original=0; }
namespace kernel {
uint8_t* g_base=memory.data();
uint32_t CallGuest(X86Context&,uint32_t fn,const uint32_t* args,size_t) {
    if(fn==0x86BC0) for(int i=1;i<=4;i++) W32(args[i],std::bit_cast<uint32_t>(float(args[0]==2?100:args[0]%25)));
    return 0;
}
}
namespace mem { uint32_t AllocSystem(uint32_t,uint32_t){return 0x50000;} }
namespace expansion { bool Active(){return expanded;} }
namespace game { bool g_fitted_poll=false; }
namespace logx { void Write(Level,const char*,...){} }
extern "C" void __imp__sub_00090AB0(X86Context&,uint8_t*){original++;}
X86_FUNC(__imp__sub_004FD2A0){}
X86_FUNC(__imp__sub_004DAEC0){}
X86_FUNC(__imp__sub_004E4FE0){}
int main() {
    using kernel::W32;using kernel::R32;
    X86Context ctx{};ctx.esp=0x60000;
    sub_00090AB0(ctx,memory.data());
    if(original!=1) throw std::runtime_error("stock poll fallback");
    for(uint32_t count: {128u,309u}) {
        expanded=count>130;game::g_fitted_poll=true;
        std::fill(memory.begin(),memory.end(),0);
        W32(kLeagueDb,0x10000);W32(kRegistry,0x20000);
        W32(0x10000+0x50,0x30000);W32(0x10000+0x54,0x32000);W32(0x10000+0x34,0x40000);W32(0x40000+8,count+1);
        W32(0x30008,0x31000);W32(0x32008,0x33000);
        W32(0x2000C,count);W32(0x20010,0x21000);
        for(uint32_t i=0;i<count;i++) W32(0x21000+i*4,i+1);
        ctx.esp=0x60000;sub_00090AB0(ctx,memory.data());
        std::set<uint32_t> ranks;
        for(uint32_t i=1;i<=count;i++)ranks.insert(R32(0x31000+i*4));
        if(ranks.size()!=count || *ranks.begin()!=1 || *ranks.rbegin()!=count || R32(0x31008)!=1)throw std::runtime_error("complete preseason ranking");
        if(expanded && expansion::PollRank(309)==0)throw std::runtime_error("rank past stock list");
        auto order=RunPoll(ctx);if(order.size()!=count || order.front()!=2)throw std::runtime_error("deterministic talent ranking");
        std::cout<<"PASS production fitted poll: "<<count<<" schools, complete ranks, talent ordering\n";
    }
    std::cout<<"PASS disabled fitted poll delegates to stock code\n";
}
