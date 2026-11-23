// Adapted to original records, packed membership, and one native combined group.
#include "specific-standings-candidate.h"
#include "specific-standings-config.h"
#include "specific-standings-model.h"
#include "division-qualification-candidate.h"
#include "annual-league-config.h"
#include "standings-initialization.h"
#include <cstring>

namespace mvp::specific_standings_candidate {
namespace {
using division_qualification::Read;
using division_qualification::Short;
using division_qualification::Put;
using qualification_model::Require;
struct Restore {X86Context& c;X86Context saved;explicit Restore(X86Context& x):c(x),saved(x){}~Restore(){c=saved;}};
const Policy* Find(unsigned key){for(const auto& p:kPolicies)if(p.key==key)return &p;return nullptr;}
bool Dated(unsigned key){return key==10||key==22||key==23||key==30;}
unsigned Year(const uint8_t* b,uint32_t manager){
 const auto root=Read(b,manager+16);Require(root&&Read(b,root+8)==1,"Specific standings: missing annual anchor");
 const auto first=int32_t(Read(b,root+20));Require(first>=annual_schedule::JanuaryFirst(2027)&&first<annual_schedule::JanuaryFirst(9999),"Specific standings: year bounds");
 unsigned year=2027+(first-annual_schedule::JanuaryFirst(2027))/366;while(annual_schedule::JanuaryFirst(year+1)<=first)++year;
 Require(first>=annual_schedule::JanuaryFirst(year)&&first<annual_schedule::JanuaryFirst(year+1),"Specific standings: season base differs");return year;
}
annual_schedule::Window Window(unsigned year,unsigned key){
 for(const auto& conf:annual_schedule::authored::Conferences())if(conf.rule.key==key)return annual_schedule::SeasonWindow(year,conf.event,annual_schedule::authored::FirstDate(year));
 throw std::runtime_error("Specific standings: missing annual policy");
}
struct Table {uint32_t root,slot,key,used,data;int32_t first;};
Table Get(const uint8_t* b,uint32_t manager,unsigned slot){
 const auto root=Read(b,manager+12+4*slot);Require(root&&Read(b,root+8)==slot,"Specific standings: table identity");
 const auto used=Read(b,root+36),limit=Read(b,root+28),data=Read(b,root+40);
 Require(used<=limit&&limit<=16384&&(!used||data)&&uint64_t(data)+20*used<=0x100000000ull,"Specific standings: table bounds");
 return {root,slot,Read(b,root+16),used,data,int32_t(Read(b,root+20))};
}
struct Dates {unsigned year;int32_t rpi,outside,start;bool complete;};
Dates Cutoffs(const uint8_t* b,uint32_t manager,const Policy& p){
 const auto year=Year(b,manager);const auto window=Window(year,p.key);const auto own=Get(b,manager,p.regular);
 Require(own.key==p.key&&own.first>=annual_schedule::JanuaryFirst(year)&&own.first<annual_schedule::JanuaryFirst(year+1),"Specific standings: regular identity/year");
 int32_t last=window.last;bool complete=own.used>0;
 for(unsigned i=0;i<own.used;++i){const auto word=Read(b,own.data+20*i),status=(word>>20)&15;Require(status<=5,"Specific standings: unknown regular status");
  if(status<2)complete=false;if(status==3)last=std::max(last,own.first+int32_t(word&511));}
 const auto rpi=p.key==10?window.last+7-annual_schedule::Weekday(window.last):last+1;
 const auto outside=window.event_start-(annual_schedule::Weekday(window.event_start)?annual_schedule::Weekday(window.event_start):7);
 return {year,rpi,outside,window.event_start,complete};
}
bool Ready(const uint8_t* b,uint32_t manager,const Policy& p){
 const auto dates=Cutoffs(b,manager,p);if(!dates.complete)return false;
 const auto cutoff=p.key==10?std::max(dates.rpi,dates.outside):dates.rpi;
 Require(cutoff<=dates.start,"Specific standings: makeup/snapshot extends past tournament start");
 for(unsigned slot=1;slot<72;++slot){if(!Read(b,manager+12+4*slot))continue;const auto table=Get(b,manager,slot);
  for(unsigned i=0;i<table.used;++i){const auto word=Read(b,table.data+20*i),status=(word>>20)&15;Require(status<=5,"Specific standings: unknown readiness status");
   if(status<2&&int64_t(table.first)+int32_t(word&511)<cutoff)return false;}}
 return true;
}
struct Data {std::vector<qualification_model::Team> teams;std::vector<specific_standings::Result> games;Dates dates;};
Data Extract(X86Context& c,uint8_t* b,uint32_t db,uint32_t manager,const Policy& p){
 Data data;data.dates=Cutoffs(b,manager,p);const bool full=Dated(p.key)||p.divided;
 const auto collection=Read(b,0x6DC5A0),keys=collection?Read(b,collection+16):0;
 Require(collection&&keys&&Read(b,collection+12)==309,"Specific standings: active collection shape");
 std::array<bool,311> seen{};for(unsigned i=0;i<309;++i){const auto id=Read(b,keys+4*i);Require(id>0&&id<=310&&id!=102&&!seen[id],"Specific standings: collection identity");seen[id]=true;}
 for(const auto& t:division_qualification::kTeams){if(!full&&t.key!=p.key)continue;Require(seen[t.id],"Specific standings: locked identity absent");const auto row=division_qualification::Row(c,b,db,t.id);
  Require(((Read(b,row+0xA4)>>6)&31u)==t.key,"Specific standings: membership changed");
  // Original packed conference bits occupy bit10; canonical division metadata is authoritative.
  data.teams.push_back({t.id,t.key,t.division,t.canonical,t.eligible});}
 const int32_t snapshot=std::max(data.dates.rpi,data.dates.outside);
 for(unsigned slot=1;slot<72;++slot){if((!full&&slot!=p.regular)||!Read(b,manager+12+4*slot))continue;const auto table=Get(b,manager,slot);
  const bool regular=slot<=15||(slot>=37&&slot<=52),national=slot>=27&&slot<=36;
  const division_qualification::Venue* venue=nullptr;
  if(!regular&&!national){for(const auto& v:division_qualification::kVenues)if(v.slot==slot)venue=&v;Require(venue&&venue->key==table.key,"Specific standings: tournament venue missing");}
  for(unsigned i=0;i<table.used;++i){const auto row=table.data+20*i,word=Read(b,row),status=(word>>20)&15;Require(status<=5,"Specific standings: unknown game status");if(status!=3)continue;
   const auto day=table.first+int32_t(word&511);if(slot!=p.regular&&day>=snapshot)continue;
   Require(day>=annual_schedule::JanuaryFirst(data.dates.year)&&day<annual_schedule::JanuaryFirst(data.dates.year+1)&&!national,"Specific standings: result year/snapshot differs");
   const auto pa=expansion::SlotAt(b,row+4),pz=expansion::SlotAt(b,row+12);Require(pa<309&&pz<309&&pa!=pz,"Specific standings: participant bounds");const auto a=Read(b,keys+4*pa),z=Read(b,keys+4*pz);
   data.games.push_back({a,z,b[row+5],b[row+13],regular&&slot!=15?table.key:0,day,regular?int8_t(1):division_qualification::Site(c,b,table.root,i,a,z,*venue),(word>>9)&2047,slot*16384+i});
  }
 }
 return data;
}
std::vector<size_t> Asun(const qualification_model::Snapshot& s,const qualification_model::Qualifier& q,std::vector<size_t> tied){
 if(tied.size()<2)return tied;std::vector<qualification_model::Record> metric;for(auto t:tied)metric.push_back(s.Against(t,tied));
 bool equal=true;for(const auto r:metric)equal=equal&&r.games&&r.games==metric.front().games;
 if(equal){auto sorted=tied;std::sort(sorted.begin(),sorted.end(),[&](auto a,auto z){return qualification_model::Compare(s.Against(a,tied),s.Against(z,tied))>0;});std::vector<std::vector<size_t>> groups;
  for(auto t:sorted){if(groups.empty()||qualification_model::Compare(s.Against(t,tied),s.Against(groups.back().front(),tied)))groups.push_back({});groups.back().push_back(t);}
  if(groups.size()>1){std::vector<size_t> out;for(const auto& g:groups){auto v=Asun(s,q,g);out.insert(out.end(),v.begin(),v.end());}return out;}}
 bool populated=true;for(auto t:tied)populated=populated&&s.total[t].games>0;if(populated)return q.RpiOrder(tied);
 // Explicit game-free display fallback only. Actual qualification retains the
 // unchanged required-snapshot guard in division_qualification::Field.
 const auto pool=tied;std::sort(tied.begin(),tied.end(),[&](auto a,auto z){return qualification_model::Lot(s,pool,a,q.season,q.conference,q.stage)<qualification_model::Lot(s,pool,z,q.season,q.conference,q.stage);});return tied;
}
std::vector<uint32_t> DivisionOrder(const Data& data,const Policy& p,int division){
 std::vector<qualification_model::Game> games;for(const auto& g:data.games)games.push_back({g.a,g.b,g.ra,g.rb,g.key,g.day,g.site});
 qualification_model::Snapshot s(data.teams,std::move(games),data.dates.rpi);
 qualification_model::Qualifier q(s,data.dates.year,p.canonical,p.key,p.key==30?qualification_model::TiePolicy::AsunRpi:qualification_model::TiePolicy::CaaCommon);
 std::vector<size_t> selected;for(auto t:q.members)if(division<0||int(s.teams[t].division)==division)selected.push_back(t);
 std::vector<size_t> ordered;if(p.key==19)ordered=q.Standings(selected);else{
  std::sort(selected.begin(),selected.end(),[&](auto a,auto z){return qualification_model::Compare(s.conference[a],s.conference[z])>0;});
  for(size_t i=0;i<selected.size();){size_t end=i+1;while(end<selected.size()&&!qualification_model::Compare(s.conference[selected[i]],s.conference[selected[end]]))++end;
   auto v=Asun(s,q,{selected.begin()+i,selected.begin()+end});ordered.insert(ordered.end(),v.begin(),v.end());i=end;}}
 std::vector<uint32_t> out;for(auto t:ordered)out.push_back(s.teams[t].id);return out;
}
void Reorder(X86Context& c,uint8_t* b,uint32_t cache){
 Require(cache,"Specific standings: null cache");const auto* p=Find(Read(b,cache));if(!p)return;
 const auto world=Read(b,0x6DC5A8),db=Read(b,0x6C6540),manager=Read(b,0x6DC59C);if(!world||!db||!manager)return;
 const auto owner=Read(b,world+20);if(!owner)return;const auto count=Read(b,owner+28),map=Read(b,owner+32);
 Require(count>p->key&&count<=64&&map,"Specific standings: cache map bounds");if(Read(b,map+4*p->key)!=cache)return;
 Require(Read(b,owner)==0x63B83C&&Read(b,owner+12)==db&&Read(b,owner+16)==manager&&Read(b,owner+20)==world,"Specific standings: current owner differs");
 const auto group_count=Read(b,cache+8),groups=Read(b,cache+12),combined=Read(b,cache+16);
 Require(group_count==1u&&groups&&combined,"Specific standings: group shape");
 for(unsigned i=0;i<group_count;++i){const auto group=Read(b,groups+4*i);Require(group,"Specific standings: null group");
  if(Read(b,group+16)!=Read(b,db+76)||Read(b,group+20)!=Read(b,db+80))return;}
 Require(Read(b,db+76)&&Read(b,db+80),"Specific standings: absent statistics/ranks");
 if(standings_initialization::AwaitingFirstSchedule(b,manager))return;
 auto data=Extract(c,b,db,manager,*p);std::vector<uint32_t> overall,qualifying;
 if(p->divided)overall=DivisionOrder(data,*p,-1);
 else{specific_standings::Resolver q(data.teams,data.games,p->key,data.dates.year,p->canonical,data.dates.rpi,data.dates.outside,true);overall=q.Order();qualifying=p->key==26?q.Order(p->field):overall;}
 if(qualifying.empty())qualifying=overall;
 struct Write {uint32_t at;std::vector<uint8_t> bytes;};std::vector<Write> writes;
 auto destination=[&](uint32_t vector,unsigned stride,unsigned offset,const std::vector<uint32_t>& order){
  const auto start=Read(b,vector+4),end=Read(b,vector+8),limit=Read(b,vector+12);const auto n=order.size();
  Require(n>0&&n<=40&&start&&end>=start&&limit>=end&&uint64_t(start)+stride*n==end&&limit-start<=40*stride,"Specific standings: destination bounds");
  for(const auto& w:writes)Require(uint64_t(start)+stride*n<=w.at||uint64_t(w.at)+w.bytes.size()<=start,"Specific standings: destination overlap");
  std::array<int,512> offsets;offsets.fill(-1);for(unsigned i=0;i<n;++i){const auto id=Read(b,start+stride*i+offset);Require(id<512&&offsets[id]<0,"Specific standings: duplicate row");offsets[id]=i;}
  Write w{start,std::vector<uint8_t>(stride*n)};for(unsigned i=0;i<n;++i){const auto id=order[i];Require(id<512&&offsets[id]>=0,"Specific standings: missing/foreign row");std::memcpy(w.bytes.data()+stride*i,b+start+stride*offsets[id],stride);}writes.push_back(std::move(w));
 };
 for(unsigned i=0;i<group_count;++i)destination(Read(b,groups+4*i),20,4,i?DivisionOrder(data,*p,int(i)-1):overall);
 destination(combined,4,0,qualifying);
 for(const auto& w:writes)std::memcpy(b+w.at,w.bytes.data(),w.bytes.size());
}
void Clear(const uint8_t* b,uint8_t* writable,uint32_t owner){
 const auto world=Read(b,0x6DC5A8);if(!world||owner!=Read(b,world+20))return;const auto map=Read(b,owner+32),count=Read(b,owner+28);
 Require(map&&count<=64&&count>30,"Specific standings: invalidation map");
 for(unsigned key:{10u,22u,23u,30u}){const auto cache=Read(b,map+4*key);Require(cache&&Read(b,cache)==key,"Specific standings: invalidation identity");writable[cache+4]=0;}
}
}
void Publish(X86Context& c,uint8_t* b,uint32_t cache){Restore restore(c);try{Reorder(c,b,cache);}catch(const std::exception& e){x86_trap(c,0x004FCE64,e.what());throw;}}
void Invalidate(X86Context& c,uint8_t* b,uint32_t owner){Restore restore(c);try{Clear(b,b,owner);}catch(const std::exception& e){x86_trap(c,0x004E4220,e.what());throw;}}
bool Defer(X86Context& c,uint8_t* b){Restore restore(c);try{
 const auto event=Read(b,c.esp+4);if(!event)return false;const auto regular=Read(b,event+8);const Policy* p=nullptr;
 for(const auto& candidate:kPolicies)if(candidate.regular==regular&&Dated(candidate.key)&&!candidate.divided)p=&candidate;if(!p)return false;
 const auto world=c.ecx,manager=Read(b,world+16);Require(world==Read(b,0x6DC5A8)&&manager==Read(b,0x6DC59C),"Specific standings: seed world differs");
 Clear(b,b,Read(b,world+20));return !Ready(b,manager,*p);
 }catch(const std::exception& e){x86_trap(c,0x005000E0,e.what());throw;}}
void Retry(X86Context& c,uint8_t* b,uint32_t world){Restore restore(c);try{
 Require(world&&world==Read(b,0x6DC5A8),"Specific standings: retry world differs");const auto manager=Read(b,world+16);Require(manager==Read(b,0x6DC59C),"Specific standings: retry manager differs");Clear(b,b,Read(b,world+20));
 for(const auto& p:kPolicies){if(!Dated(p.key)||p.divided)continue;const auto tournament=Get(b,manager,p.slot);Require(tournament.key==p.key,"Specific standings: tournament identity");if(tournament.used||!Ready(b,manager,p))continue;
  const auto saved=c;c.esp-=32;const auto event=c.esp+16;Put(b,event,0);Put(b,event+4,0xA9);Put(b,event+8,p.regular);c.esp-=8;Put(b,c.esp,0x004FF0E6);Put(b,c.esp+4,event);c.ecx=world;x86_icall(c,b,0x005000E0u);
  Require(c.esp==saved.esp-32,"Specific standings: seed retry ABI");c=saved;
 }
 }catch(const std::exception& e){x86_trap(c,0x004FF0E6,e.what());throw;}}
}
