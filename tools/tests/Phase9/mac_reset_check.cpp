#include "game/phase9/mac-postseason-candidate.h"
#include <iostream>
#include <vector>
namespace {
constexpr uint32_t root=0x1000,child=0x2000,dd=0x3000,rows=0x4000;
unsigned choices;
uint32_t Read(uint8_t* b,uint32_t p){return mvp::mac_postseason::Read(b,p);}
void Put(uint8_t* b,uint32_t p,uint32_t v){mvp::mac_postseason::Put(b,p,v);}
}
[[noreturn]] void x86_trap(X86Context&,uint32_t,const char* why){throw std::runtime_error(why);}
void x86_icall(X86Context& c,uint8_t* b,uint32_t target){
 const auto arg=[&](unsigned i){return Read(b,c.esp+4+4*i);};
 if(target==0x004DFC60){const auto game=arg(0),side=arg(1),seed=arg(2),p=dd+12*game+8;Put(b,p,(Read(b,p)&~(255u<<(16*side)))|(seed<<(16*side)));c.esp+=24;}
 else if(target==0x00079CB0){const auto game=Read(b,arg(0)+4),meta=Read(b,dd+12*game+8);c.eax=1+((meta>>(16*((choices>>game)&1)))&255);c.esp+=8;}
 else if(target==0x004DF9C0){c.eax=arg(0)-1;c.esp+=8;}
 else if(target==0x004DE070){const auto row=rows+20*Read(b,arg(0)+4);Put(b,row,(Read(b,row)&0xFF0FFFFF)|(2u<<20));c.esp+=8;}
 else throw std::runtime_error("Unexpected native call in MAC graph fixture");
}
int main(){
 unsigned restored=0,cancelled=0;
 for(choices=0;choices<1024;++choices){
  std::vector<uint8_t> memory(0x10000);auto b=memory.data();X86Context c{};c.esp=0xF000;
  Put(b,child+0x64,root);Put(b,child+0x6C,dd);Put(b,root+36,11);Put(b,root+40,rows);Put(b,root+8,65);
  for(unsigned i=0;i<11;++i){Put(b,dd+12*i+4,i);Put(b,dd+12*i+8,0x00FF00FF);}
  for(unsigned i=0;i<10;++i){mvp::mac_postseason::Advance(c,b,child,true);Put(b,rows+20*i,3u<<20);}
  // Reproduce the native router retiring a reset after the first final.
  Put(b,rows+200,5u<<20);
  const auto winner=mvp::mac_postseason::Advance(c,b,child,true);
  const auto status=(Read(b,rows+200)>>20)&15;
  if(winner==0xFFFFFFFF){if(status!=0)throw std::runtime_error("Required reset remained cancelled");++restored;}
  else {if(status!=5)throw std::runtime_error("Unnecessary reset became playable");++cancelled;}
 }
 if(!restored||!cancelled)throw std::runtime_error("Both reset branches must execute");
 std::cout<<"PASS 1024 MAC result paths: "<<restored<<" required resets restored; "<<cancelled<<" unnecessary resets retained\n";
}
