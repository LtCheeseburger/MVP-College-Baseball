// PC Options inside the game's own menus. The Options menu's "Online Options" entry leads to the game's settings list
// screen (fes_smart_options, the one Audio Options uses) with the OPTIONS_ONLINE context; without Xbox Live it has
// nothing to show, so it becomes "PC Options" and that context gets rows of its own:
//   - the screen factory (0x112BC0) makes it the list screen's base class (ctor 0x55BD70) with our row ids,
//   - the row factory (0x543BF0) makes each of our ids the game's plain choice list row (vtable 0x63D2E8),
//   - the settings getter and setter (0x5231E0, 0x523690) read and write our ids from app::Settings,
//   - the string lookup (0x83B00) returns our labels for ids from kStringBase on.
// A row's value is the index of its choice.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "common/game_hooks.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kFlowSingleton = 0x6C680C;  // the front end flow; +4 the context the next screen opens with
constexpr uint32_t kContextOnline = 0x5E;      // OPTIONS_ONLINE
constexpr uint32_t kGameNew = 0x325F20;        // operator new(size, heap), cdecl
constexpr uint32_t kAppSingleton = 0x25720;    // returns the game object, +0x54 its front end heap
constexpr uint32_t kListBaseCtor = 0x55BD70;   // the settings list screen's base class, thiscall, 12 arguments
constexpr uint32_t kListScreenSize = 0x234;    // as the plain list subclass allocates it
constexpr uint32_t kCancelPrompt = 0x3364;     // "Are you sure you want to cancel your changes?"
// setting ids the game leaves free: its row factory makes 0 to 0x8A, and its per setting arrays (the row cache,
// the store's flags) end near 0xB0, where ids past them would write into other heap blocks
constexpr uint32_t kRowBase = 0x90;
constexpr uint32_t kStringBase = 0xFE00;       // string ids past FEENG.LOC's
constexpr uint32_t kOnlineOptionsString = 27110;  // the Options menu entry, "Online Options"
constexpr uint32_t kListTitleString = 13200;      // the list screen's title, "Gameplay Options"
// the game's plain choice list row (48 of its settings): vtable, -1, setting id, label string, choices, its
// {value, string} table
constexpr uint32_t kChoiceListVtable = 0x63D2E8;
constexpr uint32_t kChoiceListSize = 0x18;

struct Row {
    const wchar_t* label;
    std::vector<const wchar_t*> choices;
    int (*get)();         // the current setting as a choice index
    void (*set)(int i);   // a choice index into the setting
};

// nearest choice to v in values
int Nearest(const std::vector<uint32_t>& values, uint32_t v) {
    int best = 0;
    for (size_t i = 0; i < values.size(); i++)
        if ((v > values[i] ? v - values[i] : values[i] - v) < (v > values[best] ? v - values[best] : values[best] - v))
            best = int(i);
    return best;
}

const std::vector<uint32_t> kRenderPercent = {25, 33, 50, 67, 75, 85, 100, 125, 150, 200};
// window sizes 16:9, then the desktop's own; the same width and height are the fullscreen mode
struct Size {
    uint32_t w, h;
};
const std::vector<Size> kWindowSizes = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {0, 0}};
const std::vector<uint32_t> kSharpening = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
const std::vector<uint32_t> kAniso = {0, 2, 4, 8, 16};
// the frame limit while the window is not active, 0 unlimited
const std::vector<uint32_t> kBackgroundFps = {15, 30, 60, 0};
const std::vector<uint32_t> kFpsLimit = {30, 60, 90, 120, 144, 165, 240, 360, 500, 1000, 0};
// copies of the saves folder kept at launch and exit, 0 off; versions kept per save
const std::vector<uint32_t> kSaveBackups = {0, 3, 5, 10, 20, 50};
const std::vector<uint32_t> kSaveHistory = {2, 3, 5, 10, 20};
const std::vector<uint32_t> kRumble = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

