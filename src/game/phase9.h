#pragma once
#include "cpu/x86_ops.h"

// Optional rule layers are selected by the installed mod set, not developer flags.
namespace phase9 {
constexpr uint32_t ScaleRecruiting(uint32_t original,uint32_t teams=309) {return (original*teams+127)/128;}
constexpr uint32_t kRecruitProspects=ScaleRecruiting(400);
constexpr uint32_t kRecruitPositionQuota=ScaleRecruiting(240);
enum class Feature { AnnualSchedules, Competition, StandingsAwards, ModernDynasty, SavesLauncher };
X86_HOST_API bool Enabled(Feature feature);
X86_HOST_API uint32_t ScheduleSlots();
X86_HOST_API uint32_t ScheduleManagerBytes();
X86_HOST_API uint32_t ScheduleImporterExtra();
X86_HOST_API uint32_t RegularSlot(uint32_t conference);
X86_HOST_API uint32_t FirstYear();
X86_HOST_API uint32_t ScheduleAnchor(uint32_t original,uint32_t earliest,uint32_t year);
X86_HOST_API void ObserveScheduleToken(X86Context& ctx);
X86_HOST_API bool ScheduleSchool(uint32_t id);
X86_HOST_API uint32_t CompactTokenBytes(uint32_t cursor,uint32_t buffer);
X86_HOST_API void NormalizeClass(uint32_t owner,uint32_t ordinal,bool assigning);
X86_HOST_API void NormalizeStatus(uint32_t roster,uint32_t slot);
X86_HOST_API uint32_t ClassLabel(uint32_t year,bool abbreviated);
uint32_t ClassText(uint32_t id);
X86_HOST_API void AddGraduateOption(X86Context& ctx);
X86_HOST_API void DynastySaveTick(X86Context& ctx);
void VerifySavedAnnual(unsigned year);
X86_HOST_API uint32_t CompetitionCapacity(uint32_t slot);
X86_HOST_API uint32_t CompetitionField(const uint8_t* base,uint32_t root);
X86_HOST_API uint32_t CompetitionGames(const uint8_t* base,uint32_t root);
X86_HOST_API void CompetitionHome(X86Context& ctx);
X86_HOST_API void PatriotFourth(X86Context& ctx);
X86_HOST_API void PatriotFixture(X86Context& ctx);
X86_HOST_API void CompetitionRetry(X86Context& ctx,uint32_t world);
int32_t CompetitionSlot(uint32_t conference);
bool CompetitionSeed(X86Context& ctx);
void CompetitionRegister(X86Context& ctx,uint32_t world);
void ProbeCompetition(X86Context& ctx);
X86_HOST_API void StandingsBegin(X86Context& ctx);
X86_HOST_API void StandingsStore(X86Context& ctx);
X86_HOST_API void StandingsClear(X86Context& ctx);
void ProbeAwards(X86Context& ctx);
bool DynastyMenuStable();
// A completed annual rollover queues one native autosave for the next stable dynasty menu.
void QueueSeasonAutosave(unsigned year);
X86_HOST_API void CompetitionStandings(X86Context& ctx,uint32_t cache);
bool CompetitionDefer(X86Context& ctx);
void CompetitionDatedRetry(X86Context& ctx,uint32_t world);
}
