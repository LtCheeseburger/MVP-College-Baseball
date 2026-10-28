// the settings file beside the exe
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace app {

namespace {

const char* kModes[] = {"fullscreen", "borderless", "windowed"};

std::filesystem::path File() { return UserDir() / kGame.settings; }

}  // namespace

Settings& Get() {
    static Settings s;
    return s;
}

void Load() {
    Settings& s = Get();
    toml::table t;
    try {
        t = toml::parse_file(File().string());
    } catch (...) {
        return;
    }
    std::string mode = t["display_mode"].value_or(std::string(kModes[uint32_t(s.display_mode)]));
    for (uint32_t i = 0; i < 3; i++)
        if (mode == kModes[i]) s.display_mode = DisplayMode(i);
    s.width = t["width"].value_or(s.width);
    s.height = t["height"].value_or(s.height);
    s.render_percent = std::clamp(t["render_percent"].value_or(s.render_percent), kMinRenderPercent, kMaxRenderPercent);
    std::string ires = t["internal_resolution"].value_or(std::string("native"));
    s.internal_height = ires == "720p" ? 720 : ires == "1080p" ? 1080 : ires == "1440p" ? 1440 : 0;
    // fps_limit and before it fps_cap were the older names; fps_cap 1 meant the monitor's refresh rate
    uint32_t limit = t["frame_limit"].value_or(t["fps_limit"].value_or(t["fps_cap"].value_or(s.fps_limit)));
    s.fps_limit = limit == 1 ? 0 : limit ? std::clamp(limit, 30u, 1000u) : 0;
    s.vsync = t["vsync"].value_or(s.vsync);
    std::string stats = t["frame_stats"].value_or(std::string("off"));
    s.frame_stats = stats == "log" ? 1 : stats == "overlay" ? 2 : stats == "both" ? 3 : 0;
    s.background_fps = t["background_fps"].value_or(s.background_fps);
    s.mute_unfocused = t["mute_unfocused"].value_or(s.mute_unfocused);
    s.aniso = t["anisotropic_filtering"].value_or(s.aniso);
    s.fxaa = t["antialiasing"].value_or(std::string(s.fxaa ? "fxaa" : "off")) != "off";
    s.fsr = t["upscaler"].value_or(std::string(s.fsr ? "fsr1" : "bilinear")) != "bilinear";
    s.sharpening = std::min(t["sharpening"].value_or(s.sharpening), 100u);
    s.dof = t["depth_of_field"].value_or(s.dof);
    s.glow = t["glow"].value_or(s.glow);
    s.fx_high = t["effects_quality"].value_or(std::string(s.fx_high ? "high" : "original")) != "original";
    s.custom_textures = t["custom_textures"].value_or(s.custom_textures);
    s.dump_textures = t["dump_textures"].value_or(s.dump_textures);
    s.texture_dump_format = std::min(t["texture_dump_format"].value_or(s.texture_dump_format), 1u);
    s.screenshot_key = std::min(t["screenshot_key"].value_or(s.screenshot_key), 0xFEu);
    s.save_backups = std::min(t["save_backups"].value_or(s.save_backups), 100u);
    s.save_history = std::clamp(t["save_history"].value_or(s.save_history), 2u, 20u);
    s.rumble = std::min(t["rumble"].value_or(s.rumble), 100u);
    s.deadzone_left = std::min(t["deadzone_left"].value_or(s.deadzone_left), 50u);
    s.deadzone_right = std::min(t["deadzone_right"].value_or(s.deadzone_right), 50u);
    s.nintendo_labels =
        t["nintendo_buttons"].value_or(std::string(s.nintendo_labels ? "label" : "position")) != "position";
    s.join_ip = t["join_ip"].value_or(s.join_ip);
    for (const Option& o : kGame.options) {
        if (o.flag) *o.flag = t[o.key].value_or(*o.flag);
        else *o.value = t[o.key].value_or(*o.value);
    }
    if (kGame.load) kGame.load(&t);
}

namespace {

toml::table Table() {
    const Settings& s = Get();
    toml::table t{{"display_mode", kModes[uint32_t(s.display_mode)]},
                  {"width", int64_t(s.width)},
                  {"height", int64_t(s.height)},
                  {"render_percent", int64_t(s.render_percent)},
                  {"internal_resolution", s.internal_height ? std::to_string(s.internal_height) + "p" : "native"},
                  {"frame_limit", int64_t(s.fps_limit)},
                  {"vsync", s.vsync},
                  {"frame_stats", s.frame_stats == 1 ? "log" : s.frame_stats == 2 ? "overlay" : s.frame_stats == 3 ? "both" : "off"},
                  {"background_fps", int64_t(s.background_fps)},
                  {"mute_unfocused", s.mute_unfocused},
                  {"anisotropic_filtering", int64_t(s.aniso)},
                  {"antialiasing", s.fxaa ? "fxaa" : "off"},
                  {"upscaler", s.fsr ? "fsr1" : "bilinear"},
                  {"sharpening", int64_t(s.sharpening)},
                  {"depth_of_field", s.dof},
                  {"glow", s.glow},
                  {"effects_quality", s.fx_high ? "high" : "original"},
                  {"custom_textures", s.custom_textures},
                  {"dump_textures", s.dump_textures},
                  {"texture_dump_format", int64_t(s.texture_dump_format)},
                  {"screenshot_key", int64_t(s.screenshot_key)},
                  {"save_backups", int64_t(s.save_backups)},
                  {"save_history", int64_t(s.save_history)},
                  {"rumble", int64_t(s.rumble)},
                  {"deadzone_left", int64_t(s.deadzone_left)},
                  {"deadzone_right", int64_t(s.deadzone_right)},
                  {"nintendo_buttons", s.nintendo_labels ? "label" : "position"},
                  {"join_ip", s.join_ip}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

}  // namespace

void Save() {
    std::ofstream f(File());
    f << Table() << "\n";
}

void LogSettings() {
    std::ostringstream text;
    text << Table();
    std::istringstream lines(text.str());
    LOGR("Settings:");
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) LOGR("  %s", line.c_str());
}

Mode Desktop() {
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
        return {dm.dmPelsWidth, dm.dmPelsHeight};
    return {uint32_t(GetSystemMetrics(SM_CXSCREEN)), uint32_t(GetSystemMetrics(SM_CYSCREEN))};
}

Mode Output() {
    const Settings& s = Get();
    if (s.width && s.height) return {s.width, s.height};
    return Desktop();
}

bool Widescreen() {
    // asked per frame and per HUD quad: the desktop mode lookup only runs when the resolution setting changes
    static std::atomic<uint64_t> key{~0ull};
    static std::atomic<bool> wide{true};
    const Settings& s = Get();
    uint64_t k = uint64_t(s.width) << 32 | s.height;
    if (key.exchange(k) != k) {
        Mode m = Output();
        wide = m.width * 2 >= m.height * 3;  // 3:2 or wider
    }
    return wide;
}

// Output for every frame: the desktop mode lookup reads the registry, so it runs again only when the resolution
// setting or the window's monitor changes
Mode CachedOutput() {
    struct Key {
        uint32_t width;
        uint32_t height;
        HMONITOR monitor;
        bool operator==(const Key&) const = default;
    };
    static std::mutex lock;
    static Key key{};
    static Mode mode{};
    const Settings& s = Get();
    Key now{s.width, s.height, MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY)};
    std::lock_guard<std::mutex> guard(lock);
    if (!(now == key) || !mode.height) {
        key = now;
        mode = Output();
    }
    return mode;
}

float RenderScale(uint32_t guest_height) {
    float h = Get().internal_height ? float(Get().internal_height)
                                    : CachedOutput().height * (std::clamp(Get().render_percent, kMinRenderPercent, kMaxRenderPercent) / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

}  // namespace app
