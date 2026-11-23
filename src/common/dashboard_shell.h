#pragma once
#include "imgui.h"
#include <string>
namespace game {
struct DynastyHomeCard;
struct TeamSelectSide;
void PublishDashboardShell(const DynastyHomeCard&);
bool DrawDashboardShell(ImDrawList*,const char* title,int section,const TeamSelectSide* fallback=nullptr,unsigned color=0,const std::string& date={});
// Maps each existing content view into the shared dashboard's content area.
// Controls keep operating on native calendar/bracket state and indices.
struct DashboardPageFrame {
 bool active=false;
 float scale=1,ox=0,oy=0,sx=1,sy=1,tx=0,ty=0;
 DashboardPageFrame(ImVec2 screen,bool enabled);
 ImVec2 Point(float x,float y)const{return {ox+(tx+x*sx)*scale,oy+(ty+y*sy)*scale};}
 float TextScale()const;
 void Content(float x,float y,float w,float h,float targetY=155,float targetH=700);
 void Footer(float sourceY=850);
};
}
