// Custom walk-up songs. A new batter makes the game post audio event 77
// (batter_dit) from 0x4A060 with his playerattrib_ditty, and the batdit bank plays one of 66 stock ditties. Here:
//   - 0x4A060 (thiscall, the at-bat object): before it runs, the batter's player key is read the way the attribute
//     getter 0x201A40 finds his team slot, and the team's slot record holds the key (0x8A4DA looks it up in the
//     player database's key map, the same u32 player_names.csv lists).
//   - 0x1AA40 (batter_dit factory, cdecl, first argument the parameter block, value = its first dword): when that
//     batter has a song, it starts on a host voice mixed into the DirectSound output and the factory returns 0 (its
//     own allocation-failure result, which the dispatcher stores as an empty slot), so the stock ditty stays silent.
//     No song: the stock factory runs unchanged.
//   - 0x1AAD0 (batter_ditty_kill): the host voice fades out over 0.5 s, then the stock kill runs.
//
// Songs, in walkup\ beside the exe (first match wins, extensions .ogg .mp3 .m4a .wav .flac .wma .aac):
//   walkup\<key>.<ext>                    key = 9 hex digits as in player_names.csv (8 also accepted)
//   walkup\<School>\<First Last>.<ext>    names as in player_names.csv beside the exe
//   walkup\_all.<ext>                     everyone else (one song for every batter, handy for testing)
// walkup\index.csv (optional): file,start_seconds,duration_seconds; file relative to walkup\, extension optional.
// Default cut: from 0 s, 12 s, the last 3.5 s faded, then the ballpark PA treatment (walkup_reverb.cpp); the pitch
// fades it out over 1.5 s. Setting walkup_volume (percent, default 100; 0 = off).
// The log shows the first 8 batter_dit events and each song's first play.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "app/app.h"
#include "audio/audio.h"
#include "common/game_hooks.h"
#include "core/log.h"
#include "game/walkup.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace game {
uint32_t g_walkup_volume = 100;
}

