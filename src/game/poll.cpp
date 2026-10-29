// National poll for every team. Replaces UpdatePoll (0x90AB0), whose stock poll 0x4F43F0 ranks at most 130 teams
// (130-entry stack arrays, the 130-id team list 0x984D0), with a C++ poll over every team in the season registry,
// using a formula fitted to the real D1Baseball Top 25 (2022-2025, season cross-validated; it also beats the stock
// formula against the Coaches, Baseball America, NCBWA and Perfect Game polls):
//
//   f = clamp(G / 30, 0, 1)
//   score = (1.00 - 0.69 f) PREV + (0.94 - 0.87 f) PRE + (0.07 + 0.42 f) z(WP) + (0.04 + 0.07 f) z(SOS)
//         + 0.12 z(SOS2) + (0.15 - 0.10 f) z(TAL) + (0.07 - 0.01 f) z(QW) - 0.07 f z(RL) + 0.02 WK
//   preseason (no games anywhere): z(TAL) + 0.25 PREVPTS(last final rank)
//   NCAA tournament under way:     2 STAGE + 0.2 NCAA_W - 0.3 NCAA_L + PREV + 0.02 z(RPI)
//
// PREV / PRE are last week's and the preseason poll points ((26 - rank) / 25 in the Top 25, else 0), WP win share,
// SOS / SOS2 opponents' WP and their SOS, TAL roster talent, QW / RL wins and losses against last week's Top 25, WK
// last week's net wins. Inputs come from the game state: W/L, opponents and NCAA games from every schedule of the
// holder [0x6DC59C], talent from the four roster grades of 0x86BC0 turned into the stock percentile, last week's rank
// from the poll vector [db+0x50], the NCAA stage from 0x4EFC20. The preseason rank (PRE) and the W/L at the previous
// poll (WK) are kept here: they are not in the save, so after loading a save mid-season PRE starts from the current
// poll and WK from the next poll.
//
// On with an expanded database (expansion.h: the stock poll cannot rank past 130 teams, and ranks.cpp reads every
// team's rank from this poll's vector); with the stock database only when the setting fitted_poll is on.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "common/game_hooks.h"
#include "core/log.h"
#include "game/expansion.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {

constexpr uint32_t kLeagueDb = 0x6C6540, kHolder = 0x6DC59C, kRegistry = 0x6DC5A0, kSeasonMgr = 0x6DC5A8;
constexpr uint32_t kSchedules = 0x25;                  // holder schedules at +0xC + i*4
constexpr uint32_t kFirstNcaa = 27, kCwsFinal = 36;    // NCAA sites 27-34, CWS 35, CWS final 36
constexpr uint32_t kRecordBytes = 20, kStatusFinal = 3;
constexpr uint32_t kTeamRanked = 25;


struct Game {
    uint32_t opp;
    bool won, ncaa;
};

struct Team {
    uint32_t id = 0;
    int w = 0, l = 0, ncaaW = 0, ncaaL = 0;
    std::vector<Game> games;
    int lastRank = 0, preRank = 0, stage = 0;
    bool cwsFinal = false;   // played in the CWS final (holder 36)
    float talent = 0, wp = 0.5f, sos = 0.5f, sos2 = 0.5f, qw = 0, rl = 0, wk = 0, score = 0;
};

// state kept between polls (team id indexed)
std::mutex g_lock;
std::vector<int> g_pre, g_snapW, g_snapL;
int g_lastGames = -1;

uint32_t CallThis(X86Context& ctx, uint32_t self, uint32_t fn, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = self;
    uint32_t r = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
    return r;
}

float AsFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

float PollPoints(int rank) { return rank >= 1 && rank <= int(kTeamRanked) ? float(26 - rank) / 25.0f : 0.0f; }

void ZScore(const std::vector<Team>& t, float Team::*field, std::vector<float>& z) {
    double s = 0, ss = 0;
    int m = 0;
    for (const Team& x : t)
        if (!x.games.empty()) {
            s += x.*field;
            ss += double(x.*field) * (x.*field);
            m++;
        }
    double mu = m ? s / m : 0, var = m ? ss / m - mu * mu : 0, sd = var > 1e-12 ? std::sqrt(var) : 0;
    z.assign(t.size(), 0.0f);
    for (size_t i = 0; i < t.size(); i++)
        if (!t[i].games.empty() && sd > 0) z[i] = float(std::clamp((t[i].*field - mu) / sd, -3.0, 3.0));
}

