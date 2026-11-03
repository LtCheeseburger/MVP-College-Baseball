#include "game/expansion.cpp"
#include <iostream>
#include <stdexcept>
namespace {std::vector<uint8_t> memory(8<<20),archive;}
namespace kernel {
uint8_t* g_base=memory.data();
bool ReadGameFile(const std::string&,std::vector<uint8_t>& out){out=archive;return true;}
uint32_t CallGuest(X86Context&,uint32_t,const uint32_t*,size_t){return 0;}
}
namespace logx {void Write(Level,const char*,...) {}}
namespace mem {uint32_t AllocSystem(uint32_t,uint32_t){return 0x70000;}}
extern "C" void __imp__sub_00086F90(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D9930(X86Context&,uint8_t*){}
extern "C" void __imp__sub_005000E0(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004F6110(X86Context&,uint8_t*){}
extern "C" void __imp__sub_00077D10(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D5130(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D1910(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004F0320(X86Context&,uint8_t*){}
void Database(uint32_t schools,uint32_t confs) {
    std::string conf="0 conf_fullname\n";
    for(uint32_t i=1;i<=confs;i++)conf+="000000001 conference\n";
    archive.assign(64+4+conf.size(),0);std::memcpy(archive.data(),"BIGF",4);
    auto be=[&](size_t p,uint32_t n){for(int i=3;i>=0;i--){archive[p+i]=uint8_t(n);n>>=8;}};
    be(8,2);be(16,64);be(20,4);std::memcpy(&archive[24],"roster.bin",11);
    be(35,68);be(39,uint32_t(conf.size()));std::memcpy(&archive[43],"conf.dat",9);
    schools++;std::memcpy(&archive[64],&schools,4);std::memcpy(&archive[68],conf.data(),conf.size());
}
int main() {
    Database(128,14);if(Detect())throw std::runtime_error("stock detection");
    Database(309,30);if(!Detect())throw std::runtime_error("309 detection");
    Database(310,30);bool rejected=false;try{Detect();}catch(const std::exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("oversized database accepted");
    for(uint32_t slot=0;slot<=expansion::kNoSlot;slot++)for(uint32_t hits: {0u,1u,64u,127u}) {
        uint8_t bytes[4]={0,91,uint8_t(hits),73};expansion::StoreSlot(bytes,0,slot);
        uint32_t side;std::memcpy(&side,bytes,4);
        if(expansion::SlotAt(bytes,0)!=slot || expansion::SideSlot(side)!=slot || (bytes[2]&127)!=hits || bytes[1]!=91 || bytes[3]!=73)throw std::runtime_error("9-bit round trip");
        bytes[2]=expansion::KeepSlotBit(bytes[2],17);
        if(expansion::SlotAt(bytes,0)!=slot || (bytes[2]&127)!=17)throw std::runtime_error("hits damaged team slot");
    }
    if(expansion::kDbTeams<310 || expansion::kTeamSlots<309 || expansion::kBatterRows<309*25 || expansion::kPitcherRows<309*12 || expansion::kNodeMapNodes<expansion::kBatterRows+500)throw std::runtime_error("309 capacity");
    std::cout<<"PASS stock/309 detection, oversized rejection, all 512 slot/sentinel encodings and preserved scores/hits, 309 player/registry/node capacities\n";
}