namespace walkup {
namespace {

namespace fs = std::filesystem;

// at-bat object and player layout (0x4A060, getter 0x201A40, team loop 0x8A450)
constexpr uint32_t kAtBatBatter = 0x124;    // at-bat object: the batter
constexpr uint32_t kBatterPlayer = 0x5C4;   // batter: its player object
constexpr uint32_t kPlayerGame = 0x3FC;     // player: -> +0x6F0 -> +0x14 team array (count, then team pointers)
constexpr uint32_t kGameTeams = 0x6F0, kTeamArray = 0x14;
constexpr uint32_t kPlayerSide = 0x5F4;     // player: index into the team array
constexpr uint32_t kPlayerSlot = 0x5F8;     // player: u8 roster slot in his team
constexpr uint32_t kTeamCount = 0x588;      // team: u8 roster count
constexpr uint32_t kTeamSlots = 0x58C, kSlotSize = 0x3C;  // team: slot records, the player key first
constexpr uint32_t kMaxSide = 64;

constexpr double kDefaultLength = 12.0, kEndFade = 3.5, kStartFade = 0.03, kKillFade = 1.5, kTailFade = 0.05;
constexpr float kLevel = 0.23f;  // songs are mastered far louder than the game's mix (0.7 was ~3x too loud in game)
constexpr uint32_t kMaxVolume = 200;
constexpr int kLoggedEvents = 8;
constexpr size_t kCacheClips = 8;
constexpr int64_t kKeyFreshMs = 2000;  // a captured key still counts for a factory call this soon after it
const wchar_t* const kExtensions[] = {L".ogg", L".mp3", L".m4a", L".wav", L".flac", L".wma", L".aac"};
const char* const kEveryone = "_all";  // walkup\_all.<ext>: every batter without a song of his own

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string Lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out(1);
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) out.emplace_back();
        else if (c != '\r') out.back() += c;
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// --- who is batting -----------------------------------------------------------------------------------------------

bool Mapped(uint32_t va) { return va >= mem::kUserLow && mem::IsCommitted(va) && mem::IsCommitted(va + 3); }

uint32_t BatterKey(uint32_t at_bat) {
    if (!Mapped(at_bat + kAtBatBatter)) return 0;
    uint32_t batter = kernel::R32(at_bat + kAtBatBatter);
    if (!Mapped(batter + kBatterPlayer)) return 0;
    uint32_t player = kernel::R32(batter + kBatterPlayer);
    if (!Mapped(player + kPlayerGame) || !Mapped(player + kPlayerSlot)) return 0;
    uint32_t game = kernel::R32(player + kPlayerGame);
    if (!Mapped(game + kGameTeams)) return 0;
    uint32_t teams = kernel::R32(game + kGameTeams);
    if (!Mapped(teams + kTeamArray)) return 0;
    uint32_t array = kernel::R32(teams + kTeamArray);
    uint32_t side = kernel::R32(player + kPlayerSide);
    if (side >= kMaxSide || !Mapped(array + 4 + side * 4)) return 0;
    uint32_t team = kernel::R32(array + 4 + side * 4);
    if (!Mapped(team + kTeamCount)) return 0;
    uint32_t slot = kernel::G<uint8_t>(player + kPlayerSlot), count = kernel::G<uint8_t>(team + kTeamCount);
    uint32_t record = team + kTeamSlots + slot * kSlotSize;
    if (slot >= count || !Mapped(record)) return 0;
    return kernel::R32(record);
}

// --- which song -----------------------------------------------------------------------------------------------------

struct Player {
    std::string school, first, last;
};

const std::unordered_map<uint32_t, Player>& Players() {
    static const auto players = [] {
        std::unordered_map<uint32_t, Player> out;
        std::ifstream in(app::ExeDir() / "player_names.csv");
        std::string line;
        std::getline(in, line);  // header: key,school,slot,first,last,...
        while (std::getline(in, line)) {
            auto cols = SplitCsv(line);
            if (cols.size() < 5 || cols[0].empty()) continue;
            char* end = nullptr;
            unsigned long key = std::strtoul(cols[0].c_str(), &end, 16);
            if (*end) continue;
            out[uint32_t(key)] = {cols[1], cols[3], cols[4]};
        }
        LOGR("walkup: %zu player names for walkup\\<School>\\<First Last> songs", out.size());
        return out;
    }();
    return players;
}

std::string Describe(uint32_t key) {
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%09x", key);
    auto it = Players().find(key);
    if (it == Players().end()) return hex;
    return std::string(hex) + " (" + it->second.school + ": " + it->second.first + " " + it->second.last + ")";
}

struct Cut {
    double start = 0, length = kDefaultLength;
};

fs::path Dir() { return app::ExeDir() / "walkup"; }

// index.csv rows by lower-case relative path, with and without the extension; reread when the file changes
const std::unordered_map<std::string, Cut>& Index() {
    static std::unordered_map<std::string, Cut> index;
    static fs::file_time_type stamp{};
    std::error_code ec;
    fs::path file = Dir() / "index.csv";
    auto now = fs::last_write_time(file, ec);
    if (ec || now == stamp) return index;
    stamp = now;
    index.clear();
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        auto cols = SplitCsv(line);
        std::string name = Lower(Trim(cols[0]));
        if (name.empty() || name == "file" || name[0] == '#') continue;
        std::replace(name.begin(), name.end(), '/', '\\');
        Cut cut;
        if (cols.size() > 1 && !Trim(cols[1]).empty()) cut.start = std::max(0.0, std::atof(cols[1].c_str()));
        if (cols.size() > 2 && !Trim(cols[2]).empty()) cut.length = std::atof(cols[2].c_str());
        index[name] = cut;
        index[Lower(fs::path(name).replace_extension().string())] = cut;
    }
    LOGR("walkup: %zu cuts from %s", index.size() / 2, file.string().c_str());
    return index;
}

struct Song {
    fs::path file;
    std::string rel;  // relative to walkup\, for the log and index.csv
    Cut cut;
};

std::mutex g_bad_lock;
std::set<std::string> g_bad;  // files that did not decode: their batters get the stock ditty

bool Bad(const std::string& rel) {
    std::lock_guard<std::mutex> lock(g_bad_lock);
    return g_bad.count(rel) != 0;
}

std::optional<Song> Find(uint32_t key) {
    std::vector<fs::path> bases;
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%09x", key);
    bases.emplace_back(hex);
    std::snprintf(hex, sizeof(hex), "%08x", key);
    bases.emplace_back(hex);
    auto it = Players().find(key);
    if (it != Players().end()) bases.push_back(fs::path(it->second.school) / (it->second.first + " " + it->second.last));
    bases.emplace_back(kEveryone);
    fs::path dir = Dir();
    for (const auto& b : bases) {
        for (const wchar_t* ext : kExtensions) {
            fs::path rel = b;
            rel += ext;
            std::error_code ec;
            if (!fs::is_regular_file(dir / rel, ec)) continue;
            Song song{dir / rel, rel.string(), {}};
            if (Bad(song.rel)) continue;
            const auto& index = Index();
            auto cut = index.find(Lower(song.rel));
            if (cut == index.end()) cut = index.find(Lower(b.string()));
            if (cut != index.end()) song.cut = cut->second;
            return song;
        }
    }
    return std::nullopt;
}

