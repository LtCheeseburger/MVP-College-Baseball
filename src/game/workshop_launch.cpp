#include "core/dev_tools.h"
// Opt-in Workshop startup routing. All state changes use native controller input;
// no guessed roster/stadium addresses or save data are written.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <chrono>
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
#include "common/overhaul.h"
#include "common/startup_ui.h"
#include "common/submenu_ui.h"
#include "common/team_select_ui.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "kernel/save_safety.h"
#include "game/phase9.h"
#include "game/pitch_clock.h"
#include "xdk/hle.h"

namespace {
std::mutex mutex;
std::atomic<bool> enabled{false};
std::string target, home, away, venue, scene, auditScene;
std::unordered_map<std::string,uint32_t> roots;
ULONGLONG started = 0, sceneAt = 0, nextPulse = 0, releaseAt = 0;
bool menuWrong = false;
bool audit = false;
int searches = 0;
int stadiumId = -1;
bool seenMatchup = false, venueVerified = false;
bool seasonRequested=false;
bool juneReady=false;
std::string seasonCheckpoint;
unsigned seasonStalls=0;
std::atomic<unsigned> dynastyYear{0};
std::atomic<bool> dynastySaved{false};
std::atomic<bool> awardsVerified{false};
bool DynastyTest(){return target=="dynasty" && dev::Env("MVP06_TEST_DYNASTY");}
bool SeasonTest(){return target=="continue" && dev::Env("MVP06_TEST_SEASON");}
unsigned SeasonTargetDay(){if(dev::Env("MVP06_TEST_SEASON_END"))return 30;if(dev::Env("MVP06_TEST_CALENDAR_JUNE"))return 7;const auto value=dev::Env("MVP06_TEST_SEASON_DAY");return value&&std::string(value)=="23"?23:31;}
std::string SeasonTargetMonth(){return dev::Env("MVP06_TEST_SEASON_END")||dev::Env("MVP06_TEST_CALENDAR_JUNE")?"JUN":"MAY";}
bool CalendarTest(){return target=="continue" && (dev::Env("MVP06_TEST_CALENDAR")||dev::Env("MVP06_TEST_CALENDAR_BOTTOM_ROW")||SeasonTest());}
bool RolloverTest(){return SeasonTest() && dev::Env("MVP06_TEST_ROLLOVER");}
bool DynastyPlayTest(){return target=="continue" && dev::Env("MVP06_TEST_DYNASTY_PLAY");}
bool PlayoffTest(){return target=="continue" && dev::Env("MVP06_TEST_PLAYOFF_TREE");}
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
    if(target=="continue")kernel::saves::LimitContinueTo({});
    std::ofstream(app::UserDir() / "workshop-launch-result.txt") << status << '\n' << message;
    if(audit)gpu::screenshot::Request();
    LOGR("workshop launch: %s: %s",status,message.c_str());
}
void Observe(uint32_t root) {
    const bool screenAudit=dev::Env("MVP06_UI_SCREEN_AUDIT");
    if((!enabled && !screenAudit) || !Valid(root))return;
    auto name=kernel::ReadCString(root+0x10,16);
    if(screenAudit){
        static const auto primary=[](){std::set<std::string> names;std::ifstream file(app::UserDir()/"ui-audit-scenes.txt");
            std::string line;while(std::getline(file,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();names.insert(line);}return names;}();
        std::lock_guard lock(mutex);roots[name]=root;
        if(primary.contains(name)&&(kernel::R16(root+0x28)&3)==3)auditScene=name;
    }
    if(!enabled)return;
    if(dev::Env("MVP06_TEST_SCHOOL_ORDER") && name=="FSELECTSCHOOL") {
        Stop("reached","Dynasty school navigation audit");
        return;
    }
    if(name=="CNTRLSELECT"){std::lock_guard lock(mutex);roots[name]=root;return;}
    bool normal=name=="TITLE" || name=="START" || name=="MAINMENU" || name=="TEAMSELECT" || name=="STADIUM" || name=="SMARTOPTIONS" || name=="SMARTOPTIONS2" || name=="STARTINGPITCHER" || name=="STARTINGLINEUP" || name=="LOADING1" || name=="TEAMSMATCHUP" || name=="HUDRIGHT";
    bool dynasty=(DynastyTest()||target=="continue") && (name=="GAMEMODES" || name=="FNEWLOAD" || name=="SAVESYS" || name=="FMGRDECISION" || name=="FOPTIONS" || name=="FGENERALOPTIONS" || name=="FGAMEOPTIONS" || name=="SELECTSCHOOL" || name=="DYNASTYHOME" || ((CalendarTest()||DynastyPlayTest()) && name=="CALENDAR"));
    if(PlayoffTest() && name=="PLAYOFFTREE")dynasty=true;
    // Simulation progress layers are rendered alongside the calendar even
    // when inactive; they must not continually reset its input debounce.
    if((SeasonTest()||DynastyPlayTest()) && (name=="FPLAYORSIMMULTI" || name=="FPLAYORSIM" || name=="FOFFSEASONMENU" || name=="CHAMPIONS" || name=="FRETIREMENT"))dynasty=true;
    // Rollover acceptance follows whichever native offseason screen is active.
    static bool rolloverHome=false;
    if(name=="DYNASTYHOME"||name=="FMGRDECISION")rolloverHome=true;
    if(RolloverTest() && rolloverHome && (name[0]=='F'||name=="CHAMPIONS") && !std::isdigit((unsigned char)name.back()) && name.rfind("FTICKER",0)!=0 && name!="FRIENDOPTIONS" && name!="FTOPBAR" && name!="FPLAYORSIMMULTI" && name!="FPLAYORSIM" && name!="NOTIFIEDSIMPROG" && name!="SIMPROGRESS" && name.rfind("POPUP",0)!=0 &&
       name!="NAVBAR" && name!="BACKMAIN" && name!="FTICKER" && name!="FTICKERMATTE" && name!="MBACKGROUND" && name!="BACKGROUND" && name!="COMMONBACK" &&
       name!="BACKRAY" && name!="TOPTITLE" && name!="FEARBAR" && name!="UNPLUGGEDCTRL" && name!="BACKIMAGE" && name!="MMVPBRAND" && name!="ANIMSLIDEIN" &&
       name!="NETWORKSTATUS" && name!="LOGOANIM" && name!="EAMESSENGER" && name!="SPORTSTRAX" && name!="MSGINTERFACE" && name!="TKEYBOARD")dynasty=true;
    if(DynastyTest() && dev::Env("MVP06_TEST_CREATE_EDIT") && name=="FSELECTSCHOOL")dynasty=true;
    if(!normal&&!dynasty){if(DynastyTest()||PlayoffTest()||SeasonTest()){std::lock_guard lock(mutex);static std::set<std::string> observed;if(observed.insert(name).second)LOGR("dynasty test: observed native root %s",name.c_str());}return;}
    std::lock_guard lock(mutex);roots[name]=root;
    if(scene!=name){scene=name;sceneAt=GetTickCount64();nextPulse=sceneAt+600;LOGR("workshop launch: scene %s",scene.c_str());}
}
}

