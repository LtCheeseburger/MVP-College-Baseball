#include "core/dev_tools.h"
// Startup routing hooks; the host renderer animates artwork over native FEL
// input.
#include "../common/startup_ui.h"
#include "../common/submenu_ui.h"
#include "../common/game_hooks.h"
#include "../common/team_select_ui.h"
#include "../common/matchup_loading_ui.h"
#include "../common/stadium_intro_ui.h"
#include "../common/mvp_transition_ui.h"
#include "../common/pitcher_info_ui.h"
#include "../common/batter_info_ui.h"
#include "../common/pause_menu_ui.h"
#include "../common/overhaul.h"
#include "gpu/texture.h"
#include "app/app.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>

namespace {
thread_local uint32_t g_legal_movie_manager = 0;
thread_local uint32_t g_legal_callback = 0;
std::atomic<uint32_t> g_submenu_root{0};
std::atomic<uint32_t> g_main_menu_root{0};
std::atomic<uint32_t> g_team_select_root{0}, g_team_controller_root{0};
std::atomic<uint32_t> g_matchup_root{0};
std::atomic<uint32_t> g_stadium_intro_root{0};
std::atomic<uint32_t> g_mvp_transition_root{0};
std::atomic<uint32_t> g_pitcher_root{0};
std::atomic<uint32_t> g_batter_root{0};
thread_local uint32_t g_submenu_grid = 0;
thread_local uint32_t g_submenu_popup = 0;
std::atomic<bool> g_popup_state_known{false}, g_popup_open{false};
std::atomic<uint32_t> g_owned_popup{0};
std::atomic<int> g_submenu_category{-1};
thread_local std::chrono::steady_clock::time_point g_legal_until;
bool TraceStartup() {
  static bool trace = dev::Env("MVP06_TRACE_FE") != nullptr;
  return trace;
}
} // namespace
namespace game {
void WorkshopMovie(X86Context& ctx);
void EndStartupLoading();
void PopupOpened(uint32_t scene) {
  BindPauseMenuTheme("POPUP",scene);
  g_owned_popup=scene;g_submenu_popup=scene;g_popup_state_known=true;g_popup_open=true;
  SetSubmenuPopupVisible(true);
  if(TraceStartup())LOGR("modal ownership: opened %08X",scene);
}
void PopupClosed(uint32_t scene) {
  if(scene==g_owned_popup.load()) {g_popup_open=false;SetSubmenuPopupVisible(false);
    if(TraceStartup())LOGR("modal ownership: closed %08X",scene);}
}
std::string NativeUiText(uint32_t node) {
  auto pointer = kernel::R32(node + 0x44);
  auto id=kernel::R32(node+0x48);
  if(id>=0xFE00 && id<0x10000)
    if(auto custom=PcOptionsString(id))pointer=custom;
  std::string result;
  if (pointer >= 0x10000 && mem::IsCommitted(pointer)) {
    for (unsigned i = 0; i < 4096 && mem::IsCommitted(pointer + i * 2 + 1);
         i++) {
      uint32_t c = kernel::R16(pointer + i * 2);
      if (!c)
        break;
      if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
        result.clear();
        break;
      }
      if (c < 128)
        result += char(c);
      else if (c < 2048) {
        result += char(0xC0 | (c >> 6));
        result += char(0x80 | (c & 63));
      } else {
        result += char(0xE0 | (c >> 12));
        result += char(0x80 | ((c >> 6) & 63));
        result += char(0x80 | (c & 63));
      }
    }
  }
  if (result.empty())
    result = UiString(kernel::R32(node + 0x48));
  for (size_t pos = 0; (pos = result.find("\\n", pos)) != std::string::npos;)
    result.replace(pos, 2, "\n");
  for (const auto &pair : {std::pair{"DynastyMode", "Dynasty Mode"},
                           std::pair{"TournamentMode", "Tournament Mode"}})
    for (size_t pos = 0;
         (pos = result.find(pair.first, pos)) != std::string::npos;
         pos += std::strlen(pair.second))
      result.replace(pos, std::strlen(pair.first), pair.second);
  return result;
}
void StartupNativeScene(const char *name, uint32_t scene) {
  BindPauseMenuTheme(name,scene);
  if(!std::strcmp(name,"PLAYERGRP"))g_batter_root=scene;
  if(!std::strcmp(name,"PITCHERINFO"))g_pitcher_root=scene;
  if(!std::strcmp(name,"TRANNCAA")){g_mvp_transition_root=scene;PollNativeMvpTransition();BeginMvpTransition();}
  if(!std::strcmp(name,"HUDRIGHT") || !std::strcmp(name,"TEAMSMATCHUP") || !std::strcmp(name,"LOADING1") || !std::strcmp(name,"MAINMENU"))EndMvpTransition();
  if (!std::strcmp(name,"TEAMSMATCHUP")) BindStadiumIntro(scene);
  if (!std::strcmp(name, "LOADING1")) BindMatchupLoading(scene);
  if (!std::strcmp(name, "TEAMSELECT")) g_team_select_root = scene;
  if (!std::strcmp(name, "CNTRLSELECT")) g_team_controller_root = scene;
  if (!std::strcmp(name, "MAINMENU")) g_main_menu_root = scene;
  if(TraceStartup() && std::string(name)=="SMARTOPTIONS")
    LOGR("settings scene binding: %08X active=%d settings=%d flags=%u depth=%u next=%08X",scene,int(StartupSubmenuActive()),int(SettingsMenuActive()),kernel::R32(scene+0x28),kernel::R32(scene+0x24),kernel::R32(scene+8));
  if (std::string(name) == "POPUP") {
    g_submenu_popup = scene;
    if(!g_popup_open && scene!=g_owned_popup.load()) {
      g_owned_popup=scene;g_popup_state_known=false;
    }
  }
  int category = SubmenuCategory(name);
  if (category >= 0) {
    g_submenu_root = scene;
    g_submenu_grid = 0;
    g_submenu_category = category;
  }
}
void BindMatchupLoading(unsigned root) {g_matchup_root=root; SuppressNativeUiScene(root);}
void PollNativeBatterInfo(){
  auto valid=[](uint32_t p){return p>=0x10000u && p<0xF0000000u && mem::IsCommitted(p+0x200);};
  auto root=g_batter_root.load();BatterCard card;
  if(!valid(root) || kernel::ReadCString(root+0x10,16)!="PLAYERGRP"){PublishBatterCard(card);return;}
  auto depth=kernel::R32(root+0x24);if(depth>=127)return;
  std::array<std::string,128> ancestors;std::array<bool,128> shown{};shown[depth]=true;
  std::vector<uint32_t> nodes;std::unordered_set<uint32_t> types;
  for(auto node=kernel::R32(root+8),count=0u;count<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++count){
    auto level=kernel::R32(node+0x24);if(level>=128)continue;auto name=kernel::ReadCString(node+0x10,16);ancestors[level]=name;shown[level]=shown[level-1] && (kernel::R16(node+0x28)&1);nodes.push_back(node);
    if(name=="STATS3")card.visible=shown[level];
    bool player=false,stats=false;for(auto l=depth+1;l<level;++l){player|=ancestors[l]=="PLAYER";stats|=ancestors[l]=="STATS3";}
    if(player && name=="NAME")card.name=NativeUiText(node);
    if(player && name=="NUMBER")card.number=NativeUiText(node);
    if(player && name=="POSITION")card.position=NativeUiText(node);
    if(stats && name.size()==6 && name.rfind("LABEL",0)==0 && name[5]>='1' && name[5]<='3')card.labels[name[5]-'1']=NativeUiText(node);
    if(stats && name.size()==5 && name.rfind("STAT",0)==0 && name[4]>='1' && name[4]<='3')card.values[name[4]-'1']=NativeUiText(node);
    if(name=="SL1"){auto id=kernel::ReadCString(node+0x4E,5);for(char c:id)if(c>='0' && c<='9')card.logo+=c;}
    if(name=="NAME" || name=="SL1" || name=="TEAMSTRIPE" || name=="F1" || name=="BLACKLINE")types.insert(kernel::R32(node));
  }
  static std::unordered_map<std::string,std::string> teams;
  if(teams.empty()){std::istringstream f(overhaul::Text(overhaul::Root()/"base-logos/teams.tsv"));{std::string line;while(std::getline(f,line)){auto t=line.find('\t');if(t!=std::string::npos){auto id=line.substr(t+1);if(!id.empty()&&id.back()=='\r')id.pop_back();teams[id]=line.substr(0,t);}}}}
  if(auto i=teams.find(card.logo);i!=teams.end())card.team=i->second;
  for(const auto& team:SnapshotMatchupLoading())if(team.name==card.team)card.rank=team.rank;
  for(auto& c:card.name)c=char(std::toupper((unsigned char)c));for(auto& label:card.labels)for(auto& c:label)c=char(std::toupper((unsigned char)c));
  card.visible=card.visible && !card.name.empty() && !card.values[0].empty();
  // Restore leaves when native switches to a different statistics layout.
  static std::unordered_set<uint32_t> hidden;
  if(card.visible){for(auto node:nodes)if(types.count(kernel::R32(node)) && (kernel::R16(node+0x28)&1)){hidden.insert(node);kernel::W16(node+0x28,kernel::R16(node+0x28)&~1u);}}
  else {for(auto node:hidden)if(valid(node))kernel::W16(node+0x28,kernel::R16(node+0x28)|1u);hidden.clear();}
  if(TraceStartup() && card.visible){static std::string last;if(last!=card.name){last=card.name;LOGR("batter card: %s #%s %s team=%s logo=%s rank=%s stats=%s/%s/%s",card.name.c_str(),card.number.c_str(),card.position.c_str(),card.team.c_str(),card.logo.c_str(),card.rank.c_str(),card.values[0].c_str(),card.values[1].c_str(),card.values[2].c_str());gpu::g_capture=true;}}
  PublishBatterCard(card);
}
void PollNativePitcherInfo(){
  auto valid=[](uint32_t p){return p>=0x10000u && p<0xF0000000u && mem::IsCommitted(p) && mem::IsCommitted(p+0x200);};
  auto root=g_pitcher_root.load();if(!valid(root) || kernel::ReadCString(root+0x10,16)!="PITCHERINFO"){PublishPitcherCard({});return;}
  auto depth=kernel::R32(root+0x24);if(depth>=127)return;
  PitcherCard card;std::array<std::string,128> ancestors;std::array<bool,128> shown{};shown[depth]=true;
  std::vector<uint32_t> nodes;std::unordered_set<uint32_t> visualTypes;
  uint32_t portraitObject=0;
  for(auto node=kernel::R32(root+8),count=0u;count<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++count){
    auto level=kernel::R32(node+0x24);if(level>=128)continue;
    auto name=kernel::ReadCString(node+0x10,16);ancestors[level]=name;shown[level]=shown[level-1] && (kernel::R32(node+0x28)&1);nodes.push_back(node);
    if(name=="PITCHER")card.visible=shown[level];
    bool header=false,info=false;int row=-1;
    for(auto l=depth+1;l<level;++l){header|=ancestors[l]=="PITCHERGRP";info|=ancestors[l]=="PITCHERINFOGRID";if(ancestors[l].size()==4 && ancestors[l][0]=='X' && ancestors[l][2]=='Y' && ancestors[l][3]>='0' && ancestors[l][3]<='4')row=ancestors[l][3]-'0';}
    if(header && name=="NUMBER")card.number=NativeUiText(node);
    if(header && name=="POSITION")card.position=NativeUiText(node);
    if(name=="NAME")card.name=NativeUiText(node);
    if(name=="SVALUE")card.stamina=NativeUiText(node);
    if(info && row>=0 && name=="TL1")card.pitches[row]=NativeUiText(node);
    if(info && row>=0 && name=="NUMBER")card.ratings[row]=NativeUiText(node);
    if(name=="TEAMLOGO") {auto id=kernel::ReadCString(node+0x4E,5);for(auto c:id)if(c>='0' && c<='9')card.teamLogo+=c;}
    if(name=="PORTRAIT"){
      portraitObject=kernel::R32(node+0x80);
      for(int i=0;i<4;++i){auto bits=kernel::R32(node+0x84+i*4);std::memcpy(&card.portraitUv[i],&bits,4);}
    }
    if(name=="PORTRAIT" || name=="TEAMLOGO" || name=="NAME" || name=="TL1" || name=="DIAMONDBACK")visualTypes.insert(kernel::R32(node));
  }
  static uint32_t lastRoot=0,lastObject=0,texture=0;static std::string lastPitcher;
  if(lastRoot!=root || lastObject!=portraitObject || lastPitcher!=card.name || (texture && (!valid(texture) || (kernel::R32(texture)&0x70000)!=0x40000))){lastRoot=root;lastObject=portraitObject;lastPitcher=card.name;texture=0;}
  if(!texture && valid(portraitObject)){
    std::vector<std::pair<uint32_t,int>> queue{{portraitObject,0}};std::unordered_set<uint32_t> visited;
    for(size_t i=0;i<queue.size() && i<256;++i){auto [object,level]=queue[i];if(!valid(object) || !visited.insert(object).second)continue;
      gpu::TextureDesc desc;
      if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 && !gpu::Format(desc.format).depth && desc.width>=64 && desc.width<=512 && desc.height>=64 && desc.height<=512 && mem::IsCommitted(desc.data)){
        texture=object;if(TraceStartup())LOGR("pitcher portrait texture: header=%08X data=%08X size=%ux%u level=%d",object,desc.data,desc.width,desc.height,level);break;
      }
      if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto ptr=kernel::R32(object+offset);if(valid(ptr) && !visited.count(ptr))queue.push_back({ptr,level+1});}
    }
  }
  card.portrait=texture;
  static std::unordered_map<std::string,std::string> logoTeams;
  if(logoTeams.empty()){
    {std::istringstream f(overhaul::Text(overhaul::Root()/"base-logos/teams.tsv"));std::string line;while(std::getline(f,line)){auto tab=line.find('\t');if(tab!=std::string::npos){auto id=line.substr(tab+1);if(!id.empty()&&id.back()=='\r')id.pop_back();logoTeams[id]=line.substr(0,tab);}}}
  }
  if(auto it=logoTeams.find(card.teamLogo);it!=logoTeams.end())card.team=it->second;
  for(const auto& team:SnapshotMatchupLoading())if(team.name==card.team)card.rank=team.rank;
  for(auto& c:card.name)c=char(std::toupper((unsigned char)c));for(auto& label:card.pitches)for(auto& c:label)c=char(std::toupper((unsigned char)c));
  if(texture && !card.name.empty() && !card.stamina.empty()){
    for(auto node:nodes)if(visualTypes.count(kernel::R32(node)))kernel::W16(node+0x28,kernel::R16(node+0x28)&~1u);
  }else card.visible=false;
  if(TraceStartup()){static std::string last;std::string key=card.name+card.number+card.position+card.stamina+card.team+card.rank+std::to_string(texture);if(key!=last){last=key;LOGR("pitcher card: %s #%s %s team=%s logo=%s rank=%s stamina=%s portrait=%08X",card.name.c_str(),card.number.c_str(),card.position.c_str(),card.team.c_str(),card.teamLogo.c_str(),card.rank.c_str(),card.stamina.c_str(),texture);for(int i=0;i<5;++i)LOGR("pitcher pitch: %s = %s",card.pitches[i].c_str(),card.ratings[i].c_str());}}
  PublishPitcherCard(card);
}
void PollNativeMvpTransition(){
  auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x50);};
  auto root=g_mvp_transition_root.load();if(!valid(root) || kernel::ReadCString(root+0x10,16)!="TRANNCAA")return;
  auto depth=kernel::R32(root+0x24);
  for(auto node=kernel::R32(root+8),count=0u;count<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++count){
    auto name=kernel::ReadCString(node+0x10,16);
    bool art=name=="BACKCOLOUR" || name=="GREEN" || name=="FADE" || name=="FADE1" || name=="BLACKSPOT" || name=="BLACKSPOT1" || name=="BLACKFADE" || name=="NCAA" || name=="NCAA1" || name=="NCAA2" || (name.size()==5 && (name.rfind("STAR",0)==0 || name.rfind("LINE",0)==0) && name[4]>='1' && name[4]<='6') || (name.size()==6 && name.rfind("WLINE",0)==0 && name[5]>='1' && name[5]<='6');
    // These two static parents are not animation targets. Hiding them prevents
    // a keyframe from re-enabling an old NCAA sprite during the wipe's reveal.
    if(art || name=="BACKART" || name=="FORGROUND")kernel::W16(node+0x28,kernel::R16(node+0x28)&~1u);
  }
}
void BindStadiumIntro(unsigned root) {g_stadium_intro_root=root;}
void PollNativeStadiumIntro() {
  auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x50);};
  auto root=g_stadium_intro_root.load();
  if(!valid(root) || kernel::ReadCString(root+0x10,16)!="TEAMSMATCHUP"){PublishStadiumIntro({},"",false);return;}
  auto depth=kernel::R32(root+0x24);if(depth>=127)return;
  std::array<std::string,128> ancestors;
  std::array<bool,128> shown{};shown[depth]=(kernel::R32(root+0x28)&1)!=0;
  std::array<MatchupSide,2> sides;
  auto loaded=SnapshotMatchupLoading();std::string stadium;bool visible=false;
  for(auto node=kernel::R32(root+8),count=0u;count<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++count){
    auto level=kernel::R32(node+0x24);if(level>=128)continue;
    auto name=kernel::ReadCString(node+0x10,16);ancestors[level]=name;
    shown[level]=shown[level-1] && (kernel::R32(node+0x28)&1);
    if(name=="ALL")visible=shown[level];
    int side=-1;for(auto l=depth+1;l<level;++l){if(ancestors[l]=="X0Y0")side=0;if(ancestors[l]=="X0Y1")side=1;}
    if(side>=0 && name=="FIRSTPART")sides[side].name=NativeUiText(node);
    if(side>=0 && name=="SECONDPART")sides[side].nickname=NativeUiText(node);
    if(name=="STADIUM")stadium=NativeUiText(node);
    // Suppress only the old artwork/text. Native groups, timers and camera
    // callbacks retain their normal visibility and transition behavior.
    if(name=="STADIUM" || name=="FIRSTPART" || name=="SECONDPART" || name=="ROUND" || name=="LOGO" || name=="LOGOCROP" || name=="GRADIENT" || name=="RT1" || name=="PAUSEBACK1" || name=="BLACKDARKEN1" || name=="BLACKDARKEN" || name=="BGRADIENT" || name=="BGRADIENT1" || name.rfind("WHITE",0)==0 || (name.size()==2 && name[0]=='F' && name[1]>='1' && name[1]<='9'))
      kernel::W16(node+0x28,kernel::R16(node+0x28)&~1u);
  }
  for(auto& team:sides){for(auto& c:team.name)c=char(std::toupper((unsigned char)c));for(auto& c:team.nickname)c=char(std::toupper((unsigned char)c));for(const auto& match:loaded)if(match.name==team.name)team.rank=match.rank;}
  PublishStadiumIntro(sides,stadium,visible);
}
void PollNativeMatchupLoading() {
  auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x50);};
  auto root=g_matchup_root.load();
  if(valid(root))SuppressNativeUiScene(root);
  if(!valid(root) || kernel::ReadCString(root+0x10,16)!="LOADING1") {PublishMatchupLoading({},false,false);return;}
  auto depth=kernel::R32(root+0x24);if(depth>=127)return;
  std::array<std::string,128> ancestors;
  std::array<bool,128> shown{};shown[depth]=true;
  std::array<MatchupSide,2> sides;
  auto selected=SnapshotTeamSelect();
  for(int i=0;i<2;++i){sides[i].name=selected[i].name;sides[i].nickname=selected[i].nickname;}
  bool regular=false,ready=false;
  for(auto node=kernel::R32(root+8),count=0u;count<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++count){
    auto level=kernel::R32(node+0x24);if(level>=128)continue;
    auto name=kernel::ReadCString(node+0x10,16);ancestors[level]=name;
    shown[level]=shown[level-1] && (kernel::R32(node+0x28)&1);
    if(name=="REGGAME")regular=shown[level];
    if(name=="PRESSBUTTON")ready=shown[level];
    int side=-1;bool inRegular=false;
    for(auto l=depth+1;l<level;++l){if(ancestors[l]=="AWAY")side=0;if(ancestors[l]=="HOME")side=1;if(ancestors[l]=="REGGAME")inRegular=true;}
    if(side<0 || !inRegular || !shown[level])continue;
    auto& team=sides[side];
    if(name=="NAME") {auto text=NativeUiText(node);if(!text.empty()) {for(auto& c:text)c=char(std::toupper((unsigned char)c));team.name=text;}}
    if(name=="WIN")team.wins=NativeUiText(node);
    if(name=="LOSS")team.losses=NativeUiText(node);
    if(name=="RANK")team.grade=NativeUiText(node);
    if(name=="NATIONALRANK")team.rank=NativeUiText(node);
  }
  // REGGAME is hidden by the native exit animation before the next scene
  // is ready. Keep the replacement covering that entire handoff.
  if(regular) PublishMatchupLoading(sides,true,ready);

}
int NativeTeamControllerLane(int port) {
  auto root=g_team_controller_root.load();
  auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p);};
  if(port<0 || port>=6 || !valid(root))return -2;
  auto depth=kernel::R32(root+0x24),node=kernel::R32(root+8);
  std::array<bool,128> visible{};
  if(depth>=128)return -2;
  visible[depth]=true;
  auto label="P"+std::to_string(port+1);
  for(int n=0;n<4096 && valid(node) && kernel::R32(node+0x24)>depth;++n,node=kernel::R32(node+8)){
    auto level=kernel::R32(node+0x24);if(level>=128)continue;
    visible[level]=visible[level-1] && (kernel::R32(node+0x28)&1);
    if(visible[level] && kernel::ReadCString(node+0x10,16)==label){
      int y=int16_t(kernel::R16(node+0x2C));
      // These are the native top, middle and bottom player-label markers.
      // Read the active label, not the static hidden P2 position templates.
      return y<160?0:y>240?1:-1;
    }
  }
  return -2;
}
void PollNativeTeamSelect() {
  auto valid = [](uint32_t p) {return p >= 0x80000000u && p < 0x90000000u && mem::IsCommitted(p);};
  auto root = g_team_select_root.load();
  if (!valid(root)) return;
  std::array<TeamSelectSide, 2> sides;
  std::vector<TeamSelectController> players;
  std::array<std::string, 128> ancestors;
  std::array<bool, 128> shown{};
  auto depth = kernel::R32(root + 0x24);
  if (depth >= 128) return;
  shown[depth] = true;
  auto node = kernel::R32(root + 8);
  for (int n = 0; n < 4096 && valid(node) && kernel::R32(node + 0x24) > depth;
       ++n, node = kernel::R32(node + 8)) {
    auto level = kernel::R32(node + 0x24);
    if (level >= 128) continue;
    auto name = kernel::ReadCString(node + 0x10, 16);
    ancestors[level] = name;
    shown[level] = shown[level - 1] && (kernel::R32(node + 0x28) & 1);
    int side = -1, rating = -1;
    for (auto l = depth + 1; l < level; ++l) {
      if (ancestors[l] == "AWAYTEAM") side = 0;
      if (ancestors[l] == "HOMETEAM") side = 1;
      const char* names[] = {"OVERALL", "PITCHING", "BATTING", "FIELDING", "RUNNING"};
      for (int j = 0; j < 5; ++j) if (ancestors[l] == names[j]) rating = j;
    }
    if (side < 0) continue;
    auto& team = sides[side];
    bool uniform=false,difficulty=false;
    for(auto l=depth+1;l<=level;++l){
      if(ancestors[l]=="HOMEJERSEY" || ancestors[l]=="AWAYJERSEY")uniform=true;
      if(ancestors[l]=="HOMEDIFFICULTY" || ancestors[l]=="AWAYDIFFICULTY")difficulty=true;
    }
    if(shown[level]){
      if(name=="HOMEJERSEY" || name=="AWAYJERSEY")team.step=TeamSelectSide::Step::Uniform;
      if(name=="HOMEDIFFICULTY" || name=="AWAYDIFFICULTY")team.step=TeamSelectSide::Step::Difficulty;
      if(name=="HOMEREADY" || name=="AWAYREADY")team.step=TeamSelectSide::Step::Ready;
      if(uniform && name=="JERSEYNAME")team.uniform=NativeUiText(node);
      if(difficulty && (name=="DHOME" || name=="DAWAY"))team.difficulty=NativeUiText(node);
      if(difficulty && name=="STAT")team.difficultyDescription=NativeUiText(node);
      if(uniform && (name=="HOMEJERSEY" || name=="AWAYJERSEY") && kernel::R32(node)==0x0060FCD0){
        // Native sprite-list class stores its active four-character XSH entry
        // inline here; this also covers alternate uniforms with separate IDs.
        auto sprite=kernel::ReadCString(node+0x4E,5);
        if(sprite.size()==4 && sprite[0]>='0' && sprite[0]<='9' && sprite[1]>='0' && sprite[1]<='9' && sprite[2]>='0' && sprite[2]<='9' && sprite[3]>='a' && sprite[3]<='z')team.uniformPreview=sprite;
      }
    }
    if (name == "FULLNAME1") team.name = NativeUiText(node);
    if (name == "FULLNAME2") team.nickname = NativeUiText(node);
    if (name == "LEAGUE") team.conference = NativeUiText(node);
    if (name == "RATING" && rating >= 0 && shown[level]) team.grades[rating] = NativeUiText(node);
    if (name == "RANK") {
      auto text = NativeUiText(node);
      if (text.find_first_of("0123456789") != std::string::npos) team.rank = text;
    }
  }
  root = g_team_controller_root.load();
  if (valid(root)) {
    depth = kernel::R32(root + 0x24);
    if (depth < 128) {
      shown.fill(false);shown[depth] = true;
      bool cpu[] = {false, false};
      std::string player[2];
      node = kernel::R32(root + 8);
      for (int n = 0; n < 4096 && valid(node) && kernel::R32(node + 0x24) > depth;
           ++n, node = kernel::R32(node + 8)) {
        auto level = kernel::R32(node + 0x24);
        if (level >= 128) continue;
        auto name = kernel::ReadCString(node + 0x10, 16);
        shown[level] = shown[level - 1] && (kernel::R32(node + 0x28) & 1);
        if (name == "LEFTCPU" && shown[level]) cpu[0] = true;
        if (name == "RIGHTCPU" && shown[level]) cpu[1] = true;
        if (shown[level] && name.size() == 2 && name[0] == 'P' && name[1] >= '1' && name[1] <= '6') {
          // Native player labels move to the top (away) or bottom (home)
          // controller marker. Hidden marker labels are static P2 templates.
          int lane = NativeTeamControllerLane(name[1]-'1');
          int side = lane==1?1:0;
          auto text = NativeUiText(node);
          players.push_back({text,lane});
          if(lane==0 || lane==1){if (!player[side].empty()) player[side] += " / ";player[side] += text;}
          if (TraceStartup()) {
            static std::string last;
            auto key = name + std::to_string(kernel::R16(node + 0x2C));
            if(last != key){last=key;LOGR("team select controller: %s x=%d y=%d text=%s",name.c_str(),int16_t(kernel::R16(node+0x2A)),int16_t(kernel::R16(node+0x2C)),text.c_str());}
          }
        }
      }
      for (int i = 0; i < 2; ++i) sides[i].controller = cpu[i] ? "CPU" : player[i].empty() ? "PLAYER" : player[i];
    }
  }
  for(auto& side:sides)
    for(auto* text:{&side.name,&side.nickname,&side.conference})
      for(auto& c:*text)if(c>='a' && c<='z')c-=32;
  PublishTeamSelect(sides,players);
}
void PollNativeMainMenu() {
  auto valid = [](uint32_t p) { return p >= 0x80000000u && p < 0x90000000u && mem::IsCommitted(p); };
  auto root = g_main_menu_root.load();
  if (!valid(root)) return;
  auto depth = kernel::R32(root + 0x24);
  auto node = kernel::R32(root + 8);
  for (int n = 0; n < 4096 && valid(node) && kernel::R32(node + 0x24) > depth;
       ++n, node = kernel::R32(node + 8)) {
    if (kernel::ReadCString(node + 0x10, 16) != "CELL") continue;
    auto v = kernel::R32(node);
    if (v < 0x500000 || v >= 0x700000 || kernel::R32(v + 0x11C) != 0xA7850) continue;
    auto columns = kernel::R32(node + 0x4C), rows = kernel::R32(node + 0x50);
    auto x = kernel::R32(node + 0x6C), y = kernel::R32(node + 0x70);
    if (columns && rows && columns * rows == 8 && x < columns && y < rows)
      StartupMainMenuSelection(int(x + columns * y));
    return;
  }
}
void PollNativeSubmenu() {
  auto valid = [](uint32_t p) { return p >= 0x80000000u && p < 0x90000000u; };
  bool popup = false;
  if(g_popup_state_known && g_owned_popup.load())g_submenu_popup=g_owned_popup.load();
  if (valid(g_submenu_popup) && kernel::R32(g_submenu_popup+0x5CC) &&
      (kernel::R32(g_submenu_popup + 0x28) & 2)) {
    auto depth = kernel::R32(g_submenu_popup + 0x24),
         node = kernel::R32(g_submenu_popup + 8);
    for (int i = 0; i < 2048 && valid(node) && kernel::R32(node + 0x24) > depth;
         i++, node = kernel::R32(node + 8)) {
      if (kernel::ReadCString(node + 0x10, 16) == "POPUPGRP") {
        popup = (kernel::R32(node + 0x28) & 1) != 0;
        break;
      }
    }
  }
  if(g_popup_state_known)
    popup=g_popup_open && valid(g_submenu_popup) && kernel::R32(g_submenu_popup+0x5CC)!=0;
  SetSubmenuPopupVisible(popup);
  if (popup) {
    // The host notification is the sole presentation; retain native modal data
    // and callbacks, including while its entry/exit timeline changes focus.
    if(PauseMenuThemeActive())SuppressNativeUiScene(g_submenu_popup);
    std::string body;
    std::array<std::string, 4> options;
    int selected = 0;
    auto depth = kernel::R32(g_submenu_popup + 0x24),
         node = kernel::R32(g_submenu_popup + 8);
    std::array<bool, 128> shown{};
    if (depth < shown.size())
      shown[depth] = true;
    for (int i = 0; i < 2048 && valid(node) && kernel::R32(node + 0x24) > depth;
         i++, node = kernel::R32(node + 8)) {
      auto name = kernel::ReadCString(node + 0x10, 16);
      auto level = kernel::R32(node + 0x24);
      bool visible = false;
      if (level && level < shown.size()) {
        shown[level] = shown[level - 1] && (kernel::R32(node + 0x28) & 1);
        visible = shown[level];
      }
      if (name == "MAINTEXT")
        body = NativeUiText(node);
      if (visible && name.size() == 7 && name.rfind("OPTION", 0) == 0 &&
          name[6] >= '0' && name[6] <= '3')
        options[name[6] - '0'] = NativeUiText(node);
      if (name == "GRID") {
        auto columns = kernel::R32(node + 0x4C);
        selected =
            int(kernel::R32(node + 0x6C) + columns * kernel::R32(node + 0x70));
      }
    }
    std::vector<std::string> choices;
    int displaySelected = 0;
    for (int i = 0; i < 4; i++)
      if (!options[i].empty()) {
        if (i == selected)
          displaySelected = int(choices.size());
        choices.push_back(options[i]);
      }
    PublishPopup(body, choices, displaySelected);
  }
  if (!StartupSubmenuActive())
    return;
  if (!valid(g_submenu_root) || g_submenu_category < 0)
    return;
  {
    g_submenu_grid = 0;
    std::array<bool, 128> visible{};
    uint32_t depth = kernel::R32(g_submenu_root + 0x24),
             node = kernel::R32(g_submenu_root + 8);
    if (depth >= visible.size())
      return;
    visible[depth] = (kernel::R32(g_submenu_root + 0x28) & 1) != 0;
    for (int n = 0; n < 4096 && valid(node) && kernel::R32(node + 0x24) > depth;
         n++, node = kernel::R32(node + 8)) {
      auto name = kernel::ReadCString(node + 0x10, 16);
      auto v = kernel::R32(node);
      auto level = kernel::R32(node + 0x24);
      if (level >= visible.size())
        continue;
      visible[level] =
          visible[level - 1] && (kernel::R32(node + 0x28) & 1) != 0;
      if (visible[level] && (kernel::R32(node + 0x28) & 2) &&
          (name == "MENU" || name == "CELL" || name == "CELL2" || name == "FILETABLE") && v >= 0x500000 && v < 0x700000 &&
          kernel::R32(v + 0x11C) == 0xA7850) {
        g_submenu_grid = node;
        break;
      }
    }
  }
  if (!valid(g_submenu_grid)) {
    if(SettingsMenuActive() && TraceStartup()) {
      static uint32_t logged=0;if(logged!=g_submenu_root.load()) {
        logged=g_submenu_root;LOGR("settings waiting grid: root=%08X flags=%u depth=%u next=%08X count=%u",logged,kernel::R32(logged+0x28),kernel::R32(logged+0x24),kernel::R32(logged+8),kernel::R32(logged+0x218));
      }
    }
    return;
  }
  uint32_t columns = kernel::R32(g_submenu_grid + 0x4C),
           rows = kernel::R32(g_submenu_grid + 0x50),
           table = kernel::R32(g_submenu_grid + 0x9C),
           selected = kernel::R32(g_submenu_grid + 0x6C) +
                      columns * kernel::R32(g_submenu_grid + 0x70);
  uint32_t count = rows * columns;
  if(SettingsMenuActive() && TraceStartup()) {
    static uint32_t logged=0;if(logged!=g_submenu_root.load()) {
      logged=g_submenu_root;LOGR("settings grid binding: %08X cols=%u rows=%u table=%08X selected=%u",g_submenu_grid,columns,rows,table,selected);
    }
  }
  if (!rows || !columns || count > 64 || !valid(table) || selected >= count)
    return;
  if(int kind=LibraryMenuKind()) {
    int number=kind==1?int(count):int(std::max(rows,columns));
    std::vector<SettingsRow> entries(number);
    for(uint32_t i=0;i<count;++i) {
      auto cell=kernel::R32(table+i*4);if(!valid(cell))continue;
      auto cellName=kernel::ReadCString(cell+0x10,16);
      auto y=cellName.find('Y');if(cellName.empty() || cellName[0]!='X' || y==std::string::npos)continue;
      int x=std::atoi(cellName.c_str()+1),row=std::atoi(cellName.c_str()+y+1);
      if(row<0 || row>=number)continue;
      auto depth=kernel::R32(cell+0x24),node=kernel::R32(cell+8);
      for(int n=0;n<256 && valid(node) && kernel::R32(node+0x24)>depth;++n,node=kernel::R32(node+8)) {
        auto name=kernel::ReadCString(node+0x10,16);
        if(name=="TB1" && (kind==1 || x==1))entries[row].label=NativeUiText(node);
        if(name=="TEXTLIST" && kind==2)entries[row].value=NativeUiText(node);
      }
    }
    std::string message,album;
    auto depth=kernel::R32(g_submenu_root+0x24),node=kernel::R32(g_submenu_root+8);
    for(int i=0;i<4096 && valid(node) && kernel::R32(node+0x24)>depth;++i,node=kernel::R32(node+8)) {
      auto name=kernel::ReadCString(node+0x10,16);
      if(name=="MESSAGE")message=NativeUiText(node);
      if(name=="ALBUM")album=NativeUiText(node);
      if(kind==2 && name=="SONGCOUNT")message=NativeUiText(node);
    }
    int row=kind==1?int(selected):int(kernel::R32(g_submenu_grid+(columns>=rows?0x6C:0x70)));
    PublishLibrary(row,entries,message,album);
    return;
  }
  if(SettingsMenuActive()) {
    auto activeCount=kernel::R32(g_submenu_root+0x218);
    if(activeCount>0 && activeCount<=count)count=activeCount;
    std::vector<SettingsRow> options;
    for(uint32_t i=0;i<count;++i) {
      uint32_t cell=kernel::R32(table+i*4);
      SettingsRow row;
      if(!valid(cell))continue;
      row.enabled = (kernel::R32(cell + 0x28) & 2) != 0;
      auto depth=kernel::R32(cell+0x24),node=kernel::R32(cell+8);
      for(int n=0;n<256 && valid(node) && kernel::R32(node+0x24)>depth;
          ++n,node=kernel::R32(node+8)) {
        auto name=kernel::ReadCString(node+0x10,16);
        if(name=="TEXT")row.label=NativeUiText(node);
        if(name=="TEXTLIST")row.value=NativeUiText(node);
      }
      auto objects=kernel::R32(g_submenu_root+0x224);
      if(valid(objects)) {
        auto object=kernel::R32(objects+i*4);
        if(valid(object) && kernel::R32(object+8)>=0x90 && kernel::R32(object+8)<0xB1)
          row.value=PcOptionValue(kernel::R32(object+8)-0x90);
        if(valid(object) && kernel::R32(object)==0x63D468 && !row.value.empty()) {
          // This native row class has six volume steps (ctor 00312BE0).
          char *end=nullptr;auto value=std::strtof(row.value.c_str(),&end);
          if(end && !*end)row.fraction=std::clamp(value/6.f,0.f,1.f);
        }
      }
      for(auto &c:row.label)if(c>='a' && c<='z')c-=32;
      for(auto &c:row.value)if(c>='a' && c<='z')c-=32;
      if(!row.label.empty())options.push_back(row);
    }
    if(TraceStartup()) {
      static uint32_t logged=0;
      if(logged!=g_submenu_root && !options.empty()) {
        logged=g_submenu_root;
        LOGR("settings native metadata: scene=%08X active=%u grid=%u",g_submenu_root.load(),activeCount,count);
        auto objects=kernel::R32(g_submenu_root+0x224);
        if(valid(objects)){auto object=kernel::R32(objects);if(valid(object))
          LOGR("settings first row object: %08X %u %u %u %u %u %u",kernel::R32(object),kernel::R32(object+4),kernel::R32(object+8),kernel::R32(object+12),kernel::R32(object+16),kernel::R32(object+20),kernel::R32(object+24));}
        for(auto &r:options)LOGR("settings native row: %s = %s enabled=%d",r.label.c_str(),r.value.c_str(),int(r.enabled));
      }
    }
    PublishSettings(int(selected),options);
    return;
  }
  std::vector<uint32_t> labels;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t cell = kernel::R32(table + i * 4), found = 0, fallback = 0;
    if (valid(cell)) {
      auto depth = kernel::R32(cell + 0x24), node = kernel::R32(cell + 8);
      for (int n = 0;
           n < 512 && valid(node) && kernel::R32(node + 0x24) > depth;
           n++, node = kernel::R32(node + 8)) {
        auto name = kernel::ReadCString(node + 0x10, 16);
        auto id = SubmenuLabelId(g_submenu_category, name.c_str());
        // New/Load uses runtime-localized text rather than the parent menu's
        // node names. Read the text ID from those native text nodes.
        if (!id && (name == "NEWDYNASTY" || name == "LOADDYNASTY"))
          id = kernel::R32(node + 0x48);
        if (id) {
          if (!fallback)
            fallback = id;
          if (kernel::R32(node + 0x28) & 1) {
            found = id;
            break;
          }
        }
      }
    }
    labels.push_back(found ? found : fallback);
  }
  PublishSubmenu(g_submenu_category, int(selected), labels);
}
} // namespace game

