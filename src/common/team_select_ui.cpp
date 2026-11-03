#include "core/dev_tools.h"
#include "team_select_ui.h"
#include "overhaul.h"
#include <sstream>
#include "ui_style.h"
#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "core/input.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <fstream>
#include <unordered_map>

namespace game {
namespace {
std::mutex mutex;
std::array<TeamSelectSide, 2> teams;
std::vector<TeamSelectController> controllers;
std::array<bool,4> initialized{};
unsigned generation = 0;
double Clock() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
float Ease(float x) {x = std::clamp(x, 0.f, 1.f);return 1 - std::pow(1-x, 3.f);}
std::filesystem::path Assets() {
  return overhaul::Root();
}
float Grade(const std::string& value) {
  if (value.empty()) return 0;
  float base = value[0]=='A' ? .91f : value[0]=='B' ? .72f : value[0]=='C' ? .53f : value[0]=='D' ? .34f : .15f;
  if (value.find('+') != std::string::npos) base += .055f;
  if (value.find('-') != std::string::npos) base -= .055f;
  return std::clamp(base, 0.f, 1.f);
}
}
std::array<TeamSelectSide,2> SnapshotTeamSelect() {std::lock_guard lock(mutex);return teams;}
void ResetTeamSelect() {
  std::lock_guard lock(mutex);
  teams = {};
  controllers.clear();initialized.fill(false);
  ++generation;
}
void MapTeamSelectPad(int port, input::Pad& pad, double elapsed) {
  if(port<0 || port>=4 || !pad.connected)return;
  bool ready;
  {std::lock_guard lock(mutex);ready=initialized[port];}
  if(!ready) {
    int lane=NativeTeamControllerLane(port);
    pad.buttons=0;pad.lx=pad.ly=0;for(auto& a:pad.analog)a=0;
    if(elapsed<.3 || lane==-2)return;
    if(lane==-1){std::lock_guard lock(mutex);initialized[port]=true;}
    else pad.ly=lane==1?32767:-32767;
    return;
  }
  auto buttons=pad.buttons;
  int lane=NativeTeamControllerLane(port);
  // Left/right already drives the native jersey and difficulty carousels.
  // Only the team/controller selection layout needs its axes rotated.
  {std::lock_guard lock(mutex);
    if(lane>=0 && lane<2 && teams[lane].step!=TeamSelectSide::Step::Team)return;
  }
  pad.buttons &= ~uint16_t(15);
  // Native horizontal directions choose teams; its vertical stick moves the
  // player between away, neutral and home. Rotate that input to our layout.
  if(buttons&1)pad.buttons|=4;
  if(buttons&2)pad.buttons|=8;
  auto x=pad.lx,y=pad.ly;
  pad.lx=int16_t(std::clamp(-int(y),-32767,32767));
  pad.ly=int16_t(std::clamp(-int(x),-32767,32767));
  if((buttons&12)==4)pad.ly=32767;
  if((buttons&12)==8)pad.ly=-32767;
}
void PublishTeamSelect(const std::array<TeamSelectSide, 2>& value, const std::vector<TeamSelectController>& players) {
  std::lock_guard lock(mutex);
  auto old=teams;
  teams = value;
  for(int i=0;i<2;++i)if(old[i].name==teams[i].name && teams[i].step!=TeamSelectSide::Step::Team){
    if(teams[i].uniform.empty())teams[i].uniform=old[i].uniform;
    if(teams[i].uniformPreview.empty())teams[i].uniformPreview=old[i].uniformPreview;
    if(teams[i].difficulty.empty())teams[i].difficulty=old[i].difficulty;
    for(int j=0;j<5;++j)if(teams[i].grades[j].empty())teams[i].grades[j]=old[i].grades[j];
  }
  controllers=players;
  for(auto& player:controllers)if(player.label.size()==2 && player.label[1]>='1' && player.label[1]<='4' && !initialized[player.label[1]-'1'])player.lane=-1;
  if (dev::Env("MVP06_TRACE_FE")) {
    std::string key;
    for (const auto& team : teams) {
      key += team.name + team.nickname + team.conference + team.controller+std::to_string(int(team.step))+team.uniform+team.difficulty;
      for (const auto& grade : team.grades) key += grade;
    }
    for(const auto& player:controllers)key+=player.label+std::to_string(player.lane);
    static std::string last;
    if (key != last) {
      last = key;
      for(const auto& player:controllers)LOGR("team controller assignment: %s lane=%d",player.label.c_str(),player.lane);
      for (int i=0;i<2;++i) LOGR("team select native: %s %s / %s / %s / %s grades=%s,%s,%s,%s,%s", i==0?"away":"home", teams[i].name.c_str(),teams[i].nickname.c_str(),teams[i].conference.c_str(),teams[i].controller.c_str(),teams[i].grades[0].c_str(),teams[i].grades[1].c_str(),teams[i].grades[2].c_str(),teams[i].grades[3].c_str(),teams[i].grades[4].c_str());
      for(int i=0;i<2;++i)LOGR("team setup: side=%d step=%d uniform=%s difficulty=%s description=%s",i,int(teams[i].step),teams[i].uniform.c_str(),teams[i].difficulty.c_str(),teams[i].difficultyDescription.c_str());
    }
  }
}
void DrawTeamSelect(double elapsed) {
  std::array<TeamSelectSide,2> state;
  std::vector<TeamSelectController> players;
  unsigned version;
  {std::lock_guard lock(mutex);state=teams;players=controllers;version=generation;}
  static auto root=Assets();
  static const auto logos=[&]{std::unordered_map<std::string,std::string> result;std::istringstream file(overhaul::Text(root/"base-logos/teams.tsv"));std::string line;while(std::getline(file,line)){auto tab=line.find('\t');if(tab!=std::string::npos){auto id=line.substr(tab+1);if(!id.empty() && id.back()=='\r')id.pop_back();result[line.substr(0,tab)]=id;}}return result;}();
  if (root.empty() || !gpu::g_ui_image) return;
  auto* d=ImGui::GetBackgroundDrawList();
  auto screen=ImGui::GetIO().DisplaySize;
  float s=std::min(screen.x/2560.f,screen.y/1440.f),ox=(screen.x-2560*s)/2,oy=(screen.y-1440*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto color=[](int r,int g,int b,int a=255){return IM_COL32(r,g,b,a);};
  static const auto palettes=[&]{
    std::unordered_map<std::string,ImVec4> result;
    std::istringstream file(overhaul::Text(root/"team-colors.tsv"));std::string line;
    while(std::getline(file,line)){
      auto tab=line.find('\t');if(tab==std::string::npos)continue;
      char* end=nullptr;auto rgb=std::strtoul(line.c_str()+tab+1,&end,16);
      if(end==line.c_str()+tab+1)continue;
      result[line.substr(0,tab)]=ImVec4(float((rgb>>16)&255),float((rgb>>8)&255),float(rgb&255),255);
    }
    return result;
  }();
  static std::array<ImVec4,2> panelColors{};
  static unsigned paletteVersion=~0u;
  for(int side=0;side<2;++side){
    auto found=palettes.find(state[side].name);
    auto target=found==palettes.end()?ImVec4(85,110,135,255):found->second;
    auto& current=panelColors[side];float blend=paletteVersion!=version?1.f:std::min(1.f,ImGui::GetIO().DeltaTime*9);
    current.x+=(target.x-current.x)*blend;current.y+=(target.y-current.y)*blend;current.z+=(target.z-current.z)*blend;
  }
  paletteVersion=version;
  auto teamColor=[&](int side,float strength,int alpha,float lift=0.f){
    auto c=panelColors[side];return color(int(c.x*strength+lift),int(c.y*strength+lift),int(c.z*strength+lift),alpha);
  };
  auto image=[&](const char* file,float x,float y,float w,float h,ImU32 tint=IM_COL32_WHITE,ImVec2 uv0=ImVec2(0,0),ImVec2 uv1=ImVec2(1,1)) {
    if(auto tex=gpu::g_ui_image(root/file))d->AddImage((ImTextureID)tex,p(x,y),p(x+w,y+h),uv0,uv1,tint);
  };
  auto text=[&](UiFont role,float size,float x,float y,const std::string& value,ImU32 tint,float width=0){UiText(d,role,size*s,p(x,y),tint,value.c_str(),width*s);};
  auto center=[&](UiFont role,float size,float x,float y,const std::string& value,ImU32 tint,float width){
    auto* font=Font(role);float actual=size;
    float w=font->CalcTextSizeA(actual*s,1e6f,0,value.c_str()).x/s;
    if(w>width){actual*=width/w;w=width;}
    text(role,actual,x-w/2,y,value,tint,width);
  };
  auto arrow=[&](float x,float y,float dir,ImU32 tint){ImVec2 q[]={p(x-dir*10,y-15),p(x+dir*3,y),p(x-dir*10,y+15)};d->AddPolyline(q,3,tint,false,4*s);};
  float entry=Ease(float(elapsed/.4));
  image("team-select-stadium-imagegen.png",0,0,2560,1440);
  d->AddRectFilled(p(0,0),p(2560,1440),color(2,5,9,65));
  const ImU32 accents[]={teamColor(0,.65f,255,85),teamColor(1,.65f,255,85)};
  for(int i=0;i<2;++i){
    float slide=(i?1.f:-1.f)*180*(1-entry);
    // A continuous mesh shades the glass without banding or overlapping seams.
    auto panelPoint=[&](float u,float v){
      float left=i?1490-215*v:430-540*v;
      float right=i?2670-220*v:1180-215*v;
      return p(left+(right-left)*u+slide,160+1085*v);
    };
    constexpr int cols=24,rows=32;
    const ImVec2 white=ImGui::GetFontTexUvWhitePixel();
    d->PrimReserve(cols*rows*6,(cols+1)*(rows+1));
    const unsigned base=d->_VtxCurrentIdx;
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){
      unsigned a=base+y*(cols+1)+x,b=a+cols+1;
      d->PrimWriteIdx(ImDrawIdx(a));d->PrimWriteIdx(ImDrawIdx(a+1));d->PrimWriteIdx(ImDrawIdx(b+1));
      d->PrimWriteIdx(ImDrawIdx(a));d->PrimWriteIdx(ImDrawIdx(b+1));d->PrimWriteIdx(ImDrawIdx(b));
    }
    for(int y=0;y<=rows;++y)for(int x=0;x<=cols;++x){
      float u=float(x)/cols,v=float(y)/rows;
      float light=std::exp(-((u-.5f)*(u-.5f)*9+(v-.22f)*(v-.22f)*12));
      float fade=1-.72f*v;
      ImU32 tint=teamColor(i,.30f+.40f*light,int(160*fade),5);
      d->PrimWriteVtx(panelPoint(u,v),white,tint);
    }
    // Restrained diagonal pinstripes and a narrow illuminated outer rail.
    for(int n=1;n<22;++n){
      float u=float(n)/22;
      d->AddLine(panelPoint(u,0),panelPoint(u,1),color(220,235,245,7),s);
    }
    float edge=i?.94f:.06f;
    ImVec2 rail[]={panelPoint(edge,0),panelPoint(edge+.012f,0),panelPoint(edge+.012f,1),panelPoint(edge,1)};
    d->AddConvexPolyFilled(rail,4,teamColor(i,.65f,30,85));
    d->AddLine(panelPoint(edge,0),panelPoint(edge,1),teamColor(i,.65f,120,85),1.5f*s);
    float inner=i?0.f:1.f;
    d->AddLine(panelPoint(inner,0),panelPoint(inner,1),color(205,225,240,75),s);
    d->AddLine(panelPoint(0,0),panelPoint(1,0),color(225,235,245,65),s);
  }
  ImVec2 middle[]={p(1180,160),p(1490,160),p(1275,1245),p(965,1245)};
  d->AddConvexPolyFilled(middle,4,color(0,2,5,170));
  d->AddRectFilledMultiColor(p(0,650),p(2560,1245),color(0,0,0,0),color(0,0,0,0),color(0,0,0,220),color(0,0,0,220));
  d->AddRectFilled(p(0,0),p(2560,160),color(0,2,5,205));
  d->AddLine(p(0,160),p(2560,160),color(210,218,225,180),2*s);
  image("team-select-logo.png",210,10,620,150,IM_COL32_WHITE,{.025f,.12f},{.98f,.9f});
  d->AddLine(p(960,45),p(960,123),color(220,225,230,190),2*s);
  text(UiFont::Heading,58,1060,33,"EXHIBITION",color(230,233,235));
  text(UiFont::Prompt,34,1070,100,"T E A M   S E L E C T",color(160,172,182));
  text(UiFont::TeamVersus,180,1130,565,"VS",color(240,240,237));
  d->AddQuadFilled(p(1190,745),p(1320,735),p(1315,747),p(1185,757),color(205,15,22));
  static std::array<TeamSelectSide,2> previous,displayed;
  static std::array<double,2> changed{};
  static std::array<std::array<float,5>,2> bars{};
  static unsigned lastVersion=~0u;
  if(lastVersion!=version){lastVersion=version;displayed={};previous={};bars={};changed={};}
  bool setup=std::any_of(state.begin(),state.end(),[](const auto& team){return team.step!=TeamSelectSide::Step::Team;});
  for(int side=0;side<2;++side){
    auto& team=state[side];float cx=side?1840:710;
    bool choosing=team.step==TeamSelectSide::Step::Team;
    if(choosing)if(auto it=logos.find(team.name);it!=logos.end()){
      auto path="base-logos/"+it->second+".png";
      image(path.c_str(),cx-215,235,430,430,IM_COL32_WHITE);
    }
    if(!choosing){
      bool uniform=team.step==TeamSelectSide::Step::Uniform;
      bool ready=team.step==TeamSelectSide::Step::Ready;
      center(UiFont::Prompt,28,cx,210,ready?"MATCH SETUP COMPLETE":uniform?"01  /  SELECT UNIFORM":"02  /  SELECT DIFFICULTY",accents[side],700);
      if(uniform && !team.uniformPreview.empty()){
        auto path="uniforms/"+team.uniformPreview+".png";
        image(path.c_str(),cx-175,252,350,350);
      }
      float top=uniform?600.f:330.f;
      d->AddRectFilled(p(cx-340,top),p(cx+340,top+74),color(3,8,14,220),5*s);
      d->AddLine(p(cx-340,top+74),p(cx+340,top+74),accents[side],2*s);
      center(UiFont::Heading,48,cx,top+10,ready?"READY":uniform?team.uniform:team.difficulty,color(245,247,249),540);
      if(!ready){arrow(cx-300,top+37,-1,accents[side]);arrow(cx+300,top+37,1,accents[side]);}
      if(team.step==TeamSelectSide::Step::Difficulty){
        auto* font=Font(UiFont::Prompt);
        auto size=font->CalcTextSizeA(34*s,1e6f,600*s,team.difficultyDescription.c_str());
        d->AddText(font,34*s,p(cx-size.x/(2*s),455),color(215,225,233),team.difficultyDescription.c_str(),nullptr,600*s);
        center(UiFont::Prompt,25,cx,610,"A  CONFIRM     B  RETURN TO UNIFORM",accents[side],680);
      }
      if(ready){
        center(UiFont::Prompt,34,cx,460,team.uniform,color(225,233,240),650);
        center(UiFont::Prompt,34,cx,515,team.difficulty,color(225,233,240),650);
        center(UiFont::Prompt,26,cx,610,"WAITING FOR OTHER PLAYERS",accents[side],650);
      }
    }
    if(displayed[side].name!=team.name){previous[side]=displayed[side];displayed[side]=team;changed[side]=Clock();}
    float a=Ease(float((Clock()-changed[side])/.28));
    if(!previous[side].name.empty() && a<1)center(UiFont::TeamDisplay,94,cx-25*a,690,previous[side].name,color(240,241,240,int(255*(1-a))),970);
    center(UiFont::TeamDisplay,94,cx+25*(1-a),690,team.name,color(248,248,245,int(255*a*entry)),970);
    center(UiFont::Prompt,40,cx,795,team.nickname,accents[side],900);
    center(UiFont::Heading,36,cx,850,team.conference,color(235,238,240),600);
    d->AddLine(p(cx-350,875),p(cx-160,875),color(200,210,215,110),s);
    d->AddLine(p(cx+160,875),p(cx+350,875),color(200,210,215,110),s);
    if(!team.rank.empty())text(UiFont::Heading,43,cx-330,235,team.rank,color(245,245,245),150);
    float x=cx-360,top=1035;
    d->AddRectFilled(p(x,top),p(x+720,top+230),color(1,3,6,195));
    d->AddRect(p(x,top),p(x+720,top+230),color(200,210,215,160),0,0,2*s);
    const char* labels[]={"OVERALL","PITCHING","HITTING","FIELDING","SPEED"};
    for(int j=0;j<5;++j){
      float y=top+7+j*44;
      if(j)d->AddLine(p(x+15,y-4),p(x+705,y-4),color(180,195,205,70),s);
      text(UiFont::Prompt,32,x+25,y,labels[j],color(228,232,236));
      text(UiFont::Heading,39,x+242,y-3,team.grades[j],color(245,246,244),100);
      float target=Grade(team.grades[j]);
      bars[side][j]+=(target-bars[side][j])*std::min(1.f,ImGui::GetIO().DeltaTime*12);
      d->AddRectFilled(p(x+350,y+11),p(x+675,y+35),color(160,175,185,85));
      d->AddRectFilledMultiColor(p(x+350,y+11),p(x+350+325*bars[side][j],y+35),accents[side],accents[side],teamColor(side,.45f,255,15),teamColor(side,.45f,255,15));
    }
    bool human=std::any_of(players.begin(),players.end(),[&](const auto& player){return player.lane==side;});
    center(UiFont::Prompt,24,cx,898,human?"PLAYER":"CPU",color(205,215,225),300);
    if(human && choosing) {
      for(int dir:{-1,1}){float y=dir<0?710:775;ImVec2 q[]={p(cx+360-14,y-dir*8),p(cx+360,y+dir*5),p(cx+360+14,y-dir*8)};d->AddPolyline(q,3,accents[side],false,4*s);}
    }
  }
  static std::array<float,6> playerX{};static unsigned playerVersion=~0u;
  if(playerVersion!=version){playerVersion=version;playerX.fill(1272);}
  d->AddLine(p(710,970),p(1840,970),color(200,215,225,70),s);
  center(UiFont::Prompt,24,1272,898,setup?"MATCH SETUP":"CHOOSE SIDE",color(200,215,225),240);
  for(size_t i=0;i<players.size() && i<playerX.size();++i){
    int count=0,slot=0;
    for(size_t j=0;j<players.size();++j)if(players[j].lane==players[i].lane){++count;if(j<i)++slot;}
    float target=(players[i].lane==0?710.f:players[i].lane==1?1840.f:1272.f)+(slot-(count-1)*.5f)*175;
    playerX[i]+=(target-playerX[i])*std::min(1.f,ImGui::GetIO().DeltaTime*14);
    float x=playerX[i],y=918;
    d->AddRectFilled(p(x-82,y),p(x+82,y+108),color(0,2,5,215),8*s);
    d->AddRect(p(x-82,y),p(x+82,y+108),color(220,230,235,220),8*s,0,2*s);
    image("team-select-controller-cutout.png",x-59,y+3,118,83);
    center(UiFont::Prompt,25,x,y+82,players[i].label,color(245,245,245),100);
    if(players[i].lane<0 || state[players[i].lane].step==TeamSelectSide::Step::Team){arrow(x-72,y+45,-1,color(220,230,235));arrow(x+72,y+45,1,color(220,230,235));}
  }
  d->AddRectFilled(p(0,1290),p(2560,1400),color(0,2,5,230));
  d->AddLine(p(0,1290),p(2560,1290),color(220,224,228,150),2*s);
  d->AddLine(p(0,1400),p(2560,1400),color(220,224,228,150),s);
  image("controller-reference-b.png",185,1317,58,58);text(UiFont::Prompt,42,265,1322,"BACK",color(242,245,247));
  if(!setup){image("controller-reference-x.png",475,1317,58,58);text(UiFont::Prompt,42,555,1322,"RANDOM",color(242,245,247));}
  image("controller-reference-a.png",2070,1317,58,58);text(UiFont::Prompt,42,2150,1322,"ADVANCE",color(242,245,247));
  text(UiFont::Prompt,28,setup?930:1050,1335,setup?"LEFT / RIGHT: CHANGE     A: CONFIRM     B: BACK":"LEFT / RIGHT: SIDE     UP / DOWN: TEAM",color(180,195,205),1000);
  if(dev::Env("MVP06_TRACE_FE")){
    static std::string captured;
    std::string key=std::to_string(version);
    for(const auto& team:state)key+=team.name+team.controller+std::to_string(int(team.step))+team.uniform+team.difficulty+team.uniformPreview;
    if(elapsed>1 && Clock()-std::max(changed[0],changed[1])>.4 && key!=captured){captured=key;gpu::g_capture=true;}
  }
}
}