const std::vector<const wchar_t*>& KeyNames() {
    static const std::vector<const wchar_t*> names = [] {
        std::vector<const wchar_t*> n;
        for (const game::KeyChoice& k : game::KeyChoices()) n.push_back(k.name);
        return n;
    }();
    return names;
}

// a keyboard button's row: the choice is an index into game::KeyChoices()
template <int N>
Row KeyRow(const wchar_t* label) {
    return {label, KeyNames(),
            [] {
                const auto& ks = game::KeyChoices();
                for (size_t i = 0; i < ks.size(); i++)
                    if (ks[i].vk == game::g_keys[N]) return int(i);
                return 0;
            },
            [](int i) { game::g_keys[N] = game::KeyChoices()[size_t(i)].vk; }};
}

// a folder in UserDir() opened in Explorer, made first when missing; its own thread, as the shell may take a while
void OpenFolder(std::filesystem::path dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    LOGR("pc options: opening %s", dir.string().c_str());
    std::thread([dir] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, L"explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        CoUninitialize();
    }).detach();
}

// the saves folder as main.cpp names it: saves, saves2.. for --instance
std::filesystem::path SavesDir() {
    return app::UserDir() / (app::Instance() > 1 ? L"saves" + std::to_wstring(app::Instance()) : std::wstring(L"saves"));
}

// the settings file put aside as <name>.bak-YYYYMMDD-HHMMSS, then a restart that finds none and starts from the
// defaults; the game's own relaunch path, so the new instance takes over the window without a gap
[[noreturn]] void ResetSettings() {
    std::filesystem::path file = app::UserDir() / app::kGame.settings;
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &now);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), ".bak-%Y%m%d-%H%M%S", &tm);
        std::filesystem::path bak = file;
        bak += stamp;
        std::filesystem::rename(file, bak, ec);
        if (ec) LOGE("pc options: cannot rename %s (%s)", file.string().c_str(), ec.message().c_str());
        else LOGR("pc options: settings kept as %s, restarting with the defaults", bak.string().c_str());
    }
    kernel::Relaunch(app::kGame.images.front().second);
}

// Reset Settings asks once more before it acts: Select, then the next choice arms it, then the one after resets.
// Any other step (the list wraps from Select to the last choice) disarms it.
int g_reset_step = 0;

