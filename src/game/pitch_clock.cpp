// NCAA pitch clock (2027-28 rules 9-4 / Appendix F, Division I timing):
// 20 seconds per pitch, 30 seconds before the first pitch to a new batter, the
// batter ready with at least eight seconds remaining. The clock runs between
// the native START_OF_PLAY and PITCH_INITIATED events on the game's own tick,
// so native pause/replay/dead-ball states (which stop those ticks or events)
// suspend it. A pitcher violation is enforced through the native
// delay-of-game ball routine 0x17CA90, the path the game itself uses for its
// online delay penalty: it runs the umpire's ball call, the fourth-ball walk
// with forced runners, statistics and the next-pitch/at-bat transition.
#include "app/app.h"
#include "core/dev_tools.h"
#include "core/log.h"
#include "game/expansion.h"
#include "common/startup_ui.h"
#include "common/pause_menu_ui.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include "common/ui_style.h"
#include "game/pitch_clock.h"
#include "imgui.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <initializer_list>
#include <cstdio>
#include <string>
#include <cstdlib>

namespace {
constexpr uint32_t kGameHolder = 0x006CA090;   // [[holder]+8] = live game object
constexpr uint32_t kGameTicks = 0x4C;           // game-thread frame counter
constexpr uint32_t kUmpireCrew = 0x6F8;
constexpr uint32_t kNativeAwardBall = 0x17CA90; // thiscall(crew): online delay-of-game ball
constexpr uint32_t kBroadcast = 0x326B50;       // thiscall(event, p1, p2, p3)
constexpr uint32_t kUmpireStrike = 0x174CA0;    // thiscall(umpire, batter): called strike, strikeout
constexpr uint32_t kResumePlay = 0x1CF970;      // thiscall(crew+0x1B8)
constexpr uint32_t kUnconventionalStart = 57;
enum Event : uint32_t { kStartOfPlay = 58, kEndOfPlay = 59, kEndHalf = 61, kStartHalf = 54, kWalk = 67, kStrikeout = 74,
                        kPitchInitiated = 100, kNewBatter = 101, kBatterReady = 113, kSubstitution = 156, kMoundVisit = 161,
                        kPaused = 179, kResumed = 176 };
constexpr uint32_t kFramesPerSecond = 60;

struct Clock {
    bool running = false, newBatter = true, batterReady = false, paused = false;
    uint32_t startTick = 0, limit = 20, violations = 0;
} g;
std::atomic<int> g_display{-1};  // whole seconds remaining for the HUD, -1 hidden
std::atomic<int> g_limit{20};
std::atomic<bool> g_gameplayHud{false};
std::atomic<long long> g_noticeUntil{0};
long long NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool Trace() { return dev::Enabled && dev::Env("MVP06_TRACE_PITCH"); }
bool RulesActive() { return expansion::Active(); }
uint32_t Game() {
    uint32_t holder = kernel::R32(kGameHolder);
    if (!holder || !mem::IsCommitted(holder + 8)) return 0;
    uint32_t game = kernel::R32(holder + 8);
    return game && mem::IsCommitted(game + 0x700) ? game : 0;
}
uint32_t Ticks(uint32_t game) { return kernel::R32(game + kGameTicks); }

void Start(uint32_t game) {
    // BATTER_READY is sent once per plate appearance, so readiness persists
    // until a new batter comes up.
    if (g.newBatter) g.batterReady = false;
    g.running = true; g.startTick = Ticks(game);
    g.limit = g.newBatter ? 30 : 20; g.newBatter = false; g_limit = int(g.limit);
    if (Trace()) LOGR("pitch clock: start %us at tick %u", g.limit, g.startTick);
}
void Stop(const char* why) {
    if (g.running && Trace()) LOGR("pitch clock: stop (%s)", why);
    g.running = false; g_display = -1;
}

std::atomic<bool> g_strikeNotice{false};
uint32_t Crew(uint32_t game) {
    uint32_t crew = kernel::R32(game + kUmpireCrew);
    return crew && mem::IsCommitted(crew + 0x1C0) && kernel::R32(crew + 0x1BC) && kernel::R32(crew + 0x1B8) ? crew : 0;
}
uint32_t Call(X86Context& base, uint32_t self, uint32_t fn, std::initializer_list<uint32_t> args) {
    X86Context ctx = base; ctx.esp = base.esp - 0x800; ctx.ecx = self;
    return kernel::CallGuest(ctx, fn, args);
}

// Pitcher violation: the native delay-of-game ball, unchanged.
void EnforcePitcher(X86Context& base, uint32_t crew) {
    ++g.violations;
    LOGR("pitch clock: pitcher violation %u, native automatic ball", g.violations);
    g.running = false; g_display = -1; g_strikeNotice = false; g_noticeUntil = NowMs() + 3500;
    Call(base, crew, kNativeAwardBall, {});
}

// Batter violation: the same native sequence as 0x17CA90, with the umpire's
// called-strike handler in place of its ball handler. The strike handler owns
// the count, the strikeout, outs, inning end and statistics. A penalty strike
// is a dead ball, so the native dropped-third-strike branch is not entered.
void EnforceBatter(X86Context& base, uint32_t crew) {
    ++g.violations;
    LOGR("pitch clock: batter violation %u, native automatic strike", g.violations);
    g.running = false; g_display = -1; g_strikeNotice = true; g_noticeUntil = NowMs() + 3500;
    Call(base, kernel::R32(crew + 4) + 0x6FC, kBroadcast, {kUnconventionalStart, 0, 0, 0});
    *static_cast<uint8_t*>(kernel::HostPtr(crew + 0x18)) = 1;
    *static_cast<uint8_t*>(kernel::HostPtr(crew + 0x19)) = 0;
    uint32_t umpire = kernel::R32(crew + 0x1BC);
    uint32_t batter = kernel::R32(kernel::R32(kernel::R32(umpire + 0x14) + 0x738) + 0x18);
    Call(base, umpire, kUmpireStrike, {batter});
    uint32_t state = kernel::R32(umpire + 8);
    kernel::W32(state + 0x18, 3);
    kernel::W32(state + 0x0C, 1);
    kernel::W32(state + 0x1C, 0xFFFFFFFFu);
    Call(base, kernel::R32(crew + 0x1B8), kResumePlay, {});
}
}

