#include "common/game_hooks.h"
#include "game/phase9.h"
#include "game/phase9/bracket-browser.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include <cstring>
#include <string>

X86_EXTERN_FUNC(__imp__sub_005208E0);
X86_FUNC(sub_005208E0) {
    if (phase9::Enabled(phase9::Feature::Competition)) {
        const auto descriptor=kernel::R32(ctx.esp+4);
        const auto manager=kernel::R32(0x6dc59c);
        const auto committed=[](uint32_t p,uint32_t bytes) {
            return p && p+bytes>p && mem::IsCommitted(p) && mem::IsCommitted(p+bytes-1);
        };
        const bool valid=mvp::bracket_browser::ResolveRow(base,manager,descriptor,phase9::ScheduleSlots(),committed)!=0;
        // Future/unassigned bracket placeholders have no game table yet.
        // The native renderer dereferences table+0x28 before checking this.
        if(!valid) { ctx.eax&=0xFFFFFF00u;ctx.esp+=12;return; }
    }
    __imp__sub_005208E0(ctx,base);
}

#include "core/log.h"
#include "core/dev_tools.h"
X86_EXTERN_FUNC(__imp__sub_0053F4E0);
X86_FUNC(sub_0053F4E0){
 const auto owner=ctx.ecx;
 // The original controller's 37-entry mapping cannot hold the expanded league.
 // Read the schedule value from the native combo instead, whose capacity is 73.
 if(phase9::Enabled(phase9::Feature::Competition) && mem::IsCommitted(owner+0x214)){
  const auto node=kernel::R32(owner+0x214),combo=node+0xe0;
  if(node && mem::IsCommitted(combo+0x2a8)){
   const auto count=kernel::R32(combo+12),index=kernel::R32(combo+8);
   if(count>19 && count<=73 && index<count){
    const auto slot=kernel::R32(combo+0x184+4*index);
    if(slot<phase9::ScheduleSlots())kernel::W32(ctx.esp+4,slot);
   }
  }
 }
 if(dev::Env("MVP06_TEST_TREE_DUMP")){
  const auto model=kernel::R32(owner+0x21c);
  LOGR("tournament selector owner=%08X model=%08X combo=%08X comboVtable=%08X",owner,model,model+0xe0,model?kernel::R32(model+0xe0):0);
 }
 __imp__sub_0053F4E0(ctx,base);
}

X86_EXTERN_FUNC(__imp__sub_005536D0);
X86_FUNC(sub_005536D0){
 const auto owner=ctx.ecx;
 __imp__sub_005536D0(ctx,base);
 if(!phase9::Enabled(phase9::Feature::Competition))return;
 const auto node=kernel::R32(owner+0x214),combo=node+0xe0;
 if(!node || !mem::IsCommitted(combo+0x2a8))return;
 const auto count=kernel::R32(combo+12);
 if(!count || count>54)return;
 static constexpr const char* names[]={"Big West","Pac-12","UAC","Summit","A-10","American","Big South","CAA","America East","NEC","Ivy","Metro","MAC","Horizon","OVC","SoCon","Southland","SWAC","ASUN"};
 static uint32_t labels=0;
 if(!labels){labels=mem::AllocSystem(sizeof(names)/sizeof(*names)*128);if(!labels)return;
  for(unsigned i=0;i<19;++i){const auto title=std::string(names[i])+" Tournament";
   for(unsigned j=0;j<=title.size();++j)kernel::W16(labels+128*i+2*j,uint16_t(title[j]));}}
 const auto savedEcx=ctx.ecx;
 for(unsigned i=0;i<19;++i){
  bool exists=false;for(unsigned j=0;j<kernel::R32(combo+12);++j)if(kernel::R32(combo+0x184+4*j)==53+i)exists=true;
  const auto manager=kernel::R32(0x6dc59c),table=manager?kernel::R32(manager+12+4*(53+i)):0;
  if(exists||!table||!mem::IsCommitted(table+43))continue;
  ctx.ecx=node;kernel::CallGuest(ctx,kernel::R32(kernel::R32(node)+0xc4),{labels+128*i,53+i});
 }
 ctx.ecx=savedEcx;
 LOGR("tournament selector: %u native schedule choices",kernel::R32(combo+12));
}

X86_EXTERN_FUNC(__imp__sub_004EA700);
X86_FUNC(sub_004EA700){
 // This specific browser probe asks whether the optional tenth game exists.
 // Smaller authored graphs have no such ordinal; do not enter the stock graph.
 if(phase9::Enabled(phase9::Feature::Competition) && kernel::R32(ctx.esp)==0x00540D2Fu &&
    kernel::R32(ctx.esp+4)>=kernel::R32(ctx.ecx+0x60)){
  ctx.eax=0xFFFFFFFFu;ctx.esp+=16;return;
 }
 __imp__sub_004EA700(ctx,base);
}

