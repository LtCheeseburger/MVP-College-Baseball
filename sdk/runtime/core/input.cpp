// controllers through SDL: Xbox, PlayStation and Switch pads read into the console controller's report
#include "core/input.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>

#include <SDL3/SDL.h>

#include "app/app.h"
#include "core/log.h"

namespace input {

namespace {

// a controller takes the lowest free port when it is plugged in and keeps it until it is unplugged
std::mutex g_lock;
bool g_sdl = false;
SDL_Gamepad* g_pads[4] = {};
// per port, the pad's buttons read as the console's A B X Y
SDL_GamepadButton g_face[4][4] = {};

constexpr SDL_GamepadButton kPositions[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                                            SDL_GAMEPAD_BUTTON_NORTH};

// SDL reports face buttons by position, so a PlayStation pad's cross is A as the console had it. A Nintendo pad
// has A and B (and X and Y) the other way round; with nintendo_buttons = "label" the button printed A is A.
void FaceButtons(int port) {
    SDL_Gamepad* pad = g_pads[port];
    std::copy(std::begin(kPositions), std::end(kPositions), g_face[port]);
    if (!app::Get().nintendo_labels) return;
    constexpr SDL_GamepadButtonLabel kLabels[] = {SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_B,
                                                  SDL_GAMEPAD_BUTTON_LABEL_X, SDL_GAMEPAD_BUTTON_LABEL_Y};
    SDL_GamepadButton found[4];
    for (int i = 0; i < 4; i++) {
        auto it = std::find_if(std::begin(kPositions), std::end(kPositions),
                               [&](SDL_GamepadButton b) { return SDL_GetGamepadButtonLabel(pad, b) == kLabels[i]; });
        if (it == std::end(kPositions)) return;  // shapes (PlayStation) or unknown: by position
        found[i] = *it;
    }
    std::copy(std::begin(found), std::end(found), g_face[port]);
}

void AddPad(SDL_JoystickID id) {
    for (SDL_Gamepad* pad : g_pads)
        if (pad && SDL_GetGamepadID(pad) == id) return;
    SDL_Gamepad** slot = std::find(std::begin(g_pads), std::end(g_pads), nullptr);
    if (slot == std::end(g_pads)) return;
    int port = int(slot - g_pads);
    *slot = SDL_OpenGamepad(id);
    if (!*slot) {
        LOGI("input: controller %d failed to open: %s", port + 1, SDL_GetError());
        return;
    }
    SDL_SetGamepadPlayerIndex(*slot, port);  // the player lights on pads that have them
    FaceButtons(port);
    const char* name = SDL_GetGamepadName(*slot);
    const char* type = SDL_GetGamepadStringForType(SDL_GetGamepadType(*slot));
    LOGI("input: controller %d plugged in: %s (%s, A is %s)", port + 1, name ? name : "", type ? type : "unknown",
         g_face[port][0] == SDL_GAMEPAD_BUTTON_SOUTH ? "south" : "east");
}

// every pad SDL knows that has no port yet, e.g. a fifth one when a port is freed
void AddWaiting() {
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++) AddPad(ids[i]);
        SDL_free(ids);
    }
}

void RemovePad(SDL_JoystickID id) {
    bool freed = false;
    for (int i = 0; i < 4; i++) {
        if (!g_pads[i] || SDL_GetGamepadID(g_pads[i]) != id) continue;
        SDL_CloseGamepad(g_pads[i]);
        g_pads[i] = nullptr;
        freed = true;
        LOGI("input: controller %d unplugged", i + 1);
    }
    if (freed) AddWaiting();
}

// a round deadzone of percent of full tilt, the rest stretched back over the full range
void Deadzone(int& x, int& y, uint32_t percent) {
    if (!percent) return;
    double m = std::sqrt(double(x) * x + double(y) * y);
    double dz = 32767.0 * percent / 100;
    if (m <= dz) {
        x = y = 0;
        return;
    }
    double k = std::min((m - dz) / (32767.0 - dz), 1.0) * 32767.0 / m;
    x = std::clamp(int(std::lround(x * k)), -32768, 32767);
    y = std::clamp(int(std::lround(y * k)), -32768, 32767);
}

