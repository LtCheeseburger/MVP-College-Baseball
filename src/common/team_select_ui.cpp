#include "roster_browser_ui.h"
#include "core/dev_tools.h"
#include "team_select_ui.h"
#include "overhaul.h"
#include <sstream>
#include "ui_style.h"
#include "dashboard_shell.h"
#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "core/input.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <fstream>
#include <unordered_map>

namespace game {
namespace {
std::mutex mutex;
std::array<TeamSelectSide, 2> teams;
std::vector<TeamSelectController> controllers;
std::string teamSelectTitle="TEAM SELECT";
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
void PublishTeamSelect(const std::array<TeamSelectSide, 2>& value, const std::vector<TeamSelectController>& players, const std::string& title) {
  std::lock_guard lock(mutex);
  teamSelectTitle=title.empty()?"TEAM SELECT":title;
  auto old=teams;
  teams = value;
  for(int i=0;i<2;++i)if(old[i].name==teams[i].name && teams[i].step!=TeamSelectSide::Step::Team){
    if(teams[i].uniform.empty())teams[i].uniform=old[i].uniform;
    if(teams[i].uniformParts[1].empty()) {
      teams[i].uniformParts=old[i].uniformParts;
      teams[i].uniformRow=old[i].uniformRow;
    }
    if(teams[i].uniformPreview.empty())teams[i].uniformPreview=old[i].uniformPreview;
    if(teams[i].difficulty.empty())teams[i].difficulty=old[i].difficulty;
    for(int j=0;j<5;++j)if(teams[i].grades[j].empty())teams[i].grades[j]=old[i].grades[j];
  }
  controllers=players;
  for(auto& player:controllers)if(player.label.size()==2 && player.label[1]>='1' && player.label[1]<='4' && !initialized[player.label[1]-'1'])player.lane=-1;
  if (dev::Env("MVP06_TRACE_FE")) {
    std::string key;
    for (const auto& team : teams) {
      key += team.name + team.nickname + team.conference + team.rank + team.controller+std::to_string(int(team.step))+team.uniform+team.difficulty;
      for (const auto& grade : team.grades) key += grade;
    }
    for(const auto& player:controllers)key+=player.label+std::to_string(player.lane);
    static std::string last;
    if (key != last) {
      last = key;
      for(const auto& player:controllers)LOGR("team controller assignment: %s lane=%d",player.label.c_str(),player.lane);
      for (int i=0;i<2;++i) LOGR("team select native: %s %s / %s / %s / %s grades=%s,%s,%s,%s,%s rank=%s", i==0?"away":"home", teams[i].name.c_str(),teams[i].nickname.c_str(),teams[i].conference.c_str(),teams[i].controller.c_str(),teams[i].grades[0].c_str(),teams[i].grades[1].c_str(),teams[i].grades[2].c_str(),teams[i].grades[3].c_str(),teams[i].grades[4].c_str(),teams[i].rank.c_str());
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
    if(auto tex=gpu::g_ui_image(overhaul::UiAsset(root/file)))d->AddImage((ImTextureID)tex,p(x,y),p(x+w,y+h),uv0,uv1,tint);
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
  UiMenuBackdrop(d,ImGui::GetIO().DisplaySize,UiBackdrop::Graphite);
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
  UiBrandLogo(d,p(210,10),p(830,160));
  d->AddLine(p(960,45),p(960,123),color(220,225,230,190),2*s);
  std::string contextTitle;{std::lock_guard lock(mutex);contextTitle=teamSelectTitle;}
  text(UiFont::Heading,58,1060,33,contextTitle,color(230,233,235),1350);
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
      bool components=uniform && !team.uniformParts[1].empty();
      bool ready=team.step==TeamSelectSide::Step::Ready;
      center(UiFont::Prompt,28,cx,210,ready?"MATCH SETUP COMPLETE":uniform?"01  /  SELECT UNIFORM":"02  /  SELECT DIFFICULTY",accents[side],700);
      if(components){
        UiBroadcastPanel(d,p(cx-350,285),p(cx-70,585),s);
        center(UiFont::Prompt,23,cx-210,300,"JERSEY PREVIEW",color(205,215,225),250);
        if(!team.uniformPreview.empty())if(auto jersey=gpu::g_ui_image(overhaul::UiAsset(root/("uniforms/"+team.uniformPreview+".png"))))
          d->AddImage((ImTextureID)jersey,p(cx-325,345),p(cx-95,570),{0,.12f},{1,1});
        for(int row=0;row<4;++row){
          float y=285+row*77.f;
          bool focused=row==team.uniformRow;
          auto lo=p(cx-45,y),hi=p(cx+350,y+68);
          UiBroadcastPanel(d,lo,hi,s);
          if(focused){d->AddRectFilled(lo,hi,color(155,12,27,135),4*s);d->AddRect(lo,hi,accents[side],4*s,0,2*s);}
          const auto& label=team.uniformParts[row];
          auto split=label.find(": ");
          text(UiFont::Prompt,22,cx-25,y+5,split==std::string::npos?"":label.substr(0,split),color(180,195,205),325);
          text(UiFont::Heading,30,cx-25,y+29,split==std::string::npos?label:label.substr(split+2),color(245,247,249),325);
          if(focused){arrow(cx-52,y+34,-1,accents[side]);arrow(cx+363,y+34,1,accents[side]);}
        }
        center(UiFont::Prompt,23,cx,610,"UP / DOWN: PART    LEFT / RIGHT: CHANGE",color(220,228,236),720);
        center(UiFont::Prompt,21,cx,642,"PRESET RESETS ALL PARTS  /  HELMET FOLLOWS HAT",color(180,195,205),720);
      }
      if(uniform && !components && !team.uniformPreview.empty()){
        auto path="uniforms/"+team.uniformPreview+".png";
        UiBroadcastPanel(d,p(cx-215,260),p(cx+215,580),s);
        center(UiFont::Prompt,25,cx,274,"JERSEY DETAIL",color(205,215,225),390);
        // This native asset is a cropped jersey sprite, not a full player.
        // Frame the garment deliberately and omit the partial chin at its top.
        if(auto jersey=gpu::g_ui_image(overhaul::UiAsset(root/path)))
          d->AddImage((ImTextureID)jersey,p(cx-145,318),p(cx+145,570),{0,.12f},{1,1});
      }
      float top=uniform?600.f:330.f;
      if(!components){
      d->AddRectFilled(p(cx-340,top),p(cx+340,top+74),color(3,8,14,220),5*s);
      d->AddLine(p(cx-340,top+74),p(cx+340,top+74),accents[side],2*s);
      center(UiFont::Heading,48,cx,top+10,ready?"READY":uniform?team.uniform:team.difficulty,color(245,247,249),540);
      if(!ready){arrow(cx-300,top+37,-1,accents[side]);arrow(cx+300,top+37,1,accents[side]);}
      }
      if(team.step==TeamSelectSide::Step::Difficulty){
        auto* font=Font(UiFont::Prompt);
        auto size=font->CalcTextSizeA(34*s,1e6f,600*s,team.difficultyDescription.c_str());
        d->AddText(font,34*s,p(cx-size.x/(2*s),455),color(215,225,233),team.difficultyDescription.c_str(),nullptr,600*s);
        UiControllerGlyph(d,p(cx-265,609),p(cx-233,641),"A");
        UiText(d,UiFont::Prompt,25*s,p(cx-222,610),accents[side],"CONFIRM");
        UiControllerGlyph(d,p(cx-70,609),p(cx-38,641),"B");
        UiText(d,UiFont::Prompt,25*s,p(cx-26,610),accents[side],"RETURN TO UNIFORM");
      }
      if(ready){
        if(!team.uniformParts[1].empty()){
          for(int row=1;row<4;++row)center(UiFont::Prompt,30,cx,400+row*45.f,team.uniformParts[row],color(225,233,240),650);
          center(UiFont::Prompt,30,cx,580,team.difficulty,color(225,233,240),650);
        }else{
          center(UiFont::Prompt,34,cx,460,team.uniform,color(225,233,240),650);
          center(UiFont::Prompt,34,cx,515,team.difficulty,color(225,233,240),650);
        }
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
    const bool hasGrades=std::any_of(team.grades.begin(),team.grades.end(),[](const std::string& grade){return !grade.empty();});
    float x=cx-360,top=1035;
    if(hasGrades){
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
  bool componentSetup=std::any_of(state.begin(),state.end(),[](const auto& team){return team.step==TeamSelectSide::Step::Uniform && !team.uniformParts[1].empty();});
  text(UiFont::Prompt,28,setup?930:1050,1335,componentSetup?"UP / DOWN: PART    LEFT / RIGHT: CHANGE":setup?"LEFT / RIGHT: CHANGE     A: CONFIRM     B: BACK":"LEFT / RIGHT: SIDE     UP / DOWN: TEAM",color(180,195,205),1000);
  if(dev::Env("MVP06_TRACE_FE")){
    static std::string captured;
    std::string key=std::to_string(version);
    for(const auto& team:state){key+=team.name+team.controller+std::to_string(int(team.step))+team.uniform+team.difficulty+team.uniformPreview+std::to_string(team.uniformRow);for(const auto& part:team.uniformParts)key+=part;}
    if(elapsed>1 && Clock()-std::max(changed[0],changed[1])>.4 && key!=captured){captured=key;gpu::g_capture=true;}
  }
}
bool DrawDynastySchool(const DynastySchoolCard& card) {
  if (!gpu::g_ui_image) return false;
  const auto root = overhaul::Root();
  const auto atlas = dev::Env("MVP06_TEST_PICKER_NO_ATLAS") ? 0 : gpu::g_ui_image(overhaul::UiAsset(root / "dynasty-team-select-atlas.png"));
  // Keep the opaque host presentation even while artwork is unavailable.
  auto* draw = ImGui::GetBackgroundDrawList();
  const auto display = ImGui::GetIO().DisplaySize;
  const float scale = std::min(display.x/1672.f, display.y/941.f);
  if (scale <= 0) return false;
  const float ox = (display.x - 1672*scale)/2, oy = (display.y - 941*scale)/2;
  auto p = [&](float x, float y) { return ImVec2(ox+x*scale, oy+y*scale); };
  auto rect = [&](float x, float y, float w, float h, ImU32 tint) {
    draw->AddRectFilled(p(x,y),p(x+w,y+h),tint);
  };
  auto region = [&](float x, float y, float w, float h, float u, float v, float uw, float vh,
                    ImU32 tint = IM_COL32_WHITE) {
    if(!atlas)return;
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),
                   {u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},tint);
  };
  auto image = [&](const std::filesystem::path& path, float x, float y, float w, float h) {
    if (!path.empty())
      if (const auto texture = gpu::g_ui_image(overhaul::UiAsset(path)))
        draw->AddImage((ImTextureID)texture,p(x,y),p(x+w,y+h));
  };
  auto text = [&](UiFont kind, const std::string& value, float x, float y, float size,
                  float width, ImU32 tint = IM_COL32_WHITE, bool right = false) {
    const auto* font = Font(kind);
    if (!font || value.empty()) return;
    const float natural = font->CalcTextSizeA(size*scale,100000,0,value.c_str()).x/scale;
    const float fit = std::min(1.f,width/std::max(natural,1.f));
    UiText(draw,kind,size*scale*fit,p(right ? x+width-natural*fit : x,y),tint,value.c_str(),width*scale);
  };
  constexpr auto muted = IM_COL32(182,192,200,255);
  constexpr auto red = IM_COL32(236,16,38,255);
  constexpr auto divider = IM_COL32(71,82,93,255);
  const auto teamColor = IM_COL32((card.color>>16)&255,(card.color>>8)&255,card.color&255,255);
  UiMenuBackdrop(draw,display,UiBackdrop::Steel);
  region(0,0,1672,100,12,9,663,106);
  region(0,864,1672,77,750,682,291,108,IM_COL32(255,255,255,160));
  rect(0,99,1672,2,divider);
  rect(0,864,1672,2,divider);
  UiBrandLogo(draw,p(72,14),p(292,87));
  text(UiFont::Prompt,"D Y N A S T Y   M O D E",1190,34,29,390,muted,true);
  rect(1456,72,124,4,red);

  rect(73,132,6,62,red);
  text(UiFont::Heading,"SELECT DYNASTY TEAM",94,126,53,700);
  text(UiFont::Prompt,"Choose a program to start your dynasty.",96,180,25,695,muted);
  constexpr float lx=73, ly=231, lw=544, rx=645, rw=954;
  constexpr float rowY=296, rowH=64;
  region(lx,ly,lw,583,11,294,555,151);
  region(lx+5,ly+5,lw-10,55,592,510,557,42);
  text(UiFont::Prompt,"LT",lx+35,ly+22,19,40);
  text(UiFont::Prompt,"RT",lx+lw-45,ly+22,19,40);
  text(UiFont::Heading,card.team.conference,lx+121,ly+13,31,lw-235);

  const auto size = card.schools.size();
  constexpr size_t maxRows = 8;
  // Keep rows stationary while moving within the viewport. Reset when the
  // conference changes instead of carrying another conference's scroll offset.
  static std::string viewportConference;
  static size_t first = 0;
  if (viewportConference != card.team.conference) {
    viewportConference = card.team.conference;
    first = 0;
  }
  if (size <= maxRows) first = 0;
  else {
    if (card.selected < first) first = card.selected;
    if (card.selected >= first + maxRows) first = card.selected - maxRows + 1;
    first = std::min(first, size - maxRows);
  }
  const auto shown = std::min(maxRows,size);
  for (size_t i=0; i<shown; ++i) {
    const auto index = first+i;
    const auto& peer = card.schools[index];
    const float y = rowY+float(i)*rowH;
    const bool selected = index == card.selected;
    if (selected) UiMenuHighlight(draw,p(lx+6,y),p(lx+lw-16,y+rowH-3),scale);
    else region(lx+6,y,lw-22,rowH-3,12,510,526,35);
    if (selected) rect(lx+7,y+3,91,rowH-9,teamColor);
    image(overhaul::TeamLogo(peer.name,peer.nickname),lx+24,y+3,55,55);
    text(UiFont::Heading,peer.name,lx+115,y+4,29,305);
    text(UiFont::Prompt,peer.nickname,lx+115,y+33,21,305,muted);
    text(UiFont::Prompt,peer.rank,lx+433,y+18,23,75,selected ? IM_COL32_WHITE : muted,true);
  }
  if (size > maxRows) {
    const float track = maxRows*rowH-4;
    rect(lx+lw-10,rowY,4,track,divider);
    rect(lx+lw-10,rowY+track*float(first)/size,4,track*maxRows/size,muted);
  }

  region(rx,215,rw,599,591,295,558,195);
  rect(rx,215,rw,214,teamColor);
  region(rx,215,rw,214,1230,144,529,122,IM_COL32(255,255,255,104));
  rect(rx,215,rw,2,muted);
  rect(rx,427,rw,2,divider);
  image(overhaul::TeamLogo(card.team.name,card.team.nickname),rx+689,222,191,191);
  text(UiFont::TeamDisplay,card.team.name,rx+37,233,70,630);
  text(UiFont::TeamDisplay,card.team.nickname,rx+37,302,57,630);
  auto location = card.location;
  if (!location.empty() && !card.team.conference.empty()) location += "  |  ";
  location += card.team.conference;
  text(UiFont::Prompt,location,rx+39,385,26,640);

  constexpr float columns[] = {rx+33,rx+343,rx+660};
  const char* labels[] = {"TEAM PRESTIGE","CONF. STANDING","NATIONAL RANK"};
  for (int i=0;i<3;++i)
    text(UiFont::Heading,labels[i],columns[i],448,26,275,muted);
  rect(rx+314,446,1,84,divider);
  rect(rx+632,446,1,84,divider);
  if (card.prestige < 0) text(UiFont::Heading,"—",columns[0],482,35,180);
  else UiPrestigeStars(draw,p(columns[0]+12,502),12*scale,27*scale,card.prestige);
  text(UiFont::TeamDisplay,card.standing.empty() ? "—" : card.standing,columns[1],480,47,260);
  text(UiFont::TeamDisplay,card.team.rank,columns[2],480,47,260);
  rect(rx+26,548,rw-52,1,divider);
  rect(rx+476,570,1,210,divider);

  text(UiFont::Heading,"SEASON RECORDS",rx+33,571,34,415);
  text(UiFont::Heading,"RATINGS",rx+506,571,34,408);
  for(unsigned i=0;i<card.seasonRecords.size();++i){
    const float y=618.f+i*49;
    if(!(i%2))rect(rx+33,y,406,45,IM_COL32(37,47,57,255));
    text(UiFont::Prompt,std::to_string(2024+i),rx+45,y+9,25,190,muted);
    text(UiFont::Heading,card.seasonRecords[i].empty()?"N/A":card.seasonRecords[i],
         rx+238,y+5,31,186,IM_COL32_WHITE,true);
  }
  text(UiFont::Prompt,"VS DIVISION I OPPONENTS",rx+45,777,18,390,muted);
  const char* grades[] = {"OVERALL","PITCHING","HITTING","FIELDING","SPEED"};
  for (int i=0;i<5;++i) {
    const float y = 610.f+i*38;
    if (!(i%2)) region(rx+506,y,408,37,1175,302,585,23);
    text(UiFont::Prompt,grades[i],rx+520,y+4,23,240,muted);
    const auto& value = card.team.grades[i];
    text(UiFont::Heading,value.empty() ? "—" : value,rx+812,y+1,29,83,IM_COL32_WHITE,true);
  }

  image(root/"controller-reference-a.png",78,880,40,40);
  text(UiFont::Prompt,"SELECT",127,884,28,120);
  image(root/"controller-reference-b.png",273,880,40,40);
  text(UiFont::Prompt,"BACK",324,884,28,120);
  text(UiFont::Prompt,"UP / DOWN  CHANGE TEAM",1270,884,23,325,muted,true);
  return true;
}
bool DrawDynastyHome(const DynastyHomeCard& card) {
  if (!gpu::g_ui_image) return false;
  const auto root = overhaul::Root();
  const auto atlas = gpu::g_ui_image(overhaul::UiAsset(root / "dynasty-home-atlas.png"));
  if (!atlas) return false;
  const auto screen = ImGui::GetIO().DisplaySize;
  const float scale = std::min(screen.x/1672.f,screen.y/941.f);
  if (scale <= 0) return false;
  auto* draw = ImGui::GetBackgroundDrawList();
  const float ox = (screen.x-1672*scale)/2, oy = (screen.y-941*scale)/2;
  auto p = [&](float x,float y){return ImVec2(ox+x*scale,oy+y*scale);};
  auto rect = [&](float x,float y,float w,float h,ImU32 color) {
    draw->AddRectFilled(p(x,y),p(x+w,y+h),color);
  };
  auto atlasPart = [&](float x,float y,float w,float h,float u,float v,float uw,float vh,
                       ImU32 tint=IM_COL32_WHITE) {
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),
                   {u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},tint);
  };
  auto image = [&](const std::filesystem::path& path,float x,float y,float w,float h) {
    if (!path.empty())
      if (const auto texture=gpu::g_ui_image(overhaul::UiAsset(path)))
        draw->AddImage((ImTextureID)texture,p(x,y),p(x+w,y+h));
  };
  auto text = [&](UiFont kind,const std::string& value,float x,float y,float size,float width,
                  ImU32 tint=IM_COL32_WHITE,bool right=false) {
    const auto* font=Font(kind);
    if (!font || value.empty()) return;
    const float natural=font->CalcTextSizeA(size*scale,100000,0,value.c_str()).x/scale;
    const float fit=std::min(1.f,width/std::max(natural,1.f));
    UiText(draw,kind,size*scale*fit,p(right?x+width-natural*fit:x,y),tint,value.c_str(),width*scale);
  };

  constexpr ImU32 muted=IM_COL32(184,194,203,255), divider=IM_COL32(77,88,98,255);
  constexpr ImU32 red=IM_COL32(228,13,35,255), gold=IM_COL32(255,190,54,255);
  const auto& school=card.school;
  const auto& team=school.team;
  const auto rgb=school.color;
  const auto color=IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,255);
  UiMenuBackdrop(draw,screen,UiBackdrop::Graphite);
  atlasPart(0,0,1672,97,12,10,634,107);
  atlasPart(0,848,1672,93,813,703,266,94,IM_COL32(255,255,255,145));
  rect(0,96,1672,2,divider);
  rect(0,848,1672,2,divider);
  UiBrandLogo(draw,p(64,12),p(284,85));
  text(UiFont::Prompt,"D Y N A S T Y   H O M E",1180,27,33,424,muted,true);
  rect(1464,70,140,4,red);
  atlasPart(64,119,603,54,40,142,481,98);
  draw->AddQuadFilled(p(78,163),p(91,129),p(102,129),p(89,163),red);
  text(UiFont::Heading,card.date,113,127,36,525);