// --- the voice ------------------------------------------------------------------------------------------------------

struct Voice {
    std::shared_ptr<const Clip> clip;
    size_t pos = 0;
    bool killing = false;
    float kill_gain = 1;
};

std::mutex g_voice_lock;
Voice g_voice;

void Mix(float* out, uint32_t frames) {
    std::lock_guard<std::mutex> lock(g_voice_lock);
    Voice& v = g_voice;
    if (!v.clip) return;
    const auto& pcm = v.clip->pcm;
    const size_t len = pcm.size() / 2, fade_from = std::min(v.clip->fade_from, len);
    const float volume = kLevel * float(std::min(game::g_walkup_volume, kMaxVolume)) / 100.0f;
    const float start_frames = float(kStartFade * audio::kRate), kill_step = float(1.0 / (kKillFade * audio::kRate));
    const float fade_frames = float(std::max<size_t>(len - fade_from, 1));
    for (uint32_t i = 0; i < frames && v.pos < len; i++, v.pos++) {
        float env = std::min(1.0f, float(v.pos) / start_frames);
        if (v.pos >= fade_from) env *= 1.0f - float(v.pos - fade_from) / fade_frames;
        if (v.killing) {
            v.kill_gain -= kill_step;
            if (v.kill_gain <= 0) {
                v.pos = len;
                break;
            }
            env *= v.kill_gain;
        }
        out[i * 2] += pcm[v.pos * 2] * env * volume;
        out[i * 2 + 1] += pcm[v.pos * 2 + 1] * env * volume;
    }
    if (v.pos >= len) v = Voice{};
}

bool Playing() {
    std::lock_guard<std::mutex> lock(g_voice_lock);
    return g_voice.clip && !g_voice.killing;
}

void Kill() {
    std::lock_guard<std::mutex> lock(g_voice_lock);
    g_voice.killing = true;
}

// --- the decode worker ----------------------------------------------------------------------------------------------

struct Request {
    uint64_t generation = 0;
    Song song;
    uint32_t key = 0;
};

std::mutex g_work_lock;
std::condition_variable g_work_cv;
std::optional<Request> g_request;
std::atomic<uint64_t> g_generation{0};

std::shared_ptr<const Clip> Load(const Song& song, std::string& error) {
    static std::list<std::pair<std::string, std::shared_ptr<const Clip>>> cache;  // worker thread only
    char tag[64];
    std::snprintf(tag, sizeof(tag), "|%.3f|%.3f", song.cut.start, song.cut.length);
    std::string id = song.file.string() + tag;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->first != id) continue;
        cache.splice(cache.begin(), cache, it);
        return it->second;
    }
    Clip clip = Decode(song.file, song.cut.start, song.cut.length, error);
    if (clip.pcm.empty()) return nullptr;
    size_t len = clip.pcm.size() / 2;
    // cut at the length: a long smooth closing fade, baked in before the reverb so the hall rings out after it;
    // the file ran out first: a short tail fade only
    bool cut_short = song.cut.length > 0 && len + audio::kRate / 10 >= size_t(song.cut.length * audio::kRate);
    size_t fade = std::min(len, size_t((cut_short ? kEndFade : kTailFade) * audio::kRate));
    for (size_t f = len - fade; f < len; f++) {
        float t = float(f - (len - fade)) / float(std::max<size_t>(fade, 1));
        float env = 0.5f * (1.0f + std::cos(3.14159265f * t));  // cosine: eases out like the stock ditties
        clip.pcm[f * 2] *= env;
        clip.pcm[f * 2 + 1] *= env;
    }
    Stadium(clip);
    len = clip.pcm.size() / 2;
    size_t tail = size_t(kTailFade * audio::kRate);
    clip.fade_from = len > tail ? len - tail : 0;
    auto shared = std::make_shared<const Clip>(std::move(clip));
    cache.emplace_front(id, shared);
    if (cache.size() > kCacheClips) cache.pop_back();
    return shared;
}

