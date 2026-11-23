// Adapted kit algorithm: original packed membership and 20-byte/9-bit game records.
#pragma once
#include "competition-bridge.h"

#include "mac-postseason-config.h"
#include "maac-postseason-candidate.h"
#include <initializer_list>
// MAC dynamic six-team routing retains original results and prior-bye exceptions.
// Original seed metadata and game descriptors own all advancement state.
namespace mvp::mac_postseason {
inline uint32_t Read(const uint8_t* b,uint32_t p){return game_record_candidate::Read(b,p);}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004EA880,why);}
using Fn=uint32_t;
inline uint32_t Call(X86Context& c,uint8_t* b,Fn fn,uint32_t owner,std::initializer_list<uint32_t> args){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 for(auto i=args.end();i!=args.begin();) {c.esp-=4;Put(b,c.esp,*--i);}c.esp-=4;Put(b,c.esp,0x004EA880);c.ecx=owner;x86_icall(c,b,fn);Require(c,c.esp==saved.esp,"MAC postseason: original callee stack differs");return c.eax;
}
inline const Profile* Slot(uint32_t slot){for(const auto& p:kProfiles)if(p.slot==slot)return &p;return nullptr;}
inline uint32_t Capacity(uint32_t slot){const auto* p=Slot(slot);return p?p->capacity:maac_postseason::Capacity(slot);}
inline uint32_t Games(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->games:maac_postseason::Games(b,root);}
inline uint32_t Field(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->field:maac_postseason::Field(b,root);}
inline const Profile* Owner(const uint8_t* b,uint32_t root){if(!root||Read(b,root)!=0x63AD30)return nullptr;for(const auto& p:kProfiles)if(Read(b,root+8)==p.slot&&Read(b,root+0x10)==p.key)return &p;return nullptr;}
inline void RegisterAdded(X86Context& c,uint8_t* b){
 const auto manager=Read(b,c.esi+0x10);
 for(const auto& p:kProfiles){
  Require(c,manager&&!Read(b,manager+12+p.slot*4),"MAC postseason registration: occupied reserved slot");
  const auto saved=c;c.esp-=12;Put(b,c.esp,0x005000E0);Put(b,c.esp+4,60);Put(b,c.esp+8,Read(b,0x6DC598));x86_icall(c,b,0x00325F20u);
  Require(c,c.esp==saved.esp-8&&c.eax,"MAC postseason registration: allocation ABI/failure");const auto root=c.eax;c=saved;
  Call(c,b,0x004F0B20u,root,{manager,p.slot});Put(b,root+0x10,p.key);Put(b,manager+12+p.slot*4,root);
 }
}
inline uint32_t ResultSeed(X86Context& c,uint8_t* b,uint32_t child,unsigned game){
 const auto root=Read(b,child+0x64),dd=Read(b,child+0x6C),saved=c.esp;c.esp-=16;Put(b,c.esp,Read(b,root+8));Put(b,c.esp+4,Read(b,dd+12*game+4));
 const auto team=Call(c,b,0x00079CB0u,root,{c.esp}),seed=Call(c,b,0x004DF9C0u,child,{team});c.esp=saved;Require(c,seed<6,"MAC result: absent winner");return seed;
}
inline uint32_t Advance(X86Context& c,uint8_t* b,uint32_t child,bool assign){
 const auto root=Read(b,child+0x64),dd=Read(b,child+0x6C),rows=Read(b,root+0x28);if(!Read(b,root+0x24))return 0xFFFFFFFF;
 uint32_t losses[6]={},wins[6]={},w[11],l[11];for(unsigned i=0;i<11;++i)w[i]=l[i]=0xFFFFFFFF;
 for(unsigned game=0;game<11;++game){
  unsigned a=0xFFFFFFFF,z=0xFFFFFFFF;
  if(game==0){a=2;z=5;}else if(game==1){a=3;z=4;}
  else if(game==2){a=l[0];z=l[1];}
  else if(game==3||game==4){if(w[0]<6&&w[1]<6){a=game-3;z=game==3?(w[0]>w[1]?w[0]:w[1]):(w[0]<w[1]?w[0]:w[1]);}}
  else if(game==5||game==6){if(l[3]<6&&l[4]<6&&w[2]<6){
   const bool both_byes=l[3]<2&&l[4]<2;
   const auto first=l[3]<2?l[3]:l[4]<2?l[4]:(l[3]>l[4]?l[3]:l[4]),other=first==l[3]?l[4]:l[3];
   if(game==5){a=both_byes?l[3]:w[2];z=both_byes?l[4]:first;}
   else {a=w[5];z=both_byes?w[2]:other;}
  }}else if(game==7){a=w[3];z=w[4];}else if(game==8){a=w[6];z=l[7];}else if(game==9){a=w[7];z=w[8];}
  else if(w[9]<6){
   if(losses[l[9]]==2){const auto row=rows+20*Read(b,dd+120+4),status=(Read(b,row)>>20)&15;if(assign&&status<2)Call(c,b,0x004DE070u,root,{dd+120});else if(assign)Require(c,status==2||status==5,"MAC unnecessary reset has result");return w[9];}
   a=w[9];z=l[9];
   // The stock six-team router may retire this row after game ten even
   // when both finalists have one loss. Restore only that required reset.
   const auto reset=rows+20*Read(b,dd+124),word=Read(b,reset),status=(word>>20)&15;
   if(assign&&(status==2||status==5))Put(b,reset,word&0xFF0FFFFFu);
  }
  if(a>=6||z>=6)continue;Require(c,a!=z&&losses[a]<2&&losses[z]<2,"MAC eliminated or duplicate participant");if(a>z){const auto tmp=a;a=z;z=tmp;}
  const auto meta=Read(b,dd+12*game+8),index=Read(b,dd+12*game+4);Require(c,index<Read(b,root+0x24),"MAC descriptor bounds");
  for(unsigned side=0;side<2;++side){const auto old=(meta>>(16*side))&255,seed=side?z:a;Require(c,old==255||old==seed,"MAC persisted pairing differs from reconstructed graph");if(assign&&old==255)Call(c,b,0x004DFC60u,child,{game,side,seed,wins[seed],losses[seed]});}
  if(((Read(b,rows+20*index)>>20)&15)!=3)continue;
  w[game]=ResultSeed(c,b,child,game);Require(c,w[game]==a||w[game]==z,"MAC winner outside dynamic pair");l[game]=w[game]==a?z:a;++wins[w[game]];++losses[l[game]];
 }
 return w[10];
}
inline bool Route(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return false;
 const auto out=Read(b,c.esp+4),game=Read(b,c.esp+8),side=Read(b,c.esp+12),loser=Read(b,c.esp+16);
 Require(c,game<p->games&&side<2&&loser<2&&out,"MAC postseason graph: invalid ordinal/side");if(!loser)Advance(c,b,child,true);
 Put(b,out,0xFFFFFFFF);Put(b,out+4,2);c.eax=out;c.esp+=20;return true;
}
inline void HomeSeed(X86Context& c,uint8_t* b){
 const auto child=c.esi,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return;
 const auto descriptor=c.ebx,row=c.edx,meta=Read(b,descriptor+8),first=meta&255,second=(meta>>16)&255;
 if(first==255||second==255)return;
 Require(c,first<p->field&&second<p->field&&first!=second&&((Read(b,row)>>20)&15)<2,"MAC postseason home assignment: invalid seed/result");
 const auto seeds=Read(b,child+0x68),a=Read(b,seeds+16*first+12)&0x7FFFFFFF,z=Read(b,seeds+16*second+12)&0x7FFFFFFF;
 Require(c,a>=1&&a<=p->field&&z>=1&&z<=p->field&&a!=z,"MAC postseason home assignment: original seed differs");if(a<z)return;
 uint8_t side[8];std::memcpy(side,b+row+4,8);std::memcpy(b+row+4,b+row+12,8);std::memcpy(b+row+12,side,8);Put(b,descriptor+8,(meta<<16)|(meta>>16));
}
inline void SeedDescriptors(X86Context& c,uint8_t* b,uint32_t child,uint32_t source,uint32_t start){
 const auto root=Read(b,child+0x64);const auto* profile=Owner(b,root);Require(c,profile&&!Read(b,root+0x24)&&Read(b,child+0x5C)==profile->field&&Read(b,child+0x60)==profile->games,"MAC postseason seeding: owner/count differs");
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
 Require(c,Owner(b,root)==profile&&!Read(b,root+0x24),"MAC postseason seeding: unexpected/nonempty tournament owner");
 const auto cache=Read(b,Read(b,Read(b,world+0x14)+0x20)+4*p.key);
 Require(c,cache,"MAC postseason seeding: missing standings cache");if(!b[cache+4])Call(c,b,0x004FC9F0u,cache,{});
 const auto ranked=Read(b,cache+0x10),ids=Read(b,ranked+4),end=Read(b,ranked+8);
 Require(c,ids&&end>=ids+4*p.field&&end<=ids+40*4,"MAC postseason seeding: standings field too small/large");
 c.esp-=0x100;const auto seeds=c.esp+0x20;
 for(unsigned i=0;i<p.field;++i){const auto team=Read(b,ids+4*i);Require(c,team>0&&team<=310&&team!=102,"MAC postseason seeding: invalid canonical team");for(unsigned j=0;j<i;++j)Require(c,Read(b,ids+4*j)!=team,"MAC postseason seeding: duplicate team");
  const auto row=Call(c,b,0x00078550u,roster,{team});Require(c,row&&((Read(b,row+0xA4)>>6)&31u)==p.key,"MAC postseason seeding: wrong/inactive conference member");
  Put(b,seeds+16*i,0x63A6E0);Put(b,seeds+16*i+4,team);Put(b,seeds+16*i+8,i+1);Put(b,seeds+16*i+12,(Read(b,row+0xA4)&0x80000000u)|(i+1));
 }
 SeedDescriptors(c,b,Read(b,root+0x38),seeds,phase9::TournamentStart(b,p.key));return true;
}
inline bool Champion(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return false;
 const auto out=Read(b,c.esp+4);Require(c,out,"MAC postseason champion: null output");
 Put(b,out+4,0);Put(b,out+8,0xFFFFFFFF);Put(b,out+12,Read(b,out+12)&0x80000000u);
 const auto seed=Advance(c,b,child,false);if(seed!=0xFFFFFFFF)Call(c,b,0x004D2B30u,out,{Read(b,child+0x68)+16*seed});
 c.eax=out;c.esp+=8;return true;
}
}