  rect(64,191,1544,164,IM_COL32(23,24,26,255));
  draw->AddQuadFilled(p(64,191),p(771,191),p(703,355),p(64,355),color);
  atlasPart(64,191,1544,164,48,266,1172,91,IM_COL32(255,255,255,110));
  rect(64,191,1544,2,divider);
  rect(64,353,1544,2,divider);
  image(overhaul::TeamLogo(team.name,team.nickname),89,200,148,148);
  text(UiFont::TeamDisplay,team.name,265,207,56,440);
  text(UiFont::TeamDisplay,team.nickname,265,259,45,425);
  text(UiFont::Prompt,school.location,267,315,24,405,muted);
  constexpr float stats[]={755,954,1151,1381};
  const bool rated = !team.grades[0].empty() && team.grades[0] != "?";
  const char* headings[]={"NATIONAL RANK","CONF. STANDING","TEAM PRESTIGE",rated?"OVERALL":"SEASON RECORD"};
  for (int i=0;i<4;++i) {
    if (i) rect(stats[i]-15,218,1,109,divider);
    text(UiFont::Heading,headings[i],stats[i],221,24,194,muted);
  }
  text(UiFont::TeamDisplay,team.rank,stats[0],261,53,170);
  text(UiFont::TeamDisplay,school.standing.empty()?"—":school.standing,stats[1],261,53,170);
  if (school.prestige>=0) UiPrestigeStars(draw,p(stats[2]+13,289),13*scale,30*scale,school.prestige);
  else text(UiFont::Heading,"—",stats[2],266,43,130,gold);
  text(UiFont::TeamDisplay,rated?team.grades[0]:(card.records[0].empty()?"—":card.records[0]),stats[3],261,55,188);

  constexpr float y=385, h=398;
  auto panel = [&](float x,float w) {
    atlasPart(x,y,w,h,1418,381,346,94);
    draw->AddRect(p(x,y),p(x+w,y+h),divider,3*scale,0,scale);
  };
  panel(64,349);
  panel(437,633);
  panel(1094,514);
  const auto panelTitle = [&](const char* title,float x) {
    draw->AddQuadFilled(p(x,441),p(x+13,400),p(x+24,400),p(x+11,441),red);
    text(UiFont::Heading,title,x+36,396,36,450);
  };
  panelTitle("UPCOMING GAMES",456);
  panelTitle("SEASON SUMMARY",1112);
  for (size_t i=0;i<card.menu.size();++i) {
    const float row=y+13+float(i)*73;
    const bool selected=int(i)==card.selected;
    if (selected) UiMenuHighlight(draw,p(70,row),p(407,row+66),scale);
    else rect(70,row,337,66,IM_COL32(22,30,37,220));
    text(UiFont::Heading,card.menu[i],91,row+12,32,293);
  }
  if (card.upcoming.empty()) text(UiFont::Prompt,"No upcoming games",463,473,26,560,muted);
  for (size_t i=0;i<card.upcoming.size();++i) {
    const auto& game=card.upcoming[i];
    const float row=459+float(i)*59;
    if (!i) UiMenuHighlight(draw,p(453,row),p(1054,row+54),scale);
    else rect(453,row,601,54,IM_COL32(17,23,29,235));
    rect(453,row+54,601,1,divider);
    text(UiFont::Prompt,game.date,473,row+13,26,100);
    text(UiFont::Prompt,game.home?"VS":"AT",574,row+13,24,52,muted);
    image(overhaul::TeamLogo(game.opponent,game.nickname),633,row+4,45,45);
    text(UiFont::Heading,game.opponent,694,row+10,30,308);
    text(UiFont::Prompt,game.home?"H":"A",1012,row+13,25,24);
  }
  const char* records[]={"SEASON RECORD","CONFERENCE RECORD","HOME RECORD","ROAD RECORD"};
  for (size_t i=0;i<card.records.size();++i) {
    const float row=459+float(i)*59;
    atlasPart(1110,row,481,50,961,442,419,31);
    text(UiFont::Prompt,records[i],1129,row+9,29,329);
    text(UiFont::Heading,card.records[i].empty()?"—":card.records[i],1459,row+7,32,111,IM_COL32_WHITE,true);
  }
  image(root/"controller-reference-a.png",68,870,39,39);
  text(UiFont::Prompt,"SELECT",118,875,27,126);
  image(root/"controller-reference-b.png",234,870,39,39);
  text(UiFont::Prompt,"QUIT",284,875,27,126);
  UiControllerGlyph(draw,p(514,867),p(573,909),"START");
  text(UiFont::Prompt,card.advance.empty()?"OPEN SCHEDULE":card.advance,590,875,27,580);
  UiControllerGlyph(draw,p(1200,870),p(1260,909),"RB");
  text(UiFont::Prompt,"TEAM EMAIL",1275,875,27,300);
  return true;
}
bool DrawSimulationProgress(const std::string& caption, float progress, bool canStop) {
  if (!gpu::g_ui_image) return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"simulation-progress-atlas.png"));
  if (!atlas) return false;
  auto* draw=ImGui::GetForegroundDrawList();
  const auto screen=ImGui::GetIO().DisplaySize;
  const float scale=std::min(screen.x/1672.f,screen.y/941.f);
  if (scale<=0) return false;
  const ImVec2 origin={(screen.x-820*scale)/2,(screen.y-180*scale)/2};
  auto p=[&](float x,float y){return ImVec2(origin.x+x*scale,origin.y+y*scale);};
  draw->AddRectFilled({0,0},screen,IM_COL32(0,0,0,135));
  draw->AddQuadFilled(p(824,-4),p(839,-4),p(763,175),p(748,175),IM_COL32(235,12,36,255));
  // Sample the panel's slanted silhouette without including adjacent atlas strips.
  draw->AddImageQuad((ImTextureID)atlas,p(68,-4),p(824,-4),p(748,175),p(-8,175),
      {126.f/1774,4.f/887},{1296.f/1774,4.f/887},
      {1178.f/1774,276.f/887},{8.f/1774,276.f/887});
  std::string date=caption;
  for (auto& c : date) c=char(std::toupper(static_cast<unsigned char>(c)));
  std::string heading="SIMULATING";
  for (const auto* prefix : {"SIMULATING UP TO ","SIMULATING TO "}) {
    if (date.starts_with(prefix)) {
      date.erase(0,std::strlen(prefix));
      heading="SIMULATING UP TO";
      break;
    }
  }
  auto centered=[&](const std::string& text,float size,float y,ImU32 color) {
    auto* font=Font(UiFont::Settings);
    const float width=font->CalcTextSizeA(size*scale,100000,0,text.c_str()).x;
    const float fit=std::min(1.f,650*scale/std::max(1.f,width));
    UiText(draw,UiFont::Settings,size*scale*fit,
           {origin.x+(820*scale-width*fit)/2,origin.y+y*scale},color,text.c_str());
  };
  centered(heading,29,20,IM_COL32(188,196,204,255));
  centered(date,52,50,IM_COL32_WHITE);
  const ImVec2 barMin=p(115,125),barMax=p(705,146);
  draw->AddImage((ImTextureID)atlas,barMin,barMax,
                 {1284.f/1774,371.f/887},{1751.f/1774,411.f/887});
  draw->AddRect(barMin,barMax,IM_COL32(184,193,201,255),2*scale,0,1.3f*scale);
  const float amount=std::clamp(progress,0.f,1.f);
  if (amount>0) {
    const auto min=p(119,129),max=p(701,142);
    draw->PushClipRect(min,{min.x+(max.x-min.x)*amount,max.y},true);
    draw->AddImage((ImTextureID)atlas,min,max,
                   {1284.f/1774,457.f/887},{1751.f/1774,493.f/887});
    draw->PopClipRect();
  }
  if (canStop){UiControllerGlyph(draw,p(345,150),p(367,172),"A");UiText(draw,UiFont::Prompt,17*scale,p(376,152),IM_COL32(188,196,204,255),"STOP SIM");}
  return true;
}

bool DrawStartingPitcher(const std::array<StartingPitcherSide,2>& sides) {
  if(!gpu::g_ui_image)return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"starting-pitcher-atlas.png"));
  if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);
  UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
  region(0,0,1672,103,6,6,850,100);
  rect(0,103,1672,2,border);
  UiBrandLogo(draw,p(54,13),p(307,88));
  text(UiFont::TeamDisplay,"SELECT STARTING PITCHER",743,22,49,873);
  rect(850,83,766,4,red);
  for(int i=0;i<2;++i) {
    const auto& side=sides[i];
    const float x=i?913.f:49.f,w=710,y=131,h=137;
    const auto color=IM_COL32((side.color>>16)&255,(side.color>>8)&255,side.color&255,255);
    draw->AddQuadFilled(p(x+35,y),p(x+w,y),p(x+w-35,y+h),p(x,y+h),color);
    draw->AddImageQuad((ImTextureID)atlas,p(x+35,y),p(x+w,y),p(x+w-35,y+h),p(x,y+h),
      {43.f/1774,133.f/887},{700.f/1774,133.f/887},{658.f/1774,246.f/887},{7.f/1774,246.f/887},IM_COL32(255,255,255,90));
    const auto path=overhaul::TeamLogo(side.team.name,side.team.nickname);
    if(!path.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1439:x+46,y+7),p(i?1567:x+174,y+135));
    const float tx=i?957:267;
    text(UiFont::Heading,side.team.name,tx,y+7,36,i?451:445);
    text(UiFont::TeamDisplay,side.team.nickname,tx,y+40,60,i?451:445);
    text(UiFont::Prompt,"NATIONAL RANK  "+side.team.rank,tx,y+103,23,440,IM_COL32(228,232,236,255));
  }
  text(UiFont::TeamDisplay,"AT",775,175,51,122,IM_COL32_WHITE,true);
  for(int i=0;i<2;++i) {
    const auto& side=sides[i];const float x=i?1257:95;
    region(x,286,320,229,16,263,406,196);
    if(side.portrait && gpu::g_ui_guest_image)if(const auto texture=gpu::g_ui_guest_image(side.portrait))
      draw->AddImage((ImTextureID)texture,p(x+58,287),p(x+262,514),{side.uv[0],side.uv[1]},{side.uv[2],side.uv[3]});
    if(side.ready){rect(x,463,320,51,IM_COL32(6,10,14,220));text(UiFont::Heading,"READY",x+20,469,36,280,IM_COL32(96,213,108,255),true);}
  }
  rect(415,286,842,229,IM_COL32(12,17,21,255));
  for(int row=0;row<6;++row) {
    const float h=229.f/6,y=286+row*h;
    region(416,y+1,840,h-2,442,298,615,25,IM_COL32(255,255,255,155));
    if(row)draw->AddLine(p(415,y),p(1257,y),border,s);
    cell(row?sides[0].ratings[row-1]:sides[0].stamina,437,y,112,h,37,IM_COL32_WHITE,true);
    cell(row?sides[1].ratings[row-1]:sides[1].stamina,1120,y,112,h,37,IM_COL32_WHITE,true);
    const auto left=row?sides[0].pitches[row-1]:std::string("STAMINA");
    const auto right=row?sides[1].pitches[row-1]:std::string("STAMINA");
    if(left==right)cell(left,569,y,535,h,31,muted,true);
    else {
      cell(left,558,y,259,h,30,muted,true);
      cell(right,858,y,250,h,30,muted,true);
    }
  }
  for(int i=0;i<2;++i) {
    const auto& side=sides[i];const float x=i?849:48,w=775,y=537;
    rect(x,y,w,293,IM_COL32(12,17,21,255));
    draw->AddRect(p(x,y),p(x+w,y+293),border,0,0,s);
    rect(x+2,y+2,w-4,45,IM_COL32((side.color>>16)&255,(side.color>>8)&255,side.color&255,215));
    region(x+2,y+2,w-4,45,16,479,697,42,IM_COL32(255,255,255,124));
    cell(side.team.name+" ROTATION",x+37,y+2,460,44,32);
    const float columns[]={x+520,x+612,x+685};
    for(int c=0;c<3;++c)cell(c==0?"POS":c==1?"THR":"OVR",columns[c],y+2,67,44,29,muted,true);
    for(int row=0;row<6;++row) {
      const float ry=y+48+row*40;
      region(x+2,ry,w-4,40,740,560,676,27,IM_COL32(255,255,255,row%2?190:135));
      draw->AddLine(p(x+2,ry),p(x+w-2,ry),border,s);
      if(row==side.selected && side.active && !side.ready) {
        region(x+11,ry+2,w-22,36,1452,137,306,45);
        draw->AddTriangleFilled(p(x+23,ry+10),p(x+23,ry+30),p(x+40,ry+20),IM_COL32_WHITE);
      }
      cell(side.rotation[row][1],x+57,ry,452,40,34);
      cell(side.rotation[row][0],columns[0],ry,67,40,33,IM_COL32_WHITE,true);
      cell(side.rotation[row][2],columns[1],ry,67,40,33,IM_COL32_WHITE,true);
      cell(side.rotation[row][3],columns[2],ry,67,40,33,IM_COL32_WHITE,true);
    }
  }
  rect(0,864,1672,77,IM_COL32(6,11,15,235));rect(0,864,1672,2,border);
  auto prompt=[&](float x,const char* letter,const char* label,ImU32 color){
    UiControllerButton(draw,p(x,904),19*s,s,letter,color);
    text(UiFont::Prompt,label,x+31,891,24,235);
  };
  prompt(72,"A","ADVANCE",IM_COL32(76,189,60,255));
  prompt(307,"B","BACK",IM_COL32(224,44,58,255));
  prompt(511,"X","EDIT ROTATION",IM_COL32(63,134,227,255));
  prompt(851,"Y","PLAYER INFO",IM_COL32(232,190,40,255));
  return true;
}

bool DrawStartingLineup(const std::array<StartingLineupSide,2>& sides) {
  if(!gpu::g_ui_image)return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"starting-lineup-atlas.png"));
  if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);
  UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
  region(0,0,1672,103,6,6,850,100);
  rect(0,103,1672,2,border);
  UiBrandLogo(draw,p(54,13),p(307,88));
  text(UiFont::TeamDisplay,"SELECT STARTING LINEUP",743,22,49,873);
  rect(850,83,766,4,red);
  for(int i=0;i<2;++i) {
    const auto& side=sides[i];
    const float x=i?913.f:49.f,w=710,y=131,h=137;
    const auto color=IM_COL32((side.color>>16)&255,(side.color>>8)&255,side.color&255,255);
    draw->AddQuadFilled(p(x+35,y),p(x+w,y),p(x+w-35,y+h),p(x,y+h),color);
    draw->AddImageQuad((ImTextureID)atlas,p(x+35,y),p(x+w,y),p(x+w-35,y+h),p(x,y+h),
      {160.f/1774,145.f/887},{470.f/1774,145.f/887},{450.f/1774,200.f/887},{140.f/1774,200.f/887},IM_COL32(255,255,255,90));
    const auto path=overhaul::TeamLogo(side.team.name,side.team.nickname);
    if(!path.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1439:x+46,y+7),p(i?1567:x+174,y+135));
    const float tx=i?957:267;
    text(UiFont::Heading,side.team.name,tx,y+7,36,i?451:445);
    text(UiFont::TeamDisplay,side.team.nickname,tx,y+40,60,i?451:445);
    text(UiFont::Prompt,"NATIONAL RANK  "+side.team.rank,tx,y+103,23,260,IM_COL32(228,232,236,255));
  }
  text(UiFont::TeamDisplay,"AT",775,175,51,122,IM_COL32_WHITE,true);
  for(int i=0;i<2;++i) {
    const auto& side=sides[i];const float x=i?859:55,w=758;
    rect(x,283,w,183,IM_COL32(9,15,19,255));
    region(x,283,w,183,1480,697,255,70,IM_COL32(255,255,255,160));
    draw->AddRect(p(x,283),p(x+w,466),border,0,0,s);
    draw->AddLine(p(x+242,283),p(x+242,466),border,s);
    draw->AddLine(p(x+419,283),p(x+419,466),border,s);
    for(int z=0;z<9;++z){
      const auto state=side.zones[z];
      const auto color=state==0?IM_COL32(25,62,157,255):state==2?IM_COL32(169,31,31,255):IM_COL32(163,169,174,255);
      const float zx=x+265+(z%3)*41,zy=297+(z/3)*41;
      rect(zx,zy,39,39,color);draw->AddRect(p(zx,zy),p(zx+39,zy+39),border,0,0,s);
    }
    if(side.portrait && gpu::g_ui_guest_image)if(const auto texture=gpu::g_ui_guest_image(side.portrait))
      draw->AddImage((ImTextureID)texture,p(x+21,284),p(x+224,465),{side.uv[0],side.uv[1]},{side.uv[2],side.uv[3]});
    for(int r=0;r<3;++r) {
      if(r)draw->AddLine(p(x+430,288+r*57),p(x+w-10,288+r*57),border,s);
      cell(r==0?"POWER":r==1?"CONTACT":"SPEED",x+435,288+r*57,210,57,34);
      cell(side.ratings[r],x+656,288+r*57,77,57,43,IM_COL32(255,220,94,255),true);
    }
    cell(side.versus,x+250,411,155,45,32,muted,true);
    const float y=486,h=354,rh=38;
    rect(x,y,w,h,IM_COL32(10,16,20,255));
    draw->AddRect(p(x,y),p(x+w,y+h),border,0,0,s);
    rect(x+2,y+2,w-4,44,IM_COL32((side.color>>16)&255,(side.color>>8)&255,side.color&255,215));
    region(x+2,y+2,w-4,44,18,525,687,35,IM_COL32(255,255,255,110));
    cell(side.team.name+" BATTING ORDER",x+28,y,508,46,32);
    cell("B",x+574,y,48,46,30,muted,true);cell("ENG",x+642,y,100,46,30,muted,true);
    for(int r=0;r<8;++r) {
      const float ry=y+47+r*rh;
      rect(x+2,ry,w-4,rh,IM_COL32(21,29,33,r%2?255:210));
      draw->AddLine(p(x+2,ry),p(x+w-2,ry),border,s);
      if(r==side.selected && side.active && !side.ready){
        rect(x+8,ry+2,w-16,rh-4,IM_COL32(186,4,24,255));
        draw->AddTriangleFilled(p(x+18,ry+9),p(x+18,ry+29),p(x+34,ry+19),IM_COL32_WHITE);
      }
      const auto& row=side.order[r];
      cell(row[0],x+42,ry,48,rh,31,IM_COL32_WHITE,true);
      cell(row[1],x+96,ry,63,rh,31,IM_COL32_WHITE,true);
      cell(row[2],x+174,ry,385,rh,31);
      cell(row[3],x+574,ry,48,rh,31,IM_COL32_WHITE,true);
      cell(row[4]+row[5],x+642,ry,100,rh,31,IM_COL32_WHITE,true);
    }
    if(side.ready || !side.active){rect(x+510,232,180,30,side.ready?IM_COL32(18,75,35,245):red);cell(side.ready?"READY":"DONE",x+510,232,180,30,25,IM_COL32_WHITE,true);}
  }
  rect(0,864,1672,77,IM_COL32(6,11,15,235));rect(0,864,1672,2,border);
  auto prompt=[&](float x,const char* letter,const char* label,ImU32 color){
    UiControllerButton(draw,p(x,904),19*s,s,letter,color);
    text(UiFont::Prompt,label,x+31,891,24,235);
  };
  prompt(72,"A","ADVANCE",IM_COL32(76,189,60,255));
  prompt(307,"B","BACK",IM_COL32(224,44,58,255));
  text(UiFont::Prompt,"D-PAD  SELECT PLAYER",630,891,24,450,muted);


  return true;
}