namespace game {
void WorkshopLaunchInit() {
    bool badArguments=false;
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    for(int i=1;arguments && i<count;i++) {
        if(!std::wcscmp(arguments[i],L"--continue-dynasty")){target="continue";continue;}
        if(i+1>=count)break;
        if(dev::Enabled && !std::wcscmp(arguments[i],L"--workshop-launch-audit"))audit=true;
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
    if(badArguments || (target!="continue" && target!="mainmenu" && target!="team" && target!="stadium" && target!="exhibition" && !DynastyTest()) ||
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
    if(DynastyTest()||target=="continue"){if(name=="GAMEMODES")return true;menuWrong=true;return false;}
    if(name=="PLAYNOW")return true;
    menuWrong=true;return false; // Never activate another native menu action during automation.
}
void WorkshopLaunchPad(int port,input::Pad& pad) {
    // Detect real input before the overhaul adds its own native-title/intro
    // pulses. Those synthetic pulses must not cancel Workshop navigation.
    bool manual=false;
    if(port==0 && enabled) {
        auto raw=pad;KeyboardPad(port,raw);
        manual=raw.buttons || std::abs(int(raw.lx))>16000 || std::abs(int(raw.ly))>16000 || std::abs(int(raw.rx))>16000 || std::abs(int(raw.ry))>16000;
        for(auto value:raw.analog)manual|=value>30;
    }
    // Isolated hidden test runs drive only synthetic input: physical controller
    // state is discarded (keyboard input applies only to a focused window).
    if(enabled && dev::Env("MVP06_TEST_IGNORE_MANUAL"))pad=input::Pad{};
    // Developer screenshot audit: commands are confined to this test user directory.
    if(port==0 && dev::Env("MVP06_UI_SCREEN_AUDIT")) {
        static ULONGLONG checked=0,pressed=0,heldAt=0;
        static int heldTrigger=-1;
        static std::string command,last;
        const auto tick=GetTickCount64();
        if(tick-checked>=50){checked=tick;std::ifstream file(app::UserDir()/"ui-audit-command.txt");
            if(file)std::getline(file,command);else command.clear();}
        if(!command.empty()) {
            // An explicit audit command takes ownership from target navigation.
            // Otherwise an early command leaves the launch target's menu gate
            // active and silently prevents auditing unrelated frontend routes.
            if(enabled)Stop("audit", "Screen audit took control of navigation");
            pad=input::Pad{};pad.connected=true;
            const auto split=command.find(' ');
            const auto action=split==std::string::npos?std::string():command.substr(split+1);
            if(command!=last){
                last=command;pressed=tick;
                if(action=="HOLD_LT" || action=="HOLD_RT"){
                    heldTrigger=action=="HOLD_LT"?6:7;heldAt=tick;
                }
                if(action=="RELEASE")heldTrigger=-1;
                LOGR("UI audit: command=%s scene=%s",command.c_str(),auditScene.c_str());
                if(action=="CAPTURE"){
                    std::string text;
                    Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){
                        if(!shown)return;const auto value=Text(node);
                        if(!value.empty()&&text.size()<12000)text+=" ["+name+"="+value+"]";
                    });
                    LOGR("UI audit: visible %s:%s",auditScene.c_str(),text.c_str());
                    gpu::screenshot::Request();
                    gpu::g_capture=true; // Includes the Overhaul layer in presented.bmp.
                }
            }
            if(tick-pressed<130){
                if(action=="UP")pad.buttons=1;
                else if(action=="DOWN")pad.buttons=2;
                else if(action=="LEFT")pad.buttons=4;
                else if(action=="RIGHT")pad.buttons=8;
                else if(action=="START")pad.buttons=0x10;
                else if(action=="LCLICK")pad.buttons=0x40;
                else if(action=="RCLICK")pad.buttons=0x80;
                else if(action=="A")pad.analog[0]=255;
                else if(action=="B")pad.analog[1]=255;
                else if(action=="X")pad.analog[2]=255;
                else if(action=="Y")pad.analog[3]=255;
                else if(action=="BLACK")pad.analog[4]=255;
                else if(action=="WHITE")pad.analog[5]=255;
                else if(action=="LT")pad.analog[6]=255;
                else if(action=="RT")pad.analog[7]=255;
                if(action.find('+')!=std::string::npos){
                    size_t begin=0;
                    while(begin<action.size()){
                        const auto end=action.find('+',begin);const auto token=action.substr(begin,end==std::string::npos?end:end-begin);
                        if(token=="LT")pad.analog[6]=255;else if(token=="RT")pad.analog[7]=255;
                        else if(token=="UP")pad.buttons|=1;else if(token=="DOWN")pad.buttons|=2;
                        else if(token=="LEFT")pad.buttons|=4;else if(token=="RIGHT")pad.buttons|=8;
                        if(end==std::string::npos)break;begin=end+1;
                    }
                }
            }
            // Modifier must precede the direction press for native range selection.
            // Confine the hold to explicit audit input and expire it automatically.
            if(heldTrigger>=0 && tick-heldAt<30000)pad.analog[heldTrigger]=255;
            else heldTrigger=-1;
            // Use the same ownership and availability gates as real input.
            if(overhaul::Active())StartupPad(port,pad);else KeyboardPad(port,pad);
            return;
        }
    }
    if (overhaul::Active()) StartupPad(port,pad); else KeyboardPad(port,pad);
    // NCAA defensive conference allowance used: the pause menu's MOUND VISIT cannot be accepted.
    if(game::MoundVisitAcceptBlocked())pad.analog[0]=0;
    if(port!=0 || !enabled)return;
    std::lock_guard lock(mutex);auto now=GetTickCount64();
    if(target=="continue"){
        static bool checked=false;
        if(!checked){
            checked=true;kernel::saves::DynastySave save;
            if(!phase9::Enabled(phase9::Feature::SavesLauncher)||!kernel::saves::NewestDynasty(save)){Stop("failed","No compatible dynasty is available for Continue Dynasty");return;}
            kernel::saves::LimitContinueTo(save.slot);LOGR("Continue Dynasty: selected %s",save.slot.filename().string().c_str());
        }
    }
    if(now-started>(RolloverTest()?2400000u:(DynastyTest()||SeasonTest()||PlayoffTest()||dev::Env("MVP06_TEST_FIELD_PITCH")||dev::Env("MVP06_TEST_FIELD_CLOCK")||dev::Env("MVP06_TEST_FIELD_MOUND")||dev::Env("MVP06_TEST_FIELD_CPU"))?(dev::Env("MVP06_TEST_FIELD_CPU")?7200000u:900000u):DynastyPlayTest()?7200000u:180000u)){Stop("failed","Target timeout; automatic input stopped");return;}
    // Hidden isolated test runs read global input state; MVP06_TEST_IGNORE_MANUAL keeps
    // the user's activity in other windows from cancelling them.
    if(manual && !dev::Env("MVP06_TEST_IGNORE_MANUAL")){Stop("cancelled","Controller/keyboard input returned control to the user");return;}
    pad.connected=true;
    if(RolloverTest()){static ULONGLONG beat=0;if(now-beat>10000){beat=now;std::string b;std::vector<std::string> c;int s=0;const bool popup=game::InspectPopup(b,c,s);LOGR("rollover driver: heartbeat scene=%s popup=%d choices=%u body=%.80s",scene.c_str(),int(popup),unsigned(c.size()),b.c_str());}}
    // Dev: MVP06_TRACE_TEAM=<index> logs every change to that team's 30 roster slots with the scene.
    if(RolloverTest()&&dev::Env("MVP06_TRACE_TEAM")){
        static const unsigned watched=unsigned(std::atoi(dev::Env("MVP06_TRACE_TEAM")));static std::array<uint32_t,60> seen{};static bool primed=false;
        const auto world=kernel::R32(0x6c6540),teams=world?kernel::R32(world+0x34):0,first=teams?kernel::R32(teams+4):0;
        if(first&&teams&&watched<kernel::R32(teams+8)&&mem::IsCommitted(first+watched*0x2D8+0xC4+240)){
            for(unsigned i=0;i<60;++i){const auto v=kernel::R32(first+watched*0x2D8+0xC4+i*4);
                if(primed&&v!=seen[i])LOGR("team watch %u: scene=%s slot %u %s %08X -> %08X",watched,scene.c_str(),i/2,(i&1)?"status":"handle",seen[i],v);seen[i]=v;}
            primed=true;
        }
    }
    if(now<releaseAt)return;
    if(now<nextPulse)return;
    nextPulse=now+360;releaseAt=now+100;
    if(target=="continue"&&dev::Env("MVP06_TEST_DYNASTY_PLAY")){
        // Dev: from a loaded dynasty, request the next game with native Start, accept play prompts, let
        // MVP06_TEST_FIELD_CPU hand both teams to the CPU, then advance post-game screens back to dynasty home
        // so the native result-triggered autosave can run. The game itself is played by the native AI.
        static int phase=0;static ULONGLONG homeAt=0,offHud=0,lastA=0;static std::string lastScene;
        if(scene!=lastScene){lastScene=scene;LOGR("dynasty play: scene %s phase %d",scene.c_str(),phase);}
        {static ULONGLONG beat=0;if(now-beat>10000){beat=now;std::string b;std::vector<std::string> c;int sel=0;const bool pop=game::InspectPopup(b,c,sel);LOGR("dynasty play: heartbeat scene=%s phase=%d stable=%d popup=%d choices=%u body=%.60s",scene.c_str(),phase,int(phase9::DynastyMenuStable()),int(pop),unsigned(c.size()),b.c_str());}}
        if(scene=="HUDRIGHT"){static ULONGLONG fieldAt=0;if(phase<2){phase=2;fieldAt=now;LOGR("dynasty play: native game on field");}offHud=0;
            // MANAGE mode: the native AI plays both sides; the driver only captures progress.
            if(now-fieldAt>15000&&now-lastA>90000){lastA=now;gpu::screenshot::Request();}
}
        else if(phase==2){if(!offHud)offHud=now;if(now-offHud>45000){phase=3;LOGR("dynasty play: field play over; advancing post-game screens");}}
        if(phase==1||phase==3){
            std::string body;std::vector<std::string> choices;int selected=0;
            if(game::InspectPopup(body,choices,selected)&&!choices.empty()){
                static std::string last;std::string d=body;for(const auto& c:choices)d+=" | "+c;
                if(d!=last){last=d;LOGR("dynasty play dialog: %s",d.c_str());}
                int wanted=-1;
                for(unsigned i=0;i<choices.size()&&wanted<0;++i)if(Normalize(choices[i])=="GOTODAY")wanted=int(i);
                for(unsigned i=0;i<choices.size()&&wanted<0;++i)if(Normalize(choices[i]).find("PLAY")!=std::string::npos)wanted=int(i);
                for(unsigned i=0;i<choices.size()&&wanted<0;++i){auto c=Normalize(choices[i]);if(c=="YES"||c=="OK"||c.starts_with("CONTINUE"))wanted=int(i);}
                if(wanted<0)wanted=selected;
                if(selected!=wanted)pad.buttons=selected>wanted?1:2;else if(now-lastA>1500){lastA=now;pad.analog[0]=255;}
                return;
            }
        }
        if(scene=="FMGRDECISION"||scene=="DYNASTYHOME"){
            if(!phase9::DynastyMenuStable()){static ULONGLONG since=0;if(phase!=1){since=0;return;}if(!since)since=now;
                if(now-since>8000){since=now;static unsigned k=0;const bool useA=(++k)%2;if(useA)pad.analog[0]=255;else pad.analog[1]=255;LOGR("dynasty play: home unstable 8 s; native %s",useA?"A":"B");gpu::screenshot::Request();}return;}
            if(phase==1&&now-lastA>8000)phase=0; // calendar did not open; ask again
            if(phase==0){if(game::DynastyHomeSelectedRow()!=0){pad.buttons=1;return;}phase=1;lastA=now;pad.analog[0]=255;LOGR("dynasty play: opening native calendar (row %d)",game::DynastyHomeSelectedRow());return;}
            if(phase>=2){if(!homeAt){homeAt=now;LOGR("dynasty play: dynasty home reached after the played game");gpu::screenshot::Request();}
                if(now-homeAt>60000){gpu::screenshot::Request();Stop("reached","Played dynasty game returned to dynasty home");}}
            return;
        }
        if(phase==1&&scene=="CALENDAR"){if(now-lastA<1500)return;lastA=now;
                // Texas's next game in this save is the Friday June 4 regional (week 0, weekday 5).
                std::string month;unsigned week=99,weekday=99;
                Walk(roots["CALENDAR"],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown&&name=="MONTH")month=Text(node);if(shown&&name=="CELLHIGHLIGHTS"){week=kernel::R32(node+0x6C);weekday=kernel::R32(node+0x70);}});
                if(month.empty())return;
                if(month!="JUN"){pad.analog[7]=255;return;}
                if(week>=6||weekday>=7)return;
                if(weekday!=5){pad.buttons=weekday>5?4:8;return;}
                if(week!=0){pad.buttons=1;return;}
                pad.analog[0]=255;LOGR("dynasty play: Go To Day June 4");gpu::screenshot::Request();return;}
        if(phase==1&&scene=="TEAMSELECT"){ // controller to the user team (TEXAS), confirm, then Start
            if(now-lastA<1200)return;lastA=now;
            auto teams=Teams();int lane=Lane();if(teams[0].name.empty()||teams[1].name.empty()||lane==-2)return;
            const int side=Normalize(teams[0].name)=="TEXAS"?0:1;
            {static std::string last;auto st=teams[0].name+" | "+teams[1].name+" | "+std::to_string(lane)+" | "+teams[side].step;if(st!=last){last=st;LOGR("dynasty play: team select %s",st.c_str());}}
            if(lane!=side){pad.ly=side==0?32767:-32767;return;}
            if(teams[side].step=="ready")pad.buttons=0x10;else pad.analog[0]=255;return;}
        if(phase==1&&(scene=="SIMPROGRESS"||scene=="NOTIFIEDSIMPROG"))return;
        if(phase==1&&(scene=="FPLAYORSIM"||scene=="FPLAYORSIMMULTI")){static int pick=0;if(now-lastA<1500)return;lastA=now;++pick;
            if(pick<=3){pad.buttons=4;return;}
            if(scene=="FPLAYORSIM"&&pick==4){pad.buttons=8;return;} // PLAY | MANAGE | SIM: native AI plays both sides
            LOGR("dynasty play: %s chosen on native play-or-sim card",scene=="FPLAYORSIM"?"MANAGE":"PLAY");gpu::screenshot::Request();pad.analog[0]=255;pick=0;return;}
        if((phase==1||phase==3)&&scene!="HUDRIGHT"&&now-lastA>3000){lastA=now;pad.analog[0]=255;return;}
        if((phase==1||phase==3)&&scene!="HUDRIGHT")return;
    }
    if(SeasonTest() && scene=="CHAMPIONS"){pad.analog[0]=255;return;}
    if(SeasonTest() && !RolloverTest() && scene=="FRETIREMENT"){Stop("reached","Native season completed through champions and offseason retirement screen; recruiting, graduation and rollover not certified");return;}
    if(RolloverTest() && scene!="FPLAYORSIMMULTI" && scene!="FPLAYORSIM" && scene!="SIMPROGRESS" && scene!="NOTIFIEDSIMPROG" && scene!="CHAMPIONS" && scene!="FMGRDECISION" && scene!="DYNASTYHOME" && scene!="CALENDAR" && scene!="GAMEMODES" && scene!="FNEWLOAD" && scene!="SAVESYS" && scene!="TITLE" && scene!="START" && scene!="MAINMENU") {
        std::string body;std::vector<std::string> choices;int selected=0;
        if(game::InspectPopup(body,choices,selected)) {
            if(choices.empty())return;
            static std::string last;std::string description=body;
            for(const auto& choice:choices)description+=" | "+choice;
            if(description!=last){last=description;LOGR("rollover acceptance dialog on %s: %s",scene.c_str(),description.c_str());}
            int wanted=-1;
            for(unsigned i=0;i<choices.size()&&wanted<0;++i){
                const auto choice=Normalize(choices[i]);
                if(choice=="YES"||choice=="OK"||choice.starts_with("CONTINUE")||choice.starts_with("AUTO")||choice.starts_with("ACCEPT")||choice.starts_with("DONE")||choice.starts_with("ADVANCE"))wanted=int(i);
            }
            // Optional choices (for example a progression position change) keep the
            // native default by cancelling; anything else stops the test.
            for(unsigned i=0;i<choices.size()&&wanted<0;++i)if(Normalize(choices[i])=="CANCEL")wanted=int(i);
            if(wanted<0){Stop("failed","Rollover test stopped at unrecognized native dialog: "+description);return;}
            if(selected!=wanted)pad.buttons=selected>wanted?1:2;else pad.analog[0]=255;
            return;
        }
        static std::string dumped;static unsigned presses=0;
        if(dumped!=scene){
            dumped=scene;presses=0;std::string text;
            Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(!shown)return;auto value=Text(node);if(!value.empty()&&text.size()<1500)text+=" ["+name+"="+value+"]";});
            LOGR("rollover acceptance: screen %s:%s",scene.c_str(),text.c_str());
            gpu::screenshot::Request();
        }
        if(now-sceneAt<2500)return;
        if(scene=="FPROMOTEDEMOTE"){
            // Native roster bounds: sign incoming players (A) until the 25-player
            // minimum is met, then advance (Start). The count is the native label.
            std::string count;
            Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown&&name=="PLAYERCOUNT")count=Text(node);});
            const auto slash=count.find('/');
            const int players=slash==std::string::npos?-1:std::atoi(count.c_str());
            static std::string lastCount;
            if(count!=lastCount){lastCount=count;LOGR("rollover acceptance: native roster count %s",count.c_str());}
            if(players>=0&&players<25){if(++presses>120){Stop("failed","Rollover test could not reach the native 25-player minimum: "+count);return;}pad.analog[0]=255;return;}
        }
        if(++presses>80){Stop("failed","Rollover test could not leave native screen "+scene);return;}
        // Native offseason screens advance with Start; B leaves an opened
        // sub-page; A is tried occasionally for screens that only confirm.
        if(presses%10==5)pad.analog[0]=255;else if(presses%10==9)pad.analog[1]=255;else pad.buttons=0x10;
        return;
    }
    if(SeasonTest() && (scene=="FMGRDECISION" || scene=="DYNASTYHOME" || scene=="CALENDAR" || scene=="FPLAYORSIMMULTI" || scene=="FPLAYORSIM" || scene=="SIMPROGRESS" || scene=="NOTIFIEDSIMPROG" || scene=="FOFFSEASONMENU")) {
        std::string body;std::vector<std::string> choices;int selected=0;
        if(game::InspectPopup(body,choices,selected)) {
            // Native save scanning/loading uses a transient modal with no actions.
            if(choices.empty())return;
            static std::string last;
            std::string description=body;
            for(const auto& choice:choices)description+=" | "+choice;
            if(description!=last){last=description;LOGR("season acceptance dialog: %s",description.c_str());}
            int wanted=-1;
            for(unsigned i=0;i<choices.size()&&wanted<0;++i){
                const auto choice=Normalize(choices[i]);
                if(choice=="YES" || choice=="OK" || choice.starts_with("CONTINUE") || choice.starts_with("AUTO"))wanted=int(i);
            }
            if(wanted<0){Stop("failed","Season test stopped at unrecognized native dialog: "+description);return;}
            if(selected!=wanted)pad.buttons=selected>wanted?1:2;else pad.analog[0]=255;
            return;
        }
        if(scene=="FOFFSEASONMENU"){Stop("reached","Native season reached offseason menu; signing and rollover not yet verified");return;}
        if(seasonRequested) {
            if(!phase9::DynastyMenuStable())return;
            if(scene=="CALENDAR"){pad.analog[1]=255;return;}
            if(scene=="FMGRDECISION" || scene=="DYNASTYHOME") {
                std::string date;
                Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown&&name=="DATE")date=Text(node);});
                LOGR("season acceptance: returned to native dynasty home date=%s",date.c_str());
                const auto normalized=Normalize(date);
                constexpr const char* monthNames[]={"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE","JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
                unsigned reachedMonth=0,reachedDay=0;
                for(unsigned m=0;m<12;++m)if(auto at=normalized.find(monthNames[m]);at!=std::string::npos){
                    reachedMonth=m+1;const auto digits=normalized.substr(at+std::strlen(monthNames[m]));
                    reachedDay=unsigned(std::atoi(digits.c_str()));break;
                }
                const unsigned targetMonth=SeasonTargetMonth()=="JUN"?6:5;
                const bool reachedTarget=reachedMonth>targetMonth || (reachedMonth==targetMonth && reachedDay>=SeasonTargetDay());
                if(!reachedTarget){
                    if(date.empty() || (date==seasonCheckpoint && ++seasonStalls>=3)){Stop("failed","Native simulation stopped advancing at: "+date);return;}
                    if(date!=seasonCheckpoint)seasonStalls=0;
                    seasonCheckpoint=date;seasonRequested=false;
                    LOGR("season acceptance: continuing after native scheduling checkpoint %s",date.c_str());return;
                }
                Stop("reached","Native calendar simulation returned to dynasty home: "+date+"; full rollover not certified");return;
            }
        }
    }
    if(target=="continue"){
        if(scene=="GAMEMODES"){if(game::SelectedSubmenuRow()!=1)pad.buttons=2;else pad.analog[0]=255;return;}
        if(scene=="FNEWLOAD"){if(game::SelectedSubmenuRow()!=1)pad.buttons=2;else pad.analog[0]=255;return;}
        if(scene=="SAVESYS"){pad.analog[0]=255;return;}
        if(PlayoffTest() && scene=="PLAYOFFTREE"){
            if(now-sceneAt<3000)return;
            if(dev::Env("MVP06_TEST_TREE_STRESS")){
                static unsigned presses=0;
                if(presses>=384){Stop("reached","384 repeated native bracket shoulder presses completed");return;}
                const unsigned buttons[]={6,7,5,4};pad.analog[buttons[presses/96]]=255;++presses;
                if(presses%24==0)LOGR("tournament stress: shoulder presses=%u",presses);
                return;
            }
            Stop("reached","Native postseason tree opened from loaded dynasty");return;
        }
        if(scene=="FMGRDECISION"||scene=="DYNASTYHOME"){
            if(dev::Env("MVP06_TEST_CALENDAR_JUNE")){
                std::string date;Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown&&name=="DATE")date=Text(node);});
                const auto current=Normalize(date);juneReady=current.find("MAY31")!=std::string::npos||current.find("JUNE")!=std::string::npos;
            }
            if(RolloverTest()){
                std::string date;
                Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown&&name=="DATE")date=Text(node);});
                static std::string lastDate;static ULONGLONG yearAt=0;
                if(date!=lastDate){lastDate=date;LOGR("rollover acceptance: dynasty home date=%s",date.c_str());}
                if(date.find("2028")!=std::string::npos){
                    if(!yearAt){yearAt=now;LOGR("rollover acceptance: native next-year dynasty home reached");}
                    if(now-yearAt>30000){Stop("reached","Native dynasty rolled over to 2028: "+date);return;}
                    return;
                }
            }
            if(SeasonTest() && dev::Env("MVP06_TEST_SEASON_END")){
                if(phase9::DynastyMenuStable())pad.buttons=0x10; // native Start: simulate the current postseason phase
                return;
            }else if(PlayoffTest()){
                if(!phase9::DynastyMenuStable())return;
                if(game::DynastyHomeSelectedRow()!=1)pad.buttons=1;
                else {LOGR("playoff test: selecting postseason tree");pad.analog[0]=255;}
            }else if(CalendarTest()){
                if(!phase9::DynastyMenuStable())return;
                if(game::DynastyHomeSelectedRow()!=0)pad.buttons=1;else pad.analog[0]=255;
            }else{if(!phase9::DynastyMenuStable())return;std::string date;Walk(roots[scene],[&](auto node,auto& name,auto,auto,auto&,bool shown){if(shown&&name=="DATE")date=Text(node);});if(date.empty())return;Stop("reached","Continue Dynasty loaded the selected native save: "+date);}
            return;
        }
        if(CalendarTest() && scene=="CALENDAR"){
            std::string month,year;
            Walk(roots["CALENDAR"],[&](auto node,auto& name,auto,auto,auto&,bool shown){
                if(shown && name=="MONTH")month=Text(node);
                if(shown && name=="YEAR")year=Text(node);
            });
            if(month.empty() || year.empty())return;
            if(SeasonTest()) {
                static std::string lastDate;
                if(lastDate!=month+year){lastDate=month+year;LOGR("season acceptance: viewing %s %s",month.c_str(),year.c_str());}
                // Follow the native month spinner; June is reachable before qualification.
                const bool juneTest=dev::Env("MVP06_TEST_CALENDAR_JUNE")!=nullptr;
                const auto destinationMonth=juneTest&&!juneReady?std::string("MAY"):SeasonTargetMonth();
                if(month!=destinationMonth){pad.analog[7]=255;return;}
                const unsigned monthNumber=destinationMonth=="JUN"?6:5;
                unsigned destinationDay=juneTest&&!juneReady?31:SeasonTargetDay();
                if(dev::Env("MVP06_TEST_SEASON_END")){
                    destinationDay=0;for(unsigned d=1;d<=30;++d)if(game::DynastyDateAvailable(unsigned(std::stoi(year)),6,d))destinationDay=d;
                    if(!destinationDay)return;
                }
                const auto first=std::chrono::sys_days{std::chrono::year{std::stoi(year)}/std::chrono::month{monthNumber}/1};
                const auto targetCell=std::chrono::weekday{first}.c_encoding()+destinationDay-1;
                const unsigned targetWeek=targetCell/7,targetDay=targetCell%7;
                unsigned week=99,weekday=99;
                Walk(roots["CALENDAR"],[&](auto node,auto& name,auto,auto,auto&,bool shown){
                    if(shown&&name=="CELLHIGHLIGHTS"){week=kernel::R32(node+0x6C);weekday=kernel::R32(node+0x70);}
                });
                if(week>=6||weekday>=7||targetWeek>=6)return;
                LOGR("season acceptance: calendar %s %s cell=%u,%u target=%u,%u",month.c_str(),year.c_str(),weekday,week,targetDay,targetWeek);
                if(week==0&&targetWeek>0)pad.buttons=2;
                else if(weekday!=targetDay)pad.buttons=weekday>targetDay?4:8;
                else if(week!=targetWeek)pad.buttons=week>targetWeek?1:2;
                else {seasonRequested=true;pad.analog[3]=255;} // native Y: Sim to Date
                return;
            }
            if(dev::Env("MVP06_TEST_CALENDAR_BOTTOM_ROW")){
                if(month!="MAY"){pad.analog[7]=255;return;}
                unsigned week=99,weekday=99;
                Walk(roots["CALENDAR"],[&](auto node,auto& name,auto,auto,auto&,bool shown){
                    if(shown&&name=="CELLHIGHLIGHTS"){week=kernel::R32(node+0x6C);weekday=kernel::R32(node+0x70);}
                });
                if(week>=6||weekday>=7)return;
                std::string nativeDay;
                const auto cell="X"+std::to_string(weekday)+"Y"+std::to_string(week);
                Walk(roots["CALENDAR"],[&](auto node,auto& name,auto level,auto depth,auto& ancestors,bool shown){
                    if(name=="DAY")for(auto l=depth+1;l<level;++l)
                        if(ancestors[l]==cell)nativeDay=Text(node);
                });
                static bool sunday=false, captured=false;
                if(week==5&&!captured){gpu::screenshot::Request();captured=true;}
                if(week==5&&weekday==0&&nativeDay=="30")sunday=true;
                if(sunday&&week==5&&weekday==1&&nativeDay=="31"){Stop("reached","Native input and day text verified for May 30 and May 31 on calendar week six");return;}
                LOGR("calendar bottom test: native week=%u weekday=%u day=%s",week,weekday,nativeDay.c_str());
                if(week==0)pad.buttons=2;
                else if(weekday!=(sunday?1u:0u))pad.buttons=weekday>(sunday?1u:0u)?4:8;
                else if(week<5)pad.buttons=2;
                else pad.buttons=8;
                return;
            }
            static std::string previous;static unsigned changes=0;static bool june=false;
            const auto date=month+" "+year;
            if(date!=previous){
                if(!previous.empty())++changes;
                previous=date;LOGR("calendar test: month %u %s",changes,date.c_str());
                if(month=="JUN"){june=true;gpu::screenshot::Request();gpu::g_capture=true;}
            }
            if(changes>=16){Stop(june?"reached":"failed",june?"Native calendar reached June and completed 16 month changes in both directions":"Native calendar never reached June");return;}
            pad.analog[changes<8?7:6]=255;
            return;
        }
        if(scene=="SMARTOPTIONS"||scene=="SMARTOPTIONS2"||scene=="SELECTSCHOOL"){Stop("failed","Continue Dynasty stopped at an unexpected new-dynasty screen");return;}
    }
    if(DynastyTest()){
        if(dynastyYear&&(!dev::Env("MVP06_TEST_NATIVE_SAVE")||dynastySaved)&&(!dev::Env("MVP06_TEST_AWARDS")||awardsVerified)){Stop("reached","Native dynasty annual schedule verified: "+std::to_string(dynastyYear.load())+(dynastySaved?"; native save published":"")+(awardsVerified?"; native awards collected":""));return;}
        if(scene=="GAMEMODES"){if(game::SelectedSubmenuRow()!=1)pad.buttons=2;else pad.analog[0]=255;return;}
        if(scene=="FNEWLOAD"){pad.analog[0]=255;return;}
        if(scene=="FOPTIONS" || scene=="FGENERALOPTIONS" || scene=="FGAMEOPTIONS"){pad.buttons=0x10;return;}
        if(dev::Env("MVP06_TEST_CREATE_EDIT") && scene=="FSELECTSCHOOL"){
            std::string school;Walk(roots[scene],[&](auto n,auto& name,auto,auto,auto&,bool shown){if(shown && name=="TEAMNAME")school=Text(n);});
            if(Normalize(school)==Normalize(home))pad.analog[0]=255;else pad.buttons=8;
            return;
        }
        if(scene=="SELECTSCHOOL"){
            if(dev::Env("MVP06_TEST_SCHOOL_ORDER"))Stop("reached","Dynasty school navigation audit");
            else pad.analog[0]=255;
            return;
        }
    }
    if(scene=="TITLE" || scene=="START"){pad.buttons=0x10;if(RolloverTest()){static ULONGLONG lastLog=0;if(now-lastLog>3000){lastLog=now;LOGR("rollover driver: Start pulse on %s port %d",scene.c_str(),port);}}return;}
    if(scene=="MAINMENU") {
        // The native menu exists while the themed intro still owns input.
        // Wait for its handoff before reporting readiness or navigating away.
        if(overhaul::Active() && !StartupMainMenuReady())return;
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
    static bool fieldHud=false;
    if(dev::Env("MVP06_TEST_FIELD_CLOCK")){static std::string loggedScene;if(scene!=loggedScene){loggedScene=scene;LOGR("field clock driver: scene %s matchup %d venue %d hud %d",scene.c_str(),int(seenMatchup),int(venueVerified),int(fieldHud));}}
    if(scene=="HUDRIGHT" && seenMatchup && target=="exhibition" && venueVerified)fieldHud=true;if(scene=="HUDRIGHT"&&target=="continue"&&dev::Env("MVP06_TEST_DYNASTY_PLAY"))fieldHud=true;
    if(fieldHud||(scene=="HUDRIGHT"&&seenMatchup)){
            // Dev-only guest memory snapshots for locating field data (va + page records).
            {static int dumps=0;static ULONGLONG dumpAt=0;if(!dumpAt)dumpAt=now;const char* dp=std::getenv("MVP06_TEST_DUMP_MEM");
             if(dp&&*dp&&dumps<3&&now-dumpAt>60000u+60000u*unsigned(dumps)){
                std::ofstream f(std::string(dp)+std::to_string(dumps)+".bin",std::ios::binary);
                auto span=[&](uint32_t lo,uint32_t hi){for(uint64_t v=lo;v<hi;v+=mem::kPage){uint32_t va=uint32_t(v);if(!mem::IsCommitted(va))continue;
                    f.write(reinterpret_cast<const char*>(&va),4);f.write(reinterpret_cast<const char*>(mem::Base()+va),mem::kPage);}};
                span(mem::kUserLow,0x20000000u);span(mem::kContiguousBase,mem::kContiguousBase+mem::kPhysicalSize);span(mem::kSystemBase,mem::kSystemBase+mem::kSystemSize);
                LOGR("dynasty play: memory snapshot %d written",dumps);++dumps;}}}
    if((scene=="HUDRIGHT" && seenMatchup && target=="exhibition" && venueVerified) || (fieldHud && (dev::Env("MVP06_TEST_FIELD_CLOCK")||dev::Env("MVP06_TEST_FIELD_PITCH")||dev::Env("MVP06_TEST_FIELD_MOUND")||dev::Env("MVP06_TEST_FIELD_CPU")))){
        if(dev::Env("MVP06_TEST_FIELD_CLOCK")){
            static unsigned shots=0;static ULONGLONG hudStart=0;if(!hudStart)hudStart=now;const auto sceneAt=hudStart;
            if(now-sceneAt>6000+shots*15000 && shots<14){gpu::screenshot::Request();++shots;}
            {static int hudCaptures=0;static ULONGLONG lastCapture=0;const int left=game::PitchClockSeconds();
             if(hudCaptures==2&&game::PitchClockNotice()){gpu::g_capture=true;++hudCaptures;LOGR("field clock acceptance: presented-frame capture of violation notice");}
             if(hudCaptures<2&&left>=0&&left<=(hudCaptures?6:25)&&now-lastCapture>5000){gpu::g_capture=true;++hudCaptures;lastCapture=now;LOGR("field clock acceptance: presented-frame capture with %d s shown",left);}}
            if(dev::Env("MVP06_TEST_FIELD_PAUSE")){
                static int pauseStep=0;
                if(pauseStep==0&&now-sceneAt>10000){pad.buttons=0x10;pauseStep=1;LOGR("field clock acceptance: native pause requested");return;}
                if(pauseStep==1&&now-sceneAt>14000){gpu::screenshot::Request();pauseStep=2;return;}
                if(pauseStep==2&&now-sceneAt>40000){pad.buttons=0x10;pauseStep=3;LOGR("field clock acceptance: native resume requested");return;}
            }
            if(now-sceneAt<200000)return;
            Stop("reached","Native field idle for 200 seconds without pitch input; pitch-clock enforcement observed in log");return;
        }
        if(dev::Env("MVP06_TEST_FIELD_CPU")){
            // Dev: hand both teams to the CPU through the native pause menu (SELECT CONTROLLER), then leave the game to
            // play itself; MVP06_TEST_FIELD_CPU_DIR picks the controller move (4 left, 8 right).
            static ULONGLONG next=0;static int step=0;if(!next)next=now+8000;
            if(now<next)return;next=now+1200;++step;
            const uint16_t dir=uint16_t(std::atoi(dev::Env("MVP06_TEST_FIELD_CPU_DIR")?dev::Env("MVP06_TEST_FIELD_CPU_DIR"):"4"));
            if(step==1){pad.buttons=0x10;LOGR("cpu driver: pause");return;}
            if(step>=2&&step<=4){pad.buttons=2;return;}
            if(step==5){pad.analog[0]=255;LOGR("cpu driver: select controller");return;}
            if(step==7){pad.buttons=dir;LOGR("cpu driver: controller moved %u",dir);return;}
            if(step==9){pad.buttons=0x10;LOGR("cpu driver: resume requested");return;}
            if(step>9&&step%50==0)gpu::screenshot::Request();
            {static ULONGLONG ended=0;if(game::TenRunEnded()&&!ended){ended=now;LOGR("cpu driver: native ten-run ending observed");gpu::screenshot::Request();}
             if(ended&&now-ended>30000){gpu::screenshot::Request();Stop("reached","CPU-vs-CPU game ended by the native ten-run rule");return;}}
            return;
        }
        if(dev::Env("MVP06_TEST_FIELD_MOUND")){
            // Dev: request eight mound visits through the native pause menu (Start, ROSTER OPTIONS, PITCHING/DEFENSE,
            // MOUND VISIT, each the menu's default row) to exercise the defensive-conference allowance.
            static ULONGLONG next=0;static int step=0;if(!next)next=now+8000;
            if(now<next)return;
            const int visit=step/5,phase=step%5;++step;next=now+(phase==4?14000:1500);
            if(visit>=8){Stop("reached","Eight mound visits requested through the native pause menu");return;}
            if(phase==0){pad.buttons=0x10;LOGR("mound driver: visit %d requested",visit+1);return;}
            if(phase<4){if(game::MoundVisitAcceptBlocked()){LOGR("mound driver: MOUND VISIT accept blocked");return;}pad.analog[0]=255;return;}
            if(phase==4&&visit==6)gpu::screenshot::Request();
            return;
        }
        if(dev::Env("MVP06_TEST_FIELD_PITCH")){
            static ULONGLONG pitchStart=0;static unsigned pitchShots=0;if(!pitchStart)pitchStart=now;
            if(now-pitchStart>3000+pitchShots*9000 && pitchShots<16){gpu::screenshot::Request();++pitchShots;}
            if(now-pitchStart<150000){if((now/4000)%2)pad.analog[0]=255;return;}
            Stop("reached","Native field pitched via controller input for 150 seconds");return;
        }
        if(dev::Env("MVP06_TEST_FIELD_IDLE")){
            static bool captured=false;
            if(!captured&&now-sceneAt>6000){gpu::screenshot::Request();captured=true;LOGR("field idle acceptance: native HUD baseline captured; no controller pitch input");}
            if(now-sceneAt<40000)return;
            Stop("reached","Native field remained responsive for 40 seconds without pitch input; inspect screenshots for clock enforcement");return;
        }
        Stop("reached",away+" at "+home);return;
    }
}
}

namespace game {
void WorkshopDynastyCreated(unsigned year){if(DynastyTest())dynastyYear=year;}
void WorkshopDynastySaved(){if(DynastyTest())dynastySaved=true;}
void WorkshopAwardsVerified(){if(DynastyTest())awardsVerified=true;}
void WorkshopObserve(uint32_t root) { Observe(root); }
void WorkshopMovie(X86Context& ctx) {
    if(enabled){auto movie=kernel::R32(ctx.esp+4);if(movie==9 || movie==11)kernel::W32(ctx.esp+4,~0u);}
}
}


