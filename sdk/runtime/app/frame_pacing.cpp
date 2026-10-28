// frame pacing: the precise waits, the frame limiter, how many refreshes a present waits for, and the frame time
// statistics (frame_stats = "log" / "overlay" / "both")
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace app {

namespace {

using clock = std::chrono::steady_clock;

// the refresh rate of the monitor the window is on, exact (59.94 is not 59), from the display configuration
double QueryRefresh() {
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return 0;
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return 0;
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size = sizeof(name);
        name.header.adapterId = paths[i].sourceInfo.adapterId;
        name.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS || wcscmp(name.viewGdiDeviceName, mi.szDevice) != 0)
            continue;
        const DISPLAYCONFIG_RATIONAL& r = paths[i].targetInfo.refreshRate;
        if (r.Denominator) return double(r.Numerator) / r.Denominator;
    }
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    return EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1
               ? double(dm.dmDisplayFrequency)
               : 0;
}

std::atomic<double> g_refresh{0};
std::atomic<int64_t> g_refresh_at{0};

uint32_t Cap() { return Focused() ? Get().fps_limit : Get().background_fps; }

// min/avg/max of the intervals between calls, over a second
struct Window1s {
    clock::time_point last{}, start{};
    double min = 1e9, max = 0, sum = 0;
    uint32_t n = 0, hitches = 0;
    // the last full second
    double out_min = 0, out_avg = 0, out_max = 0, out_rate = 0;
    uint32_t out_hitches = 0;

    // true when a second just finished; target_ms 0 counts no hitches
    bool Note(double target_ms) {
        auto now = clock::now();
        bool done = false;
        if (last != clock::time_point{}) {
            double ms = std::chrono::duration<double, std::milli>(now - last).count();
            min = std::min(min, ms), max = std::max(max, ms), sum += ms, n++;
            if (target_ms > 0 && ms > target_ms * 1.5) hitches++;
            double el = std::chrono::duration<double>(now - start).count();
            if (el >= 1.0) {
                out_min = min, out_max = max, out_avg = sum / n, out_rate = n / el, out_hitches = hitches;
                min = 1e9, max = sum = 0, n = hitches = 0;
                start = now;
                done = true;
            }
        } else {
            start = now;
        }
        last = now;
        return done;
    }
};

std::mutex g_stats_lock;
Window1s g_guest, g_host;
std::string g_text;

void UpdateText() {
    double refresh = RefreshRate();
    uint32_t k = SwapInterval(), cap = Cap();
    char mode[64];
    if (!Get().vsync) snprintf(mode, sizeof(mode), "vsync off, limiter %u", cap);
    else if (k) snprintf(mode, sizeof(mode), "vsync, %u refresh%s a frame", k, k > 1 ? "es" : "");
    else if (cap) snprintf(mode, sizeof(mode), "vsync + limiter %u", cap);
    else snprintf(mode, sizeof(mode), "vsync");
    char buf[512];
    snprintf(buf, sizeof(buf),
             "%.2f Hz display, %s\n"
             "guest %6.2f fps  %5.2f / %5.2f / %5.2f ms  hitches %u\n"
             "host  %6.2f fps  %5.2f / %5.2f / %5.2f ms  hitches %u",
             refresh, mode, g_guest.out_rate, g_guest.out_min, g_guest.out_avg, g_guest.out_max, g_guest.out_hitches,
             g_host.out_rate, g_host.out_min, g_host.out_avg, g_host.out_max, g_host.out_hitches);
    g_text = buf;
}

}  // namespace

void SleepUntil(std::chrono::steady_clock::time_point t) {
    // a high resolution timer to half a millisecond before, then a spin: Sleep(1) wakes up to a millisecond late
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    constexpr auto kSpin = std::chrono::microseconds(500);
    auto left = t - clock::now();
    if (left > kSpin) {
        LARGE_INTEGER due;
        due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(left - kSpin).count() / 100;
        if (timer && SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0))
            WaitForSingleObject(timer, INFINITE);
        else
            while (clock::now() + std::chrono::milliseconds(2) < t) Sleep(1);
    }
    while (clock::now() < t) _mm_pause();
}

double RefreshRate() {
    int64_t now = clock::now().time_since_epoch().count();
    // the window may move to another monitor or the mode change: asked again every second
    if (now - g_refresh_at.load() > std::chrono::duration_cast<clock::duration>(std::chrono::seconds(1)).count()) {
        g_refresh_at = now;
        g_refresh = QueryRefresh();
    }
    return g_refresh.load();
}

uint32_t SwapInterval() {
    if (!Get().vsync) return 0;
    uint32_t cap = Cap();
    if (!cap) return 1;
    double refresh = RefreshRate();
    if (refresh <= 0) return 0;
    // the display paces the frames when its refresh is a whole multiple of the limit (60 on 60, 120 or 240 Hz, 30 on
    // 60): the limiter's clock would drift against the display's and drop or double a frame every few seconds
    double k = std::round(refresh / cap);
    if (k < 1 || k > 4 || std::fabs(refresh - k * cap) > refresh * 0.005) return 0;
    return uint32_t(k);
}

void Pace() {
    static clock::time_point next = clock::now();
    uint32_t cap = Cap();
    auto now = clock::now();
    if (!cap || SwapInterval()) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    SleepUntil(next);
}

void NoteGuestFrame() {
    if (!Get().frame_stats) return;
    std::lock_guard lock(g_stats_lock);
    uint32_t cap = Cap();
    if (g_guest.Note(cap ? 1000.0 / cap : 0)) {
        UpdateText();
        if (Get().frame_stats & 1)
            LOGR("frame: guest %.2f fps, %.2f/%.2f/%.2f ms min/avg/max, %u hitches; host %.2f fps, %.2f/%.2f/%.2f ms, %u hitches; %.2f Hz, vsync %d, limit %u, interval %u",
                 g_guest.out_rate, g_guest.out_min, g_guest.out_avg, g_guest.out_max, g_guest.out_hitches,
                 g_host.out_rate, g_host.out_min, g_host.out_avg, g_host.out_max, g_host.out_hitches, RefreshRate(),
                 Get().vsync, cap, SwapInterval());
    }
}

void NoteHostPresent() {
    if (!Get().frame_stats) return;
    std::lock_guard lock(g_stats_lock);
    uint32_t cap = Cap();
    g_host.Note(cap ? 1000.0 / cap : 0);
}

std::string FrameStatsText() {
    std::lock_guard lock(g_stats_lock);
    return g_text;
}

}  // namespace app