bool DrawFinalPlaySim(const FinalPlaySimCard& card) {
  if(!gpu::g_ui_image)return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"final-play-sim-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);
  UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
  region(0,0,1672,110,8,10,1380,104);
  UiBrandLogo(draw,p(55,15),p(310,92));
  cell("PLAY OR SIM",1060,6,553,87,65,IM_COL32_WHITE,true);rect(1110,91,500,5,red);
  rect(0,111,1672,2,border);
  for(int i=0;i<2;++i){
    const auto& side=card.sides[i];const float x=i?901:83,y=157,w=688,h=156;
    const auto color=IM_COL32((side.color>>16)&255,(side.color>>8)&255,side.color&255,255);
    draw->AddQuadFilled(p(x+42,y),p(x+w,y),p(x+w-42,y+h),p(x,y+h),color);
    draw->AddImageQuad((ImTextureID)atlas,p(x+42,y),p(x+w,y),p(x+w-42,y+h),p(x,y+h),
      {105.f/1774,185.f/887},{640.f/1774,185.f/887},{595.f/1774,309.f/887},{58.f/1774,309.f/887},IM_COL32(255,255,255,95));
    draw->AddQuad(p(x+42,y),p(x+w,y),p(x+w-42,y+h),p(x,y+h),border,s);
    const auto path=overhaul::TeamLogo(side.team.name,side.team.nickname);
    if(!path.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1370:x+52,y+9),p(i?1521:x+203,y+146));
    const float tx=i?950:333;
    cell(side.team.name,tx,y+6,423,35,33);
    cell(side.team.nickname,tx,y+40,423,68,57);
    cell(card.records[i]+"   NATIONAL RANK "+side.team.rank,tx,y+112,420,32,24,muted);
  }
  cell("AT",770,175,130,104,64,IM_COL32_WHITE,true);
  region(135,341,1410,63,20,346,1340,60);
  rect(135,404,1410,300,IM_COL32(10,16,20,255));
  region(138,405,515,228,30,413,435,185);
  region(1033,405,509,228,925,413,425,185);
  draw->AddRect(p(135,341),p(1545,704),border,0,0,2*s);
  draw->AddLine(p(135,404),p(1545,404),border,s);
  draw->AddLine(p(655,420),p(655,621),border,s);
  draw->AddLine(p(1031,420),p(1031,621),border,s);
  rect(655,637,376,65,IM_COL32(61,10,20,160));
  draw->AddLine(p(135,635),p(1545,635),border,s);
  cell("STARTING PITCHERS",205,346,1100,56,45);

  for(int i=0;i<2;++i){
    const auto& side=card.sides[i];const float x=i?1032:138;
    const auto path=overhaul::TeamLogo(side.team.name,side.team.nickname);
    if(!path.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1320:160,427),p(i?1510:350,602),{0,0},{1,1},IM_COL32(255,255,255,42));
    if(side.portrait && gpu::g_ui_guest_image)if(const auto texture=gpu::g_ui_guest_image(side.portrait))
      draw->AddImage((ImTextureID)texture,p(i?1211:228,419),p(i?1460:477,634),{side.uv[0],side.uv[1]},{side.uv[2],side.uv[3]});
    cell(card.players[i],i?1046:454,427,214,65,40,IM_COL32_WHITE,true);
    const float bx=i?1060:221,by=650,bw=397;
    rect(bx,by,bw,32,IM_COL32(8,11,14,255));draw->AddRect(p(bx,by),p(bx+bw,by+32),border,0,0,s);
    for(int k=0;k<14;++k){const bool filled=(k+.5f)/14<=card.stamina[i];rect(bx+7+k*27.5f,by+6,24,20,filled?IM_COL32(247,123,24,255):IM_COL32(49,54,58,255));}
  }
  // Use our own boundaries so native text never intersects atlas dividers.
  rect(655,422,374,199,IM_COL32(9,15,19,255));
  for(int r=0;r<2;++r){const float y=450+r*74;
    cell(r?card.wins[0]:card.era[0],671,y,106,66,41,IM_COL32_WHITE,true);
    cell(r?"W-L":"ERA",783,y,119,66,35,muted,true);
    cell(r?card.wins[1]:card.era[1],910,y,106,66,41,IM_COL32_WHITE,true);
  }
  cell("STAMINA",649,640,380,55,42,IM_COL32_WHITE,true);
  for(int i=0;i<3;++i){const float x=295+i*377,y=736,w=348,h=82;const bool selected=i==card.selected;
    draw->AddQuadFilled(p(x+12,y),p(x+w,y),p(x+w-12,y+h),p(x,y+h),selected?IM_COL32(193,4,25,255):IM_COL32(28,35,40,255));
    draw->AddImageQuad((ImTextureID)atlas,p(x+12,y),p(x+w,y),p(x+w-12,y+h),p(x,y+h),
      {210.f/1774,720.f/887},{450.f/1774,720.f/887},{435.f/1774,770.f/887},{195.f/1774,770.f/887},selected?IM_COL32(240,20,40,180):IM_COL32(100,110,120,95));
    draw->AddQuad(p(x+12,y),p(x+w,y),p(x+w-12,y+h),p(x,y+h),selected?IM_COL32_WHITE:border,2*s);
    if(selected)draw->AddTriangleFilled(p(x+30,y+26),p(x+30,y+56),p(x+55,y+41),IM_COL32_WHITE);
    cell(card.actions[i],x+62,y+4,w-88,h-8,57,IM_COL32_WHITE,true);
  }
  rect(0,862,1672,79,IM_COL32(6,11,15,245));rect(0,862,1672,2,border);
  UiControllerButton(draw,p(91,902),21*s,s,"A",IM_COL32(76,189,60,255));
  cell("SELECT",126,878,220,48,28);
  return true;
}

bool DrawDynastyOptions(const DynastyOptionsCard& card) {
 UtilityMenuCard menu;menu.title="OPTIONS";
 menu.subtitle=card.school.team.name.empty()?"TOURNAMENT OPTIONS":card.school.team.name+"  /  PROGRAM OPTIONS";
 menu.selected=card.selected;
 for(int i=0;i<5;++i){menu.labels.push_back(card.menu[i]);menu.enabled.push_back(card.enabled[i]);}
 return DrawUtilityMenu(menu);
}

bool DrawRecruitProfile(const RecruitProfileCard& card) {
 if(!gpu::g_ui_image)return false;auto screen=ImGui::GetIO().DisplaySize;float s=std::min(screen.x/1672.f,screen.y/941.f);if(s<=0)return false;
 auto* d=ImGui::GetBackgroundDrawList();float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
 auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
 auto line=[&](float x,float y,float x2,float y2,ImU32 c,float w=1){d->AddLine(p(x,y),p(x2,y2),c,w*s);};
 auto text=[&](const std::string& v,float x,float y,float w,float size,ImU32 c=IM_COL32_WHITE,bool center=false){
  if(v.empty())return;std::string label=v;for(auto& ch:label)ch=char(std::toupper(static_cast<unsigned char>(ch)));
  const auto role=size>=30?UiFont::Heading:UiFont::Prompt;auto* f=Font(role);float width=f->CalcTextSizeA(size*s,1e6f,0,label.c_str()).x/s;
  size*=std::min(1.f,w/std::max(1.f,width));width=f->CalcTextSizeA(size*s,1e6f,0,label.c_str()).x/s;
  UiText(d,role,size*s,p(center?x+(w-width)/2:x,y),c,label.c_str(),w*s);
 };
 auto panel=[&](float x,float y,float w,float h,bool selected=false){
  UiBroadcastPanel(d,p(x,y),p(x+w,y+h),s,selected);
 };

 constexpr auto muted=IM_COL32(185,199,210,255),gold=IM_COL32(255,206,25,255),red=IM_COL32(237,12,40,255);
 UiMenuBackdrop(d,screen,UiBackdrop::Steel);UiBroadcastPanel(d,p(0,0),p(1672,112),s);
 if(auto a=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"team-management-atlas.png")))d->AddImage((ImTextureID)a,p(0,0),p(1672,110),{10.f/1774,8.f/887},{1404.f/1774,111.f/887},IM_COL32(255,255,255,100));
 UiBrandLogo(d,p(55,15),p(310,92));text("RECRUIT PROFILE",900,12,725,60,IM_COL32_WHITE,true);d->AddRectFilled(p(1060,88),p(1620,95),red);
 panel(438,137,1184,195);panel(50,137,368,275);
 d->PushClipRect(p(53,140),p(415,409),true);
 if(card.portrait && gpu::g_ui_guest_image)if(auto face=gpu::g_ui_guest_image(card.portrait))d->AddImage((ImTextureID)face,p(114,152),p(354,392),{card.uv[0],card.uv[1]},{card.uv[2],card.uv[3]});
 d->PopClipRect();
 text(card.name,449,151,390,49);text(card.position,452,218,340,33,muted);
 text("PROSPECT",453,279,340,22,muted);
 const float tx[]={855,1013,1178,1435};text("RANK",tx[0],155,125,27,muted,true);text("#"+card.rank,tx[0],192,125,61,gold,true);
 text("OVR",tx[1],155,125,27,muted,true);text(card.overall,tx[1],192,125,61,gold,true);
 text("INTEREST",1178,155,222,27,muted,true);d->AddRectFilled(p(1179,208),p(1399,241),IM_COL32(3,8,13,255),3*s);
 if(card.interest>0)d->AddRectFilled(p(1184,213),p(1184+210*card.interest,236),gold,2*s);
 d->AddRect(p(1179,208),p(1399,241),muted,3*s,0,2*s);
 text("STATUS",1435,155,164,27,muted,true);d->AddRectFilled(p(1430,205),p(1602,245),card.status=="Available"?IM_COL32(16,96,42,255):IM_COL32(47,61,74,255),3*s);d->AddRect(p(1430,205),p(1602,245),muted,3*s,0,s);text(card.status,1437,207,158,30,IM_COL32_WHITE,true);
 line(835,263,1600,263,IM_COL32(161,179,193,100));
 std::string vitals[]={"HT  "+(card.height.empty()?"--":card.height),"WT  "+(card.weight.empty()?"--":card.weight+" LBS"),"BATS  "+(card.bats.empty()?"--":card.bats),"THROWS  "+(card.throws.empty()?"--":card.throws)};
 for(int i=0;i<4;++i){text(vitals[i],846+i*192,279,183,28,IM_COL32_WHITE,true);if(i<3)line(1034+i*192,279,1034+i*192,309,muted);}
 // These are the native profile pages, not invented scouting actions.
 panel(50,435,368,394);text("PROFILE PAGES",78,454,310,24,muted);
 // The native visible grid owns which attributes are available. Do not infer
 // its contents from a recruit's primary position (two-way recruits vary).
 const char* pages[]={"OVERVIEW / VITALS","PLAYER RATINGS"};
 for(int i=0;i<2;++i){bool active=(i==0)==(card.page=="VITALS");float y=509+i*92;
  if(active)panel(65,y-4,338,72,true);
  text(pages[i],88,y+14,292,29,active?IM_COL32_WHITE:muted);
 }
 line(77,712,391,712,muted);UiTriggerBadge(d,p(79,738),p(128,771),s,"LT");UiTriggerBadge(d,p(340,738),p(389,771),s,"RT");text("CHANGE PAGE",140,740,186,24,muted,true);text("UP / DOWN  NEXT RECRUIT",78,790,312,23,muted);
 panel(438,357,674,235);panel(453,371,644,49,true);text("SKILL LEVEL PROJECTION",481,375,584,34);
 text("99",459,433,55,23,muted,true);text("1",459,534,55,23,muted,true);
 for(int i=0;i<5;++i){float x=530+i*110;d->AddRectFilled(p(x,438),p(x+96,549),IM_COL32(27,39,48,255),3*s);d->AddRect(p(x,438),p(x+96,549),IM_COL32(141,158,171,220),3*s,0,s);text(card.unavailable[i]?"N/A":"?",x+6,455,84,64,gold,true);text(card.years[i],x,554,96,25,IM_COL32_WHITE,true);}
 panel(438,607,674,222);panel(453,620,644,46,true);text("RECRUIT INFO",481,622,584,34);
 std::string keys[]={"ELIGIBILITY","HOME STATE","FROM","ATTITUDE / ACADEMIC","SPECIAL NEEDS"};
 std::string vals[]={card.eligibility+" YEARS",card.home,card.from,card.attitude+" / "+card.academic,card.needs};
 for(int i=0;i<5;++i){float y=675+i*28;text(keys[i],467,y,290,25,muted);text(vals[i],780,y,304,25,i==0?gold:IM_COL32_WHITE);if(i<4)line(465,y+27,1085,y+27,IM_COL32(140,164,181,65));}
 panel(1130,357,492,472);panel(1146,371,460,49,true);text("RECRUIT SUMMARY",1170,375,412,34);
 text("RECRUITING SNAPSHOT",1164,447,425,24,muted);text(card.home.empty()?"VIEW VITALS FOR RECRUIT DETAILS":card.from+"  /  "+card.home,1164,486,425,31);
 text(card.position+"  |  "+card.eligibility+" YEARS ELIGIBILITY",1164,533,425,29);
 line(1161,586,1590,586,IM_COL32(166,183,198,130));
 bool any=false;for(auto& v:card.ratings)any|=!v.empty();
 text(any?"PLAYER RATINGS":"SCOUTING RATINGS",1165,606,424,25,muted);
 for(int i=0;i<6;++i){if(i==5 && card.labels[i].empty())continue;float y=648+i*28;
  static const char* unknown[]={"POWER","FIELDING","SPEED","CONTACT","THROWING","STAMINA"};
  text(card.labels[i].empty()?unknown[i]:card.labels[i],1165,y,180,25);
  d->AddRectFilled(p(1360,y+7),p(1516,y+20),IM_COL32(5,10,18,255),3*s);d->AddRect(p(1360,y+7),p(1516,y+20),IM_COL32(123,142,160,230),3*s,0,s);
  if(!card.ratings[i].empty()){int rating=std::clamp(std::atoi(card.ratings[i].c_str()),0,99);d->AddRectFilled(p(1363,y+10),p(1363+150*rating/99.f,y+17),IM_COL32(195,202,212,255),3*s);}
  text(card.ratings[i].empty()?"?":card.ratings[i],1526,y,59,28,card.ratings[i].empty()?muted:gold,true);
 }
 d->AddRectFilled(p(0,855),p(1672,941),IM_COL32(5,12,17,255));line(0,856,1672,856,muted,2);line(0,920,1672,920,muted,2);
 for(float x:{39.f,1558.f})d->AddQuadFilled(p(x+37,857),p(x+73,857),p(x+36,919),p(x,919),red);
 UiControllerButton(d,p(176,888),22*s,s,"B",IM_COL32(220,20,35,255));text("BACK",219,866,220,33);
 UiControllerButton(d,p(977,888),22*s,s,"A",IM_COL32(56,177,48,255));text("RECRUIT",1020,866,280,33);
 return true;
}

bool DrawRecruitList(const RecruitListCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"recruit-list-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);region(0,0,1672,110,10,8,1394,103);UiBrandLogo(draw,p(55,15),p(310,92));
 cell(card.title,760,6,860,80,58,IM_COL32_WHITE,true);rect(965,91,650,5,red);
 // A restrained selector, exact row boundaries and one border per table.
 draw->AddQuadFilled(p(590,132),p(1105,132),p(1080,188),p(565,188),IM_COL32(27,34,39,255));
 draw->AddQuad(p(590,132),p(1105,132),p(1080,188),p(565,188),border,2*s);
 draw->AddTriangleFilled(p(612,146),p(612,174),p(590,160),IM_COL32_WHITE);draw->AddTriangleFilled(p(1050,146),p(1050,174),p(1072,160),IM_COL32_WHITE);
 cell(card.filter,636,134,393,52,38,IM_COL32_WHITE,true);
 rect(200,202,1272,506,IM_COL32(9,15,19,255));
 rect(202,204,1268,47,IM_COL32(106,3,17,255));region(203,205,1266,45,370,409,580,65,IM_COL32(255,80,90,140));
 const char* headers[]={"#","PLAYER","YEAR","POS","OVR","INTEREST"};
 const float xs[]={215,350,744,884,1025,1168},ws[]={112,380,112,125,115,270};
 for(int c=0;c<6;++c)cell(headers[c],xs[c],205,ws[c],45,32,IM_COL32_WHITE,c!=1);
 bool any=false;for(int r=0;r<10;++r){float yy=255+r*45;bool populated=!card.rows[r][1].empty();any|=populated;
  if(populated && r==card.selected){rect(202,yy,1268,45,IM_COL32(174,3,24,255));draw->AddTriangleFilled(p(207,yy+15),p(207,yy+30),p(217,yy+22),IM_COL32_WHITE);}
  for(int c=0;c<5;++c)cell(card.rows[r][c],xs[c]+(c==0?8:0),yy,ws[c]-(c==0?8:0),45,c==1?33:34,c==0?IM_COL32(255,221,107,255):IM_COL32_WHITE,c!=1);
  if(populated){float bx=1172,by=yy+11;rect(bx,by,249,24,IM_COL32(4,7,9,255));draw->AddRect(p(bx,by),p(bx+249,by+24),border,0,0,s);
   for(int k=0;k<12;++k){float amount=std::clamp(card.interest[r]*12-k,0.f,1.f);rect(bx+5+k*20,by+4,17,16,IM_COL32(39,44,48,255));if(amount>0)draw->AddRectFilledMultiColor(p(bx+5+k*20,by+4),p(bx+5+k*20+17*amount,by+20),IM_COL32(255,169,62,255),IM_COL32(255,169,62,255),IM_COL32(170,72,8,255),IM_COL32(170,72,8,255));}
  }
  draw->AddLine(p(202,yy+45),p(1470,yy+45),IM_COL32(68,80,89,255),s);
 }
 if(!any)cell("NO PROSPECTS MATCH THIS LIST",250,370,1170,80,36,muted,true);
 draw->AddRect(p(200,202),p(1472,708),border,0,0,2*s);
 region(388,736,890,80,32,728,944,69);cell("RECRUIT POINTS",530,742,475,64,44);cell(card.points,1050,740,203,67,57,IM_COL32(255,218,88,255),true);
 rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);
 const char* buttons[]={"B","X","Y","A"};const char* labels[]={"BACK","FILTER","DEPTH CHART","ADVANCE"};const float foot[]={91,305,524,859};const ImU32 colors[]={IM_COL32(225,35,45,255),IM_COL32(55,145,235,255),IM_COL32(240,193,15,255),IM_COL32(76,189,60,255)};
 for(int i=0;i<4;++i){UiControllerButton(draw,p(foot[i],901),22*s,s,buttons[i],colors[i]);cell(labels[i],foot[i]+35,878,i==2?260:190,48,28);}
 return true;
}

bool DrawStatisticsTable(const StatisticsTableCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"statistics-tables-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,100));UiBrandLogo(draw,p(55,15),p(310,92));
 cell(card.title,675,8,945,80,59,IM_COL32_WHITE,true,UiFont::Settings);rect(1020,88,600,7,red);
 auto panel=[&](float x,float y,float w,float h){UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s);};
 panel(180,135,415,57);panel(1135,135,360,57);
 cell(card.team.empty()?"TEAMS":card.team,218,138,338,50,34,IM_COL32_WHITE,true);
 cell(card.conference.empty()?"CONFERENCE":card.conference,1200,138,220,50,34,IM_COL32_WHITE,true,UiFont::Settings);
 UiTriggerBadge(draw,p(1145,147),p(1190,179),s,"LT");UiTriggerBadge(draw,p(1437,147),p(1482,179),s,"RT");
 cell(card.mode.empty()?"CURRENT STATS OVERALL":card.mode,630,138,470,50,28,muted,true);
 for(float x:{198.f})draw->AddTriangleFilled(p(x+16,150),p(x,163),p(x+16,176),IM_COL32_WHITE);
 for(float x:{560.f})draw->AddTriangleFilled(p(x,150),p(x+16,163),p(x,176),IM_COL32_WHITE);
 if(card.kind==2){
  const char* names[]={"TEAMS","PLAYERS"};
  for(int side=0;side<2;++side){float x=146+side*700;panel(x,224,680,576);rect(x+2,226,676,54,IM_COL32(115,0,15,255));cell(names[side],x+28,227,620,50,36);
   for(int r=0;r<10;++r){float y=280+r*51;if(r==card.selections[side])rect(x+2,y,676,51,IM_COL32(165,63,5,255));cell(card.lists[side][r],x+28,y,620,51,31);rect(x+15,y+50,650,1,border);}
  }
 }else{
  bool hero=card.kind!=1;float top=hero?448:220;int count=hero?7:12;float rh=hero?42:44;
  if(hero){panel(146,220,1382,207);auto rgb=card.color;draw->AddQuadFilled(p(148,222),p(620,222),p(520,425),p(148,425),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,190));
   draw->AddImageQuad((ImTextureID)atlas,p(148,222),p(620,222),p(520,425),p(148,425),{.79f,.65f},{.99f,.65f},{.99f,.75f},{.79f,.75f},IM_COL32(255,255,255,24));
   if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.team,""))))draw->AddImage((ImTextureID)logo,card.portrait?p(390,252):p(250,226),card.portrait?p(535,397):p(445,421));
   if(card.portrait && gpu::g_ui_guest_image)if(auto face=gpu::g_ui_guest_image(card.portrait))draw->AddImage((ImTextureID)face,p(175,222),p(365,425),{card.uv[0],card.uv[1]},{card.uv[2],card.uv[3]});
   draw->AddLine(p(620,222),p(520,425),IM_COL32(201,211,219,180),2*s);
   cell(card.player,646,228,820,59,46,IM_COL32_WHITE,true);rect(622,290,872,1,border);
   for(int i=0;i<8;++i){float x=635+(i%4)*218,y=301+(i/4)*57;cell(card.labels[i],x,y,90,50,29,muted,true);cell(card.values[i],x+94,y,105,50,38,IM_COL32(255,211,37,255),true);}
  }
  panel(146,top,1382,57+count*rh);UiBroadcastPanel(draw,p(148,top+2),p(1526,top+55),s,true);
  std::vector<int> cols;for(int c=0;c<16;++c){bool used=!card.rows[0][c].empty();for(int r=1;r<=count;++r)used|=!card.rows[r][c].empty();if(used)cols.push_back(c);}
  if(!cols.empty()){
   float firstWidth=cols.size()>2?365:650;float other=(1346-firstWidth)/std::max(1,int(cols.size())-1);
   for(int r=0;r<=count;++r){float y=top+(r?55+(r-1)*rh:0),h=r?rh:55;if(r && r==card.selected)UiBroadcastPanel(draw,p(148,y),p(1526,y+h),s,true);else if(r && r%2==0)rect(150,y,1374,h,IM_COL32(255,255,255,5));float x=174;
    for(size_t c=0;c<cols.size();++c){float w=c?other:firstWidth;cell(card.rows[r][cols[c]],x+6,y,w-12,h,r?31:32,IM_COL32_WHITE,c!=0);x+=w;}
    if(r)rect(158,y+h-1,1358,1,IM_COL32(97,113,123,220));
   }
  }
  if(!card.empty.empty())cell(card.empty,220,top+170,1230,90,38,muted,true);
 }
 rect(0,844,1672,97,IM_COL32(4,12,17,255));rect(0,844,1672,2,border);rect(0,914,1672,2,border);
 const char* buttons[]={"B","X","Y","A"};const char* labels[]={"BACK","TOGGLE STATS","PLAYER INFO","SORT"};const ImU32 colors[]={IM_COL32(220,10,28,255),IM_COL32(30,100,210,255),IM_COL32(245,181,0,255),IM_COL32(60,170,42,255)};
 int hints=card.kind==2?1:4;
 for(int i=0;i<hints;++i){float x=205+i*390;UiControllerButton(draw,p(x,881),21*s,s,buttons[i],colors[i]);cell(labels[i],x+38,856,295,50,29);}
 return true;
}