// The movie manager owns its completion callback. Route EA through its normal
// stop/completed request (-1), rather than returning with a pending callback.
X86_EXTERN_FUNC(__imp__sub_000C0230);
X86_FUNC(sub_000C0230) {
  game::WorkshopMovie(ctx);
  if (!overhaul::Active()) return __imp__sub_000C0230(ctx, base);
  uint32_t movie = kernel::R32(ctx.esp + 4);
  if(movie==9 || movie==11)game::EndStartupLoading();
  if (TraceStartup())
    LOGR("startup: movie request %d callback %08X", int(movie),
         kernel::R32(ctx.ecx + 0x18));
  if (movie == 11) {
    game::BeginLicensing();
    if (TraceStartup())
      gpu::g_capture = true;
    g_legal_movie_manager = ctx.ecx;
    g_legal_callback = kernel::R32(ctx.ecx + 0x18);
    g_legal_until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    LOGR("startup: holding licensing screen before completing MVP intro skip");
    kernel::Return(ctx, 0, 2);
    return;
  }
  if (movie == 9 || movie == 11) {
    LOGR("startup: skipping %s through movie completion",
         movie == 9 ? "EA Sports" : "MVP intro");
    kernel::W32(ctx.esp + 4, ~0u);
  }
  __imp__sub_000C0230(ctx, base);
}

