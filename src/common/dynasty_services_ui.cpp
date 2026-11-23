#include "team_select_ui.h"
#include "ui_style.h"
#include "overhaul.h"
#include "gpu/backend.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace game {
namespace {
constexpr ImU32 white=IM_COL32_WHITE,red=IM_COL32(237,12,40,255),pale=IM_COL32(217,225,232,255),gold=IM_COL32(255,207,35,255),muted=IM_COL32(177,189,205,255);
struct ServiceCanvas {
 ImDrawList* d;float s,ox,oy;bool ready,upright;
 ServiceCanvas(const char* title,bool campus=false,bool overlay=false,bool uprightText=false):upright(uprightText){
  const auto screen=ImGui::GetIO().DisplaySize;s=std::min(screen.x/1672.f,screen.y/941.f);ox=(screen.x-1672*s)/2;oy=(screen.y-941*s)/2;d=ImGui::GetBackgroundDrawList();ready=s>0&&bool(gpu::g_ui_image);if(!ready)return;
  if(overlay){
   d->AddRectFilled({0,0},screen,IM_COL32(0,0,0,185));UiBroadcastPanel(d,P(35,145),P(1637,800),s);
   Text(title,70,160,1500,48);d->AddRectFilled(P(70,215),P(1600,220),red);Button("B","DISMISS",690,738);return;
  }
  UiMenuBackdrop(d,screen,UiBackdrop::Graphite);
  UiBroadcastPanel(d,P(0,0),P(1672,112),s);UiBrandLogo(d,P(55,13),P(310,97));Text(title,790,11,835,60,white,true);d->AddRectFilled(P(980,85),P(1635,94),red);
  d->AddRectFilled(P(0,848),P(1672,923),IM_COL32(2,9,16,250));Line(0,848,1672,848,pale,2);Line(0,923,1672,923,pale,2);
  Button("B","BACK",145,870);
 }
 ImVec2 P(float x,float y)const{return {ox+x*s,oy+y*s};}
 void Text(const std::string& t,float x,float y,float w,float size=32,ImU32 color=white,bool center=false,UiFont role=UiFont::Settings){
  auto label=t;
  if(upright){for(auto& ch:label)ch=char(std::toupper(static_cast<unsigned char>(ch)));if(role==UiFont::Settings)role=size>=30?UiFont::Heading:UiFont::Prompt;}
  auto* f=Font(role);if(label.empty()||!f)return;const auto width=f->CalcTextSizeA(size*s,100000,0,label.c_str()).x/s;const auto fit=std::min(1.f,w/std::max(1.f,width));UiText(d,role,size*s*fit,P(x+(center?(w-width*fit)/2:0),y),color,label.c_str(),w*s);
 }
 void Line(float x,float y,float xx,float yy,ImU32 color=muted,float width=1){d->AddLine(P(x,y),P(xx,yy),color,width*s);}
 void Panel(float x,float y,float w,float h,bool selected=false){
  UiBroadcastPanel(d,P(x,y),P(x+w,y+h),s,selected);
 }
 void Highlight(float x,float y,float w,float h,bool selected,ImU32 color=red){if(selected)UiMenuHighlight(d,P(x,y),P(x+w,y+h),s);}
 void Button(const char* key,const std::string& label,float x,float y){UiControllerGlyph(d,P(x,y),P(x+49,y+43),key);Text(label,x+66,y+3,330,33);}
 void Icon(int i,float x,float y,float size=50){
  if(auto t=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"recruit-actions-icons.png")))d->AddImage((ImTextureID)t,P(x,y),P(x+size,y+size),{(i%4)/4.f,(i/4)/2.f},{(i%4+1)/4.f,(i/4+1)/2.f});
 }
 void Player(const RecruitProfileCard& c,float x,float y,float w,float h,bool showInterest=true){
  if(upright){
   Panel(x,y,w,h);
   Text(c.name,x+26,y+22,w-52,40);Text(c.position,x+26,y+75,w-52,30,muted);
   Line(x+22,y+116,x+w-22,y+116,IM_COL32(95,106,118,170));
   // Keep the native square portrait proportional, with no crop or red wash.
   const float size=std::min(240.f,w-52),left=x+(w-size)/2,top=y+127;
   if(c.portrait&&gpu::g_ui_guest_image)if(auto t=gpu::g_ui_guest_image(c.portrait))d->AddImage((ImTextureID)t,P(left,top),P(left+size,top+size),{c.uv[0],c.uv[1]},{c.uv[2],c.uv[3]});
   Text("RANK",x+26,y+390,w*.42f,24,muted);Text("OVR",x+w*.54f,y+390,w*.4f,24,muted);
   Text(c.rank,x+26,y+422,w*.42f,42,gold);Text(c.overall,x+w*.54f,y+422,w*.4f,42,gold);
   Line(x+22,y+h-111,x+w-22,y+h-111,IM_COL32(95,106,118,170));
   Text("STATUS",x+26,y+h-90,w*.3f,25,muted);Text(c.status,x+w*.37f,y+h-90,w*.57f,28,white,true);
   if(showInterest){
    Text("INTEREST",x+26,y+h-42,w*.3f,25,muted);const float bx=x+w*.37f,by=y+h-37,bw=w*.56f;
    d->AddRectFilled(P(bx,by),P(bx+bw,by+17),IM_COL32(19,27,39,255),3*s);
    const float amount=std::clamp(c.interest,0.f,1.f);if(amount>0)d->AddRectFilled(P(bx,by),P(bx+bw*amount,by+17),gold,3*s);
    d->AddRect(P(bx,by),P(bx+bw,by+17),pale,3*s);
   }
   return;
  }
  Panel(x,y,w,h);d->AddRectFilled(P(x+3,y+3),P(x+w-3,y+h-3),IM_COL32(112,8,24,160),20*s);
  d->PushClipRect(P(x+3,y+3),P(x+w-3,y+h-3),true);
  if(c.portrait&&gpu::g_ui_guest_image)if(auto t=gpu::g_ui_guest_image(c.portrait)){const float fw=std::min(w*.50f,(h*.72f-12)*.82f),fh=fw/.82f,fx=x+(w*.53f-fw)/2;d->AddImage((ImTextureID)t,P(fx,y+h*.72f-fh),P(fx+fw,y+h*.72f),{c.uv[0],c.uv[1]},{c.uv[2],c.uv[3]});}
  const float tx=x+w*.54f;Text(c.name,tx,y+45,w*.42f,40);Text(c.position,tx,y+106,w*.42f,30,muted);
  Text("RANK",tx,y+188,w*.18f,22,muted);Text(c.rank,tx,y+217,w*.18f,42,gold);if(!c.overall.empty()){Text("OVR",tx+w*.23f,y+188,w*.17f,22,muted);Text(c.overall,tx+w*.23f,y+217,w*.17f,42,gold);}
  d->PopClipRect();Line(x+20,y+h*.73f,x+w-20,y+h*.73f,muted);
  Text("STATUS",x+26,y+h*.78f,w*.3f,23,muted);Text(c.status,x+w*.37f,y+h*.78f,w*.57f,26,white,true);
  if(!showInterest)return;
  Text("INTEREST",x+26,y+h*.89f,w*.3f,23,muted);const float bx=x+w*.37f,by=y+h*.90f,bw=w*.56f;
  d->AddRectFilled(P(bx,by),P(bx+bw,by+17),IM_COL32(19,27,39,255),8*s);d->AddRectFilled(P(bx,by),P(bx+bw*std::clamp(c.interest,0.f,1.f),by+17),gold,8*s);d->AddRect(P(bx,by),P(bx+bw,by+17),pale,8*s);
 }
 void School(const DynastySchoolCard& school,float x,float y,float w,float h){
  Panel(x,y,w,h);if(auto t=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(school.team.name,school.team.nickname))))d->AddImage((ImTextureID)t,P(x+20,y+15),P(x+20+h-30,y+h-15));Text(school.team.name,x+h,y+(h<120?15:27),w-h-20,h<120?28:35);Text(school.team.nickname,x+h,y+(h<120?50:75),w-h-20,h<120?20:26,muted);
 }
};
}
bool DrawRecruitActions(const RecruitActionsCard& c){
 ServiceCanvas v("RECRUITING ACTIONS",false,false,true);if(!v.ready)return false;
 v.Player(c.player,26,145,550,574);v.School(c.school,26,738,550,93);
 v.Panel(605,145,1038,70);v.Text("RECRUIT POINTS REMAINING",635,163,790,34);v.Text(c.points,1480,153,130,48,gold,true);
 v.Panel(605,232,1038,477);
 for(int i=0;i<7;++i){float y=240+i*65;bool focus=c.selected==i;v.Highlight(615,y,1018,61,focus,gold);
  v.d->AddRect(v.P(633,y+16),v.P(662,y+46),c.checked[i]?IM_COL32(58,217,92,255):muted,3*v.s,0,2*v.s);
  if(c.checked[i]){v.Line(639,y+31,647,y+39,IM_COL32(63,233,97,255),4);v.Line(647,y+39,659,y+22,IM_COL32(63,233,97,255),4);}
  v.Icon(i,686,y+6,49);v.Text(c.labels[i],753,y+10,700,33,c.enabled[i]?white:muted);v.Text(c.costs[i],1500,y+6,105,39,c.enabled[i]?gold:muted,true);
  if(i<6)v.Line(628,y+64,1620,y+64,IM_COL32(111,115,135,170));
 }
 v.Panel(861,747,570,70,c.selected==7);v.Text("DONE",880,759,532,40,white,true);v.Button("A",c.selected==7?"DONE":"TOGGLE",1100,870);return true;
}
bool DrawRecruitOffer(const RecruitOfferCard& c){
 ServiceCanvas v("RECRUITING OFFER");if(!v.ready)return false;
 v.Player(c.player,25,145,600,392,false);v.Panel(25,562,600,246);v.Text("PLAYER REQUESTS",58,577,520,35);v.Line(40,628,610,628);v.Icon(7,55,651,52);v.Text("SCHOLARSHIP",127,657,320,29);v.Text(c.requestScholarship,465,650,125,36,gold,true);v.Text("PLAYING TIME",55,735,380,29);v.Text(c.requestTime,432,730,160,33,gold,true);
 v.Panel(653,145,990,435);v.Text("YOUR OFFER",690,168,860,42);v.Line(675,226,1620,226);
 const std::string labels[]={"SCHOLARSHIP","PLAYING TIME"},values[]={c.scholarship,c.time};
 for(int i=0;i<2;++i){float y=251+i*107;v.Highlight(679,y,934,87,c.selected==i,gold);v.Icon(i?2:7,703,y+17,53);v.Text(labels[i],782,y+20,395,32);v.Text("<",1180,y+15,55,45,white,true);v.Text(values[i],1245,y+18,285,36,gold,true);v.Text(">",1542,y+15,55,45,white,true);}
 v.Panel(837,482,635,69,c.selected==2);v.Text(c.button.empty()?"MAKE OFFER":c.button,852,494,605,39,white,true);
 v.Panel(653,605,990,202);v.School(c.school,653,605,380,202);v.Line(1050,624,1050,786);v.Text("CURRENT SCHOLARSHIPS",1080,636,385,28);v.Text(c.current,1490,627,120,43,gold,true);v.Text("NEXT YEAR SCHOLARSHIPS",1080,730,385,27);v.Text(c.next,1490,716,120,43,gold,true);v.Button("A","SELECT",1210,870);return true;
}
bool DrawInbox(const InboxCard& c){
 ServiceCanvas v("INBOX",true);if(!v.ready)return false;
 v.Panel(40,145,1592,665);v.Text("MY INBOX",70,163,390,45);v.Text(c.date,950,174,640,30,muted,true);v.Line(56,222,1615,222);
 int count=0,unread=0;for(const auto& m:c.messages)if(!m.subject.empty()){++count;unread+=m.unread;}
 v.Highlight(63,261,251,67,true);v.Icon(3,79,269,48);v.Text("ALL MAIL",145,275,153,31);v.Text("Y  FILTERS",85,367,215,27,muted);
 if(unread){v.Text(std::to_string(unread)+(c.first==0&&count<9?" NEW":" UNREAD ON PAGE"),67,449,245,27,pale,true);}
 v.Line(335,239,335,790);v.Text("FROM",363,240,195,25,muted);v.Text("SUBJECT",570,240,420,25,muted);v.Text("DATE",1012,240,130,25,muted);
 for(int i=0;i<9;++i){const auto& m=c.messages[i];float y=282+i*54;v.Highlight(354,y-2,795,50,i==c.selected&&!m.subject.empty());if(m.unread)v.d->AddCircleFilled(v.P(370,y+20),6*v.s,red);v.Text(m.sender,389,y+5,168,29);v.Text(m.subject,570,y+5,430,29);v.Text(m.date,1012,y+8,128,25,muted,true);v.Line(355,y+51,1148,y+51,IM_COL32(88,99,117,190));}
 v.Line(1174,241,1174,790);v.Icon(3,1330,384,130);v.Text(count?"SELECT A MESSAGE":"YOUR INBOX IS EMPTY",1204,559,392,34,muted,true);if(count)v.Text("TO READ",1204,611,392,34,muted,true);
 if(count)v.Button("X","DELETE",430,870);v.Button("Y","FILTERS",805,870);if(count)v.Button("A","READ",1210,870);return true;
}
bool DrawMailReader(const MailReaderCard& c){
 ServiceCanvas v("INBOX",true);if(!v.ready)return false;
 v.Panel(48,145,435,665);v.Text("MESSAGE",78,167,370,44);v.Icon(3,191,285,130);v.Text(c.subject,78,485,370,40,pale,true);v.Text(c.sender,78,561,370,28,muted,true);
 v.Panel(509,145,1123,665);v.Text(c.subject,540,169,750,43);v.Text(c.date+" "+c.age,1275,180,325,24,muted,true);v.Line(524,231,1618,231);v.Text("FROM:  "+c.sender,548,249,1035,29,muted);v.Line(524,303,1618,303);
 const auto* font=Font(UiFont::Body);v.d->PushClipRect(v.P(539,320),v.P(1605,700),true);for(int i=0;i<8;++i){if(font)v.d->AddText(font,28*v.s,v.P(548,326+i*44),white,c.lines[i].c_str(),nullptr,1038*v.s);}v.d->PopClipRect();v.Line(527,714,1613,714);
 if(!c.hotlink.empty()){v.Highlight(550,735,750,53,true);v.Text(c.hotlink,570,744,710,29,pale,true);}v.Button("A","HOTLINK",1210,870);
 UiControllerGlyph(v.d,v.P(1350,747),v.P(1400,780),"LT");v.Text(c.index,1415,743,100,28,white,true);UiControllerGlyph(v.d,v.P(1531,747),v.P(1581,780),"RT");v.Text("D-PAD  SCROLL MESSAGE",510,876,560,27,muted,true);return true;
}
}