bool DrawRosterEditor(const RosterEditorCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"roster-management-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,85));UiBrandLogo(draw,p(55,15),p(310,92));
 constexpr auto gold=IM_COL32(255,211,37,255);
 cell(card.title,765,8,855,80,59,IM_COL32_WHITE,true,UiFont::Settings);rect(1030,88,590,7,red);
 auto panel=[&](float x,float y,float w,float h,bool selected=false){UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s,selected);draw->AddRect(p(x,y),p(x+w,y+h),IM_COL32(202,211,217,210),12*s,0,1.4f*s);};
 float hw=card.kind==0?554:450;panel(48,144,hw,666);
 auto rgb=card.school.color;
 draw->PushClipRect(p(51,147),p(45+hw,419),true);
 draw->AddQuadFilled(p(51,147),p(51+hw*.76f,147),p(51+hw*.40f,419),p(51,419),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,220));
 draw->AddRectFilledMultiColor(p(51,147),p(hw+45,419),IM_COL32(255,147,40,25),IM_COL32(0,0,0,70),IM_COL32(0,0,0,110),IM_COL32(0,0,0,0));
 if(gpu::g_ui_image)if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))draw->AddImage((ImTextureID)logo,p(hw*.57f,230),p(hw+25,405));
 if(card.portrait && gpu::g_ui_guest_image)if(auto face=gpu::g_ui_guest_image(card.portrait))draw->AddImage((ImTextureID)face,p(52,147),p(293,420),{card.uv[0],card.uv[1]},{card.uv[2],card.uv[3]});
 draw->PopClipRect();
 rect(51,420,hw-6,58,IM_COL32(8,15,22,240));rect(51,420,hw-6,1,border);
 std::string playerTitle=card.player;auto firstDash=playerTitle.find(" - "),lastDash=playerTitle.rfind(" - ");if(firstDash!=std::string::npos && lastDash>firstDash){auto pos=playerTitle.substr(0,firstDash),name=playerTitle.substr(firstDash+3,lastDash-firstDash-3),number=playerTitle.substr(lastDash+3);playerTitle=name.find('#')!=std::string::npos?number+" | "+pos:name+" | "+pos+" "+number;}
 cell(playerTitle,70,424,hw-42,50,36,IM_COL32_WHITE,false,UiFont::Settings);
 if(card.kind==0){
  cell(card.attributeLabels[0],78,494,hw-190,48,31,muted,false,UiFont::Settings);cell(card.attributes[0],hw-85,494,140,48,39,gold,true,UiFont::Settings);
  for(int i=1;i<6;++i){int col=i>=4?1:0,row=i>=4?i-4:i-1;float x=78+col*263,y=561+row*65;
   cell(card.attributeLabels[i],x,y,125,55,32,IM_COL32_WHITE,false,UiFont::Settings);cell(card.attributes[i],x+127,y,95,55,38,gold,true,UiFont::Settings);
  }
  rect(329,565,1,176,border);
 }else{
  cell("VS LHP",72,486,185,28,25,muted,true,UiFont::Settings);cell("VS RHP",290,486,185,28,25,muted,true,UiFont::Settings);
  for(int k=0;k<2;++k)for(int r=0;r<3;++r){float x=74+k*219,y=522+r*38;cell(card.attributeLabels[k*3+r],x,y,98,37,27,IM_COL32_WHITE,false,UiFont::Settings);cell(card.attributes[k*3+r],x+100,y,78,37,30,gold,true,UiFont::Settings);}
  rect(72,643,hw-48,1,border);
  for(int i=0;i<3;++i){float x=72+i*142;cell(card.statLabels[i],x,647,126,23,22,muted,true);cell(card.stats[i],x,668,126,25,27,gold,true,UiFont::Settings);}
  const ImU32 zoneColors[]={IM_COL32(28,55,112,255),IM_COL32(154,160,164,255),IM_COL32(146,41,36,255)};
  for(int k=0;k<2;++k){float x=146+k*213;draw->AddRect(p(x-26,696),p(x+26,748),border,0,0,s);
   for(int r=0;r<3;++r)for(int c=0;c<3;++c){float xx=x-25+c*17,yy=697+r*17;int value=std::min(2u,card.zones[k][r*3+c]);rect(xx,yy,16,16,zoneColors[value]);}
  }
  UiBroadcastPanel(draw,p(95,751),p(hw+2,795),s,true);cell(card.mode,126,751,hw-155,44,28,IM_COL32_WHITE,true,UiFont::Settings);
  draw->AddTriangleFilled(p(108,765),p(99,773),p(108,781),IM_COL32_WHITE);draw->AddTriangleFilled(p(hw-16,765),p(hw-7,773),p(hw-16,781),IM_COL32_WHITE);
 }
 auto table=[&](int k,float x,float y,float w,float h,const char* caption,int visible){
  panel(x,y,w,h);draw->PushClipRect(p(x+2,y+2),p(x+w-2,y+53),true);UiBroadcastPanel(draw,p(x,y),p(x+w,y+55),s,k==card.active);draw->PopClipRect();
  cell(caption,x+25,y+3,w-50,48,37,IM_COL32_WHITE,false,UiFont::Settings);
  float head=y+56,rowY=y+100,rh=(h-112)/visible;
  float c0=x+16,c1=x+w*.14f,c2=x+w*.29f,c3=x+w*.69f,c4=x+w*.84f;
  if(card.kind==0 && k==1){c1=x+20;c2=x+w*.27f;}
  else cell("#",c0,head,w*.09f,35,26,muted,true);
  cell(card.kind==0 && k==1?"ROLE":"POS",c1,head,w*.13f,35,26,muted,true);
  cell("PLAYER",c2,head,w*.37f,35,26,muted);
  cell(card.kind==0?"THR":"B",c3,head,w*.12f,35,26,muted,true);cell(card.kind==0?"STM":"ENG",c4,head,w*.145f,35,26,muted,true);
  for(int r=0;r<visible;++r){auto& row=card.rows[k][r];if(row[1].empty())continue;float ry=rowY+r*rh;
   if(k==card.active && r==card.selected[k])UiBroadcastPanel(draw,p(x+3,ry),p(x+w-3,ry+rh),s,true);
   if(!(card.kind==0 && k==1))cell(std::to_string(card.first[k]+r+1),c0,ry,w*.09f,rh,29,IM_COL32_WHITE,true,UiFont::Settings);
   cell(row[0],c1,ry,w*.13f,rh,29,IM_COL32_WHITE,true,UiFont::Settings);cell(row[1],c2,ry,w*.37f,rh,29,IM_COL32_WHITE,false,UiFont::Settings);
   cell(row[2],c3,ry,w*.12f,rh,28,IM_COL32_WHITE,true,UiFont::Settings);cell(row[3],c4,ry,w*.145f,rh,28,IM_COL32_WHITE,true,UiFont::Settings);
   rect(x+12,ry+rh,w-24,1,IM_COL32(135,154,167,105));
  }
  if(card.first[k]>0)cell("MORE ABOVE",x+15,y+h-14,w-30,12,12,muted,true);
 };
 if(card.kind==0){table(0,656,133,955,250,"ROTATION",3);table(1,656,405,955,405,"BULLPEN",7);}
 else if(card.kind==1){table(0,528,144,541,666,"BATTING ORDER",9);table(1,1085,144,541,666,"BENCH",7);}
 else{
  panel(518,140,1108,324);rect(521,143,1102,318,IM_COL32(1,15,23,210));const auto etch=IM_COL32(163,181,188,115);
  draw->AddBezierCubic(p(608,285),p(850,152),p(1280,152),p(1526,285),etch,2*s,48);
  draw->AddQuad(p(608,285),p(1070,449),p(1526,285),p(1070,293),etch,2*s);
  const char* positions[]={"LF","CF","RF","SS","2B","3B","1B","C"};const float dx[]={740,1055,1370,920,1190,765,1342,1055};const float dy[]={191,180,191,279,279,360,360,405};
  for(int i=0;i<8;++i){std::string player;for(const auto& row:card.rows[0])if(row[0]==positions[i]){player=row[1];break;}
   if(player.empty())continue;auto hash=player.find('#');if(hash!=std::string::npos && hash<=3)player=player.substr(hash);
   float x=dx[i]-73,y=dy[i]-33;bool selected=false;auto sel=std::clamp(card.selected[0],0,31);selected=card.active==0 && card.rows[0][sel][0]==positions[i];
   panel(x,y,146,65);rect(x+2,y+2,142,29,IM_COL32(119,5,22,255));cell(positions[i],x+3,y,140,30,25,IM_COL32_WHITE,true,UiFont::Settings);cell(player,x+5,y+30,136,33,25,IM_COL32_WHITE,true,UiFont::Settings);
   if(selected)draw->AddRect(p(x-1,y-1),p(x+147,y+66),gold,10*s,0,2*s);
  }
  table(0,518,480,541,330,"STARTING LINEUP",9);table(1,1076,480,550,330,"BENCH",7);
 }
 rect(0,848,1672,93,IM_COL32(5,12,17,255));rect(0,848,1672,2,border);rect(0,912,1672,2,border);
 const char* keys[]={"B","X","Y","A"};const char* captions[]={"BACK",card.kind==0?"SET NEXT SP":"LINEUP","PLAYER INFO","SELECT"};const ImU32 colors[]={IM_COL32(220,20,35,255),IM_COL32(30,102,211,255),IM_COL32(239,177,0,255),IM_COL32(56,177,48,255)};
 for(int i=0;i<4;++i){float x=165+i*405;UiControllerButton(draw,p(x,881),21*s,s,keys[i],colors[i]);cell(captions[i],x+41,855,315,52,29,IM_COL32_WHITE,false,UiFont::Settings);}
 return true;
}

bool DrawManagementDetail(const ManagementDetailCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"roster-management-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,85));UiBrandLogo(draw,p(55,15),p(310,92));
 constexpr auto gold=IM_COL32(255,211,37,255);cell(card.goals?"TEAM GOALS":"POSITIONAL RANKING",765,8,855,80,57,IM_COL32_WHITE,true,UiFont::Settings);rect(1030,88,590,7,red);
 auto panel=[&](float x,float y,float w,float h,bool selected=false){UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s,selected);draw->AddRect(p(x,y),p(x+w,y+h),IM_COL32(202,211,217,210),12*s,0,1.4f*s);};
 float sy=card.goals?149:244;panel(56,sy,320,790-sy);auto rgb=card.school.color;
 draw->PushClipRect(p(59,sy+3),p(373,sy+324),true);draw->AddQuadFilled(p(59,sy+3),p(373,sy+3),p(230,sy+324),p(59,sy+324),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,190));draw->PopClipRect();
 if(gpu::g_ui_image)if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))draw->AddImage((ImTextureID)logo,p(91,sy+60),p(341,sy+268));
 cell(card.school.team.name.empty()?"ROSTER RANKING":card.school.team.name,76,sy+316,280,65,48,IM_COL32_WHITE,true,UiFont::Settings);cell(card.school.team.nickname,76,sy+382,280,37,29,muted,true,UiFont::Settings);
 if(card.goals){rect(85,590,262,1,border);cell("DYNASTY GOALS",75,615,282,48,31,IM_COL32_WHITE,true,UiFont::Settings);cell("4 GOALS",75,704,282,43,28,muted,true,UiFont::Settings);}
 else{
  panel(340,134,1258,88);cell("POSITION",365,155,190,43,28,muted,true,UiFont::Settings);panel(570,150,295,57);cell(card.position,610,151,215,55,34,IM_COL32_WHITE,true,UiFont::Settings);
  draw->AddTriangleFilled(p(595,169),p(583,179),p(595,189),IM_COL32_WHITE);draw->AddTriangleFilled(p(840,169),p(852,179),p(840,189),IM_COL32_WHITE);
  UiTriggerBadge(draw,p(950,156),p(1006,202),s,"LT");UiTriggerBadge(draw,p(1490,156),p(1546,202),s,"RT");cell(card.view,1054,151,390,55,32,IM_COL32_WHITE,true,UiFont::Settings);
 }
 if(card.goals){
  panel(402,149,1214,641);draw->PushClipRect(p(405,152),p(1613,225),true);UiBroadcastPanel(draw,p(402,149),p(1616,228),s,true);draw->PopClipRect();
  cell("OFF-SEASON DYNASTY GOALS",592,162,982,55,43,IM_COL32_WHITE,false,UiFont::Settings);
  if(gpu::g_ui_image)if(auto icons=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"team-management-icons.png")))draw->AddImage((ImTextureID)icons,p(438,155),p(520,225),{.4f,.5f},{.6f,1});
  cell(card.description,438,238,1140,48,26,muted,true,UiFont::Settings);
  auto icons=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"statistics-icons-v2.png"));const auto& idx=card.icons;
  for(int r=0;r<4;++r){float y=307+r*91;panel(424,y,1170,75,r==card.selected);draw->AddRect(p(448,y+19),p(488,y+57),muted,5*s,0,1.5f*s);
   const auto& row=card.rows[0][r];if(card.completed[r]){draw->AddLine(p(455,y+37),p(465,y+48),gold,3*s);draw->AddLine(p(465,y+48),p(482,y+25),gold,3*s);}
   if(icons){float u=(idx[r]%3)/3.f,v=(idx[r]/3)/2.f;draw->AddImage((ImTextureID)icons,p(512,y+4),p(574,y+70),{u,v},{u+1.f/3,v+.5f});}
   cell(row[2]+" "+row[3],611,y+3,817,69,32,IM_COL32_WHITE,false,UiFont::Settings);cell(row[4],1443,y+3,126,69,35,r==card.selected?gold:IM_COL32_WHITE,true,UiFont::Settings);
  }
  rect(433,684,1150,1,border);cell(card.help,580,700,990,68,25,muted,false,UiFont::Settings);cell("i",450,707,75,53,37,IM_COL32_WHITE,true);
  if(card.button>=0){for(int i=0;i<2;++i){panel(750+i*340,730,300,50,i==card.button);cell(card.buttons[i],765+i*340,730,270,50,28,IM_COL32_WHITE,true,UiFont::Settings);}}
 }else{
  panel(387,246,1210,546);int count=0;for(auto& row:card.rows[1])if(!row[1].empty())++count;
  cell("ELIGIBLE PLAYERS",76,690,280,35,25,muted,true,UiFont::Settings);cell(std::to_string(count),76,730,280,46,42,gold,true,UiFont::Settings);
  // Keep both authoritative native grids: ranking slots and eligible players.
  for(int k=0;k<2;++k){float x=k?1010:404,w=k?569:589;UiBroadcastPanel(draw,p(x,249),p(x+w,305),s,k==card.active);
   cell(k?"ELIGIBLE PLAYERS":"RANKING",x+16,251,w-32,51,31,IM_COL32_WHITE,false,UiFont::Settings);
   cell("#",x+12,308,45,39,25,muted,true);cell("PLAYER",x+83,308,w-210,39,25,muted);cell("OVR",x+w-95,308,80,39,25,muted,true);
   for(int r=0;r<10;++r){const auto& row=card.rows[k][r];float y=350+r*43;bool selected=k==card.active && r==card.selections[k];if(selected)UiBroadcastPanel(draw,p(x+3,y),p(x+w-3,y+42),s,true);
    cell(std::to_string(r+1),x+12,y,45,42,29,IM_COL32_WHITE,true,UiFont::Settings);std::string player=row[1];if(player.empty())player="—";
    cell(player,x+83,y,w-210,42,29,IM_COL32_WHITE,false,UiFont::Settings);cell(row[2],x+w-95,y,80,42,30,selected?gold:IM_COL32_WHITE,true,UiFont::Settings);rect(x+12,y+42,w-24,1,IM_COL32(135,154,167,105));
   }
  }
 }
 rect(0,848,1672,93,IM_COL32(5,12,17,255));rect(0,848,1672,2,border);rect(0,912,1672,2,border);
 UiControllerButton(draw,p(166,881),21*s,s,"B",IM_COL32(220,20,35,255));cell("BACK",207,855,220,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 if(!card.goals){UiControllerButton(draw,p(770,881),21*s,s,"Y",IM_COL32(239,177,0,255));cell("PLAYER INFO",811,855,320,52,30,IM_COL32_WHITE,false,UiFont::Settings);UiControllerButton(draw,p(1335,881),21*s,s,"A",IM_COL32(56,177,48,255));cell("SELECT",1376,855,220,52,31,IM_COL32_WHITE,false,UiFont::Settings);}
 return true;
}