namespace game {
void NotePitchClockScene(const char* name) {
    const std::string scene=name;
    if(scene=="HUDRIGHT")g_gameplayHud=true;
    else if(scene=="MAINMENU" || scene=="START" || scene=="TEAMSELECT" || scene=="LOADING1")g_gameplayHud=false;
}
int PitchClockSeconds() { return g_display.load(); }
bool PitchClockNotice() { return NowMs() < g_noticeUntil.load(); }
void DrawPitchClock() {
    if(!g_gameplayHud || !Game() || ReplayUiActive() || PauseMenuThemeActive() || StartupSubmenuActive())return;
    const int seconds = g_display.load();
    const bool notice = NowMs() < g_noticeUntil.load();
    if (Trace()) { static bool once = false, drawn = false; if (!once) { once = true; LOGR("pitch clock HUD: UI callback active"); }
        if (!drawn && seconds >= 0) { drawn = true; LOGR("pitch clock HUD: drawing %d s at %.0fx%.0f", seconds, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y); } }
    if (seconds < 0 && !notice) return;
    auto* d = ImGui::GetBackgroundDrawList();
    auto screen = ImGui::GetIO().DisplaySize;
    float s = std::min(screen.x / 2560.f, screen.y / 1440.f);
    // Reserve the top scorebug band. Both clock states share this anchor,
    // so changing batters cannot move the timer over the team names.
    ImVec2 o((screen.x - 2560 * s) * .5f + 48 * s,
             (screen.y - 1440 * s) * .5f + 240 * s);
    auto p = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };
    const bool warn = seconds >= 0 && seconds <= 8;
    ImU32 accent = warn ? IM_COL32(214, 48, 49, 255) : IM_COL32(16, 75, 171, 255);
    d->AddRectFilled(p(0, 0), p(300, 120), IM_COL32(10, 14, 20, 235));
    d->AddRectFilled(p(0, 0), p(300, 6), accent);
    UiText(d, UiFont::Heading, 26 * s, p(18, 14), IM_COL32(220, 228, 239, 255), g_limit.load() == 30 ? "BETWEEN BATTERS" : "PITCH CLOCK", 264 * s);
    char value[16];
    if (notice) std::snprintf(value, sizeof value, g_strikeNotice ? "STRIKE" : "BALL");
    else std::snprintf(value, sizeof value, "0:%02d", seconds);
    UiText(d, UiFont::TeamDisplay, 60 * s, p(18, 46), notice ? IM_COL32(255, 210, 80, 255) : IM_COL32(245, 247, 250, 255), value, 264 * s);
    if (notice) UiText(d, UiFont::Prompt, 22 * s, p(140, 72), IM_COL32(220, 228, 239, 255), "VIOLATION", 150 * s);
}
}