// Rank percentile (1 = best .. 1/n) of v over all teams, descending, as the stock talent list does (0x4CF5D0).
std::vector<float> Percentile(const std::vector<float>& v) {
    std::vector<size_t> order(v.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return v[a] > v[b]; });
    std::vector<float> p(v.size());
    for (size_t k = 0; k < order.size(); k++) p[order[k]] = float(order.size() - k) / float(order.size());
    return p;
}

// Talent (0..1, 1 = best roster): the stock TALENT term. 0x86BC0(team, &a, &b, &c, &d) gives four roster grades;
// each becomes a percentile, they combine as 0.1 * (4 (pa + pb) + pc + pd), then a percentile again.
void Talent(X86Context& ctx, uint32_t db, std::vector<Team>& teams) {
    static uint32_t scratch = mem::AllocSystem(64);
    if (!scratch) return;
    std::vector<float> g[4];
    for (Team& t : teams) {
        for (int k = 0; k < 4; k++) kernel::W32(scratch + 4 * uint32_t(k), 0);
        CallThis(ctx, db, 0x86BC0, {t.id, scratch, scratch + 4, scratch + 8, scratch + 12});
        for (int k = 0; k < 4; k++) g[k].push_back(AsFloat(kernel::R32(scratch + 4 * uint32_t(k))));
    }
    std::vector<float> p[4];
    for (int k = 0; k < 4; k++) p[k] = Percentile(g[k]);
    std::vector<float> combined(teams.size());
    for (size_t i = 0; i < teams.size(); i++) combined[i] = 0.1f * (4.0f * (p[0][i] + p[1][i]) + p[2][i] + p[3][i]);
    std::vector<float> tal = Percentile(combined);
    for (size_t i = 0; i < teams.size(); i++) teams[i].talent = tal[i];
}

// Every final game of every schedule: W/L, opponents, NCAA games. Returns the number of final games.
int CollectGames(std::vector<Team>& teams, const std::vector<int>& index) {
    uint32_t holder = kernel::R32(kHolder), reg = kernel::R32(kRegistry);
    if (!holder || !reg) return 0;
    uint32_t regCount = kernel::R32(reg + 0xC), regIds = kernel::R32(reg + 0x10);
    auto team = [&](uint32_t side) -> int {
        uint32_t slot = expansion::SideSlot(side);
        if (slot >= regCount) return -1;
        uint32_t id = kernel::R32(regIds + slot * 4);
        return id < index.size() ? index[id] : -1;
    };
    int finals = 0;
    for (uint32_t i = 0; i < kSchedules; i++) {
        uint32_t s = kernel::R32(holder + 0xC + 4 * i);
        if (!s) continue;
        uint32_t arr = kernel::R32(s + 0x28), n = kernel::R32(s + 0x24);
        bool ncaa = i >= kFirstNcaa && i <= kCwsFinal;
        for (uint32_t g = 0; arr && g < n && g < 40000; g++) {
            uint32_t r = arr + g * kRecordBytes, d0 = kernel::R32(r);
            if (((d0 >> 20) & 0xF) != kStatusFinal) continue;
            uint32_t home = kernel::R32(r + 4), away = kernel::R32(r + 12);
            int h = team(home), a = team(away);
            if (h < 0 || a < 0 || h == a) continue;
            uint32_t hr = (home >> 8) & 0xFF, ar = (away >> 8) & 0xFF;
            if (hr == ar) continue;   // no ties in baseball; an unplayed record
            bool homeWon = hr > ar;
            Team &th = teams[size_t(h)], &ta = teams[size_t(a)];
            th.games.push_back({uint32_t(a), homeWon, ncaa});
            ta.games.push_back({uint32_t(h), !homeWon, ncaa});
            (homeWon ? th.w : th.l)++;
            (homeWon ? ta.l : ta.w)++;
            if (i == kCwsFinal) th.cwsFinal = ta.cwsFinal = true;
            if (ncaa) {
                (homeWon ? th.ncaaW : th.ncaaL)++;
                (homeWon ? ta.ncaaL : ta.ncaaW)++;
            }
            finals++;
        }
    }
    return finals;
}