bool DrawRosterStatus(const RosterStatusCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"roster-management-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,85));UiBrandLogo(draw,p(55,15),p(310,92));
 constexpr auto gold=IM_COL32(255,211,37,255);cell("ROSTER STATUS",765,8,855,80,59,IM_COL32_WHITE,true,UiFont::Settings);rect(1030,88,590,7,red);
 auto panel=[&](float x,float y,float w,float h,bool selected=false){UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s,selected);draw->AddRect(p(x,y),p(x+w,y+h),IM_COL32(202,211,217,210),12*s,0,1.4f*s);};
 panel(48,144,400,610);auto rgb=card.school.color;
 draw->PushClipRect(p(51,147),p(445,449),true);draw->AddQuadFilled(p(51,147),p(400,147),p(270,449),p(51,449),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,210));
 draw->AddRectFilledMultiColor(p(51,147),p(445,449),IM_COL32(255,147,40,25),IM_COL32(0,0,0,70),IM_COL32(0,0,0,110),IM_COL32(0,0,0,0));
 if(gpu::g_ui_image)if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))draw->AddImage((ImTextureID)logo,p(257,245),p(433,388));
 if(card.portrait && gpu::g_ui_guest_image)if(auto face=gpu::g_ui_guest_image(card.portrait))draw->AddImage((ImTextureID)face,p(51,158),p(285,451),{card.uv[0],card.uv[1]},{card.uv[2],card.uv[3]});draw->PopClipRect();
 rect(51,452,394,58,IM_COL32(98,4,18,255));
 std::string title=card.player;auto d=title.find(" - "),e=title.rfind(" - ");if(d!=std::string::npos && e>d){auto pos=title.substr(0,d),name=title.substr(d+3,e-d-3),num=title.substr(e+3);title=name.find('#')!=std::string::npos?num+" | "+pos:name+" | "+pos+" "+num;}
 cell(title,71,456,354,50,35,IM_COL32_WHITE,false,UiFont::Settings);
 const int order[]={0,2,1,4,5,3,6,7};for(int i=0;i<8;++i){int index=card.labels[4]=="AVG"?order[i]:i;int col=i/4,row=i%4;float x=72+col*189,y=530+row*49;cell(card.labels[index],x,y,72,44,27,muted,false,UiFont::Settings);cell(card.values[index],x+75,y,91,44,32,gold,true,UiFont::Settings);}
 rect(251,535,1,184,border);
 panel(472,147,1156,114);panel(541,166,488,72,true);panel(1073,166,506,72);
 cell("ACTIVE PLAYERS",569,174,291,55,33,IM_COL32_WHITE,false,UiFont::Settings);cell(card.active,859,174,144,55,39,gold,true,UiFont::Settings);
 cell("INACTIVE PLAYERS",1101,174,335,55,32,IM_COL32_WHITE,false,UiFont::Settings);cell(card.inactive,1444,174,108,55,39,gold,true,UiFont::Settings);
 panel(472,285,1156,469);draw->PushClipRect(p(475,288),p(1625,353),true);UiBroadcastPanel(draw,p(472,285),p(1628,354),s,true);draw->PopClipRect();
 const float x[]={488,574,682,956,1090,1275,1450};const float w[]={72,92,253,117,170,162,135};const char* heads[]={"#","POS","PLAYER","YEAR","STATUS","SCH %","PROM."};
 for(int c=0;c<7;++c)cell(heads[c],x[c],292,w[c],54,29,IM_COL32_WHITE,c!=2,UiFont::Settings);
 for(int r=0;r<7;++r){const auto& row=card.rows[r];if(row[2].empty())continue;float y=356+r*54;
  if(r==card.selected)UiBroadcastPanel(draw,p(478,y),p(1589,y+52),s,true);
  std::string num;auto hash=row[2].rfind('#');if(hash!=std::string::npos)num=row[2].substr(hash+1);else num=std::to_string(card.first+r+1);
  cell(num,x[0],y,w[0],52,31,IM_COL32_WHITE,true,UiFont::Settings);
  for(int c=1;c<7;++c)cell(row[c],x[c],y,w[c],52,c==2?32:28,IM_COL32_WHITE,c!=2,UiFont::Settings);
  rect(485,y+53,1100,1,IM_COL32(145,164,179,110));
 }
 int active=0,inactive=0;std::sscanf(card.active.c_str(),"%d",&active);std::sscanf(card.inactive.c_str(),"%d",&inactive);int total=std::max(7,active+inactive);
 rect(1601,401,3,289,border);float thumb=std::max(32.f,289.f*7/total),offset=std::clamp(float(card.first)/std::max(1,total-7),0.f,1.f)*(289-thumb);UiBroadcastPanel(draw,p(1595,401+offset),p(1613,401+offset+thumb),s,true);
 draw->AddTriangleFilled(p(1595,387),p(1613,387),p(1604,369),IM_COL32_WHITE);draw->AddTriangleFilled(p(1595,715),p(1613,715),p(1604,733),IM_COL32_WHITE);
 rect(0,848,1672,93,IM_COL32(5,12,17,255));rect(0,848,1672,2,border);rect(0,912,1672,2,border);
 UiControllerButton(draw,p(166,881),21*s,s,"B",IM_COL32(220,20,35,255));cell("BACK",207,855,220,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 UiControllerButton(draw,p(667,881),21*s,s,"Y",IM_COL32(239,177,0,255));cell("PLAYER INFO",708,855,320,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 UiControllerButton(draw,p(1210,881),21*s,s,"A",IM_COL32(56,177,48,255));cell("TOGGLE STATUS",1251,855,360,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 return true;
}

bool DrawDepthChart(const DepthChartCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"depth-chart-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,85));UiBrandLogo(draw,p(55,15),p(310,92));
 cell("DEPTH CHART",900,8,720,80,61,IM_COL32_WHITE,true,UiFont::Settings);rect(1030,88,590,7,red);
 constexpr auto gold=IM_COL32(255,211,37,255);
 UiBroadcastPanel(draw,p(48,161),p(317,748),s);
 auto rgb=card.school.color;
 draw->AddQuadFilled(p(51,164),p(313,164),p(313,390),p(51,510),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,150));

 if(gpu::g_ui_image)if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))draw->AddImage((ImTextureID)logo,p(68,211),p(297,424));
 cell(card.school.team.name.empty()?"ROSTER OVERVIEW":card.school.team.name,65,449,235,58,47,IM_COL32_WHITE,true,UiFont::Settings);
 cell(card.school.team.name.empty()?"SELECT A POSITION":card.school.team.nickname,65,510,235,37,28,muted,true,UiFont::Settings);
 rect(78,564,210,1,border);cell("TOTAL PLAYERS",63,599,239,40,28,muted,true,UiFont::Settings);cell(card.total,65,643,235,72,70,gold,true,UiFont::Settings);
 UiBroadcastPanel(draw,p(338,134),p(1626,809),s);
 rect(341,137,1282,669,IM_COL32(0,14,21,220));
 draw->PushClipRect(p(342,138),p(1622,805),true);
 const auto etch=IM_COL32(151,169,174,100);
 draw->AddQuadFilled(p(550,607),p(977,425),p(1404,607),p(977,777),IM_COL32(88,108,111,25));
 draw->AddLine(p(358,418),p(977,777),etch,2*s);draw->AddLine(p(977,777),p(1595,418),etch,2*s);
 draw->AddLine(p(550,607),p(977,425),etch,2*s);draw->AddLine(p(977,425),p(1404,607),etch,2*s);
 draw->AddBezierCubic(p(358,418),p(650,190),p(1295,190),p(1595,418),etch,2*s,64);
 draw->AddCircle(p(977,777),71*s,etch,48,1*s);draw->AddCircle(p(977,561),42*s,etch,32,1*s);
 for(auto base:std::array<ImVec2,4>{{{550,607},{977,425},{1404,607},{977,777}}})draw->AddQuadFilled(p(base.x-16,base.y),p(base.x,base.y-9),p(base.x+16,base.y),p(base.x,base.y+9),IM_COL32(203,211,216,180));
 draw->PopClipRect();
 const float xs[]={429,830,1232,606,1040,405,824,1248,621,1036};
 const float ys[]={160,152,160,309,309,460,458,458,622,622};
 for(int i=0;i<10;++i){const auto& item=card.positions[i];float x=xs[i],y=ys[i],w=304,h=std::max(124.f,item.rows[2][0].empty()?124.f:140.f);
  UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s);
  draw->PushClipRect(p(x+2,y+2),p(x+w-2,y+40),true);UiBroadcastPanel(draw,p(x,y),p(x+w,y+43),s,true);draw->PopClipRect();
  if(item.selected){draw->AddRect(p(x-2,y-2),p(x+w+2,y+h+2),gold,10*s,0,2*s);draw->AddTriangleFilled(p(x+11,y+12),p(x+11,y+28),p(x+23,y+20),gold);}
  cell(item.label+" "+item.count,x+28,y+1,w-56,40,34,IM_COL32_WHITE,true,UiFont::Settings);
  for(int r=0;r<3;++r){const auto& row=item.rows[r];if(row[0].empty())continue;float ry=y+43+r*30;
   if(r)rect(x+12,ry,w-24,1,IM_COL32(122,142,158,65));
   std::string name=row[0];auto number=name.find('#');if(number!=std::string::npos && number<=3)name=name.substr(number);
   cell(name,x+24,ry,174,29,28,IM_COL32_WHITE,false,UiFont::Settings);
   cell(row[2],x+202,ry,43,29,26,muted,true,UiFont::Settings);cell(row[1],x+253,ry,40,29,28,gold,true,UiFont::Settings);
  }
 }
 if(card.overlay){
  rect(0,112,1672,735,IM_COL32(0,4,8,210));UiBroadcastPanel(draw,p(360,164),p(1312,796),s);
  cell(card.actions?"PLAYER ACTIONS":"ROSTER",392,177,884,54,43,IM_COL32_WHITE,false,UiFont::Settings);
  if(card.actions){for(int r=0;r<4;++r){if(card.commands[r].empty())continue;float y=265+r*96;UiBroadcastPanel(draw,p(393,y),p(1279,y+76),s,r==card.action);cell(card.commands[r],420,y,833,76,37,IM_COL32_WHITE,false,UiFont::Settings);}}
  else{
   cell("PLAYER",400,235,540,40,26,muted);cell("OVR",1000,235,105,40,26,muted,true);cell("YEAR",1150,235,100,40,26,muted,true);
   for(int r=0;r<10 && card.first+r<30;++r){int index=card.first+r;const auto& row=card.players[index];if(row[0].empty())continue;float y=282+r*46;
    if(index==card.selected)UiBroadcastPanel(draw,p(392,y),p(1280,y+45),s,true);
    cell(row[0],412,y,540,45,29);cell(row[1],1000,y,105,45,29,gold,true);cell(row[2],1150,y,100,45,27,muted,true);rect(404,y+45,864,1,IM_COL32(139,157,171,80));
   }
  }
 }
 rect(0,848,1672,93,IM_COL32(5,12,17,255));rect(0,848,1672,2,border);rect(0,912,1672,2,border);
 draw->AddQuadFilled(p(76,849),p(114,849),p(76,912),p(38,912),red);draw->AddQuadFilled(p(1593,849),p(1631,849),p(1593,912),p(1555,912),red);
 UiControllerButton(draw,p(166,881),21*s,s,"B",IM_COL32(220,20,35,255));cell("BACK",207,855,220,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 UiControllerButton(draw,p(1335,881),21*s,s,"A",IM_COL32(56,177,48,255));cell(card.overlay?"SELECT":"ADVANCE",1376,855,180,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 return true;
}

bool DrawTeamManagement(const TeamManagementCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"team-management-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,100));UiBrandLogo(draw,p(55,15),p(310,92));
 cell("TEAM MANAGEMENT",805,8,815,80,59,IM_COL32_WHITE,true,UiFont::Settings);rect(1020,88,600,7,red);
 cell(card.date,480,114,720,25,22,muted,true);
 UiBroadcastPanel(draw,p(90,147),p(1582,309),s);
 auto rgb=card.school.color;
 draw->AddQuadFilled(p(93,150),p(443,150),p(350,306),p(93,306),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,180));
 region(93,150,1486,156,720,8,675,103,IM_COL32(255,255,255,22));
 if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))draw->AddImage((ImTextureID)logo,p(172,154),p(335,303));
 cell(card.school.team.name,470,169,320,71,59,IM_COL32_WHITE,false,UiFont::Settings);
 cell(card.school.team.nickname,477,244,303,43,32,muted,false,UiFont::Settings);
 cell(card.school.team.rank,830,174,185,69,60,IM_COL32_WHITE,true,UiFont::Settings);cell("NATIONAL RANK",829,258,185,28,24,muted,true);
 cell("TEAM PRESTIGE",1053,175,276,32,27,muted,true,UiFont::Settings);
 if(card.school.prestige>=0)UiPrestigeStars(draw,p(1090,241),18*s,40*s,card.school.prestige);
 cell("RECORD",1380,174,173,32,27,muted,true,UiFont::Settings);cell(card.record.empty()?"—":card.record,1373,219,185,69,55,IM_COL32(255,211,37,255),true,UiFont::Settings);
 for(float x:{444.f,809.f,1034.f,1356.f})rect(x,171,1,110,IM_COL32(151,169,181,170));
 UiBroadcastPanel(draw,p(90,340),p(1582,673),s);
 const auto icons=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"team-management-icons.png"));
 for(int i=0;i<10;++i){float x=128+(i%5)*287,y=369+(i/5)*145;bool selected=i==card.selected;
  UiBroadcastPanel(draw,p(x,y),p(x+272,y+126),s,selected);
  if(icons){float u=(i%5)/5.f,v=(i/5)/2.f;draw->AddImage((ImTextureID)icons,p(x+101,y),p(x+171,y+70),{u,v},{u+.2f,v+.5f},card.enabled[i]?IM_COL32_WHITE:IM_COL32(155,165,174,145));}
  std::string first=card.menu[i],second;
  auto split=first.find(' ',first.size()/2);if(split==std::string::npos)split=first.rfind(' ');
  if(split!=std::string::npos){second=first.substr(split+1);first.resize(split);}
  auto color=card.enabled[i]?IM_COL32_WHITE:IM_COL32(133,145,155,255);
  cell(first,x+10,y+67,252,29,30,color,true,UiFont::Settings);cell(second,x+10,y+96,252,27,29,color,true,UiFont::Settings);
 }
 UiBroadcastPanel(draw,p(90,704),p(1582,810),s);
 draw->AddQuadFilled(p(93,707),p(465,707),p(407,807),p(93,807),IM_COL32(119,3,20,255));
 cell("TEAM GRADES",117,726,280,58,37,IM_COL32_WHITE,true,UiFont::Settings);
 auto statsIcons=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"statistics-icons-v2.png"));
 const char* categories[]={"PITCH","HIT","FIELD","SPEED"};const int indices[]={1,0,2,3};
 for(int i=0;i<4;++i){float x=462+i*130;
  if(statsIcons){int icon=indices[i];float u=(icon%3)/3.f,v=(icon/3)/2.f;draw->AddImage((ImTextureID)statsIcons,p(x+23,700),p(x+89,766),{u,v},{u+1.f/3,v+.5f});}
  cell(card.grades[i].empty()?"—":card.grades[i],x,754,112,46,39,IM_COL32(255,211,37,255),true,UiFont::Settings);
  if(!statsIcons)cell(categories[i],x,712,112,17,15,muted,true);
 }
 rect(989,724,1,66,IM_COL32(151,169,181,170));rect(1270,724,1,66,IM_COL32(151,169,181,170));
 cell("COACH LEVELS",1010,714,239,30,25,muted,true,UiFont::Settings);
 std::string coach;for(int i=0;i<4;++i){if(i)coach+=" / ";coach+=card.coach[i].empty()?"—":card.coach[i];}
 cell(coach,1004,754,251,43,31,IM_COL32(255,211,37,255),true,UiFont::Settings);
 cell("SCOUTING",1300,714,250,30,25,muted,true,UiFont::Settings);cell(card.scouting.empty()?"—":card.scouting,1300,749,250,50,40,IM_COL32(255,211,37,255),true,UiFont::Settings);
 rect(0,848,1672,93,IM_COL32(5,12,17,255));rect(0,848,1672,2,border);rect(0,912,1672,2,border);
 UiControllerButton(draw,p(185,881),21*s,s,"B",IM_COL32(220,20,35,255));cell("BACK",225,855,220,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 UiControllerButton(draw,p(1335,881),21*s,s,"A",IM_COL32(56,177,48,255));cell("ADVANCE",1376,855,220,52,31,IM_COL32_WHITE,false,UiFont::Settings);
 return true;
}

bool DrawStatisticsMenu(const StatisticsMenuCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"statistics-menu-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,100));UiBrandLogo(draw,p(55,15),p(310,92));
 cell("STATISTICS",1070,5,550,85,66,IM_COL32_WHITE,true,UiFont::Settings);rect(1060,91,550,5,red);
 draw->AddLine(p(345,174),p(536,174),border,2*s);draw->AddLine(p(1135,174),p(1325,174),border,2*s);
 draw->AddRect(p(563,153),p(600,191),IM_COL32_WHITE,3*s,0,3*s);
 for(int i=0;i<2;++i)draw->AddLine(p(573+i*16,148),p(573+i*16,159),IM_COL32_WHITE,3*s);
 for(int r=0;r<2;++r)for(int c=0;c<3;++c)rect(570+c*10,168+r*9,4,4,IM_COL32_WHITE);
 const bool tournament=caps(card.menu[4]).find("TOURNAMENT")!=std::string::npos;
 cell(tournament?"TOURNAMENT OVERVIEW":card.date,637,146,489,55,39);
 const auto rgb=card.school.color;
 UiBroadcastPanel(draw,p(165,237),p(1507,438),s);
 draw->AddQuadFilled(p(168,240),p(518,240),p(411,435),p(168,435),IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,145));
 region(168,240,1336,192,720,8,675,103,IM_COL32(255,255,255,30));
 draw->AddLine(p(518,239),p(410,436),IM_COL32_WHITE,3*s);
 draw->AddLine(p(466,335),p(1505,335),border,2*s);
 if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))draw->AddImage((ImTextureID)logo,p(223,253),p(400,427));
 cell(card.school.team.name+" "+card.school.team.nickname,585,250,870,74,62);
 const float xs[]={489,726,994,1252},ws[]={202,249,250,222};
 if(tournament){
  cell(card.school.team.rank,535,347,395,43,45,IM_COL32_WHITE,true);
  cell(card.record.empty()?"—":card.record,1010,347,395,43,45,IM_COL32(255,218,88,255),true);
  cell("NATIONAL RANK",535,393,395,29,25,muted,true);cell("TOURNAMENT RECORD",1010,393,395,29,25,muted,true);
  rect(970,350,1,72,border);
 }else{
 cell(card.school.team.rank,xs[0],347,ws[0],43,45,IM_COL32_WHITE,true);
 if(card.school.prestige>=0)UiPrestigeStars(draw,p(765,371),16*s,31*s,card.school.prestige);else cell("N/A",726,347,249,43,36,muted,true);
 cell(card.school.standing.empty()?"---":card.school.standing,xs[2],347,ws[2],43,45,IM_COL32_WHITE,true);
 cell(card.record.empty()?"—":card.record,xs[3],347,ws[3],43,45,IM_COL32(255,218,88,255),true);
 const char* labels[]={"NATIONAL RANK","TEAM PRESTIGE","CONF. STANDING","RECORD"};
 for(int i=0;i<4;++i){cell(labels[i],xs[i],393,ws[i],29,25,muted,true);if(i<3)rect(xs[i]+ws[i]+4,350,1,72,border);}
 }
 draw->AddRect(p(165,237),p(1507,438),border,5*s,0,s);
 auto iconAtlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"statistics-icons-v2.png"));
 for(int i=0;i<6;++i){
  const float x=156+(i%3)*461,y=496+(i/3)*141,w=441,h=117;const bool selected=card.selected==i;
  const bool available=card.enabled[i] && !card.menu[i].empty();
  UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s,selected);
  draw->AddLine(p(x+121,y+20),p(x+121,y+h-20),IM_COL32(174,191,203,selected?110:45),s);
  if(iconAtlas){float u=(i%3)/3.f,v=(i/3)/2.f;draw->AddImage((ImTextureID)iconAtlas,p(x+5,y+2),p(x+119,y+116),{u,v},{u+1.f/3,v+.5f},available?IM_COL32_WHITE:IM_COL32(140,148,155,200));}
  cell(card.menu[i].empty()?(i==5?"CAREER STATISTICS":"UNAVAILABLE"):card.menu[i],x+139,y+20,w-157,49,37,available?IM_COL32_WHITE:IM_COL32(118,128,137,255),true,UiFont::Settings);
  const char* subtitles[]={"HITTING & PRODUCTION","PITCHING PERFORMANCE","DEFENSE & ERRORS","SPEED & STEALS","LEAGUE LEADERBOARDS","PLAYER CAREER HISTORY"};
  text(UiFont::Prompt,i==5&&!available?"AVAILABLE IN DYNASTY":subtitles[i],x+143,y+77,19,w-164,IM_COL32(191,202,212,255),true);
 }
 rect(0,844,1672,97,IM_COL32(6,11,15,245));rect(0,844,1672,2,border);rect(0,906,1672,2,border);
 for(int i=0;i<2;++i){float x=i?1270:101;auto c=i?IM_COL32(76,189,60,255):IM_COL32(225,35,45,255);UiControllerButton(draw,p(x,875),20*s,s,i?"A":"B",c);cell(i?"ADVANCE":"BACK",x+35,852,250,48,28);}
 return true;
}

bool DrawLeagueTable(const LeagueTableCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"league-tables-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,100));UiBrandLogo(draw,p(55,15),p(310,92));
 cell(card.standings?"CONFERENCE STANDINGS":"TOP 25 RANK",900,5,720,85,62,IM_COL32_WHITE,true);rect(1060,91,550,5,red);
 const float tx=card.standings?144.f:252.f,tw=card.standings?1384.f:1310.f;
 const float hy=card.standings?216.f:214.f,rh=card.standings?44.f:45.f,ry=hy+54;
 if(!card.standings){
  // Decorative stitched baseball at the left edge.
  draw->PushClipRect(p(0,112),p(249,850),true);
  draw->AddCircleFilled(p(-80,650),280*s,IM_COL32(150,162,168,27),96);
  for(int i=0;i<22;++i){float y=370+i*20,x=170-std::pow((y-580)/26,2);draw->AddLine(p(x-12,y-7),p(x+12,y+7),IM_COL32(148,158,165,65),3*s);}
  draw->PopClipRect();
  draw->AddRectFilledMultiColor(p(tx,134),p(tx+tw,212),IM_COL32(152,0,20,255),IM_COL32(18,26,31,255),IM_COL32(18,26,31,255),IM_COL32(100,0,15,255));
  // Trophy glyph, drawn independently of text.
  draw->AddQuadFilled(p(312,150),p(350,150),p(343,177),p(319,177),IM_COL32_WHITE);
  draw->AddLine(p(331,173),p(331,193),IM_COL32_WHITE,5*s);rect(318,193,26,5,IM_COL32_WHITE);
  draw->AddBezierCubic(p(314,154),p(291,146),p(297,179),p(325,181),IM_COL32_WHITE,3*s);
  draw->AddBezierCubic(p(349,154),p(371,146),p(364,179),p(337,181),IM_COL32_WHITE,3*s);
  cell("TOP 25 RANK",379,141,850,63,56);
 }else{
  UiBroadcastPanel(draw,p(615,125),p(1057,191),s);
  UiTriggerBadge(draw,p(629,141),p(677,175),s,"LT");
  UiTriggerBadge(draw,p(995,141),p(1043,175),s,"RT");
  cell(card.conference,680,129,311,58,44,IM_COL32_WHITE,true);
  draw->AddLine(p(350,159),p(576,159),border,2*s);draw->AddLine(p(1096,159),p(1323,159),border,2*s);
 }
 UiBroadcastPanel(draw,p(tx,hy),p(tx+tw,hy+54+12*rh),s);
 UiBroadcastPanel(draw,p(tx,hy),p(tx+tw,hy+54),s,true);
 if(!card.standings){
  cell("#",tx+19,hy+3,110,48,36,IM_COL32_WHITE,true);cell("TEAM",tx+232,hy+3,570,48,36);
  cell("RECORD",tx+800,hy+3,170,48,36,IM_COL32_WHITE,true);cell("LAST WEEK",tx+1035,hy+3,235,48,36,IM_COL32_WHITE,true);
 }else{
  cell("#",tx+9,hy+3,100,48,34,IM_COL32_WHITE,true);cell("TEAM",tx+235,hy+3,300,48,34);
  const char* defaults[]={"CONF","PCT","GB","OVR","PCT","L10"};
  for(int c=2;c<8;++c){auto label=card.rows[0][c];if(label.empty())label=defaults[c-2];if(label=="W%")label="PCT";cell(label,tx+550+(c-2)*133,hy+3,130,48,34,IM_COL32_WHITE,true);}
 }
 int first=std::clamp(card.first,0,card.standings?19:13);
 // Standings row zero contains the native column headings.
 for(int i=0;i<12;++i){
  int r=card.standings?i+1:first+i;if(r>=32)break;const auto& row=card.rows[r];float y=ry+i*rh;
  const bool selected=r==(card.standings?card.selected-card.first:card.selected),managed=!row[1].empty() && caps(row[1])==caps(card.managedTeam);
  if(managed || selected)UiBroadcastPanel(draw,p(tx+3,y),p(tx+tw-3,y+rh),s,true);else if(i%2)rect(tx+3,y,tw-6,rh,IM_COL32(255,255,255,5));
  if(!row[1].empty()){
   cell(row[0],tx+10,y+2,106,rh-4,37,card.standings?IM_COL32_WHITE:IM_COL32(255,218,88,255),true);
   auto logo=overhaul::TeamLogo(row[1]);if(!logo.empty())if(auto texture=gpu::g_ui_image(overhaul::UiAsset(logo)))draw->AddImage((ImTextureID)texture,p(tx+156,y+1),p(tx+156+rh-2,y+rh-1));
   cell(row[1],tx+235,y+2,card.standings?305:540,rh-4,36);
   if(card.standings){for(int c=2;c<8;++c)cell(row[c],tx+550+(c-2)*133,y+2,130,rh-4,35,IM_COL32_WHITE,true);}
   else {cell(row[2],tx+800,y+2,170,rh-4,37,IM_COL32_WHITE,true);cell(row[3],tx+1035,y+2,235,rh-4,37,IM_COL32_WHITE,true);}
  }
  draw->AddLine(p(tx+5,y+rh),p(tx+tw-5,y+rh),IM_COL32(100,113,122,190),s);
 }
 draw->AddRect(p(tx,card.standings?hy:134),p(tx+tw,ry+12*rh),border,card.standings?15*s:0,0,3*s);
 rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);
 UiControllerButton(draw,p(91,901),20*s,s,"B",IM_COL32(225,35,45,255));cell("BACK",126,878,250,48,28);
 draw->AddQuadFilled(p(1585,862),p(1610,862),p(1582,917),p(1557,917),red);
 return true;
}

