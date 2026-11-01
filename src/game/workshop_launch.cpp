// Opt-in Workshop startup routing. All state changes use native controller input;
// no guessed roster/stadium addresses or save data are written.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <fstream>
#include "app/app.h"
#include "gpu/backend.h"
#include "gpu/screenshot.h"
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include "kernel/modvfs.h"
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include "common/game_hooks.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {
std::mutex mutex;
std::atomic<bool> enabled{false};
std::string target, home, away, venue, scene;
std::unordered_map<std::string,uint32_t> roots;
ULONGLONG started = 0, sceneAt = 0, nextPulse = 0, releaseAt = 0;
bool menuWrong = false;
bool audit = false;
int searches = 0;
int stadiumId = -1;
bool seenMatchup = false, venueVerified = false;
bool Valid(uint32_t pointer) { return pointer >= 0x80000000u && pointer < 0x90000000u && mem::IsCommitted(pointer) && mem::IsCommitted(pointer+0x50); }
std::string Normalize(std::string text) { std::string out; for (unsigned char c : text) if(std::isalnum(c))out+=char(std::toupper(c)); return out; }
bool PatchLaunchVenue(std::vector<uint8_t>& data) {
    std::lock_guard lock(mutex);
    auto read=[&](size_t at){uint32_t value;std::memcpy(&value,data.data()+at,4);return value;};
    if(data.size()<4 || stadiumId<0)return false;
    auto count=read(0);if(count<1 || count>1024)return false;
    size_t offset=4;
    for(uint32_t index=0;index<count;index++) {
        if(offset+487>data.size())return false;
        auto players=read(offset+192);if(players>100 || offset+487+size_t(players)*8>data.size())return false;
        std::string name(reinterpret_cast<const char*>(data.data()+offset+8),48);name.resize(name.find('\0')==std::string::npos?name.size():name.find('\0'));
        if(!name.empty() && (home.empty() || Normalize(name)==Normalize(home))) {
            if(home.empty())home=name;
            // Same decoded six-bit field as TeamAssignmentEditor; every other roster bit is retained.
            uint32_t before=read(offset+168),after=(before & ~(63u<<4)) | (uint32_t(stadiumId)<<4);
            std::memcpy(data.data()+offset+168,&after,4);
            LOGR("workshop launch: temporary stadium assignment %s -> enum %d (roster bytes only; source and saves unchanged)",home.c_str(),stadiumId);
            return true;
        }
        offset+=487+size_t(players)*8;
    }
    LOGE("workshop launch: school not found for temporary stadium assignment");return false;
}
std::string Utf8(const wchar_t* text) {
    int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr); if(size<1)return {};
    std::string result(size,0);WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),size,nullptr,nullptr);result.pop_back();return result;
}
std::string Text(uint32_t node) {
    auto pointer=kernel::R32(node+0x44);std::string result;
    if(pointer<0x10000)return result;
    for(unsigned i=0;i<256 && mem::IsCommitted(pointer+i*2+1);i++) {
        auto c=kernel::R16(pointer+i*2);if(!c)break;if(c<128)result+=char(c);
    }
    return result;
}
template<class Visitor> void Walk(uint32_t root, Visitor visitor) {
    if(!Valid(root))return;
    auto depth=kernel::R32(root+0x24);if(depth>=128)return;
    std::array<std::string,128> ancestors;std::array<bool,128> shown{};shown[depth]=true;
    auto node=kernel::R32(root+8);
    for(int count=0;count<4096 && Valid(node) && kernel::R32(node+0x24)>depth;count++,node=kernel::R32(node+8)) {
        auto level=kernel::R32(node+0x24);if(!level || level>=128)continue;
        auto name=kernel::ReadCString(node+0x10,16);ancestors[level]=name;
        shown[level]=shown[level-1] && (kernel::R32(node+0x28)&1);
        visitor(node,name,level,depth,ancestors,shown[level]);
    }
}
struct Side { std::string name, conference, step="team"; };
std::array<Side,2> Teams() {
    std::array<Side,2> sides;
    Walk(roots["TEAMSELECT"],[&](auto node,auto& name,auto level,auto depth,auto& ancestors,bool shown) {
        int side=-1;for(auto i=depth+1;i<level;i++){if(ancestors[i]=="AWAYTEAM")side=0;if(ancestors[i]=="HOMETEAM")side=1;}
        if(side<0)return;
        if(name=="FULLNAME1")sides[side].name=Text(node);
        if(name=="LEAGUE")sides[side].conference=Text(node);
        if(shown && (name=="HOMEJERSEY" || name=="AWAYJERSEY"))sides[side].step="uniform";
        if(shown && (name=="HOMEDIFFICULTY" || name=="AWAYDIFFICULTY"))sides[side].step="difficulty";
        if(shown && (name=="HOMEREADY" || name=="AWAYREADY"))sides[side].step="ready";
    });return sides;
}
int Lane() {
    int lane=-2;
    Walk(roots["CNTRLSELECT"],[&](auto node,auto& name,auto,auto,auto&,bool shown) {
        if(shown && name=="P1"){int y=int16_t(kernel::R16(node+0x2C));lane=y<160?0:y>240?1:-1;}
    });return lane;
}
void Stop(const char* status,const std::string& message) {
    enabled=false;
    std::ofstream(app::UserDir() / "workshop-launch-result.txt") << status << '\n' << message;
    if(audit)gpu::screenshot::Request();
    LOGR("workshop launch: %s: %s",status,message.c_str());
}
void Observe(uint32_t root) {
    if(!enabled || !Valid(root))return;
    auto name=kernel::ReadCString(root+0x10,16);
    if(name=="CNTRLSELECT"){std::lock_guard lock(mutex);roots[name]=root;return;}
    if(name!="TITLE" && name!="START" && name!="MAINMENU" && name!="TEAMSELECT" && name!="STADIUM" && name!="SMARTOPTIONS" && name!="SMARTOPTIONS2" && name!="STARTINGPITCHER" && name!="STARTINGLINEUP" && name!="LOADING1" && name!="TEAMSMATCHUP" && name!="HUDRIGHT")return;
    std::lock_guard lock(mutex);roots[name]=root;
    if(scene!=name){scene=name;sceneAt=GetTickCount64();nextPulse=sceneAt+600;LOGR("workshop launch: scene %s",scene.c_str());}
}
}

