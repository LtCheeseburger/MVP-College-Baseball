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