namespace game {
bool DrawMailFilters(const MailFiltersCard& c){
 ServiceCanvas v("EMAIL FILTERS",true);if(!v.ready)return false;
 v.Panel(28,145,470,660);v.Icon(3,190,191,116);v.Text("EMAIL TYPES",68,348,389,42,white,true);v.Line(59,420,468,420);v.Text("MESSAGE VISIBILITY",67,453,390,29,muted,true);v.Text("LEFT / RIGHT  CHANGE FILTER",65,510,393,26,white,true);v.Text("EMAIL PREFERENCES",65,567,393,25,muted,true);
 v.Panel(526,145,1116,660);v.Text("EMAIL TYPE",565,170,700,37);v.Text("SHOW / HIDE",1344,176,250,30,muted,true);v.Line(547,227,1623,227);
 int first=std::clamp(c.first,0,18);if(c.selected<first)first=c.selected;if(c.selected>=first+11)first=c.selected-10;
 const auto populated=std::count_if(c.labels.begin(),c.labels.end(),[](const auto& label){return !label.empty();});const bool spacious=first==0&&populated<=5;const float step=spacious?93.f:45.f,size=spacious?39.f:29.f;
 for(int i=0;i<11&&first+i<19;++i){int index=first+i;float y=247+i*step;const auto& label=c.labels[index];if(label.empty())continue;v.Highlight(547,y-1,1076,step-4,index==c.selected,gold);v.Text(label,573,y+(spacious?20:5),710,size);std::string value=c.values[index];if(value=="No"||value=="NO")value="SHOW";else if(value=="Yes"||value=="YES")value="HIDE";v.Text("<",1325,y+(spacious?20:4),40,size,muted,true);v.Text(value,1377,y+(spacious?20:4),155,size,value=="HIDE"?muted:gold,true);v.Text(">",1552,y+(spacious?20:4),40,size,muted,true);v.Line(565,y+step-1,1600,y+step-1,IM_COL32(86,99,111,150));}
 v.Text("HIDDEN TYPES WILL NOT APPEAR IN YOUR INBOX",565,767,1028,23,muted,true);v.Button("A","ACCEPT",1210,870);return true;
}
bool DrawOffseason(const OffseasonCard& c){
 static constexpr const char* titles[]={"OUTGOING PLAYERS","PLAYER PROGRESSION","INCOMING PLAYERS","SIGN / RELEASE PLAYERS"};
 ServiceCanvas v(titles[c.kind]);if(!v.ready)return false;
 v.d->AddRectFilled(v.P(1,851),v.P(1671,920),IM_COL32(2,9,16,255));
 auto player=[&](int k,float x,float y,float w,float h){
  const auto& p=c.players[k];v.Panel(x,y,w,h);unsigned color=c.school.color;const auto tint=IM_COL32((color>>16)&255,(color>>8)&255,color&255,115);v.d->AddRectFilled(v.P(x+3,y+3),v.P(x+w-3,y+h-3),tint,20*v.s);
  v.d->PushClipRect(v.P(x+3,y+3),v.P(x+w-3,y+h-3),true);
  const float logoSize=std::min(w*.46f,h-30);if(auto logo=gpu::g_ui_image(overhaul::UiAsset(overhaul::TeamLogo(c.school.team.name,c.school.team.nickname))))v.d->AddImage((ImTextureID)logo,v.P(x+15,y+(h-logoSize)/2),v.P(x+15+logoSize,y+(h+logoSize)/2),{0,0},{1,1},IM_COL32(255,255,255,85));
  if(p.portrait&&gpu::g_ui_guest_image)if(auto tex=gpu::g_ui_guest_image(p.portrait)){const float fw=std::min(w*.49f,(h-19)*.85f),fx=x+(w*.5f-fw)/2;v.d->AddImage((ImTextureID)tex,v.P(fx,y+15),v.P(fx+fw,y+h-4),{p.uv[0],p.uv[1]},{p.uv[2],p.uv[3]});}
  v.Text(p.name,x+w*.53f,y+35,w*.43f,37);v.Text(p.position,x+w*.53f,y+98,w*.42f,30,muted);v.Text(p.year,x+w*.53f,y+146,w*.42f,25,muted);if(!p.overall.empty())v.Text("OVR  "+p.overall,x+w*.53f,y+190,w*.42f,31,gold);
  v.d->PopClipRect();
 };
 if(c.kind==3){
  for(int k=0;k<2;++k){float x=28+k*630;v.Panel(x,145,610,664);v.Text(k?"CURRENT ROSTER":"INCOMING PLAYERS",x+28,161,554,38);player(k,x+5,225,600,223);v.Text("POS",x+25,459,85,26,muted);v.Text("PLAYER",x+112,459,320,26,muted);v.Text("SCH %",x+465,459,120,26,muted,true);
   for(int i=0;i<6;++i){auto& r=c.rows[k][i];float yy=505+i*47;v.Highlight(x+14,yy-3,582,44,c.active==k&&c.selected[k]==i,gold);v.Text(r[0],x+26,yy,75,28);v.Text(r[1],x+112,yy,340,29);v.Text(r[2],x+465,yy,120,29,white,true);v.Line(x+20,yy+43,x+590,yy+43,IM_COL32(95,106,118,170));}
  }
  v.Panel(1300,145,342,664);v.Text("ROSTER",1321,174,300,34,white,true);v.Text(c.count,1321,229,300,47,gold,true);v.Highlight(1320,333,302,77,true,gold);v.Text(c.active?"RELEASE":"SIGN",1340,353,262,39,white,true);v.Text("A  CONFIRM ACTION",1323,450,296,26,muted,true);v.Text("LEFT / RIGHT",1323,530,296,27,white,true);v.Text("SWITCH ROSTER",1323,574,296,27,muted,true);
  v.Button("A",c.active?"RELEASE":"SIGN",80,870);v.Button("X","DEPTH CHART",470,870);v.Button("Y","PLAYER INFO",880,870);v.Button("START","ADVANCE",1300,870);return true;
 }
 player(0,28,145,550,330);v.Panel(28,491,550,317);v.Text(c.kind==0?"SEASON STATS":c.pitcher?"PITCHING RATINGS":"RATINGS & PROGRESSION",57,510,490,34);v.Line(48,560,556,560);
 if(c.kind==0){for(int i=0;i<8;++i){float x=60+(i%2)*254,y=588+(i/2)*48;v.Text(c.statLabels[i],x,y,123,27,muted);v.Text(c.stats[i],x+130,y,90,32,gold,true);}}
 else if(c.pitcher&&c.kind==2){for(int i=0;i<6;++i){float y=579+i*35;v.Text(c.ratingLabels[0][i],58,y,358,25,muted);v.Text(c.ratings[0][i],433,y,105,29,gold,true);}}
 else{if(c.pitcher)v.Text("RATINGS  /  CHANGE",58,576,485,27,muted,true);else{v.Text("VS LHP",58,576,228,27,muted,true);v.Text("VS RHP",315,576,228,27,muted,true);}v.Line(302,615,302,783);
  for(int k=0;k<2;++k)for(int i=0;i<3;++i){float x=58+k*257,y=628+i*51;v.Text(c.ratingLabels[k][i],x,y,93,26,muted);v.Text(c.ratings[k][i],x+96,y,62,31,gold,true);const int delta=std::atoi(c.deltas[k][i].c_str());v.Text(c.deltas[k][i],x+163,y,68,28,delta<0?IM_COL32(255,82,98,255):delta>0?IM_COL32(108,224,84,255):muted,true);}
 }
 v.Panel(605,145,1037,664);const char* h0[]={"POS","PLAYER","YEAR","DEPARTURE"};const char* h1[]={"POS","PLAYER","CHANGE","YEAR"};const char* h2[]={"POS","PLAYER","OVR","YEAR","STATUS","TEAM","SCH %"};
 const float widths0[]={100,425,210,230},widths2[]={75,275,85,90,205,105,115};
 const int cols=c.kind==2?7:4;float xx=626;for(int j=0;j<cols;++j){float w=c.kind==2?widths2[j]:widths0[j];v.Text(c.kind==2?h2[j]:c.kind==1?h1[j]:h0[j],xx,174,w-10,29,muted);xx+=w;}
 v.Line(624,229,1623,229);
 int count=0;for(int i=0;i<8;++i){auto& r=c.rows[0][i];if(!r[1].empty())++count;float yy=250+i*63;v.Highlight(618,yy-1,1012,58,i==c.selected[0]&&!r[1].empty(),gold);float x=626;
  for(int j=0;j<cols;++j){const float w=c.kind==2?widths2[j]:widths0[j];const int source=c.kind==0&&j>=2?j+1:j;std::string value=r[source];ImU32 color=white;if(c.kind==1&&j==2&&!r[1].empty()){value=c.changes[i]>0?"UP":c.changes[i]<0?"DOWN":"UNCHANGED";color=c.changes[i]>0?IM_COL32(108,224,84,255):c.changes[i]<0?gold:muted;}if(c.kind==2&&j==2)color=gold;v.Text(value,x,yy+10,w-12,c.kind==2?27:32,color);x+=w;}
  v.Line(624,yy+61,1623,yy+61,IM_COL32(95,106,118,170));
 }
 if(!count)v.Text("NO PLAYERS TO DISPLAY",665,423,900,38,muted,true);
 v.Button("Y","PLAYER INFO",c.kind==2?850:650,870);if(c.kind==1)v.Button("A","TRAINER",90,870);if(c.kind==2){v.Button("A","RECRUIT",55,870);v.Button("X","DEPTH CHART",420,870);}v.Button("START","ADVANCE",1300,870);return true;
}
}


