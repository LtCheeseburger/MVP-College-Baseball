#include "startup_ui.h"
#include "ui_style.h"
#include "app/app.h"
#include "game_hooks.h"
#include "overhaul.h"



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
    if (overhaul::Active()) game::InitializeUi(hwnd);
}
void OverhaulUi(double elapsed) {
    if (overhaul::Active()) game::StartupUi(elapsed);
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
};
