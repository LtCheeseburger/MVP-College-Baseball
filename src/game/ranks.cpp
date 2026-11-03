// National rank lookups for every team, with an expanded database (expansion.h). The national rank list
// (0x4FD2A0 with conference 0: team ids in poll order, {id, score} x 130 inline, count +0x410, valid flag +0x414) is
// built from the 130-id team list 0x984D0, so it holds ranks 1-130 only and a lookup of any team ranked lower
// answers 0: Dynasty Home showed "national rank 0" / prestige "#0" for teams past the first 130, prestige's rank term
// ((N + 1 - rank) / N) and the recruiting rank factor (0x4E4FE0, (C - rank) * k) treated them as better than #1.
// The poll (poll.cpp) ranks every team in the poll vector [db+0x50]; these hooks answer from it when a national list
// does not hold the team. The lists stay at 130 entries (Top 25 and other rank-order readers are unaffected). With
// the stock database every team fits the lists and the stock code runs.
#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "common/game_hooks.h"
#include "core/log.h"
#include "game/expansion.h"
#include "kernel/kernel.h"

namespace {

constexpr uint32_t kLeagueDb = 0x6C6540;
constexpr uint32_t kListCount = 0x410, kListValid = 0x414;
// the global national list (0x6DC640) and its one-entry cache used by 0x4E4FE0
constexpr uint32_t kGlobalList = 0x6DC640, kCacheValid = 0x6DC5B4, kCacheTeam = 0x6DC5B8, kCacheValue = 0x6DC5BC;
constexpr uint32_t kRankFactorTop = 0x61E398, kRankFactorScale = 0x63B288;

struct Signature {
    uint32_t count, first;
};
std::mutex g_lock;
std::unordered_map<uint32_t, Signature> g_national;   // list object -> what it held when built as national

uint32_t Arg(X86Context& ctx, int i) { return kernel::R32(ctx.esp + 4 + 4 * uint32_t(i)); }

bool IsNational(uint32_t list) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_national.find(list);
    return it != g_national.end() && it->second.count == kernel::R32(list + kListCount) &&
           it->second.first == kernel::R32(list);
}

}  // namespace

namespace expansion {
// National poll rank of a team (1 = best, 0 = unranked or unknown), from the current poll vector.
uint32_t PollRank(uint32_t team) {
    uint32_t db = kernel::R32(kLeagueDb);
    if (!db || !team || team >= kDbTeams) return 0;
    uint32_t vec = kernel::R32(db + 0x50), buf = vec ? kernel::R32(vec + 8) : 0;
    if (!buf) return 0;
    uint32_t rank = kernel::R32(buf + team * 4);
    if (rank) return rank;
    // Before the first weekly poll of a new Dynasty the vector holds the preseason ranks of ids 1-130 only (the
    // others arrive as 0). Rank the unranked after the last ranked team, in id order, until the poll ranks everyone.
    uint32_t teams = std::min(kernel::R32(kernel::R32(db + 0x34) + 8), kDbTeams), maxRank = 0, before = 0;
    if (team >= teams) return 0;
    for (uint32_t id = 1; id < teams; id++) {
        uint32_t r = kernel::R32(buf + id * 4);
        maxRank = r > maxRank ? r : maxRank;
        if (!r && id < team) before++;
    }
    return maxRank + 1 + before;
}
}  // namespace expansion

// Rank list fill (thiscall, conference; 0 = national).
X86_EXTERN_FUNC(__imp__sub_004FD2A0);
X86_FUNC(sub_004FD2A0) {
    uint32_t self = ctx.ecx, conf = Arg(ctx, 0);
    __imp__sub_004FD2A0(ctx, base);
    if (!expansion::Active()) return;
    std::lock_guard<std::mutex> lock(g_lock);
    if (conf == 0)
        g_national[self] = {kernel::R32(self + kListCount), kernel::R32(self)};
    else
        g_national.erase(self);
}

// Position of a value in a rank list (thiscall, value) -> 1-based index or 0. On a national list the value is a team
// id and the answer its national rank.
X86_EXTERN_FUNC(__imp__sub_004DAEC0);
X86_FUNC(sub_004DAEC0) {
    uint32_t self = ctx.ecx, team = Arg(ctx, 0);
    __imp__sub_004DAEC0(ctx, base);
    if (!expansion::Active() || ctx.eax || (kernel::R32(self + kListValid) & 0xFF) != 1 || !IsNational(self)) return;
    ctx.eax = expansion::PollRank(team);
}

// Recruiting rank factor of team edx from the global national list: (C - rank) * k, cached for one team. A team the
// list does not hold got rank 0 (the best value); use its poll rank.
X86_EXTERN_FUNC(__imp__sub_004E4FE0);
X86_FUNC(sub_004E4FE0) {
    if (!expansion::Active()) return __imp__sub_004E4FE0(ctx, base);
    uint32_t team = ctx.edx;
    bool cached = (kernel::R32(kCacheValid) & 0xFF) && kernel::R32(kCacheTeam) == team;
    __imp__sub_004E4FE0(ctx, base);
    if (cached || ctx.eax || (kernel::R32(kGlobalList + kListValid) & 0xFF) != 1 || !IsNational(kGlobalList)) return;
    uint32_t rank = expansion::PollRank(team);
    if (!rank) return;
    float top, scale;
    uint32_t a = kernel::R32(kRankFactorTop), b = kernel::R32(kRankFactorScale);
    std::memcpy(&top, &a, 4);
    std::memcpy(&scale, &b, 4);
    float v = (top - float(rank)) * scale;   // (130 - rank) * 0.1923: 24.8 for #1, 0 for #130
    if (v < 0) v = 0;                        // teams ranked past 130 count as #130
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    ctx.xmm[0].f[0] = v;
    kernel::W32(kCacheValue, bits);
    ctx.eax = rank;
}