namespace game {
bool DrawHireStaff(const HireStaffCard& c){
 ServiceCanvas v("HIRE STAFF");if(!v.ready)return false;
 v.Panel(45,145,350,655);v.Text("STAFF ROLE",72,174,285,27,muted);v.Line(70,220,370,220);
 UiControllerGlyph(v.d,v.P(79,257),v.P(137,297),"LT");UiControllerGlyph(v.d,v.P(305,257),v.P(363,297),"RT");
 v.Text(c.role,65,330,310,44,white,true);v.Text("COMPARE CANDIDATES",72,465,285,25,muted,true);
 v.Text("USE THE TRIGGERS",72,550,285,23,white,true);v.Text("CHANGE STAFF ROLE",72,584,285,23,muted,true);
 v.d->AddRectFilled(v.P(40,851),v.P(580,920),IM_COL32(2,9,16,255));v.Button("A","HIRE STAFF",145,870);v.Button("START","ADVANCE",1210,870);
 v.Panel(425,145,1202,357);v.Text(c.role+" COMPARISON",455,162,1140,36);v.Line(450,216,1600,216);
 for(int i=0;i<2;++i){float x=i?1280:450;v.Text(i?"SELECTED COACH":"CURRENT COACH",x,239,315,23,muted,true);v.Text(c.names[i],x,279,315,37,white,true);v.Text("LEVEL",x+17,348,145,23,muted);v.Text(c.levels[i],x+180,338,100,40,gold,true);v.Text("OVERALL",x+17,407,145,23,muted);v.Text(c.overall[i],x+180,395,100,45,white,true);}
 for(int i=0;i<5&&!c.labels[i].empty();++i){float y=238+i*45;v.Text(c.values[0][i],795,y,75,31,white,true);v.Text(c.labels[i],885,y,270,28,muted,true);v.Text(c.values[1][i],1170,y,75,31,gold,true);}
 v.Panel(425,526,1202,274);v.Text("COACH POOL",455,546,650,32);v.Text("LEVEL",1260,550,130,23,muted,true);v.Text("OVERALL",1435,550,150,23,muted,true);v.Line(450,591,1600,591);
 bool any=false;for(int i=0;i<3;++i){if(c.pool[i][0].empty())continue;any=true;float y=603+i*58;v.Highlight(447,y,1158,54,i==c.selected);v.Text(c.pool[i][0],468,y+9,750,30);v.Text(c.pool[i][1],1260,y+9,130,30,white,true);v.Text(c.pool[i][2],1435,y+9,150,30,gold,true);}
 if(!any)v.Text("NO AVAILABLE CANDIDATES",470,649,1100,30,muted,true);
 return true;
}
bool DrawPostseasonScores(const PostseasonScoresCard& c){
 std::string title=c.popup?"LINESCORE":c.title.empty()?"SERIES SCORES":c.title;for(auto& ch:title)ch=char(std::toupper(static_cast<unsigned char>(ch)));
 ServiceCanvas v(title.c_str(),false,c.popup);if(!v.ready)return false;
 // A popup uses the same neutral material but leaves its native dismiss action
 // and the selected bracket/game intact. No invented result or navigation.
 if(!c.emptyMessage.empty()){v.Panel(50,200,1572,555);v.Text(c.emptyMessage,120,420,1432,40,muted,true);return true;}
 const int count=c.popup?1:3;
 for(int row=0;row<count;++row){const auto& g=c.games[row];if(g.names[0].empty()&&g.names[1].empty())continue;
  float y=c.popup?240.f:150.f+row*217.f,h=c.popup?470.f:199.f;v.Panel(50,y,1572,h);
  if(!c.popup)v.Highlight(55,y+4,1562,48,row==c.selected);
  v.Text(g.label.empty()?(c.popup?"GAME SUMMARY":"GAME "+std::to_string(row+1)):g.label,75,y+15,470,28,muted);
  v.Text(g.date.empty()?c.date:g.date,970,y+15,610,25,muted,true);
  if(g.headings[0].empty()){
   for(int side=0;side<2;++side){float x=side?965.f:190.f;if(!g.logos[side].empty())if(auto t=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"base-logos"/(g.logos[side]+".png"))))v.d->AddImage((ImTextureID)t,v.P(x,y+72),v.P(x+94,y+166));v.Text(g.names[side],x+120,y+85,340,45);v.Text(g.details[side],x+120,y+143,340,23,muted);}
   v.Text("AT",735,y+99,150,35,muted,true);continue;
  }
  float tableY=y+(c.popup?167:67),nameW=385,cellW=94;
  for(int i=0;i<12;++i){v.Text(g.headings[i],445+i*cellW,tableY-3,cellW,24,muted,true);}
  for(int side=0;side<2;++side){float yy=tableY+37+side*(c.popup?90:43);
   if(!g.logos[side].empty())if(auto t=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"base-logos"/(g.logos[side]+".png"))))v.d->AddImage((ImTextureID)t,v.P(78,yy-3),v.P(118,yy+37));
   v.Text(g.names[side],138,yy,nameW-85,c.popup?39:29);
   for(int i=0;i<12;++i)v.Text(g.values[side][i],445+i*cellW,yy,cellW,c.popup?39:29,i>=9?gold:white,true);
   v.Line(75,yy+(c.popup?65:39),1590,yy+(c.popup?65:39),IM_COL32(130,132,137,75));
  }
 }
 if(!c.popup){v.Text(c.round,590,788,950,28,muted,true);v.Text("UP / DOWN  SELECT GAME",470,875,700,26,muted,true);}
 return true;
}
}

