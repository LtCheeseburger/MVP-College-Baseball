// Adapted kit algorithm: original packed membership and 20-byte/9-bit game records.
#pragma once
#include "competition-bridge.h"

#include "division-postseason-config.h"
#include "mac-postseason-candidate.h"
#include <initializer_list>
#include "division-qualification-candidate.h"
// CAA DE6 and ASUN play-ins plus DE4 retain the original generic bracket class.
// The play-in winners enter the DE stage with zero losses; original seeds persist.
namespace mvp::division_postseason {
inline uint32_t Read(const uint8_t* b,uint32_t p){return game_record_candidate::Read(b,p);}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004EA880,why);}
using Fn=uint32_t;
inline uint32_t Call(X86Context& c,uint8_t* b,Fn fn,uint32_t owner,std::initializer_list<uint32_t> args){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 for(auto i=args.end();i!=args.begin();) {c.esp-=4;Put(b,c.esp,*--i);}c.esp-=4;Put(b,c.esp,0x004EA880);c.ecx=owner;x86_icall(c,b,fn);Require(c,c.esp==saved.esp,"Divisional postseason: original callee stack differs");return c.eax;
}
inline const Profile* Slot(uint32_t slot){for(const auto& p:kProfiles)if(p.slot==slot)return &p;return nullptr;}
inline uint32_t Capacity(uint32_t slot){const auto* p=Slot(slot);return p?p->capacity:mac_postseason::Capacity(slot);}
inline uint32_t Games(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->games:mac_postseason::Games(b,root);}
inline uint32_t Field(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->field:mac_postseason::Field(b,root);}
inline const Profile* Owner(const uint8_t* b,uint32_t root){if(!root||Read(b,root)!=0x63AD30)return nullptr;for(const auto& p:kProfiles)if(Read(b,root+8)==p.slot&&Read(b,root+0x10)==p.key)return &p;return nullptr;}
inline void RegisterAdded(X86Context& c,uint8_t* b){
 const auto manager=Read(b,c.esi+0x10);
 for(const auto& p:kProfiles){
  if(p.slot==22||p.slot==26)continue;
  Require(c,manager&&!Read(b,manager+12+p.slot*4),"Divisional postseason registration: occupied reserved slot");
  const auto saved=c;c.esp-=12;Put(b,c.esp,0x005000E0);Put(b,c.esp+4,60);Put(b,c.esp+8,Read(b,0x6DC598));x86_icall(c,b,0x00325F20u);
  Require(c,c.esp==saved.esp-8&&c.eax,"Divisional postseason registration: allocation ABI/failure");const auto root=c.eax;c=saved;
  Call(c,b,0x004F0B20u,root,{manager,p.slot});Put(b,root+0x10,p.key);Put(b,manager+12+p.slot*4,root);
 }
}
inline bool Route(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* profile=Owner(b,root);if(!profile)return false;const auto& p=*profile;
 const auto out=Read(b,c.esp+4),game=Read(b,c.esp+8),side=Read(b,c.esp+12),loser=Read(b,c.esp+16);
 Require(c,game<p.games&&side<2&&loser<2&&out,"Divisional postseason graph: invalid ordinal/side");
 uint32_t target=0xFFFFFFFF,which=2;
 bool route=true;
 if(game==p.games-2){const auto meta=Read(b,Read(b,child+0x6C)+12*game+8),winner_side=side^loser;route=((meta>>(16*winner_side+12))&15)!=0;}
 if(route){target=p.routes[game][loser][0];which=p.routes[game][loser][1];}
 Put(b,out,target);Put(b,out+4,which);c.eax=out;c.esp+=20;return true;
}
inline void HomeSeed(X86Context& c,uint8_t* b){
 const auto child=c.esi,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return;
 const auto descriptor=c.ebx,row=c.edx,meta=Read(b,descriptor+8),first=meta&255,second=(meta>>16)&255;
 if(first==255||second==255)return;
 Require(c,first<p->field&&second<p->field&&first!=second&&((Read(b,row)>>20)&15)<2,"Divisional postseason home assignment: invalid seed/result");
 const auto seeds=Read(b,child+0x68),a=Read(b,seeds+16*first+12)&0x7FFFFFFF,z=Read(b,seeds+16*second+12)&0x7FFFFFFF;
 Require(c,a>=1&&a<=p->field&&z>=1&&z<=p->field&&a!=z,"Divisional postseason home assignment: original seed differs");if(a<z)return;
 uint8_t side[8];std::memcpy(side,b+row+4,8);std::memcpy(b+row+4,b+row+12,8);std::memcpy(b+row+12,side,8);Put(b,descriptor+8,(meta<<16)|(meta>>16));
}
inline void SeedDescriptors(X86Context& c,uint8_t* b,uint32_t child,uint32_t source,uint32_t start){
 const auto root=Read(b,child+0x64);const auto* profile=Owner(b,root);Require(c,profile&&!Read(b,root+0x24)&&Read(b,child+0x5C)==profile->field&&Read(b,child+0x60)==profile->games,"Divisional postseason seeding: owner/count differs");
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};c.esp-=0x40;const auto stamp=c.esp+0x10,out=c.esp+0x20;
 const auto& p=*profile;
 for(unsigned i=0;i<p.field;++i)std::memcpy(b+Read(b,child+0x68)+16*i+4,b+source+16*i+4,12);
 for(unsigned i=0;i<p.games;++i){Put(b,stamp,0x5FFB60);Put(b,stamp+4,start+p.days[i]);Put(b,stamp+8,p.minutes[i]);Call(c,b,0x004E9F60u,child,{out,stamp,i});}
 bool swap=false;
 if(p.key==30){
  auto division=[&](unsigned seed){const auto id=Read(b,source+16*seed+4);for(const auto& team:division_qualification::kTeams)if(team.id==id)return team.division;Require(c,false,"ASUN pairing: unknown canonical member");return 0u;};
  swap=division(2)==division(5);Require(c,(division(3)==division(4))==swap,"ASUN pairing: division finish tiers are inconsistent");
 }
 for(unsigned i=0;i<p.games;++i)for(unsigned side=0;side<2;++side){auto seed=p.initial[i][side];if(i<2&&swap&&(seed==4||seed==5))seed=9-seed;if(seed!=0xFFFFFFFF)Call(c,b,0x004DFC60u,child,{i,side,seed,0,0});}

 Call(c,b,0x004E7DF0u,root,{});Call(c,b,0x004D2960u,root,{});
}
inline void SeedField(X86Context& c,uint8_t* b,uint32_t world,const Profile& p){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 const auto manager=Read(b,world+0x10),root=Read(b,manager+12+p.slot*4),roster=Read(b,world+0xC);
 Require(c,Owner(b,root)==&p&&!Read(b,root+0x24),"Divisional seeding: unexpected/nonempty owner");
 const auto ids=division_qualification::Field(c,b,world,p.key);Require(c,ids.size()==6,"Divisional seeding: six qualifiers required");
 c.esp-=0x100;const auto seeds=c.esp+0x20;
 for(unsigned i=0;i<6;++i){const auto team=ids[i];const auto row=Call(c,b,0x00078550u,roster,{team});Require(c,row&&((Read(b,row+0xA4)>>6)&31u)==p.key,"Divisional seeding: wrong/inactive conference member");
  Put(b,seeds+16*i,0x63A6E0);Put(b,seeds+16*i+4,team);Put(b,seeds+16*i+8,i+1);Put(b,seeds+16*i+12,(Read(b,row+0xA4)&0x80000000u)|(i+1));
 }
 SeedDescriptors(c,b,Read(b,root+0x38),seeds,phase9::TournamentStart(b,p.key));
}
inline void Retry(X86Context& c,uint8_t* b,uint32_t world){
 const auto manager=Read(b,world+16);Require(c,world&&manager&&manager==Read(b,0x6DC59C),"Divisional seeding retry: unexpected competition controller");
 for(const auto& p:kProfiles){const auto root=Read(b,manager+12+4*p.slot);Require(c,Owner(b,root)==&p,"Divisional seeding retry: missing authored tournament");if(Read(b,root+36))continue;
  const division_qualification::Selection* selection=nullptr;for(const auto& q:division_qualification::kSelections)if(q.key==p.key)selection=&q;Require(c,selection!=nullptr,"Divisional seeding retry: qualification configuration missing");
  int32_t cutoff=0;if(!division_qualification::ReadyCutoff(c,b,world,*selection,cutoff))continue;
  Require(c,cutoff<=int32_t(phase9::TournamentStart(b,p.key)),"Divisional seeding: makeup extends beyond tournament start; explicit weather policy required");SeedField(c,b,world,p);
 }
}
inline bool Seed(X86Context& c,uint8_t* b){
 const auto event=Read(b,c.esp+4);if(!event)return false;const auto regular=Read(b,event+8);bool owns=false;for(const auto& p:kProfiles)owns=owns||p.regular==regular;
 if(owns)Retry(c,b,c.ecx);return owns;
}
}
