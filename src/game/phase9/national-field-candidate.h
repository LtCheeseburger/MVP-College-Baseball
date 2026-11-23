// Adapted to original packed team membership; original 64-team collector retained.
#pragma once
#include "competition-bridge.h"
#include "national-field-config.h"
#include <cstring>

// The artwork nibble is shared by modern conferences. AQ routing uses the
// immutable conference key and the separately allocated tournament slot.
// This candidate preserves the original 64-entry collector and ranking order.
// Modern eligibility and the eight unfinished tournament adapters are separate.
namespace mvp::national_field {
inline uint32_t Read(const uint8_t* b,uint32_t p){uint32_t v;std::memcpy(&v,b+p,4);return v;}
inline void Put(uint8_t* b,uint32_t p,uint32_t v){std::memcpy(b+p,&v,4);}
inline void Require(X86Context& c,bool ok,const char* why){if(!ok)x86_trap(c,0x004FEDE0,why);}
inline uint32_t Slot(uint32_t key){for(const auto& p:kConferences)if(p.key==key)return p.slot;return 0xFFFFFFFFu;}
inline void Resolve(X86Context& c,uint8_t* b){c.eax=Slot(Read(b,c.esp+4));c.esp+=8;}
inline uint32_t Call(X86Context& c,uint8_t* b,uint32_t target,uint32_t owner,uint32_t arg){
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 c.esp-=8;Put(b,c.esp,0x004FEDE0);Put(b,c.esp+4,arg);c.ecx=owner;x86_icall(c,b,target);
 Require(c,c.esp==saved.esp,"National field: callee ABI differs");return c.eax;
}
inline uint32_t Team(X86Context& c,uint8_t* b,uint32_t roster,uint32_t id){
 Require(c,id>0&&id<=310&&id!=102,"National field: invalid team identity");
 const auto row=Call(c,b,0x00078550,roster,id),membership=(Read(b,row+0xA4)>>6)&31u;
 Require(c,row&&expansion::SchoolActive(id)&&Slot(membership)!=0xFFFFFFFFu,"National field: inactive or unknown conference");return membership;
}
inline void ValidateInputs(X86Context& c,uint8_t* b){
 const auto world=c.ecx,roster=Read(b,world+12),manager=Read(b,world+16),out=Read(b,c.esp+4);
 Require(c,roster&&manager&&out&&Read(b,out+16)==64&&Read(b,out+4)==Read(b,out+8),"National field: invalid world or nonempty output");
 const auto conferences=Read(b,roster+0x2C),map=Read(b,conferences+4);
 Require(c,conferences&&map&&Read(b,map+8)==30,"National field: conference count differs");
 const auto ranks=Call(c,b,0x00078540,roster,0),rankdata=Read(b,ranks+8);
 Require(c,ranks&&Read(b,ranks+4)==311&&rankdata,"National field: wrong ranking population");
 bool seen[310]{};unsigned active=0;
 for(unsigned i=0;i<311;++i){const auto rank=Read(b,rankdata+4*i);if(i==0||i==102){Require(c,rank==0,"National field: inactive team ranked");continue;}
  Team(c,b,roster,i);Require(c,rank>0&&rank<=309&&!seen[rank],"National field: ranks are not a complete permutation");seen[rank]=true;++active;
 }
 Require(c,active==309,"National field: active population differs");
 const auto saved=c;struct Restore{X86Context& c;X86Context saved;~Restore(){c=saved;}} restore{c,saved};
 c.esp-=32;const auto seed=c.esp;
 for(const auto& p:kConferences){const auto root=Read(b,manager+12+4*p.slot);
  Require(c,root&&Read(b,root+8)==p.slot&&Read(b,root+16)==p.key,"National field: missing or wrong conference tournament owner");
  const auto vt=Read(b,root),fn=Read(b,vt+0x58);Require(c,vt&&fn,"National field: missing champion getter");
  std::memset(b+seed,0,16);const auto result=Call(c,b,fn,root,seed);
  Require(c,result==seed&&Team(c,b,roster,Read(b,seed+4))==p.key,"National field: champion belongs to another conference");
 }
}
inline void ValidateOutput(X86Context& c,uint8_t* b){
 const auto out=c.ebp,first=Read(b,out+4),end=Read(b,out+8),roster=Read(b,c.ebx+12);
 Require(c,first&&end==first+64*12,"National field: incomplete 64-team field");
 bool seen[311]{};uint32_t rank=0;
 for(unsigned i=0;i<64;++i){const auto at=first+12*i,team=Read(b,at),conf=Read(b,at+4),next=Read(b,at+8);
  Require(c,Team(c,b,roster,team)==conf&&!seen[team]&&next>rank,"National field: duplicate, wrong conference or unsorted rank");seen[team]=true;rank=next;
 }
}
}
