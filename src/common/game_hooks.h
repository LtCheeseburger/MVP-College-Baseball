#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace input {
struct Pad;
}

// host side hooks game_info.cpp hands the runtime
namespace game {

void WorkshopLaunchInit();
void WorkshopLaunchPad(int port, input::Pad& pad);
bool WorkshopMenuSelection(const std::string& name);
void WorkshopObserve(uint32_t root);
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
uint32_t PcOptionsString(uint32_t id);
std::string PcOptionValue(uint32_t index);

// settings file options, game_info.cpp
extern bool g_xbox_720p;  // the game's 1280x720 mode on a widescreen output; read at boot
extern uint32_t g_walkup_volume;  // custom walk-up volume %, 0 preserves stock ditties
extern uint32_t g_frame_aspect;
extern bool g_ultrawide_menus;
float FrameAspect();
bool StartupSplashActive();
void EndStartupLoading();

}  // namespace game