// Hold only the title screen's routing update, while native scene animations
// continue. Complete while TITLE is still alive, before its normal transition.
X86_EXTERN_FUNC(__imp__sub_0053B8E0);
X86_FUNC(sub_0053B8E0) {
  if (!overhaul::Active()) return __imp__sub_0053B8E0(ctx, base);
  if (g_legal_movie_manager) {
    if (std::chrono::steady_clock::now() < g_legal_until) {
      kernel::Return(ctx, 0, 0);
      return;
    }
    auto saved = ctx;
    uint32_t manager = g_legal_movie_manager;
    g_legal_movie_manager = 0;
    kernel::W32(manager + 0x18, g_legal_callback);
    ctx.ecx = manager;
    kernel::CallGuest(ctx, 0x000C0230, {~0u, 0});
    ctx = saved;
    LOGR("startup: licensing hold complete");
  }
  __imp__sub_0053B8E0(ctx, base);
}

// Resolve a named native FEL animation; traces verify which timelines the
// original screen classes start, before adding custom screen-level timing.
X86_EXTERN_FUNC(__imp__sub_000AFCE0);
X86_FUNC(sub_000AFCE0) {
  game::WorkshopObserve(ctx.ecx);
  if (!overhaul::Active()) return __imp__sub_000AFCE0(ctx, base);
  if (TraceStartup())
    LOGR("startup: timeline lookup %s on %08X",
         kernel::ReadCString(kernel::R32(ctx.esp + 4), 64).c_str(), ctx.ecx);
  if (TraceStartup()) {
    uint32_t head = kernel::R32(ctx.ecx + 0x50);
    LOGR("startup: timeline list %08X %s", head,
         head ? kernel::ReadCString(head + 5, 32).c_str() : "empty");
  }
  auto timelineName = kernel::ReadCString(kernel::R32(ctx.esp + 4), 64);
  auto timelineOwner=kernel::ReadCString(ctx.ecx+0x10,16);
  if(timelineOwner=="TEAMSMATCHUP")game::StadiumIntroTimeline(timelineName.c_str());
  if(timelineOwner=="PITCHERINFO")game::PitcherCardTimeline(timelineName.c_str());
  if(timelineOwner=="PLAYERGRP")game::BatterCardTimeline(timelineName.c_str());
  game::PauseMenuThemeTimeline(timelineOwner.c_str(),timelineName.c_str());
  __imp__sub_000AFCE0(ctx, base);
  // Popups use the same UPMENU/DOWNMENU names. They are independent cursors.
  if(timelineOwner=="MAINMENU" && !game::SubmenuPopupVisible())
    game::StartupTimeline(timelineName.c_str());
  if (ctx.eax && timelineName == "STARTKA")
    game::StartupReady();
  if (TraceStartup())
    LOGR("startup: timeline object %08X", ctx.eax);
}