const std::vector<Row>& Rows() {
    static const std::vector<Row> rows = {
        {L"Display Mode", {L"Fullscreen", L"Borderless", L"Windowed"},
         [] { return int(app::Get().display_mode); },
         [](int i) {
             app::Get().display_mode = app::DisplayMode(i);
             app::ApplyDisplay();
         }},
        {L"Window Size", {L"1280 x 720", L"1600 x 900", L"1920 x 1080", L"2560 x 1440", L"3840 x 2160", L"Desktop"},
         [] {
             const app::Settings& st = app::Get();
             for (size_t i = 0; i < kWindowSizes.size(); i++)
                 if (kWindowSizes[i].w == st.width && kWindowSizes[i].h == st.height) return int(i);
             return 0;
         },
         [](int i) {
             app::Get().width = kWindowSizes[size_t(i)].w;
             app::Get().height = kWindowSizes[size_t(i)].h;
             app::ApplyDisplay();
         }},
        {L"Render Resolution", {L"25%", L"33%", L"50%", L"67%", L"75%", L"85%", L"100%", L"125%", L"150%", L"200%"},
         [] { return Nearest(kRenderPercent, app::Get().render_percent); },
         [](int i) { app::Get().render_percent = kRenderPercent[size_t(i)]; }},
        {L"Anti-Aliasing", {L"Off", L"FXAA"}, [] { return app::Get().fxaa ? 1 : 0; },
         [](int i) { app::Get().fxaa = i != 0; }},
        {L"Upscaler", {L"Bilinear", L"AMD FSR 1"}, [] { return app::Get().fsr ? 1 : 0; },
         [](int i) { app::Get().fsr = i != 0; }},
        {L"Sharpening", {L"Off", L"10", L"20", L"30", L"40", L"50", L"60", L"70", L"80", L"90", L"100"},
         [] { return Nearest(kSharpening, app::Get().sharpening); },
         [](int i) { app::Get().sharpening = kSharpening[size_t(i)]; }},
        {L"Depth of Field", {L"Off", L"On"}, [] { return app::Get().dof ? 1 : 0; },
         [](int i) { app::Get().dof = i != 0; }},
        {L"Glow", {L"Off", L"On"}, [] { return app::Get().glow ? 1 : 0; },
         [](int i) { app::Get().glow = i != 0; }},
        {L"Effects Quality", {L"Original", L"High"}, [] { return app::Get().fx_high ? 1 : 0; },
         [](int i) { app::Get().fx_high = i != 0; }},
        {L"Texture Filtering", {L"Game Default", L"2x Anisotropic", L"4x Anisotropic", L"8x Anisotropic",
                                L"16x Anisotropic"},
         [] { return Nearest(kAniso, app::Get().aniso); },
         [](int i) { app::Get().aniso = kAniso[size_t(i)]; }},
        {L"Frame Limit", {L"30", L"60", L"90", L"120", L"144", L"165", L"240", L"360", L"500", L"1000", L"Unlimited"},
         [] { return app::Get().fps_limit ? Nearest(kFpsLimit, app::Get().fps_limit) : int(kFpsLimit.size()) - 1; },
         [](int i) { app::Get().fps_limit = kFpsLimit[size_t(i)]; }},
        {L"Vertical Sync", {L"Off", L"On"}, [] { return app::Get().vsync ? 1 : 0; },
         [](int i) { app::Get().vsync = i != 0; }},
        {L"Background Frame Rate", {L"15", L"30", L"60", L"Unlimited"},
         [] {
             for (size_t i = 0; i < kBackgroundFps.size(); i++)
                 if (kBackgroundFps[i] == app::Get().background_fps) return int(i);
             return 1;
         },
         [](int i) { app::Get().background_fps = kBackgroundFps[size_t(i)]; }},
        {L"Mute When Unfocused", {L"Off", L"On"}, [] { return app::Get().mute_unfocused ? 1 : 0; },
         [](int i) { app::Get().mute_unfocused = i != 0; }},
        {L"HD 720p Mode (Restart)", {L"Off", L"On"}, [] { return game::g_xbox_720p ? 1 : 0; },
         [](int i) { game::g_xbox_720p = i != 0; }},
        KeyRow<0>(L"Key: A"),
        KeyRow<1>(L"Key: B"),
        KeyRow<2>(L"Key: X"),
        KeyRow<3>(L"Key: Y"),
        KeyRow<4>(L"Key: Start"),
        KeyRow<5>(L"Key: Back"),
        KeyRow<6>(L"Key: Left Trigger"),
        KeyRow<7>(L"Key: Right Trigger"),
        KeyRow<8>(L"Key: White"),
        KeyRow<9>(L"Key: Black"),
        // choosing Reset puts the default keys back; the row shows Select again, so it can be chosen again
        {L"Reset Keyboard Keys", {L"Select", L"Reset"}, [] { return 0; }, [](int i) { if (i == 1) game::ResetKeys(); }},
        // takes the last free row id; deadzones and the Nintendo layout are in the settings file only
        {L"Controller Vibration", {L"Off", L"10%", L"20%", L"30%", L"40%", L"50%", L"60%", L"70%", L"80%", L"90%",
                                   L"100%"},
         [] { return Nearest(kRumble, app::Get().rumble); },
         [](int i) { app::Get().rumble = kRumble[size_t(i)]; }},
        {L"Save Backups", {L"Off", L"3", L"5", L"10", L"20", L"50"},
         [] { return Nearest(kSaveBackups, app::Get().save_backups); },
         [](int i) { app::Get().save_backups = kSaveBackups[size_t(i)]; }},
        {L"Versions Kept Per Save", {L"2", L"3", L"5", L"10", L"20"},
         [] { return Nearest(kSaveHistory, app::Get().save_history); },
         [](int i) { app::Get().save_history = kSaveHistory[size_t(i)]; }},
        {L"Open Saves Folder", {L"Select", L"Open"}, [] { return 0; },
         [](int i) { if (i == 1) OpenFolder(SavesDir()); }},
        {L"Open Logs Folder", {L"Select", L"Open"}, [] { return 0; },
         [](int i) { if (i == 1) OpenFolder(app::UserDir() / "logs"); }},
        {L"Open Screenshots Folder", {L"Select", L"Open"}, [] { return 0; },
         [](int i) { if (i == 1) OpenFolder(app::UserDir() / "screenshots"); }},
        {L"Reset Settings (Restart)", {L"Select", L"Are You Sure? Once More", L"Reset and Restart"},
         [] { return g_reset_step; },
         [](int i) {
             if (g_reset_step == 1 && i == 2) ResetSettings();
             g_reset_step = g_reset_step == 0 && i == 1 ? 1 : 0;
         }},
    };
    return rows;
}