// The poll: every registry team, best first.
std::vector<uint32_t> RunPoll(X86Context& ctx) {
    uint32_t db = kernel::R32(kLeagueDb), reg = kernel::R32(kRegistry), mgr = kernel::R32(kSeasonMgr);
    uint32_t regCount = kernel::R32(reg + 0xC), regIds = kernel::R32(reg + 0x10);
    uint32_t vec = kernel::R32(db + 0x50), ranks = kernel::R32(vec + 8), vecCount = kernel::R32(vec + 4);
    std::vector<Team> teams;
    std::vector<int> index(expansion::kDbTeams + 1, -1);
    for (uint32_t s = 0; s < regCount; s++) {
        uint32_t id = kernel::R32(regIds + s * 4);
        if (!id || id > expansion::kDbTeams || index[id] >= 0) continue;
        index[id] = int(teams.size());
        Team t;
        t.id = id;
        t.lastRank = id < vecCount ? int(kernel::R32(ranks + id * 4)) : 0;
        teams.push_back(t);
    }
    int finals = CollectGames(teams, index);
    Talent(ctx, db, teams);
    const size_t n = teams.size();

    std::lock_guard<std::mutex> lock(g_lock);
    bool reset = g_pre.size() != index.size() || finals < g_lastGames;
    if (reset) {   // first poll of this session, or a new season / Dynasty: PRE from the current poll
        g_pre.assign(index.size(), 0);
        g_snapW.assign(index.size(), 0);
        g_snapL.assign(index.size(), 0);
        for (const Team& t : teams) {
            g_pre[t.id] = t.lastRank;
            g_snapW[t.id] = t.w;
            g_snapL[t.id] = t.l;
        }
    }
    for (Team& t : teams) {
        t.preRank = g_pre[t.id];
        t.wk = float((t.w - g_snapW[t.id]) - (t.l - g_snapL[t.id]));
    }

    const bool preseason = finals == 0;
    bool ncaa = false;
    for (const Team& t : teams) ncaa = ncaa || t.ncaaW + t.ncaaL > 0;

    // WP, SOS, SOS2, quality wins / losses against last week's Top 25
    for (Team& t : teams) {
        int g = int(t.games.size());
        t.wp = g ? float(t.w) / float(g) : 0.5f;
    }
    for (Team& t : teams) {
        float s = 0;
        for (const Game& g : t.games) {
            const Team& o = teams[g.opp];
            s += o.wp;
            bool ranked = o.lastRank >= 1 && o.lastRank <= int(kTeamRanked);
            if (ranked && g.won) t.qw += 1;
            if (ranked && !g.won) t.rl += 1;
        }
        t.sos = t.games.empty() ? 0.5f : s / float(t.games.size());
    }
    for (Team& t : teams) {
        float s = 0;
        for (const Game& g : t.games) s += teams[g.opp].sos;
        t.sos2 = t.games.empty() ? 0.5f : s / float(t.games.size());
    }

    if (preseason) {
        // every team G = 0: z over all teams
        double m = 0, ss = 0;
        for (const Team& t : teams) m += t.talent;
        m /= double(std::max<size_t>(n, 1));
        for (const Team& t : teams) ss += (t.talent - m) * (t.talent - m);
        double sd = n ? std::sqrt(ss / double(n)) : 0;
        for (Team& t : teams) {
            float z = sd > 0 ? float(std::clamp((t.talent - m) / sd, -3.0, 3.0)) : 0.0f;
            t.score = z + 0.25f * PollPoints(t.lastRank);
        }
    } else {
        std::vector<float> zwp, zsos, zsos2, ztal, zqw, zrl, zrpi;
        ZScore(teams, &Team::wp, zwp);
        ZScore(teams, &Team::sos, zsos);
        ZScore(teams, &Team::sos2, zsos2);
        ZScore(teams, &Team::talent, ztal);
        ZScore(teams, &Team::qw, zqw);
        ZScore(teams, &Team::rl, zrl);
        if (ncaa) {
            std::vector<Team> rpi = teams;
            for (Team& t : rpi) t.score = 0.25f * t.wp + 0.5f * t.sos + 0.25f * t.sos2;
            ZScore(rpi, &Team::score, zrpi);
        }
        for (size_t i = 0; i < n; i++) {
            Team& t = teams[i];
            float prev = PollPoints(t.lastRank), pre = PollPoints(t.preRank);
            if (t.games.empty()) {
                t.score = -1000.0f + pre + 0.01f * t.talent;
                continue;
            }
            if (ncaa) {
                // game stage (0x4EFC20): 5 champion, 4 CWS, 3 alive at an NCAA site, 2 out at a site, else 0/1
                uint32_t st = mgr ? CallThis(ctx, mgr, 0x4EFC20, {t.id}) : 0;
                float stage = st >= 5 ? 5.0f : st == 4 ? 3.0f : st == 3 ? 2.0f : st == 2 ? 1.0f : 0.0f;
                if (st == 4 && t.cwsFinal) stage = 4.0f;
                t.stage = int(stage);
                t.score = 2.0f * stage + 0.2f * float(t.ncaaW) - 0.3f * float(t.ncaaL) + prev + 0.02f * zrpi[i];
                continue;
            }
            float f = std::clamp(float(t.games.size()) / 30.0f, 0.0f, 1.0f);
            t.score = (1.00f - 0.69f * f) * prev + (0.94f - 0.87f * f) * pre + (0.07f + 0.42f * f) * zwp[i] +
                      (0.04f + 0.07f * f) * zsos[i] + 0.12f * zsos2[i] + (0.15f - 0.10f * f) * ztal[i] +
                      (0.07f - 0.01f * f) * zqw[i] - 0.07f * f * zrl[i] + 0.02f * t.wk;
        }
    }

    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (teams[a].score != teams[b].score) return teams[a].score > teams[b].score;
        return teams[a].id < teams[b].id;
    });
    std::vector<uint32_t> ids;
    for (size_t k = 0; k < n; k++) ids.push_back(teams[order[k]].id);

    // remember for the next poll
    if (preseason)
        for (size_t k = 0; k < n; k++) g_pre[teams[order[k]].id] = int(k + 1);
    for (const Team& t : teams) {
        g_snapW[t.id] = t.w;
        g_snapL[t.id] = t.l;
    }
    g_lastGames = finals;

    return ids;
}

}  // namespace

