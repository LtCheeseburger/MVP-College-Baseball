#include "startup_ui.h"
#include "ui_style.h"
#include "app/app.h"
#include "game_hooks.h"
#include "overhaul.h"
#include "player_overhaul.h"
#include "kernel/mods.h"
#include "game/pitch_clock.h"
#include "submenu_ui.h"
#include "team_select_ui.h"
#include "imgui.h"
#include "core/dev_tools.h"
#include "gpu/backend.h"
#include <memory>
#include <cstring>
#include <chrono>



namespace game {
bool g_xbox_720p = true;
uint32_t g_frame_aspect = 0;
bool g_ultrawide_menus = false;
bool g_fitted_poll = false;
float FrameAspect() {
    if (g_frame_aspect) return g_frame_aspect / 100.0f;
    app::Mode mode = app::Output();
    return mode.height ? float(mode.width) / float(mode.height) : 16.0f / 9.0f;
}
}

// the UI overhaul draws and takes the pad only while mvp27-ui is on (overhaul.h)
namespace {
void OverhaulWindow(void* hwnd) {
    game::player_overhaul::Install();
    if (overhaul::Active()) game::InitializeUi(hwnd);
}
// Retain draw geometry, not a GPU readback: no synchronous capture or asset
// reads are needed when the loader owns the mount or a modal disables its menu.
void AppendPresentation(ImDrawList* dst,const ImDrawList* src) {
 const int vertex=dst->VtxBuffer.Size,index=dst->IdxBuffer.Size;
 dst->VtxBuffer.resize(vertex+src->VtxBuffer.Size);dst->IdxBuffer.resize(index+src->IdxBuffer.Size);
 if(src->VtxBuffer.Size)std::memcpy(dst->VtxBuffer.Data+vertex,src->VtxBuffer.Data,src->VtxBuffer.Size*sizeof(ImDrawVert));
 if(src->IdxBuffer.Size)std::memcpy(dst->IdxBuffer.Data+index,src->IdxBuffer.Data,src->IdxBuffer.Size*sizeof(ImDrawIdx));
 if(dst->CmdBuffer.Size && !dst->CmdBuffer.back().ElemCount && !dst->CmdBuffer.back().UserCallback)dst->CmdBuffer.pop_back();
 for(auto cmd:src->CmdBuffer){if(!cmd.ElemCount)continue;cmd.VtxOffset+=vertex;cmd.IdxOffset+=index;dst->CmdBuffer.push_back(cmd);}
 dst->_VtxWritePtr=dst->VtxBuffer.Data+dst->VtxBuffer.Size;dst->_IdxWritePtr=dst->IdxBuffer.Data+dst->IdxBuffer.Size;
 dst->_CmdHeader.VtxOffset=dst->VtxBuffer.Size;dst->_VtxCurrentIdx=0;dst->AddDrawCmd();
}
bool FullPresentation(const ImDrawList* d,ImVec2 size){
 for(int i=0;i+3<d->VtxBuffer.Size && i<256;i+=4){const auto* v=d->VtxBuffer.Data+i;
  if(v[0].pos.x<=1 && v[0].pos.y<=1 && v[2].pos.x>=size.x-1 && v[2].pos.y>=size.y-1 && (v[0].col>>24)==255 && (v[2].col>>24)==255)return true;
 }return false;
}
void OverhaulUi(double elapsed) {
 game::ResetActionNavigation();
 static std::unique_ptr<ImDrawList> screen,composed;static ImVec2 dimensions{};static double lastFull=0;
 auto* draw=ImGui::GetBackgroundDrawList();const auto size=ImGui::GetIO().DisplaySize;
 const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 if(size.x!=dimensions.x || size.y!=dimensions.y){screen.reset();composed.reset();dimensions=size;}
 auto mount=kernel::mods::TryLock();
 if(!mount.owns_lock()){if(composed)AppendPresentation(draw,composed.get());return;}
 if(!overhaul::Active()){gpu::g_menu_model_offset=0;screen.reset();composed.reset();return;}
 if(dev::Env("MVP06_TEST_UI_ISSUES")){static double captured=0;if(now-captured>.5){gpu::g_capture=true;captured=now;}}
 game::PollNativeSubmenu();
 const bool modal=game::SubmenuPopupVisible();
 if(modal){if(screen){AppendPresentation(draw,screen.get());lastFull=now;}else game::UiMenuBackdrop(draw,size,game::UiBackdrop::Steel);}
 else{
  game::StartupUi(elapsed);
  if(FullPresentation(draw,size)){screen.reset(draw->CloneOutput());lastFull=now;}
  else if(screen && now-lastFull<.12 && !draw->VtxBuffer.Size)AppendPresentation(draw,screen.get());
  else if(!modal){screen.reset();}
 }
 game::DrawNotification();
 game::DrawPitchClock();
 if(draw->VtxBuffer.Size)composed.reset(draw->CloneOutput());else composed.reset();
}
void OverhaulPad(int port, input::Pad& pad) {
    game::WorkshopLaunchPad(port, pad);

}
}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"MVP 06 NCAA Baseball",     // window and splash
    .short_title = L"MVP 06",             // message boxes
    .name = L"MVP06",                     // window classes, crash dumps, handover event
    .log = "mvp06",                       // log file prefix
    .settings = "mvp06-settings.toml",    // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MVP06"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"xbox_720p", &game::g_xbox_720p, nullptr},
                {"frame_aspect", nullptr, &game::g_frame_aspect},
                {"ultrawide_menus", &game::g_ultrawide_menus, nullptr},
                {"fitted_poll", &game::g_fitted_poll, nullptr},
                {"walkup_volume", nullptr, &game::g_walkup_volume},
                {"key_a", nullptr, &game::g_keys[0]},
                {"key_b", nullptr, &game::g_keys[1]},
                {"key_x", nullptr, &game::g_keys[2]},
                {"key_y", nullptr, &game::g_keys[3]},
                {"key_start", nullptr, &game::g_keys[4]},
                {"key_back", nullptr, &game::g_keys[5]},
                {"key_left_trigger", nullptr, &game::g_keys[6]},
                {"key_right_trigger", nullptr, &game::g_keys[7]},
                {"key_white", nullptr, &game::g_keys[8]},
                {"key_black", nullptr, &game::g_keys[9]}},  // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = game::WorkshopLaunchInit,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = OverhaulWindow,             // load UI fonts before renderer boot
    .message = nullptr,                   // window messages first; true takes one
    .ui = OverhaulUi,                     // ImGui drawing each frame
    .pad = OverhaulPad,                   // changes a polled pad: the keyboard as the first controller
    .rumble = nullptr,                    // changes a rumble before it is set
    .required_mods = {"mvp27-league"},
    .required_mod_replacement = overhaul::kCombinedModId,
};
