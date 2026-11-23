#include "core/dev_tools.h"
#include "mvp_transition_ui.h"
#include "ui_style.h"
#include "overhaul.h"
#include "app/app.h"
#include "gpu/backend.h"
#include "core/log.h"
#include "imgui.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace game {
namespace {
std::atomic<double> started{0};
double Now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::filesystem::path Assets(){return overhaul::Root();}
float Ease(double v){float t=float(std::clamp(v,0.,1.));return t*t*(3-2*t);}
}
void BeginMvpTransition(){started=Now();if(dev::Env("MVP06_TRACE_FE"))LOGR("MVP27 transition: begin");}
void EndMvpTransition(){if(started.exchange(0)>0 && dev::Env("MVP06_TRACE_FE"))LOGR("MVP27 transition: native handoff");}
void DrawMvpTransition(){
  double at=started.load();if(!at)return;
  double t=Now()-at;if(t>1.65)return;
  static auto root=Assets();if(!gpu::g_ui_image)return;
  auto* d=ImGui::GetBackgroundDrawList();auto screen=ImGui::GetIO().DisplaySize;
  float s=std::min(screen.x/2560.f,screen.y/1440.f),ox=(screen.x-2560*s)/2,oy=(screen.y-1440*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  float close=Ease(t/.22),open=Ease((t-1.24)/.36);
  float offset=(1-close)*1650-open*3200;
  auto atlas=gpu::g_ui_image(root/"matchup-texture-atlas.png");
  auto quad=[&](float shift,ImU32 color){d->AddQuadFilled(p(-700+shift,0),p(3200+shift,0),p(2800+shift,1440),p(-1100+shift,1440),color);};
  quad(offset,IM_COL32(5,6,8,255));
  if(atlas)d->AddImageQuad((ImTextureID)atlas,p(-700+offset,0),p(3200+offset,0),p(2800+offset,1440),p(-1100+offset,1440),{345.f/1671,18.f/941},{800.f/1671,18.f/941},{800.f/1671,305.f/941},{345.f/1671,305.f/941},IM_COL32(150,150,150,115));
  for(int i=0;i<3;++i){float x=140+i*105+offset;d->AddQuadFilled(p(x,0),p(x+62,0),p(x-338,1440),p(x-400,1440),IM_COL32(i==1?245:110,0,20,255));}
  d->AddQuadFilled(p(2390+offset,0),p(2420+offset,0),p(2020+offset,1440),p(1990+offset,1440),IM_COL32(220,0,25,255));
  float logoAlpha=Ease((t-.12)/.2)*(1-Ease((t-1.20)/.22));
  float zoom=.94f+.06f*Ease((t-.1)/.7);
  float w=1480*zoom,h=490*zoom,cx=1280+offset*.20f,cy=690;
  UiBrandLogo(d,p(cx-w/2,cy-h/2),p(cx+w/2,cy+h/2),IM_COL32(255,255,255,int(logoAlpha*255)));
  d->AddRectFilled(p(cx-270,985),p(cx+270,994),IM_COL32(225,0,25,int(logoAlpha*255)));
  if(dev::Env("MVP06_TRACE_FE")){static double last=0;static int stage=0;if(last!=at){last=at;stage=0;}if((stage==0&&t>.48)||(stage==1&&t>1.40)){stage++;gpu::g_capture=true;}}
}
}
