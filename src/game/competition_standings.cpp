#include "common/game_hooks.h"
#include "game/phase9/annual-league-config.h"
#include "game/phase9/common-standings-candidate.inc"
#include "game/phase9/specific-standings-candidate.inc"
#include "kernel/kernel.h"

void phase9::CompetitionStandings(X86Context& ctx,uint32_t cache){
    if(!Enabled(Feature::Competition))return;
    mvp::common_standings_candidate::Publish(ctx,kernel::g_base,cache);
    mvp::specific_standings_candidate::Publish(ctx,kernel::g_base,cache);
}
bool phase9::CompetitionDefer(X86Context& ctx){return Enabled(Feature::Competition)&&mvp::specific_standings_candidate::Defer(ctx,kernel::g_base);}
void phase9::CompetitionDatedRetry(X86Context& ctx,uint32_t world){if(Enabled(Feature::Competition))mvp::specific_standings_candidate::Retry(ctx,kernel::g_base,world);}
X86_EXTERN_FUNC(__imp__sub_004E4220);
X86_FUNC(sub_004E4220){if(phase9::Enabled(phase9::Feature::Competition))mvp::specific_standings_candidate::Invalidate(ctx,base,ctx.ecx);__imp__sub_004E4220(ctx,base);}
