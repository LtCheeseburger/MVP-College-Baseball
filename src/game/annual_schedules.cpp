// Rewritten bridge for PR #1's original 20-byte records; donor wide-record
// adapters and save serializers are deliberately not part of this module.
#include "common/game_hooks.h"
#include "game/phase9.h"
#include "game/expansion.h"
#include "game/phase9/annual-league-config.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "core/patch_json.h"
#include "core/log.h"
#include <sstream>

namespace {
namespace a=mvp::annual_schedule;
uint32_t Arg(const X86Context& c,unsigned n){return kernel::R32(c.esp+4+4*n);}
void Require(bool value,const char* message){if(!value){LOGE("annual schedules: %s",message);throw std::runtime_error(message);}}
struct Payload {unsigned slot;std::string csv="DATE,TIME,HOME,ROAD\r\n";unsigned count=0;};
struct Import;
thread_local Import* active=nullptr;
struct Import {
    unsigned year;uint32_t manager;
    std::vector<a::Fixture> fixtures;
    std::map<unsigned,std::pair<unsigned,std::string>> tokens;
    std::map<std::string,Payload> payloads;
    std::map<uint32_t,std::string> allocations;
    std::set<std::string> loaded,freed;
    Import(X86Context& c,unsigned y):year(y),manager(kernel::R32(c.ecx+16)) {
        Require(!active&&manager&&year>=2027&&year<=2056,"Annual schedules: invalid session/year (supported 2027-2056)");
        if(year==2027){
            std::vector<uint8_t> bytes;Require(kernel::ReadGameFile("D:\\data\\phase9\\annual\\fixtures-2027.json",bytes),"Annual schedules: first-season fixture payload missing");
            patch_json::Json doc;Require(patch_json::ParseJson(std::string(bytes.begin(),bytes.end()),doc),"Annual schedules: malformed first-season fixture payload");
            const auto* games=doc.Get("games");Require(doc.Num("season",0)==2027&&games&&games->items.size()==8588,"Annual schedules: invalid first-season population");
            auto teams=a::authored::Teams();std::map<std::string,unsigned> indices;for(unsigned i=0;i<teams.size();++i)indices[teams[i].id]=i;
            for(const auto& g:games->items){unsigned y0,m,d,h,minute;Require(std::sscanf(g.Str("date").c_str(),"%u-%u-%u",&y0,&m,&d)==3&&std::sscanf(g.Str("time").c_str(),"%u:%u",&h,&minute)==2&&y0==2027,"Annual schedules: malformed fixture date");
                unsigned home=indices.at(g.Str("home")),away=indices.at(g.Str("away"));bool conf=teams[home].key==teams[away].key;
                fixtures.push_back({a::Day(year,m,d),h*60+minute,home,away,0,conf?a::FixtureKind::Conference:a::FixtureKind::Midweek,conf?teams[home].key:0,0});}
        } else fixtures=a::authored::Generate(year).fixtures;
        active=this;
    }
    ~Import(){for(auto& [at,name]:allocations){(void)name;mem::FreeSystem(at);}active=nullptr;}
    void Build(){
        Require(tokens.size()==309,"Annual schedules: ranked token map omitted active schools");
        for(const auto& c:a::authored::Conferences())payloads.emplace(c.stem+".dat",Payload{c.regular});
        payloads.emplace("intrconf.dat",Payload{15});auto teams=a::authored::Teams();
        auto token=[&](unsigned index){const auto& t=teams.at(index);auto [key,text]=tokens.at(a::authored::kEngine.at(t.id));Require(key==t.key&&!text.empty(),"Annual schedules: ranked token differs from canonical conference");return text;};
        std::vector<unsigned> games(309),homes(309);std::set<std::tuple<unsigned,int32_t,unsigned>> dates;
        for(const auto& g:fixtures){Require(g.home<309&&g.away<309&&g.home!=g.away,"Annual schedules: bad participants");
            for(auto team:{g.home,g.away}){++games[team];Require(dates.emplace(team,g.day,g.minute).second,"Annual schedules: simultaneous games");}++homes[g.home];
            unsigned month=1;while(month<12&&g.day>=a::Day(year,month+1,1))++month;unsigned day=g.day-a::Day(year,month,1)+1;
            std::string name="intrconf.dat";if(g.key)for(const auto& c:a::authored::Conferences())if(c.rule.key==g.key)name=c.stem+".dat";
            auto& p=payloads.at(name);p.csv+=std::to_string(month)+"/"+std::to_string(day)+"/"+std::to_string(year)+","+std::to_string(g.minute/60)+":"+(g.minute%60<10?"0":"")+std::to_string(g.minute%60)+","+token(g.home)+","+token(g.away)+"\r\n";++p.count;
        }
        for(unsigned i=0;i<309;++i)Require(games[i]==teams[i].budget&&homes[i]*2==games[i],"Annual schedules: team budget/home balance differs");
    }
    void Verify(bool imported=true){
        Require(!imported||(allocations.empty()&&loaded.size()==31&&loaded==freed),"Annual schedules: native CSV lifetime incomplete");
        const auto collection=kernel::R32(0x6DC5A0);Require(collection&&kernel::R32(collection+12)==309,"Annual schedules: native collection differs");const auto keys=kernel::R32(collection+16);
        using Row=std::tuple<unsigned,int32_t,unsigned,unsigned,unsigned>;std::multiset<Row> expected,actual;auto teams=a::authored::Teams();
        for(const auto& g:fixtures)expected.emplace(g.key?phase9::RegularSlot(g.key):15,g.day,g.minute,a::authored::kEngine.at(teams[g.home].id),a::authored::kEngine.at(teams[g.away].id));
        for(const auto& [name,p]:payloads){(void)name;auto table=kernel::R32(manager+12+4*p.slot);Require(table&&kernel::R32(table+36)==p.count,"Annual schedules: native table count differs");auto data=kernel::R32(table+40);int32_t first=kernel::R32(table+20);
            for(unsigned i=0;i<p.count;++i){auto row=data+20*i,word=kernel::R32(row);auto home=expansion::SlotAt(kernel::g_base,row+4),away=expansion::SlotAt(kernel::g_base,row+12);if(!(home<309&&away<309&&home!=away))LOGE("annual schedules: invalid %s row %u sides %08X %08X slots %u %u",name.c_str(),i,kernel::R32(row+4),kernel::R32(row+12),home,away);Require(home<309&&away<309&&home!=away,"Annual schedules: native 9-bit participant differs");actual.emplace(p.slot,first+(word&511),(word>>9)&2047,kernel::R32(keys+4*home),kernel::R32(keys+4*away));}}
        Require(expected==actual,"Annual schedules: native imported fixtures differ");LOGR("annual schedules: %u verified 8588 games / 309 schools / 30 conferences / original 20-byte records",year);
        if(imported&&year>phase9::FirstYear()){expansion::DropUnmappedRosterHandles();phase9::QueueSeasonAutosave(year);}
    }
};
}

