// Expanded databases (up to 305 schools, 31 conferences): detection, and the hooks that raise the stock limits next
// to the generated copies in expansion_gen.cpp (player table capacities, the database block, per-team buffers, the
// 5-bit conference id, 9-bit schedule slots; tools/expansion/expansion_spec.json). Every hook here runs the stock
// code unless Active(). Constants and the reasons for them: expansion.h.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/game_hooks.h"
#include "core/log.h"
#include "game/expansion.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {

constexpr uint32_t kStockTeamSlots = 0x82;

// id list (0x4F0320): vtable, +4 data, +8 count, +0xC capacity, +0x10/+0x14 cursors, inline storage at +0x18
constexpr uint32_t kListData = 0x4, kListCapacity = 0xC;
constexpr uint32_t kStockListCapacity = 0xCB2;

std::mutex g_lists_lock;
std::unordered_map<uint32_t, uint32_t> g_list_buffers;  // list object address -> its id buffer

uint32_t Arg(X86Context& ctx, int i) { return kernel::R32(ctx.esp + 4 + 4 * uint32_t(i)); }

uint32_t CallThis(X86Context& ctx, uint32_t self, uint32_t fn, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = self;
    uint32_t r = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
    return r;
}

// ---- Detection -----------------------------------------------------------------------------------------------------

uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// RefPack (EA's LZ77), as the database members are stored; the first `limit` bytes are enough here
std::vector<uint8_t> Unpack(const uint8_t* d, size_t size, size_t limit) {
    std::vector<uint8_t> out;
    if (size < 5 || (d[0] & 0x3E) != 0x10 || d[1] != 0xFB) return std::vector<uint8_t>(d, d + size);
    size_t p = 2 + ((d[0] & 0x01) ? ((d[0] & 0x80) ? 4 : 3) : 0), n = (d[0] & 0x80) ? 4 : 3, total = 0;
    for (size_t i = 0; i < n && p < size; i++) total = total << 8 | d[p++];
    limit = std::min(limit, total);
    auto copy = [&](size_t lit) {
        for (size_t i = 0; i < lit && p < size; i++) out.push_back(d[p++]);
    };
    while (p < size && out.size() < limit) {
        uint8_t c = d[p];
        size_t lit, length, dist;
        if (c < 0x80) {
            if (p + 2 > size) break;
            uint8_t b1 = d[p + 1];
            p += 2;
            lit = c & 3, length = ((c >> 2) & 7) + 3, dist = ((c & 0x60) << 3) + b1 + 1;
        } else if (c < 0xC0) {
            if (p + 3 > size) break;
            uint8_t b1 = d[p + 1], b2 = d[p + 2];
            p += 3;
            lit = b1 >> 6, length = (c & 0x3F) + 4, dist = ((b1 & 0x3F) << 8) + b2 + 1;
        } else if (c < 0xE0) {
            if (p + 4 > size) break;
            uint8_t b1 = d[p + 1], b2 = d[p + 2], b3 = d[p + 3];
            p += 4;
            lit = c & 3, length = ((c & 0x0C) << 6) + b3 + 5, dist = ((c & 0x10) << 12) + (b1 << 8) + b2 + 1;
        } else if (c < 0xFC) {
            p++;
            copy(((c & 0x1F) << 2) + 4);
            continue;
        } else {
            p++;
            copy(c & 3);
            break;
        }
        copy(lit);
        if (dist > out.size()) break;
        for (size_t i = 0; i < length; i++) out.push_back(out[out.size() - dist]);
    }
    if (out.size() > limit) out.resize(limit);
    return out;
}