namespace game {
void WorkshopLaunchInit() {
    bool badArguments=false;
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    for(int i=1;arguments && i+1<count;i++) {
        if(!std::wcscmp(arguments[i],L"--workshop-launch-audit"))audit=true;
        else if(!std::wcscmp(arguments[i],L"--workshop-target"))target=Utf8(arguments[++i]);
        else if(!std::wcscmp(arguments[i],L"--team") || !std::wcscmp(arguments[i],L"--home"))home=Utf8(arguments[++i]);
        else if(!std::wcscmp(arguments[i],L"--away"))away=Utf8(arguments[++i]);
        else if(!std::wcscmp(arguments[i],L"--stadium"))venue=Utf8(arguments[++i]);
        else if(!std::wcscmp(arguments[i],L"--stadium-id")) {
            wchar_t* end=nullptr;long value=std::wcstol(arguments[++i],&end,10);
            if(!end || *end || value<0 || value>=30)badArguments=true;else stadiumId=int(value);
        }
    }
    LocalFree(arguments);
    if(target.empty())return;
    if(badArguments || (target!="mainmenu" && target!="team" && target!="stadium" && target!="exhibition") ||
        ((target=="team" || target=="exhibition") && home.empty()) || (target=="exhibition" && (away.empty() || Normalize(home)==Normalize(away))) || (target=="stadium" && venue.empty())) {
        LOGE("workshop launch: invalid target arguments");return;
    }
    started=GetTickCount64();enabled=true;
    if(stadiumId>=0)kernel::modvfs::AddPatch("DATABASE.BIG","roster.bin",PatchLaunchVenue);
    LOGR("workshop launch: requested %s home=%s away=%s stadium=%s",target.c_str(),home.c_str(),away.c_str(),venue.c_str());
}
bool WorkshopMenuSelection(const std::string& name) {
    if(!enabled)return true;
    std::lock_guard lock(mutex);
    if(name=="PLAYNOW")return true;
    menuWrong=true;return false; // Never activate another native menu action during automation.
}
void WorkshopLaunchPad(int port,input::Pad& pad) {
    KeyboardPad(port,pad);
    if(port!=0 || !enabled)return;
    std::lock_guard lock(mutex);auto now=GetTickCount64();
    if(now-started>180000){Stop("failed","Target was not reached within three minutes; automatic input stopped");return;}
    bool manual=pad.buttons || std::abs(int(pad.lx))>16000 || std::abs(int(pad.ly))>16000 || std::abs(int(pad.rx))>16000 || std::abs(int(pad.ry))>16000;
    for(auto value:pad.analog)manual|=value>30;
    if(manual){Stop("cancelled","Controller/keyboard input returned control to the user");return;}
    pad.connected=true;
    if(now<releaseAt)return;
    if(now<nextPulse)return;
    nextPulse=now+360;releaseAt=now+100;
    if(scene=="TITLE" || scene=="START"){pad.buttons=0x10;return;}
    if(scene=="MAINMENU") {
        if(target=="mainmenu"){Stop("reached","Main menu");return;}
        if(menuWrong){pad.buttons=2;menuWrong=false;}else pad.analog[0]=255;
        return;
    }
    if(scene=="TEAMSELECT") {
        auto teams=Teams();int lane=Lane();
        static std::string lastTeams;
        auto teamState=teams[0].name+" | "+teams[1].name+" | "+std::to_string(lane)+" | "+teams[0].step+" | "+teams[1].step+" | "+teams[1].conference;
        if(teamState!=lastTeams){lastTeams=teamState;LOGR("workshop launch: teams %s",teamState.c_str());}
        if(teams[0].name.empty() || teams[1].name.empty() || lane==-2)return;
        int wanted=!away.empty() && Normalize(teams[0].name)!=Normalize(away)?0:1;
        auto name=wanted==0?away:home;
        if(!name.empty() && Normalize(teams[wanted].name)!=Normalize(name)) {
            if(lane!=wanted){pad.ly=wanted==0?32767:-32767;return;}
            if(teams[wanted].step!="team"){pad.analog[1]=255;return;}
            if(++searches>300){Stop("failed","Requested school was not found in native team selection");return;}
            static std::set<std::string> visited[2]; static std::string lastName[2];
            auto observed=Normalize(teams[wanted].name);
            if(observed!=lastName[wanted]) {
                lastName[wanted]=observed;
                if(!visited[wanted].insert(observed).second) {
                    visited[wanted].clear();lastName[wanted].clear();pad.analog[7]=255;
                    return;
                }
            }
            pad.buttons=8;return;
        }
        if(target=="team"){Stop("reached","Team selector: "+home);return;}
        if(lane!=1){pad.ly=-32767;return;}
        if(teams[1].step=="ready")pad.buttons=0x10;else pad.analog[0]=255;
        return;
    }
    if(scene=="STADIUM") {
        std::string selected;
        Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown && name=="NAME")selected=Text(node);});
        if(!venue.empty() && Normalize(selected)!=Normalize(venue)) {
            if(selected.empty())return;
            if(++searches>420){Stop("failed","Requested stadium was not found in native stadium selection");return;}
            pad.buttons=8;return;
        }
        if(target=="stadium"){Stop("reached","Stadium selector: "+venue);return;}
        pad.buttons=0x10;return;
    }
    if(scene=="SMARTOPTIONS" || scene=="SMARTOPTIONS2" || scene=="STARTINGPITCHER" || scene=="STARTINGLINEUP"){pad.buttons=0x10;return;}
    if(scene=="TEAMSMATCHUP") {
        seenMatchup=true;std::string selected;
        Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown && name=="STADIUM")selected=Text(node);});
        static std::string lastVenue;
        if(selected!=lastVenue){lastVenue=selected;LOGR("workshop launch: native stadium %s",selected.c_str());}
        venueVerified=venue.empty() || (!selected.empty() && Normalize(selected)==Normalize(venue));
        if(!selected.empty() && !venueVerified && now-sceneAt>1000){Stop("failed","Native stadium differs from target: "+selected);return;}
        if(target=="stadium" && venueVerified){Stop("reached","Stadium: "+selected);return;}
        if(now-sceneAt>4000 && venueVerified)pad.analog[0]=255;
        return;
    }
    if(scene=="LOADING1"){if(now-sceneAt>4000)pad.analog[0]=255;return;}
    if(scene=="HUDRIGHT" && seenMatchup && target=="exhibition" && venueVerified){Stop("reached",away+" at "+home);return;}
}
}

X86_EXTERN_FUNC(__imp__sub_000BBAC0);
X86_FUNC(sub_000BBAC0) { Observe(ctx.ecx); __imp__sub_000BBAC0(ctx,base); }
X86_EXTERN_FUNC(__imp__sub_000AFCE0);
X86_FUNC(sub_000AFCE0) { Observe(ctx.ecx); __imp__sub_000AFCE0(ctx,base); }
X86_EXTERN_FUNC(__imp__sub_000C0230);
X86_FUNC(sub_000C0230) {
    if(enabled){auto movie=kernel::R32(ctx.esp+4);if(movie==9 || movie==11)kernel::W32(ctx.esp+4,~0u);}
    __imp__sub_000C0230(ctx,base);
}
