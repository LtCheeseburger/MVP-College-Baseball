#include "roster_browser_ui.h"
#include "team_select_ui.h"
#include "ui_style.h"
#include "overhaul.h"
#include "gpu/backend.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace game {
namespace {
constexpr ImU32 white=IM_COL32(245,247,249,255),muted=IM_COL32(168,180,190,255),red=IM_COL32(237,12,40,255);
struct Canvas {
 ImDrawList* d;float s,ox,oy;
 Canvas(bool foreground=false){auto z=ImGui::GetIO().DisplaySize;s=std::min(z.x/1920.f,z.y/1080.f);ox=(z.x-1920*s)/2;oy=(z.y-1080*s)/2;d=foreground?ImGui::GetForegroundDrawList():ImGui::GetBackgroundDrawList();}
 ImVec2 p(float x,float y)const{return {ox+x*s,oy+y*s};}
 void text(std::string v,float x,float y,float w,float size=30,ImU32 c=white,UiFont font=UiFont::Heading,bool caps=true){
  if(v.empty())return;if(caps)for(auto& ch:v)ch=char(std::toupper(static_cast<unsigned char>(ch)));auto f=Font(font);if(!f)return;
  auto width=f->CalcTextSizeA(size*s,1e6f,0,v.c_str()).x;size*=std::min(1.f,w*s/std::max(1.f,width));
  d->PushClipRect(p(x,y-4),p(x+w,y+size+15),true);UiText(d,font,size*s,p(x,y),c,v.c_str(),w*s);d->PopClipRect();
 }
 void line(float x,float y,float w){d->AddLine(p(x,y),p(x+w,y),IM_COL32(119,133,145,90),s);}
 void panel(float x,float y,float w,float h,bool selected=false){UiBroadcastPanel(d,p(x,y),p(x+w,y+h),s,selected);}
 void face(uint32_t portrait,const std::array<float,4>& uv,float x,float y,float w,float h){if(portrait&&gpu::g_ui_guest_image)if(auto image=gpu::g_ui_guest_image(portrait))d->AddImage((ImTextureID)image,p(x,y),p(x+w,y+h),{uv[0],uv[1]},{uv[2],uv[3]});}
 void hint(const char* key,const char* label,float x){UiControllerButton(d,p(x,1020),23*s,s,key,white);text(label,x+39,1000,325,28,white,UiFont::Prompt);}
 void shell(const char* title){UiMenuBackdrop(d,ImGui::GetIO().DisplaySize,UiBackdrop::Graphite);UiBrandLogo(d,p(55,22),p(320,105));text(title,770,30,1090,66);d->AddRectFilled(p(1190,115),p(1860,121),red);line(45,148,1830);d->AddRectFilled(p(0,975),p(1920,1080),IM_COL32(4,11,16,255));line(0,975,1920);}
};
}
bool DrawRosterBrowser(const RosterBrowserCard& card){
 if(!gpu::g_ui_image)return false;Canvas c;c.shell(card.trade?"TRADE PLAYERS":"EDIT PLAYER");
 const int lists=card.trade?2:1;const float left=card.trade?55:530,width=card.trade?890:1335;
 if(!card.trade){c.panel(55,185,440,745);c.face(card.portrait,card.uv,99,210,350,350);c.line(85,590,380);c.text(card.player,85,610,380,43);c.text("PLAYER SUMMARY",85,680,380,25,muted);
  int row=0;for(auto& [label,value]:card.attributes){if(row>=6)break;int col=row/3;float y=728+(row%3)*58,x=85+col*200;++row;c.text(label,x,y,125,21,muted);c.text(value,x+130,y,65,28);}
 }
 for(int side=0;side<lists;++side){float x=left+side*925;c.panel(x,185,width,745);c.text(card.trade?card.listNames[side]:card.team,x+28,207,width-56,40);c.text(card.trade?(side==card.active?"ACTIVE ROSTER":"OTHER ROSTER"):card.conference,x+28,261,width-56,25,muted);c.line(x+25,307,width-50);
  const char* editLabels[]={"POS","PLAYER","BATS","OVR","TEAM"};const char* tradeLabels[]={"POS","PLAYER","OVR"};
  const float e[]={.025f,.13f,.55f,.66f,.77f},t[]={.03f,.17f,.83f};int count=card.trade?3:5;
  for(int col=0;col<count;++col)c.text(card.trade?tradeLabels[col]:editLabels[col],x+width*(card.trade?t[col]:e[col]),328,width*(col==1?.4f:.18f),25,muted);
  auto& rows=card.rows[side];int visible=card.trade?10:7;float rh=card.trade?49:70;
  for(int row=0;row<std::min(visible,int(rows.size()));++row){float y=377+row*rh;bool selected=row==card.selected[side];if(selected)c.panel(x+13,y-4,width-26,rh,side==card.active);else if(row%2)c.d->AddRectFilled(c.p(x+14,y-4),c.p(x+width-14,y+rh-4),IM_COL32(255,255,255,5));
   for(int col=0;col<count;++col){float rel=card.trade?t[col]:e[col];float cw=col==1?width*.4f:width*.17f;c.text(rows[row][col],x+width*rel,y+7,cw-12,card.trade?30:32);}
   c.line(x+23,y+rh-5,width-46);
  }
  const bool populated=std::any_of(rows.begin(),rows.end(),[](const auto& row){return !row[1].empty();});
  if(populated)c.text("ROW "+std::to_string(card.first[side]+std::max(0,card.selected[side])+1),x+28,891,width-56,22,muted,UiFont::Prompt);
  else c.text("NO AVAILABLE PLAYERS",x+28,891,width-56,22,muted,UiFont::Prompt);
 }
 c.hint("B","BACK",85);c.hint("Y","PLAYER INFO",590);c.hint("A",card.trade?"SELECT PLAYER":"EDIT PLAYER",1125);
 if(!card.trade)c.text("X  SORT  /  LT RT  CHANGE TEAM",590,936,1150,23,muted,UiFont::Prompt);
 else c.text("LEFT / RIGHT  ACTIVE ROSTER   /   LT RT  CHANGE TEAM",70,936,1780,23,muted,UiFont::Prompt);
 return true;
}
bool DrawPlayerInformation(const PlayerInformationCard& card,bool backdrop){
 if(backdrop){Canvas b;b.shell("PLAYER INFORMATION");}Canvas c(true);auto z=ImGui::GetIO().DisplaySize;c.d->AddRectFilled({0,0},z,IM_COL32(0,0,0,190));c.panel(230,130,1460,820);c.text("PLAYER INFORMATION",275,160,1350,49);c.d->AddRectFilled(c.p(275,229),c.p(1645,235),red);
 c.panel(275,270,410,560);c.face(card.portrait,card.uv,310,290,340,340);c.text(card.name,300,658,365,40);c.text(card.position+"  /  #"+card.number,300,722,365,28,muted);
 c.text(card.page.empty()?"PROFILE":card.page,735,267,870,39);c.line(735,324,870);
 int row=0;for(auto& [label,value]:card.identity){if(row>=8)break;int col=row/4;float y=351+(row%4)*46,x=735+col*450;++row;c.text(label,x,y,230,24,muted);c.text(value,x+240,y,180,28);}
 int n=0;for(auto& [label,value]:card.stats){if(n>=10)break;int column=n%2,rr=n/2;float x=735+column*450,y=568+rr*51;c.text(label,x,y,270,25,muted);c.text(value,x+278,y,145,31);c.line(x,y+43,420);++n;}
 c.text("LT / RT  CHANGE INFO PAGE",735,854,870,27,muted,UiFont::Prompt);c.text("Y  CLOSE PLAYER INFO",735,902,870,27,white,UiFont::Prompt);return true;
}
bool DrawCreateEdit(const CreateEditCard& card){
 if(!gpu::g_ui_image)return false;Canvas c;c.shell("CREATE / EDIT PLAYER");
 bool school=!card.school.team.name.empty();c.text(school?card.school.team.name+"  /  "+card.school.team.nickname:"ROSTER WORKSHOP",70,178,1780,37);
 c.text("BUILD YOUR ROSTER",70,244,1780,25,muted,UiFont::Prompt);
 for(int i=0;i<2;++i){float x=70+i*920;bool selected=card.selected==i;c.panel(x,325,860,495,selected);
  c.text(i?"02":"01",x+38,357,740,29,selected?white:muted);c.text(i?"EDIT PLAYER":"CREATE PLAYER",x+38,530,740,66);
  c.line(x+38,627,784);c.text(i?"SELECT AN EXISTING PLAYER":"START WITH A NEW PLAYER",x+38,671,740,30,muted,UiFont::Prompt);
  c.text(i?"APPEARANCE / RATINGS / EQUIPMENT":"IDENTITY / APPEARANCE / ATTRIBUTES",x+38,724,740,25,muted,UiFont::Prompt);
 }
 c.text(card.loading?"PREPARING PLAYER MODELS AND APPEARANCE...":"LEFT / RIGHT  CHOOSE AN ACTION",70,870,1780,29,muted,UiFont::Prompt);
 c.hint("B","BACK",85);c.hint("A",card.loading?"LOADING PLAYER EDITOR":"SELECT",1125);return true;
}
bool DrawTextKeyboard(const TextKeyboardCard& card){
 if(!gpu::g_ui_image)return false;Canvas c;c.shell("TEXT ENTRY");c.text(card.title.empty()?"ENTER NAME":card.title,70,180,1780,40);
 c.panel(70,255,1780,105);c.text(card.entered.empty()?"_":card.entered,100,279,1720,51,white,UiFont::Heading,false);
 for(int row=0;row<5;++row)for(int col=0;col<13;++col){
  const auto& key=card.keys[row][col];if(key.empty())continue;
  int first=col,last=col;for(int x=0;x<13;++x)if(card.keyOwner[row][x]==col){first=std::min(first,x);last=std::max(last,x);}
  float x=70+first*138,y=407+row*94,w=(last-first+1)*138-15;
  bool active=card.y==row&&card.keyOwner[row][card.x]==col;
  c.panel(x,y,w,77,active);c.text(key,x+15,y+20,w-30,key.size()>3?29:36,active?white:muted,UiFont::Heading,false);
 }
 c.text("D-PAD  MOVE   /   SHIFT  CHANGE CASE   /   BACK  DELETE CHARACTER",70,905,1780,27,muted,UiFont::Prompt);
 c.hint("B","CANCEL",85);c.hint("A","SELECT KEY",1125);return true;
}
bool DrawUtilityMenu(const UtilityMenuCard& card){
 if(!gpu::g_ui_image)return false;Canvas c;
 if(card.loading){
  c.shell("BALLPARK STUDIO");c.panel(230,340,1460,330);
  c.text("LOADING BALLPARK",285,390,1350,65);
  c.text("Preparing stadium geometry and field settings",285,491,1350,34,muted,UiFont::Body);
  c.d->AddRectFilled(c.p(285,590),c.p(1635,596),IM_COL32(62,67,73,255));
  const float travel=float(std::fmod(ImGui::GetTime()*.4,1.0));
  c.d->AddRectFilled(c.p(285+travel*1060,590),c.p(575+travel*1060,596),red);
  return true;
 }
 if(card.replay){
  if(card.hidden){c.panel(1340,953,520,70);UiControllerButton(c.d,c.p(1372,988),19*c.s,c.s,"BLACK",white);c.text("SHOW REPLAY CONTROLS",1405,974,430,24);return true;}
  c.d->AddRectFilled(c.p(0,730),c.p(1920,1080),IM_COL32(4,11,16,250));c.line(0,730,1920);c.text("INSTANT REPLAY",70,758,850,40);c.text(card.subtitle,1100,765,750,28,muted);
  const char* keys[]={"X","LT","A","RT","B"};const char* labels[]={"REWIND","PREVIOUS FRAME","PLAY / PAUSE","NEXT FRAME","FAST FORWARD"};
  for(int i=0;i<5;++i){float x=82+i*365;UiControllerButton(c.d,c.p(x,860),22*c.s,c.s,keys[i],white);c.text(labels[i],x+37,841,310,26);}
  c.text("STICKS  MOVE / ROTATE CAMERA   ·   D-PAD  ZOOM",70,919,1780,26,muted,UiFont::Prompt);
  c.hint("Y","CAMERA MODE",85);c.hint("BLACK","HIDE CONTROLS",650);c.hint("START","EXIT REPLAY",1240);return true;
 }
 c.shell(card.title.c_str());c.text(card.subtitle,70,178,1780,30,muted,UiFont::Prompt);
 const int n=int(card.labels.size());const float top=270,rh=std::min(116.f,650.f/std::max(1,n));
 for(int i=0;i<n && i<10;++i){float y=top+i*rh;bool selected=i==card.selected;bool enabled=i>=int(card.enabled.size())||card.enabled[i];c.panel(70,y,1780,rh-11,selected);
  float x=108;if(card.slots){c.text("SLOT "+std::to_string(i+1),x,y+16,160,25,muted);x+=180;
   if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(card.labels[i],""))))c.d->AddImage((ImTextureID)logo,c.p(x,y+7),c.p(x+rh-25,y+rh-18));x+=rh;
  }
  c.text(card.labels[i].empty()?"UNAVAILABLE":card.labels[i],x,y+15,card.slots?850:1420,std::min(44.f,rh*.45f),enabled?white:muted);
  if(i<int(card.values.size()))c.text(card.values[i],card.slots?1270:1410,y+20,card.slots?530:390,23,muted,UiFont::Prompt);
 }
 c.hint("B","BACK",85);c.hint("A",card.slots?"ADVANCE":"SELECT",1125);
 if(card.slots)c.hint("X","SWITCH SIDES",650);
 c.text(card.slots?"UP / DOWN  SLOT   ·   LEFT / RIGHT  CHANGE TEAM":card.teamSwitch?"UP / DOWN  ACTION   ·   LEFT / RIGHT  CHANGE PROGRAM":"UP / DOWN  CHOOSE AN ACTION",70,936,1780,24,muted,UiFont::Prompt);return true;
}
bool DrawStadiumSelection(const StadiumSelectionCard& card){
 Canvas c(true);
 // Keep the native stadium image and all selection callbacks. Cover only
 // the old chrome outside its original image viewport (y=109..365/480).
 c.d->PushClipRect(c.p(0,0),c.p(1920,245),true);
 UiMenuBackdrop(c.d,ImGui::GetIO().DisplaySize,UiBackdrop::Graphite);
 c.d->PopClipRect();
 UiBrandLogo(c.d,c.p(55,22),c.p(320,105));
 c.text("SELECT BALLPARK",770,30,1090,66);
 c.d->AddRectFilled(c.p(1190,115),c.p(1860,121),red);
 c.line(45,148,1830);c.text(card.venue,70,172,1780,49);
 c.d->AddRectFilled(c.p(0,821),c.p(1920,1080),IM_COL32(4,11,16,255));
 c.line(0,821,1920);c.panel(55,841,880,111,card.timeSelected);c.panel(965,841,900,111);
 c.text("GAME TIME",80,852,350,24,muted);c.text(card.time,80,888,820,37);
 c.text("WIND",990,852,350,24,muted);c.text(card.wind,990,888,820,37);
 c.text("LT / RT  WIND DIRECTION    WHITE / BLACK  WIND SPEED",80,952,1780,22,muted,UiFont::Prompt);
 c.line(0,975,1920);c.hint("B","BACK",85);c.hint("X","RANDOM BALLPARK",650);c.hint("A","ADVANCE",1240);
 return true;
}
bool DrawBallparkEditor(const BallparkEditorCard& card){
 Canvas c;
 // Keep the game's live stadium and wall handles visible beside the controls.
 // The opaque sidebar covers the complete legacy HUD footprint.
 if(!card.hidden){
  c.d->AddRectFilled(c.p(0,0),c.p(740,975),IM_COL32(10,14,18,255));
  UiBrandLogo(c.d,c.p(45,25),c.p(295,105));
  c.text(card.wall?"CUSTOM WALL":"BALLPARK STUDIO",45,145,650,52);
  c.d->AddRectFilled(c.p(45,215),c.p(695,221),red);
  if(card.wall){
   c.text(card.mode.empty()?"WALL ADJUSTMENT":card.mode,55,255,630,40);
   c.panel(45,330,650,210);c.text(card.points,75,360,590,29,muted);
   // Global mode reports the change applied across the selected range, and
   // the native formatter drops the leading zero (".5"). Show a signed offset.
   std::string upperPoints=card.points;for(auto& ch:upperPoints)ch=char(std::toupper(static_cast<unsigned char>(ch)));
   const bool offset=upperPoints=="GLOBAL" && !card.measurement.empty();
   std::string measurement=card.measurement;
   if(offset){
    const bool negative=measurement[0]=='-';if(negative)measurement.erase(0,1);
    if(!measurement.empty() && measurement[0]=='.')measurement.insert(0,"0");
    if(measurement.find_first_not_of("0.")!=std::string::npos)measurement.insert(0,negative?"-":"+");
   }
   c.text(measurement,75,416,590,76);c.text(offset?"FEET CHANGE":"FEET",offset?455:520,470,offset?210:145,26,muted);
   c.panel(45,570,650,125);c.text(card.divider,75,604,590,39);
   c.text("UP / DOWN  ADJUST WALL",55,725,630,25,muted,UiFont::Prompt);
   c.text("LEFT / RIGHT  SELECT DIVIDER / EDGE",55,766,630,25,muted,UiFont::Prompt);
   c.text("BLACK / WHITE  DISTANCE / HEIGHT",55,807,630,25,muted,UiFont::Prompt);
   c.text("HOLD LT  SELECTED ONLY   /   HOLD RT  MULTI-SELECT",55,848,630,23,muted,UiFont::Prompt);
  }else{
   for(int i=0;i<5;++i){float y=255+i*123;c.panel(45,y,650,104,i==card.selected);
    c.text(card.labels[i],70,y+13,600,28,i==card.selected?white:muted);
    c.text(card.values[i],70,y+52,600,37);
   }
   c.text("UP / DOWN  CATEGORY   /   LEFT / RIGHT  OPTION",55,900,630,24,muted,UiFont::Prompt);
  }
 }
 // Cover the original footer independently of the hide-controls state.
 c.d->AddRectFilled(c.p(0,900),c.p(1920,1080),IM_COL32(4,11,16,255));c.line(0,900,1920);
 if(card.hidden){c.d->AddRectFilled(c.p(0,0),c.p(500,180),IM_COL32(10,14,18,255));c.text("LIVE BALLPARK PREVIEW",40,48,420,34);}
 c.text("LS CLICK  SHOW / HIDE CONTROLS",45,929,1780,25,muted,UiFont::Prompt);
 c.hint("B","BACK",75);c.hint("X","RANDOMIZE",515);
 if(card.wall)c.hint("Y","RESET WALL",955);
 else if(card.selected==0 && (card.values[0].find("Custom")!=std::string::npos || card.values[0].find("CUSTOM")!=std::string::npos))c.hint("A","CUSTOM WALL",955);
 else c.text("D-PAD  CHANGE OPTION",955,1000,355,26,white,UiFont::Prompt);
 c.hint("START","CREATE BALLPARK",1390);
 return true;
}
}