void phase9::ObserveScheduleToken(X86Context& c){if(!active)return;auto engine=kernel::R32(c.edi),key=kernel::R32(c.edi+4),rank=kernel::R32(c.esi+12);Require(expansion::SchoolActive(engine)&&key>=1&&key<=30&&rank>=1&&rank<=40&&active->tokens.emplace(engine,std::pair(key,kernel::ReadCString(c.esp+0x24,64))).second,"Annual schedules: invalid/duplicate native ranked token");}
uint32_t phase9::ScheduleAnchor(uint32_t original,uint32_t earliest,uint32_t year){if(!Enabled(Feature::AnnualSchedules))return original;Require(year>=2027&&year<=2056&&int32_t(earliest)>=a::JanuaryFirst(year)&&int32_t(earliest)<a::JanuaryFirst(year+1),"Annual schedules: stale fixture year");return earliest;}

void phase9::VerifySavedAnnual(unsigned year){
    // Exact initial-fixture equality is intentionally strict. Played seasons
    // can append runtime game rows; audit their exported records separately.
    X86Context ctx{};ctx.ecx=kernel::R32(0x6dc5a8);Import session(ctx,year);
    for(const auto& c:a::authored::Conferences())session.payloads.emplace(c.stem+".dat",Payload{c.regular});
    session.payloads.emplace("intrconf.dat",Payload{15});
    for(const auto& g:session.fixtures){unsigned slot=g.key?RegularSlot(g.key):15;for(auto& [name,p]:session.payloads)if(p.slot==slot){++p.count;break;}}
    session.Verify(false);LOGR("annual schedules: cold-loaded native save fixture equality passed");
}

X86_EXTERN_FUNC(__imp__sub_00500E70);
X86_FUNC(sub_00500E70){if(!phase9::Enabled(phase9::Feature::AnnualSchedules))return __imp__sub_00500E70(ctx,base);try{Import session(ctx,Arg(ctx,0)&0xffffu);__imp__sub_00500E70(ctx,base);session.Verify();phase9::ProbeCompetition(ctx);game::WorkshopDynastyCreated(session.year);}catch(const std::exception& error){LOGE("annual schedules: %s",error.what());throw;}}
X86_EXTERN_FUNC(__imp__sub_002726B0);
X86_FUNC(sub_002726B0){
    if(active&&Arg(ctx,1)==0x100&&Arg(ctx,2)==5){if(active->payloads.empty())active->Build();auto name=kernel::ReadCString(Arg(ctx,0),64);auto found=active->payloads.find(name);
        if(found!=active->payloads.end()){Require(active->loaded.insert(name).second,"Annual schedules: repeated CSV load");auto table=kernel::R32(active->manager+12+4*found->second.slot);Require(table!=0,"Annual schedules: native regular table was not constructed");LOGR("annual schedules: load %s slot %u table %08X capacity %u games %u",name.c_str(),found->second.slot,table,kernel::R32(table+28),found->second.count);auto& csv=found->second.csv;auto at=mem::AllocSystem(uint32_t(csv.size()+1));Require(at!=0,"Annual schedules: CSV allocation failed");std::memcpy(kernel::HostPtr(at),csv.c_str(),csv.size()+1);kernel::W32(Arg(ctx,3),uint32_t(csv.size()));active->allocations.emplace(at,name);ctx.eax=at;ctx.esp+=4;return;}}
    __imp__sub_002726B0(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_002670E0);
X86_FUNC(sub_002670E0){if(active){auto found=active->allocations.find(Arg(ctx,0));if(found!=active->allocations.end()){Require(Arg(ctx,1)==5,"Annual schedules: wrong native CSV heap");active->freed.insert(found->second);mem::FreeSystem(found->first);active->allocations.erase(found);ctx.esp+=4;return;}}__imp__sub_002670E0(ctx,base);}
