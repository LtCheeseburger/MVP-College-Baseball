#include "common/team_select_ui.h"
#include "common/overhaul.h"
#include "common/dashboard_shell.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "core/log.h"
#include <array>
#include <bit>

#include <mutex>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace {
constexpr uint32_t kFirst=62000,kCount=64,kChars=256;
std::array<uint32_t,kCount> pointers{};

std::mutex textMutex;
void Color(uint32_t node,unsigned color,float strength=1.f){
 // Native FERect stores normalized RGB / 510 in two RGBA endpoints.
 // Verified against the authored 60/66/74 selection rectangle in a live scene.
 for(unsigned i=0;i<3;++i){float value=float((color>>(16-8*i))&255)/510.f*strength;kernel::W32(node+0x48+4*i,std::bit_cast<uint32_t>(value));kernel::W32(node+0x58+4*i,std::bit_cast<uint32_t>(value*.65f));}
}
uint32_t Allocate(unsigned i){
 if(!pointers[i]){pointers[i]=mem::AllocSystem(kChars*2);if(pointers[i])std::memset(kernel::HostPtr(pointers[i]),0,kChars*2);}
 return pointers[i];
}
std::string Upper(std::string value){for(auto& c:value)if(uint8_t(c)<128)c=char(std::toupper(uint8_t(c)));return value;}
void Text(unsigned i,const std::string& text){
 if(i>=kCount)return;
 const auto pointer=Allocate(i);if(!pointer)return;
 auto* dest=static_cast<char16_t*>(kernel::HostPtr(pointer));unsigned n=0;
 for(size_t p=0;p<text.size() && n+2<kChars;){
  unsigned c=uint8_t(text[p++]);
  if(c>=0xc0){unsigned extra=c<0xe0?1:c<0xf0?2:3;c&=extra==1?31:extra==2?15:7;
   while(extra-- && p<text.size())c=(c<<6)|(uint8_t(text[p++])&63);}
  if(c>0xffff){c-=0x10000;dest[n++]=char16_t(0xd800+(c>>10));dest[n++]=char16_t(0xdc00+(c&1023));}
  else dest[n++]=char16_t(c);
 }
 dest[n]=0;
}
}
uint32_t game::DashboardText(uint32_t id){
 if(!overhaul::Active() || id<kFirst || id>=kFirst+kCount)return 0;
 std::lock_guard guard(textMutex);return Allocate(id-kFirst);
}
bool game::BindNativeDashboard(uint32_t root,const DynastyHomeCard& card,const std::vector<std::string>& updates){
 PublishDashboardShell(card);
 std::array<uint32_t,kCount> nodes{};bool marker=false;
 auto depth=kernel::R32(root+0x24);
 for(auto node=kernel::R32(root+8),n=0u;node && n<4096 && mem::IsCommitted(node+0x74) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto name=kernel::ReadCString(node+0x10,16);if(name=="M27DASH")marker=true;
  unsigned logo;
  if((std::sscanf(name.c_str(),"DH%u",&logo)==1)||(std::sscanf(name.c_str(),"DU%u",&logo)==1)||(std::sscanf(name.c_str(),"DO%u",&logo)==1)||(std::sscanf(name.c_str(),"DT%u",&logo)==1)){
   const bool show=logo==(name[1]=='O'?card.awayLogo:card.homeLogo);kernel::W16(node+0x28,(kernel::R16(node+0x28)&~1u)|unsigned(show));
  }
  if(name.starts_with("DACC"))Color(node,card.school.color);
  if(name=="DOVERVIEW"){kernel::W16(node+0x28,(kernel::R16(node+0x28)&~1u)|unsigned(card.section==0));Color(node,card.school.color,.7f);}
  if(name=="DCTA" || name=="DASH56"){
   kernel::W16(node+0x28,(kernel::R16(node+0x28)&~1u)|unsigned(card.section<=1));
   if(name=="DCTA")Color(node,card.school.color);
  }
  if(name.starts_with("DSTAR") && name.size()==6){unsigned star=name[5]-'0';auto flags=kernel::R16(node+0x28);kernel::W16(node+0x28,(flags&~1u)|(card.school.prestige>int(star)));}
  if(name.starts_with("DBAR") && name.size()==5){unsigned row=name[4]-'0';const int grades[]={2,1,3,4};if(row<4){auto grade=card.school.team.grades[grades[row]];float amount=grade.empty()?0.f:grade[0]=='A'?.9f:grade[0]=='B'?.72f:grade[0]=='C'?.54f:grade[0]=='D'?.36f:.18f;if(grade.find('+')!=std::string::npos)amount+=.06f;if(grade.find('-')!=std::string::npos)amount-=.06f;kernel::W16(node+0x2e,uint16_t(std::clamp(amount,0.f,1.f)*78));Color(node,card.school.color);}}
  unsigned index;
  if(std::sscanf(name.c_str(),"DASH%u",&index)==1 && index<kCount)nodes[index]=node;
  if(name.starts_with("DSEL") && name.size()==5){unsigned row=name[4]-'0';auto flags=kernel::R16(node+0x28);kernel::W16(node+0x28,(flags&~1u)|(card.section>0 && row==unsigned(card.section-1)));Color(node,card.school.color,.7f);}
 }
 if(!marker)return false;
 std::lock_guard guard(textMutex);
 const char* fixed[]={"DYNASTY HOME","OVERVIEW","SEASON OVERVIEW","UPCOMING GAME","TEAM RATINGS","UPCOMING SCHEDULE","PROGRAM UPDATES","RECORD","CONFERENCE","NAT. RANK","CONF. PLACE","PRESTIGE","HITTING","PITCHING","FIELDING","SPEED"};
 for(unsigned i=0;i<std::size(fixed);++i)Text(i,fixed[i]);
 Text(16,Upper(card.date));Text(17,Upper(card.school.team.name));Text(18,Upper(card.school.team.nickname));Text(19,Upper(card.school.team.conference.empty()?card.school.location:card.school.team.conference));
 Text(20,card.records[0]);Text(21,card.records[1]);Text(22,card.school.team.rank);Text(23,card.school.standing);
 Text(24,card.school.prestige<0?"--":std::to_string(card.school.prestige)+" / 6");
 for(unsigned i=0;i<4;++i){const int grade[]={2,1,3,4};Text(25+i,card.school.team.grades[grade[i]]);}
 Text(29,Upper(card.school.team.name));
 Text(30,card.upcoming.empty()?"NO UPCOMING GAME":Upper(card.upcoming[0].opponent));
 Text(31,card.upcoming.empty()?"Open Schedule for season details":card.upcoming[0].date);
 for(unsigned i=0;i<3;++i)Text(32+i,i<card.upcoming.size()?card.upcoming[i].date+"   "+(card.upcoming[i].home?"vs ":"at ")+card.upcoming[i].opponent:"");
 for(unsigned i=0;i<3;++i)Text(35+i,i<updates.size()?updates[i]:i==0?"No completed games this season":i==1?"Recruiting and roster tools are available":"");
 Text(38,"OVERALL");Text(39,card.school.team.grades[0]);
 for(unsigned i=0;i<5;++i)Text(40+i,i==2?"STATISTICS":i<card.menu.size()?Upper(card.menu[i]):"");
 Text(45,"Select a section to manage your program");
 Text(46,card.upcoming.empty()?"":card.upcoming[0].home?"VS":"AT");
 Text(47,card.school.team.rank);Text(48,Upper(card.conferenceAbbreviation.empty()?card.school.location:card.conferenceAbbreviation));Text(49,"NATIONAL RANK");
 for(unsigned i=0;i<6;++i)Text(50+i,std::string(1,char('A'+i)));
 Text(56,"OPEN SCHEDULE");Text(57,"DYNASTY / HOME");Text(58,"CONFERENCE RECORD");
 Text(59,card.conferenceAbbreviation.empty()?"LOCATION":"CONFERENCE");
 Text(60,"RECORD");Text(61,card.records[0]);Text(62,"HEAD COACH");Text(63,Upper(card.school.team.name));
 return true;
}



void game::RefreshNativeDashboard(X86Context& ctx,uint32_t root){
 if(kernel::ReadCString(root+0x10,16)!="FMGRDECISION")return;
 std::lock_guard guard(textMutex);const auto depth=kernel::R32(root+0x24);
 for(auto node=kernel::R32(root+8),n=0u;node && n<4096 && mem::IsCommitted(node+0x74) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto name=kernel::ReadCString(node+0x10,16);if(name=="RANK" || name=="COVER")kernel::W16(node+0x28,kernel::R16(node+0x28)&~1u);unsigned index;
  if(std::sscanf(name.c_str(),"DASH%u",&index)!=1 || index>=kCount)continue;
  auto source=Allocate(index),owned=kernel::R32(node+0x44);if(!source)continue;
  bool same=owned!=0;
  for(unsigned i=0;same && i<kChars;++i){auto value=kernel::R16(source+2*i);if(!mem::IsCommitted(owned+2*i+1) || value!=kernel::R16(owned+2*i)){same=false;break;}if(!value)break;}
  if(!same){auto saved=ctx;ctx.ecx=node;kernel::CallGuest(ctx,0x000E70F0,{source});ctx=saved;}
 }
}
