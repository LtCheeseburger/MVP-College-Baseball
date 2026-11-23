#include "dashboard_shell.h"
#include "team_select_ui.h"
#include "overhaul.h"
#include "ui_style.h"
#include "gpu/backend.h"
#include <algorithm>
#include <cctype>
#include <mutex>
namespace game {
namespace {
std::mutex shellMutex;
DynastyHomeCard shellCard;
}
void PublishDashboardShell(const DynastyHomeCard& card){std::lock_guard lock(shellMutex);shellCard=card;}
DashboardPageFrame::DashboardPageFrame(ImVec2 screen,bool enabled):active(enabled){scale=std::min(screen.x/1672.f,screen.y/941.f);ox=(screen.x-1672*scale)/2;oy=(screen.y-941*scale)/2;}
float DashboardPageFrame::TextScale()const{return std::min(sx,sy);}
void DashboardPageFrame::Content(float x,float y,float w,float h,float targetY,float targetH){if(!active)return;sx=1336.f/w;sy=targetH/h;tx=321-x*sx;ty=targetY-y*sy;}
void DashboardPageFrame::Footer(float sourceY){if(!active)return;sx=1;sy=58.f/(941-sourceY);tx=0;ty=883-sourceY*sy;}
bool DrawDashboardShell(ImDrawList* d,const char* title,int section,const TeamSelectSide* fallback,unsigned color,const std::string& date){
 if(!gpu::g_ui_image)return false;
 const auto root=overhaul::Root();auto skin=gpu::g_ui_image(overhaul::UiAsset(root/"dynasty-dashboard-chrome-v1.png"));if(!skin)return false;
 DynastyHomeCard card;{std::lock_guard lock(shellMutex);card=shellCard;}
 if(card.school.team.name.empty()&&fallback)card.school.team=*fallback;
 if(color)card.school.color=color;
 if(card.school.color==0)card.school.color=0xA94B08;
 DashboardPageFrame frame(ImGui::GetIO().DisplaySize,false);auto p=[&](float x,float y){return frame.Point(x,y);};const float s=frame.scale;
 d->AddRectFilled({0,0},ImGui::GetIO().DisplaySize,IM_COL32(3,6,8,255));d->AddImage((ImTextureID)skin,p(0,0),p(1672,941));
 UiBrandLogo(d,p(49,13),p(278,89));
 auto text=[&](UiFont font,const std::string& value,float x,float y,float size,float width,ImU32 tint=IM_COL32_WHITE){UiText(d,font,size*s,p(x,y),tint,value.c_str(),width*s);};
 const auto muted=IM_COL32(183,190,196,255);text(UiFont::Prompt,std::string("DYNASTY / ")+title,329,18,16,550,muted);text(UiFont::Heading,title,329,41,46,650);
 text(UiFont::Prompt,date.empty()?card.date:date,1118,44,20,280);
 auto logo=overhaul::TeamLogo(card.school.team.name,card.school.team.nickname);
 if(!logo.empty())if(auto t=gpu::g_ui_image(overhaul::UiAsset(logo)))d->AddImage((ImTextureID)t,p(1410,43),p(1450,82));
 text(UiFont::Heading,card.school.team.name,1460,45,20,174);text(UiFont::Prompt,"HEAD COACH",1460,70,13,174,muted);
 std::string competition=card.menu.size()>1?card.menu[1]:"RECRUITING";
 std::transform(competition.begin(),competition.end(),competition.begin(),[](unsigned char c){return char(std::toupper(c));});
 const std::string labels[]={"OVERVIEW","SCHEDULE",competition,"STATISTICS","TEAM MANAGEMENT","OPTIONS"};
 const unsigned c=card.school.color;const auto accent=IM_COL32((c>>16)&255,(c>>8)&255,c&255,235);
 for(int i=0;i<6;++i){const float y=i?221.f+(i-1)*74.5f:145.f;
  if(i==section){d->AddRectFilled(p(0,y-15),p(307,y+47),accent);d->AddRectFilled(p(0,y-15),p(5,y+47),IM_COL32(255,255,255,95));}
  if(auto icon=gpu::g_ui_image(overhaul::UiAsset(root/("dashboard-icon-"+std::to_string(i)+".png"))))d->AddImage((ImTextureID)icon,p(20,y),p(45,y+25));
  text(UiFont::Heading,labels[i],61,y+2,25,234);
 }
 return true;
}
}
