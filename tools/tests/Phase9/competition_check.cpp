#include "game/phase9/four-team-de-candidate.h"
#include "game/phase9/six-team-de-candidate.h"
#include "game/phase9/division-postseason-candidate.h"
#include "game/phase9/bracket-browser.h"
#include <iostream>
#include <set>
[[noreturn]] void x86_trap(X86Context&,uint32_t,const char* what){throw std::runtime_error(what);}
int main(){
    {
        std::vector<uint8_t> memory(0x8000);
        const auto put=[&](unsigned at,unsigned value){std::memcpy(memory.data()+at,&value,4);};
        constexpr unsigned root=0x1000,child=0x2000,descriptors=0x3000,rows=0x4000;
        put(child+0x64,root);put(child+0x60,18);put(child+0x6C,descriptors);
        put(root+0x24,18);put(root+0x28,rows);
        for(unsigned i=0;i<18;++i){put(descriptors+12*i,16);put(descriptors+12*i+4,i);put(rows+20*i,3u<<20);}
        if(mvp::bracket_browser::Pending(memory.data(),child)!=std::pair{0xFFFFFFFFu,0xFFFFFFFFu})throw std::runtime_error("Completed large bracket did not terminate");
        put(rows+20*11,2u<<20);put(rows+20*16,0);
        if(mvp::bracket_browser::Pending(memory.data(),child)!=std::pair{16u,16u})throw std::runtime_error("Bracket browser missed pending row beyond stock cap or selected cancelled reset");
        put(descriptors+12*16+4,999);
        if(mvp::bracket_browser::Pending(memory.data(),child)!=std::pair{0xFFFFFFFFu,0xFFFFFFFFu})throw std::runtime_error("Bracket browser accepted out-of-range descriptor");
        std::cout<<"PASS bounded postseason browser: completed 18-game bracket, pending ordinal16, cancelled resets and invalid descriptors\n";
    }
    std::set<unsigned> keys,slots;
    auto add=[&](const auto& profiles){for(const auto& p:profiles)if(!keys.insert(p.key).second||!slots.insert(p.slot).second||p.regular!=(p.key<=14?p.key:p.key+22)||p.start<28856||p.start>29220)throw std::runtime_error("Tournament profile identity/date overlap");};
    add(mvp::four_team_de::kProfiles);add(mvp::six_team_de::kProfiles);add(mvp::large_de::kProfiles);add(mvp::single_elimination::kProfiles);
    add(mvp::hybrid_postseason::kProfiles);add(mvp::twin_bracket::kProfiles);add(mvp::southland_postseason::kProfiles);add(mvp::staggered_postseason::kProfiles);
    add(mvp::bigten_postseason::kProfiles);add(mvp::cusa_postseason::kProfiles);add(mvp::maac_postseason::kProfiles);add(mvp::mac_postseason::kProfiles);add(mvp::division_postseason::kProfiles);
    if(keys.size()!=29||keys.contains(10)||slots.contains(23))throw std::runtime_error("Patriot series must retain its separate native owner");
    for(auto slot:slots)if(slot!=17&&slot!=21){auto count=mvp::division_postseason::Capacity(slot);if(count<9||count>20)throw std::runtime_error("Native tournament capacity");}
    unsigned swaps=0;
    auto home=[&](const auto& profiles,auto assign,bool four=false){for(const auto& p:profiles){
        std::vector<uint8_t> memory(0x8000);auto put=[&](unsigned at,unsigned value){std::memcpy(memory.data()+at,&value,4);};
        constexpr unsigned root=0x2000,child=0x3000,seeds=0x4000,descriptor=0x5000,row=0x1000;
        put(root,four?0x63acd0:0x63ad30);put(root+8,p.slot);put(root+16,p.key);put(child+0x64,root);put(child+0x68,seeds);
        put(seeds+12,1);put(seeds+28,2);put(descriptor+8,1);
        for(unsigned i=4;i<36;++i)memory[row+i]=uint8_t(i+32);
        expansion::StoreSlot(memory.data(),row+4,308);expansion::StoreSlot(memory.data(),row+12,255);
        auto before=memory;X86Context ctx{};ctx.esi=child;ctx.ebx=descriptor;ctx.edx=row;
        assign(ctx,memory.data());
        if(expansion::SlotAt(memory.data(),row+4)!=255||expansion::SlotAt(memory.data(),row+12)!=308||std::memcmp(memory.data()+row+4,before.data()+row+12,8)||std::memcmp(memory.data()+row+12,before.data()+row+4,8)||std::memcmp(memory.data()+row+20,before.data()+row+20,16))throw std::runtime_error("Home assignment corrupted a participant, side, or next 20-byte row");
        auto ordered=memory;assign(ctx,memory.data());if(memory!=ordered)throw std::runtime_error("Home assignment was not idempotent");++swaps;
    }};
#define HOME(name) home(mvp::name::kProfiles,mvp::name::HomeSeed);
    home(mvp::four_team_de::kProfiles,mvp::four_team_de::HomeSeed,true);HOME(six_team_de) HOME(large_de) HOME(single_elimination) HOME(hybrid_postseason)
    HOME(twin_bracket) HOME(southland_postseason) HOME(staggered_postseason) HOME(bigten_postseason) HOME(cusa_postseason) HOME(maac_postseason) HOME(mac_postseason) HOME(division_postseason)
#undef HOME
    if(swaps!=29)throw std::runtime_error("Missing native home-assignment family");
    std::cout<<"PASS 29 native home assignments: slot255/308, full eight-byte side swaps, next-row canaries and idempotency\n";
    std::cout<<"PASS 29 unique conference graphs, regular/tournament slots, authored 2027 windows, dynamic native capacities; Patriot series separate\n";
}
