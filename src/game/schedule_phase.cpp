#include "common/game_hooks.h"
#include "game/phase9.h"
#include "game/phase9/schedule-phase.h"
#include "kernel/kernel.h"
X86_EXTERN_FUNC(__imp__sub_004D9650);
X86_FUNC(sub_004D9650){
 const auto argument=ctx.esp+8,slot=kernel::R32(argument);
 const auto mapped=phase9::LegacyPhaseSlot(slot);
 const bool modern=phase9::Enabled(phase9::Feature::AnnualSchedules)||phase9::Enabled(phase9::Feature::Competition);
 if(!modern || mapped==slot)return __imp__sub_004D9650(ctx,base);
 // Keep the original native classifier and calling convention. Its input is
 // a slot value, not a pointer to an owner; no schedule or game row is changed.
 kernel::W32(argument,mapped);
 struct Restore{uint32_t argument,slot;~Restore(){kernel::W32(argument,slot);}} restore{argument,slot};
 __imp__sub_004D9650(ctx,base);
}

#include "kernel/memory.h"
#include "core/log.h"
#include "game/phase9/national-field-config.h"
#include <stdexcept>
namespace {
bool Committed(uint32_t address,uint32_t bytes){return address>=0x80000000u && address<0x90000000u && bytes && address+bytes>address && mem::IsCommitted(address) && mem::IsCommitted(address+bytes-1);}
}
namespace {
bool AllConferenceGamesFinished(uint32_t manager){
 for(unsigned slot=1;slot<phase9::ScheduleSlots();++slot){
  if(slot>=27&&slot<=36)continue;
  auto table=kernel::R32(manager+12+4*slot);if(!Committed(table,44))return false;
  auto count=kernel::R32(table+36),rows=kernel::R32(table+40);
  if(!count || count>40000 || !Committed(rows,count*20))return false;
  for(unsigned i=0;i<count;++i){const auto status=(kernel::R32(rows+20*i)>>20)&15;if(status<2 || status>5)return false;}
 }
 return true;
}
bool NoNationalGames(uint32_t manager){
 for(unsigned slot=27;slot<=36;++slot){const auto table=kernel::R32(manager+12+4*slot);if(table && (!Committed(table,44)||kernel::R32(table+36)))return false;}
 return true;
}
bool ChampionsReady(X86Context& ctx,uint8_t* base,uint32_t manager){
 const auto saved=ctx;
 for(const auto& p:mvp::national_field::kConferences){
  const auto root=kernel::R32(manager+12+4*p.slot);
  if(!Committed(root,44))return false;
  const auto vt=kernel::R32(root);if(!vt||!mem::IsCommitted(vt)||!mem::IsCommitted(vt+0x58))return false;
  const auto fn=kernel::R32(vt+0x58);if(!fn)return false;
  auto call=saved;const auto output=saved.esp-48;
  for(unsigned i=0;i<16;i+=4)kernel::W32(output+i,0);
  call.esp=output-8;kernel::W32(call.esp,0x00500910);kernel::W32(call.esp+4,output);call.ecx=root;
  x86_icall(call,base,fn);
  if(call.esp!=output)throw std::runtime_error("Native champion getter ABI differs");
  const auto team=kernel::R32(output+4);
  if(team==0||team>310||team==102){LOGR("postseason awaiting champion: conference=%u slot=%u team=%u",p.key,p.slot,team);return false;}
 }
 return true;
}
bool WorldOwner(uint32_t world,uint32_t manager){return Committed(world,24)&&Committed(manager,phase9::ScheduleManagerBytes())&&kernel::R32(world+16)==manager;}
}
X86_EXTERN_FUNC(__imp__sub_00500910);
X86_FUNC(sub_00500910){
 const auto world=ctx.ecx,manager=kernel::R32(0x6dc59c);
 if(phase9::Enabled(phase9::Feature::Competition)&&WorldOwner(world,manager))phase9::RefreshConferenceResets(ctx,base,manager);
 // The stock completion check covers the original conference slots. Modern
 // conferences finish on different dates; do not collect any automatic bids
 // until their real games are terminal too. Existing NCAA stages are untouched.
 if(phase9::Enabled(phase9::Feature::Competition) && WorldOwner(world,manager) && NoNationalGames(manager) && (!AllConferenceGamesFinished(manager)||!ChampionsReady(ctx,base,manager))){
  ctx.esp+=4;return;
 }
 __imp__sub_00500910(ctx,base);
}
void phase9::ResumePostseason(X86Context& ctx,uint8_t* base){
 if(!Enabled(Feature::Competition))return;
 const auto world=kernel::R32(0x6dc5a8),manager=kernel::R32(0x6dc59c);
 if(WorldOwner(world,manager))RefreshConferenceResets(ctx,base,manager);
 if(!WorldOwner(world,manager)||!NoNationalGames(manager)||!AllConferenceGamesFinished(manager)||!ChampionsReady(ctx,base,manager))return;
 // Re-run the native, idempotent world scheduling check. It classifies the
 // last game, collects the real 64-team field and creates the original NCAA
 // fixtures. No winners, scores, dates or standings are injected here.
 auto call=ctx;call.esp-=4;kernel::W32(call.esp,0x0054e470);call.ecx=world;
 x86_icall(call,base,0x00500910);
 if(call.esp!=ctx.esp)throw std::runtime_error("Native postseason scheduling ABI differs");
 const auto regionals=kernel::R32(manager+12+27*4);
 LOGR("postseason handoff: native scheduling check resumed, first regional games=%u",kernel::R32(regionals+36));
}