bool DrawStatsLanding(const StatsLandingCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"stats-landing-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false,UiFont role=UiFont::Heading){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,role,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
 if(auto art=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"stats-landing-players.png")))
   draw->AddImage((ImTextureID)art,p(1160,110),p(1690,858),{.27f,0},{1,1});
 region(0,0,1672,110,10,8,1394,103,IM_COL32(255,255,255,100));UiBrandLogo(draw,p(55,15),p(310,92));
 cell("STATS",1260,5,360,85,70,IM_COL32_WHITE,true,UiFont::Settings);rect(1060,91,550,5,red);
 auto panel=[&](float x,float y,float w,float h,ImU32){UiBroadcastPanel(draw,p(x,y),p(x+w,y+h),s);};
 auto slant=[&](float x,float y,float w,float h,ImU32 color){
  draw->AddQuadFilled(p(x+30,y),p(x+w,y),p(x+w-30,y+h),p(x,y+h),color);
  ImVec2 v[]={p(x+30,y),p(x+w,y),p(x+w-30,y+h),p(x,y+h)};
  draw->AddPolyline(v,4,IM_COL32_WHITE,ImDrawFlags_Closed,2*s);
 };
 slant(168,138,724,61,IM_COL32(118,3,19,255));
 // Calendar glyph.
 draw->AddRect(p(223,151),p(259,186),IM_COL32_WHITE,2*s,0,3*s);
 for(int i=0;i<2;++i)draw->AddLine(p(233+i*15,146),p(233+i*15,157),IM_COL32_WHITE,3*s);
 for(int r=0;r<2;++r)for(int c=0;c<3;++c)rect(231+c*9,165+r*9,4,4,IM_COL32_WHITE);
 cell(card.date,290,141,560,53,41);
 panel(168,224,1137,202,IM_COL32(11,19,24,245));
 auto rgb=card.school.color;auto color=IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,225);
 draw->AddQuadFilled(p(171,227),p(474,227),p(366,423),p(171,423),color);
 rect(478,227,824,104,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,160));
 region(478,227,824,104,720,8,675,103,IM_COL32(255,255,255,40));
 draw->AddLine(p(474,226),p(366,424),IM_COL32_WHITE,2*s);
 draw->AddLine(p(417,334),p(1303,334),border,2*s);
 if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.school.team.name,card.school.team.nickname))))
  draw->AddImage((ImTextureID)logo,p(200,244),p(403,401));
 cell(card.school.team.name,511,228,740,42,42);
 cell(card.school.team.nickname,500,269,775,57,63);
 cell(card.school.team.rank,420,338,190,45,45,IM_COL32_WHITE,true);
 cell("NATIONAL RANK",411,386,215,28,24,muted,true);
 if(card.school.prestige>=0)UiPrestigeStars(draw,p(678,360),16*s,31*s,card.school.prestige);
 cell("TEAM PRESTIGE",634,386,237,28,24,muted,true);
 cell(card.school.standing.empty()?"---":card.school.standing,877,338,210,45,43,IM_COL32(255,218,88,255),true);
 cell("CONF. STANDING",871,386,230,28,24,muted,true);
 cell(card.school.location,1096,340,195,40,24,muted,true);
 cell("LOCATION",1096,386,195,28,24,muted,true);
 for(int i=0;i<3;++i)rect(626+i*238,349,1,62,border);
 auto icons=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"statistics-icons-v2.png"));
 for(int i=0;i<4;++i){float y=459+i*86;bool selected=i==card.selected;
  UiBroadcastPanel(draw,p(124,y),p(689,y+73),s,selected);
  int icon=i==0 || i==3?5:4;
  if(icons){float u=(icon%3)/3.f,v=(icon/3)/2.f;draw->AddImage((ImTextureID)icons,p(136,y-10),p(230,y+84),{u,v},{u+1.f/3,v+.5f},card.enabled[i]?IM_COL32_WHITE:IM_COL32(130,140,150,160));}
  cell(card.menu[i],256,y+4,400,65,43,card.enabled[i]?IM_COL32_WHITE:IM_COL32(116,124,132,255),false,UiFont::Settings);
 }
 panel(723,459,582,305,IM_COL32(12,19,29,245));
 UiBroadcastPanel(draw,p(725,461),p(1303,519),s,true);
 cell("RECORD:",747,464,200,51,44);cell(card.record.empty()?"—":card.record,959,464,310,51,46,IM_COL32(255,218,88,255));
 UiBroadcastPanel(draw,p(725,520),p(1303,694),s);
 const char* headings[]={"PITCH","HIT","FIELD","SPEED"};
 for(int i=0;i<4;++i){float x=934+i*87;cell(headings[i],x,533,81,36,24,muted,true);
  cell(card.grades[i].empty()?"—":card.grades[i],x,581,81,48,38,IM_COL32_WHITE,true);
  cell(card.coach[i].empty()?"—":card.coach[i],x,637,81,45,39,IM_COL32(255,218,88,255),true);
 }
 cell("TEAM",749,582,174,44,34);cell("COACH LVL",749,638,174,44,34);
 rect(918,579,1,103,border);rect(928,629,366,1,border);
 UiBroadcastPanel(draw,p(725,696),p(1303,762),s,true);cell("SCOUTING:",746,701,238,56,42);
 cell(card.scouting.empty()?"—":card.scouting,992,701,292,56,44,IM_COL32(255,218,88,255));
 rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);
 for(int i=0;i<2;++i){float x=i?1135:91;auto c=i?IM_COL32(76,189,60,255):IM_COL32(225,35,45,255);UiControllerButton(draw,p(x,901),20*s,s,i?"A":"B",c);cell(i?"ADVANCE":"BACK",x+35,878,250,48,28);}
 return true;
}

bool DrawRecruiting(const RecruitingCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"recruiting-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);
 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);region(0,0,1672,110,10,8,1394,103);UiBrandLogo(draw,p(55,15),p(310,92));cell("RECRUITING",1040,5,580,85,65,IM_COL32_WHITE,true);rect(1060,91,550,5,red);
 region(168,142,1336,226,32,140,1325,236);
 auto rgb=card.school.color;auto color=IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,200);
 draw->AddQuadFilled(p(171,145),p(531,145),p(407,365),p(171,365),color);rect(534,145,967,122,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,60));
 auto path=overhaul::TeamLogo(card.school.team.name,card.school.team.nickname);if(!path.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(205,163),p(455,340));
 cell(card.school.team.name,570,148,850,40,39);cell(card.school.team.nickname,570,188,860,72,66);
 cell(card.school.location,1180,274,290,27,23,muted,true);
 cell(card.school.team.rank,490,280,190,40,42,IM_COL32_WHITE,true);cell("NATIONAL RANK",474,323,212,29,24,muted,true);
 if(card.school.prestige>=0)UiPrestigeStars(draw,p(744,302),16*s,32*s,card.school.prestige);cell("TEAM PRESTIGE",712,323,260,29,24,muted,true);
 cell(card.school.standing,1000,280,225,40,41,IM_COL32(255,218,88,255),true);cell("CONF. STANDING",992,323,240,29,24,muted,true);
 cell("RECRUITING",1252,315,220,37,27,muted,true);
 for(int i=0;i<4;++i){float y=394+i*80;bool selected=i==card.selected;rect(354,y,966,72,selected?IM_COL32(169,2,23,255):IM_COL32(12,20,24,255));
  // Keep the supplied icons at their authored aspect ratio; tint only the selected row.
  region(356,y+2,962,68,46,405+i*79,929,71,selected?IM_COL32(255,80,90,210):i==0?IM_COL32(65,78,87,255):IM_COL32_WHITE);
  if(selected && i!=0)draw->AddTriangleFilled(p(369,y+26),p(369,y+46),p(382,y+36),IM_COL32_WHITE);
  cell(card.menu[i],490,y+5,794,62,44);draw->AddRect(p(354,y),p(1320,y+72),selected?IM_COL32_WHITE:border,0,0,2*s);
 }
 region(374,735,910,80,32,728,944,69);cell("RECRUIT POINTS",517,741,470,65,45);cell(card.points,1040,740,220,67,58,IM_COL32(255,218,88,255),true);
 rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);
 for(int i=0;i<2;++i){float x=i?1135:91;auto c=i?IM_COL32(76,189,60,255):IM_COL32(225,35,45,255);UiControllerButton(draw,p(x,901),22*s,s,i?"A":"B",c);cell(i?"ADVANCE":"BACK",x+35,878,250,48,28);}
 return true;
}

bool DrawBoxScore(const BoxScoreCard& card) {
 if(!gpu::g_ui_image)return false;const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"box-score-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);



 UiMenuBackdrop(draw,screen,UiBackdrop::Steel);region(0,0,1672,110,14,12,1380,98);UiBrandLogo(draw,p(55,15),p(310,92));cell("BOX SCORE",1040,5,580,85,65,IM_COL32_WHITE,true);rect(1060,91,550,5,red);
 rect(190,142,1288,168,IM_COL32(8,14,18,255));rect(192,144,1284,45,IM_COL32(105,4,16,255));
 for(int r=0;r<3;++r)for(int c=0;c<13;++c){float x=c?580+(c-1)*74.8333f:190,w=c?74.8333f:390,y=142+(r?48+(r-1)*60:0),hh=r?60:48;
  if(r && !c){auto path=overhaul::TeamLogo(card.teams[r-1].name,card.teams[r-1].nickname);if(!path.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(x+34,y+4),p(x+34+hh-8,y+hh-4));cell(card.teams[r-1].name,x+115,y,w-127,hh,34);}else cell(card.score[r][c],x+4,y,w-8,hh,r?39:33,IM_COL32_WHITE,true);draw->AddRect(p(x,y),p(x+w,y+hh),border,0,0,s);
 }
 rect(110,340,1452,480,IM_COL32(9,15,19,255));region(112,342,1448,60,24,339,1340,62);rect(141,349,17,44,red);cell("GAME STATISTICS",183,345,1200,53,44);
 // Preserve the native viewport and its section/header rows while replacing ASCII dividers.
 auto separator=[](const std::string& v){return !v.empty() && v.find_first_not_of("-_= ")==std::string::npos;};
 const float widths[9]={510,110,110,95,95,110,95,95,160};
 int visible=0;for(int r=0;r<11;++r){bool any=false,sep=false;for(auto& v:card.rows[r]){any|=!v.empty();sep|=separator(v);}if(!any || sep)continue;++visible;}
 float rh=std::min(52.f,405.f/std::max(1,visible)),yy=407;
 for(int r=0;r<11;++r){bool any=false,sep=false;for(auto& v:card.rows[r]){any|=!v.empty();sep|=separator(v);}if(!any || sep)continue;
  if(r==card.selected)rect(130,yy,1410,rh,IM_COL32(176,3,24,255));float xx=145;
  bool note=true;for(int c=1;c<9;++c)note &= card.rows[r][c].empty();
  if(note)cell(card.rows[r][0],150,yy,1370,rh,31);
  else for(int c=0;c<9;++c){cell(card.rows[r][c],xx+5,yy,widths[c]-10,rh,c?32:34,IM_COL32_WHITE,c>0);xx+=widths[c];}
  draw->AddLine(p(140,yy+rh),p(1540,yy+rh),border,s);yy+=rh;
 }
 draw->AddRect(p(110,340),p(1562,820),border,0,0,2*s);
 rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);UiControllerButton(draw,p(91,901),20*s,s,"B",IM_COL32(225,35,45,255));cell("BACK",126,878,250,48,28);
 cell("UP / DOWN  SCROLL TEAMS & STATISTICS",480,878,1030,48,27,IM_COL32(188,197,205,255),true);
 if(card.total>11){const auto position="ROW "+std::to_string(card.first+1)+" / "+std::to_string(card.total);cell(position,1120,345,405,53,25,IM_COL32(188,197,205,255),true);}
 return true;
}

bool DrawFinalScore(const FinalScoreCard& card) {
  if(!gpu::g_ui_image)return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"final-score-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);


  UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
  region(0,0,1672,110,14,12,1380,98);UiBrandLogo(draw,p(55,15),p(310,92));
  cell("FINAL SCORE",1040,5,580,85,65,IM_COL32_WHITE,true);rect(1060,91,550,5,red);
  cell(card.outcome,665,119,340,45,42,IM_COL32(255,223,125,255),true);
  for(int i=0;i<2;++i){const auto& team=card.teams[i];float x=i?880:78,y=171,w=735,h=144;unsigned rgb=card.colors[i];auto color=IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,255);
    draw->AddQuadFilled(p(x+45,y),p(x+w,y),p(x+w-45,y+h),p(x,y+h),color);
    draw->AddImageQuad((ImTextureID)atlas,p(x+45,y),p(x+w,y),p(x+w-45,y+h),p(x,y+h),{80.f/1774,185.f/887},{640.f/1774,185.f/887},{595.f/1774,309.f/887},{35.f/1774,309.f/887},IM_COL32(255,255,255,60));
    draw->AddQuad(p(x+45,y),p(x+w,y),p(x+w-45,y+h),p(x,y+h),border,2*s);
    const auto path=overhaul::TeamLogo(team.name,team.nickname);if(!path.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1400:145,y+9),p(i?1558:303,y+135));
    cell(team.name,i?1108:340,y+5,280,40,33);cell(team.nickname,i?1108:340,y+45,280,81,51);
    cell(card.scores[i],i?929:650,y+7,115,130,100,IM_COL32_WHITE,true);
  }
  cell("AT",785,196,100,94,58,IM_COL32_WHITE,true);
  rect(113,337,1445,368,IM_COL32(9,15,19,255));region(115,339,1441,63,24,339,1340,62);cell("GAME SUMMARY",201,340,1100,60,46);
  for(int i=0;i<2;++i){float x=i?1099:115;const auto& player=card.players[i];unsigned rgb=card.colors[i];region(x,405,457,240,32,416,398,203);rect(x,405,457,240,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,42));
    auto path=overhaul::TeamLogo(card.teams[i].name,card.teams[i].nickname);if(!path.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1342:146,425),p(i?1532:336,600),{0,0},{1,1},IM_COL32(255,255,255,45));
    if(player.portrait && gpu::g_ui_guest_image)if(auto face=gpu::g_ui_guest_image(player.portrait))draw->AddImage((ImTextureID)face,p(i?1190:280,418),p(i?1440:530,645),{player.uv[0],player.uv[1]},{player.uv[2],player.uv[3]});
    rect(x,646,457,57,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,180));cell(card.results[i],x+14,647,429,54,31,IM_COL32_WHITE,true);
  }
  for(int r=0;r<5;++r){float y=406+r*57;const auto& a=card.players[0].stats[r];const auto& z=card.players[1].stats[r];cell(a[1],595,y,145,56,42,IM_COL32_WHITE,true);cell(a[0],752,y,166,56,34,muted,true);cell(z[1],932,y,145,56,42,IM_COL32_WHITE,true);if(r<4)draw->AddLine(p(575,y+57),p(1098,y+57),border,s);}
  draw->AddRect(p(113,337),p(1558,705),border,0,0,2*s);draw->AddLine(p(113,403),p(1558,403),border,s);
  for(int i=0;i<(int)card.actions.size();++i){float w=card.actions.size()==2?480:350,x=card.actions.size()==2?330+i*530:260+i*390,y=738,h=82;bool selected=i==card.selected;
    draw->AddQuadFilled(p(x+24,y),p(x+w,y),p(x+w-24,y+h),p(x,y+h),selected?IM_COL32(190,3,25,255):IM_COL32(28,35,40,255));draw->AddQuad(p(x+24,y),p(x+w,y),p(x+w-24,y+h),p(x,y+h),selected?IM_COL32_WHITE:border,2*s);if(selected)draw->AddTriangleFilled(p(x+39,y+25),p(x+39,y+57),p(x+65,y+41),IM_COL32_WHITE);cell(card.actions[i],x+76,y+3,w-100,h-6,52,IM_COL32_WHITE,true);
  }
  rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);UiControllerButton(draw,p(91,901),22*s,s,"A",IM_COL32(76,189,60,255));cell("ADVANCE",126,878,250,48,28);
  return true;
}

bool DrawSimGame(const SimGameCard& card) {
  if(!gpu::g_ui_image)return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"sim-game-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);

  UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
  region(0,0,1672,110,10,6,1410,104);
  UiBrandLogo(draw,p(55,15),p(310,92));
  cell("SIMULATING GAME",970,5,650,85,64,IM_COL32_WHITE,true);rect(1050,91,566,5,red);
  region(586,131,497,58,110,128,500,69);
  cell(card.speed,665,134,335,50,40,IM_COL32_WHITE,true);
  rect(233,202,1165,155,IM_COL32(9,15,19,255));
  region(235,204,1161,43,78,219,1285,44,IM_COL32(220,35,50,255));
  for(int r=0;r<3;++r){float y=202+(r?47+(r-1)*54:0),h=r?54:47;
    for(int c=0;c<13;++c){float x=c?593+(c-1)*67.0833f:233,w=c?67.0833f:360;
      if(r && c==0){const auto& t=card.teams[r-1];unsigned rgb=card.colors[r-1];rect(x+1,y+1,w-2,h-2,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,95));
        const auto path=overhaul::TeamLogo(t.name,t.nickname);if(!path.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(x+16,y+4),p(x+80,y+h-4));
        cell(t.name,x+99,y,w-111,h,33);
      }else cell(card.lineScore[r][c],x+3,y,w-6,h,r?37:32,IM_COL32_WHITE,true);
      draw->AddRect(p(x,y),p(x+w,y+h),border,0,0,s);
    }
  }
  rect(140,377,1391,365,IM_COL32(9,15,19,255));
  region(142,440,491,228,80,470,390,150);
  region(1038,440,491,228,960,470,390,150);
  for(int i=0;i<2;++i){const auto& t=card.teams[i];const auto& player=card.players[i];unsigned rgb=card.colors[i];float x=i?1038:142;
    rect(x,379,491,59,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,230));
    cell(card.positions[i]+"   "+player.name,x+20,380,453,56,39,IM_COL32_WHITE,i==1);
    rect(x,440,491,228,IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,35));
    const auto path=overhaul::TeamLogo(t.name,t.nickname);if(!path.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1310:160,460),p(i?1500:350,625),{0,0},{1,1},IM_COL32(255,255,255,45));
    if(player.portrait && gpu::g_ui_guest_image)if(auto face=gpu::g_ui_guest_image(player.portrait))draw->AddImage((ImTextureID)face,p(i?1190:230,450),p(i?1440:480,668),{player.uv[0],player.uv[1]},{player.uv[2],player.uv[3]});
    float bx=i?1071:236;rect(bx,684,374,40,IM_COL32(6,10,13,255));draw->AddRect(p(bx,684),p(bx+374,724),border,0,0,s);
    for(int k=0;k<14;++k)rect(bx+8+k*25.6f,692,22,24,(k+.5f)/14<=card.stamina[i]?IM_COL32(244,115,20,255):IM_COL32(47,53,57,255));
  }
  region(633,379,405,59,552,401,356,48);cell("PITCHER MATCHUP",643,380,385,56,36,IM_COL32(10,15,19,255),true);
  for(int r=0;r<5;++r){float y=442+r*44;const auto& a=card.players[0].stats[r];const auto& z=card.players[1].stats[r];
    if(a[0]==z[0] || a[0].empty() || z[0].empty()){
      cell(a[1],642,y,98,44,36,IM_COL32_WHITE,true);cell(a[0].empty()?z[0]:a[0],744,y,181,44,29,muted,true);cell(z[1],930,y,98,44,36,IM_COL32_WHITE,true);
    }else {cell(a[0],637,y,120,44,22,muted);cell(a[1],759,y,65,44,32,IM_COL32_WHITE,true);cell(z[0],837,y,120,44,22,muted);cell(z[1],960,y,65,44,32,IM_COL32_WHITE,true);}
    if(r<4)draw->AddLine(p(633,y+44),p(1038,y+44),border,s);
  }
  cell("STAMINA",638,673,395,61,43,IM_COL32_WHITE,true);
  draw->AddRect(p(140,377),p(1531,742),border,0,0,2*s);draw->AddLine(p(140,439),p(1531,439),border,s);draw->AddLine(p(140,668),p(1531,668),border,s);
  int count=(int)card.actions.size();for(int i=0;i<count;++i){float w=count==1?470:348,x=count==1?600:295+i*377,y=759,h=77;bool selected=i==card.selected;
    draw->AddQuadFilled(p(x+18,y),p(x+w,y),p(x+w-18,y+h),p(x,y+h),selected?IM_COL32(190,3,25,255):IM_COL32(28,35,40,255));
    draw->AddQuad(p(x+18,y),p(x+w,y),p(x+w-18,y+h),p(x,y+h),selected?IM_COL32_WHITE:border,2*s);cell(card.actions[i],x+25,y+2,w-50,h-4,50,IM_COL32_WHITE,true);
  }
  rect(0,858,1672,83,IM_COL32(6,11,15,245));rect(0,858,1672,2,border);
  for(int i=0;i<2;++i){float x=i?338:91;auto color=i?IM_COL32(55,145,235,255):IM_COL32(76,189,60,255);UiControllerButton(draw,p(x,901),22*s,s,i?"X":"A",color);cell(i?"STATS/ATTRIB":"ADVANCE",x+35,878,250,48,28);}
  return true;
}

