// Keyboard as the first controller, merged with a real one on that port, while the game window has focus:
//   arrows d-pad, WASD left stick, IJKL right stick, Space A, X B, C X, V Y, Enter Start, Tab Back,
//   Q left trigger, E right trigger, R white, F black, T left thumb, G right thumb
// The ten buttons and triggers after A to Back can be rebound (key_* in the settings file, PC Options); the rest are fixed.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "game_hooks.h"

namespace game {

uint32_t g_keys[kKeyCount] = {VK_SPACE, 'X', 'C', 'V', VK_RETURN, VK_TAB, 'Q', 'E', 'R', 'F'};

const char* const kKeyNames[kKeyCount] = {"A", "B", "X", "Y", "Start", "Back", "Left Trigger", "Right Trigger", "White", "Black"};

void ResetKeys() {
    const uint32_t defaults[kKeyCount] = {VK_SPACE, 'X', 'C', 'V', VK_RETURN, VK_TAB, 'Q', 'E', 'R', 'F'};
    for (int i = 0; i < kKeyCount; i++) g_keys[i] = defaults[i];
}

const std::vector<KeyChoice>& KeyChoices() {
    static const std::vector<KeyChoice> keys = [] {
        std::vector<KeyChoice> k = {{VK_SPACE, L"Space"}, {VK_RETURN, L"Enter"}, {VK_TAB, L"Tab"}, {VK_BACK, L"Backspace"},
                                    {VK_SHIFT, L"Shift"}, {VK_CONTROL, L"Ctrl"}, {VK_MENU, L"Alt"}, {VK_INSERT, L"Insert"},
                                    {VK_DELETE, L"Delete"}, {VK_HOME, L"Home"}, {VK_END, L"End"}, {VK_PRIOR, L"Page Up"},
                                    {VK_NEXT, L"Page Down"}, {VK_OEM_COMMA, L","}, {VK_OEM_PERIOD, L"."},
                                    {VK_OEM_2, L"/"}, {VK_OEM_1, L";"}, {VK_OEM_7, L"'"}, {VK_OEM_4, L"["}, {VK_OEM_6, L"]"}};
        static const wchar_t* const kLetters[] = {L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J", L"K", L"L", L"M",
                                                  L"N", L"O", L"P", L"Q", L"R", L"S", L"T", L"U", L"V", L"W", L"X", L"Y", L"Z"};
        static const wchar_t* const kDigits[] = {L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"};
        static const wchar_t* const kPad[] = {L"Num 0", L"Num 1", L"Num 2", L"Num 3", L"Num 4",
                                              L"Num 5", L"Num 6", L"Num 7", L"Num 8", L"Num 9"};
        for (int i = 0; i < 26; i++) k.push_back({uint32_t('A' + i), kLetters[i]});
        for (int i = 0; i < 10; i++) k.push_back({uint32_t('0' + i), kDigits[i]});
        for (int i = 0; i < 10; i++) k.push_back({uint32_t(VK_NUMPAD0 + i), kPad[i]});
        return k;
    }();
    return keys;
}

namespace {

bool Down(int vk) { return GetAsyncKeyState(vk) & 0x8000; }

int16_t Axis(int neg, int pos) { return int16_t((Down(pos) ? 32767 : 0) - (Down(neg) ? 32767 : 0)); }

void Analog(input::Pad& p, int index, int vk) {
    if (Down(vk)) p.analog[index] = 255;
}

}  // namespace

void KeyboardPad(int port, input::Pad& p) {
    if (port != 0) return;
    p.connected = true;  // a keyboard player always has a controller in port 1
    if (GetForegroundWindow() != static_cast<HWND>(app::Window())) return;
    const int kButtons[][2] = {{VK_UP, 0x01},    {VK_DOWN, 0x02}, {VK_LEFT, 0x04}, {VK_RIGHT, 0x08},
                               {int(g_keys[4]), 0x10}, {int(g_keys[5]), 0x20}, {'T', 0x40}, {'G', 0x80}};
    for (auto& b : kButtons)
        if (Down(b[0])) p.buttons |= uint16_t(b[1]);
    Analog(p, 0, int(g_keys[0]));  // A
    Analog(p, 1, int(g_keys[1]));       // B
    Analog(p, 2, int(g_keys[2]));       // X
    Analog(p, 3, int(g_keys[3]));       // Y
    Analog(p, 4, int(g_keys[9]));       // black
    Analog(p, 5, int(g_keys[8]));       // white
    Analog(p, 6, int(g_keys[6]));       // left trigger
    Analog(p, 7, int(g_keys[7]));       // right trigger
    if (int16_t x = Axis('A', 'D')) p.lx = x;
    if (int16_t y = Axis('S', 'W')) p.ly = y;
    if (int16_t x = Axis('J', 'L')) p.rx = x;
    if (int16_t y = Axis('K', 'I')) p.ry = y;
}

}  // namespace game
