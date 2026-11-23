#include "common/game_hooks.h"
#include "game/phase9.h"
#include "game/expansion.h"
#include "core/log.h"
#include "core/dev_tools.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "kernel/save_safety.h"
#include "app/app.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace {
bool Readable(uint32_t at,uint32_t length){
    if(!at || uint64_t(at)+length>0x100000000ull)return false;
    for(uint64_t p=at;p<uint64_t(at)+length;p=(p&~4095ull)+4096)if(!mem::IsCommitted(uint32_t(p)))return false;
    return !length||mem::IsCommitted(at+length-1);
}
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
thread_local unsigned loading=0;
thread_local bool saving=false;
uint32_t pendingWorld=0;
constexpr uint32_t kFlags=0x10121311;

bool Stable(){
    auto manager=kernel::R32(0x6C6818),front=kernel::R32(0x6C656C);
    if(!Readable(manager,0xd0)||kernel::g_base[manager+0xc8]||!Readable(front,0x54)||kernel::R32(front+0x50))return false;
    auto flow=kernel::R32(manager+0xbc);if(!Readable(flow,128))return false;
    auto resource=kernel::ReadCString(flow+0x20,80);
    bool home=resource=="fes_dynastyhome.fel",calendar=resource=="fes_calendarbase.fel"||resource=="fes_calendargame.fel";
    if(!home&&!calendar)return false;
    auto screens=kernel::R32(manager+0xb8);if(!Readable(screens,0x3fc))return false;
    auto count=kernel::R32(screens+0x3f8);if(!count||count>240)return false;
    bool ready=false;
    for(unsigned i=0;i<count;++i){
        auto screen=kernel::R32(screens+0x38+4*i);if(!Readable(screen,0x2c))return false;
        auto label=kernel::ReadCString(screen+0x10,16);auto flags=kernel::g_base[screen+0x28];
        if((flags&1)&&(label=="SIMPROGRESS"||label=="NOTIFIEDSIMPROG"||label=="POPUP"))return false;
        if((flags&3)==3&&((home&&label=="FMGRDECISION")||(calendar&&(label=="GAMECALENDAR"||label=="CALENDAR"))))ready=true;
    }
    return ready;
}

// Uses the original SaveSys serializer and original four-section signatures.
// No new envelope, team layout, or schedule-record layout is introduced.
std::vector<uint8_t> Serialize(X86Context& ctx){
    auto saved=ctx;struct Restore{X86Context& c;X86Context old;~Restore(){c=old;}} restore{ctx,saved};
    Require(saved.esp>=0x1000&&Readable(saved.esp-0x800,0x800),"Autosave scratch stack unavailable");
    const auto object=saved.esp-0x400,descriptor=object+0x200,stream=object+0x220,world=kernel::R32(0x6c6540);
    Require(Readable(world,0xf4),"Dynasty is unavailable");
    std::memset(kernel::HostPtr(object),0,0x300);
    auto call=[&](uint32_t fn,uint32_t self,std::initializer_list<uint32_t> args){ctx=saved;ctx.esp=saved.esp-0x600;ctx.ecx=self;return kernel::CallGuest(ctx,fn,args);};
    auto buffer=call(0x77da0,world,{descriptor+0x18,kFlags}),capacity=kernel::R32(descriptor+0x18);
    Require(buffer!=0,"Autosave allocation failed");
    struct Free{X86Context& c;uint32_t world,at,stack;~Free(){c.esp=stack;c.ecx=world;kernel::CallGuest(c,0x77df0,{at});}} free{ctx,world,buffer,saved.esp-0x600};
    Require(capacity>=100&&capacity<=64*1024*1024&&Readable(buffer,capacity),"Autosave allocation bounds differ");
    std::memset(kernel::HostPtr(buffer),0,capacity);kernel::W32(stream,0x6057fc);
    auto used=call(0x91e70,world,{buffer,capacity,kFlags,0,stream});
    Require(used>0&&uint64_t(used)+4<=capacity&&kernel::R32(buffer+used)==used,"Native dynasty payload is incomplete");
    call(0x140450,object,{});
    auto text=[&](uint32_t at,const char16_t* value){for(unsigned i=0;;++i){kernel::W16(at+2*i,value[i]);if(!value[i])break;}};
    text(object+0xc0,u"MVP27 Auto");text(object+0xe0,u"Dynasty");text(object+0x100,u"MVP27 Auto (Dynasty)");
    std::memcpy(kernel::HostPtr(object+0x140),"Save Game",10);kernel::W32(object+0xbc,kFlags);
    kernel::W32(descriptor,buffer);kernel::W32(descriptor+4,capacity);kernel::W32(object+0x16c,descriptor);
    kernel::W32(object+0x170,descriptor+8);kernel::W32(object+0x174,descriptor+8);
    call(0x1406e0,object,{});call(0x140570,object,{});
    for(unsigned i=0;i<4;++i)Require(kernel::R32(object+8+4*i)==1,"Native autosave signature verification failed");
    std::vector<uint8_t> bytes(264+capacity);std::memcpy(bytes.data(),kernel::HostPtr(object+0x58),264);std::memcpy(bytes.data()+264,kernel::HostPtr(buffer),capacity);
    LOGR("dynasty autosave: original serializer verified %u used / %u capacity bytes",used,capacity);return bytes;
}
}

