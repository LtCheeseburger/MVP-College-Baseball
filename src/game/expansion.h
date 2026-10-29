#pragma once
// Expanded databases: up to 305 schools in up to 31 conferences (the 2027 D1 universe), where the stock code holds
// 128 schools in 14 conferences. Everything here applies only while Active(): with the stock database (or any
// database within the stock limits) the stock code runs. The copies of game functions in expansion_gen.cpp
// (generated at build time by tools/expansion/clone_func.py from tools/expansion/expansion_spec.json) use these
// constants in place of the stock ones; hooks in expansion.cpp, poll.cpp and ranks.cpp do the rest.
#include <cstdint>

namespace expansion {

// The installed database.big (with mods) holds more than the stock code can: roster.bin lists more than
// kStockMaxSchools schools or conf.dat more than kStockConferences conferences. Read once, on first use (the game
// heap setup at boot), and fixed for the session.
constexpr uint32_t kStockMaxSchools = 130;   // registry and team buffers: 130 (the disc has 128)
constexpr uint32_t kStockConferences = 14;
bool Active();

// 0x990D0 builds the player database inside one block: database.big is read into its top and each table is
// decompressed from its bottom. Stock 0x12C000 (1.2 MB) only fits the 128-school tables.
constexpr uint32_t kDbBlock = 0x800000;
constexpr uint32_t kDbArchiveTop = kDbBlock - 0x400;  // stock 0x12BC00 = block - 0x400

// More game heap (hooks.cpp, 0x317590): the database block above is allocated from it while the tables are built,
// and the player tables and per-team buffers below grow it for good (255 schools ran the stock heap out at boot). The
// out-of-memory assert ends the process through HalInitiateShutdown.
constexpr uint32_t kExtraHeap = 0x3000000;  // 48 MB: 96 MB heap, ~34 MB of the 128 MB contiguous range left

// Schools the expanded build is sized for.
constexpr uint32_t kSchools = 305;
// Player table capacities (stock 3,250 batters = 0xCB2, 1,800 pitchers = 0x708). The season rollover fills the
// batter table up to its capacity with new players, so the margin over 25 x schools is the recruit pool. Keep the
// stock margin of 49 rows: creating a Dynasty generates new players until the table is full, and 3,349 at once
// crashed in the player generator.
constexpr uint32_t kBatterRows = kSchools * 25 + 1 + 49;   // 7,675 (stock 128 schools: 3,201 rows + 49 = 3,250)
constexpr uint32_t kPitcherRows = kSchools * 12 > 0x708 ? kSchools * 12 : 0x708;  // stock 1,800; ~10.4 per school

// Teams the database holds: per-team buffers of the player/team database (0x90080, 0x900F0, 0x98A29 and their
// copies and clears) are sized for 130 teams (0x208 = 130 dwords, 0x1860 = 130 x 0x30 records). The roster.bin team
// table itself is sized from the file.
constexpr uint32_t kDbTeams = (kSchools + 1 + 63) / 64 * 64;   // 320 for 305 schools (stock buffers: 130)
constexpr uint32_t kTeamDwordBytes = kDbTeams * 4;
constexpr uint32_t kTeamRecordBytes = kDbTeams * 0x30;
constexpr uint32_t kStockTeamDwords = 130;
// Dwords in the per-team buffers (the poll vectors [db+0x50] / [db+0x54] among them) as this session made them.
inline uint32_t TeamDwords() { return Active() ? kDbTeams : kStockTeamDwords; }

// Team registry (0x4E08D0 -> 0x4D5130, stock 130 = 0x82): every team in roster.bin gets a slot at boot, and the
// menus, exhibition and the season all look teams up through it (Play Now quits on a team without a slot).
constexpr uint32_t kTeamSlots = kSchools;

// Menu spinner lists (vtable 0x610040, e.g. the Play Now conference spinner) remember a value per item in 17 slots
// at +0x18 (0xA8A80); a 24-conference spinner wrote item 18+ over the widget's item strings (+0x60 from the list) and
// Play Now crashed. Items past 17 are not remembered.
constexpr uint32_t kSpinnerMemory = 17;

// Season sub-heap (0x4D94C0, [0x6DC598]): schedules, conference lists and standings live in a 125 KB heap (0x1F400)
// made for 128 teams; 255 teams ran it out when a Dynasty was created (allocation returned 0 at 0x4F3183).
constexpr uint32_t kSeasonHeap = 0x100000;  // 1 MB (stock 125 KB)

// Inter-conference schedule capacity (0x4F3200): stock (teams x 9 x 4 / 2) = 18 games per team, enough for the
// stock ~16 non-conference games each. Expanded schedules hold ~26 non-conference games per team plus the conference
// games of conferences past the holder's 14: 255 schools overflowed 4,608 games into the season heap (allocator
// crash when a Dynasty was created). Games per team here; the schedule keeps capacity = teams x this / 2.
constexpr uint32_t kIntrconfGamesPerTeam = 64;

// Node map at [player db + 0xD8] (ctor 0x94130, allocated 0x98CEB): a red-black tree whose 24-byte nodes come
// from a pool of 3,750 (0xEA6) stored inside the object, the free-slot cursor right after it (+0x15F9C). With 6,400
// players the pool ran out during a Dynasty sim and the node allocator 0x79320 handed out a used node (endless loop
// in the tree fix-up 0x79520). Pool, cursor offset, clears and the object size scale with the player table.
constexpr uint32_t kNodeMapNodes = kBatterRows + 500 > 0xEA6 ? kBatterRows + 500 : 0xEA6;
constexpr uint32_t kNodeMapCursor = 0xC + kNodeMapNodes * 24;   // stock 0x15F9C
constexpr uint32_t kNodeMapBytes = kNodeMapCursor + 4;          // stock 0x15FA0
constexpr uint32_t kNodeMapPoolDwords = kNodeMapNodes * 6;      // stock 0x57E4

// Row capacities of the small tables built at 0x98880: conf.dat (stock 16 rows, row 0 unused) and tstat.dat with
// its pool at 0x93540 (stock 130).
constexpr uint32_t kConferenceRows = 32;
constexpr uint32_t kTeamRows = kDbTeams;

// Conference ids: the team record word at +0xA4 keeps the conference in bits 6-9 (15 ids) and the division in bit
// 10. The copies read bits 6-10 as the conference (ids up to 31) and the division getter 0x4D1910 answers 0, so an
// expanded database has one division per conference and bit 10 clear in every team record.

// The schedule holder keeps one schedule per conference at +0x10 + index * 4 (index 0-based: conf.dat row 1 is 0)
// up to the inter-conference schedule at +0x48; later words are postseason schedules. Conferences past the 14th load
// no file of their own (their games are in intrconf.dat).
constexpr uint32_t kScheduleConfs = 14;

// 0x500E70: whether the conference with this index loads <schedulename>.dat into the holder.
bool LoadConferenceSchedule(uint32_t conf);

// 9-bit team slots in schedule game records. A game record (20 bytes) has two sides, home at +4 and away at +0xC:
// byte 0 = team registry slot, byte 1 = runs, byte 2 = hits, byte 3 = errors and 9th-inning runs, then the innings
// 1-8 dword. Stock slots are one byte with 0xFF = no team, so only 255 teams could play. Slot bit 8 lives in bit 7 of
// the hits byte (hits never reach 128). Stored 0x0FF still means no team and slot 255 is stored as 0x1FF, so in
// registers the two swap: decoded no-team is kNoSlot (0x1FF) and slot 255 is 0xFF. Every reader of a slot decodes,
// every none check compares with kNoSlot, every hits reader masks 0x7F and every hits writer keeps bit 7 (the spec's
// "slot9" edits). Saves hold team ids, not slots, so their layout is unchanged. Stock records decode to themselves.
constexpr uint32_t kNoSlot = 0x1FF;
constexpr uint32_t kHitsMask = 0x7F;
inline uint32_t SlotSwap(uint32_t v) { return (v & 0xFF) == 0xFF ? v ^ 0x100 : v; }
// A side's first dword (slot in bits 0-7, hits in 16-23: slot bit 8 is bit 23) -> slot or kNoSlot.
inline uint32_t SideSlot(uint32_t side) { return SlotSwap((side & 0xFF) | ((side >> 15) & 0x100)); }
// The slot byte at ea (hits byte at ea + 2).
inline uint32_t SlotAt(const uint8_t* base, uint32_t ea) {
    return SlotSwap(base[ea] | ((base[uint32_t(ea + 2)] & 0x80u) << 1));
}
inline void StoreSlot(uint8_t* base, uint32_t ea, uint32_t slot) {
    uint32_t raw = SlotSwap(slot & 0x1FF);
    base[ea] = uint8_t(raw);
    uint8_t& hits = base[uint32_t(ea + 2)];
    hits = uint8_t((hits & kHitsMask) | ((raw >> 1) & 0x80));
}
// A hits byte written over the old one: keeps slot bit 8.
inline uint8_t KeepSlotBit(uint8_t old, uint32_t hits) { return uint8_t((old & 0x80) | (hits & kHitsMask)); }

// Standings screen (class ctor 0x5219D0, allocated 0x65C bytes in the screen factory 0x112BC0): its row -> team id
// array at +0x62C holds 12 rows and is the end of the object; the fill 0x542190 writes one per team of the
// conference, so a 16-team conference wrote over the next heap block's header and leaving Standings crashed in the
// allocator (0x419D15 from the widget dtor 0xC5780). Room for kStandingsRows rows (rows past 12 are not highlighted:
// 0x521A60 still walks 12).
constexpr uint32_t kStandingsRows = 32;
constexpr uint32_t kStandingsScreenBytes = 0x62C + kStandingsRows * 4;

// Save buffers (0x77D10 sizes, allocated by 0x77DA0) and the off-season departures buffers (0x4D79A0): stock sizes
// fit 3,250 players and the serializers / the departures writer 0x4EF2B0 do not check the end; ours are scaled by
// this (kBatterRows / 3,250 rounded up, plus room).
constexpr uint32_t kSaveScale = (kBatterRows + 3249) / 3250 + 1;   // 4 for 7,675 rows

}  // namespace expansion