// the guest copies: row ids, each row's choice table and every string, made once
struct Guest {
    uint32_t ids = 0;
    std::vector<uint32_t> tables;   // per row
    std::vector<uint32_t> labels;   // per row: string id
    std::vector<uint32_t> strings;  // per string id from kStringBase: UTF-16 text
    uint32_t pc_options = 0;        // "PC Options" in place of "Online Options"
};

uint32_t GuestText(const wchar_t* s) {
    std::u16string u(s, s + std::char_traits<wchar_t>::length(s));
    uint32_t p = mem::AllocSystem(uint32_t((u.size() + 1) * 2));
    std::memcpy(kernel::HostPtr(p), u.c_str(), (u.size() + 1) * 2);
    return p;
}

const Guest& G() {
    static const Guest g = [] {
        Guest g;
        const auto& rows = Rows();
        g.ids = mem::AllocSystem(uint32_t(rows.size() * 4));
        for (size_t r = 0; r < rows.size(); r++) {
            kernel::W32(g.ids + uint32_t(r * 4), kRowBase + uint32_t(r));
            g.labels.push_back(kStringBase + uint32_t(g.strings.size()));
            g.strings.push_back(GuestText(rows[r].label));
            uint32_t t = mem::AllocSystem(uint32_t(rows[r].choices.size() * 8));
            for (size_t c = 0; c < rows[r].choices.size(); c++) {
                kernel::W32(t + uint32_t(c * 8), uint32_t(c));
                kernel::W32(t + uint32_t(c * 8 + 4), kStringBase + uint32_t(g.strings.size()));
                g.strings.push_back(GuestText(rows[r].choices[c]));
            }
            g.tables.push_back(t);
        }
        g.pc_options = GuestText(L"PC Options");
        return g;
    }();
    return g;
}

int RowOf(uint32_t id) {
    return id >= kRowBase && id < kRowBase + Rows().size() ? int(id - kRowBase) : -1;
}

uint32_t FlowContext() {
    uint32_t flow = kernel::R32(kFlowSingleton);
    return flow ? kernel::R32(flow + 4) : ~0u;
}

}  // namespace

// string lookup, thiscall (id): our labels, the Online Options entry renamed and the list's title on our screen
X86_EXTERN_FUNC(__imp__sub_00083B00);
X86_FUNC(sub_00083B00) {
    uint32_t id = kernel::R32(ctx.esp + 4);
    if (id == kOnlineOptionsString || (id == kListTitleString && FlowContext() == kContextOnline))
        return kernel::Return(ctx, G().pc_options, 1);
    if (id >= kStringBase && id - kStringBase < G().strings.size())
        return kernel::Return(ctx, G().strings[id - kStringBase], 1);
    if (id == game::kQuitString) return kernel::Return(ctx, game::QuitText(), 1);
    __imp__sub_00083B00(ctx, base);
}