bool DrawCoach(const CoachCard& card) {
  if(!gpu::g_ui_image)return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"coach-atlas.png"));if(!atlas)return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if(s<=0)return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),{u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  const auto caps=[](std::string value){for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
  auto text=[&](UiFont role,const std::string& value,float x,float y,float size,float width,ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(role);if(!font || value.empty())return;
    const auto label=caps(value);const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(1.f,measured));
    UiText(draw,role,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,label.c_str(),width*s);
  };
  auto cell=[&](const std::string& value,float x,float y,float w,float h,float size,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(UiFont::Heading);if(!font || value.empty())return;
    const auto label=caps(value);
    const float measured=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    size*=std::min(1.f,w/std::max(1.f,measured));
    float top=font->FontSize,bottom=0;
    for(unsigned char ch:label)if(ch<128)if(const auto* glyph=font->FindGlyph(ch)){
      if(glyph->Y1>glyph->Y0){top=std::min(top,glyph->Y0);bottom=std::max(bottom,glyph->Y1);}
    }
    const float scale=size/font->FontSize;
    const float baseline=y+(h-(bottom-top)*scale)/2-top*scale;
    const float width=font->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;
    draw->PushClipRect(p(x,y),p(x+w,y+h),true);
    UiText(draw,UiFont::Heading,size*s,p(center?x+(w-width)/2:x,baseline),color,label.c_str(),w*s);
    draw->PopClipRect();
  };
  constexpr auto border=IM_COL32(128,139,148,255),muted=IM_COL32(188,197,205,255),red=IM_COL32(237,12,40,255);
  UiMenuBackdrop(draw,screen,UiBackdrop::Steel);
  region(0,0,1672,116,14,7,1397,113);
  UiBrandLogo(draw,p(57,15),p(310,98));
  cell("COACH",1240,6,375,88,68,IM_COL32_WHITE,true);rect(1110,91,530,5,red);
  rect(0,117,1672,2,border);
  const int n=int(card.commands.size());const float menuX=36,menuY=169,menuW=357,rh=std::min(88.f,540.f/std::max(1,n));
  rect(menuX,menuY,menuW,rh*n,IM_COL32(9,15,20,255));draw->AddRect(p(menuX,menuY),p(menuX+menuW,menuY+rh*n),border,0,0,2*s);
  for(int r=0;r<n;++r){const float y=menuY+r*rh;const bool selected=r==card.selected;
    region(menuX+3,y+2,menuW-6,rh-3,20,275,325,55,IM_COL32(255,255,255,selected?255:160));
    if(selected)rect(menuX+4,y+4,menuW-8,rh-8,IM_COL32(188,3,24,245));
    if(r)draw->AddLine(p(menuX,y),p(menuX+menuW,y),border,s);
    if(selected)draw->AddTriangleFilled(p(menuX+22,y+rh/2-13),p(menuX+22,y+rh/2+13),p(menuX+44,y+rh/2),IM_COL32_WHITE);
    cell(card.commands[r],menuX+62,y+2,menuW-78,rh-4,n>6?31:39,r<int(card.enabled.size()) && !card.enabled[r]?IM_COL32(108,118,127,255):IM_COL32_WHITE);
  }
  const float fx=411,fy=145,fw=1226,fh=325;
  region(fx,fy,fw,fh,378,149,1025,325);
  // Preserve the atlas detail while giving the field and each team its own color.
  draw->AddTriangleFilled(p(615,155),p(1441,155),p(1022,454),IM_COL32(49,113,34,155));
  draw->AddQuadFilled(p(1022,261),p(1182,366),p(1022,454),p(856,363),IM_COL32(165,117,68,145));
  const int batting=std::clamp(card.batting,0,1),pitching=1-batting;
  for(int i=0;i<2;++i){const int team=i?pitching:batting;const auto color=card.colors[team];
    draw->AddTriangleFilled(p(i?1630:420,220),p(i?1630:420,470),p(1020,470),IM_COL32((color>>16)&255,(color>>8)&255,color&255,155));
    cell(i?"PITCHING":"AT BAT",i?1358:500,284,232,50,37,IM_COL32_WHITE,true);
    const auto path=overhaul::TeamLogo(card.teams[team].name,card.teams[team].nickname);
    if(!path.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(i?1450:472,347),p(i?1585:607,462));
    cell(card.scores[team],i?1280:634,350,115,110,90,IM_COL32_WHITE,true);
  }
  rect(416,150,250,75,IM_COL32(9,16,20,255));
  rect(1428,150,205,75,IM_COL32(9,16,20,255));
  rect(423,155,217,59,IM_COL32(10,16,21,255));draw->AddRect(p(423,155),p(640,214),border,4*s,0,2*s);
  cell("OUT",444,157,85,55,36);
  for(int i=0;i<3;++i){draw->AddCircle(p(550+i*26,185),8*s,IM_COL32(208,216,221,255),24,2*s);if(i<card.outs)draw->AddCircleFilled(p(550+i*26,185),5*s,red,24);}
  rect(1446,155,181,59,IM_COL32(10,16,21,255));draw->AddRect(p(1446,155),p(1627,214),border,4*s,0,2*s);
  if(batting==0)draw->AddTriangleFilled(p(1466,193),p(1495,193),p(1480,172),IM_COL32_WHITE);
  else draw->AddTriangleFilled(p(1466,174),p(1495,174),p(1480,195),IM_COL32_WHITE);
  cell(card.inning,1506,157,107,55,38,IM_COL32_WHITE,true);
  const float bx[]={1182,1022,856,1022},by[]={363,261,363,386};
  for(int k=0;k<4;++k)if(!card.runners[k].empty()){
    const float w=k==3?220:185;rect(bx[k]-w/2,by[k]-18,w,36,IM_COL32(6,15,12,235));
    draw->AddRect(p(bx[k]-w/2,by[k]-18),p(bx[k]+w/2,by[k]+18),border,3*s,0,s);
    cell(card.runners[k],bx[k]-w/2+5,by[k]-17,w-10,34,27,IM_COL32_WHITE,true);
  }
  rect(413,473,1222,50,IM_COL32(19,27,32,250));
  auto result=card.result;for(auto& ch:result)if(ch=='\r'||ch=='\n'||ch=='\t')ch=' ';
  cell(result,432,474,1180,47,32,IM_COL32_WHITE,true);
  const bool textView=!card.playByPlay.empty() && card.players[0].name.empty();
  if(card.lines || textView){
    rect(413,218,1222,603,IM_COL32(10,16,20,255));
    draw->AddRect(p(413,218),p(1635,821),border,0,0,2*s);
    if(card.lines){
      cell("LINE SCORE",440,230,1150,60,43);
      for(int r=0;r<3;++r)for(int c=0;c<16;++c)cell(card.lineScore[r][c],426+c*75,320+r*68,72,66,29,IM_COL32_WHITE,true);
    }else{
      cell("PLAY BY PLAY",440,230,1150,60,43);
      UiText(draw,UiFont::Prompt,28*s,p(444,314),IM_COL32_WHITE,card.playByPlay.c_str(),1150*s);
    }
  }
  if(!card.lines && !textView)
  for(int i=0;i<2;++i){const auto& out=card.players[i?pitching:batting];const int team=i?pitching:batting;const auto color=card.colors[team];const float x=i?1018:411,w=i?619:591,y=547;
    rect(x,y,w,274,IM_COL32(10,16,20,255));draw->AddRect(p(x,y),p(x+w,y+274),border,0,0,2*s);
    rect(x+2,y+2,w-4,55,IM_COL32((color>>16)&255,(color>>8)&255,color&255,220));
    region(x+2,y+2,w-4,55,385,504,485,52,IM_COL32(255,255,255,90));
    cell(out.name,x+42,y+1,w-58,55,40);
    const auto path=overhaul::TeamLogo(card.teams[team].name,card.teams[team].nickname);
    if(!path.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))draw->AddImage((ImTextureID)logo,p(x+18,y+92),p(x+183,y+231),{0,0},{1,1},IM_COL32(255,255,255,38));
    if(out.portrait && gpu::g_ui_guest_image)if(const auto texture=gpu::g_ui_guest_image(out.portrait))draw->AddImage((ImTextureID)texture,p(x+95,y+63),p(x+324,y+272),{out.uv[0],out.uv[1]},{out.uv[2],out.uv[3]});
    for(int r=0;r<6;++r){const auto& stat=out.stats[r];if(stat[0].empty() && stat[1].empty())continue;const float sy=y+61+r*34;
      if(r)draw->AddLine(p(x+330,sy),p(x+w-16,sy),IM_COL32(85,97,105,160),s);
      cell(stat[0],x+341,sy,142,34,29);cell(stat[1],x+w-83,sy,65,34,30,IM_COL32_WHITE,true);
    }
  }
  rect(0,855,1672,86,IM_COL32(6,11,15,245));rect(0,855,1672,2,border);
  auto prompt=[&](float x,const char* letter,const char* label,ImU32 color){UiControllerButton(draw,p(x,894),20*s,s,letter,color);cell(label,x+31,870,270,48,27);};
  prompt(93,"A","ADVANCE",IM_COL32(76,189,60,255));prompt(320,"B","BACK",IM_COL32(224,44,58,255));
  prompt(523,"X","CYCLE DISPLAY",IM_COL32(63,134,227,255));prompt(846,"Y","PLAYER INFO",IM_COL32(232,190,40,255));
  return true;
}

bool DrawPlaySim(const PlaySimCard& card) {
  if (!gpu::g_ui_image) return false;
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"play-sim-atlas.png"));
  if (!atlas) return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if (s<=0) return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){draw->AddRectFilled(p(x,y),p(x+w,y+h),color);};
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),
                   {u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  auto text=[&](UiFont font,const std::string& value,float x,float y,float size,float width,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* f=Font(font);if (!f || value.empty()) return;
    const auto measured=f->CalcTextSizeA(size*s,100000,0,value.c_str()).x/s;
    const float fit=std::min(1.f,width/std::max(measured,1.f));
    UiText(draw,font,size*s*fit,p(center?x+(width-measured*fit)/2:x,y),color,value.c_str(),width*s);
  };
  constexpr auto border=IM_COL32(111,121,130,255),muted=IM_COL32(188,197,204,255),red=IM_COL32(238,9,39,255);
  const bool complete=!card.scores[0].empty() && !card.scores[1].empty();
  const auto caps=[](std::string value){
    for (auto& ch:value) ch=char(std::toupper(static_cast<unsigned char>(ch)));
    return value;
  };
  UiMenuBackdrop(draw,screen,UiBackdrop::Graphite);
  region(0,0,1672,100,14,8,653,113);
  rect(0,100,1672,2,border);
  UiBrandLogo(draw,p(58,13),p(290,86));
  text(UiFont::Prompt,"P L A Y  /  S I M",1348,28,30,260,muted,true);
  rect(1455,72,153,4,red);
  draw->AddQuadFilled(p(55,200),p(76,129),p(94,129),p(73,200),red);
  text(UiFont::TeamDisplay,"PLAY / SIM",108,119,56,640);
  text(UiFont::Prompt,"Choose how to handle today's game.",110,180,26,700,muted);
  draw->AddImageQuad((ImTextureID)atlas,p(421,239),p(1251,239),p(1231,298),p(401,298),
                    {143.f/1774,311.f/887},{815.f/1774,311.f/887},
                    {792.f/1774,376.f/887},{118.f/1774,376.f/887});
  draw->AddQuadFilled(p(400,298),p(421,239),p(441,239),p(420,298),red);
  draw->AddQuadFilled(p(1231,298),p(1251,239),p(1271,239),p(1251,298),red);
  text(UiFont::Heading,card.date,431,248,37,810,IM_COL32_WHITE,true);
  if (!card.games) {
    text(UiFont::Heading,"NO GAMES SCHEDULED",285,420,48,1102,IM_COL32_WHITE,true);
    text(UiFont::Prompt,"Choose another date on the schedule.",285,487,28,1102,muted,true);
    rect(0,852,1672,89,IM_COL32(8,12,16,235));
    rect(0,852,1672,2,border);
    UiControllerButton(draw,p(78,897),19*s,s,"B",IM_COL32(225,40,59,255));
    text(UiFont::Prompt,"BACK",112,883,26,160);
    return true;
  }

  constexpr float x=86,y=325,w=1500,h=218;
  rect(x,y,w,h,IM_COL32(8,13,18,255));
  auto teamColor=[&](int i){return IM_COL32((card.colors[i]>>16)&255,(card.colors[i]>>8)&255,card.colors[i]&255,255);};
  draw->AddQuadFilled(p(x,y),p(712,y),p(638,y+h),p(x,y+h),teamColor(0));
  draw->AddQuadFilled(p(1054,y),p(x+w,y),p(x+w,y+h),p(980,y+h),teamColor(1));
  region(x,y,w,h,15,399,1343,167,IM_COL32(255,255,255,72));
  for (int i=0;i<2;++i) {
    const auto& team=card.teams[i];
    const float logoX=i?1340:115,tx=i?1072:393,tw=i?255:278;
    const auto path=overhaul::TeamLogo(team.name,team.nickname);
    if (!path.empty()) if (const auto logo=gpu::g_ui_image(overhaul::UiAsset(path)))
      draw->AddImage((ImTextureID)logo,p(logoX,y+22),p(logoX+176,y+198));
    text(UiFont::TeamDisplay,caps(team.name),tx,y+20,49,tw);
    text(UiFont::TeamDisplay,caps(team.nickname),tx,y+77,38,tw);
    rect(tx,y+141,tw-24,1,IM_COL32(221,225,228,195));
    const auto record=card.records[i].empty()?std::string("—"):card.records[i];
    text(UiFont::Prompt,record+(team.rank.empty()?"":"    |    "+team.rank),tx,y+158,29,tw,IM_COL32_WHITE);
  }
  text(UiFont::Heading,complete?"FINAL":caps(card.time),730,y+31,39,240,IM_COL32_WHITE,true);
  rect(751,y+93,196,1,border);
  text(UiFont::TeamDisplay,complete?card.scores[0]+" - "+card.scores[1]:"AT",715,y+112,68,268,IM_COL32(214,221,226,255),true);
  draw->AddRect(p(x,y),p(x+w,y+h),IM_COL32(181,188,194,255),0,0,2*s);

  for (int i=0;!complete && i<2;++i) {
    const float by=590+i*100,bx=468,bw=736,bh=86;
    const bool selected=card.selected==i;
    const float u=selected?924.f:1347.f,sw=selected?405.f:413.f;
    // Keep the atlas end caps at their original scale; only the center stretches.
    region(bx,by,14,bh,u,310,12,68);
    region(bx+14,by,bw-28,bh,u+12,310,sw-24,68);
    region(bx+bw-14,by,14,bh,u+sw-12,310,12,68);
    const float ix=bx+51,iy=by+23;
    draw->AddTriangleFilled(p(ix,iy),p(ix,iy+40),p(ix+35,iy+20),IM_COL32_WHITE);
    if (i) draw->AddTriangleFilled(p(ix+29,iy),p(ix+29,iy+40),p(ix+64,iy+20),IM_COL32_WHITE);
    text(UiFont::TeamDisplay,caps(card.actions[i]),bx+155,by+15,47,bw-187);
  }
  if (complete) {
    text(UiFont::Heading,"GAME COMPLETE",468,620,44,736,IM_COL32_WHITE,true);
    text(UiFont::Prompt,"Press B to return to the schedule.",468,680,28,736,muted,true);
  }
  if (card.games>1)
    text(UiFont::Prompt,"LEFT / RIGHT  CHANGE GAME     "+std::to_string(card.game+1)+" / "+std::to_string(card.games),468,802,22,736,muted,true);
  rect(0,852,1672,89,IM_COL32(8,12,16,235));
  region(0,852,1672,89,14,8,653,113,IM_COL32(255,255,255,86));
  rect(0,852,1672,2,border);
  auto button=[&](float bx,const char* letter,const char* label,ImU32 color){
    UiControllerButton(draw,p(bx,897),19*s,s,letter,color);
    text(UiFont::Prompt,label,bx+34,883,26,160);
  };
  if (!complete) button(78,"A","SELECT",IM_COL32(72,182,54,255));
  button(complete?78:281,"B","BACK",IM_COL32(225,40,59,255));
  if (!complete) text(UiFont::Prompt,"UP / DOWN  CHOOSE ACTION",1234,884,22,372,muted);
  return true;
}

