#include "game/phase9/bracket-browser.h"
#include <vector>
#include <iostream>
#include <stdexcept>
int main(){
 std::vector<uint8_t> b(0x10000);
 auto put=[&](unsigned p,unsigned v){std::memcpy(b.data()+p,&v,4);};
 auto committed=[&](uint32_t p,uint32_t n){return p>=0x1000&&n&&uint64_t(p)+n<=b.size();};
 auto require=[](bool ok){if(!ok)throw std::runtime_error("Bracket browser bounds regression");};
 constexpr unsigned manager=0x1000,descriptor=0x2000,table=0x3000,rows=0x4000;
 auto resolve=[&]{return mvp::bracket_browser::ResolveRow(b.data(),manager,descriptor,72,committed);};
 put(descriptor,36);put(descriptor+4,0);require(!resolve()); // Unallocated future owner: actual crash.
 put(manager+12+36*4,table);put(table+36,7);put(table+40,rows);require(resolve()==rows);
 put(descriptor+4,6);require(resolve()==rows+120);
 put(descriptor+4,7);require(!resolve());put(descriptor+4,0xFFFFFFFF);require(!resolve());
 put(descriptor+4,0);put(descriptor,72);require(!resolve());put(descriptor,0xFFFFFFFF);require(!resolve());
 put(descriptor,36);put(table+36,0);require(!resolve());put(table+36,40001);require(!resolve());
 put(table+36,7);put(table+40,0);require(!resolve());put(table+40,0xFFFFFFF0);require(!resolve());
 for(unsigned games=1;games<=20;++games)for(unsigned i=0;i<24;++i){
  require(mvp::bracket_browser::EmptyVisualOrdinal(0x520403,i,games)==(i>=games));
  require(mvp::bracket_browser::EmptyVisualOrdinal(0x520414,i,games)==(i>=games));
  require(mvp::bracket_browser::EmptyVisualOrdinal(0x520442,i,games)==(i>=games));
  require(mvp::bracket_browser::EmptyVisualOrdinal(0x520453,i,games)==(i>=games));
  require(!mvp::bracket_browser::EmptyVisualOrdinal(0x4EA880,i,games));
 }
 std::cout<<"PASS null/future schedule owners, row bounds, invalid slots, uncommitted storage and visual-only ordinal handling\n";
}