// a member of a BIGF archive, unpacked up to `limit` bytes; empty when missing
std::vector<uint8_t> Member(const std::vector<uint8_t>& big, const char* name, size_t limit) {
    if (big.size() < 16 || std::memcmp(big.data(), "BIGF", 4) != 0) return {};
    uint32_t count = BE32(&big[8]);
    size_t pos = 16;
    for (uint32_t i = 0; i < count && pos + 9 <= big.size(); i++) {
        uint32_t off = BE32(&big[pos]), size = BE32(&big[pos + 4]);
        size_t end = pos + 8;
        while (end < big.size() && big[end]) end++;
        std::string member(reinterpret_cast<const char*>(&big[pos + 8]), end - pos - 8);
        pos = end + 1;
        if (_stricmp(member.c_str(), name) == 0 && uint64_t(off) + size <= big.size())
            return Unpack(&big[off], size, limit);
    }
    return {};
}

// conf.dat records: lines starting with a 9-digit hex row key (the header line starts "0 conf_fullname")
uint32_t ConferenceRows(const std::vector<uint8_t>& conf) {
    std::string text(conf.begin(), conf.end());
    uint32_t rows = 0;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        bool key = end - at > 9 && text[at + 9] == ',';
        for (size_t i = 0; key && i < 9; i++) key = std::isxdigit(uint8_t(text[at + i])) != 0;
        rows += key;
        at = end + 1;
    }
    return rows;
}

bool Detect() {
    std::vector<uint8_t> big;
    if (!kernel::ReadGameFile("D:\\data\\database\\database.big", big)) {
        LOGW("expansion: database.big not readable, stock limits");
        return false;
    }
    std::vector<uint8_t> roster = Member(big, "roster.bin", 4);
    std::vector<uint8_t> conf = Member(big, "conf.dat", size_t(1) << 20);
    uint32_t teams = 0;
    if (roster.size() >= 4) std::memcpy(&teams, roster.data(), 4);
    uint32_t schools = teams ? teams - 1 : 0, conferences = ConferenceRows(conf);   // roster.bin counts empty slot 0
    bool on = schools > expansion::kStockMaxSchools || conferences > expansion::kStockConferences;
    if (on && (schools > expansion::kSchools || conferences > expansion::kConferenceRows - 1)) {
        LOGE("expansion: the database has %u schools in %u conferences, more than the %u / %u this build holds; "
             "stock limits", schools, conferences, expansion::kSchools, expansion::kConferenceRows - 1);
        return false;
    }
    LOGR("expansion: database with %u schools in %u conferences: %s", schools, conferences,
         on ? "expanded limits" : "stock limits");
    return on;
}

}  // namespace

namespace expansion {

bool Active() {
    static const bool on = Detect();
    return on;
}

bool LoadConferenceSchedule(uint32_t conf) {
    return conf < kScheduleConfs;   // 0-based here: conf.dat row 1 is 0
}

}  // namespace expansion

// ---- Conference tournaments and automatic bids ------------------------------------------------------------------
// The stock code keys the 11 conference tournaments to conf.dat *asset ids* (the conference logo slot: bits 0-3 of
// the conf.dat record, field 0 of 0x7CB10; bits 4-5 = division count), not to conference ids: the builder 0x4F6110
// and the dispatcher 0x5000E0 ask 0x86F90(k) for "the conference whose asset id is k" (k = 1..14, last match wins),
// and 0x4D9930(conference) answers the tournament slot of that conference's *asset id*. In the disc's data asset id
// == conference id. An expanded database has up to 31 conferences sharing 14 logo slots, so 0x86F90 picked arbitrary
// conferences (the last one with that logo) and 0x4D9930 sent auto-bid lookups to other conferences' brackets. With
// an expanded database both use the conference id: tournament k is conference k's, and the data orders conferences
// so every bracket fits. 0x86F90 has no other callers; the asset id keeps choosing logos (0x8B3E0) and NCAA regions
// (0x4D9A50).
namespace {

// Teams the stock bracket of tournament k reads from the conference standings (0 = no tournament):
// 0x4FE060 8-team pods (k = 1, 2, 4, 6, 11, 12), 0x4FE590 6 (3, 7, 8), 0x4FE8D0 4 (10), 0x4FEBF0 2 (14).
uint32_t TournamentTeams(uint32_t k) {
    switch (k) {
        case 1: case 2: case 4: case 6: case 11: case 12: return 8;
        case 3: case 7: case 8: return 6;
        case 10: return 4;
        case 14: return 2;
        default: return 0;
    }
}

// Tournament slot (holder index) of conference k, as the stock 0x4D9930 switch.
int32_t TournamentSlot(uint32_t k) {
    static const int32_t slots[15] = {-1, 0x10, 0x11, 0x12, 0x13, -1, 0x14, 0x15, 0x16, -1, 0x17, 0x18, 0x19, -1, 0x1A};
    return k < 15 ? slots[k] : -1;
}

std::mutex g_tourney_lock;
bool g_tourney_skipped[64] = {};   // conference id -> its bracket was not built (auto bid = regular-season leader)

// Teams in conference k's standings (season manager [mgr+0x14] +0x20 array by conference id; rank vector +0x10).
uint32_t StandingsTeams(X86Context& ctx, uint32_t mgr, uint32_t k) {
    uint32_t st = kernel::R32(mgr + 0x14);
    if (!st || k >= kernel::R32(st + 0x1C)) return 0;
    uint32_t obj = kernel::R32(kernel::R32(st + 0x20) + k * 4);
    if (!obj) return 0;
    if (!(kernel::R32(obj + 4) & 0xFF)) CallThis(ctx, obj, 0x4FC9F0, {});
    uint32_t vec = kernel::R32(obj + 0x10);
    return vec ? (kernel::R32(vec + 8) - kernel::R32(vec + 4)) / 4 : 0;
}

}  // namespace