namespace game {
bool DrawPlayerEditor(const PlayerEditorCard& c){
 auto* d=ImGui::GetBackgroundDrawList();const auto screen=ImGui::GetIO().DisplaySize;
 const float s=std::min(screen.x/1672.f,screen.y/941.f);if(s<=0)return false;
 const float ox=(screen.x-1672*s)/2,oy=(screen.y-941*s)/2;
 auto p=[&](float x,float y){return ImVec2(ox+x*s,oy+y*s);};
 const bool sculpt=c.paired,zones=c.grid.starts_with("HOTCOLD");
 const bool refined=c.grid=="GENERAL"||c.grid=="APPEARANCE"||sculpt||zones;
 auto text=[&](const std::string& v,float x,float y,float w,float fs=30,ImU32 color=white){
  auto label=v;if(refined)for(auto& ch:label)ch=char(std::toupper(static_cast<unsigned char>(ch)));
  UiText(d,refined?(fs>=30?UiFont::Heading:UiFont::Prompt):UiFont::Settings,fs*s,p(x,y),color,label.c_str(),w*s);
 };
 auto panel=[&](float x,float y,float w,float h){UiBroadcastPanel(d,p(x,y),p(x+w,y+h),s);};
 auto button=[&](const char* key,const char* label,float x){UiControllerGlyph(d,p(x,866),p(x+48,910),key);text(label,x+65,872,390,28);};
 // Four clipped background passes leave the native player viewport untouched.
 // Its camera, head zoom, equipment and animations remain the game's own.
 const auto a=p(1000,113),b=p(1640,779);
 const ImVec4 clips[]={{0,0,screen.x,a.y},{0,b.y,screen.x,screen.y},{0,a.y,a.x,b.y},{b.x,a.y,screen.x,b.y}};
 for(auto r:clips){if(r.z<=r.x||r.w<=r.y)continue;d->PushClipRect({r.x,r.y},{r.z,r.w},true);UiMenuBackdrop(d,screen,UiBackdrop::Graphite);d->PopClipRect();}
 panel(0,0,1672,112);UiBrandLogo(d,p(55,13),p(310,97));text(c.edit?"EDIT PLAYER":"CREATE PLAYER",1010,13,610,61);d->AddRectFilled(p(980,85),p(1630,94),red);
 panel(35,145,285,680);text("PLAYER EDITOR",55,170,245,32);d->AddLine(p(55,220),p(300,220),muted,s);
 const char* sections[]={"GENERAL INFO","APPEARANCE","EQUIPMENT","RATINGS","TENDENCIES"};
 const char* fullSections[]={"GENERAL INFO","APPEARANCE","FACE SHAPE","BODY BUILD","EQUIPMENT","RATINGS","TENDENCIES","HOT / COLD ZONES"};
 const bool expanded=sculpt||zones;
 const int active=expanded?(c.grid=="FACESHAPE"?2:c.grid=="BODYBUILD"?3:7):(c.grid=="GENERAL"?0:c.grid=="APPEARANCE"?1:c.grid=="EQUIPMENT"?2:c.grid=="HITTENDANCY"?4:3);
 for(int i=0;i<(expanded?8:5);++i){float y=237+i*(expanded?51:73);if(i==active)UiMenuHighlight(d,p(46,y),p(310,y+(expanded?45:61)),s);text(expanded?fullSections[i]:sections[i],62,y+12,235,expanded?25:27,i==active?white:muted);}
 UiControllerGlyph(d,p(65,670),p(120,710),"LT");UiControllerGlyph(d,p(235,670),p(290,710),"RT");text("CHANGE PAGE",105,730,190,22,muted);
 panel(345,145,640,680);text(c.page,370,164,590,36);d->AddLine(p(365,220),p(965,220),muted,s);
 if(sculpt){
  const int picked=std::clamp(c.selected,0,5);
  for(int side=0;side<2;++side){float x=365+side*300;panel(x,239,285,177);
   text(side?"RIGHT STICK":"LEFT STICK",x+18,255,250,23,muted);
   text(side?(c.values[picked].empty()?"NOT USED":c.values[picked]):c.labels[picked],x+18,296,250,33);
   const auto center=p(x+142,378);d->AddCircle(center,22*s,muted,32,2*s);
   d->AddLine(p(x+98,378),p(x+186,378),muted,s);d->AddLine(p(x+142,344),p(x+142,407),muted,s);
   d->AddCircleFilled(center,6*s,red);
  }
  text("UP / DOWN  SELECT FEATURE PAIR",375,432,580,23,muted);
  for(int row=0;row<6;++row){if(c.labels[row].empty()&&c.values[row].empty())continue;float y=472+row*49;
   if(row==picked)UiMenuHighlight(d,p(358,y),p(970,y+44),s);
   else d->AddLine(p(370,y+44),p(958,y+44),IM_COL32(90,97,105,80),s);
   text(c.labels[row],377,y+10,276,28);text(c.values[row],682,y+10,272,28);
  }
  text("ADJUST WITH LEFT / RIGHT STICK",375,790,510,22,muted);
 }else if(zones){
  auto zoneColor=[](std::string value){for(auto& ch:value)ch=char(std::toupper(static_cast<unsigned char>(ch)));return value=="HOT"?IM_COL32(184,15,43,255):value=="COLD"?IM_COL32(23,85,157,255):IM_COL32(72,79,87,255);};
  text(c.grid=="HOTCOLDLEFTP"?"VS LEFT-HANDED PITCHING":"VS RIGHT-HANDED PITCHING",390,238,560,26,muted);
  for(int i=0;i<9;++i){float x=393+(i%3)*67,y=282+(i/3)*54;d->AddRectFilled(p(x,y),p(x+63,y+50),zoneColor(c.zones[i]));d->AddRect(p(x,y),p(x+63,y+50),muted,0,0,s);if(i==c.first+c.selected)d->AddRect(p(x-3,y-3),p(x+66,y+53),white,0,0,2*s);}
  const char* legend[]={"HOT","NEUTRAL","COLD"};for(int i=0;i<3;++i){d->AddRectFilled(p(655,291+i*50),p(673,309+i*50),zoneColor(legend[i]));text(legend[i],690,287+i*50,242,25,muted);}
  for(int row=0;row<6;++row){if(c.labels[row].empty())continue;float y=464+row*53;if(row==c.selected)UiMenuHighlight(d,p(358,y),p(970,y+47),s);text(c.labels[row],377,y+12,290,29);text(c.values[row],710,y+12,240,29);}
 }else{
 for(int row=0;row<6;++row){if(c.labels[row].empty())continue;const float y=242+row*89;const bool selected=row==c.selected;
  if(selected)UiMenuHighlight(d,p(358,y),p(970,y+79),s);else d->AddRectFilled(p(358,y),p(970,y+79),IM_COL32(255,255,255,3));
  text(c.labels[row],375,y+(refined?23:12),refined?285:345,refined?31:27,c.enabled[row]?white:muted);
  const auto& value=c.values[row];bool numeric=!value.empty()&&std::all_of(value.begin(),value.end(),[](char ch){return ch>='0'&&ch<='9';});
  if(c.ratings&&numeric){float fraction=std::clamp(std::atoi(value.c_str())/100.f,0.f,1.f);d->AddRectFilled(p(375,y+54),p(858,y+62),IM_COL32(56,58,61,255),3*s);d->AddRectFilled(p(375,y+54),p(375+483*fraction,y+62),red,3*s);text(value,888,y+27,65,35,gold);}
  else{text(value,refined?694:704,y+(refined?23:28),refined?255:245,refined?33:30,selected?white:muted);}
 }
 }
 if(c.total>6){const float rail=500,thumb=std::max(24.f,rail*6/c.total),offset=std::min(1.f,c.first/float(std::max(1,c.total-6)));d->AddRectFilled(p(976,245),p(980,245+rail),IM_COL32(55,58,62,255),2*s);d->AddRectFilled(p(976,245+(rail-thumb)*offset),p(980,245+(rail-thumb)*offset+thumb),red,2*s);}
 if(c.grid=="HITTENDANCY"){const char* families[]={"FASTBALL","CURVEBALL","SLIDER"};text(families[std::clamp((c.first+c.selected)/7,0,2)],375,790,400,22,muted);}
 if(c.total>6)text(std::to_string(c.first+c.selected+1)+" / "+std::to_string(c.total),870,790,100,22,muted);
 // Frame only: never paint over the actual player model inside this area.
 d->AddRect(a,b,IM_COL32(139,143,148,210),3*s,0,s);text("LIVE PLAYER PREVIEW",1025,800,545,25,muted);
 if(!c.overall.empty())text(c.overall,1530,800,85,25,gold);
 d->AddRectFilled(p(0,848),p(1672,923),IM_COL32(2,9,16,255));d->AddLine(p(0,848),p(1672,848),muted,s);d->AddLine(p(0,923),p(1672,923),muted,s);
 button("B","BACK",115);if(c.nameField)button("A","SELECT NAME",520);else{text(sculpt?"STICKS  ADJUST FEATURE":"D-PAD  CHANGE OPTION",sculpt?430:555,875,430,26,muted);}
 if(sculpt){UiControllerGlyph(d,p(880,866),p(928,910),"Y");text("RESET",942,875,180,28);}
 button("START",c.edit?"SAVE PLAYER":"CREATE PLAYER",1180);
 return true;
}
}