void Work() {
    std::set<std::string> played;
    for (;;) {
        Request r;
        {
            std::unique_lock<std::mutex> lock(g_work_lock);
            g_work_cv.wait(lock, [] { return g_request.has_value(); });
            r = std::move(*g_request);
            g_request.reset();
        }
        if (r.generation != g_generation.load()) continue;  // killed or replaced before it was read
        std::string error;
        auto clip = Load(r.song, error);
        if (!clip) {
            LOGR("walkup: %s did not decode: %s; that batter gets the stock ditty from now on", r.song.rel.c_str(),
                 error.c_str());
            std::lock_guard<std::mutex> lock(g_bad_lock);
            g_bad.insert(r.song.rel);
            continue;
        }
        if (played.insert(r.song.rel).second)
            LOGR("walkup: playing %s for %s (from %.1f s, %.1f s of %.1f s decoded)", r.song.rel.c_str(),
                 Describe(r.key).c_str(), r.song.cut.start, r.song.cut.length,
                 double(clip->pcm.size() / 2) / audio::kRate);
        std::lock_guard<std::mutex> lock(g_voice_lock);
        if (r.generation != g_generation.load()) continue;  // the pitch came first
        g_voice = Voice{clip};
    }
}

void Queue(const Song& song, uint32_t key) {
    static std::once_flag started;
    std::call_once(started, [] {
        audio::g_extra_mix.store(Mix, std::memory_order_release);
        std::thread(Work).detach();
    });
    {
        std::lock_guard<std::mutex> lock(g_work_lock);
        g_request = Request{++g_generation, song, key};
    }
    g_work_cv.notify_one();
}

// --- the hooks' state -----------------------------------------------------------------------------------------------

std::atomic<bool> g_armed{false};     // 0x4A060 is running: the factory call is its batter's
std::atomic<uint32_t> g_key{0};       // that batter's key
std::atomic<int64_t> g_key_ms{0};     // when it was read
int g_events = 0;

// the factory's choice: true plays our song and keeps the stock ditty silent
bool OnBatterDitty(int32_t value) {
    bool armed = g_armed.exchange(false);
    uint32_t key = g_key.exchange(0);
    bool fresh = key && (armed || NowMs() - g_key_ms.load() < kKeyFreshMs);
    bool logged = g_events < kLoggedEvents;
    g_events++;
    if (!fresh) {
        // the fixed-7 post from 0x4A300 (or an unknown batter): stock, unless our song is still playing
        bool ours = game::g_walkup_volume && Playing();
        if (logged)
            LOGR("walkup: batter_dit value %d without a batter key (0x4A300 path?) -> %s", value,
                 ours ? "kept silent, a custom song is playing" : "stock");
        return ours;
    }
    if (!game::g_walkup_volume) {
        if (logged) LOGR("walkup: batter_dit value %d, batter %s -> stock (walkup_volume = 0)", value, Describe(key).c_str());
        return false;
    }
    auto song = Find(key);
    if (logged)
        LOGR("walkup: batter_dit value %d, batter %s -> %s", value, Describe(key).c_str(),
             song ? song->rel.c_str() : "stock (no song)");
    if (!song) return false;
    Queue(*song, key);
    return true;
}

}  // namespace
}  // namespace walkup

// New batter (thiscall, the at-bat object; no arguments): reads the batter's ditty and posts batter_dit.
X86_EXTERN_FUNC(__imp__sub_0004A060);
X86_FUNC(sub_0004A060) {
    uint32_t key = walkup::BatterKey(ctx.ecx);
    walkup::g_key.store(key);
    walkup::g_key_ms.store(walkup::NowMs());
    walkup::g_armed.store(key != 0);
    __imp__sub_0004A060(ctx, base);
    walkup::g_armed.store(false);
}

// batter_dit event factory (cdecl, the event's parameter block): returns the event object, 0 for none.
X86_EXTERN_FUNC(__imp__sub_0001AA40);
X86_FUNC(sub_0001AA40) {
    uint32_t params = kernel::R32(ctx.esp + 4);
    int32_t value = params ? int32_t(kernel::R32(params)) : -1;
    if (walkup::OnBatterDitty(value)) {
        kernel::Return(ctx, 0, 0);
        return;
    }
    __imp__sub_0001AA40(ctx, base);
}

// batter_ditty_kill event factory (cdecl): the at-bat starts, the walk-up stops.
X86_EXTERN_FUNC(__imp__sub_0001AAD0);
X86_FUNC(sub_0001AAD0) {
    walkup::g_generation++;  // a song still decoding never starts
    walkup::Kill();
    __imp__sub_0001AAD0(ctx, base);
}