bool DrawDynastyCalendar(const DynastyCalendarCard& card) {
  if (!gpu::g_ui_image) return false;
  const auto root=overhaul::Root();
  const auto atlas=gpu::g_ui_image(overhaul::UiAsset(root/"dynasty-calendar-atlas.png"));
  if (!atlas) return false;
  const auto screen=ImGui::GetIO().DisplaySize;
  const float s=std::min(screen.x/1672.f,screen.y/941.f);
  if (s<=0) return false;
  auto* draw=ImGui::GetBackgroundDrawList();
  const bool shell=DrawDashboardShell(draw,"SCHEDULE",1,&card.team,card.color,card.month+" "+card.year);
  DashboardPageFrame frame(screen,shell);
  const float ox=(screen.x-1672*s)/2, oy=(screen.y-941*s)/2;
  auto p=[&](float x,float y){return frame.Point(x,y);};
  auto rect=[&](float x,float y,float w,float h,ImU32 color){
    draw->AddRectFilled(p(x,y),p(x+w,y+h),color);
  };
  auto region=[&](float x,float y,float w,float h,float u,float v,float uw,float vh,ImU32 color=IM_COL32_WHITE){
    draw->AddImage((ImTextureID)atlas,p(x,y),p(x+w,y+h),
                   {u/1774.f,v/887.f},{(u+uw)/1774.f,(v+vh)/887.f},color);
  };
  auto image=[&](const std::filesystem::path& path,float x,float y,float w,float h){
    if (!path.empty())
      if (const auto t=gpu::g_ui_image(overhaul::UiAsset(path))) {
        auto lo=p(x,y),hi=p(x+w,y+h);
        if(shell){const float fit=std::min((hi.x-lo.x)/w,(hi.y-lo.y)/h);const ImVec2 center{(lo.x+hi.x)/2,(lo.y+hi.y)/2};lo={center.x-w*fit/2,center.y-h*fit/2};hi={center.x+w*fit/2,center.y+h*fit/2};}
        draw->AddImage((ImTextureID)t,lo,hi);
      }
  };
  auto text=[&](UiFont kind,const std::string& value,float x,float y,float size,float width,
                ImU32 color=IM_COL32_WHITE,bool center=false){
    const auto* font=Font(kind);
    if (!font || value.empty()) return;
    const float measured=font->CalcTextSizeA(size*s*frame.TextScale(),100000,0,value.c_str()).x/(s*frame.sx);
    const float fit=std::min(1.f,width/std::max(measured,1.f));
    UiText(draw,kind,size*s*frame.TextScale()*fit,p(center?x+(width-measured*fit)/2:x,y),color,value.c_str(),width*s*frame.sx);
  };
  constexpr ImU32 muted=IM_COL32(190,199,208,255), border=IM_COL32(88,99,109,255);
  constexpr ImU32 red=IM_COL32(237,15,45,255), blue=IM_COL32(55,120,208,255);
  if(!shell){
  UiMenuBackdrop(draw,screen,UiBackdrop::Graphite);
  region(0,0,1672,96,13,9,664,106);
  region(0,850,1672,91,754,657,269,110,IM_COL32(255,255,255,144));
  rect(0,96,1672,2,border);
  rect(0,850,1672,2,border);
  UiBrandLogo(draw,p(66,12),p(286,85));
  text(UiFont::Prompt,"D Y N A S T Y   M O D E",1325,28,29,281,muted);
  rect(1463,69,143,4,red);
  draw->AddQuadFilled(p(61,194),p(80,127),p(98,127),p(79,194),red);
  text(UiFont::TeamDisplay,"SCHEDULE",111,117,56,580);
  text(UiFont::Prompt,"Select a date to play or simulate. Tournament rounds advance at scheduling checkpoints.",112,177,26,1490,muted);

  }
  frame.Content(67,226,1538,592);
  constexpr float x=67,y=226,w=930,h=592;
  constexpr ImU32 gold=IM_COL32(255,206,65,255);
  const auto material=gpu::g_ui_image(overhaul::UiAsset(root/"calendar-material-imagegen-v2.png"));
  auto panel=[&](float px,float py,float pw,float ph){
    draw->AddRectFilled(p(px,py),p(px+pw,py+ph),IM_COL32(9,17,23,255),10*s);
    if(material)draw->AddImageRounded((ImTextureID)material,p(px,py),p(px+pw,py+ph),{0,0},{1,1},IM_COL32(255,255,255,75),10*s);
    draw->AddRect(p(px,py),p(px+pw,py+ph),border,10*s,0,1.5f*s);
  };
  panel(x,y,w,h);
  draw->AddRectFilled(p(x+12,y+12),p(x+w-12,y+66),IM_COL32(36,46,55,230),6*s);
  UiTriggerBadge(draw,p(x+25,y+20),p(x+84,y+58),s,"LT");
  UiTriggerBadge(draw,p(x+w-84,y+20),p(x+w-25,y+58),s,"RT");
  text(UiFont::Heading,"<",x+109,y+20,28,28,muted,true);
  text(UiFont::Heading,">",x+w-137,y+20,28,28,muted,true);
  text(UiFont::Heading,card.month+" "+card.year,x+162,y+18,34,w-324,IM_COL32_WHITE,true);
  constexpr float gap=6,gx=x+14,gy=y+110,cw=(w-28-6*6)/7;
  const char* weekdays[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
  for(int i=0;i<7;++i)text(UiFont::Heading,weekdays[i],gx+i*(cw+gap),y+79,24,cw,muted,true);
  const bool sixWeeks=std::any_of(card.days.begin()+35,card.days.end(),[](const auto& d){return d.number;});
  const int rows=sixWeeks?6:5,cells=rows*7;
  const float ch=(h-124-(rows-1)*gap)/rows;
  const int selected=std::clamp(card.selected,0,41);
  unsigned games=0,homeGames=0,awayGames=0,openDays=0;
  for(int i=0;i<cells;++i){
    const auto& day=card.days[i];
    games+=day.games;if(day.home)homeGames+=day.games;else awayGames+=day.games;
    if(day.number&&!day.games)++openDays;
    const float dx=gx+(i%7)*(cw+gap),dy=gy+(i/7)*(ch+gap);
    const auto fill=day.games?(day.home?IM_COL32(16,43,71,255):IM_COL32(65,18,29,255)):IM_COL32(21,28,33,255);
    draw->AddRectFilled(p(dx,dy),p(dx+cw,dy+ch),day.number?fill:IM_COL32(11,18,23,255),4*s);
    if(material&&day.number)draw->AddImageRounded((ImTextureID)material,p(dx,dy),p(dx+cw,dy+ch),{0,0},{1,1},IM_COL32(255,255,255,38),4*s);
    draw->AddRect(p(dx,dy),p(dx+cw,dy+ch),IM_COL32(61,72,81,day.number?180:65),4*s,0,s);
    if(day.games)rect(dx+5,dy+1,cw-10,3,day.home?blue:red);
    if(i==selected){
      draw->AddRect(p(dx-2,dy-2),p(dx+cw+2,dy+ch+2),IM_COL32(255,202,55,45),6*s,0,7*s);
      draw->AddRect(p(dx,dy),p(dx+cw,dy+ch),gold,4*s,0,2*s);
      draw->AddRectFilled(p(dx+4,dy+5),p(dx+38,dy+34),gold,3*s);
    }
    if(day.number)text(UiFont::Heading,std::to_string(day.number),dx+9,dy+5,27,31,i==selected?IM_COL32(12,18,23,255):day.available?IM_COL32_WHITE:muted);
    if(day.games){
      // Reserve a separate bottom line for results, including six-week months.
      const float logo=std::min(48.f,ch-(day.result.empty()?25.f:41.f));
      image(overhaul::TeamLogo(day.opponent,day.nickname),dx+cw-logo-10,dy+15,logo,logo);
      text(UiFont::Prompt,day.home?"H":"A",dx+9,dy+37,18,24,IM_COL32_WHITE);
      if(day.games>1)text(UiFont::Prompt,"x"+std::to_string(day.games),dx+37,dy+38,18,32,IM_COL32_WHITE);
      if(!day.result.empty()){
        const auto resultColor=day.result[0]=='W'?IM_COL32(143,226,172,255):day.result[0]=='L'?IM_COL32(255,166,166,255):muted;
        text(UiFont::Prompt,day.result,dx+8,dy+ch-25,20,cw-16,resultColor);
      }
    }
  }
  constexpr float rx=1021,rw=584;
  // Use the same neutral material as other menus; school color is an accent,
  // rather than a wash over the old purple stadium photograph.
  UiBroadcastPanel(draw,p(rx,226),p(rx+rw,387),s);
  const auto schoolAccent=IM_COL32((card.color>>16)&255,(card.color>>8)&255,card.color&255,255);
  rect(rx+1,227,5,159,schoolAccent);
  rect(rx+132,241,1,84,border);
  rect(rx+2,342,rw-4,43,IM_COL32(8,13,18,220));
  image(overhaul::TeamLogo(card.team.name,card.team.nickname),rx+18,231,105,105);
  text(UiFont::TeamDisplay,card.team.name,rx+145,240,40,416);
  text(UiFont::TeamDisplay,card.team.nickname,rx+147,283,27,416);
  const std::string record=card.record.empty()?"--":card.record;
  text(UiFont::Prompt,"OVERALL  "+record+"     |     NATIONAL RANK  "+card.team.rank,rx+19,352,24,rw-38,IM_COL32_WHITE,true);
  const auto& day=card.days[selected];
  panel(rx,401,rw,207);
  const auto date=day.number?card.month+" "+std::to_string(day.number)+", "+card.year:card.month+" "+card.year;
  rect(rx+20,419,4,28,gold);
  text(UiFont::Heading,date,rx+34,416,29,rw-58);
  if(day.games){
    image(overhaul::TeamLogo(day.opponent,day.nickname),rx+22,465,94,94);
    text(UiFont::Heading,day.rank+"  "+day.opponent,rx+139,466,31,rw-159);
    text(UiFont::Prompt,day.nickname,rx+141,503,24,rw-163,muted);
    auto detail=day.home?std::string("HOME"):std::string("AWAY");
    if(!day.result.empty())detail+="   |   "+day.result;
    if(day.games>1)detail+="   |   "+std::to_string(day.games)+" GAMES";
    text(UiFont::Prompt,detail,rx+141,541,22,rw-163,muted);
    if(!day.stage.empty())text(UiFont::Prompt,day.stage,rx+24,577,18,rw-48,gold);
  }else{
    text(UiFont::Heading,!day.number?"SELECT A DATE":day.available?"OPEN DATE":"SCHEDULE PENDING",rx+26,474,31,rw-52);
    text(UiFont::Prompt,!day.number?"Use the D-pad to choose a day in this month.":day.available?"No game scheduled":"Advance the current round to generate later games.",rx+26,519,24,rw-52,muted);
  }
  panel(rx,623,rw,195);
  text(UiFont::Heading,"THIS MONTH",rx+24,642,25,rw-48,muted);
  const std::string totals[]={std::to_string(games),std::to_string(homeGames),std::to_string(awayGames),std::to_string(openDays)};
  const char* totalLabels[]={"GAMES","HOME","AWAY","OPEN DATES"};
  for(int i=0;i<4;++i){
    const float dx=rx+16+i*141.f;
    if(i)rect(dx-4,691,1,87,border);
    text(UiFont::Heading,totals[i],dx,688,42,132,i?IM_COL32_WHITE:gold,true);
    text(UiFont::Prompt,totalLabels[i],dx,748,21,132,muted,true);
  }

  frame.Footer();
  constexpr ImU32 legend[]={blue,red,border};
  const char* labels[]={"HOME","AWAY","OPEN"};
  for (int i=0;i<3;++i) {
    const float dx=66+i*140.f;
    rect(dx,872,27,27,IM_COL32(23,31,39,255));
    draw->AddRect(p(dx,872),p(dx+27,899),legend[i],0,0,2*s);
    text(UiFont::Prompt,labels[i],dx+38,873,25,85);
  }
  const auto action=[&](const char* icon,const char* label,float x0) {
    image(root/icon,x0,866,40,40);
    text(UiFont::Prompt,label,x0+50,874,25,168,shell&&std::string(label)=="SECTIONS"?IM_COL32(16,19,22,255):IM_COL32_WHITE);
  };
  UiControllerButton(draw,p(622,886),20*s,s,"A",IM_COL32(76,189,60,255));
  text(UiFont::Prompt,"GO TO DATE",653,874,25,164,day.number&&day.available?IM_COL32_WHITE:border);
  UiControllerButton(draw,p(905,886),20*s,s,"Y",IM_COL32(230,188,42,255));
  text(UiFont::Prompt,"SIM TO DATE",936,874,25,169,day.number&&day.available?IM_COL32_WHITE:border);
  action("controller-reference-x.png","VIEW GAMES",1180);
  action("controller-reference-b.png",shell?"SECTIONS":"BACK",1470);
  return true;
}
}

namespace game {
bool DrawTournamentTree(const TournamentTreeCard& card){
 if(!gpu::g_ui_image)return false;
 const auto screen=ImGui::GetIO().DisplaySize;const float s=std::min(screen.x/1672.f,screen.y/941.f);if(s<=0)return false;
 auto* d=ImGui::GetBackgroundDrawList();const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
 const bool shell=DrawDashboardShell(d,"PLAYOFF TREE",2);
 DashboardPageFrame frame(screen,shell);
 auto p=[&](float x,float y){return frame.Point(x,y);};
 auto logoImage=[&](ImTextureID texture,ImVec2 lo,ImVec2 hi,float aspect=1.f,ImVec2 uv0=ImVec2(0,0),ImVec2 uv1=ImVec2(1,1)){const float fit=std::min((hi.x-lo.x)/aspect,hi.y-lo.y);const ImVec2 center{(lo.x+hi.x)/2,(lo.y+hi.y)/2};d->AddImage(texture,{center.x-fit*aspect/2,center.y-fit/2},{center.x+fit*aspect/2,center.y+fit/2},uv0,uv1);};
 auto text=[&](const std::string& v,float x,float y,float w,float size,ImU32 c=IM_COL32_WHITE,bool center=false){
  const auto role=shell?(size>=31?UiFont::Heading:UiFont::Prompt):UiFont::Settings;const auto* f=Font(role);if(!f||v.empty())return;const float measured=f->CalcTextSizeA(size*s*frame.TextScale(),100000,0,v.c_str()).x/(s*frame.sx),fit=std::min(1.f,w/std::max(1.f,measured));
  UiText(d,role,size*s*frame.TextScale()*fit,p(x+(center?(w-measured*fit)/2:0),y),c,v.c_str(),w*s*frame.sx);
 };
 constexpr auto red=IM_COL32(237,12,40,255),pale=IM_COL32(218,225,232,255),gold=IM_COL32(255,210,57,255),muted=IM_COL32(170,183,202,255);
 if(!shell){
 UiMenuBackdrop(d,screen,UiBackdrop::Graphite);
 UiBroadcastPanel(d,p(0,0),p(1672,112),s);UiBrandLogo(d,p(56,12),p(310,97));text("PLAYOFF TREE",1120,10,510,61);d->AddRectFilled(p(1110,86),p(1632,94),IM_COL32(236,15,45,255));
 }
 frame.Content(0,130,1672,710,145,710);
 d->AddRectFilled(p(425,136),p(1247,199),IM_COL32(76,7,20,240),31*s);d->AddRect(p(425,136),p(1247,199),pale,31*s,0,2*s);
 UiControllerGlyph(d,p(445,148),p(508,186),"LB");UiControllerGlyph(d,p(1164,148),p(1227,186),"RB");
 text(card.title,525,147,625,38,IM_COL32_WHITE,true);
 if(!card.series.empty()){UiControllerGlyph(d,p(576,205),p(626,234),"LT");text(card.series,643,202,386,28,pale,true);UiControllerGlyph(d,p(1046,205),p(1096,234),"RT");}
 if(card.matches.size()==1&&!card.format.empty()) {
  const auto& m=card.matches[0];text(card.format,560,245,552,41,gold,true);
  for(int side=0;side<2;++side){const auto& t=m.teams[side];const float x=side?864.f:70.f;
   UiBroadcastPanel(d,p(x,310),p(x+738,738),s);
   d->AddRectFilled(p(x+2,312),p(x+736,365),t.color);
   text(t.seed.empty()?"":"SEED "+t.seed,x+20,320,690,28,pale);
   if(!t.logo.empty())if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"base-logos"/(t.logo+".png"))))logoImage((ImTextureID)logo,p(x+42,386),p(x+250,594));
   text(t.name.empty()?"TBD":t.name,x+280,402,420,43);text(t.record,x+280,461,420,26,muted);
   text("SERIES SCORE",x+280,540,420,23,muted);text(t.score.empty()?"0":t.score,x+280,577,420,89,gold);
  }
  frame.Footer(861);
  if(!shell)d->AddRectFilled(p(0,861),p(1672,925),IM_COL32(3,5,7,255));d->AddLine(p(0,861),p(1672,861),pale,s);
  UiControllerGlyph(d,p(120,873),p(170,915),"B");text(shell?"SECTIONS":"BACK",190,879,220,31);
  UiControllerGlyph(d,p(970,873),p(1020,915),"Y");text("SERIES SCORES",1040,879,480,31);
  return true;
 }
 float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
 std::vector<float> columns;
 for(const auto& m:card.matches){minx=std::min(minx,m.x);maxx=std::max(maxx,m.x);miny=std::min(miny,m.y);maxy=std::max(maxy,m.y);if(std::none_of(columns.begin(),columns.end(),[&](float v){return std::abs(v-m.x)<8;}))columns.push_back(m.x);}
 const bool native=maxx-minx>20&&maxy-miny>20;
 const unsigned n=unsigned(card.matches.size());const unsigned cols=n==1?1:n<=6?2:3;
 const float w=native?(columns.size()>2?365.f:438.f):(cols==1?660.f:cols==2?438.f:430.f),h=n>9?110.f:136.f;
 std::vector<ImVec2> locations;
 for(unsigned i=0;i<n;++i){const auto& m=card.matches[i];float x,y;
  if(native){x=95+(m.x-minx)/(maxx-minx)*(1482-w);y=236+(m.y-miny)/(maxy-miny)*(500-h);}
  else if(n==1){x=(1672-w)/2;y=350;}
  else {const unsigned rows=(n+cols-1)/cols,col=i/rows,row=i%rows;x=90+col*((1492-w)/(cols-1));y=238+row*(515-h)/std::max(1u,rows-1);}
  if(n==7&&card.title.find("Regional")!=std::string::npos&&!m.letter.empty()){
   switch(m.letter[0]){case 'A':x=80;y=242;break;case 'B':x=80;y=418;break;case 'D':x=80;y=594;break;
    case 'C':x=652;y=326;break;case 'E':x=652;y=594;break;case 'F':x=1220;y=326;break;case 'G':x=1220;y=594;break;}
  }
  locations.push_back({x,y});
 }
 // Only draw advancement links whose unresolved native labels identify their
 // source game; resolved matchups never invent graph edges from team names.
 for(unsigned i=0;i<n;++i)for(const auto& t:card.matches[i].teams)for(unsigned j=0;j<n;++j){
  if(i==j||card.matches[j].letter.empty())continue;
  if(t.name=="Winner "+card.matches[j].letter||t.name=="WINNER "+card.matches[j].letter){const auto a=locations[j],b=locations[i];d->AddLine(p(a.x+w,a.y+h/2),p(b.x,b.y+h/2),pale,2*s);}
 }
 if(n==7&&card.title.find("Regional")!=std::string::npos){text("ELIMINATION GAMES",80,558,930,23,muted);text("CHAMPIONSHIP",1220,286,365,23,muted,true);}
 // A completed four-team regional still needs its winner/loser paths. Native
 // letters A-G identify this exact seven-game double-elimination format.
 if(n==7&&card.title.find("Regional")!=std::string::npos){
  const char* edges[]={"AC","BC","AD","BD","CE","DE","CF","EF","FG"};
  for(auto edge:edges){int from=-1,to=-1;for(unsigned i=0;i<n;++i){if(card.matches[i].letter==std::string(1,edge[0]))from=i;if(card.matches[i].letter==std::string(1,edge[1]))to=i;}
   if(from<0||to<0)continue;auto a=locations[from],b=locations[to];float mid=(a.x+w+b.x)/2;
   d->AddLine(p(a.x+w,a.y+h/2),p(mid,a.y+h/2),IM_COL32(148,151,153,110),s);d->AddLine(p(mid,a.y+h/2),p(mid,b.y+h/2),IM_COL32(148,151,153,110),s);d->AddLine(p(mid,b.y+h/2),p(b.x,b.y+h/2),IM_COL32(148,151,153,110),s);
  }
 }
 for(unsigned i=0;i<n;++i){const auto& m=card.matches[i];const float x=locations[i].x,y=locations[i].y;
  UiBroadcastPanel(d,p(x,y),p(x+w,y+h),s,m.selected);
  d->AddLine(p(x+17,y+h/2),p(x+w-17,y+h/2),IM_COL32(108,100,132,170),s);
  text(m.letter.empty()?std::to_string(i+1):m.letter,x-21,y+h/2-18,44,31,gold,true);
  const float badge=62;
  for(int side=0;side<2;++side){const auto& t=m.teams[side];const float yy=y+9+side*h/2;
   if(!t.logo.empty())if(const auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"base-logos"/(t.logo+".png"))))logoImage((ImTextureID)logo,p(x+36,yy),p(x+85,yy+49));
   text(t.seed,x+13,yy+2,20,19,muted,true);text(t.name.empty()?"TBD":t.name,x+94,yy,w-190,29);
   text(t.record,x+95,yy+30,w-195,18,muted);text(t.score,x+w-89,yy+6,32,30,gold,true);
  }
  d->AddRectFilled(p(x+w-badge+7,y+14),p(x+w-10,y+h-14),IM_COL32(4,8,14,225),12*s);text("GAME",x+w-badge+10,y+h/2-28,badge-25,16,muted,true);
  text(m.number.empty()?m.letter:m.number,x+w-badge+10,y+h/2-5,badge-25,34,gold,true);
 }
 if(native&&columns.size()==2){
  bool crest=false;if(card.logo&&gpu::g_ui_guest_image)if(auto logo=gpu::g_ui_guest_image(card.logo)){
   const float w=std::min(280.f,220.f*card.logoAspect),hh=w/std::max(.1f,card.logoAspect);
   logoImage((ImTextureID)logo,p(836-w/2,365-hh/2),p(836+w/2,365+hh/2),card.logoAspect,{card.logoUv[0],card.logoUv[1]},{card.logoUv[2],card.logoUv[3]});crest=true;
  }
  if(!crest)text("POSTSEASON",677,366,318,34,pale,true);text(card.title,677,498,318,34,IM_COL32_WHITE,true);text(card.format,677,540,318,25,gold,true);
 }
 d->AddRectFilled(p(200,780),p(1472,831),IM_COL32(42,8,15,236),22*s);d->AddRect(p(200,780),p(1472,831),red,22*s,0,2*s);
 text(card.format.empty()?"LIVE BRACKET  /  RESULTS & ADVANCEMENT":card.format,240,790,1192,29,pale,true);
 frame.Footer(861);
  if(!shell)d->AddRectFilled(p(0,861),p(1672,925),IM_COL32(2,10,16,250));d->AddLine(p(0,861),p(1672,861),pale,2*s);d->AddLine(p(0,925),p(1672,925),pale,2*s);
 UiControllerGlyph(d,p(140,872),p(190,914),"B");text(shell?"SECTIONS":"BACK",209,876,220,33);text("D-PAD  SELECT GAME",600,876,475,25,muted,true);
 if(std::any_of(card.matches.begin(),card.matches.end(),[](const auto& m){return m.selected&&!m.teams[0].score.empty()&&!m.teams[1].score.empty();})){UiControllerGlyph(d,p(1215,872),p(1265,914),"Y");text("LINESCORE",1285,876,300,33);}
 return true;
}
}


