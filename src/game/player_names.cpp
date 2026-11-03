// Real player names. The stock player table has no names: attrib.dat keeps the school in first_name and the slot
// number in last_name, and every time the game builds its player database it gives every row a random name from
// plyrgen.big/names.csv (0x91C30, below). After that pass this hook puts real names on the rows listed in
// player_names.csv beside the exe, matched by the player's key (the attrib.dat row key, the same u32 roster.bin links
// teams with). Rows not in the file keep their random names; no file, no change.
//
// player_names.csv: header line, then key,school,slot,first,last (key = 9 hex digits as in attrib.dat; school and slot
// are for people only). Names are cut to the table's fields: 11 characters first, 15 last.
// Booting does not run 0x91C30 (names come from the auto-loaded roster save, which keeps the names it was made with):
// Manage Rosters -> Reset Rosters -> Use Generated Names applies the file, Save Rosters keeps the result.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "common/game_hooks.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {

constexpr uint32_t kMapOffset = 0x7C;       // database object: key -> row map (count at +8, 12-byte entries at +4)
constexpr uint32_t kTableOffset = 0x80;     // database object: the player attribute table
constexpr uint32_t kEntrySize = 12;         // map entry: key, row, (other)
constexpr uint32_t kSetIntSlot = 0x1C;      // table vtable: (field, row, value)
constexpr uint32_t kSetStringSlot = 0x20;   // table vtable: (field, row, string, 0)
constexpr uint32_t kFirstField = 0, kLastField = 1, kAudioField = 6;  // 0x4ECE00 writes the audio id to field 6
constexpr uint32_t kAudioTable = 0x6C656C;  // audioid.csv holder; 0x85640 (thiscall, last name) -> id, 0 if none
constexpr uint32_t kAudioLookup = 0x85640;
constexpr size_t kFirstMax = 11, kLastMax = 15;
constexpr uint32_t kScratchSize = 64;

struct Name {
    std::string first, last;
};

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

std::unordered_map<uint32_t, Name> LoadNames() {
    std::unordered_map<uint32_t, Name> names;
    const auto path = app::ExeDir() / "player_names.csv";
    std::ifstream in(path);
    if (!in) {
        LOGR("player names: no %s, random names kept", path.string().c_str());
        return names;
    }
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        auto cols = SplitCsv(line);
        if (cols.size() < 5 || cols[0].empty()) continue;
        char* end = nullptr;
        unsigned long key = std::strtoul(cols[0].c_str(), &end, 16);
        if (*end || cols[3].empty() || cols[4].empty()) continue;
        names[uint32_t(key)] = {cols[3].substr(0, kFirstMax), cols[4].substr(0, kLastMax)};
    }
    LOGR("player names: %zu names from %s", names.size(), path.string().c_str());
    return names;
}

const std::unordered_map<uint32_t, Name>& Names() {
    static const auto names = LoadNames();
    return names;
}

uint32_t Scratch() {
    static const uint32_t p = mem::AllocSystem(kScratchSize);
    return p;
}

uint32_t PutString(const std::string& s) {
    uint32_t p = Scratch();
    std::memset(kernel::HostPtr(p), 0, kScratchSize);
    std::memcpy(kernel::HostPtr(p), s.data(), std::min<size_t>(s.size(), kScratchSize - 1));
    return p;
}

uint32_t CallThis(X86Context& ctx, uint32_t self, uint32_t fn, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = self;
    uint32_t r = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
    return r;
}

}  // namespace

// Random names for the whole player table (thiscall, the player database; no arguments). Runs when a roster set is
// built (new game/roster, reset); it loads names.csv, gives rows 1..count-1 a random first/last name, frees the list.
X86_EXTERN_FUNC(__imp__sub_00091C30);
X86_FUNC(sub_00091C30) {
    uint32_t db = ctx.ecx;
    __imp__sub_00091C30(ctx, base);
    const auto& names = Names();
    uint32_t map = kernel::R32(db + kMapOffset), table = kernel::R32(db + kTableOffset);
    if (names.empty() || !map || !table) return;
    uint32_t entries = kernel::R32(map + 4), count = kernel::R32(map + 8);
    uint32_t vtable = kernel::R32(table);
    uint32_t set_int = kernel::R32(vtable + kSetIntSlot), set_string = kernel::R32(vtable + kSetStringSlot);
    uint32_t audio = kernel::R32(kAudioTable);
    uint32_t saved_eax = ctx.eax, renamed = 0, voiced = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key = kernel::R32(entries + i * kEntrySize), row = kernel::R32(entries + i * kEntrySize + 4);
        auto it = names.find(key);
        if (it == names.end() || row == 0 || row >= count) continue;
        CallThis(ctx, table, set_string, {kFirstField, row, PutString(it->second.first), 0});
        uint32_t last = PutString(it->second.last);
        CallThis(ctx, table, set_string, {kLastField, row, last, 0});
        int32_t id = audio ? int32_t(CallThis(ctx, audio, kAudioLookup, {last})) : 0;
        if (id > 0) {
            CallThis(ctx, table, set_int, {kAudioField, row, uint32_t(id)});
            voiced++;
        }
        renamed++;
    }
    ctx.eax = saved_eax;
    LOGR("player names: db %08X, %u rows, %u renamed, %u with announcer audio", db, count, renamed, voiced);
}
