// Adapted kit algorithm: original packed membership and 20-byte/9-bit game records.
#pragma once
#include "competition-bridge.h"

#include "bigten-postseason-config.h"
#include "staggered-postseason-candidate.h"
#include <initializer_list>
// Big Ten qualifiers provide four survivors to a fixed single-elimination field.
// Original seed metadata and game descriptors own all advancement state.
namespace mvp::bigten_postseason {
inline uint32_t Read(const uint8_t* b,uint32_t p){return game_record_candidate::Read(b,p);}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004EA880,why);}
using Fn=uint32_t;
inline uint32_t Call(X86Context& c,uint8_t* b,Fn fn,uint32_t owner,std::initializer_list<uint32_t> args){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 for(auto i=args.end();i!=args.begin();) {c.esp-=4;Put(b,c.esp,*--i);}c.esp-=4;Put(b,c.esp,0x004EA880);c.ecx=owner;x86_icall(c,b,fn);Require(c,c.esp==saved.esp,"Big Ten postseason: original callee stack differs");return c.eax;
}
inline const Profile* Slot(uint32_t slot){for(const auto& p:kProfiles)if(p.slot==slot)return &p;return nullptr;}
inline uint32_t Capacity(uint32_t slot){const auto* p=Slot(slot);return p?p->capacity:staggered_postseason::Capacity(slot);}
inline uint32_t Games(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->games:staggered_postseason::Games(b,root);}
inline uint32_t Field(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->field:staggered_postseason::Field(b,root);}
inline const Profile* Owner(const uint8_t* b,uint32_t root){if(!root||Read(b,root)!=0x63AD30)return nullptr;for(const auto& p:kProfiles)if(Read(b,root+8)==p.slot&&Read(b,root+0x10)==p.key)return &p;return nullptr;}
inline bool Route(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* profile=Owner(b,root);if(!profile)return false;const auto& p=*profile;
 const auto out=Read(b,c.esp+4),game=Read(b,c.esp+8),side=Read(b,c.esp+12),loser=Read(b,c.esp+16);
 Require(c,game<p.games&&side<2&&loser<2&&out,"Big Ten postseason graph: invalid ordinal/side");
 uint32_t target=p.routes[game][loser][0],which=p.routes[game][loser][1];
 Put(b,out,target);Put(b,out+4,which);c.eax=out;c.esp+=20;return true;
}
inline void HomeSeed(X86Context& c,uint8_t* b){
 const auto child=c.esi,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return;
 const auto descriptor=c.ebx,row=c.edx,meta=Read(b,descriptor+8),first=meta&255,second=(meta>>16)&255;
 if(first==255||second==255)return;
 Require(c,first<p->field&&second<p->field&&first!=second&&((Read(b,row)>>20)&15)<2,"Big Ten postseason home assignment: invalid seed/result");
 const auto seeds=Read(b,child+0x68),a=Read(b,seeds+16*first+12)&0x7FFFFFFF,z=Read(b,seeds+16*second+12)&0x7FFFFFFF;
 Require(c,a>=1&&a<=p->field&&z>=1&&z<=p->field&&a!=z,"Big Ten postseason home assignment: original seed differs");if(a<z)return;
 uint8_t side[8];std::memcpy(side,b+row+4,8);std::memcpy(b+row+4,b+row+12,8);std::memcpy(b+row+12,side,8);Put(b,descriptor+8,(meta<<16)|(meta>>16));
}
inline void SeedDescriptors(X86Context& c,uint8_t* b,uint32_t child,uint32_t source,uint32_t start){
 const auto root=Read(b,child+0x64);const auto* profile=Owner(b,root);Require(c,profile&&!Read(b,root+0x24)&&Read(b,child+0x5C)==profile->field&&Read(b,child+0x60)==profile->games,"Big Ten postseason seeding: owner/count differs");
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};c.esp-=0x40;const auto stamp=c.esp+0x10,out=c.esp+0x20;
 const auto& p=*profile;
 for(unsigned i=0;i<p.field;++i)std::memcpy(b+Read(b,child+0x68)+16*i+4,b+source+16*i+4,12);
 for(unsigned i=0;i<p.games;++i){Put(b,stamp,0x5FFB60);Put(b,stamp+4,start+p.days[i]);Put(b,stamp+8,p.minutes[i]);Call(c,b,0x004E9F60u,child,{out,stamp,i});}
 for(unsigned i=0;i<p.games;++i)for(unsigned side=0;side<2;++side)if(p.initial[i][side]!=0xFFFFFFFF)Call(c,b,0x004DFC60u,child,{i,side,p.initial[i][side],0,0});
 Call(c,b,0x004E7DF0u,root,{});Call(c,b,0x004D2960u,root,{});
}
inline bool Seed(X86Context& c,uint8_t* b){
 const auto event=Read(b,c.esp+4);if(!event)return false;const auto regular=Read(b,event+8);
 const Profile* profile=nullptr;for(const auto& p:kProfiles)if(p.regular==regular)profile=&p;if(!profile)return false;const auto& p=*profile;
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 const auto world=c.ecx,manager=Read(b,world+0x10),root=Read(b,manager+12+p.slot*4),roster=Read(b,world+0xC);
 Require(c,Owner(b,root)==profile&&!Read(b,root+0x24),"Big Ten postseason seeding: unexpected/nonempty tournament owner");
 const auto cache=Read(b,Read(b,Read(b,world+0x14)+0x20)+4*p.key);
 Require(c,cache,"Big Ten postseason seeding: missing standings cache");if(!b[cache+4])Call(c,b,0x004FC9F0u,cache,{});
 const auto ranked=Read(b,cache+0x10),ids=Read(b,ranked+4),end=Read(b,ranked+8);
 Require(c,ids&&end>=ids+4*p.field&&end<=ids+40*4,"Big Ten postseason seeding: standings field too small/large");
 c.esp-=0x100;const auto seeds=c.esp+0x20;
 for(unsigned i=0;i<p.field;++i){const auto team=Read(b,ids+4*i);Require(c,team>0&&team<=310&&team!=102,"Big Ten postseason seeding: invalid canonical team");for(unsigned j=0;j<i;++j)Require(c,Read(b,ids+4*j)!=team,"Big Ten postseason seeding: duplicate team");
  const auto row=Call(c,b,0x00078550u,roster,{team});Require(c,row&&((Read(b,row+0xA4)>>6)&31u)==p.key,"Big Ten postseason seeding: wrong/inactive conference member");
  Put(b,seeds+16*i,0x63A6E0);Put(b,seeds+16*i+4,team);Put(b,seeds+16*i+8,i+1);Put(b,seeds+16*i+12,(Read(b,row+0xA4)&0x80000000u)|(i+1));
 }
 SeedDescriptors(c,b,Read(b,root+0x38),seeds,phase9::TournamentStart(b,p.key));return true;
}
inline bool Champion(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return false;
 const auto out=Read(b,c.esp+4);Require(c,out,"Big Ten postseason champion: null output");
 Put(b,out+4,0);Put(b,out+8,0xFFFFFFFF);Put(b,out+12,Read(b,out+12)&0x80000000u);
 if(Read(b,root+0x24)){
  Require(c,Read(b,root+0x24)>=p->games,"Big Ten postseason champion: incomplete descriptor table");
  const auto index=Read(b,Read(b,child+0x6C)+12*(p->games-1)+4);Require(c,index<Read(b,root+0x24),"Big Ten postseason champion: row bounds");
  const auto row=Read(b,root+0x28)+20*index;
  if(((Read(b,row)>>20)&15)==3){const auto original_sp=c.esp;c.esp-=0x20;Put(b,c.esp,p->slot);Put(b,c.esp+4,index);const auto team=Call(c,b,0x00079CB0u,root,{c.esp}),seed=Call(c,b,0x004DF9C0u,child,{team});c.esp=original_sp;Require(c,seed<p->field,"Big Ten postseason champion: winner absent from seed list");Call(c,b,0x004D2B30u,out,{Read(b,child+0x68)+16*seed});}
 }
 c.eax=out;c.esp+=8;return true;
}
}
