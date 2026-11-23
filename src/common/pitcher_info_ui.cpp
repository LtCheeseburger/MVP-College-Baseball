#include "core/dev_tools.h"
#include "pitcher_info_ui.h"
#include "overhaul.h"
#include <sstream>
#include "ui_style.h"
#include "app/app.h"
#include "gpu/backend.h"
#include "core/log.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace game {
namespace {
std::mutex mutex;
PitcherCard state;
double began=0,exitAt=0;
bool exiting=false;
double Clock(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::filesystem::path Assets(){return overhaul::Root();}
}
void PublishPitcherCard(const PitcherCard& card){std::lock_guard lock(mutex);if(card.visible && (!state.visible || state.name!=card.name)){began=Clock();exiting=false;}state=card;}
void PitcherCardTimeline(const char* name){std::lock_guard lock(mutex);if(std::string(name)=="ENTER"){began=Clock();exiting=false;}if(std::string(name)=="OVEXIT"){exitAt=Clock();exiting=true;}if(dev::Env("MVP06_TRACE_FE"))LOGR("pitcher card timeline: %s",name);}
void DrawPitcherCard(){
  PitcherCard card;double at,out;bool exit;
  {std::lock_guard lock(mutex);card=state;at=began;out=exitAt;exit=exiting;}
  if(dev::Env("MVP06_TRACE_FE") && out>0 && Clock()-out>.5){static double capturedExit=0;if(capturedExit!=out){capturedExit=out;gpu::g_capture=true;}}
  if(!card.visible || card.name.empty() || !gpu::g_ui_image)return;
  float t=float(std::clamp((Clock()-(exit?out:at))/.266,0.,1.));float alpha=exit?1-t:t;if(alpha<=0)return;
  static auto root=Assets();
  static auto colors=[&]{std::unordered_map<std::string,unsigned> r;std::istringstream f(overhaul::Text(root/"team-colors.tsv"));std::string line;while(std::getline(f,line)){auto tab=line.find('\t');if(tab!=std::string::npos)r[line.substr(0,tab)]=std::strtoul(line.c_str()+tab+1,nullptr,16);}return r;}();
  unsigned rgb=0xB75412;if(auto c=colors.find(card.team);c!=colors.end())rgb=c->second;
  auto* d=ImGui::GetBackgroundDrawList();auto screen=ImGui::GetIO().DisplaySize;float s=std::min(screen.x/2560.f,screen.y/1440.f),ox=(screen.x-2560*s)/2,oy=(screen.y-1440*s)/2;
  constexpr float left=1820,top=240,width=580;
  auto p=[&](float x,float y){return ImVec2(ox+(left+x+90*(1-alpha)*(1-alpha))*s,oy+(top+y)*s);};
  auto tint=[&](int r,int g,int b,int a=255){return IM_COL32(r,g,b,int(a*alpha));};
  auto primary=[&](int a=255){return tint((rgb>>16)&255,(rgb>>8)&255,rgb&255,a);};
  auto text=[&](UiFont font,float size,float x,float y,const std::string& value,float max){UiText(d,font,size*s,p(x,y),tint(248,248,248),value.c_str(),max*s);};
  auto atlas=gpu::g_ui_image(root/"pitcher-info-atlas.png");
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color){if(atlas)d->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1536,v/1024},{(u+uw)/1536,(v+vh)/1024},color);};
  auto frame=[&](float x,float y,float w,float h){d->AddRectFilled(p(x,y),p(x+w,y+h),tint(4,6,8,245));d->AddRect(p(x,y),p(x+w,y+h),tint(191,194,196),0,0,2*s);};
  frame(0,0,width,72);region(3,3,width-6,66,60,25,800,70,tint(255,255,255));
  d->AddQuadFilled(p(3,3),p(140,3),p(170,69),p(3,69),primary());
  text(UiFont::Heading,56,26,3,card.number,100);
  text(UiFont::Heading,49,width-103,7,card.position,88);
  text(UiFont::Prompt,29,185,22,card.team.empty()?"PITCHER PROFILE":card.team,285);
  frame(0,80,width,270);region(3,83,width-6,264,353,199,490,164,primary());
  if(!card.teamLogo.empty())if(auto logo=gpu::g_ui_image(root/("base-logos/"+card.teamLogo+".png")))d->AddImage((ImTextureID)logo,p(352,82),p(520,250),{0,0},{1,1},tint(255,255,255));
  if(card.portrait && gpu::g_ui_guest_image)if(auto portrait=gpu::g_ui_guest_image(card.portrait))d->AddImage((ImTextureID)portrait,p(12,80),p(232,350),{card.portraitUv[0],card.portraitUv[1]},{card.portraitUv[2],card.portraitUv[3]},tint(255,255,255));
  d->AddQuadFilled(p(280,225),p(width-3,225),p(width-3,347),p(244,347),tint(5,7,9,245));
  region(281,230,width-286,112,460,386,385,100,tint(255,255,255,180));
  auto split=card.name.find_last_of(' ');std::string first=split==std::string::npos?"":card.name.substr(0,split),last=split==std::string::npos?card.name:card.name.substr(split+1);
  float x=300;for(char c:first){char ch[]={c,0};text(UiFont::Prompt,29,x,232,ch,45);x+=Font(UiFont::Prompt)->CalcTextSizeA(29*s,10000,0,ch).x/s+6;}
  text(UiFont::TeamDisplay,94,293,253,last,268);
  frame(0,366,width,454);region(3,369,width-6,448,1080,390,132,100,tint(255,255,255,90));
  text(UiFont::Heading,42,24,376,"STAMINA",260);text(UiFont::Heading,48,395,370,card.stamina,165);
  int current=std::atoi(card.stamina.c_str()),maximum=current;auto slash=card.stamina.find('/');if(slash!=std::string::npos)maximum=std::atoi(card.stamina.c_str()+slash+1);
  // The broadcast meter shares the pitch ratings' 0-100 scale; the native
  // current/maximum text still shows this pitcher's available stamina.
  float stamina=std::clamp(float(current)/std::max(100,maximum),0.f,1.f);
  d->AddRectFilled(p(24,433),p(width-24,452),tint(65,67,70));d->AddRectFilled(p(24,433),p(24+(width-48)*stamina,452),primary());
  for(int i=0;i<5;++i){float y=465+i*66;d->AddLine(p(15,y),p(width-15,y),tint(110,113,116,160),s);if(card.pitches[i].empty())continue;
    text(UiFont::Heading,40,24,y+8,card.pitches[i],290);text(UiFont::Heading,44,515,y+4,card.ratings[i],51);
    float strength=std::clamp(std::atoi(card.ratings[i].c_str())/100.f,0.f,1.f);
    d->AddRectFilled(p(328,y+20),p(500,y+43),tint(63,65,68));d->AddRectFilled(p(328,y+20),p(328+172*strength,y+43),primary());
  }
  d->AddLine(p(width-35,817),p(width-3,785),primary(),4*s);
  if(dev::Env("MVP06_TRACE_FE")){static double captured=0;if(captured!=at && Clock()-at>.45){captured=at;gpu::g_capture=true;}}
}
}