bool phase9::DynastyMenuStable(){return Stable();}
// The season rollover changes every roster and schedule but has no user game
// result, so it would otherwise stay unsaved until the first game. Queue one
// attempt through the same serializer/publication path; loads clear it.
void phase9::QueueSeasonAutosave(unsigned year){
    if(!Enabled(Feature::SavesLauncher)||loading)return;
    pendingWorld=kernel::R32(0x6c6540);
    LOGR("dynasty autosave: %u season rollover queued",year);
}
void phase9::DynastySaveTick(X86Context& ctx){
    ProbeAwards(ctx);
    bool test=dev::Env("MVP06_TEST_NATIVE_SAVE");
    if((!Enabled(Feature::SavesLauncher)&&!test)||loading||saving)return;
    auto world=kernel::R32(0x6c6540);
    static bool testQueued=false;
    if(test&&!testQueued&&Stable()){pendingWorld=world;testQueued=true;}
    if(pendingWorld&&pendingWorld!=world)pendingWorld=0;
    if(!pendingWorld||!Stable())return;
    saving=true;
    // One attempt per completed result. Failure preserves the previous save;
    // there is no timer and no automatic repeat while the menu is idle.
    pendingWorld=0;
    try{auto bytes=Serialize(ctx);std::string reason;if(!kernel::saves::PublishDynastyAutosave(bytes,reason))LOGE("dynasty autosave: %s",reason.c_str());else if(test)game::WorkshopDynastySaved();}
    catch(const std::exception& e){LOGE("dynasty autosave: %s",e.what());}
    saving=false;
}

