#include "common/game_hooks.h"
#include "common/overhaul.h"
#include "game/phase9.h"
#include "game/phase9/competition-bridge.h"
#include "game/phase9/schedule-phase.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "core/log.h"
#include "core/dev_tools.h"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace {
thread_local bool calendarRange=false;
std::mutex datesMutex;
std::unordered_map<uint32_t,uint32_t> dateStates;
bool Valid(uint32_t p,unsigned bytes){return p>=0x80000000u && p<0x90000000u && bytes && mem::IsCommitted(p) && mem::IsCommitted(p+bytes-1);}
int32_t CalendarEnd(const uint8_t* base,int32_t original){
 const auto manager=kernel::R32(0x6dc59c);if(!Valid(manager,phase9::ScheduleManagerBytes()))return original;
 const auto regular=kernel::R32(manager+16);if(!Valid(regular,44))return original;
 const unsigned y=phase9::SeasonYear(base);using namespace std::chrono;
 const auto dayNumber=[](sys_days date){return int32_t((date-sys_days{year{2028}/1/1}).count())+29221;};
 const auto first=dayNumber(sys_days{year{int(y)}/1/1}),next=dayNumber(sys_days{year{int(y+1)}/1/1});
 // NCAA fixtures do not exist until qualification; the whole postseason month
 // must remain reachable even before those participants and rows are created.
 int32_t end=std::max(original,dayNumber(sys_days{year{int(y)}/6/30}));
 std::unordered_set<uint32_t> seen;
 for(unsigned slot=0;slot<phase9::ScheduleSlots();++slot){
  auto root=kernel::R32(manager+12+slot*4);if(!Valid(root,44)||!seen.insert(root).second)continue;
  auto count=kernel::R32(root+36),rows=kernel::R32(root+40);auto start=int32_t(kernel::R32(root+20));
  if(!count||count>40000||!Valid(rows,count*20)||start<first||start>=next)continue;
  for(unsigned i=0;i<count;++i){auto word=kernel::R32(rows+i*20);auto date=start+int32_t(word&511);if(((word>>20)&15)<=3 && date>=first && date<next)end=std::max(end,date);}
 }
 return end;
}
}
X86_EXTERN_FUNC(__imp__sub_0054E470);
X86_FUNC(sub_0054E470){
 phase9::ResumePostseason(ctx,base);
 const bool previous=calendarRange;calendarRange=overhaul::Active() && phase9::Enabled(phase9::Feature::Competition);
 struct Restore{bool previous;~Restore(){calendarRange=previous;}} restore{previous};
 __imp__sub_0054E470(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_004F0DB0);
X86_FUNC(sub_004F0DB0){
 const auto caller=kernel::R32(ctx.esp),out=kernel::R32(ctx.esp+4);__imp__sub_004F0DB0(ctx,base);
 if(dev::Env("MVP06_TEST_DATE_BOUNDS") && ctx.eax==out && out && mem::IsCommitted(out+15)){
  static thread_local std::unordered_set<uint64_t> reported;
  const auto end=kernel::R32(out+4);if(reported.insert((uint64_t(caller)<<32)|end).second)LOGR("native date bound: caller=%08X end=%u",caller,end);
 }
 // Only the temporary month-display output is extended. Event dates,
 // game rows, simulation results and postseason advancement stay native.
 if(!calendarRange || ctx.eax!=out || !out || out>0xfffffff0u || !mem::IsCommitted(out) || !mem::IsCommitted(out+15))return;
 const auto original=int32_t(kernel::R32(out+4)),end=CalendarEnd(base,original);
 if(end>original){kernel::W32(out+4,uint32_t(end));LOGR("calendar range: native end=%d presentation end=%d",original,end);}
}

X86_EXTERN_FUNC(__imp__sub_00535DE0);
X86_FUNC(sub_00535DE0){
 const auto caller=kernel::R32(ctx.esp),model=ctx.ecx;
 __imp__sub_00535DE0(ctx,base);
 // Observe the native validity decision instead of enabling ungenerated dates.
 // The original input callbacks and simulation constraints stay authoritative.
 if(caller==0x0054E9F8u && overhaul::Active() && Valid(model,36)){
  std::lock_guard lock(datesMutex);dateStates[kernel::R32(model+0x20)]=kernel::R32(model+0x1c);
 }
}
namespace game {
bool DynastyDateAvailable(unsigned year,unsigned month,unsigned day){
 if(!day||month<1||month>12)return false;
 const uint32_t key=(year<<16)|(day<<8)|month;
 std::lock_guard lock(datesMutex);const auto it=dateStates.find(key);
 return it==dateStates.end() || it->second!=4;
}
}
