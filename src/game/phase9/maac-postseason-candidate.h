// Adapted kit algorithm: original packed membership and 20-byte/9-bit game records.
#pragma once
#include "competition-bridge.h"

#include "maac-postseason-config.h"
#include "cusa-postseason-candidate.h"
#include <initializer_list>
// Authored MAAC reseeding is reconstructed from original game results at each round.
// Original seed metadata and game descriptors own all advancement state.
namespace mvp::maac_postseason {
inline uint32_t Read(const uint8_t* b,uint32_t p){return game_record_candidate::Read(b,p);}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004EA880,why);}
using Fn=uint32_t;
inline uint32_t Call(X86Context& c,uint8_t* b,Fn fn,uint32_t owner,std::initializer_list<uint32_t> args){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 for(auto i=args.end();i!=args.begin();) {c.esp-=4;Put(b,c.esp,*--i);}c.esp-=4;Put(b,c.esp,0x004EA880);c.ecx=owner;x86_icall(c,b,fn);Require(c,c.esp==saved.esp,"MAAC postseason: original callee stack differs");return c.eax;
}
inline const Profile* Slot(uint32_t slot){for(const auto& p:kProfiles)if(p.slot==slot)return &p;return nullptr;}
inline uint32_t Capacity(uint32_t slot){const auto* p=Slot(slot);return p?p->capacity:cusa_postseason::Capacity(slot);}
inline uint32_t Games(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->games:cusa_postseason::Games(b,root);}
inline uint32_t Field(const uint8_t* b,uint32_t root){const auto* p=Slot(Read(b,root+8));return p?p->field:cusa_postseason::Field(b,root);}
inline const Profile* Owner(const uint8_t* b,uint32_t root){if(!root||Read(b,root)!=0x63AD30)return nullptr;for(const auto& p:kProfiles)if(Read(b,root+8)==p.slot&&Read(b,root+0x10)==p.key)return &p;return nullptr;}
inline void RegisterAdded(X86Context& c,uint8_t* b){
 const auto manager=Read(b,c.esi+0x10);
 for(const auto& p:kProfiles){
  Require(c,manager&&!Read(b,manager+12+p.slot*4),"MAAC postseason registration: occupied reserved slot");
  const auto saved=c;c.esp-=12;Put(b,c.esp,0x005000E0);Put(b,c.esp+4,60);Put(b,c.esp+8,Read(b,0x6DC598));x86_icall(c,b,0x00325F20u);
  Require(c,c.esp==saved.esp-8&&c.eax,"MAAC postseason registration: allocation ABI/failure");const auto root=c.eax;c=saved;
  Call(c,b,0x004F0B20u,root,{manager,p.slot});Put(b,root+0x10,p.key);Put(b,manager+12+p.slot*4,root);
 }
}
inline uint32_t ResultSeed(X86Context& c,uint8_t* b,uint32_t child,unsigned game){
 const auto root=Read(b,child+0x64),dd=Read(b,child+0x6C),saved=c.esp;c.esp-=16;Put(b,c.esp,Read(b,root+8));Put(b,c.esp+4,Read(b,dd+12*game+4));
 const auto team=Call(c,b,0x00079CB0u,root,{c.esp}),seed=Call(c,b,0x004DF9C0u,child,{team});c.esp=saved;Require(c,seed<6,"MAAC result: absent winner");return seed;
}
inline uint32_t Advance(X86Context& c,uint8_t* b,uint32_t child,bool assign){
 const auto root=Read(b,child+0x64),dd=Read(b,child+0x6C),rows=Read(b,root+0x28);if(!Read(b,root+0x24))return 0xFFFFFFFF;
 uint32_t losses[6]={},wins[6]={},byes[6]={};unsigned at=0,round=0;
 while(at<11){
  unsigned alive[6],n=0;for(unsigned seed=0;seed<6;++seed)if(losses[seed]<2)alive[n++]=seed;
  if(n==1){
   Require(c,at==10||at==11,"MAAC champion before final");
   if(assign&&at==10){const auto row=rows+20*Read(b,dd+12*at+4),status=(Read(b,row)>>20)&15;if(status<2)Call(c,b,0x004DE070u,root,{dd+12*at});else Require(c,status==2||status==5,"MAAC unnecessary reset has result");}
   return alive[0];
  }
  unsigned bucket[6],count=0,bye=0xFFFFFFFF,pairs[3][2],width=0;
  if(round==0){pairs[0][0]=2;pairs[0][1]=5;pairs[1][0]=3;pairs[1][1]=4;width=2;}
  else if(n==2){pairs[0][0]=alive[0];pairs[0][1]=alive[1];width=1;}
  else{
   for(unsigned i=0;i<n;++i)if(losses[alive[i]]==0)bucket[count++]=alive[i];
   if(count<2){count=0;for(unsigned i=0;i<n;++i)if(losses[alive[i]]==1)bucket[count++]=alive[i];}
   Require(c,count>=2,"MAAC reseeding: no playable loss bucket");
   if(count%2){bye=bucket[0];for(unsigned i=1;i<count;++i)if(byes[bucket[i]]<byes[bye]||(byes[bucket[i]]==byes[bye]&&bucket[i]<bye))bye=bucket[i];unsigned next=0;for(unsigned i=0;i<count;++i)if(bucket[i]!=bye)bucket[next++]=bucket[i];count=next;}
   width=count/2;for(unsigned i=0;i<width;++i){pairs[i][0]=bucket[i];pairs[i][1]=bucket[count-1-i];}
  }
  Require(c,at+width<=11,"MAAC round exceeds game allocation");bool complete=true;
  for(unsigned i=0;i<width;++i){const auto game=at+i,meta=Read(b,dd+12*game+8),index=Read(b,dd+12*game+4);Require(c,index<Read(b,root+0x24),"MAAC descriptor bounds");
   for(unsigned side=0;side<2;++side){const auto old=(meta>>(16*side))&255,seed=pairs[i][side];Require(c,old==255||old==seed,"MAAC persisted pairing differs from reconstructed round");if(assign&&old==255)Call(c,b,0x004DFC60u,child,{game,side,seed,wins[seed],losses[seed]});}
   if(((Read(b,rows+20*index)>>20)&15)!=3)complete=false;
  }
  if(!complete)return 0xFFFFFFFF;
  for(unsigned i=0;i<width;++i){const auto winner=ResultSeed(c,b,child,at+i);Require(c,winner==pairs[i][0]||winner==pairs[i][1],"MAAC winner outside reconstructed round");++wins[winner];++losses[pairs[i][winner==pairs[i][0]?1:0]];}
  if(round==0){++byes[0];++byes[1];}else if(bye!=0xFFFFFFFF)++byes[bye];at+=width;++round;
 }
 unsigned champion=0xFFFFFFFF;for(unsigned seed=0;seed<6;++seed)if(losses[seed]<2){Require(c,champion==0xFFFFFFFF,"MAAC multiple survivors after reset");champion=seed;}Require(c,champion<6,"MAAC no survivor");return champion;
}
inline bool Route(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return false;
 const auto out=Read(b,c.esp+4),game=Read(b,c.esp+8),side=Read(b,c.esp+12),loser=Read(b,c.esp+16);
 Require(c,game<p->games&&side<2&&loser<2&&out,"MAAC postseason graph: invalid ordinal/side");if(!loser)Advance(c,b,child,true);
 Put(b,out,0xFFFFFFFF);Put(b,out+4,2);c.eax=out;c.esp+=20;return true;
}
inline void HomeSeed(X86Context& c,uint8_t* b){
 const auto child=c.esi,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return;
 const auto descriptor=c.ebx,row=c.edx,meta=Read(b,descriptor+8),first=meta&255,second=(meta>>16)&255;
 if(first==255||second==255)return;
 Require(c,first<p->field&&second<p->field&&first!=second&&((Read(b,row)>>20)&15)<2,"MAAC postseason home assignment: invalid seed/result");
 const auto seeds=Read(b,child+0x68),a=Read(b,seeds+16*first+12)&0x7FFFFFFF,z=Read(b,seeds+16*second+12)&0x7FFFFFFF;
 Require(c,a>=1&&a<=p->field&&z>=1&&z<=p->field&&a!=z,"MAAC postseason home assignment: original seed differs");if(a<z)return;
 uint8_t side[8];std::memcpy(side,b+row+4,8);std::memcpy(b+row+4,b+row+12,8);std::memcpy(b+row+12,side,8);Put(b,descriptor+8,(meta<<16)|(meta>>16));
}
inline void SeedDescriptors(X86Context& c,uint8_t* b,uint32_t child,uint32_t source,uint32_t start){
 const auto root=Read(b,child+0x64);const auto* profile=Owner(b,root);Require(c,profile&&!Read(b,root+0x24)&&Read(b,child+0x5C)==profile->field&&Read(b,child+0x60)==profile->games,"MAAC postseason seeding: owner/count differs");
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
 Require(c,Owner(b,root)==profile&&!Read(b,root+0x24),"MAAC postseason seeding: unexpected/nonempty tournament owner");
 const auto cache=Read(b,Read(b,Read(b,world+0x14)+0x20)+4*p.key);
 Require(c,cache,"MAAC postseason seeding: missing standings cache");if(!b[cache+4])Call(c,b,0x004FC9F0u,cache,{});
 const auto ranked=Read(b,cache+0x10),ids=Read(b,ranked+4),end=Read(b,ranked+8);
 Require(c,ids&&end>=ids+4*p.field&&end<=ids+40*4,"MAAC postseason seeding: standings field too small/large");
 c.esp-=0x100;const auto seeds=c.esp+0x20;
 for(unsigned i=0;i<p.field;++i){const auto team=Read(b,ids+4*i);Require(c,team>0&&team<=310&&team!=102,"MAAC postseason seeding: invalid canonical team");for(unsigned j=0;j<i;++j)Require(c,Read(b,ids+4*j)!=team,"MAAC postseason seeding: duplicate team");
  const auto row=Call(c,b,0x00078550u,roster,{team});Require(c,row&&((Read(b,row+0xA4)>>6)&31u)==p.key,"MAAC postseason seeding: wrong/inactive conference member");
  Put(b,seeds+16*i,0x63A6E0);Put(b,seeds+16*i+4,team);Put(b,seeds+16*i+8,i+1);Put(b,seeds+16*i+12,(Read(b,row+0xA4)&0x80000000u)|(i+1));
 }
 SeedDescriptors(c,b,Read(b,root+0x38),seeds,phase9::TournamentStart(b,p.key));return true;
}
inline bool Champion(X86Context& c,uint8_t* b){
 const auto child=c.ecx,root=Read(b,child+0x64);const auto* p=Owner(b,root);if(!p)return false;
 const auto out=Read(b,c.esp+4);Require(c,out,"MAAC postseason champion: null output");
 Put(b,out+4,0);Put(b,out+8,0xFFFFFFFF);Put(b,out+12,Read(b,out+12)&0x80000000u);
 const auto seed=Advance(c,b,child,false);if(seed!=0xFFFFFFFF)Call(c,b,0x004D2B30u,out,{Read(b,child+0x68)+16*seed});
 c.eax=out;c.esp+=8;return true;
}
}
