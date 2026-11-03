#include "core/dev_tools.h"
#include "stadium_intro_ui.h"
#include "overhaul.h"
#include <sstream>
#include "ui_style.h"
#include "app/app.h"
#include "gpu/backend.h"
#include "core/log.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace game {
namespace {
std::mutex mutex;
std::array<MatchupSide,2> sides;
std::string venue;
bool visible=false,exiting=false;
double began=0,exitAt=0;
double Clock(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::filesystem::path Assets(){return overhaul::Root();}
}
void StadiumIntroTimeline(const char* name){std::lock_guard lock(mutex);if(!std::strcmp(name,"ENTER")){began=Clock();exiting=false;exitAt=0;}if(!std::strcmp(name,"OVEXIT")){exiting=true;exitAt=Clock();}if(dev::Env("MVP06_TRACE_FE"))LOGR("stadium intro timeline: %s",name);}
void PublishStadiumIntro(const std::array<MatchupSide,2>& teams,const std::string& stadium,bool show){
  std::lock_guard lock(mutex);
  if(show && !visible){began=Clock();exiting=false;exitAt=0;}
  sides=teams;venue=stadium;visible=show;
  if(show && dev::Env("MVP06_TRACE_FE")) {std::string key=venue;for(auto& t:sides)key+=t.name+t.nickname+t.rank;static std::string last;if(key!=last){last=key;LOGR("stadium intro: venue=%s away=%s rank=%s home=%s rank=%s",venue.c_str(),sides[0].name.c_str(),sides[0].rank.c_str(),sides[1].name.c_str(),sides[1].rank.c_str());}}
}
void DrawStadiumIntro(){
  std::array<MatchupSide,2> teams;std::string stadium;double at,out;bool show,exit;
  {std::lock_guard lock(mutex);teams=sides;stadium=venue;at=began;out=exitAt;show=visible;exit=exiting;}
  if(dev::Env("MVP06_TRACE_FE") && out>0 && Clock()-out>.5){static double capturedExit=0;if(capturedExit!=out){capturedExit=out;gpu::g_capture=true;}}
  if(!show || stadium.empty() || teams[0].name.empty() || !gpu::g_ui_image)return;
  float progress=float(std::clamp((Clock()-(exit?out:at))/.266,0.,1.));
  float alpha=exit?1-progress:progress;if(alpha<=0)return;
  float motion=(1-alpha)*(1-alpha);
  static auto root=Assets();
  static auto logos=[&]{std::unordered_map<std::string,std::string> r;std::istringstream f(overhaul::Text(root/"base-logos/teams.tsv"));std::string line;while(std::getline(f,line)){auto tab=line.find('\t');if(tab==std::string::npos)continue;auto id=line.substr(tab+1);if(!id.empty()&&id.back()=='\r')id.pop_back();r[line.substr(0,tab)]=id;}return r;}();
  static auto colors=[&]{std::unordered_map<std::string,unsigned> r;std::istringstream f(overhaul::Text(root/"team-colors.tsv"));std::string line;while(std::getline(f,line)){auto tab=line.find('\t');if(tab!=std::string::npos)r[line.substr(0,tab)]=std::strtoul(line.c_str()+tab+1,nullptr,16);}return r;}();
  auto* d=ImGui::GetBackgroundDrawList();auto screen=ImGui::GetIO().DisplaySize;
  float base=std::min(screen.x/2560.f,screen.y/1440.f);
  constexpr float compact=.88f;
  // Uniformly shrink around the lower-left anchor; the baseline and margins
  // stay fixed while lettering, artwork, borders and rank badges scale together.
  float scale=base*compact;
  float ox=(screen.x-2560*base)/2+180*base*(1-compact);
  float oy=(screen.y-1440*base)/2+1284*base*(1-compact);
  auto p=[&](float x,float y){return ImVec2(ox+(x-110*motion)*scale,oy+y*scale);};
  auto tint=[&](int r,int g,int b,int a=255){return IM_COL32(r,g,b,int(a*alpha));};
  auto texture=gpu::g_ui_image(root/"stadium-intro-atlas.png");
  auto panel=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color){
    ImVec2 q[]={p(x+18,y),p(x+w,y),p(x+w,y+h-18),p(x+w-18,y+h),p(x,y+h),p(x,y+18)};
    d->AddConvexPolyFilled(q,6,tint(8,10,13,248));
    // Texture only the interior; the atlas's checkerboard gutters and corner
    // cutouts never enter the sampling rectangle.
    if(texture)d->AddImageQuad((ImTextureID)texture,p(x+18,y+3),p(x+w-3,y+3),p(x+w-18,y+h-3),p(x+3,y+h-3),{u/1774,v/887},{(u+uw)/1774,v/887},{(u+uw)/1774,(v+vh)/887},{u/1774,(v+vh)/887},color);
    d->AddPolyline(q,6,tint(191,194,195),true,2*scale);
    d->AddLine(p(x+20,y+4),p(x+w-4,y+4),tint(245,245,245,120),scale);
  };
  auto text=[&](UiFont font,float size,float x,float y,ImU32 color,const std::string& label,float max){UiText(d,font,size*scale,p(x,y),color,label.c_str(),max*scale);};
  constexpr float left=180,top=840,width=1220,header=78,row=174;
  panel(left,top,width,header,85,72,1390,78,tint(255,255,255));
  d->AddRectFilled(p(left+32,top+15),p(left+43,top+header-15),tint(240,5,25));
  std::transform(stadium.begin(),stadium.end(),stadium.begin(),[](unsigned char c){return char(std::toupper(c));});
  text(UiFont::Heading,48,left+65,top+9,tint(245,245,245),stadium,width-110);
  for(int i=0;i<2;++i){
    float y=top+header+10+i*(row+8);
    auto it=colors.find(teams[i].name);unsigned rgb=it==colors.end()?(i?0xC35A10:0x004AC0):it->second;
    auto primary=[&](float strength,int a=255){return tint(int(((rgb>>16)&255)*strength),int(((rgb>>8)&255)*strength),int((rgb&255)*strength),a);};
    panel(left,y,width,row,175,430,1200,180,primary(1));
    // Low-contrast team watermark sits behind the text, separate from the logo.
    auto logo=logos.find(teams[i].name);
    if(logo!=logos.end())if(auto tex=gpu::g_ui_image(root/("base-logos/"+logo->second+".png"))){
      d->AddImage((ImTextureID)tex,p(left+width-390,y-10),p(left+width-80,y+row+10),{0,0},{1,1},primary(.9f,32));
      d->AddImage((ImTextureID)tex,p(left+95,y+9),p(left+250,y+164),{0,0},{1,1},tint(255,255,255));
    }
    // Colored slash accents echo the supplied reference without tinting borders.
    d->AddQuadFilled(p(left+53,y+5),p(left+79,y+5),p(left+18,y+row-5),p(left+5,y+row-5),tint(i?245:255,i?123:108,i?25:6));
    d->AddQuadFilled(p(left+width-33,y+5),p(left+width-21,y+5),p(left+width-77,y+row-5),p(left+width-89,y+row-5),primary(1));
    text(UiFont::Navigation,94,left+330,y+14,tint(255,255,255),teams[i].name,710);
    float x=left+340;
    for(char c:teams[i].nickname){char ch[]={c,0};text(UiFont::Prompt,32,x,y+116,tint(237,239,241),ch,60);x+=Font(UiFont::Prompt)->CalcTextSizeA(32*scale,10000,0,ch).x/scale+8;}
    if(!teams[i].rank.empty()){
      std::string rank=teams[i].rank=="0"?"NR":"#"+teams[i].rank;
      d->AddRectFilled(p(left+width-145,y+26),p(left+width-35,y+83),tint(0,0,0,165));
      text(UiFont::Heading,42,left+width-132,y+29,tint(250,250,250),rank,92);
    }
  }
  if(dev::Env("MVP06_TRACE_FE")){static double captured=0;if(captured!=at && Clock()-at>.35){captured=at;gpu::g_capture=true;}}
}
}
