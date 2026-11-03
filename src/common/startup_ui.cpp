#include "core/dev_tools.h"
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
bool matchupAuditGameplay=false;
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
void StartupPad(int port, input::Pad &pad) {
  KeyboardPad(port, pad);
  if (port != 0) {
    if(phase.load()==8)MapTeamSelectPad(port,pad,Now()-began.load());
    return;
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
      pad.analog[0]=0; // The settings audit observes values without activating actions.
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
  } else if ((phase.load() == 5 || phase.load() == 6 || phase.load() == 8 || phase.load() == 9) &&
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
  DrawPauseMenuTheme();
  PollNativePitcherInfo();
  PollNativeBatterInfo();
  DrawBatterCard();
  DrawPitcherCard();
  PollNativeMvpTransition();
  DrawMvpTransition();
  if(dev::Env("MVP06_PREVIEW_STADIUM")) {
    auto tex=gpu::g_ui_image(Assets()/"stadium-intro-preview-bg.png");
    if(tex)ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)tex,{0,0},ImGui::GetIO().DisplaySize);
    PublishStadiumIntro({MatchupSide{"FLORIDA","GATORS","48","23","A+","2"},MatchupSide{"TEXAS","LONGHORNS","56","16","A+","1"}},"Disch-Falk Field",true);
    DrawStadiumIntro();
    return;
  }
  PollNativeStadiumIntro();
  DrawStadiumIntro();
  // Opt-in visual fixture: renders the layout without waiting for a game load.
  // Normal screen routing and native input never use these sample values.
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
    DrawNotification();
    return;
  }
  if (c == 8) {
    PollNativeTeamSelect();
    DrawTeamSelect(Now() - began.load());
    DrawNotification();
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
    DrawNotification();
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
    DrawNotification();
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
    auto tex = gpu::g_ui_image(root / f);
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
    im("lg27.png", 145, 120, 830, 275, alpha);
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
    auto strip = gpu::g_ui_image(root / "menu-selection-imagegen.png");
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
    DrawNotification();
    return;
  }
  im("loading-background.png", 0, 0, 2560, 1440);
  im("licensing-background-imagegen.png", 0, 0, 2560, 1440,
     c == 1 ? Fade(t, 0, .45) : 1);
  if (c == 2 || c == 3)
    im("press-start-background-2560x1440.png", 0, 0, 2560, 1440,
       c == 3 ? 1 : Fade(t, 0, .65));
  float e = c == 3 ? 1 : Fade(t, 0, .9);
  e = 1 - (1 - e) * (1 - e) * (1 - e);
  float x = c == 1 ? 453 : 300, y = c == 1 ? 460 : 440,
        w = c == 1 ? 1653 : 1080, h = c == 1 ? 537 : 360;
  im("lg27.png", x + ((c == 1 ? 300 : 620) - x) * e,
     y + ((c == 1 ? 440 : 450) - y) * e, w + ((c == 1 ? 1080 : 1320) - w) * e,
     h + ((c == 1 ? 360 : 440) - h) * e);
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