// UpdatePoll (no arguments): new poll into [db+0x50], the previous one into [db+0x54], event 0xAF posted on the
// schedule holder (0x326B50, thiscall). The stock one is the switch in expansion_gen.cpp (its copy sizes the vectors
// for an expanded database).
X86_EXTERN_FUNC(sub_00090AB0_gen);
X86_FUNC(sub_00090AB0) {
    if (!expansion::Active() && !game::g_fitted_poll) return sub_00090AB0_gen(ctx, base);
    uint32_t db = kernel::R32(kLeagueDb);
    std::vector<uint32_t> ids = RunPoll(ctx);
    const uint32_t dwords = expansion::TeamDwords();   // the vectors' buffers: 130 dwords stock
    uint32_t cur = kernel::R32(db + 0x50), prev = kernel::R32(db + 0x54);
    uint32_t teamCount = kernel::R32(kernel::R32(db + 0x34) + 8);
    kernel::W32(prev + 4, kernel::R32(cur + 4));
    std::memcpy(kernel::HostPtr(kernel::R32(prev + 8)), kernel::HostPtr(kernel::R32(cur + 8)), dwords * 4);
    *static_cast<uint8_t*>(kernel::HostPtr(prev)) = *static_cast<uint8_t*>(kernel::HostPtr(cur));
    kernel::W32(cur + 4, teamCount);
    uint32_t buf = kernel::R32(cur + 8);
    std::vector<uint32_t> rank(dwords, 0);
    for (size_t k = 0; k < ids.size(); k++)
        if (ids[k] < dwords) rank[ids[k]] = uint32_t(k + 1);
    for (uint32_t i = 0; i < dwords; i++) kernel::W32(buf + i * 4, rank[i]);
    *static_cast<uint8_t*>(kernel::HostPtr(cur)) = 1;
    CallThis(ctx, kernel::R32(kHolder), 0x326B50, {0xAF, 0, 0, 0});   // thiscall on the schedule holder
    ctx.esp += 4;   // ret (no arguments)
}