// Native answer/cancel callbacks release modal ownership before dispatching.
X86_EXTERN_FUNC(__imp__sub_000B8E10);
X86_FUNC(sub_000B8E10) {
  if (!overhaul::Active()) return __imp__sub_000B8E10(ctx, base);
  game::PopupClosed(ctx.ecx);
  __imp__sub_000B8E10(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_000B95C0);
X86_FUNC(sub_000B95C0) {
  if (!overhaul::Active()) return __imp__sub_000B95C0(ctx, base);
  game::PopupClosed(ctx.ecx);
  __imp__sub_000B95C0(ctx,base);
}

// FEGroup::Render is also invoked directly by the scene manager, rather than
// solely through the virtual dispatcher. Skip presentation, never updates/input.
X86_EXTERN_FUNC(__imp__sub_000BBAC0);
X86_FUNC(sub_000BBAC0) {
  game::WorkshopObserve(ctx.ecx);
  if (!overhaul::Active()) return __imp__sub_000BBAC0(ctx, base);
  if(game::NativeUiRenderSuppressed(ctx.ecx,0x000BBAC0)) {
    if(TraceStartup()){static std::unordered_set<uint32_t> seen;if(seen.insert(ctx.ecx).second)LOGR("native scene draw bypassed: %s",kernel::ReadCString(ctx.ecx+0x10,16).c_str());}
    ctx.esp+=4;return;
  }
  __imp__sub_000BBAC0(ctx,base);
}

// Popup::Render also writes its visible-state latch after drawing. Keep that
// native state write while the existing host notification owns presentation.
X86_EXTERN_FUNC(__imp__sub_000B8DD0);
X86_FUNC(sub_000B8DD0) {
  if (!overhaul::Active()) return __imp__sub_000B8DD0(ctx, base);
  if(game::NativeUiRenderSuppressed(ctx.ecx,0x000BBAC0)) {
    kernel::W16(ctx.ecx+0x5FE,(kernel::R16(ctx.ecx+0x5FE)&0xFF) | ((kernel::R16(ctx.ecx+0x28)&1)<<8));
    ctx.esp+=4;return;
  }
  __imp__sub_000B8DD0(ctx,base);
}
