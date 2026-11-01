#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace input {
struct Pad;
}

// host side hooks game_info.cpp hands the runtime
namespace game {

void WorkshopLaunchInit();
void WorkshopLaunchPad(int port, input::Pad& p);
bool WorkshopMenuSelection(const std::string& name);
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

}  // namespace game