// the screen factory, thiscall (screen name, 8 more), returns the screen: PC Options is the settings list's base
// class. Every SMARTOPTIONS subclass expects its own rows (OPTIONS_ONLINE's keeps a value per row inside its 0x298
// bytes, the plain one looks its setting ids up), so ours is made as they make their base: the game's allocator with
// the factory's heap, then the base constructor with the factory's arguments, 1, the cancel prompt and our rows.
X86_EXTERN_FUNC(__imp__sub_00112BC0);
X86_FUNC(sub_00112BC0) {
    // MVP06_TRACE_FE logs each screen made, for tests that wait for a menu
    static const bool trace = std::getenv("MVP06_TRACE_FE") != nullptr;
    if (trace) LOGR("FE screen %s", kernel::ReadCString(kernel::R32(ctx.esp + 4), 32).c_str());
    if (FlowContext() != kContextOnline || kernel::ReadCString(kernel::R32(ctx.esp + 4), 16) != "SMARTOPTIONS") {
        __imp__sub_00112BC0(ctx, base);
        return;
    }
    uint32_t screen = kernel::CallGuest(ctx, kGameNew, {kListScreenSize, kernel::R32(ctx.ecx + 8)});
    if (screen) {
        uint32_t args[12];
        for (uint32_t i = 0; i < 8; i++) args[i] = kernel::R32(ctx.esp + 8 + 4 * i);
        args[8] = 1;
        args[9] = kCancelPrompt;
        args[10] = uint32_t(Rows().size());
        args[11] = G().ids;
        uint32_t keep = ctx.ecx;
        ctx.ecx = screen;
        kernel::CallGuest(ctx, kListBaseCtor, args, 12);
        ctx.ecx = keep;
        LOGR("pc options: %u rows", uint32_t(Rows().size()));
    }
    kernel::Return(ctx, screen, 9);
}

// the row factory, thiscall (setting id): our ids become the game's plain choice lists

X86_EXTERN_FUNC(__imp__sub_00543BF0);
X86_FUNC(sub_00543BF0) {
    int r = RowOf(kernel::R32(ctx.esp + 4));
    if (r < 0) return __imp__sub_00543BF0(ctx, base);
    // the screen deletes its rows when it closes: from the game's heap, as the factory takes it (0x25720 + 0x54)
    uint32_t heap = kernel::R32(kernel::CallGuest(ctx, kAppSingleton, {}) + 0x54);
    uint32_t obj = kernel::CallGuest(ctx, kGameNew, {kChoiceListSize, heap});
    if (!obj) return kernel::Return(ctx, 0, 1);
    kernel::W32(obj + 0x00, kChoiceListVtable);
    kernel::W32(obj + 0x04, ~0u);
    kernel::W32(obj + 0x08, kRowBase + uint32_t(r));
    kernel::W32(obj + 0x0C, G().labels[size_t(r)]);
    kernel::W32(obj + 0x10, uint32_t(Rows()[size_t(r)].choices.size()));
    kernel::W32(obj + 0x14, G().tables[size_t(r)]);
    kernel::Return(ctx, obj, 1);
}

// settings getter, cdecl (setting id, store)
X86_EXTERN_FUNC(__imp__sub_005231E0);
X86_FUNC(sub_005231E0) {
    int r = RowOf(kernel::R32(ctx.esp + 4));
    if (r < 0) return __imp__sub_005231E0(ctx, base);
    kernel::Return(ctx, uint32_t(Rows()[size_t(r)].get()), 0);
}

// settings setter, cdecl (setting id, store, value): applied at once and kept in the settings file
X86_EXTERN_FUNC(__imp__sub_00523690);
X86_FUNC(sub_00523690) {
    int r = RowOf(kernel::R32(ctx.esp + 4));
    if (r < 0) return __imp__sub_00523690(ctx, base);
    int v = int(kernel::R32(ctx.esp + 12));
    const Row& row = Rows()[size_t(r)];
    if (v >= 0 && size_t(v) < row.choices.size() && v != row.get()) {
        row.set(v);
        app::Save();
        LOGR("pc options: %ls = %ls", row.label, row.choices[size_t(v)]);
    }
    kernel::Return(ctx, 0, 0);
}