// once a millisecond at most, however many ports and threads read
void UpdatePads() {
    static uint64_t last = 0;
    uint64_t now = SDL_GetTicks();
    if (!g_sdl || now == last) return;
    last = now;
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) AddPad(e.gdevice.which);
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) RemovePad(e.gdevice.which);
    }
}

}  // namespace

void Init() {
    if (g_sdl) return;  // started already for the game code window
    // WGI reads the triggers apart and HIDAPI takes PlayStation and Switch pads. Reads come from the guest's threads,
    // so SDL's own thread takes the device notices.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        LOGE("input: SDL failed to start, no controllers: %s", SDL_GetError());
        return;
    }
    int v = SDL_GetVersion();
    LOGI("input: SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    std::lock_guard lock(g_lock);
    g_sdl = true;
    AddWaiting();
}

Pad Poll(int port) {
    Pad p;
    {
        std::lock_guard lock(g_lock);
        UpdatePads();
        if (SDL_Gamepad* pad = port >= 0 && port < 4 ? g_pads[port] : nullptr) {
            p.connected = true;
            // unfocused the pad reads as untouched, so nothing is held when the window comes back
            if (!app::Focused()) return p;
            constexpr SDL_GamepadButton kButtons[] = {
                SDL_GAMEPAD_BUTTON_DPAD_UP,   SDL_GAMEPAD_BUTTON_DPAD_DOWN,  SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_START,     SDL_GAMEPAD_BUTTON_BACK,
                SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
            for (int i = 0; i < 8; i++) p.buttons |= uint16_t(SDL_GetGamepadButton(pad, kButtons[i]) << i);
            // a PlayStation touchpad press is Back as well as Share/Create
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_TOUCHPAD)) p.buttons |= 0x20;
            // A B X Y, then black white
            for (int i = 0; i < 4; i++) p.analog[i] = SDL_GetGamepadButton(pad, g_face[port][i]) ? 0xFF : 0;
            p.analog[4] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ? 0xFF : 0;
            p.analog[5] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? 0xFF : 0;
            auto axis = [&](SDL_GamepadAxis a) { return int(SDL_GetGamepadAxis(pad, a)); };
            // 0 to 32767 into 0 to 255
            p.analog[6] = uint8_t(std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0) >> 7);
            p.analog[7] = uint8_t(std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0) >> 7);
            int lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
            int rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
            Deadzone(lx, ly, app::Get().deadzone_left);
            Deadzone(rx, ry, app::Get().deadzone_right);
            // SDL's sticks have y down
            // not plain negation: -32768 has no positive
            p.lx = int16_t(lx);
            p.ly = int16_t(-1 - ly);
            p.rx = int16_t(rx);
            p.ry = int16_t(-1 - ry);
        }
    }
    if (app::kGame.pad) app::kGame.pad(port, p);
    return p;
}

uint16_t HeldOnAnyController() {
    std::lock_guard lock(g_lock);
    UpdatePads();
    uint16_t held = 0;
    if (!app::Focused()) return held;
    // XINPUT_GAMEPAD_A, B, X, Y
    for (int port = 0; port < 4; port++)
        for (int i = 0; i < 4; i++)
            if (g_pads[port] && SDL_GetGamepadButton(g_pads[port], g_face[port][i])) held |= uint16_t(0x1000 << i);
    return held;
}

// the motors run until set again, scaled by the rumble setting
void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    uint32_t strength = app::Get().rumble;
    left = uint16_t(left * strength / 100);
    right = uint16_t(right * strength / 100);
    std::lock_guard lock(g_lock);
    if (port >= 0 && port < 4 && g_pads[port]) SDL_RumbleGamepad(g_pads[port], left, right, 0);
}

}  // namespace input
