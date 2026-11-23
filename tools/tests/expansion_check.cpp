#include "game/expansion.cpp"
#include <iostream>
#include <stdexcept>
namespace {std::vector<uint8_t> memory(8<<20),archive;
uint32_t textureCached=0,textureEntry=0;unsigned textureLookups=0,nativeTextureLoads=0;}
namespace kernel {
uint8_t* g_base=memory.data();
bool ReadGameFile(const std::string&,std::vector<uint8_t>& out){out=archive;return true;}
uint32_t CallGuest(X86Context& ctx,uint32_t fn,const uint32_t*,size_t){
    ++textureLookups;ctx.edi=0xBAD;ctx.eax=fn==0x267740?textureCached:textureEntry;return ctx.eax;
}
std::string ReadCString(uint32_t at,size_t limit){return std::string(reinterpret_cast<char*>(g_base+at),std::min(limit,std::strlen(reinterpret_cast<char*>(g_base+at))));}
}
namespace logx {void Write(Level,const char*,...) {}}
namespace phase9 {uint32_t ScheduleSlots(){return 37;}bool Enabled(Feature){return false;}int32_t CompetitionSlot(uint32_t){return -1;}bool CompetitionSeed(X86Context&){return false;}bool CompetitionDefer(X86Context&){return false;}void CompetitionRegister(X86Context&,uint32_t){}}
namespace mem {uint32_t AllocSystem(uint32_t size,uint32_t){static uint32_t next=0x70000;auto result=next;next+=size;return result;}}
extern "C" void __imp__sub_00086F90(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D9930(X86Context&,uint8_t*){}
extern "C" void __imp__sub_005000E0(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004F6110(X86Context&,uint8_t*){}
extern "C" void __imp__sub_00077D10(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D5130(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004D1910(X86Context&,uint8_t*){}
extern "C" void __imp__sub_004F0320(X86Context&,uint8_t*){}
extern "C" void __imp__sub_00279760(X86Context& ctx,uint8_t*){++nativeTextureLoads;ctx.esp+=12;}
extern "C" void __imp__sub_000E64F0(X86Context& ctx,uint8_t*){++nativeTextureLoads;ctx.esp+=4;}
void Database(uint32_t schools,uint32_t confs,bool gap=false) {
    std::string conf="0 conf_fullname\n";
    for(uint32_t i=1;i<=confs;i++)conf+="000000001 conference\n";
    uint32_t rows=schools+1+(gap?1:0),bytes=4+rows*487;
    archive.assign(64+bytes+conf.size(),0);std::memcpy(archive.data(),"BIGF",4);
    auto be=[&](size_t p,uint32_t n){for(int i=3;i>=0;i--){archive[p+i]=uint8_t(n);n>>=8;}};
    be(8,2);be(16,64);be(20,bytes);std::memcpy(&archive[24],"roster.bin",11);
    be(35,64+bytes);be(39,uint32_t(conf.size()));std::memcpy(&archive[43],"conf.dat",9);
    std::memcpy(&archive[64],&rows,4);for(uint32_t i=1;i<rows;i++)if(!gap||i!=102)archive[68+i*487+8]='T';
    std::memcpy(&archive[64+bytes],conf.data(),conf.size());
}
int main(int argc,char**) {
    auto first=expansion::LogoTable(0x1000),second=expansion::LogoTable(0x2000);
    if(first==second || expansion::LogoTable(0x1000)!=first || expansion::LogoTable(first)!=first || kernel::R32(first+512*60+20*24)!=0)throw std::runtime_error("expanded texture directory storage");
    auto textureCheck=[](uint32_t cached,uint32_t entry,unsigned lookups,unsigned loads){
        textureCached=cached;textureEntry=entry;textureLookups=nativeTextureLoads=0;
        X86Context ctx{};ctx.esp=0x10000;ctx.ecx=0x20000;ctx.edi=123;
        kernel::W32(ctx.esp+4,0x11000);kernel::W32(ctx.esp+8,0x12000);kernel::W32(0x11000,99);
        std::memcpy(memory.data()+0x12000,"missing",8);
        sub_00279760(ctx,kernel::g_base);
        if(ctx.esp!=0x1000C || ctx.ecx!=0x20000 || ctx.edi!=123 || textureLookups!=lookups || nativeTextureLoads!=loads ||
           (!loads && (kernel::R32(0x11000)!=0 || ctx.eax!=0)))throw std::runtime_error("native texture miss/ABI regression");
    };
    if(argc>1){
        Database(309,30);
        textureCheck(0,0,2,0);textureCheck(1,0,1,1);textureCheck(0,1,2,1);
        for(auto present:{false,true}){
            textureCached=0;textureEntry=present;textureLookups=nativeTextureLoads=0;
            X86Context ctx{};ctx.ecx=0x30000;ctx.esp=0x10000;ctx.edi=123;
            kernel::W32(0x6D1B10,0x20000);kernel::W16(ctx.ecx+0x28,1);
            memory[ctx.ecx+0x6C]=memory[ctx.ecx+0x68]=1;
            std::memcpy(memory.data()+ctx.ecx+0x4E,"missing",8);
            sub_000E64F0(ctx,kernel::g_base);
            if(ctx.esp!=0x10004 || ctx.ecx!=0x30000 || ctx.edi!=123 || nativeTextureLoads!=unsigned(present) || textureLookups!=2)
                throw std::runtime_error("missing native texture queued a draw or damaged guest ABI");
        }
        std::cout<<"PASS missing/cached/present native textures, output initialization and preserved guest ABI\n";
        return 0;
    }
    Database(128,14);if(Detect())throw std::runtime_error("stock detection");
    textureCheck(0,0,0,1);
    Database(309,30);if(!Detect())throw std::runtime_error("309 detection");
    Database(309,30,true);if(!Detect())throw std::runtime_error("309 detection with retired ID gap");
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
