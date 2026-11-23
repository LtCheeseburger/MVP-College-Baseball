#include "common/game_hooks.h"
#include "game/phase9.h"
#include "game/phase9/schedule-phase.h"
#include "game/phase9/annual-league-config.h"
#include "game/phase9/four-team-de-candidate.h"
#include "game/phase9/six-team-de-candidate.h"
#include "game/phase9/division-postseason-candidate.h"
#include "game/phase9/patriot-postseason-candidate.h"
#include "game/phase9/national-field-candidate.h"
#include "game/phase9/bracket-browser.h"
#include "kernel/kernel.h"
#include "core/log.h"
#include "core/dev_tools.h"

namespace {
namespace a=mvp::annual_schedule;
#define FAMILIES(X) X(four_team_de) X(six_team_de) X(large_de) X(single_elimination) X(hybrid_postseason) X(twin_bracket) X(southland_postseason) X(staggered_postseason) X(bigten_postseason) X(cusa_postseason) X(maac_postseason) X(mac_postseason) X(division_postseason)
}
X86_EXTERN_FUNC(__imp__sub_004DF800);
X86_FUNC(sub_004DF800){
    if(!phase9::Enabled(phase9::Feature::Competition))return __imp__sub_004DF800(ctx,base);
    const auto out=kernel::R32(ctx.esp+4);
    const auto [table,row]=mvp::bracket_browser::Pending(base,ctx.ecx);
    kernel::W32(out,table);kernel::W32(out+4,row);
    ctx.eax=out;ctx.esp+=8;
}
unsigned phase9::SeasonYear(const uint8_t* b){
    auto read=[&](uint32_t at){return mvp::game_record_candidate::Read(b,at);};
    auto manager=read(0x6dc59c),table=manager?read(manager+16):0;
    a::GraphRequire(table&&read(table+8)==1,"Competition calendar: regular table 1 is absent");
    auto first=int32_t(read(table+20));
    a::GraphRequire(first>=a::JanuaryFirst(2027)&&first<a::JanuaryFirst(2057),"Competition calendar: season year outside supported range");
    unsigned year=2027;while(a::JanuaryFirst(year+1)<=first)++year;return year;
}
uint32_t phase9::TournamentStart(const uint8_t* b,unsigned key){
    auto year=SeasonYear(b);for(const auto& c:a::authored::Conferences())if(c.rule.key==key)return a::SeasonWindow(year,c.event,a::authored::FirstDate(year)).event_start;
    throw std::runtime_error("Competition calendar: unknown conference");
}
int32_t phase9::QualificationCutoff(const uint8_t* b,unsigned key){
    auto year=SeasonYear(b);for(const auto& c:a::authored::Conferences())if(c.rule.key==key)return a::SeasonWindow(year,c.event,a::authored::FirstDate(year)).last+1;
    throw std::runtime_error("Competition calendar: unknown conference");
}
uint32_t phase9::CompetitionCapacity(uint32_t slot){return mvp::division_postseason::Capacity(slot);}
uint32_t phase9::CompetitionField(const uint8_t* b,uint32_t root){return mvp::division_postseason::Field(b,root);}
uint32_t phase9::CompetitionGames(const uint8_t* b,uint32_t root){return mvp::division_postseason::Games(b,root);}
void phase9::PatriotFourth(X86Context& ctx){if(Enabled(Feature::Competition))mvp::patriot_postseason::FourthSeed(ctx,kernel::g_base);}
void phase9::PatriotFixture(X86Context& ctx){if(Enabled(Feature::Competition))mvp::patriot_postseason::SeriesFixture(ctx,kernel::g_base);}
void phase9::CompetitionRetry(X86Context& ctx,uint32_t world){
    if(!Enabled(Feature::Competition))return;
    const auto current=kernel::R32(0x6DC5A8),manager=kernel::R32(0x6DC59C);
    // The event notification caller can own a dispatcher rather than the league world.
    // Resolve that owner through the active season manager, retaining its invariant.
    if(current && manager && kernel::R32(current+0x10)==manager && world!=current){
        world=current;
    }
    mvp::division_postseason::Retry(ctx,kernel::g_base,world);
    CompetitionDatedRetry(ctx,world);
}
int32_t phase9::CompetitionSlot(uint32_t key){for(const auto& c:a::authored::Conferences())if(c.rule.key==key)return c.tournament;return -1;}
void phase9::CompetitionHome(X86Context& ctx){
    if(!Enabled(Feature::Competition))return;
#define HOME(name) mvp::name::HomeSeed(ctx,kernel::g_base);
    FAMILIES(HOME)
#undef HOME
}
bool phase9::CompetitionSeed(X86Context& ctx){
    if(!Enabled(Feature::Competition))return false;
#define SEED(name) if(mvp::name::Seed(ctx,kernel::g_base))return true;
    FAMILIES(SEED)
#undef SEED
    return false;
}
void phase9::CompetitionRegister(X86Context& ctx,uint32_t world){
    if(!Enabled(Feature::Competition))return;
    const auto saved=ctx;struct Restore{X86Context& c;X86Context old;~Restore(){c=old;}} restore{ctx,saved};ctx.esi=world;
#define REGISTER(name) mvp::name::RegisterAdded(ctx,kernel::g_base);
    REGISTER(four_team_de) REGISTER(six_team_de) REGISTER(large_de) REGISTER(hybrid_postseason) REGISTER(twin_bracket)
    REGISTER(southland_postseason) REGISTER(staggered_postseason) REGISTER(maac_postseason) REGISTER(mac_postseason) REGISTER(division_postseason)
#undef REGISTER
    auto manager=kernel::R32(world+16);unsigned verified=0;
    for(const auto& conf:a::authored::Conferences()){
        auto root=kernel::R32(manager+12+4*conf.tournament);
        auto valid=root&&kernel::R32(root+8)==conf.tournament&&kernel::R32(root+16)==conf.rule.key;
        if(valid&&conf.rule.key!=10){
            auto child=kernel::R32(root+0x38);auto four=mvp::four_team_de::Owner(kernel::g_base,root)!=nullptr;
            valid=child&&kernel::R32(child+0x64)==root&&kernel::R32(child+0x5c)==(four?4:CompetitionField(kernel::g_base,root))&&kernel::R32(child+0x60)==(four?7:CompetitionGames(kernel::g_base,root));
        }
        if(!valid){LOGE("competition: native owner differs for conference %u / slot %u",conf.rule.key,conf.tournament);throw std::runtime_error("Authored tournament owner shape differs");}
        ++verified;
    }
    LOGR("competition: %u native conference owners verified",verified);
    LOGR("competition: authored conference tournament owners registered");
}
X86_EXTERN_FUNC(__imp__sub_004EA4D0);
X86_FUNC(sub_004EA4D0){if(phase9::Enabled(phase9::Feature::Competition)&&mvp::four_team_de::Route(ctx,base))return;__imp__sub_004EA4D0(ctx,base);}
X86_EXTERN_FUNC(__imp__sub_004EA880);
X86_FUNC(sub_004EA880){
    if(phase9::Enabled(phase9::Feature::Competition)){
        // The native bracket renderer probes eleven visual cells even when
        // this conference's graph has fewer games. Such cells are empty, not
        // invalid gameplay requests. Keep strict adapter validation elsewhere.
        if(mvp::bracket_browser::EmptyVisualOrdinal(kernel::R32(ctx.esp),kernel::R32(ctx.esp+8),kernel::R32(ctx.ecx+0x60))){
            const auto out=kernel::R32(ctx.esp+4);
            kernel::W32(out,0xFFFFFFFFu);kernel::W32(out+4,2);
            ctx.eax=out;ctx.esp+=20;return;
        }
#define ROUTE(name) if(mvp::name::Route(ctx,base))return;
        FAMILIES(ROUTE)
#undef ROUTE
    }
    __imp__sub_004EA880(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_004E9F60);
X86_FUNC(sub_004E9F60){if(phase9::Enabled(phase9::Feature::Competition))mvp::four_team_de::Fixture(ctx,base);__imp__sub_004E9F60(ctx,base);}
X86_EXTERN_FUNC(__imp__sub_004F1BA0);
X86_FUNC(sub_004F1BA0){
    if(phase9::Enabled(phase9::Feature::Competition)){
        auto root=kernel::R32(ctx.ecx+0x64);
#define OWNER(name) if(mvp::name::Owner(base,root)){x86_icall(ctx,base,0x4e9a50);return;}
        FAMILIES(OWNER)
#undef OWNER
    }
    __imp__sub_004F1BA0(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_004E9C70);
X86_FUNC(sub_004E9C70){
    if(phase9::Enabled(phase9::Feature::Competition)){
#define CHAMPION(name) if(mvp::name::Champion(ctx,base))return;
        CHAMPION(single_elimination) CHAMPION(twin_bracket) CHAMPION(southland_postseason) CHAMPION(staggered_postseason)
        CHAMPION(bigten_postseason) CHAMPION(cusa_postseason) CHAMPION(maac_postseason) CHAMPION(mac_postseason)
#undef CHAMPION
    }
    __imp__sub_004E9C70(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_004FEDE0);
X86_FUNC(sub_004FEDE0){
    if(!phase9::Enabled(phase9::Feature::Competition))return __imp__sub_004FEDE0(ctx,base);
    auto out=kernel::R32(ctx.esp+4),world=ctx.ecx;
    mvp::national_field::ValidateInputs(ctx,base);
    __imp__sub_004FEDE0(ctx,base);
    auto check=ctx;check.ebp=out;check.ebx=world;mvp::national_field::ValidateOutput(check,base);
    LOGR("competition: native 64-team national field verified, 30 conference champions / complete 309-school poll");
}
#undef FAMILIES

// Controlled integration fixture, available only in DEV. This does not simulate
// a season or certify gameplay/statistics: it exercises the real native bracket
// allocation/seeding paths against a deterministic set of completed game rows.
void phase9::ProbeCompetition(X86Context& ctx){
    if(!dev::Env("MVP06_TEST_COMPETITION_SEED"))return;
    if(!Enabled(Feature::Competition))throw std::runtime_error("Competition probe requires its optional mod");
    auto saved=ctx;struct Restore{X86Context& c;X86Context old;~Restore(){c=old;}} restore{ctx,saved};
    auto manager=kernel::R32(0x6dc59c),world=kernel::R32(0x6dc5a8);unsigned finals=0;
    for(unsigned slot=1;slot<72;++slot){
        if(slot>15&&(slot<37||slot>52))continue;
        auto root=kernel::R32(manager+12+4*slot);if(!root)continue;
        auto count=kernel::R32(root+36),data=kernel::R32(root+40);
        for(unsigned i=0;i<count;++i){auto row=data+20*i;kernel::W32(row,(kernel::R32(row)&~0xf00000u)|(3u<<20));kernel::g_base[row+5]=1;kernel::g_base[row+13]=0;++finals;}
    }
    if(finals!=8588)throw std::runtime_error("Competition probe regular population differs");
    auto event=saved.esp-0x100,descriptor=event+0x20,champion=event+0x40,collector=event+0x60;
    auto call=[&](unsigned method,unsigned self,std::initializer_list<uint32_t> args){ctx=saved;ctx.esp=saved.esp-0x800;ctx.ecx=self;return kernel::CallGuest(ctx,method,args);};
    auto seed=[&](unsigned regular){kernel::W32(event+8,regular);call(0x5000e0,world,{event});};
    // Division snapshots must precede construction of other earlier-dated
    // postseason rows; their normal runtime retry waits for those results.
    seed(41);seed(52);
    std::set<unsigned> completed;unsigned results=0;
    for(unsigned attempt=0;attempt<60&&completed.size()<30;++attempt){
        bool progress=false;
        for(const auto& conf:a::authored::Conferences()){
            if(completed.contains(conf.rule.key))continue;
            auto root=kernel::R32(manager+12+4*conf.tournament);if(!kernel::R32(root+36))seed(conf.regular);
            unsigned transitions=0;
            for(;transitions<40;++transitions){
                auto count=kernel::R32(root+36),capacity=kernel::R32(root+28),data=kernel::R32(root+40);bool played=false;
                if(count>capacity)throw std::runtime_error("Competition probe native table exceeded capacity");
                for(unsigned i=0;i<count;++i){
                    auto row=data+20*i,status=(kernel::R32(row)>>20)&15;
                    auto home=expansion::SlotAt(kernel::g_base,row+4),away=expansion::SlotAt(kernel::g_base,row+12);
                    if(status>=2||home>=309||away>=309)continue;
                    if(home==away)throw std::runtime_error("Competition probe self-match");
                    kernel::g_base[row+5]=(i&1)?2:5;kernel::g_base[row+13]=(i&1)?5:2;
                    kernel::W32(row,(kernel::R32(row)&~0xf00000u)|(3u<<20));kernel::W32(descriptor,conf.tournament);kernel::W32(descriptor+4,i);
                    call(conf.rule.key==10?0x4fac70:0x4e89d0,root,{descriptor});
                    ++results;played=progress=true;break;
                }
                if(!played)break;
            }
            if(transitions==40)throw std::runtime_error("Competition probe did not converge");
            std::memset(kernel::HostPtr(champion),0,16);
            call(kernel::R32(kernel::R32(root)+0x58),root,{champion});
            auto team=kernel::R32(champion+4);
            if(team){if(!expansion::SchoolActive(team))throw std::runtime_error("Competition probe invalid champion");completed.insert(conf.rule.key);progress=true;LOGR("competition probe: conference %u champion %u",conf.rule.key,team);}
        }
        if(!progress){
            for(const auto& conf:a::authored::Conferences())if(!completed.contains(conf.rule.key)){
                auto root=kernel::R32(manager+12+4*conf.tournament);LOGE("competition probe: stalled conference %u, %u rows",conf.rule.key,kernel::R32(root+36));
                auto ownData=kernel::R32(root+40),child=kernel::R32(root+0x38),descriptors=kernel::R32(child+0x6c);
                for(unsigned i=0;i<kernel::R32(root+36);++i){auto row=ownData+20*i;LOGE("competition probe: own row %u status %u participants %u/%u descriptor %08X index %u",i,(kernel::R32(row)>>20)&15,expansion::SlotAt(kernel::g_base,row+4),expansion::SlotAt(kernel::g_base,row+12),kernel::R32(descriptors+12*i+8),kernel::R32(descriptors+12*i+4));}
                auto cutoff=QualificationCutoff(kernel::g_base,conf.rule.key);
                for(unsigned slot=1;slot<72;++slot){auto table=kernel::R32(manager+12+4*slot);if(!table)continue;auto data=kernel::R32(table+40);
                    for(unsigned i=0;i<kernel::R32(table+36);++i){auto row=data+20*i,word=kernel::R32(row);auto day=int32_t(kernel::R32(table+20)+(word&511));if(((word>>20)&15)<2&&day<cutoff)LOGE("competition probe: pending slot %u row %u day %d before cutoff %d participants %u/%u",slot,i,day,cutoff,expansion::SlotAt(kernel::g_base,row+4),expansion::SlotAt(kernel::g_base,row+12));}
                }
            }
            throw std::runtime_error("Competition probe stalled at deferred bracket/snapshot");
        }
    }
    if(completed.size()!=30)throw std::runtime_error("Competition probe missing conference champions");
    call(0x90ab0,0,{}); // Production fitted poll, all309 schools.
    call(0x30fe50,collector,{64,0});
    try{call(0x4fede0,world,{collector});}catch(...){call(0x30feb0,collector,{});throw;}
    call(0x30feb0,collector,{});
    LOGR("competition probe: 30 native brackets completed, %u controlled postseason results, original national collector passed; gameplay/season simulation not certified",results);
}


void phase9::RefreshConferenceResets(X86Context& ctx,uint8_t* base,uint32_t manager){
    const auto root=kernel::R32(manager+12+65*4);
    if(!root||!mvp::mac_postseason::Owner(base,root)||!kernel::R32(root+36))return;
    const auto child=kernel::R32(root+0x38);
    if(!child)return;
    auto copy=ctx;
    const auto rows=kernel::R32(root+40),count=kernel::R32(root+36);
    uint32_t before=0;for(unsigned i=0;i<count;++i)before+=kernel::R32(rows+20*i)&0x00F00000u;
    mvp::mac_postseason::Advance(copy,base,child,true);
    uint32_t after=0;for(unsigned i=0;i<count;++i)after+=kernel::R32(rows+20*i)&0x00F00000u;
    if(before!=after){
        mvp::mac_postseason::Call(copy,base,0x004E7DF0u,root,{});
        mvp::mac_postseason::Call(copy,base,0x004D2960u,root,{});
        LOGR("competition: required MAC championship reset restored from real one-loss finalists");
    }
}



