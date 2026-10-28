#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace input {
struct Pad;
}

// the host side: window, settings
namespace app {

enum class DisplayMode : uint32_t { Fullscreen, Borderless, Windowed };

constexpr uint32_t kMinRenderPercent = 25, kMaxRenderPercent = 200;

struct Settings {
    DisplayMode display_mode = DisplayMode::Borderless;
    uint32_t width = 0, height = 0;  // 0 is the desktop mode
    uint32_t render_percent = 100;   // of the output height, 25 to 200: past 100 the frame is averaged down, below it scaled up
    uint32_t internal_height = 0;    // internal_resolution: 0 "native" follows render_percent, else 720/1080/1440 drawn whatever the window
    uint32_t fps_limit = 60;         // presentation pacing only, the guest keeps its 60 Hz blank clock; 0 off, else 30-1000
    bool vsync = true;               // independent of fps_limit: the swap waits for the monitor's refresh
    uint32_t frame_stats = 0;        // 1 log each second, 2 overlay, 3 both: guest and host frame times min/avg/max
    uint32_t background_fps = 30;    // fps_limit while the window is not the active one, 0 unlimited
    bool mute_unfocused = true;      // no sound while the window is not the active one
    uint32_t aniso = 0;              // 0 as the game asks
    bool fxaa = true;                // antialiasing = "fxaa": edges smoothed on the finished frame, else "off"
    bool fsr = false;                // upscaler = "fsr1": AMD FSR 1 when drawn smaller than the window, else "bilinear"
    uint32_t sharpening = 0;         // 0 off, 1 to 100 AMD RCAS from weakest to strongest
    bool dof = true;                 // depth_of_field: the game's blur of the background, else the sharp frame
    bool glow = true;                // glow: the bloom the game adds over bright areas
    bool fx_high = true;             // effects_quality = "high": finer glow, blur and shadow buffers, else "original"
    bool custom_textures = false;    // images in textures\replacement\ beside the exe replace the game's
    bool dump_textures = false;      // each texture once into textures\dumps\ beside the exe
    uint32_t texture_dump_format = 0;  // 0 PNG, 1 DDS
    uint32_t screenshot_key = 0x7B;  // virtual key code, F12: the frame as a PNG in screenshots\, 0 off
    uint32_t save_backups = 10;      // copies of the saves folder at launch and exit kept in backups\, 0 off
    uint32_t save_history = 5;       // earlier versions kept per save in save_history\, 2 to 20
    uint32_t rumble = 100;           // controller vibration strength, 0 off to 100 as the game asks
    uint32_t deadzone_left = 0;      // round stick deadzone, percent of full tilt, 0 to 50; 0 leaves the game's own only
    uint32_t deadzone_right = 0;
    bool nintendo_labels = true;     // nintendo_buttons = "label": a Nintendo pad's A is A, else "position" (south is A)
    std::string join_ip;                   // co-op: a host past the LAN the system link search also asks
};

Settings& Get();
void Load();
void Save();
void LogSettings();  // every setting, as the settings file has them
void LogSystem();    // the machine and the settings, at a run's start
void ApplyDisplay();  // any thread
struct Mode {
    uint32_t width, height;
};
Mode Desktop();
Mode Output();
float RenderScale(uint32_t guest_height);
// the output is 3:2 or wider: the console reports a widescreen TV and the frame is shown 16:9, else 4:3 as on a 4:3 TV
bool Widescreen();

std::filesystem::path ExeDir();
// where the player's files go: Documents\<name> Recomp (saves, settings, logs, crash dumps), or the exe's folder
// when portable.txt is beside it; the first call moves in what older versions left beside the exe
std::filesystem::path UserDir();
// the game in assets (game_files.cpp): its folder and this executable's xbe, from the extracted files or a disc
// image (.iso), which it mounts as D:; false after telling the player what is missing
struct GameFiles {
    std::filesystem::path dir;
    std::vector<uint8_t> xbe;
};
bool FindGame(GameFiles& game);
std::string Sha1(const std::vector<uint8_t>& data);  // lower case hex
// the recompiled code, <exe>.dll, built from the game in assets when missing or out of date (game_code.cpp) and
// loaded; false after telling the player why the game cannot start, or when they chose not to start it yet
bool PrepareGameCode(const GameFiles& game);
// --build-game-code (setup): builds what is missing behind the same window and exits; 0 ready and the player chose
// Start (or nothing to build), 2 ready and they chose Exit, 1 failed or stopped
int BuildGameCode(const GameFiles& game);
int Instance();  // --instance N on the command line, 0 without: a second copy on this PC for system link tests
void* Window();  // HWND
// boot splash: GameInfo::splash .bmp or .png beside the exe, up before the game window
bool OpenSplash();        // window thread, at launch; false without one, and the game window opens at once
void SplashFirstFrame();  // the game's first Swap: waits out the splash's minimum, then opens the game window
void CloseSplash();       // window thread
void OpenGameWindow(bool apply = true);  // any thread but the window thread's; returns once the window is up
// relaunched: the previous instance keeps its window up until this one has shown its first frames (handover.cpp)
bool StartHandover();   // window thread, at launch; true: the game window starts hidden
bool HandingOver();
void FinishHandover();  // any thread but the window thread's: opens the game window, lets the old instance go

// held around ImGui: the window thread's messages and the renderer's frame
std::recursive_mutex& UiLock();

// what the game repo defines once
struct Option {  // a setting in the settings file, flag or value
    const char* key;
    bool* flag;
    uint32_t* value;
};
struct GameInfo {
    const wchar_t* title;        // window and splash
    const wchar_t* short_title;  // message boxes
    const wchar_t* name;         // window classes, crash dumps, handover event
    const char* log;             // log prefix
    const char* settings;        // settings file beside the exe
    const wchar_t* splash;       // .bmp or .png beside the exe
    const wchar_t* missing;      // message when neither the game files nor a disc image are found
    std::vector<std::pair<const char*, const char*>> images;  // xbe file name, executable name
    std::vector<Option> options;
    // the settings file's toml::table, for what options cannot hold
    void (*load)(const void* table);
    void (*save)(void* table);
    void (*init)();                // after the settings load, before the window and boot
    void (*loaded)();              // image in memory, before its entry runs
    void (*window)(void* hwnd);    // the game window is up, before boot
    // window thread, each message after ImGui's; true returns result instead of the host's handling
    bool (*message)(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
    void (*ui)(double fps);        // each frame inside the ImGui frame, UiLock held
    void (*pad)(int port, input::Pad& pad);  // a polled pad before the game gets it
    void (*rumble)(int port, uint16_t& left, uint16_t& right);
};
extern const GameInfo kGame;

// frame_pacing.cpp. Pace sleeps out the rest of the frame for the fps cap, or the background cap while the window is
// not active, unless the display paces it: SwapInterval is the refreshes a vsync present waits for, 0 when the
// limiter paces (or vsync off)
void Pace();
void SleepUntil(std::chrono::steady_clock::time_point t);  // to within a few microseconds, any thread
double RefreshRate();       // Hz of the window's monitor, 0 unknown
uint32_t SwapInterval();
void NoteGuestFrame();      // each guest swap, after Pace
void NoteHostPresent();     // each host present, after it returns
std::string FrameStatsText();  // the overlay's lines for the last second
bool Focused();   // any thread: the game window is the active one
bool MuteNow();   // any thread: mute_unfocused and the window is not active

}  // namespace app