// Conference with tournament key k (thiscall on the league db, k; ret 4) -> conference id k if it exists, else 0.
X86_EXTERN_FUNC(__imp__sub_00086F90);
X86_FUNC(sub_00086F90) {
    if (!expansion::Active()) return __imp__sub_00086F90(ctx, base);
    uint32_t db = ctx.ecx, k = Arg(ctx, 0), table = kernel::R32(db + 0x2C), vt = kernel::R32(table);
    uint32_t rows = CallThis(ctx, table, kernel::R32(vt + 0x34), {}), found = 0;
    for (uint32_t r = 0; r < rows && !found; r++)
        if (CallThis(ctx, table, kernel::R32(vt + 0x38), {r}) == k) found = k;
    ctx.eax = found;
    ctx.esp += 8;
}

// Tournament slot of a conference (thiscall on the season manager, conference id; ret 4), -1 = none (the
// conference's regular-season leader takes the automatic bid: 0x4FEDE0, 0x97DB0).
X86_EXTERN_FUNC(__imp__sub_004D9930);
X86_FUNC(sub_004D9930) {
    if (!expansion::Active()) return __imp__sub_004D9930(ctx, base);
    uint32_t k = Arg(ctx, 0);
    int32_t slot = TournamentSlot(k);
    {
        std::lock_guard<std::mutex> lock(g_tourney_lock);
        if (k < 64 && g_tourney_skipped[k]) slot = -1;
    }
    ctx.eax = uint32_t(slot);
    ctx.esp += 8;
}

// Conference tournament dispatch (thiscall on the season manager, event with the tournament key at +8; ret 4):
// builds tournament k from conference k's standings. A conference with fewer teams than its bracket reads (the
// stock builders take standings entries 0..n-1 unchecked) gets no tournament: its regular-season leader takes the
// automatic bid (0x4D9930 answers -1 for it).
X86_EXTERN_FUNC(__imp__sub_005000E0);
X86_FUNC(sub_005000E0) {
    if (!expansion::Active()) return __imp__sub_005000E0(ctx, base);
    uint32_t mgr = ctx.ecx, ev = Arg(ctx, 0), k = ev ? kernel::R32(ev + 8) : 0;
    uint32_t need = TournamentTeams(k), have = need ? StandingsTeams(ctx, mgr, k) : 0;
    if (need && have < need) {
        LOGR("expansion: conference %u has %u teams, its tournament needs %u: no tournament, regular-season leader "
             "gets the automatic bid", k, have, need);
        std::lock_guard<std::mutex> lock(g_tourney_lock);
        if (k < 64) g_tourney_skipped[k] = true;
        ctx.esp += 8;
        return;
    }
    __imp__sub_005000E0(ctx, base);
}

