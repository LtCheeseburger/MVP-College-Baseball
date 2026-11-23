#include "game/dynasty_saves.cpp"
#include <iostream>
namespace {std::vector<uint8_t> memory(8<<20);bool enabled=true,finalize=true;uint32_t gameRow=0x10000;}
namespace kernel {
uint8_t* g_base=memory.data();
std::string ReadCString(uint32_t at,size_t limit){std::string result;for(size_t i=0;i<limit&&memory[at+i];++i)result+=char(memory[at+i]);return result;}
uint32_t CallGuest(X86Context&,uint32_t,const uint32_t*,size_t){throw std::runtime_error("Unexpected serialization at unstable menu");}
}
namespace mem {bool IsCommitted(uint32_t at){return at<memory.size();}}
namespace app {std::filesystem::path UserDir(){return {};}}
namespace phase9 {bool Enabled(Feature){return enabled;}uint32_t ScheduleSlots(){return 72;}void VerifySavedAnnual(unsigned) {}void ProbeAwards(X86Context&) {}}
namespace kernel::saves {bool PublishDynastyAutosave(const std::vector<uint8_t>&,std::string&){throw std::runtime_error("Unexpected save publication");}}
namespace game {void WorkshopDynastySaved(){}}
namespace logx {void Write(Level,const char*,...) {}}
extern "C" void __imp__sub_004DDDD0(X86Context&,uint8_t*){if(finalize)kernel::W32(gameRow,3u<<20);}
extern "C" void __imp__sub_00097AF0(X86Context&,uint8_t*){}
extern "C" void __imp__sub_000985A0(X86Context&,uint8_t*){}
int main(){
    using kernel::W32;X86Context ctx{};ctx.esp=0x20000;ctx.ecx=0x30000;
    W32(ctx.esp+4,0x40000);W32(0x40004,0);W32(ctx.ecx+0x24,1);W32(ctx.ecx+0x28,gameRow);
    W32(0x6c6540,0x50000);W32(0x50008,0x51000);W32(0x51000,1);W32(0x51008,310);
    W32(0x6c656c,0x60000);W32(0x6dc5a0,0x70000);W32(0x7000c,309);W32(0x70010,0x71000);
    W32(0x71000+4*308,310);expansion::StoreSlot(memory.data(),gameRow+4,308);expansion::StoreSlot(memory.data(),gameRow+12,0);
    auto run=[&](uint32_t status){W32(gameRow,status<<20);sub_004DDDD0(ctx,memory.data());};
    run(1);if(pendingWorld!=0x50000)throw std::runtime_error("High-ID user final result not queued");
    phase9::DynastySaveTick(ctx);if(pendingWorld!=0x50000)throw std::runtime_error("Unstable menu consumed pending save");
    pendingWorld=0;run(3);if(pendingWorld)throw std::runtime_error("Already final result queued twice");
    finalize=false;run(1);if(pendingWorld)throw std::runtime_error("Nonfinal result queued");finalize=true;
    W32(0x51008,1);run(1);if(pendingWorld)throw std::runtime_error("Other school's result queued");W32(0x51008,310);
    enabled=false;run(1);if(pendingWorld)throw std::runtime_error("Disabled autosave queued");enabled=true;
    loading=1;run(1);if(pendingWorld)throw std::runtime_error("Load queued a save");loading=0;
    run(1);sub_000985A0(ctx,memory.data());if(pendingWorld)throw std::runtime_error("New dynasty retained old pending result");
    std::cout<<"PASS finalized user result only, high IDs, stable-menu deferral, disabled/load/duplicate exclusion, new-dynasty reset\n";
}
