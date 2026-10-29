#pragma once
#include <cstdint>
#include <vector>

namespace input {
struct Pad;
}

// host side hooks game_info.cpp hands the runtime
namespace game {

void KeyboardPad(int port, input::Pad& p);  // keyboard.cpp

// rebindable keyboard buttons, keyboard.cpp: virtual key codes for A, B, X, Y, Start, Back, LT, RT, White, Black
constexpr int kKeyCount = 10;
struct KeyChoice {
    uint32_t vk;
    const wchar_t* name;
};
extern uint32_t g_keys[kKeyCount];
extern const char* const kKeyNames[kKeyCount];
const std::vector<KeyChoice>& KeyChoices();
void ResetKeys();

// main menu Quit Game row, main_menu.cpp: the label's string id and its text (UTF-16 in guest memory)
constexpr uint32_t kQuitString = 0xFDF0;
uint32_t QuitText();

// settings file options, game_info.cpp
extern bool g_xbox_720p;  // the game's 1280x720 mode on a widescreen output; read at boot
// frame_aspect: the 720p frame's width over height in hundredths (356 = 32:9, 237 = 21:9); 0 follows the output.
// Wider than 16:9 widens the game's frame buffers, camera and HUD (ultrawide.cpp); read at boot.
extern uint32_t g_frame_aspect;
// ultrawide_menus: the menus fill an ultrawide frame too; off (default) they stay 16:9, pillarboxed (ultrawide.cpp)
extern bool g_ultrawide_menus;
float FrameAspect();
// walkup_volume: custom walk-up songs from walkup\ beside the exe (walkup.cpp), their volume in percent (default 100);
// 0 turns them off and every batter keeps the game's own ditty
extern uint32_t g_walkup_volume;
// fitted_poll: the national poll fitted to the real Top 25 (poll.cpp) with the stock database too; an expanded
// database always uses it
extern bool g_fitted_poll;

}  // namespace game
