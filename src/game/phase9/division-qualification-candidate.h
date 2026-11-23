// Adapted kit algorithm: original packed membership and 20-byte/9-bit game records.
#pragma once
#include "competition-bridge.h"
#include "competition-qualification-model.h"
#include "division-qualification-config.h"
#include <cstring>

namespace mvp::division_qualification {
inline uint32_t Read(const uint8_t* b,uint32_t p){uint32_t v;std::memcpy(&v,b+p,4);return v;}
inline uint16_t Short(const uint8_t* b,uint32_t p){uint16_t v;std::memcpy(&v,b+p,2);return v;}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x005000E0,why);}
inline uint32_t Row(X86Context& c,uint8_t* b,uint32_t roster,uint32_t id){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 c.esp-=8;Put(b,c.esp,0x005000E0);Put(b,c.esp+4,id);c.ecx=roster;x86_icall(c,b,0x00078550u);
 Require(c,c.esp==saved.esp&&c.eax,"Divisional qualification: original roster lookup ABI/null row");return c.eax;
}
inline uint32_t SeedTeam(X86Context& c,const uint8_t* b,uint32_t root,unsigned seed){
 Require(c,Read(b,root)==0x63AD30||Read(b,root)==0x63ACD0,"Divisional qualification: unsupported host seed owner");const auto child=Read(b,root+0x38);
 Require(c,child&&Read(b,child+0x64)==root&&seed<Read(b,child+0x5C),"Divisional qualification: host seed bounds");const auto seeds=Read(b,child+0x68),team=Read(b,seeds+16*seed+4);
 Require(c,seeds&&team>0&&team<=310&&team!=102&&Read(b,seeds+16*seed+8)==seed+1,"Divisional qualification: absent host seed");return team;
}
inline int8_t Site(X86Context& c,const uint8_t* b,uint32_t root,unsigned index,uint32_t a,uint32_t z,const Venue& venue){
 uint32_t host=venue.host;
 if(venue.mode==1)host=a; // higher original seed owns the park for this game/series
 else if(venue.mode==2)host=SeedTeam(c,b,root,0);
 else if(venue.mode==3){ // Southland: seeded campus groups, then higher finalist
  const auto child=Read(b,root+0x38),dd=Read(b,child+0x6C);Require(c,child&&Read(b,child+0x60)==17&&dd,"Divisional qualification: Southland descriptor shape");unsigned ordinal=17;
  for(unsigned i=0;i<17;++i)if(Read(b,dd+12*i+4)==index){Require(c,ordinal==17,"Divisional qualification: duplicate Southland descriptor");ordinal=i;}
  Require(c,ordinal<17,"Divisional qualification: Southland result lacks descriptor");host=ordinal>=14?a:SeedTeam(c,b,root,ordinal<4?ordinal/2:ordinal%2);
 }
 Require(c,venue.mode<=3,"Divisional qualification: unknown venue rule");return host==a?1:host==z?-1:0;
}
inline bool ReadyCutoff(X86Context& c,const uint8_t* b,uint32_t world,const Selection& selection,int32_t& cutoff){
 const auto manager=Read(b,world+16),regular=Read(b,manager+12+4*selection.regular);Require(c,manager&&regular&&Read(b,regular+8)==selection.regular,"Divisional qualification: missing regular owner");
 const auto count=Read(b,regular+36),capacity=Read(b,regular+28),data=Read(b,regular+40),first=Read(b,regular+20);Require(c,count<=capacity&&capacity<=16384&&(!count||data),"Divisional qualification: regular bounds");if(!count)return false;cutoff=phase9::QualificationCutoff(b,selection.key);
 for(unsigned i=0;i<count;++i){const auto word=Read(b,data+20*i),status=(word>>20)&15;Require(c,status<=5,"Divisional qualification: regular status");if(status==0||status==1)return false;if(status==3)cutoff=std::max(cutoff,int32_t(first+(word&511)+1));}
 // Completion notifications arrive during result processing. Wait until every
 // earlier-dated league result is final, including conference postseason, so
 // seeding cannot depend on the order in which same-day games were simulated.
 for(unsigned slot=1;slot<72;++slot){const auto root=Read(b,manager+12+4*slot);if(!root)continue;const auto n=Read(b,root+36),limit=Read(b,root+28),rows=Read(b,root+40),start=Read(b,root+20);Require(c,n<=limit&&limit<=16384&&(!n||rows),"Divisional qualification: snapshot readiness bounds");
  for(unsigned i=0;i<n;++i){const auto word=Read(b,rows+20*i),status=(word>>20)&15;Require(c,status<=5,"Divisional qualification: snapshot status");if((status==0||status==1)&&int64_t(start+(word&511))<cutoff)return false;}
 }
 return true;
}
inline qualification_model::Snapshot Extract(X86Context& c,uint8_t* b,uint32_t world,int32_t cutoff,uint32_t completed_regular){
 const auto manager=Read(b,world+16),roster=Read(b,world+12),collection=Read(b,0x6DC5A0),keys=Read(b,collection+16);
 Require(c,manager&&roster&&collection&&keys&&Read(b,collection+12)==309&&Read(b,0x6DC59C)==manager,"Divisional qualification: unexpected world population/manager");
 bool seen[311]{};for(unsigned i=0;i<309;++i){const auto id=Read(b,keys+4*i);Require(c,id>0&&id<=310&&id!=102&&!seen[id],"Divisional qualification: duplicate/invalid collection identity");seen[id]=true;}
 std::vector<qualification_model::Team> teams;teams.reserve(309);
 for(const auto& t:kTeams){Require(c,seen[t.id],"Divisional qualification: missing locked identity");const auto row=Row(c,b,roster,t.id);Require(c,((Read(b,row+0xA4)>>6)&31u)==t.key,"Divisional qualification: changed/inactive membership");teams.push_back({t.id,t.key,t.division,t.canonical,t.eligible});}
 Require(c,teams.size()==309,"Divisional qualification: generated population differs");std::vector<qualification_model::Game> games;games.reserve(10000);bool completed_owner=false;
 for(unsigned slot=1;slot<72;++slot){const auto root=Read(b,manager+12+4*slot);if(!root){Require(c,slot!=completed_regular,"Divisional qualification: missing regular owner");continue;}
  const bool regular=(slot<=15)||(slot>=37&&slot<=52),national=slot>=27&&slot<=36;
  const auto count=Read(b,root+0x24),capacity=Read(b,root+0x1C),data=Read(b,root+0x28),first=Read(b,root+0x14),key=Read(b,root+0x10);
  Require(c,Read(b,root+8)==slot&&count<=capacity&&capacity<=16384&&(!count||data),"Divisional qualification: invalid competition allocation/owner");
  const Venue* venue=nullptr;if(!regular&&!national){for(const auto& v:kVenues)if(v.slot==slot)venue=&v;Require(c,venue&&venue->key==key,"Divisional qualification: missing authored venue");}
  for(unsigned i=0;i<count;++i){const auto row=data+20*i,word=Read(b,row),status=(word>>20)&15,day=first+(word&511);
   Require(c,status<=5,"Divisional qualification: unrecognized game status");
   if(slot==completed_regular){Require(c,status!=0&&status!=1,"Divisional qualification: own regular season still pending");completed_owner=true;}
   if(status!=3||int64_t(day)>=cutoff)continue;
   Require(c,!national,"Divisional qualification: unexpected national result before snapshot");
   const auto pa=expansion::SlotAt(b,row+4),pz=expansion::SlotAt(b,row+12);Require(c,pa<309&&pz<309&&pa!=pz,"Divisional qualification: completed participant bounds");const auto a=Read(b,keys+4*pa),z=Read(b,keys+4*pz);
   games.push_back({a,z,b[row+5],b[row+13],regular&&slot!=15?key:0,int32_t(day),regular?int8_t(1):Site(c,b,root,i,a,z,*venue)});
  }
 }
 Require(c,completed_owner,"Divisional qualification: empty completed conference");
 return qualification_model::Snapshot(std::move(teams),std::move(games),cutoff);
}
inline std::vector<uint32_t> Field(X86Context& c,uint8_t* b,uint32_t world,uint32_t key){
 const Selection* selection=nullptr;for(const auto& p:kSelections)if(p.key==key)selection=&p;Require(c,selection,"Divisional qualification: unsupported profile");
 int32_t cutoff=0;Require(c,ReadyCutoff(c,b,world,*selection,cutoff),"Divisional qualification: snapshot has unfinished earlier games");
 try{auto snapshot=Extract(c,b,world,cutoff,selection->regular);qualification_model::Qualifier q(snapshot,phase9::SeasonYear(b),selection->canonical,key,key==30?qualification_model::TiePolicy::AsunRpi:qualification_model::TiePolicy::CaaCommon);return q.Field(key==30?3:2,key==30?0:2);}
 catch(const std::exception& e){x86_trap(c,0x005000E0,e.what());throw;}
}
}
