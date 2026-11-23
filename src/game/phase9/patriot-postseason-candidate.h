#pragma once
#include "competition-bridge.h"


// The original series objects, result propagation and serializers remain the
// owners. This adapter changes the authored four-team field and series dates.
// It is deliberately limited to the Patriot table until other families pass.
namespace mvp::patriot_postseason {
inline uint32_t Read(const uint8_t* b,uint32_t p){return game_record_candidate::Read(b,p);}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004FE8D0,why);}
inline bool Owns(const uint8_t* b,uint32_t root){return root && Read(b,root)==0x63AF18 && Read(b,root+8)==23 && Read(b,root+0x10)==10;}
inline void FourthSeed(X86Context& c,uint8_t* b){
 Require(c,Owns(b,c.esi),"Patriot field: unexpected table owner");
 const auto caches=Read(b,Read(b,c.edi+0x14)+0x20);
 const auto cache=Read(b,caches+Read(b,c.esi+0x10)*4);
 const auto ranked=Read(b,cache+0x10),ids=Read(b,ranked+4),end=Read(b,ranked+8);
 const auto vector=c.esp+0x1C,start=Read(b,vector),used=Read(b,vector+4),limit=Read(b,vector+8);
 Require(c,ids && end>=ids+16 && end<=ids+40*4 && start && used==start+48 && limit>=start+64,
         "Patriot field: ranked list or four-descriptor allocation differs");
 for(unsigned i=0;i<3;++i)Require(c,Read(b,start+16*i+4)==Read(b,ids+4*i),"Patriot field: existing seeds differ from standings");
 const auto team=Read(b,ids+12);
 Require(c,expansion::SchoolActive(team),"Patriot field: inactive/out-of-range fourth seed");
 for(unsigned i=0;i<3;++i)Require(c,Read(b,ids+4*i)!=team,"Patriot field: duplicate fourth seed");
 const auto saved=c;
 struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 c.esp-=8;Put(b,c.esp,0);Put(b,c.esp+4,team);c.ecx=Read(b,saved.edi+0xC);
 x86_icall(c,b,0x78550);
 Require(c,c.esp==saved.esp && c.eax,"Patriot field: roster lookup ABI changed");
 const auto user=Read(b,c.eax+0xA4)&0x80000000u;
 Put(b,used,0x63A6E0);Put(b,used+4,team);Put(b,used+8,4);Put(b,used+12,user|4);
 Put(b,vector+4,used+16);
}
inline void SeriesFixture(X86Context& c,uint8_t* b){
 const auto group=Read(b,c.esi+0x5C),root=group?Read(b,group+4):0;
 if(!Owns(b,root))return;
 const auto stage=Read(b,group+8),game=Read(b,c.esp+0x1C);
 Require(c,(stage==1||stage==2) && game<3 && Read(b,group+0x20)==2,
         "Patriot series: expected best-of-three semifinal/final");
 const auto first=Read(b,c.esi+0x64),second=Read(b,c.esi+0x74);
 const auto first_seed=Read(b,c.esi+0x6C)&0x7FFFFFFFu,second_seed=Read(b,c.esi+0x7C)&0x7FFFFFFFu;
 Require(c,first && second && first!=second && first<512 && second<512 && first_seed>=1 && first_seed<=4 && second_seed>=1 && second_seed<=4 && first_seed!=second_seed,
         "Patriot series: invalid participant/seed identity");
 const auto start=phase9::TournamentStart(b,10)+(stage==2?7u:0u);
 const auto base_day=Read(b,root+0x14);
 // Original reset leaves an unanchored empty table. The first original
 // append establishes its calendar-year base from our authored timestamp.
 Require(c,(!Read(b,root+0x24) && base_day==0xFFFFFFFFu) || (base_day<=start && start-base_day<510),
         "Patriot series: incompatible season or game-date span");
 c.ebp=first_seed<second_seed?first:second;c.ebx=first_seed<second_seed?second:first;
 Put(b,c.esp+0x5C,start+(game==2));
 c.edi=game==0?720:game==1?960:780;
}
}