// Default conference tournament setup (season start): a new season's tournaments start unskipped.
X86_EXTERN_FUNC(__imp__sub_004F6110);
X86_FUNC(sub_004F6110) {
    {
        std::lock_guard<std::mutex> lock(g_tourney_lock);
        std::memset(g_tourney_skipped, 0, sizeof g_tourney_skipped);
    }
    __imp__sub_004F6110(ctx, base);
}

// ---- Capacities ----------------------------------------------------------------------------------------------------

// Save buffer size (cdecl-like, flags; ret 4; 0x77D10): fixed sizes per saved section, made for the stock 3,250
// players (roster 0xF7800, dynasty 0x1C3400, ...). The serializers do not check the end of the buffer, so saving a
// 305-school Dynasty ran off the end of "cDBManager::SaveBuffer" (0x77DA0) into the rest of memory (crash in the
// node-map writer 0x797B0). Every section gets kSaveScale times the room.
X86_EXTERN_FUNC(__imp__sub_00077D10);
X86_FUNC(sub_00077D10) {
    __imp__sub_00077D10(ctx, base);
    if (expansion::Active()) ctx.eax *= expansion::kSaveScale;
}

// Team registry ctor (thiscall, capacity): built once at boot (0x4E08D0) for every team in roster.bin.
X86_EXTERN_FUNC(__imp__sub_004D5130);
X86_FUNC(sub_004D5130) {
    uint32_t self = ctx.ecx;
    bool ours = expansion::Active() && Arg(ctx, 0) == kStockTeamSlots;
    // one spare entry: a lookup of a team id past the last one reads the sorted list at the count
    if (ours) kernel::W32(ctx.esp + 4, expansion::kTeamSlots + 1);
    __imp__sub_004D5130(ctx, base);
    if (!ours) return;
    // the ctor marks unused sorted-list entries 0xFF (no team); with 9-bit slots that is slot 255, so use kNoSlot
    uint32_t sorted = kernel::R32(self + 0x14), capacity = kernel::R32(self + 0x04);
    for (uint32_t i = 0; sorted && i < capacity; i++) kernel::W32(sorted + i * 4, expansion::kNoSlot);
}

// Division of a team (thiscall, bit 10 of +0xA4): with an expanded database that bit is the conference id's fifth.
X86_EXTERN_FUNC(__imp__sub_004D1910);
X86_FUNC(sub_004D1910) {
    if (!expansion::Active()) return __imp__sub_004D1910(ctx, base);
    ctx.eax = 0;
    ctx.esp += 4;
}

// Player id list ctor (thiscall): 3,250 ids stored inside the object. Lists that big get a buffer the size of the
// batter table instead, so a list of every player still fits. Most of these lists live on the stack and their
// destructor is inlined (it only resets the vtable), so a buffer is never freed: it belongs to the object's address
// and is reused by the next list built there (allocating per list from the game heap ran it out of memory after
// about two weeks of Dynasty simming). The buffers come from system memory, outside the game heap.
X86_EXTERN_FUNC(__imp__sub_004F0320);
X86_FUNC(sub_004F0320) {
    uint32_t self = ctx.ecx;
    __imp__sub_004F0320(ctx, base);
    if (!expansion::Active() || kernel::R32(self + kListCapacity) != kStockListCapacity) return;
    uint32_t bytes = expansion::kBatterRows * 4;
    uint32_t buffer;
    {
        std::lock_guard<std::mutex> lock(g_lists_lock);
        uint32_t& slot = g_list_buffers[self];
        if (!slot) slot = mem::AllocSystem(bytes);
        buffer = slot;
    }
    if (!buffer) {
        LOGE("expansion: no memory for a %u-player list, kept the stock 3,250", expansion::kBatterRows);
        return;
    }
    std::memset(kernel::HostPtr(buffer), 0, bytes);
    kernel::W32(self + kListData, buffer);
    kernel::W32(self + kListCapacity, expansion::kBatterRows);
}