// Native event dispatcher (thiscall: event, p1, p2): observes rule-clock states.
// Defensive conferences (NCAA 2027-28 rule 5-9 / Appendix F): six charged conferences through nine innings plus
// one per extra inning. The only native conference is the mound visit (event MOUND_VISIT 161), chosen from the
// pause menu's PITCHING/DEFENSE screen (IGROSTERPOPT row 0). A 161 that follows that screen within a few seconds is
// the user's charged visit; once the allowance is used, the pause menu disables the native row. Counts reset with
// each new game object. The inning comes from the native game-state record the ten-run check 0x79C30 reads
// (+4 = inning, zero-based). Offensive conferences have no native mechanic in this game.
namespace {
std::atomic<uint32_t> g_confGame{0}, g_confState{0};
std::atomic<int> g_userConferences{0};
std::atomic<unsigned long long> g_defenseMenuAt{0};
void NoteMoundVisit(uint32_t game) {
    if (g_confGame.exchange(game) != game) g_userConferences = 0;
    const auto at = g_defenseMenuAt.load();
    if (at && NowMs() - at < 10000) {
        g_defenseMenuAt = 0;
        LOGR("defensive conference %d charged (allowance %d)", ++g_userConferences, game::DefensiveConferenceAllowance());
    }
}
}
namespace game {
int DefensiveConferenceAllowance() {
    int inning = 0;
    if (dev::Enabled && dev::Env("MVP06_TEST_CONFERENCE_ALLOWANCE")) return std::atoi(dev::Env("MVP06_TEST_CONFERENCE_ALLOWANCE"));
    if (uint32_t state = g_confState; state && mem::IsCommitted(state + 8)) {
        const uint32_t value = kernel::R32(state + 4);
        if (value < 40) inning = int(value) + 1;  // a stale record is ignored
    }
    return 6 + std::max(0, inning - 9);
}
bool DefensiveConferenceLimitReached() {
    if (!RulesActive()) return false;
    const int used = g_confGame.load() == Game() ? g_userConferences.load() : 0;  // a new game has used none
    return used >= DefensiveConferenceAllowance();
}
void NoteDefensiveMenu() { g_defenseMenuAt = NowMs(); }
static std::atomic<long long> g_moundSelectedAt{0};
void NoteMoundVisitSelected(bool selected) { g_moundSelectedAt = selected ? NowMs() : 0; }
bool MoundVisitAcceptBlocked() {
    const auto at = g_moundSelectedAt.load();
    return at && NowMs() - at < 250 && DefensiveConferenceLimitReached();
}
}
X86_EXTERN_FUNC(__imp__sub_00079C30);
// The native ten-run check (thiscall; game-state record: +0 half, +4 zero-based inning, +0x10/+0x14 scores; options
// at [this+0x10], ten-run setting at +0x1C0). MVP06_TEST_TEN_RUN (dev) turns the setting on, as the game settings menu
// would, and logs each check and its result.
std::atomic<bool> g_tenRunEnded{false};
X86_FUNC(sub_00079C30) {
    const uint32_t state = kernel::R32(ctx.esp + 4), self = ctx.ecx;
    if (Game()) g_confState = state;
    const bool test = dev::Enabled && dev::Env("MVP06_TEST_TEN_RUN");
    if (test && kernel::R32(self + 0x10)) kernel::W32(kernel::R32(self + 0x10) + 0x1C0, 1);
    __imp__sub_00079C30(ctx, base);
    if (!test) return;
    static std::string last;
    char text[160]; std::snprintf(text, sizeof text, "ten-run check: inning %u half %u score %u-%u outs %u -> %u", kernel::R32(state + 4) + 1,
        *static_cast<uint8_t *>(kernel::HostPtr(state)), kernel::R32(state + 0x10), kernel::R32(state + 0x14), kernel::R32(state + 0xC), ctx.eax & 0xFF);
    if (last != text) { last = text; LOGR("%s", text); }
    if (ctx.eax & 0xFF) g_tenRunEnded = true;
}
namespace game { bool TenRunEnded() { return g_tenRunEnded; } }

X86_EXTERN_FUNC(__imp__sub_0018A850);
X86_FUNC(sub_0018A850) {
    uint32_t ev = kernel::R32(ctx.esp + 4);
    if (RulesActive()) {
        if (uint32_t game = Game()) switch (ev) {
            case kStartOfPlay: Start(game); break;
            case kPitchInitiated: Stop("pitch motion"); break;
            case kBatterReady: g.batterReady = true; break;
            case kNewBatter: case kWalk: case kStrikeout: case kStartHalf: case kEndHalf: g.newBatter = true; Stop("new batter"); break;
            case kEndOfPlay: case kSubstitution: Stop("dead ball"); break;
            case kMoundVisit: Stop("dead ball"); NoteMoundVisit(game); break;
            default: break;
        }
        if (Trace() && (ev == 58 || ev == 59 || ev == 67 || ev == 74 || ev == 82 || ev == 100 || ev == 101 || ev == 113 || ev == 176 || ev == 179))
            LOGR("pitch trace: ev %u game %08X tick %u", ev, Game(), Game() ? Ticks(Game()) : 0);
    }
    __imp__sub_0018A850(ctx, base);
}

// Pitching-interface update; ticks the clock on the game thread.
X86_EXTERN_FUNC(__imp__sub_001FB350);
X86_FUNC(sub_001FB350) {
    __imp__sub_001FB350(ctx, base);
    if (!RulesActive() || !g.running) return;
    uint32_t game = Game();
    if (!game) { Stop("no game"); return; }
    uint32_t elapsed = (Ticks(game) - g.startTick) / kFramesPerSecond;
    int remaining = int(g.limit) - int(elapsed);
    g_display = remaining < 0 ? 0 : remaining;
    static int last = -2;
    if (Trace() && remaining != last) { last = remaining; LOGR("pitch clock: %d s", remaining); }
    if (remaining > 8 && remaining > 0) return;
    uint32_t crew = Crew(game);
    if (!crew) { Stop("no umpire crew"); return; }
    const bool batterReady = g.batterReady && !(dev::Enabled && dev::Env("MVP06_TEST_BATTER_VIOLATION"));
    if (!batterReady) EnforceBatter(ctx, crew);
    else if (remaining <= 0) EnforcePitcher(ctx, crew);
}