X86_EXTERN_FUNC(__imp__sub_004DDDD0);
X86_FUNC(sub_004DDDD0){
    uint32_t row=0,world=0;
    if(phase9::Enabled(phase9::Feature::SavesLauncher)&&!loading&&Readable(ctx.ecx,0x30)&&Readable(ctx.esp,12)){
        auto descriptor=kernel::R32(ctx.esp+4),root=ctx.ecx;
        if(Readable(descriptor,8)){
            auto index=kernel::R32(descriptor+4),count=kernel::R32(root+0x24),data=kernel::R32(root+0x28);
            if(index<count&&index<40000&&Readable(data+20*index,20)){
                auto candidate=data+20*index,status=(kernel::R32(candidate)>>20)&15;world=kernel::R32(0x6c6540);
                auto front=kernel::R32(0x6c656c),collection=kernel::R32(0x6dc5a0),users=Readable(world,0xf4)?kernel::R32(world+8):0;
                if(status<3&&Readable(front,0x54)&&!kernel::R32(front+0x50)&&Readable(users,12)&&kernel::R32(users)==1&&Readable(collection,20)){
                    auto n=kernel::R32(collection+12),ids=kernel::R32(collection+16),user=kernel::R32(users+8);
                    if(n<=309&&Readable(ids,n*4))for(unsigned offset:{4u,12u}){auto position=expansion::SlotAt(base,candidate+offset);if(position<n&&kernel::R32(ids+4*position)==user)row=candidate;}
                }
            }
        }
    }
    __imp__sub_004DDDD0(ctx,base);
    if(row&&kernel::R32(0x6c6540)==world&&((kernel::R32(row)>>20)&15)==3){pendingWorld=world;LOGR("dynasty autosave: final user result queued");}
}
namespace game{void ApplyPlaceholderNames(X86Context& ctx);}
X86_EXTERN_FUNC(__imp__sub_00097AF0);
X86_FUNC(sub_00097AF0){
    struct Scope{Scope(){++loading;pendingWorld=0;}~Scope(){--loading;}} scope;
    __imp__sub_00097AF0(ctx,base);
    game::ApplyPlaceholderNames(ctx);
    if(dev::Env("MVP06_TEST_SAVED_TABLES")){
        const auto owner=kernel::R32(0x6dc5a8),active=kernel::R32(0x6dc59c);
        const auto imported=Readable(owner,20)?kernel::R32(owner+16):0;
        LOGR("saved table audit: active=%08X import-owner=%08X",active,imported);
        const auto rows=[](uint32_t holder,unsigned slot){
            if(!Readable(holder,16+slot*4))return 0xffffffffu;
            const auto table=kernel::R32(holder+12+slot*4);
            return Readable(table,44)?kernel::R32(table+36):0xffffffffu;
        };
        for(unsigned slot=1;slot<phase9::ScheduleSlots();++slot)
            LOGR("saved table audit: slot=%u active-rows=%u import-rows=%u",slot,rows(active,slot),rows(imported,slot));
        const auto collection=kernel::R32(0x6dc5a0);
        if(Readable(collection,20)){
            const auto n=kernel::R32(collection+12),ids=kernel::R32(collection+16);
            if(n<=512&&Readable(ids,n*4)){
                std::ofstream out(app::UserDir()/"saved-regular-tables.csv");
                out<<"slot,day,minute,home,away,status\n";
                for(unsigned slot=1;slot<phase9::ScheduleSlots();++slot){
                    if(slot>15&&(slot<37||slot>52))continue;
                    const auto count=rows(active,slot);
                    if(count>40000)continue;
                    const auto table=kernel::R32(active+12+4*slot),data=kernel::R32(table+40);
                    if(!Readable(data,count*20))continue;
                    for(unsigned i=0;i<count;++i){
                        const auto row=data+20*i,word=kernel::R32(row),h=expansion::SlotAt(base,row+4),a=expansion::SlotAt(base,row+12);
                        Require(h<n&&a<n,"Saved table audit: invalid participant");
                        out<<slot<<','<<int32_t(kernel::R32(table+20)+(word&511))<<','<<((word>>9)&2047)<<','<<kernel::R32(ids+4*h)<<','<<kernel::R32(ids+4*a)<<','<<((word>>20)&15)<<'\n';
                    }
                }
            }
        }
    }
    if(dev::Env("MVP06_TEST_PLAYER_AUDIT")){
        // Persistent identity/class audit: every key in the native key->row map with
        // the attribute row's eligibility word (class bits 12-14, legacy redshirt bit 31).
        const auto world=kernel::R32(0x6c6540);
        const auto map=Readable(world,0x84)?kernel::R32(world+0x7C):0,table=Readable(world,0x84)?kernel::R32(world+0x80):0;
        const auto entries=Readable(map,12)?kernel::R32(map+4):0,count=Readable(map,12)?kernel::R32(map+8):0;
        const auto rows=Readable(table,0x28)?kernel::R32(table+0x20):0,data=Readable(table,0x28)?kernel::R32(table+0x24):0;
        Require(count&&count<20000&&rows&&rows<20000&&Readable(entries,count*12)&&Readable(data,rows*84),"Player audit: native player tables unavailable");
        std::ofstream out(app::UserDir()/"player-audit.csv");
        out<<"key,row,class,redshirt,word0,word4,word8\n";
        for(unsigned i=0;i<count;++i){
            const auto key=kernel::R32(entries+12*i),row=kernel::R32(entries+12*i+4);
            if(row>=rows)continue;
            const auto at=data+84*row,w4=kernel::R32(at+4);
            out<<std::hex<<key<<std::dec<<','<<row<<','<<((w4>>12)&7)<<','<<(w4>>31)<<','<<std::hex<<kernel::R32(at)<<','<<w4<<','<<kernel::R32(at+8)<<std::dec<<'\n';
        }
        LOGR("player audit: %u keys, %u attribute rows written",count,rows);
        // Team membership: [world+0x34] container, team i at [container+4]+i*0x2D8,
        // 30 roster slots of (player handle, status) at +0xC4 (0x4D1E40/0x4D23F0).
        const auto teams=Readable(world,0x38)?kernel::R32(world+0x34):0;
        const auto first=Readable(teams,12)?kernel::R32(teams+4):0,teamCount=Readable(teams,12)?kernel::R32(teams+8):0;
        if(first&&teamCount&&teamCount<=400&&Readable(first,teamCount*0x2D8)){
            std::ofstream roster(app::UserDir()/"roster-audit.csv");
            roster<<"team,slot,handle,status\n";unsigned filled=0;
            for(unsigned team=0;team<teamCount;++team)for(unsigned slot=0;slot<30;++slot){
                const auto at=first+team*0x2D8+0xC4+slot*8,handle=kernel::R32(at);
                if(!handle)continue;++filled;
                roster<<team<<','<<slot<<','<<std::hex<<handle<<','<<kernel::R32(at+4)<<std::dec<<'\n';
            }
            LOGR("player audit: %u team containers, %u rostered slots written",teamCount,filled);
        }else LOGR("player audit: team container unavailable (%08X, %u)",teams,teamCount);
    }
    if(dev::Env("MVP06_TEST_ANNUAL_SAVE"))phase9::VerifySavedAnnual(2027);
}
X86_EXTERN_FUNC(__imp__sub_000985A0);
X86_FUNC(sub_000985A0){pendingWorld=0;__imp__sub_000985A0(ctx,base);}
