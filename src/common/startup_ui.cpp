#include "core/dev_tools.h"
#include "roster_browser_ui.h"
#include "startup_ui.h"
#include "overhaul.h"
#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "game_hooks.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "intro_video.h"
#include "submenu_ui.h"
#include "ui_style.h"
#include "team_select_ui.h"
#include "matchup_loading_ui.h"
#include "stadium_intro_ui.h"
#include "mvp_transition_ui.h"
#include "pitcher_info_ui.h"
#include "batter_info_ui.h"
#include "pause_menu_ui.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace game {
namespace {
std::atomic<int> phase{0};
std::atomic<double> began{0}, ready{0};
std::atomic<int> menuRow{0};
std::atomic<bool> menuPresent{false}, introPlayed{false};
std::atomic<bool> returnFromSubmenu{false};
std::atomic<bool> leafFromSubmenu{false};
std::atomic<bool> dynastySchoolVisible{false};
bool matchupAuditGameplay=false;
std::atomic<double> teamManagementAuditAt{0},optionsAuditAt{0};
std::atomic<double> dynastyHomeAuditAt{0};
std::atomic<double> startingPitcherAuditAt{0},startingLineupAuditAt{0},finalPlaySimAuditAt{0},coachAuditAt{0},simGameAuditAt{0},finalScoreAuditAt{0},recruitingAuditAt{0},statsLandingAuditAt{0},statisticsMenuAuditAt{0};
double pauseAuditGameplayAt=0;
double Now() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
float Fade(double t, double a, double d) {
  return float(std::clamp((t - a) / d, 0., 1.));
}
std::filesystem::path Assets() {
  return overhaul::Root();
}
} // namespace
void StartupTimeline(const char *name) {
  // Animation lookups can describe outgoing/restored timelines rather than
  // the active grid cursor. PollNativeMainMenu owns the displayed selection.
}
void StartupMainMenuSelection(int row) {
  if (phase.load() != 5 || SubmenuPopupVisible() || row < 0 || row > 7)
    return;
  if (menuRow.exchange(row) != row && dev::Env("MVP06_TRACE_FE"))
    LOGR("main menu native selection: %d", row);
}
void StartupReady() {
  if (phase == 2 && ready.load() == 0)
    ready = Now();
}
bool StartupMainMenuReady() {
  return phase.load() == 5 && Now() - began.load() >= .7;
}
void StartupPad(int port, input::Pad &pad) {
  KeyboardPad(port, pad);
  if(HandleTutorialNoticePad(port,pad))return;
  // The roster validator supplies a No answer but no native B callback.
  // Back must decline optimization, including when Yes was highlighted.
  static bool rosterCancelPending[4]{},rosterCancelHeld[4]{},rosterCancelArmed[4]{};
  static double rosterAcceptUntil[4]{};
  if(port>=0 && port<4 && overhaul::Active()){
    if(rosterAcceptUntil[port]>Now() && SubmenuPopupVisible()){
      pad.buttons=0;pad.lx=pad.ly=0;for(auto& value:pad.analog)value=0;pad.analog[0]=255;return;
    }
    if(rosterCancelHeld[port]){
      if(!pad.analog[0] && !pad.analog[1])rosterCancelHeld[port]=false;
      pad.buttons=0;pad.lx=pad.ly=0;for(auto& value:pad.analog)value=0;return;
    }
    std::string body;std::vector<std::string> choices;int selected=0;
    const bool validation=InspectPopup(body,choices,selected) && choices.size()==2 &&
      choices[0]=="No" && choices[1]=="Yes" && body.find("optimize")!=std::string::npos && body.find("CPU")!=std::string::npos;
    if(!validation){rosterCancelPending[port]=false;rosterCancelArmed[port]=false;}
    else if(!pad.analog[1] && !rosterCancelPending[port])rosterCancelArmed[port]=true;
    else if((pad.analog[1] && rosterCancelArmed[port]) || rosterCancelPending[port]){
      rosterCancelPending[port]=true;pad.buttons=0;pad.lx=pad.ly=0;for(auto& value:pad.analog)value=0;
      if(selected>0)pad.buttons=1;
      else {pad.analog[0]=255;rosterAcceptUntil[port]=Now()+.15;rosterCancelPending[port]=false;rosterCancelHeld[port]=true;}
      return;
    }
  }
  static bool onlineDismissHeld[4]{};
  if(port>=0 && port<4 && onlineDismissHeld[port]){
    if(!pad.analog[0] && !pad.analog[1])onlineDismissHeld[port]=false;
    pad.buttons=0;pad.lx=pad.ly=0;for(auto& value:pad.analog)value=0;return;
  }
  if(overhaul::Active() && LegacyOnlineNotice()){
    // This native confirmation has no B/cancel callback. Accept its No row,
    // keeping the original flow's cleanup without entering the network backend.
    const auto dismiss=std::max(pad.analog[0],pad.analog[1]);
    pad.buttons=0;pad.lx=pad.ly=0;for(auto& value:pad.analog)value=0;
    std::string text;std::vector<std::string> choices;int selected=0;
    InspectPopup(text,choices,selected);
    if(dismiss){if(selected>0)pad.buttons=1;else{pad.analog[0]=dismiss;if(port>=0 && port<4)onlineDismissHeld[port]=true;}}
    return;
  }
  if(port==0 && dev::Env("MVP06_TEST_BUTTON_ICONS"))pad.connected=true;
  MapDynastyPickerConfirm(port,pad);
  if(!dev::Env("MVP06_TEST_TEAM_MANAGEMENT"))MapTeamManagementPad(port,pad);
  if(!dev::Env("MVP06_TEST_CREATE_EDIT"))MapPlaySimPad(port, pad);
  if(!dev::Env("MVP06_TEST_STATISTICS_MENU"))MapStatisticsMenuPad(port,pad);
  if(!dev::Env("MVP06_TEST_TEAM_MANAGEMENT") && !dev::Env("MVP06_TEST_CREATE_EDIT") && !dev::Env("MVP06_TEST_STATISTICS_MENU"))MapDashboardPad(port,pad);
  if(port==0 && dev::Env("MVP06_TEST_CREATE_EDIT") && teamManagementAuditAt.load()>0){
   const double e=Now()-teamManagementAuditAt.load();pad.connected=true;pad.buttons=0;std::fill(std::begin(pad.analog),std::end(pad.analog),0);
   if(e<3 && std::fmod(e,.45)<.12 && TeamManagementSelection()>=0 && TeamManagementSelection()!=4)pad.buttons=8;
   if(e>4 && e<4.14)pad.analog[0]=255;
   if(e>6 && e<6.14)pad.buttons=8;if(e>7 && e<7.14)pad.buttons=4;
   if(!std::strcmp(dev::Env("MVP06_TEST_CREATE_EDIT"),"edit") && e>8 && e<8.14)pad.buttons=8;
   if(e>9 && e<9.14)pad.analog[0]=255;
   static double rosterAt=0,editorAt=0;
   if(e>10 && CreateEditAuditStage()==2 && !rosterAt)rosterAt=Now();
   if(rosterAt && Now()-rosterAt>1 && Now()-rosterAt<1.14)pad.analog[0]=255;
   if(e>10 && CreateEditAuditStage()==3 && !PlayerEditorLoading() && !editorAt)editorAt=Now();
   if(editorAt){double f=Now()-editorAt;for(double at:{2.,4.})if(f>at && f<at+.14)pad.analog[7]=255;for(double at:{6.,8.})if(f>at && f<at+.14)pad.buttons=2;}

   MapTeamManagementPad(port,pad);MapPlaySimPad(port,pad);return;
  }
  if(port==0 && dev::Env("MVP06_TEST_UI_ISSUES") && teamManagementAuditAt.load()>0){
   const double e=Now()-teamManagementAuditAt.load();pad.connected=true;pad.buttons=0;std::fill(std::begin(pad.analog),std::end(pad.analog),0);
   if(e<2 && std::fmod(e,.45)<.12 && TeamManagementSelection()>=0 && TeamManagementSelection()!=5)pad.buttons=2;
   for(double at:{3.,7.,16.})if(e>at && e<at+.14)pad.analog[0]=255;
   if(e>5 && e<5.14)pad.buttons=2;if(e>6 && e<6.14)pad.buttons=1;
   if(e>11 && e<11.14)pad.analog[1]=255;
   for(double at:{21.,29.})if(e>at && e<at+.14)pad.buttons=1;
   for(double at:{18.,31.})if(e>at && e<at+.14)pad.buttons=8;
   for(double at:{19.,30.})if(e>at && e<at+.14)pad.buttons=2;
   for(double at:{20.,28.})if(e>at && e<at+.14)pad.buttons=4;
   if(e>24 && e<25.2)pad.buttons=8;
   MapTeamManagementPad(port,pad);MapStatisticsMenuPad(port,pad);MapPlaySimPad(port,pad);
   return;
  }
  if (port == 0 && dev::Env("MVP06_TEST_SCHOOL_ORDER") &&
      phase.load() == 10 && dynastySchoolVisible.load()) {
    static double entered = Now();
    const double elapsed = Now() - entered;
    const double interval=dev::Env("MVP06_TEST_PICKER_SWEEP")?.30:.45;
    const int pulse=int(elapsed/interval);
    pad.connected=true;pad.buttons=0;std::fill(std::begin(pad.analog),std::end(pad.analog),0);
    if(elapsed/interval-pulse<.4){
      if(dev::Env("MVP06_TEST_PICKER_SWEEP")){
        if(pulse<320){int move=pulse%10;if(move<6)pad.buttons=2;else if(move<9)pad.buttons=1;else pad.analog[7]=255;}
      }else{
        if(pulse<72){int move=pulse%18;if(move<12)pad.buttons=2;else if(move<16)pad.buttons=1;else pad.analog[7]=255;}
        else if(pulse<108)pad.analog[7]=255;else if(pulse<144)pad.analog[6]=255;else if(pulse==146)pad.analog[0]=255;
      }
    }
    static bool captured = false;
    if (pulse >= 144 && !captured) { captured = true; gpu::g_capture = true; }
  }
  if (phase.load() == 10 && dynastySchoolVisible.load() && pad.connected) {
    // The native school picker moves between teams horizontally. Translate
    // vertical list navigation into that same input so A confirms the native team.
    constexpr uint16_t up = 0x01, down = 0x02, left = 0x04, right = 0x08;
    const auto vertical = pad.buttons & (up | down);
    if (vertical) {
      pad.buttons &= ~uint16_t(up | down | left | right);
      if (vertical == up) pad.buttons |= right;
      if (vertical == down) pad.buttons |= left;
    }
    if (std::abs(int(pad.ly)) > std::abs(int(pad.lx)))
      pad.lx = pad.ly;
    pad.ly = 0;
  }
  if (port != 0) {
    if(phase.load()==8)MapTeamSelectPad(port,pad,Now()-began.load());
    return;
  }
  if(dev::Env("MVP06_TEST_SIM_GAME") && !dev::Env("MVP06_TEST_FINAL_SCORE") && simGameAuditAt.load()>0){
    const double t=Now()-simGameAuditAt.load();
    for(double at:{1.,5.})if(t>at && t<at+.14)pad.buttons|=4;
    if(t>3 && t<3.14)pad.buttons|=8;
    for(double at:{7.,9.})if(t>at && t<at+.14)pad.analog[2]=255;
    for(double at:{11.,15.})if(t>at && t<at+.14)pad.analog[0]=255;
    if(t>13 && t<13.14)pad.buttons|=8;
  }
  if(dev::Env("MVP06_TEST_FINAL_SCORE") && finalScoreAuditAt.load()>0){
    const double t=Now()-finalScoreAuditAt.load();
    if(t>2 && t<2.14)pad.analog[0]=255;
    if(dev::Env("MVP06_TEST_BOX_SCORE")){for(double at=3.;at<60.;at+=.5)if(t>at && t<at+.14)pad.buttons|=2;if(t>62 && t<62.14)pad.analog[1]=255;return;}
    if(t>5 && t<5.14)pad.analog[1]=255;
    if(t>7 && t<7.14)pad.buttons|=8;
    if(t>10 && t<10.14)pad.analog[0]=255;
  }
  if(dev::Env("MVP06_TEST_COACH") && coachAuditAt.load()>0){
    const double t=Now()-coachAuditAt.load();
    for(double at:{1.,2.})if(t>at && t<at+.14)pad.buttons|=dev::Env("MVP06_TEST_UI_HORIZONTAL")?8:2;
    for(double at:{3.,4.})if(t>at && t<at+.14)pad.buttons|=dev::Env("MVP06_TEST_UI_HORIZONTAL")?4:1;
    if(dev::Env("MVP06_TEST_UI_HORIZONTAL"))MapPlaySimPad(port,pad);
    for(double at:{5.,7.,9.,11.})if(t>at && t<at+.14)pad.analog[2]=255;
    for(double at:{13.,16.,19.,22.})if(t>at && t<at+.14)pad.analog[0]=255;
  }
  if(dev::Env("MVP06_TEST_FINAL_PLAY_SIM") && startingLineupAuditAt.load()>0){
    const double t=Now()-startingLineupAuditAt.load();if(t>.8 && t<.94)pad.buttons|=0x10;
    if(finalPlaySimAuditAt.load()>0){const double f=Now()-finalPlaySimAuditAt.load();
      for(double at:{1.,2.})if(f>at && f<at+.14)pad.buttons|=8;
      for(double at:{3.,4.})if(f>at && f<at+.14)pad.buttons|=4;
      if(dev::Env("MVP06_TEST_COACH") && f>5 && f<5.14)pad.buttons|=8;
      if(dev::Env("MVP06_TEST_SIM_GAME"))for(double at:{5.,5.5})if(f>at && f<at+.14)pad.buttons|=8;
      if(f>6 && f<6.14)pad.analog[0]=255;
      if(dev::Env("MVP06_TEST_UI_HORIZONTAL"))MapPlaySimPad(port,pad);
    }
  }
  if(dev::Env("MVP06_TEST_STARTING_LINEUP") && !dev::Env("MVP06_TEST_FINAL_PLAY_SIM") && startingLineupAuditAt.load()>0) {
    const double t=Now()-startingLineupAuditAt.load();
    for(double at:{1.,2.,3.,4.,5.,6.,7.,8.,9.,10.,11.})if(t>at && t<at+.14)pad.buttons|=2;
    if(t>12.5 && t<12.64)pad.buttons|=1;
    if(t>14 && t<14.14)pad.analog[0]=255;
    if(t>16 && t<16.14)pad.buttons|=2;
    if(t>18 && t<18.14)pad.analog[0]=255;
    if(t>20 && t<20.14)pad.analog[1]=255;
  }
  if (dev::Env("MVP06_TEST_STARTING_PITCHER") && startingPitcherAuditAt.load()>0) {
    const double t=Now()-startingPitcherAuditAt.load();
    for(double at:{.8,1.5,2.2,2.9,3.6,4.3,5.,5.7,6.4,7.1})if(t>at && t<at+.14)pad.buttons|=2;
    if(t>8.3 && t<8.44)pad.buttons|=1;
    if(t>9.5 && t<9.64)pad.analog[2]=255;
    if(t>11.5 && t<11.64)pad.analog[1]=255;
    if(t>13.3 && t<13.44)pad.analog[3]=255;
    if(t>15.3 && t<15.44)pad.analog[1]=255;
    if(t>17.5 && t<17.64)pad.analog[0]=255;
    if(t>19.5 && t<19.64)pad.buttons|=0x10;
  }
  if (dev::Env("MVP06_TEST_DYNASTY_HOME") && dynastyHomeAuditAt.load()>0) {
    const double t = Now()-dynastyHomeAuditAt.load();
    if(dev::Env("MVP06_TEST_OPTIONS")){
      for(double at:{.8,1.2,1.6,2.})if(t>at && t<at+.14)pad.buttons|=2;
      if(t>2.8 && t<2.94)pad.analog[0]=255;
      if(optionsAuditAt.load()>0){double e=Now()-optionsAuditAt.load();int target=int(e/6);double f=e-target*6;
       if(target<5){for(int i=0;i<target;++i){double at=.55+i*.28;if(f>at && f<at+.13)pad.buttons|=2;}
        if(f>2 && f<2.14)pad.analog[0]=255;if(f>4 && f<4.14)pad.analog[1]=255;
       }
      }
      return;
    }
    if(dev::Env("MVP06_TEST_TEAM_MANAGEMENT")){
      if(teamManagementAuditAt.load()==0){for(double at:{.8,1.2,1.6})if(t>at && t<at+.14)pad.buttons|=2;if(t>2.3 && t<2.44)pad.analog[0]=255;}
      else if(dev::Env("MVP06_TEST_ROSTER_MANAGEMENT") && !std::strcmp(dev::Env("MVP06_TEST_ROSTER_MANAGEMENT"),"status")){
        double e=Now()-teamManagementAuditAt.load();int current=TeamManagementSelection();
        if(e<3 && std::fmod(e,.45)<.10 && current>=0 && current!=9)pad.buttons|=(current/5!=1)?2:8;
        if(e>4 && e<4.14)pad.analog[0]=255;
        if(current<0){
          for(double at:{7.,9.,11.,11.5,12.,12.5,13.,13.5,14.,14.5})if(e>at && e<at+.14)pad.buttons|=2;
          for(double at:{8.,10.})if(e>at && e<at+.14)pad.analog[0]=255;
          if(e>17 && e<17.14)pad.analog[3]=255;
          for(double at:{23.,26.})if(e>at && e<at+.14)pad.analog[1]=255;
        }
      }
      else if(dev::Env("MVP06_TEST_ROSTER_MANAGEMENT")){
        double e=Now()-teamManagementAuditAt.load();int segment=int(e/30),targets[]={1,2,3,6,7,9};double f=e-segment*30;if(!std::strcmp(dev::Env("MVP06_TEST_ROSTER_MANAGEMENT"),"pitch") && segment>0)segment=6;
        if(segment<6){int current=TeamManagementSelection(),target=targets[segment];
          if(f<3 && std::fmod(f,.45)<.10 && current>=0 && current!=target)pad.buttons|=(current/5!=target/5)?2:8;
          if(f>4 && f<4.14)pad.analog[0]=255;
          if(current<0){
            for(double at:{7.,9.,20.})if(f>at && f<at+.14)pad.buttons|=2;
            if(f>18 && f<18.14)pad.buttons|=8;
            if(segment!=2){for(double at:{8.,10.})if(f>at && f<at+.14)pad.analog[0]=255;}
            if(f>12 && f<12.14)pad.analog[2]=255;
            if(f>14 && f<14.14)pad.analog[3]=255;
            for(double at:{17.,26.,28.})if(f>at && f<at+.14)pad.analog[1]=255;
          }
        }
      }
      else if(dev::Env("MVP06_TEST_DEPTH_CHART")){
        double e=Now()-teamManagementAuditAt.load();
        for(double at:{4.,30.,38.})if(e>at && e<at+.14)pad.analog[0]=255;
        for(double at:{7.,9.,21.,27.})if(e>at && e<at+.14)pad.buttons|=8;
        for(double at:{11.,13.,23.,25.,32.,33.,34.,35.,36.,40.})if(e>at && e<at+.14)pad.buttons|=2;
        for(double at:{15.,17.})if(e>at && e<at+.14)pad.buttons|=4;
        if(e>19 && e<19.14)pad.buttons|=1;
        for(double at:{42.,44.,46.})if(e>at && e<at+.14)pad.analog[1]=255;
      }
      else{double elapsed=Now()-teamManagementAuditAt.load();int offset=std::strcmp(dev::Env("MVP06_TEST_TEAM_MANAGEMENT"),"tail")==0?6:0;int segment=int(elapsed/12);int target=offset+segment;double phase=elapsed-segment*12;
        if(target<10 && TeamManagementAvailable(target)){
          int current=TeamManagementSelection();
          if(phase<2.8 && std::fmod(phase,.45)<.10 && current>=0 && current!=target)pad.buttons|=(current/5!=target/5)?2:8;
          if(phase>4 && phase<4.14)pad.analog[0]=255;
          if((phase>8 && phase<8.14 || phase>10 && phase<10.14) && TeamManagementSelection()<0)pad.analog[1]=255;
        }
      }
      MapTeamManagementPad(port,pad);return;
    }
    if(dev::Env("MVP06_TEST_STATS_LANDING")){
      for(double at:{.8,1.2})if(t>at && t<at+.14)pad.buttons|=2;
      if(t>2 && t<2.14)pad.analog[0]=255;
      if(statsLandingAuditAt.load()>0){double r=Now()-statsLandingAuditAt.load();
        if(dev::Env("MVP06_TEST_STATISTICS_MENU")){
          if(statisticsMenuAuditAt.load()==0){
            for(double at:{1.,1.4})if(r>at && r<at+.14)pad.buttons|=2;
            if(r>2.5 && r<2.64)pad.analog[0]=255;
          }else{
            double elapsed=Now()-statisticsMenuAuditAt.load();int target=int(elapsed/(dev::Env("MVP06_TEST_STATS_TABLES")?16:8));double phase=elapsed-target*(dev::Env("MVP06_TEST_STATS_TABLES")?16:8);
            if(target<6){
              int current=StatisticsMenuSelection();
              if(phase<2.5 && std::fmod(phase,.5)<.09 && current>=0 && current!=target)pad.buttons|=(current/3!=target/3)?2:8;
              if(phase>3 && phase<3.14)pad.analog[0]=255;
              if(dev::Env("MVP06_TEST_STATS_TABLES")){
                  if(target<5){for(double at:{4.2,4.6,5.0,5.4})if(phase>at && phase<at+.12)pad.buttons|=2;
                    if(phase>6 && phase<6.14)pad.analog[0]=255;
                    if(phase>7 && phase<7.14)pad.analog[2]=255;
                    if(phase>8 && phase<8.14)pad.buttons|=8;
                    if(phase>9 && phase<9.14)pad.analog[3]=255;
                  }else{
                    if(phase>4.2 && phase<4.34)pad.buttons|=8;
                    if(phase>5 && phase<5.14)pad.analog[0]=255;
                    if(phase>7 && phase<7.14)pad.analog[2]=255;
                  }
                  if(phase>11 && phase<11.14)pad.analog[1]=255;
                  if(phase>13 && phase<13.14 && StatisticsMenuSelection()<0)pad.analog[1]=255;
                }else if(phase>6 && phase<6.14)pad.analog[1]=255;
            }
          }
          MapStatisticsMenuPad(port,pad);return;
        }
        if(dev::Env("MVP06_TEST_LEAGUE_TABLES")){
          if(r>1 && r<1.14)pad.analog[0]=255;
          for(double at=2;at<9;at+=.3)if(r>at && r<at+.14)pad.buttons|=2;
          for(double at=10;at<13;at+=.3)if(r>at && r<at+.14)pad.buttons|=1;
          if(r>14 && r<14.14)pad.analog[1]=255;
          if(r>15 && r<15.14)pad.buttons|=2;
          if(r>16 && r<16.14)pad.analog[0]=255;
          for(double at=17;at<22;at+=.3)if(r>at && r<at+.14)pad.buttons|=2;
          for(double at:{23.,25.,27.,29.})if(r>at && r<at+.14)pad.analog[7]=255;
          for(double at:{31.,33.})if(r>at && r<at+.14)pad.analog[6]=255;
          for(double at=35;at<40;at+=.3)if(r>at && r<at+.14)pad.buttons|=8;
          for(double at=41;at<46;at+=.3)if(r>at && r<at+.14)pad.buttons|=4;
          if(r>48 && r<48.14)pad.analog[1]=255;
          return;
        }
        for(double at:{1.,7.,13.})if(r>at && r<at+.14)pad.analog[0]=255;
        for(double at:{4.,10.,16.})if(r>at && r<at+.14)pad.analog[1]=255;
        for(double at:{5.,11.,11.4,17.})if(r>at && r<at+.14)pad.buttons|=2;
      }return;
    }
    if (t>.8 && t<.94) pad.buttons |= 2;
    if(dev::Env("MVP06_TEST_RECRUITING")){if(t>2 && t<2.14)pad.analog[0]=255;if(recruitingAuditAt.load()>0){double r=Now()-recruitingAuditAt.load();if(dev::Env("MVP06_TEST_RECRUIT_PROFILE")){
 if(r>1 && r<1.14)pad.buttons|=2;
 for(double at:{2.,6.,26.})if(r>at && r<at+.14)pad.analog[0]=255;

 for(double at:{10.,14.})if(r>at && r<at+.14)pad.analog[7]=255;
 if(r>18 && r<18.14)pad.analog[6]=255;
 if(r>22 && r<22.14)pad.buttons|=2;
 for(double at:{29.,33.})if(r>at && r<at+.14)pad.analog[1]=255;
 return;
 }if(dev::Env("MVP06_TEST_RECRUIT_LIST")){for(double at:{1.,3.,3.3,3.6,4.,4.3,4.6,5.,5.3,5.6,6.,6.3,6.6,7.,23.,23.4,31.,31.4,31.8,37.,37.3,37.6,38.,38.3,38.6,39.,39.3,39.6,40.,40.3,40.6,41.})if(r>at && r<at+.14)pad.buttons|=2;for(double at:{2.,18.,24.,32.,36.})if(r>at && r<at+.14)pad.analog[0]=255;for(double at:{16.,20.,22.,30.,34.,43.})if(r>at && r<at+.14)pad.analog[1]=255;if(r>8 && r<8.14)pad.buttons|=8;if(r>9 && r<9.14)pad.buttons|=4;if(r>10 && r<10.14)pad.analog[2]=255;if(r>14 && r<14.14)pad.analog[3]=255;return;}for(double at:{1.,5.,5.4,9.,9.3,9.6})if(r>at && r<at+.14)pad.buttons|=2;for(double at:{2.,6.,10.,14.})if(r>at && r<at+.14)pad.analog[0]=255;for(double at:{4.,8.,12.,16.})if(r>at && r<at+.14)pad.analog[1]=255;}return;}
    if (t>1.6 && t<1.74) pad.buttons |= 1;
    if (t>2.4 && t<2.54) pad.analog[0] = 255;
    if (const auto test=dev::Env("MVP06_TEST_PLAY_SIM")) {
      if (t>3.8 && t<3.94) pad.analog[0]=255;
      if (t>5.4 && t<5.54) pad.buttons|=8;
      if (t>6.6 && t<6.74) pad.buttons|=4;
      if (t>7.8 && t<7.94) pad.analog[1]=255;
      if (t>9.4 && t<9.54) pad.analog[0]=255;
      if (std::strcmp(test,"play")!=0 && t>11 && t<11.14) pad.buttons|=8;
      if (t>12.6 && t<12.74) pad.analog[0]=255;
      if (std::strcmp(test,"play")!=0 && t>22 && t<22.14) pad.analog[1]=255;
    } else if (dev::Env("MVP06_TEST_SIMULATION")) {
      if (t>3.5 && t<3.64) pad.buttons|=8;
      if (t>5.3 && t<5.44) pad.analog[3]=255;
      if (t>6.2 && t<6.34) pad.analog[0]=255;
    } else if (dev::Env("MVP06_TEST_DYNASTY_CALENDAR")) {
      if (t>3.5 && t<3.64) pad.buttons |= 8;
      if (t>4.4 && t<4.54) pad.buttons |= 4;
      if (t>5.3 && t<5.44) pad.analog[7] = 255;
      if (t>6.2 && t<6.34) pad.analog[6] = 255;
      if (t>7.1 && t<7.24) pad.analog[2] = 255;
    }
  }
  int c = phase.load();
  if(dev::Env("MVP06_TEST_PAUSE") && pauseAuditGameplayAt>0){
    double t=Now()-pauseAuditGameplayAt;
    if((t>50 && t<50.12)||(t>105 && t<105.12))pad.buttons|=0x10;
    for(double at:{55.,66.,73.,74.,84.,85.,86.,93.,94.,95.,96.,97.})if(t>at && t<at+.12)pad.buttons|=2;
    for(double at:{60.,63.,67.,76.,88.,99.,103.})if(t>at && t<at+.12)pad.analog[0]=255;
    for(double at:{65.,70.,72.,82.,91.})if(t>at && t<at+.12)pad.analog[1]=255;
    // The pause audit normally keeps the confirmation's default Cancel.
    // Explicitly opt into selecting Yes for the quit-match heap regression.
    if(dev::Env("MVP06_TEST_QUIT_MATCH") && t>101 && t<101.12)pad.buttons|=2;
    if((t>53 && t<53.12)||(t>58 && t<58.12))pad.analog[3]=255;
  }
  double r = ready.load();
  PollNativeSubmenu();
  if (c == 5) PollNativeMainMenu();
  if (c == 8) PollNativeTeamSelect();
  if(dev::Env("MVP06_TEST_MATCHUP")) {
    double t=Now()-began.load();
    static double readyAt=0;
    if(c==9 && MatchupLoadingReady()) {if(!readyAt)readyAt=Now();if(Now()-readyAt>3 && std::fmod(Now()-readyAt-3,4.)<.15)pad.analog[0]=255;}
    if(c==6 && SettingsMenuActive() && t>3 && t<3.15)pad.buttons|=0x10;
    if(c==0 && !matchupAuditGameplay && t>3 && std::fmod(t-3,4.)<.15)pad.analog[0]=255;
  }
  if(c==8 && dev::Env("MVP06_TEST_TEAM_SETUP")){
    double t=Now()-began.load();
    bool back=std::strcmp(dev::Env("MVP06_TEST_TEAM_SETUP"),"back")==0;
    if(t>2 && t<2.12)pad.buttons|=back?4:8;
    if(back && t>3 && t<3.12)pad.buttons|=1;
    if((t>4 && t<4.12)||(t>14 && t<14.12)||(t>24 && t<24.12)||(t>34 && t<34.12))pad.analog[0]=255;
    if(t>6 && t<(back?10:12) && std::fmod(t-6,1.5)<.12)pad.buttons|=8;
    if(t>16 && t<22 && std::fmod(t-16,1.5)<.12)pad.buttons|=8;
    if(dev::Env("MVP06_TEST_MATCHUP") && t>23 && t<23.15)pad.buttons|=0x10;
    if(back){
      if((t>10 && t<10.12)||(t>18 && t<18.12))pad.analog[1]=255;
      if((t>12 && t<12.12)||(t>20 && t<20.12))pad.analog[0]=255;
    }
  }
  if (c == 8 && dev::Env("MVP06_TEST_TEAM_HORIZONTAL")) {
    double t=Now()-began.load();
    if(t>2 && t<2.14)pad.buttons|=4;
    if(t>4 && t<4.14)pad.buttons|=1;
    if((t>6 && t<6.14)||(t>8 && t<8.14))pad.buttons|=8;
    if(t>10 && t<10.14)pad.buttons|=2;
    if(t>12 && t<12.14)pad.analog[7]=255;
    if(t>14 && t<14.14)pad.analog[2]=255;
    if(t>16 && t<16.14)pad.analog[1]=255;
  }
  if (c == 8 && dev::Env("MVP06_TEST_TEAM_ACTIONS")) {
    double t = Now() - began.load();
    if(t>2 && t<2.14)pad.analog[2]=255;
    if((t>4 && t<4.14)||(t>6 && t<6.14))pad.ly=32767;
    if(t>8 && t<8.14)pad.buttons|=8;
    if(t>10 && t<10.14)pad.analog[7]=255;
    if((t>12 && t<12.14)||(t>14 && t<14.14))pad.ly=-32767;
    if(t>16 && t<16.14)pad.analog[1]=255;
  }
  // Read-only graphics audit after Exhibition reaches its native team selector.
  if ((c == 0 || c == 8) && dev::Env("MVP06_TEST_EXHIBITION")) {
    double t = Now() - began.load();
    if (t > 2 && t < 18 && std::fmod(t - 2, 2.) < .12) pad.buttons |= 8;
    if (t > 4 && t < 18 && std::fmod(t - 4, 4.) < .12) pad.buttons |= 2;
    static int captured = -1;
    int step = t < 2 ? int(t * 4) : 8 + int(t / 2);
    if (t > .03 && step != captured) {captured = step; gpu::g_capture = true;}
  }
  // Opt-in navigation audit: walk every native grid, then return without
  // editing.
  static int cycleCategory = 0;
  static bool awaitingReturn = false;
  static double dialogAt = 0;
  if (dev::Env("MVP06_TEST_SUBMENU_CYCLE")) {
    double elapsed = Now() - began.load();
    if (c == 5 && awaitingReturn) {
      cycleCategory++;
      awaitingReturn = false;
      dialogAt = 0;
    }
    if (c == 5 && cycleCategory < 7 && elapsed > 2) {
      if (menuRow.load() != cycleCategory) {
        if (std::fmod(elapsed - 2, .7) < .12)
          pad.buttons |= 2;
      } else if (elapsed > 3 && std::fmod(elapsed - 3, 1.) < .12)
        pad.analog[0] = 255;
    }
    if (c == 6 || c == 7) {
      awaitingReturn = true;
      int last = SubmenuRowCount() - 1;
      double backAt = 2 + std::max(1, last) * .7;
      if (SubmenuPopupVisible()) {
        if (!dialogAt)
          dialogAt = Now();
        double t = Now() - dialogAt;
        if (t > .4 && t < .54)
          pad.buttons |= 2;
        if (t > 1 && t < 1.14)
          pad.analog[0] = 255;
      } else if (c == 6 && elapsed > 1.2 && SelectedSubmenuRow() != last) {
        if (std::fmod(elapsed - 1.2, .7) < .12)
          pad.buttons |= 2;
      } else if (elapsed > backAt && std::fmod(elapsed - backAt, 1.) < .12)
        pad.analog[1] = 255;
    }
  }
  static bool release = false;
  if (c == 3 || c == 4 || (c == 5 && Now() - began.load() < .7)) {
    // Device discovery also polls the pad. A Start observed there must still
    // reach the native title screen before the intro takes ownership of input.
    pad.buttons = (c == 3 || c == 4) && !menuPresent.load() &&
                          std::fmod(Now() - began.load(), .5) < .15 ? 0x10 : 0;
    for (auto &a : pad.analog)
      a = 0;
    release = true;
    return;
  }
  if (c == 1 || (c == 2 && (r == 0 || Now() - r < .65))) {
    pad.buttons = 0;
    for (auto &a : pad.analog)
      a = 0;
    release = true;
    return;
  }
  if (c == 2 && release) {
    if (!(pad.buttons & 0x10) && !pad.analog[0])
      release = false;
    pad.buttons = 0;
    for (auto &a : pad.analog)
      a = 0;
  }
  // Explicit diagnostic input only; never enabled in a normal launch.
  if (c == 2 && r > 0 && dev::Env("MVP06_TEST_START")) {
    double t = Now() - r;
    if (t > 2 && t < 2.15)
      pad.buttons |= 0x10;
  }
  if (c == 2 && r > 0 && Now() - r >= .65 &&
      ((pad.buttons & 0x10) || pad.analog[0])) {
    began = Now();
    phase = 3;
    LOGR("menu intro: fade to black");
  }
  if (c == 5 && release) {
    if (!(pad.buttons & 0x10) && !pad.analog[0])
      release = false;
    pad.buttons = 0;
    for (auto &a : pad.analog)
      a = 0;
  }
  static bool nestedAuditEntered = false;
  static int tuningAuditIndex=0;
  static bool tuningAuditSeen=false;
  static bool libraryAuditReturned=false;
  if ((c == 6 || c == 7) && dev::Env("MVP06_TEST_SUBMENU")) {
    double elapsed = Now() - began.load();
    const char *test = dev::Env("MVP06_TEST_SUBMENU");
    if (SettingsMenuActive()) {
      pad.analog[0]=0; // Other settings audits remain read-only.
      if(dev::Env("MVP06_TEST_BUTTON_ICONS") && SubmenuRowCount()>=16){
        if(SelectedSubmenuRow()!=15){if(std::fmod(elapsed,.24)<.10)pad.buttons|=2;}
        else{static double reached=0;if(!reached)reached=Now();double e=Now()-reached;if(e>1 && e<1.14)pad.buttons|=8;if(e>2 && e<2.14)pad.analog[0]=255;}
      }
      if (dev::Env("MVP06_TEST_FORMAT")) {
        if (elapsed > 1 && elapsed < 1.14) pad.buttons |= 2;
        if (elapsed > 2 && elapsed < 10 && std::fmod(elapsed-2,.8)<.12) pad.buttons |= 8;
        if (elapsed > 11 && elapsed < 11.14) pad.buttons |= 2;
      }
      if (dev::Env("MVP06_TEST_RULES")) {
        int target = RulesSettingsRow();
        if (elapsed > 1 && elapsed < 5 && SelectedSubmenuRow() < target && std::fmod(elapsed-1,.7)<.12) pad.buttons |= 2;
        if (target >= 0 && SelectedSubmenuRow() == target &&
            ((elapsed > 5 && elapsed < 5.14) || (elapsed > 6 && elapsed < 6.14))) pad.buttons |= 8;
        if (elapsed > 7 && elapsed < 13 && std::fmod(elapsed - 7, .7) < .12) pad.buttons |= 2;
        // Never Accept: the process ends with these temporary choices unsaved.
      }
      if(dev::Env("MVP06_TEST_LIBRARY") && LibraryMenuKind() && elapsed>7 && elapsed<7.14) {
        pad.analog[1]=255;libraryAuditReturned=true;
      }
      if(dev::Env("MVP06_TEST_SETTINGS") && elapsed>3 && elapsed<3.14)pad.buttons|=2;
      if(dev::Env("MVP06_TEST_SETTINGS") &&
         !std::strcmp(dev::Env("MVP06_TEST_SETTINGS"),"scroll") && elapsed>1 &&
         SelectedSubmenuRow()<SubmenuRowCount()-1 && std::fmod(elapsed-1,.4)<.10)
        pad.buttons|=2;
      if(dev::Env("MVP06_TEST_TUNING") && !std::strcmp(dev::Env("MVP06_TEST_TUNING"),"cycle")) {
        tuningAuditSeen=true;
        if(elapsed>5 && elapsed<5.14)pad.analog[1]=255;
      }
    } else if(dev::Env("MVP06_TEST_LIBRARY") && libraryAuditReturned) {
      pad.analog[0]=0; // Do not re-enter or activate a file operation after Back.
    } else if(dev::Env("MVP06_TEST_TUNING") && NestedSubmenuActive()) {
      bool cycle=!std::strcmp(dev::Env("MVP06_TEST_TUNING"),"cycle");
      if(cycle && tuningAuditSeen){++tuningAuditIndex;tuningAuditSeen=false;}
      int target=cycle?tuningAuditIndex:std::clamp(std::atoi(dev::Env("MVP06_TEST_TUNING")),0,4);
      if(target<5) {
        if(elapsed>1 && SelectedSubmenuRow()!=target && std::fmod(elapsed-1,.7)<.12)pad.buttons|=2;
        else if(SelectedSubmenuRow()==target && elapsed>3 && std::fmod(elapsed-3,1.)<.12)pad.analog[0]=255;
      }
    } else if (dev::Env("MVP06_TEST_CREATE") && NestedSubmenuActive()) {
      if (elapsed > 1 && elapsed < 1.14) pad.analog[0] = 255;
    } else if (dev::Env("MVP06_TEST_NESTED") && NestedSubmenuActive()) {
      nestedAuditEntered = true;
      pad.analog[0] = 0; // Never select a child action in this audit.
      // Walk the child choices and return without creating, saving or deleting.
      if (elapsed > 1 && elapsed < 5 &&
          SelectedSubmenuRow() < SubmenuRowCount()-1 && std::fmod(elapsed-1,.6)<.12)
        pad.buttons |= 2;
      if (elapsed > 7 && elapsed < 7.14) pad.analog[1] = 255;
    } else if (dev::Env("MVP06_TEST_NESTED") && nestedAuditEntered) {
      pad.analog[0] = 0; // Do not re-enter after the audit has returned.
    } else if (!std::strcmp(test, "back")) {
      if (elapsed > 4 && elapsed < 4.15)
        pad.analog[1] = 255;
    } else if (elapsed > 4) {
      int target = std::atoi(test);
      if (SelectedSubmenuRow() != target) {
        if (std::fmod(elapsed - 4, .7) < .12)
          pad.buttons |= 2;
      } else if (elapsed > 5 && std::fmod(elapsed - 5, 1.) < .12)
        pad.analog[0] = 255;
    }
  }
  static bool releasedAfterModal=false;
  static double modalTestAt=0;
  static bool modalTestDone=false;
  if(SubmenuPopupVisible()) {
    if(dev::Env("MVP06_TEST_MODAL") && !modalTestDone) {
      if(!modalTestAt)modalTestAt=Now();
      double elapsed=Now()-modalTestAt;
      if(elapsed>.7 && elapsed<.84)pad.buttons|=2;
      if(elapsed>1.5 && elapsed<1.64)pad.buttons|=1;
      if(elapsed>2.3 && elapsed<2.44)pad.analog[1]=255;
      if(elapsed>3.1 && elapsed<3.24) {pad.analog[0]=255;modalTestDone=true;}
    }
    releasedAfterModal=true;
    return;
  }
  if(modalTestAt>0 && !SubmenuPopupVisible())modalTestDone=true;
  if(releasedAfterModal) {
    bool held=pad.buttons || std::abs(int(pad.ly))>16000;
    for(auto a:pad.analog)held|=a>30;
    if(!held)releasedAfterModal=false;
    pad.buttons=0;pad.ly=0;
    for(auto& a:pad.analog)a=0;
    return;
  }
  if (c == 7) {
    static bool releasePlay = true;
    if (releasePlay) {
      if (!pad.analog[0] && !(pad.buttons & 0x10))
        releasePlay = false;
      pad.buttons = 0;
      for (auto &a : pad.analog)
        a = 0;
      return;
    }
    if (pad.analog[1]) {
      returnFromSubmenu = true;
      phase = 5;
      began = Now();
      pad.analog[1] = 0;
      releasePlay = true;
      return;
    }
    if (pad.analog[0] || (pad.buttons & 0x10)) {
      phase = 0;
      releasePlay = true;
      return;
    }
    if (pad.analog[2])
      return; // retain the native main menu's Sign In action
    pad.buttons = 0;
    for (auto &a : pad.analog)
      a = 0;
    return;
  }
  if (c == 5 && dev::Env("MVP06_TEST_MENU") &&
      !dev::Env("MVP06_TEST_SUBMENU_CYCLE") && !modalTestDone) {
    double elapsed = Now() - began.load();
    int target = std::clamp(std::atoi(dev::Env("MVP06_TEST_MENU")), 0, 7);
    if (elapsed > 2 && menuRow.load() != target) {
      double pulse = std::fmod(elapsed - 2, .7);
      if (pulse < .12)
        pad.buttons |= 2;
    }
    if (elapsed > 3 && menuRow.load() == target) {
      double pulse = std::fmod(elapsed - 3, 1.);
      if (pulse < .12)
        pad.analog[0] = 255;
    }
  }
  if (c == 5 && menuRow.load() == 0 &&
      (pad.analog[0] || (pad.buttons & 0x10))) {
    OpenSubmenu(0);
    phase = 7;
    began = Now();
    pad.buttons = 0;
    for (auto &a : pad.analog)
      a = 0;
  }
  if(c==8)MapTeamSelectPad(port,pad,Now()-began.load());
}
void BeginLicensing() {
  began = Now();
  phase = 1;
}
bool StartupSubmenuActive() { return phase.load() == 6; }
void StartupScreen(const char *n) {
  if (!std::strcmp(n, "START") || !std::strcmp(n, "MAINMENU")) EndStartupLoading();
  if(!std::strcmp(n,"TRANNCAA") || !std::strcmp(n,"TEAMSMATCHUP") || !std::strcmp(n,"HUDRIGHT") || !std::strcmp(n,"MAINMENU") || !std::strcmp(n,"TEAMSELECT"))HideMatchupLoading();
  if(!std::strcmp(n,"HUDRIGHT") && dev::Env("MVP06_TEST_PAUSE") && pauseAuditGameplayAt==0)pauseAuditGameplayAt=Now();
  if(!std::strcmp(n,"HUDRIGHT") && dev::Env("MVP06_TEST_MATCHUP"))matchupAuditGameplay=true;
  if (!std::strcmp(n, "LOADING1")) {ResetMatchupLoading();phase=9;began=Now();return;}
  if (!std::strcmp(n, "FSELECTSCHOOL") || !std::strcmp(n, "SELECTSCHOOL")) {
    dynastySchoolVisible = false;
    phase = 10;
    began = Now();
    return;
  }
  if (!std::strcmp(n, "TEAMSELECT")) {
    ResetTeamSelect();
    phase = 8;
    began = Now();
    return;
  }
  int category = SubmenuCategory(n);
  if (category >= 0) {
    OpenSubmenu(category, n);
    menuRow = category;
    phase = 6;
    began = Now();
    return;
  }
  if (!std::strcmp(n, "START")) {
    ready = 0;
    began = Now();
    phase = 2;
  } else if (!std::strcmp(n, "MAINMENU")) {
    returnFromSubmenu = (phase.load() == 6 || phase.load() == 7);
    menuPresent = true;
    // A newly constructed native MAINMENU starts at row zero. Do not let
    // the old category leak into the return animation before its first poll.
    menuRow = 0;
    if (phase.load() != 3 && phase.load() != 4) {
      began = Now();
      phase = introPlayed.load() ? 5 : 3;
    }
  } else if ((phase.load() == 5 || phase.load() == 6 || phase.load() == 8 || phase.load() == 9 || phase.load() == 10) &&
             std::strcmp(n, "CNTRLSELECT") && std::strcmp(n, "POPUPLEFT") && std::strcmp(n, "POPUPRIGHT") &&
             std::strcmp(n, "BACKMAIN") && std::strcmp(n, "NAVBAR") &&
             std::strcmp(n, "LOGOANIM") && std::strcmp(n, "EAMESSENGER") &&
             std::strcmp(n, "SEARCHRESULTS") &&
             std::strcmp(n, "FRIENDOPTIONS") &&
             std::strcmp(n, "MSGINTERFACE") && std::strcmp(n, "TKEYBOARD") &&
             std::strcmp(n, "POPUP") && std::strcmp(n, "SPORTSTRAX") &&
             std::strcmp(n, "BACKIMAGE") && std::strcmp(n, "FTICKER") &&
             std::strcmp(n, "FTICKERMATTE") && std::strcmp(n, "MBACKGROUND") &&
             std::strcmp(n, "MMVPBRAND") && std::strcmp(n, "BACKGROUND") &&
             std::strcmp(n,"COMMONBACK") && std::strcmp(n,"BACKRAY") && std::strcmp(n,"TOPTITLE") &&
             std::strcmp(n,"FEARBAR") && std::strcmp(n,"UNPLUGGEDCTRL") && std::strcmp(n,"ANIMSLIDEIN") && std::strcmp(n,"NETWORKSTATUS")) {
    leafFromSubmenu = phase.load() == 6;
    if(dev::Env("MVP06_TRACE_FE"))LOGR("host UI native leaf: %s",n);
    phase = 0;
    began = Now();
    menuPresent = false;
  }
}
void StartupUi(double) {
  if(NativeMenuPresentation())return;
  if(dev::Env("MVP06_TEST_PICKER_RENDER") && dynastySchoolVisible.load()){
    static double entered=Now(),captured=0;if(Now()-entered<12 && Now()-captured>.25){captured=Now();gpu::g_capture=true;}
  }
  if(dev::Env("MVP06_TEST_BUTTON_ICONS") || dev::Env("MVP06_TEST_OPTIONS") || dev::Env("MVP06_TEST_RECRUIT_LIST") || dev::Env("MVP06_TEST_STATS_LANDING") || dev::Env("MVP06_TEST_TEAM_MANAGEMENT")){static double captured=0;if(Now()-captured>1){gpu::g_capture=true;captured=Now();}}
  if (DrawLoadout()) return;
  if (DrawNativeBallparkLoading()) return;
  if (DrawNativeBallparkEditor()) return;
  if (DrawNativeTextKeyboard()) return;
  if (DrawNativePlayerInformation()) return;
  if (DrawNativeRosterBrowser()) return;
  if (DrawNativeDynastyOptions()){if(dev::Env("MVP06_TEST_OPTIONS") && optionsAuditAt.load()==0)optionsAuditAt=Now();return;}
    if(DrawNativePostseasonScores())return;
    if(DrawNativeHireStaff())return;
    if(DrawNativeMailFilters())return;
    if(DrawNativeOffseason())return;
    if (DrawNativeRecruitProfile())return;
  if (DrawNativeRosterStatus())return;
  if (DrawNativeRosterEditor())return;
  if (DrawNativeManagementDetail())return;
  if (DrawNativeDepthChart())return;
  if (DrawNativeAwardWatch())return;
  if (DrawNativePlayerEditor())return;
  if (DrawNativeCreateEdit())return;
  if (DrawNativeTeamManagement()){if(dev::Env("MVP06_TEST_TEAM_MANAGEMENT") && teamManagementAuditAt.load()==0)teamManagementAuditAt=Now();return;}
  if (DrawNativeStatisticsTable()) return;
  if (DrawNativeStatisticsMenu()) {if(dev::Env("MVP06_TEST_STATISTICS_MENU") && statisticsMenuAuditAt.load()==0)statisticsMenuAuditAt=Now();return;}
  if (DrawNativeLeagueTable()) return;
  if (DrawNativeStatsLanding()) {if(dev::Env("MVP06_TEST_STATS_LANDING") && statsLandingAuditAt.load()==0)statsLandingAuditAt=Now();return;}
  if (DrawNativeRecruitList()) return;
  if (DrawNativeRecruiting()) {if(dev::Env("MVP06_TEST_RECRUITING") && recruitingAuditAt.load()==0)recruitingAuditAt=Now();return;}
  if (DrawNativeBoxScore()) return;
  if (DrawNativeFinalScore()) {if(dev::Env("MVP06_TEST_FINAL_SCORE") && finalScoreAuditAt.load()==0)finalScoreAuditAt=Now();return;}
  if (DrawNativeSimGame()) {if(dev::Env("MVP06_TEST_SIM_GAME") && simGameAuditAt.load()==0)simGameAuditAt=Now();return;}
  if (DrawNativeCoach()) {if(dev::Env("MVP06_TEST_COACH") && coachAuditAt.load()==0)coachAuditAt=Now();return;}
  if (DrawNativeFinalPlaySim()) {if(dev::Env("MVP06_TEST_FINAL_PLAY_SIM") && finalPlaySimAuditAt.load()==0)finalPlaySimAuditAt=Now();return;}
  if (DrawNativeStadiumSelection()) return;
  if (DrawNativeStartingLineup()) {if(dev::Env("MVP06_TEST_STARTING_LINEUP") && startingLineupAuditAt.load()==0)startingLineupAuditAt=Now();return;}
  if (DrawNativeStartingPitcher()) {
    if(dev::Env("MVP06_TEST_STARTING_PITCHER") && startingPitcherAuditAt.load()==0)startingPitcherAuditAt=Now();
    return;
  }
  if (DrawNativePlaySim()) return;
  if(DrawNativeRecruitActions())return;
  if(DrawNativeRecruitOffer())return;
  if(DrawNativeMailReader())return;
  if(DrawNativeInbox())return;
  if(DrawNativeTournamentTree())return;
  const bool calendar=DrawNativeDynastyCalendar();
  if (DrawNativeSimulationProgress(calendar)) return;
  if (calendar) return;
  if (DrawNativeDynastyHome()) {
    if (dev::Env("MVP06_TEST_DYNASTY_HOME") && dynastyHomeAuditAt.load()==0)
      dynastyHomeAuditAt = Now();
    return;
  }
  {
    const bool visible = DrawNativeDynastySchool();
    dynastySchoolVisible = visible;
    if (visible) return;
  }
  if (DrawNativeUtilityMenu()) return;
  DrawPauseMenuTheme();
  PollNativePitcherInfo();
  PollNativeBatterInfo();
  DrawBatterCard();
  DrawPitcherCard();
  PollNativeMvpTransition();
  DrawMvpTransition();
  if(dev::Env("MVP06_PREVIEW_STADIUM")) {
    auto tex=gpu::g_ui_image(overhaul::UiAsset(Assets()/"stadium-intro-preview-bg.png"));
    if(tex)ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)tex,{0,0},ImGui::GetIO().DisplaySize);
    PublishStadiumIntro({MatchupSide{"FLORIDA","GATORS","48","23","A+","2"},MatchupSide{"TEXAS","LONGHORNS","56","16","A+","1"}},"Disch-Falk Field",true);
    DrawStadiumIntro();
    return;
  }
  PollNativeStadiumIntro();
  DrawStadiumIntro();
  // Opt-in visual fixture: renders the layout without waiting for a game load.
  // Normal screen routing and native input never use these sample values.
  if (const char* preview = dev::Env("MVP06_PREVIEW_MATCHUP"); preview && !std::strcmp(preview, "loadout")) {
    static const double previewAt = Now();
    const std::array<MatchupSide,2> sides = {{
        {"FLORIDA","GATORS","","","","77"}, {"TEXAS","LONGHORNS","","","","115"}}};
    LoadoutScore score{};
    score[0][0] = "UF";
    score[1][0] = "TEX";
    score[0][1] = "0";
    for (auto& row : score)
      for (int col = 10; col < 13; ++col) row[col] = "0";
    DrawLoadoutCard(sides, score, Now() - previewAt);
    return;
  }
  if(dev::Env("MVP06_PREVIEW_MATCHUP")) {
    static double previewAt=Now();
    PublishMatchupLoading({MatchupSide{"FLORIDA","GATORS","48","23","A+","2"},MatchupSide{"TEXAS","LONGHORNS","56","16","A+","1"}},true,true);
    DrawMatchupLoading(Now()-previewAt);
    return;
  }
  if (phase.load() == 5) PollNativeMainMenu();
  // Settings may idle without another controller poll. Keep their live read-only
  // presentation synchronized from the render loop as well.
  if(SettingsMenuActive() && StartupSubmenuActive())PollNativeSubmenu();
  int c = phase.load();
  if (c == 9 || MatchupLoadingVisible()) {
    if(c==9)PollNativeMatchupLoading();
    DrawMatchupLoading(Now()-began.load());
    return;
  }
  if (c == 8) {
    PollNativeTeamSelect();
    DrawTeamSelect(Now() - began.load());
    return;
  }
  if (!c) {
    if (leafFromSubmenu.load() && Now() - began.load() < .25) {
      auto *draw = ImGui::GetBackgroundDrawList();
      int first = draw->VtxBuffer.Size;
      DrawSubmenu(1);
      float alpha = 1 - Fade(Now() - began.load(), 0, .25);
      for (int i = first; i < draw->VtxBuffer.Size; i++) {
        auto &color = draw->VtxBuffer[i].col;
        color = (color & ~IM_COL32_A_MASK) |
                (ImU32(((color >> IM_COL32_A_SHIFT) & 255) * alpha)
                 << IM_COL32_A_SHIFT);
      }
    }
    static double capturedAt = 0;
    if (dev::Env("MVP06_TRACE_FE") && began.load() != capturedAt &&
        Now() - began.load() > 3) {
      capturedAt = began.load();
      gpu::g_capture = true;
    }
    return;
  }
  if (!gpu::g_ui_image)
    return;
  static auto root = Assets();
  if (root.empty())
    return;
  double t = Now() - began.load();
  if (c == 6 || c == 7) {
    DrawSubmenu(t);
    return;
  }
  if (c == 3 && t >= .55) {
    IntroStart(overhaul::DiskFile(root / "intro.mp4"));
    phase = 4;
    c = 4;
    began = Now();
    t = 0;
  }
  if (c == 4 && IntroDone() && menuPresent.load()) {
    introPlayed = true;
    phase = 5;
    c = 5;
    began = Now();
    t = 0;
    LOGR("menu intro: static background handoff");
  }
  auto *d = ImGui::GetBackgroundDrawList();
  auto size = ImGui::GetIO().DisplaySize;
  float s = std::min(size.x / 2560.f, size.y / 1440.f),
        ox = (size.x - 2560 * s) * .5f, oy = (size.y - 1440 * s) * .5f;
  auto p = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
  auto im = [&](const char *f, float x, float y, float w, float h,
                float a = 1.f) {
    if (a <= 0)
      return;
    auto tex = gpu::g_ui_image(overhaul::UiAsset(root / f));
    if (tex)
      d->AddImage((ImTextureID)tex, p(x, y), p(x + w, y + h), {0, 0}, {1, 1},
                  IM_COL32(255, 255, 255, int(255 * std::clamp(a, 0.f, 1.f))));
  };
  if (c == 4) {
    d->AddRectFilled({0, 0}, size, IM_COL32(0, 0, 0, 255));
    auto texture = IntroTexture();
    if (texture)
      d->AddImage((ImTextureID)texture, p(0, 0), p(2560, 1440));
    static bool captured = false;
    if (!captured && t > 2 && dev::Env("MVP06_TRACE_FE")) {
      captured = true;
      gpu::g_capture = true;
    }
    return;
  }
  if (c == 5) {
    if (returnFromSubmenu.load() && t < .35) {
      DrawSubmenu(.35 - t, true, menuRow.load());
      return;
    }
    im("menu_bg.png", 0, 0, 2560, 1440);
    if (t < .12) {
      auto texture = IntroTexture();
      if (texture)
        d->AddImage((ImTextureID)texture, p(0, 0), p(2560, 1440), {0, 0},
                    {1, 1},
                    IM_COL32(255, 255, 255, int(255 * (1 - Fade(t, 0, .12)))));
    }
    float alpha = returnFromSubmenu.load() ? 1.f : Fade(t, .12, .55);
    d->AddRectFilledMultiColor(p(0, 0), p(1060, 1440),
                               IM_COL32(0, 0, 0, int(alpha * 185)),
                               IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                               IM_COL32(0, 0, 0, int(alpha * 220)));
    im("menu-trim-imagegen.png", 0, 0, 600, 1440, alpha);
    UiBrandLogo(d,p(145,120),p(975,395),IM_COL32(255,255,255,int(255*alpha)));
    int row = menuRow.load();
    static float visualRow = 0, fromRow = 0;
    static int targetRow = 0;
    static double moveBegan = 0, lastMenuBegan = 0;
    if (lastMenuBegan != began.load()) {
      lastMenuBegan = began.load();
      visualRow = fromRow = float(row);
      targetRow = row;
      moveBegan = Now();
    }
    if (targetRow != row) {
      fromRow = visualRow;
      targetRow = row;
      moveBegan = Now();
    }
    static int capturedMove = -1, moveCaptureStage = 0;
    if (capturedMove != row) {
      capturedMove = row;
      moveCaptureStage = 0;
    }
    if (dev::Env("MVP06_TRACE_FE") && dev::Env("MVP06_TEST_MENU") &&
        row > 0) {
      double elapsed = Now() - moveBegan;
      if ((moveCaptureStage == 0 && elapsed > .04) ||
          (moveCaptureStage == 1 && elapsed > .28)) {
        gpu::g_capture = true;
        moveCaptureStage++;
      }
    }
    float move = Fade(Now() - moveBegan, 0, .22);
    move = 1 - std::pow(1 - move, 3.f);
    visualRow = fromRow + (targetRow - fromRow) * move;
    constexpr float rowTop = 395, rowHeight = 100, rightTop = 930,
                    rowSlant = 28;
    for (int i = 0; i < 8; i++) {
      float y = rowTop + i * rowHeight, right = rightTop - i * rowSlant;
      d->AddQuadFilled(p(0, y), p(right, y), p(right - rowSlant, y + rowHeight),
                       p(0, y + rowHeight),
                       IM_COL32(0, 0, 0, int(alpha * 125)));
      d->AddLine(p(0, y + rowHeight), p(right - rowSlant, y + rowHeight),
                 IM_COL32(230, 230, 230, int(alpha * 150)), s);
    }
    float highlightY = rowTop + visualRow * rowHeight,
          highlightRight = rightTop - visualRow * rowSlant;
    auto highlight = std::array<ImVec2, 4>{
        p(0, highlightY), p(highlightRight, highlightY),
        p(highlightRight - rowSlant, highlightY + rowHeight),
        p(0, highlightY + rowHeight)};
    auto strip = gpu::g_ui_image(overhaul::UiAsset(root / "menu-selection-imagegen.png"));
    // Sample only the red texture core. The same quad defines selected and idle
    // row silhouettes; the source sprite's baked angle/border must not distort
    // it.
    if (strip)
      d->AddImageQuad((ImTextureID)strip, highlight[0], highlight[1],
                      highlight[2], highlight[3], {.02f, .31f}, {.83f, .31f},
                      {.83f, .64f}, {.02f, .64f},
                      IM_COL32(255, 255, 255, int(alpha * 255)));
    d->AddPolyline(highlight.data(), 4,
                   IM_COL32(242, 240, 232, int(alpha * 255)), true, 2 * s);
    for (int i = 0; i < 8; i++) {
      constexpr float accentSlant = 20;
      float y = rowTop + i * rowHeight, edge = 145 - i * accentSlant;
      d->AddQuadFilled(p(edge, y + 3), p(edge + 10, y + 3),
                       p(edge + 10 - accentSlant, y + rowHeight - 2),
                       p(edge - accentSlant, y + rowHeight - 2),
                       IM_COL32(180, 22, 35, int(alpha * 230)));
      float weight = std::max(0.f, 1 - std::abs(float(i) - visualRow));
      float entrance =
          returnFromSubmenu.load() ? 1.f : Fade(t, .12 + i * .035, .45);
      const char *labels[] = {"PLAY NOW",      "GAME MODES",     "LEARN MVP",
                              "CREATION ZONE", "MANAGE ROSTERS", "OPTIONS",
                              "ESPN EXTRAS", "QUIT GAME"};
      float x = 175 + 12 * weight - 45 * (1 - entrance);
      float available = rightTop - i * rowSlant - 115 - x;
      UiText(d, UiFont::Navigation, 54 * s, p(x, y + 20),
             IM_COL32(255, 255, 255,
                      int(255 * alpha * (.76f + .24f * weight) * entrance)),
             labels[i], available * s);
    }
    float xx = highlightRight - 92, yy = highlightY + 32;
    for (int j = 0; j < 2; j++) {
      float q = xx + j * 22;
      auto points =
          std::array<ImVec2, 3>{p(q, yy), p(q + 19, yy + 18), p(q, yy + 36)};
      d->AddPolyline(points.data(), 3,
                     IM_COL32(255, 240, 240, int(alpha * 230)), false, 7 * s);
    }
    im("controller-reference-a.png", 103, 1293, 54, 54, alpha);
    im("menu-select.png", 177, 1294, 220, 70, alpha);
    d->AddLine(p(322, 1300), p(322, 1340),
               IM_COL32(230, 230, 230, int(alpha * 220)), s);
    im("controller-reference-x.png", 373, 1293, 54, 54, alpha);
    im("menu-signin.png", 447, 1294, 220, 70, alpha);
    static double captureBegan = 0;
    if (captureBegan != began.load() && t > 1.1 &&
        dev::Env("MVP06_TRACE_FE")) {
      captureBegan = began.load();
      gpu::g_capture = true;
    }
    return;
  }
  UiMenuBackdrop(d,size,UiBackdrop::Charcoal);
  if (c == 2 || c == 3)
    im("press-start-background-2560x1440.png",0,0,2560,1440,c == 3 ? 1.f : Fade(t,0,.65));
  float e = c == 3 ? 1 : Fade(t, 0, .9);
  e = 1 - (1 - e) * (1 - e) * (1 - e);
  float x = c == 1 ? 453 : 300, y = c == 1 ? 460 : 440,
        w = c == 1 ? 1653 : 1080, h = c == 1 ? 537 : 360;
  const float logoX=x+((c==1 ? 300 : 620)-x)*e;
  const float logoY=y+((c==1 ? 440 : 450)-y)*e;
  const float logoW=w+((c==1 ? 1080 : 1320)-w)*e;
  const float logoH=h+((c==1 ? 360 : 440)-h)*e;
  UiBrandLogo(d,p(logoX,logoY),p(logoX+logoW,logoY+logoH));
  float a = c == 3 ? 0 : (c == 1 ? Fade(t, .3, .65) : 1 - Fade(t, 0, .3));
  if (a > 0) {
    d->AddLine(p(1445, 285), p(1445, 1045),
               IM_COL32(210, 210, 210, int(a * 160)), s);
    im("copy.png", 1505, 285 + (1 - a) * 18, 750, 430, a);
    im("ncaa.png", 1505, 810, 230, 230, c == 1 ? Fade(t, .55, .5) : a);
    im("esrb.png", 1835, 825, 450, 225, c == 1 ? Fade(t, .65, .5) : a);
    im("cprt.png", 1505, 1100, 800, 50, c == 1 ? Fade(t, .75, .5) : a);
  }
  if (c == 2 || c == 3) {
    im("prst.png", 940, 940, 680, 85,
       ready.load() > 0
           ? Fade(Now() - ready.load(), 0, .6) *
                 (.5f +
                  .5f * std::cos(float(std::max(0., Now() - ready.load() - .6) *
                                       6.283185307179586 / 3.6)))
           : 0);
    im("cprt.png", 850, 1330, 860, 54, Fade(t, .7, .6));
  }
  if (c == 3)
    d->AddRectFilled({0, 0}, size,
                     IM_COL32(0, 0, 0, int(255 * Fade(t, 0, .55))));
  static int last = 0;
  static bool captured = false;
  if (last != c) {
    last = c;
    captured = false;
  }
  if (!captured &&
      (c == 1 ? t > 1.5 : (ready.load() > 0 && Now() - ready.load() > 1.0)) &&
      dev::Env("MVP06_TRACE_FE")) {
    captured = true;
    gpu::g_capture = true;
  }
}
} // namespace game
