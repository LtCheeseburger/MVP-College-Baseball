// Adapted to original records, packed membership, and one native combined group.
#include "common-standings-candidate.h"
#include "competition-qualification-model.h"
#include "division-qualification-config.h"
#include "annual-schedule-calendar.h"
#include "standings-initialization.h"
#include <cstring>

namespace mvp::common_standings_candidate {
namespace {
struct Policy {uint32_t key; const char* canonical;};
// The native proof checks this exact set and all policy parameters against the
// locked authoring profiles, retaining their source/default distinctions.
constexpr Policy policies[] = {
    {15,"ncaa-conf:819"},{18,"ncaa-conf:826"},{3,"ncaa-conf:827"},
    {20,"ncaa-conf:845"},{25,"ncaa-conf:881"},{7,"ncaa-conf:884"},
    {5,"ncaa-conf:904"},{9,"ncaa-conf:905"},{27,"ncaa-conf:912"},
    {28,"ncaa-conf:914"},{14,"ncaa-conf:922"},{13,"ncaa-conf:923"},
    {8,"ncaa-conf:5486"},{6,"ncaa-conf:24312"},{2,"ncaa-conf:30184"}
};
uint32_t Word(const uint8_t* b,uint32_t p) {uint32_t v;std::memcpy(&v,b+p,4);return v;}
uint16_t Short(const uint8_t* b,uint32_t p) {uint16_t v;std::memcpy(&v,b+p,2);return v;}
void Put(uint8_t* b,uint32_t p,uint32_t v) {std::memcpy(b+p,&v,4);}
void Check(bool ok,const char* why) {qualification_model::Require(ok,why);}
uint32_t TeamRow(X86Context& c,uint8_t* b,uint32_t db,uint32_t id) {
    const auto saved=c;
    struct Restore {X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
    c.esp-=8;Put(b,c.esp,0x004FCE64);Put(b,c.esp+4,id);c.ecx=db;
    x86_icall(c,b,0x00078550u);
    Check(c.esp==saved.esp&&c.eax,"Shared standings: roster lookup ABI/null row");
    return c.eax;
}
void Reorder(X86Context& c,uint8_t* b,uint32_t cache) {
    Check(cache!=0,"Shared standings: null cache");
    const auto key=Word(b,cache);const Policy* policy=nullptr;
    for(const auto& p:policies)if(p.key==key)policy=&p;
    if(!policy)return;
    const auto controller=Word(b,0x6DC5A8),db=Word(b,0x6C6540),schedule=Word(b,0x6DC59C);
    // Foreign/historical caches keep their original semantics.
    if(!controller||!db||!schedule)return;
    const auto owner=Word(b,controller+0x14);if(!owner)return;
    const auto count=Word(b,owner+0x1C),map=Word(b,owner+0x20);
    Check(count>key&&count<=64&&map,"Shared standings: current cache map bounds");
    if(Word(b,map+key*4)!=cache)return;
    Check(Word(b,owner)==0x63B83C&&Word(b,owner+12)==db&&
          Word(b,owner+16)==schedule&&Word(b,owner+20)==controller,
          "Shared standings: current manager ownership");
    const auto groups=Word(b,cache+12),combined=Word(b,cache+16);
    Check(Word(b,cache+8)==1&&groups&&combined,"Shared standings: expected one undivided group");
    const auto group=Word(b,groups);Check(group!=0,"Shared standings: null group");
    if(Word(b,group+16)!=Word(b,db+0x4C)||Word(b,group+20)!=Word(b,db+0x50))return;
    Check(Word(b,db+0x4C)&&Word(b,db+0x50),"Shared standings: absent current statistics/ranks");
    if(standings_initialization::AwaitingFirstSchedule(b,schedule))return;

    const auto collection=Word(b,0x6DC5A0);Check(collection!=0,"Shared standings: absent collection");
    const auto keys=Word(b,collection+16);
    Check(Word(b,collection+12)==309&&keys,"Shared standings: active collection shape");
    std::array<bool,311> active{};
    for(unsigned i=0;i<309;++i){const auto id=Word(b,keys+4*i);
        Check(id>0&&id<=310&&id!=102&&!active[id],"Shared standings: invalid/duplicate collection identity");active[id]=true;}
    std::vector<qualification_model::Team> teams;
    for(const auto& t:division_qualification::kTeams)if(t.key==key){
        Check(t.id<active.size()&&active[t.id]&&t.division==0,"Shared standings: missing locked member/division");
        const auto row=TeamRow(c,b,db,t.id);
        Check(((Word(b,row+0xA4)>>6)&31u)==key,"Shared standings: changed membership");
        teams.push_back({t.id,t.key,t.division,t.canonical,t.eligible});
    }
    const auto n=uint32_t(teams.size());Check(n>0&&n<=40,"Shared standings: member capacity");
    const auto rows=Word(b,group+4),end=Word(b,group+8),limit=Word(b,group+12);
    const auto ids=Word(b,combined+4),ids_end=Word(b,combined+8),ids_limit=Word(b,combined+12);
    Check(rows&&end>=rows&&limit>=end&&end-rows==20*n&&limit-rows<=40*20&&
          ids&&ids_end>=ids&&ids_limit>=ids_end&&ids_end-ids==4*n&&ids_limit-ids<=40*4,
          "Shared standings: destination vector bounds");
    Check(uint64_t(rows)+20*n<=ids||uint64_t(ids)+4*n<=rows,"Shared standings: overlapping destinations");
    std::array<int,512> offsets;offsets.fill(-1);std::array<bool,512> combined_seen{};
    for(unsigned i=0;i<n;++i){const auto id=Word(b,rows+20*i+4),other=Word(b,ids+4*i);
        Check(id<512&&offsets[id]<0&&other<512&&!combined_seen[other],"Shared standings: duplicate cache row");
        offsets[id]=int(i);combined_seen[other]=true;}
    for(const auto& t:teams)Check(offsets[t.id]>=0&&combined_seen[t.id],"Shared standings: foreign/missing cache row");

    const auto slot=key<=14?key:key+22,root=Word(b,schedule+12+4*slot),anchor=Word(b,schedule+16);
    Check(root&&anchor&&Word(b,root+8)==slot&&Word(b,root+16)==key&&Word(b,anchor+8)==1,
          "Shared standings: regular table identity");
    const int32_t first=int32_t(Word(b,root+20));
    Check(first==int32_t(Word(b,anchor+20))&&first>=annual_schedule::JanuaryFirst(2027)&&
          first<annual_schedule::JanuaryFirst(9999),"Shared standings: regular season base");
    int32_t year=2027+(first-annual_schedule::JanuaryFirst(2027))/366;
    while(annual_schedule::JanuaryFirst(year+1)<=first)++year;
    Check(first>=annual_schedule::JanuaryFirst(year)&&first<annual_schedule::JanuaryFirst(year+1),"Shared standings: season base differs");
    const auto used=Word(b,root+36),capacity=Word(b,root+28),data=Word(b,root+40);
    Check(used<=capacity&&capacity<=16384&&(!used||data),"Shared standings: game table bounds");
    std::vector<qualification_model::Game> games;games.reserve(used);
    for(unsigned i=0;i<used;++i){const auto row=data+20*i,word=Word(b,row),status=(word>>20)&15;
        Check(status<=5,"Shared standings: unknown result status");if(status!=3)continue;
        const auto a=expansion::SlotAt(b,row+4),z=expansion::SlotAt(b,row+12);
        Check(a<309&&z<309&&a!=z,"Shared standings: result participant position");
        const auto day=first+int32_t(word&511);
        Check(day<annual_schedule::JanuaryFirst(year+1),"Shared standings: result year differs");
        games.push_back({Word(b,keys+4*a),Word(b,keys+4*z),b[row+5],b[row+13],key,day,1});
    }
    // Common-policy criteria use conference results only. The native proof
    // compares this bounded extraction with the independent full-league audit.
    qualification_model::Snapshot snapshot(std::move(teams),std::move(games),annual_schedule::JanuaryFirst(year+1));
    qualification_model::Qualifier q(snapshot,year,policy->canonical,key,qualification_model::TiePolicy::CaaCommon);
    const auto order=q.Standings(q.members);Check(order.size()==n,"Shared standings: incomplete order");
    std::vector<uint8_t> reordered(20*n);std::vector<uint32_t> ordered_ids;ordered_ids.reserve(n);
    for(unsigned i=0;i<n;++i){const auto id=snapshot.teams[order[i]].id;ordered_ids.push_back(id);
        std::memcpy(reordered.data()+20*i,b+rows+20*offsets[id],20);}
    // All validation and allocations finish before either consumer is changed.
    std::memcpy(b+rows,reordered.data(),reordered.size());
    std::memcpy(b+ids,ordered_ids.data(),4*n);
}
}
void Publish(X86Context& c,uint8_t* b,uint32_t cache) {
    const auto saved=c;
    struct Restore {X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
    try {Reorder(c,b,cache);}
    catch(const std::exception& e){x86_trap(c,0x004FCE64,e.what());throw;}
}
}
