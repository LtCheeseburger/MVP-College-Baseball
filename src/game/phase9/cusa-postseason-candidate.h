// Adapted kit algorithm: original packed membership and 20-byte/9-bit game records.
#pragma once
#include "competition-bridge.h"

#include "cusa-postseason-config.h"
#include "bigten-postseason-candidate.h"
#include <initializer_list>
// CUSA round-robin pool winners and a wildcard form a seeded semifinal field.
// Original seed metadata and game descriptors own all advancement state.
namespace mvp::cusa_postseason {
inline uint32_t Read(const uint8_t* b,uint32_t p){return game_record_candidate::Read(b,p);}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004EA880,why);}
using Fn=uint32_t;
inline uint32_t Call(X86Context& c,uint8_t* b,Fn fn,uint32_t owner,std::initializer_list<uint32_t> args){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 for(auto i=args.end();i!=args.begin();) {c.esp-=4;Put(b,c.esp,*--i);}c.esp-=4;Put(b,c.esp,0x004EA880);c.ecx=owner;x86_icall(c,b,fn);Require(c,c.esp==saved.esp,"CUSA postseason: original callee stack differs");return c.eax;
}
inline const Profile* Slot(uint32_t slot){for(const auto& p:kProfiles)if(p.slot==slot)return &p;return nullptr;}
inline uint32_t Capacity(uint32_t slot){const auto* p=Slot(slot);return p?p->capacity:bigten_postseason::Capacity(slot);}
inline uint32_t Games(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->games:bigten_postseason::Games(b,root);}
inline uint32_t Field(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->field:bigten_postseason::Field(b,root);}
inline const Profile* Owner(const uint8_t* b,uint32_t root){if(!root||Read(b,root)!=0x63AD30)return nullptr;for(const auto& p:kProfiles)if(Read(b,root+8)==p.slot&&Read(b,root+0x10)==p.key)return &p;return nullptr;}
// Resolve qualifiers from original completed rows; no out-of-band bracket state.
inline void Qualify(X86Context& c,uint8_t* b,uint32_t child,const Profile& p){
 const auto root=Read(b,child+0x64),descriptors=Read(b,child+0x6C),rows=Read(b,root+0x28);
 uint32_t wins[9]={},played[9]={};
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};c.esp-=0x20;const auto descriptor=c.esp;
 for(unsigned game=0;game<9;++game){
  const auto at=Read(b,descriptors+12*game+4);Require(c,at<Read(b,root+0x24),"CUSA pools: descriptor bounds");
  const auto row=rows+20*at;if(((Read(b,row)>>20)&15)!=3)return;
  Put(b,descriptor,p.slot);Put(b,descriptor+4,at);
  const auto winner=Call(c,b,0x00079CB0u,root,{descriptor}),seed=Call(c,b,0x004DF9C0u,child,{winner});
  const auto first=p.initial[game][0],second=p.initial[game][1];
  Require(c,first<9&&second<9&&(seed==first||seed==second),"CUSA pools: winner outside authored pair");
  ++wins[seed];++played[first];++played[second];
 }
 for(unsigned seed=0;seed<9;++seed)Require(c,played[seed]==2,"CUSA pools: incomplete participation");
 // Three decisive games yield either distinct 2/1/0 records or a three-way
 // 1/1/1 tie. Head-to-head cannot split that cycle; original seed breaks it.
 uint32_t advanced[4],wildcard=0xFFFFFFFF;
 for(unsigned group=0;group<3;++group){
  auto best=p.pools[group][0];for(unsigned i=1;i<3;++i){auto seed=p.pools[group][i];if(wins[seed]>wins[best]||(wins[seed]==wins[best]&&seed<best))best=seed;}
  advanced[group]=best;
 }
 for(unsigned seed=0;seed<9;++seed){bool winner=false;for(unsigned i=0;i<3;++i)winner|=advanced[i]==seed;if(winner)continue;
  if(wildcard==0xFFFFFFFF||wins[seed]>wins[wildcard]||(wins[seed]==wins[wildcard]&&seed<wildcard))wildcard=seed;
 }
 Require(c,wildcard<9,"CUSA pools: no wildcard");advanced[3]=wildcard;
 for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)if(advanced[j]<advanced[i]){auto tmp=advanced[i];advanced[i]=advanced[j];advanced[j]=tmp;}
 const uint32_t pairs[2][2]={{advanced[0],advanced[3]},{advanced[1],advanced[2]}};
 for(unsigned game=0;game<2;++game){const auto meta=Read(b,descriptors+12*(game+9)+8);
  for(unsigned side=0;side<2;++side){const auto old=(meta>>(16*side))&255;
   Require(c,old==255||old==pairs[game][side],"CUSA pools: existing semifinal differs");
   if(old==255)Call(c,b,0x004DFC60u,child,{game+9,side,pairs[game][side],0,0});
  }
 }
}
inline bool Route(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* profile=Owner(b,root);if(!profile)return false;const auto& p=*profile;
 const auto out=Read(b,c.esp+4),game=Read(b,c.esp+8),side=Read(b,c.esp+12),loser=Read(b,c.esp+16);
 Require(c,game<p.games&&side<2&&loser<2&&out,"CUSA postseason graph: invalid ordinal/side");
 if(game<9&&!loser)Qualify(c,b,child,p);
 uint32_t target=p.routes[game][loser][0],which=p.routes[game][loser][1];
 Put(b,out,target);Put(b,out+4,which);c.eax=out;c.esp+=20;return true;
}
inline void HomeSeed(X86Context& c,uint8_t* b){
 const auto child=c.esi,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return;
 const auto descriptor=c.ebx,row=c.edx,meta=Read(b,descriptor+8),first=meta&255,second=(meta>>16)&255;
 if(first==255||second==255)return;
 Require(c,first<p->field&&second<p->field&&first!=second&&((Read(b,row)>>20)&15)<2,"CUSA postseason home assignment: invalid seed/result");
 const auto seeds=Read(b,child+0x68),a=Read(b,seeds+16*first+12)&0x7FFFFFFF,z=Read(b,seeds+16*second+12)&0x7FFFFFFF;
 Require(c,a>=1&&a<=p->field&&z>=1&&z<=p->field&&a!=z,"CUSA postseason home assignment: original seed differs");if(a<z)return;
 uint8_t side[8];std::memcpy(side,b+row+4,8);std::memcpy(b+row+4,b+row+12,8);std::memcpy(b+row+12,side,8);Put(b,descriptor+8,(meta<<16)|(meta>>16));
}
inline void SeedDescriptors(X86Context& c,uint8_t* b,uint32_t child,uint32_t source,uint32_t start){
 const auto root=Read(b,child+0x64);const auto* profile=Owner(b,root);Require(c,profile&&!Read(b,root+0x24)&&Read(b,child+0x5C)==profile->field&&Read(b,child+0x60)==profile->games,"CUSA postseason seeding: owner/count differs");
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
 Require(c,Owner(b,root)==profile&&!Read(b,root+0x24),"CUSA postseason seeding: unexpected/nonempty tournament owner");
 const auto cache=Read(b,Read(b,Read(b,world+0x14)+0x20)+4*p.key);
 Require(c,cache,"CUSA postseason seeding: missing standings cache");if(!b[cache+4])Call(c,b,0x004FC9F0u,cache,{});
 const auto ranked=Read(b,cache+0x10),ids=Read(b,ranked+4),end=Read(b,ranked+8);
 Require(c,ids&&end>=ids+4*p.field&&end<=ids+40*4,"CUSA postseason seeding: standings field too small/large");
 c.esp-=0x100;const auto seeds=c.esp+0x20;
 for(unsigned i=0;i<p.field;++i){const auto team=Read(b,ids+4*i);Require(c,team>0&&team<=310&&team!=102,"CUSA postseason seeding: invalid canonical team");for(unsigned j=0;j<i;++j)Require(c,Read(b,ids+4*j)!=team,"CUSA postseason seeding: duplicate team");
  const auto row=Call(c,b,0x00078550u,roster,{team});Require(c,row&&((Read(b,row+0xA4)>>6)&31u)==p.key,"CUSA postseason seeding: wrong/inactive conference member");
  Put(b,seeds+16*i,0x63A6E0);Put(b,seeds+16*i+4,team);Put(b,seeds+16*i+8,i+1);Put(b,seeds+16*i+12,(Read(b,row+0xA4)&0x80000000u)|(i+1));
 }
 SeedDescriptors(c,b,Read(b,root+0x38),seeds,phase9::TournamentStart(b,p.key));return true;
}
inline bool Champion(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return false;
 const auto out=Read(b,c.esp+4);Require(c,out,"CUSA postseason champion: null output");
 Put(b,out+4,0);Put(b,out+8,0xFFFFFFFF);Put(b,out+12,Read(b,out+12)&0x80000000u);
 if(Read(b,root+0x24)){
  Require(c,Read(b,root+0x24)>=p->games,"CUSA postseason champion: incomplete descriptor table");
  const auto index=Read(b,Read(b,child+0x6C)+12*(p->games-1)+4);Require(c,index<Read(b,root+0x24),"CUSA postseason champion: row bounds");
  const auto row=Read(b,root+0x28)+20*index;
  if(((Read(b,row)>>20)&15)==3){const auto original_sp=c.esp;c.esp-=0x20;Put(b,c.esp,p->slot);Put(b,c.esp+4,index);const auto team=Call(c,b,0x00079CB0u,root,{c.esp}),seed=Call(c,b,0x004DF9C0u,child,{team});c.esp=original_sp;Require(c,seed<p->field,"CUSA postseason champion: winner absent from seed list");Call(c,b,0x004D2B30u,out,{Read(b,child+0x68)+16*seed});}
 }
 c.eax=out;c.esp+=8;return true;
}
}