namespace game {
bool DrawAwardWatch(const AwardWatchCard& c){
 ServiceCanvas v("AWARD WATCH",false,false,true);if(!v.ready)return false;
 v.Panel(35,145,1602,103);v.Text("<",58,173,45,42);v.Text(c.award.empty()?"SEASON AWARDS":c.award,120,170,1445,42);v.Text(">",1570,173,45,42);
 v.Panel(35,267,1083,563);v.Text("CANDIDATES",60,287,480,35);v.Text(c.group+((c.group.empty()||c.group2.empty())?"":"  /  ")+c.group2,580,291,510,28,muted,true);
 const int columns=std::clamp(c.columns,1,5);const float widths4[]={105,440,150,315},widths5[]={80,105,420,125,280};
 const char* fallback4[]={"POS","PLAYER","CLASS","SCHOOL"};const char* fallback5[]={"RANK","POS","PLAYER","CLASS","SCHOOL"};
 float x=65;for(int col=0;col<columns;++col){float w=columns==5?widths5[col]:widths4[std::min(col,3)];v.Text(c.headings[col].empty()?(columns==5?fallback5[col]:fallback4[std::min(col,3)]):c.headings[col],x,345,w-14,25,muted);x+=w;}
 v.Line(58,383,1096,383);
 bool any=false;for(int row=0;row<10;++row){bool filled=false;for(auto& cell:c.rows[row])filled|=!cell.empty();if(!filled)continue;any=true;float y=391+row*40;v.Highlight(47,y,1059,38,row==c.selected);x=65;for(int col=0;col<columns;++col){float w=columns==5?widths5[col]:widths4[std::min(col,3)];v.Text(c.rows[row][col],x,y+5,w-14,29);x+=w;}if(row!=c.selected)v.Line(61,y+39,1091,y+39,IM_COL32(100,106,112,75));}
 if(!any){v.Text(c.champion.empty()?"NO CANDIDATES ANNOUNCED":c.champion,90,470,970,40,muted,true);}
 v.Panel(1138,267,499,563);v.Text("AWARD SPOTLIGHT",1165,290,440,34);v.Line(1163,341,1612,341);
 // A restrained medal replaces the old floating, pixelated trophy presentation.
 v.d->AddCircle(v.P(1387,446),63*v.s,IM_COL32(168,177,187,200),64,3*v.s);v.d->AddCircle(v.P(1387,446),54*v.s,IM_COL32(90,97,105,200),64,v.s);
 ImVec2 star[10];for(int i=0;i<10;++i){float a=-1.5707963f+i*.6283185f,r=i%2?17.f:36.f;star[i]=v.P(1387+std::cos(a)*r,446+std::sin(a)*r);}v.d->AddPolyline(star,10,white,ImDrawFlags_Closed,2*v.s);v.Line(1349,505,1328,539,red,8);v.Line(1425,505,1446,539,red,8);
 auto& selected=c.rows[std::clamp(c.selected,0,9)];const auto& name=selected[columns==5?2:1];const bool announced=!name.empty()&&name!="None Chosen"&&name!="--";
 v.Text(announced?name:"AWAITING ANNOUNCEMENT",1166,578,441,announced?44:35,white,true);
 const auto detail=announced?(selected[columns==5?1:0]+"  /  "+selected[columns==5?3:2]+"  /  "+selected[columns==5?4:3]):"SEASON AWARDS";
 v.Text(detail,1165,641,443,29,muted,true);v.Text(c.group,1165,731,443,26,muted,true);
 v.Text("UP / DOWN  SELECT PLAYER",70,796,760,23,muted);v.Text("LEFT / RIGHT  CHANGE AWARD",475,861,530,23,muted);v.Text(c.group2.empty()?"LT / RT  CHANGE GROUP":"LT / RT TIER   /   LB / RB CONFERENCE",475,891,530,23,muted);UiControllerGlyph(v.d,v.P(1240,870),v.P(1289,913),"Y");v.Text("PLAYER INFO",1306,873,320,33,announced?white:muted);
 return true;
}
}
