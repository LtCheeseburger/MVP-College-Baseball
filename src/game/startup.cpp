#include "core/dev_tools.h"
#include "core/patch_json.h"
#include "game/expansion.h"
#include "game/phase9.h"
#include "game/pitch_clock.h"
#include <cstdio>
#include <algorithm>
// Startup routing hooks; the host renderer animates artwork over native FEL
// input.
#include "../common/startup_ui.h"
#include "../common/submenu_ui.h"
#include "../common/game_hooks.h"
#include "../common/team_select_ui.h"
#include "../common/roster_browser_ui.h"
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
#include "core/input.h"
#include "gpu/backend.h"
#include "kernel/memory.h"
#include "kernel/mods.h"
#include "xdk/hle.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <mutex>

namespace game { namespace { bool FeScene(uint32_t,const char*); bool FeVisibleScene(uint32_t,const char*); } }

namespace {
thread_local uint32_t g_legal_movie_manager = 0;
thread_local uint32_t g_legal_callback = 0;
std::atomic<uint32_t> g_submenu_root{0};
std::atomic<uint32_t> g_main_menu_root{0};
std::atomic<uint32_t> g_team_select_root{0}, g_team_controller_root{0};
std::atomic<uint32_t> g_matchup_root{0};
std::atomic<uint32_t> g_loadout_root{0};
std::atomic<uint32_t> g_dynasty_school_root{0};
// Publish the alphabetic picker only after all native spinner steps finish.
std::recursive_mutex g_dynasty_picker_mutex;
std::atomic<unsigned> g_picker_expected{0},g_picker_displayed{0};
std::atomic<bool> g_picker_changing{false};
std::atomic<double> g_picker_ready_after{0};
double PickerNow(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::atomic<uint32_t> g_dynasty_home_root{0};
std::atomic<int> g_dashboard_section{0};
std::atomic<int> g_dynasty_home_row{0};
std::atomic<uint32_t> g_playoff_root{0},g_hire_staff_root{0},g_series_scores_root{0},g_series_summary_root{0},g_linescore_popup_root{0};
std::atomic<uint32_t> g_calendar_root{0}, g_calendar_game_root{0}, g_dynasty_navbar_root{0};
std::atomic<uint32_t> g_play_sim_root{0};
std::atomic<uint32_t> g_starting_pitcher_root{0},g_starting_lineup_root{0},g_final_play_sim_root{0},g_coach_root{0},g_sim_game_root{0},g_final_score_root{0},g_box_score_root{0},g_recruiting_root{0},g_stats_landing_root{0},g_statistics_menu_root{0},g_top25_root{0},g_standings_root{0},g_dynasty_rightbar_root{0},g_recruit_list_root{0},g_recruit_title_root{0};
std::atomic<uint32_t> g_depth_chart_root{0},g_roster_editor_root{0},g_batpitch_info_root{0},g_pos_rank_root{0},g_team_goals_root{0},g_roster_status_root{0};
game::DynastySchoolCard g_managed_school;
std::atomic<uint32_t> g_recruit_profile_root{0};
std::atomic<bool> g_player_editor_loading{false};
std::atomic<double> g_editor_transition_until{0};
std::atomic<uint32_t> g_create_edit_root{0},g_edit_list_root{0},g_create_editor_root{0},g_editor_keyboard_root{0},g_award_watch_root{0};
std::atomic<uint32_t> g_trade_root{0};
std::atomic<uint32_t> g_ballpark_loading_root{0};
std::atomic<bool> g_ballpark_session{false};
std::atomic<uint32_t> g_tutorial_movie{~0u};
std::array<std::atomic<uint32_t>,7> g_utility_roots{};
std::atomic<uint32_t> g_recruit_actions_root{0},g_recruit_offer_root{0},g_mail_root{0},g_mail_reader_root{0};
std::atomic<uint32_t> g_recruit_portrait_epoch{0};
std::atomic<uint32_t> g_mail_filters_root{0};
std::array<std::atomic<uint32_t>,4> g_offseason_roots{};
std::atomic<uint32_t> g_dynasty_options_root{0};
std::atomic<uint32_t> g_team_manage_root{0},g_team_manage_grid{0};
std::atomic<int> g_team_manage_target{-1};
std::atomic<unsigned> g_team_manage_enabled{1023};
std::atomic<bool> g_team_manage_presented{false};
std::atomic<uint32_t> g_statistics_grid{0};
std::atomic<int> g_statistics_target{-1};
std::array<game::TeamSelectSide,2> g_pregame_teams;
std::atomic<int> g_action_navigation{0};
std::atomic<unsigned> g_setup_mode{0};

struct LeagueSchool {
  unsigned index, asset, conference, color;
  std::string name, nickname, location, abbreviation;
};

const std::vector<LeagueSchool>& LeagueSchools() {
  static const auto schools = [] {
    std::vector<LeagueSchool> result;
    std::vector<uint8_t> bytes;
    if (!kernel::mods::Load(kernel::mods::GameDir() / "data/league/teams.json", bytes)) return result;
    const std::string json(bytes.begin(), bytes.end());
    patch_json::Parser parser{json};
    const auto data = parser.Value();
    parser.Space();
    if (parser.bad || parser.p != json.size() || data.type != patch_json::Json::Array) return result;
    for (const auto& row : data.items) {
      auto index = unsigned(row.Num("index", 0));
      auto asset = unsigned(row.Num("assetId", 0));
      auto color = row.Str("primaryColor");
      auto location = row.Str("city");
      auto state = row.Str("state");
      if (!state.empty()) location += (location.empty() ? "" : ", ") + state;
      if (index && asset)
        result.push_back({index, asset, unsigned(row.Num("conference", 0)),
                          color.size() == 7 ? unsigned(std::strtoul(color.c_str()+1, nullptr, 16)) : 0x343C46,
                          row.Str("name"), row.Str("nickname"), std::move(location),row.Str("abbreviation")});
    }
    // The native picker cycles by school name, independent of database ID or rank.
    std::stable_sort(result.begin(), result.end(), [](const LeagueSchool& a, const LeagueSchool& b) {
      return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return result;
  }();
  return schools;
}
std::array<std::string,3> LeagueSeasonRecords(unsigned index) {
  static const auto records=[] {
    std::unordered_map<unsigned,std::array<std::string,3>> result;
    std::vector<uint8_t> bytes;
    if(!kernel::mods::Load(kernel::mods::GameDir()/"data/league/season-records.json",bytes))return result;
    const std::string json(bytes.begin(),bytes.end());
    patch_json::Parser parser{json};const auto data=parser.Value();parser.Space();
    if(parser.bad || parser.p!=json.size() || data.type!=patch_json::Json::Array)return result;
    for(const auto& row:data.items){
      const auto id=unsigned(row.Num("index",0));if(!id)continue;
      std::array<std::string,3> seasons;
      for(unsigned i=0;i<seasons.size();++i){
        const auto year=std::to_string(2024+i);
        auto record=row.Str(year.c_str());
        // Never treat malformed or missing historical data as a 0-0 season.
        unsigned wins=0,losses=0,ties=0;int used=0;
        const int parts=std::sscanf(record.c_str(),"%u-%u-%u%n",&wins,&losses,&ties,&used);
        bool valid=parts==3 && used==record.size();
        if(!valid){used=0;valid=std::sscanf(record.c_str(),"%u-%u%n",&wins,&losses,&used)==2 && used==record.size();}
        if(valid && wins<=200 && losses<=200 && ties<=200)seasons[i]=std::move(record);
      }
      result.emplace(id,std::move(seasons));
    }
    return result;
  }();
  const auto found=records.find(index);
  return found==records.end()?std::array<std::string,3>{}:found->second;
}
std::array<std::string,2> LeagueConferenceLabels(unsigned id) {
 static const auto labels=[] {
  std::unordered_map<unsigned,std::array<std::string,2>> result;
  std::vector<uint8_t> bytes;
  if(!kernel::mods::Load(kernel::mods::GameDir()/"data/database/database_big/conf.dat",bytes))return result;
  std::istringstream lines(std::string(bytes.begin(),bytes.end()));std::string line;
  while(std::getline(lines,line)) {
   const auto full=line.find(",0 "),shortName=line.find(",1 "),end=line.find(",2 ");
   if(full==std::string::npos || shortName==std::string::npos || end==std::string::npos)continue;
   const auto index=unsigned(std::strtoul(line.c_str(),nullptr,16));
   result[index]={line.substr(full+3,shortName-full-3),line.substr(shortName+3,end-shortName-3)};
  }
  return result;
 }();
 auto it=labels.find(id);return it==labels.end()?std::array<std::string,2>{}:it->second;
}


const LeagueSchool* SchoolFor(const std::string& name, const std::string& nickname = {}) {
  const auto logo = overhaul::TeamLogo(name, nickname);
  if (logo.empty()) return nullptr;
  const auto asset = std::strtoul(logo.stem().string().c_str(), nullptr, 10);
  for (const auto& school : LeagueSchools())
    if (school.asset == asset) return &school;
  return nullptr;
}

std::string RankFor(const LeagueSchool* school, const std::string& native = {}) {
  const auto digit = native.find_first_of("123456789");
  if (digit != std::string::npos && std::atoi(native.c_str()+digit) > 0)
    return "#" + native.substr(digit);
  if (school)
    if (const auto rank = expansion::PollRank(school->index)) return "#" + std::to_string(rank);
  return "NR";
}
std::atomic<uint32_t> g_stadium_intro_root{0};
std::atomic<uint32_t> g_mvp_transition_root{0};
std::atomic<uint32_t> g_pitcher_root{0};
std::atomic<uint32_t> g_batter_root{0};
std::array<std::atomic<uint32_t>,2> g_simulation_roots{};
std::optional<game::DynastyCalendarCard> g_calendar_snapshot;
uint32_t g_calendar_snapshot_root=0;
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
int DynastyHomeSelectedRow() { return g_dynasty_home_row.load(); }
void WorkshopMovie(X86Context& ctx);
void EndStartupLoading();
void PopupOpened(uint32_t scene) {
  g_editor_transition_until=0;
  BindPauseMenuTheme("POPUP",scene);
  g_owned_popup=scene;g_submenu_popup=scene;g_popup_state_known=true;g_popup_open=true;
  SetSubmenuPopupVisible(true);
  if(TraceStartup() || dev::Env("MVP06_UI_SCREEN_AUDIT"))LOGR("modal ownership: opened %08X",scene);
}
void PopupClosed(uint32_t scene) {
  if(scene==g_owned_popup.load()) {g_popup_open=false;SetSubmenuPopupVisible(false);
    if(TraceStartup() || dev::Env("MVP06_UI_SCREEN_AUDIT"))LOGR("modal ownership: closed %08X",scene);}
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
std::array<std::atomic<uint32_t>,4> g_stat_table_roots{};
std::atomic<uint32_t> g_stat_summary_root{0},g_stat_face_root{0},g_stat_playerbar_root{0};
std::atomic<uint32_t> g_stadium_select_root{0};
void StartupNativeScene(const char *name, uint32_t scene) {
  game::NotePitchClockScene(name);
  if(!std::strcmp(name,"MAINMENU"))g_tutorial_movie=~0u;
  if(dev::Env("MVP06_UI_SCREEN_AUDIT") && !std::strcmp(name,"DEMOMOVIE"))
    LOGR("tutorial scene: root=%08X vtable=%08X init=%08X",scene,kernel::R32(scene),kernel::R32(kernel::R32(scene)+8));
  if(!std::strcmp(name,"MAINMENU"))g_setup_mode=0;
  if(!std::strcmp(name,"STADIUM") || kernel::ReadCString(scene+0x10,16)=="STADIUM")g_stadium_select_root=scene;
  // Scenario setup replaces the pregame screens rather than covering them.
  // Release those cached presentation roots before the heap is reused.
  if(!std::strcmp(name,"SMARTOPTIONS") || !std::strcmp(name,"MAINMENU") || !std::strcmp(name,"TEAMSELECT")){
    g_starting_lineup_root=0;g_starting_pitcher_root=0;g_final_play_sim_root=0;
  }
  if(!std::strcmp(name,"BALLPARKHUD"))g_ballpark_session=true;
  if(!std::strcmp(name,"LOADINGOUT") ||
     (!std::strcmp(name,"LOADOUT") && g_ballpark_session.load()))g_ballpark_loading_root=scene;
  if(!std::strcmp(name,"SAVESYS") || !std::strcmp(name,"MAINMENU") || !std::strcmp(name,"TEAMSELECT")){
    g_ballpark_loading_root=0;
    if(std::strcmp(name,"SAVESYS"))g_ballpark_session=false;
  }
  if(!std::strcmp(name,"AWARDWATCH"))g_award_watch_root=scene;
  if(!std::strcmp(name,"RECRUITPROFILE")||!std::strcmp(name,"RECRUITACTIONS")||!std::strcmp(name,"RECRUITOFFER"))++g_recruit_portrait_epoch;
  if(!std::strcmp(name,"FEMAILFREQUENCY"))g_mail_filters_root=scene;
  static constexpr const char* offseason[]={"FRETIREMENT","FPLAYERPROGRESS","FINCOMING","FPROMOTEDEMOTE"};
  for(unsigned i=0;i<4;++i)if(!std::strcmp(name,offseason[i])){g_offseason_roots[i]=scene;++g_recruit_portrait_epoch;}
  if(!std::strcmp(name,"FHIRESTAFF"))g_hire_staff_root=scene;
  if(!std::strcmp(name,"SERIESSUMMARY")||!std::strcmp(name,"DAYSGAMES"))g_series_summary_root=scene;
  if(!std::strcmp(name,"TLINESCORES"))g_series_scores_root=scene;
  if(!std::strcmp(name,"LINESCOREPOPUP")){
   // The native factory uses LINESCOREPOPUP for both its single-game popup
   // and the separate series-summary FEL. Classify the constructed scene.
   const auto actual=kernel::ReadCString(scene+0x10,16);
   if(actual=="SERIESSUMMARY")g_series_summary_root=scene;
   else if(actual=="TLINESCORES")g_series_scores_root=scene;
   else g_linescore_popup_root=scene;
   if(TraceStartup())LOGR("postseason scene: factory=%s actual=%s",name,actual.c_str());
  }
  if(!std::strcmp(name,"PLAYOFFTREE"))g_playoff_root=scene;
  if(!std::strcmp(name,"RECRUITACTIONS"))g_recruit_actions_root=scene;
  if(!std::strcmp(name,"RECRUITOFFER"))g_recruit_offer_root=scene;
  if(!std::strcmp(name,"FEMAIL"))g_mail_root=scene;
  if(!std::strcmp(name,"FEMAILITEM"))g_mail_reader_root=scene;
  const char* statsNames[]={"PLAYERSTATS","LEADERS","CAREERSTATS","INDCAREERSTATS"};
  for(int i=0;i<4;++i)if(!std::strcmp(name,statsNames[i]))g_stat_table_roots[i]=scene;
  if(!std::strcmp(name,"PLAYERINFOBAR"))g_stat_playerbar_root=scene;
  if(!std::strcmp(name,"TBPLAYERINFO"))g_stat_face_root=scene;
  if(!std::strcmp(name,"BATPITCHSTATS"))g_stat_summary_root=scene;
  if(!std::strcmp(name,"FPLAYERSTATUS"))g_roster_status_root=scene;
  if(!std::strcmp(name,"CONFLINEUP2"))g_roster_editor_root=scene;
  if(!std::strcmp(name,"BATPITCHINFO"))g_batpitch_info_root=scene;
  if(!std::strcmp(name,"POSRANKING"))g_pos_rank_root=scene;
  if(!std::strcmp(name,"FTEAMGOALS"))g_team_goals_root=scene;
  if(!std::strcmp(name,"DEPTHCHART"))g_depth_chart_root=scene;
  if(!std::strcmp(name,"EDITPLAYER")){g_edit_list_root=scene;g_editor_transition_until=0;}
  if(!std::strcmp(name,"TRADEPLAYERS"))g_trade_root=scene;
  {const auto actual=scene>=0x80000000u && scene<0x90000000u && mem::IsCommitted(scene+0xA0)?kernel::ReadCString(scene+0x10,16):std::string();
   const char* names[]={"THUB","TROSTERMENU","FOPTIONS","TELIMSETUP","CBPMENU","INGAMEREPLAY","BALLPARKHUD"};for(int i=0;i<7;++i)if(!std::strcmp(name,names[i]) || actual==names[i])g_utility_roots[i]=scene;}
  if(!std::strcmp(name,"TSELECTSCHOOLS"))g_utility_roots[3]=scene;
  if(!std::strcmp(name,"FMGRDECISION"))g_utility_roots[0]=scene;
  if(!std::strcmp(name,"CREATEAPLAYER"))g_create_editor_root=scene;
  if(!std::strcmp(name,"TKEYBOARD"))g_editor_keyboard_root=scene;
  if(!std::strcmp(name,"CPMENU")){g_create_edit_root=scene;g_editor_transition_until=0;}
  if(!std::strcmp(name,"FGENOPTIONS"))g_dynasty_options_root=scene;
  if(!std::strcmp(name,"FOPTIONS")){g_team_manage_root=scene;g_team_manage_presented=false;g_team_manage_grid=0;g_team_manage_target=-1;}
  if(!std::strcmp(name,"STATSMENU"))g_statistics_menu_root=scene;
  if(!std::strcmp(name,"FTOP25RANK"))g_top25_root=scene;
  if(!std::strcmp(name,"STANDINGS"))g_standings_root=scene;
  if(!std::strcmp(name,"INFOMENU"))g_stats_landing_root=scene;
  if(!std::strcmp(name,"FEARBAR"))g_dynasty_rightbar_root=scene;
  if(!std::strcmp(name,"RECRUITPROFILE"))g_recruit_profile_root=scene;
  if (!std::strcmp(name,"TOP100")) g_recruit_list_root=scene;
  if (!std::strcmp(name,"TOPTITLE")) g_recruit_title_root=scene;
  if (!std::strcmp(name,"FWATCHLIST")) g_recruiting_root=scene;
  if (!std::strcmp(name,"BOXSCORES")) g_box_score_root=scene;
  if (!std::strcmp(name,"FRESULTS")) g_final_score_root=scene;
  if (!std::strcmp(name,"FSIMPERINNING")) g_sim_game_root=scene;
  if (!std::strcmp(name,"FSIMPERATBAT")) g_coach_root=scene;
  if (!std::strcmp(name,"FPLAYORSIM")) g_final_play_sim_root=scene;
  if (!std::strcmp(name,"STARTINGLINEUP")) g_starting_lineup_root=scene;
  if (!std::strcmp(name,"STARTINGPITCHER")) {
    g_starting_pitcher_root=scene;
  }
  if (!std::strcmp(name,"FPLAYORSIMMULTI")) {
    g_play_sim_root=scene;
    if (overhaul::Active() && kernel::mods::IsFile(overhaul::Root()/"play-sim-atlas.png"))
      SuppressNativeUiScene(scene);
  }
  if (!std::strcmp(name,"SIMPROGRESS") || !std::strcmp(name,"NOTIFIEDSIMPROG")) {
    g_simulation_roots[std::strcmp(name,"SIMPROGRESS") ? 1 : 0]=scene;
    if (overhaul::Active() && kernel::mods::IsFile(overhaul::Root()/"simulation-progress-atlas.png"))
      SuppressNativeUiScene(scene);
  }
  BindPauseMenuTheme(name,scene);
  if (!std::strcmp(name,"CALENDAR")) {
    g_calendar_game_root = 0;
    g_calendar_root = scene;
  }
  if (!std::strcmp(name,"GAMECALENDAR")) g_calendar_game_root = scene;
  if (!std::strcmp(name,"NAVBAR")) g_dynasty_navbar_root=scene;
  if (!std::strcmp(name,"FMGRDECISION") || !std::strcmp(name,"DYNASTYHOME")) {
    g_dynasty_home_row = 0;g_dashboard_section=0;
    g_dynasty_home_root = scene;
  }
  if (!std::strcmp(name, "FSELECTSCHOOL") || !std::strcmp(name, "SELECTSCHOOL")) {
    g_dynasty_school_root = scene;g_picker_expected=0;g_picker_displayed=0;g_picker_ready_after=0;
  }
  if (!std::strcmp(name,"LOADOUT")) g_loadout_root = scene;
  if (!std::strcmp(name,"START") || !std::strcmp(name,"MAINMENU") ||
      !std::strcmp(name,"LOADING1") || !std::strcmp(name,"TEAMSMATCHUP") ||
      !std::strcmp(name,"HUDRIGHT") || !std::strcmp(name,"TRANNCAA"))
    g_loadout_root = 0;
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
    if(name=="REGGAME" || name=="PLAYOFF")regular |= shown[level];
    if(name=="PRESSBUTTON")ready=shown[level];
    int side=-1;bool inRegular=false;
    for(auto l=depth+1;l<level;++l){if(ancestors[l]=="AWAY" || ancestors[l]=="AWAYP")side=0;if(ancestors[l]=="HOME" || ancestors[l]=="HOMEP")side=1;if(ancestors[l]=="REGGAME" || ancestors[l]=="PLAYOFF")inRegular=true;}
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
bool DrawLoadout() {
  const auto valid = [](uint32_t p) {
    return p >= 0x80000000u && p < 0x90000000u && mem::IsCommitted(p + 0x50);
  };
  const auto root = g_loadout_root.load();
  if (!valid(root) || kernel::ReadCString(root + 0x10, 16) != "LOADOUT") return false;
  const auto depth = kernel::R32(root + 0x24);
  if (depth >= 127) return false;

  std::array<MatchupSide,2> teams;
  LoadoutScore score;
  std::array<bool,128> shown{}, inScoreboard{}, inNames{};
  std::array<int,128> rows{}, columns{};
  rows.fill(-1);
  columns.fill(-1);
  shown[depth] = true;
  for (auto node = kernel::R32(root + 8), count = 0u;
       count < 4096 && valid(node) && kernel::R32(node + 0x24) > depth;
       node = kernel::R32(node + 8), ++count) {
    const auto level = kernel::R32(node + 0x24);
    if (level >= 128) continue;
    const auto name = kernel::ReadCString(node + 0x10, 16);
    shown[level] = shown[level - 1] && (kernel::R16(node + 0x28) & 1);
    inNames[level] = inNames[level - 1] || name == "NOPITCHERINFO";
    inScoreboard[level] = inScoreboard[level - 1] || name == "SCOREBOARD";
    rows[level] = rows[level - 1];
    columns[level] = columns[level - 1];
    if (!shown[level]) continue;
    if (inNames[level] && (name == "AWAY" || name == "HOME"))
      teams[name == "HOME"].name = NativeUiText(node);
    if (!inScoreboard[level]) continue;
    int column, row;
    if (std::sscanf(name.c_str(), "X%dY%d", &column, &row) == 2) {
      rows[level] = row;
      columns[level] = column;
    }
    row = rows[level];
    column = columns[level];
    if (name == "TEXT" && row >= 1 && row <= 2 && column >= 0 && column < 13)
      score[row - 1][column] = NativeUiText(node);
  }
  if (teams[0].name.empty() || teams[1].name.empty()) return false;

  const auto matchup = SnapshotMatchupLoading();
  for (auto& team : teams) {
    for (auto& c : team.name) c = char(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& previous : matchup)
      if (team.name == previous.name) {
        team.rank = previous.rank;
        team.nickname = previous.nickname;
        break;
      }
    if (team.rank.empty() || team.rank == "0") team.rank = RankFor(SchoolFor(team.name, team.nickname));
  }
  SuppressNativeUiScene(root);
  static uint32_t previousRoot = 0;
  static auto began = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (previousRoot != root) {
    previousRoot = root;
    began = now;
    if (TraceStartup()) {
      LOGR("loadout: %s #%s at %s #%s", teams[0].name.c_str(), teams[0].rank.c_str(),
           teams[1].name.c_str(), teams[1].rank.c_str());
      gpu::g_capture = true;
    }
  }
  DrawLoadoutCard(teams, score, std::chrono::duration<double>(now - began).count());
  return true;
}

namespace {
struct DynastyFixture {
  int day;
  unsigned time, opponent;
  bool home;
  std::string result, stage;
};

std::chrono::year_month_day DynastyDate(int day) {
  using namespace std::chrono;
  return year_month_day{sys_days{year{2028}/1/1} + days{day-29221}};
}

void ReadDynastySchedule(DynastyHomeCard& card, const LeagueSchool& school,
                         std::vector<DynastyFixture>* calendar = nullptr) {
  const auto manager = kernel::R32(0x6dc59c);
  const auto collection = kernel::R32(0x6dc5a0);
  if (!manager || !collection || !mem::IsCommitted(manager + 12 + 4*phase9::ScheduleSlots()) ||
      !mem::IsCommitted(collection + 19)) return;
  const auto count = kernel::R32(collection + 12), ids = kernel::R32(collection + 16);
  if (!ids || !count || count > 512 || !mem::IsCommitted(ids) ||
      !mem::IsCommitted(ids + count*4 - 1)) return;
  unsigned user = count;
  for (unsigned i=0; i<count; ++i)
    if (kernel::R32(ids + i*4) == school.index) { user = i; break; }
  if (user == count) return;


  std::vector<DynastyFixture> upcoming;
  std::array<unsigned,4> wins{}, losses{};
  bool found = false;
  // Conference tournaments and NCAA stages use the same native game rows,
  // in additional schedule slots. Read all owners, once per table pointer.
  std::unordered_set<uint32_t> visited;
  for (unsigned slot=0; slot<phase9::ScheduleSlots(); ++slot) {
    const auto table = kernel::R32(manager + 12 + slot*4);
    if (!table || !mem::IsCommitted(table+43) || !visited.insert(table).second) continue;
    const bool conferenceRegular=slot==phase9::RegularSlot(school.conference);
    const unsigned rows = kernel::R32(table+36), data = kernel::R32(table+40);
    if (!data || !rows || rows > 40000 || !mem::IsCommitted(data) ||
        !mem::IsCommitted(data + rows*20-1)) continue;
    for (unsigned i=0; i<rows; ++i) {
      const auto row = data + i*20;
      const auto home = expansion::SlotAt(kernel::g_base,row+4);
      const auto away = expansion::SlotAt(kernel::g_base,row+12);
      if (home >= count || away >= count || home==away || (home != user && away != user)) continue;
      found = true;
      const bool atHome = home == user;
      const auto word = kernel::R32(row);
      const unsigned status = (word >> 20) & 15;
      // 2 is an unused/cancelled if-needed game, not an upcoming fixture.
      if(status==2 || status>3)continue;
      if (calendar) {
        DynastyFixture game{int(kernel::R32(table+20)) + int(word&511), (word>>9)&2047,
                            kernel::R32(ids+4*(atHome ? away : home)),atHome,{}};
        if (status == 3) {
          const auto own = kernel::g_base[row+(atHome ? 5 : 13)];
          const auto other = kernel::g_base[row+(atHome ? 13 : 5)];
          game.result = std::string(own>other ? "W " : own<other ? "L " : "T ") +
                        std::to_string(own) + "-" + std::to_string(other);
        }
        if(slot>=27 && slot<=36)game.stage="NCAA TOURNAMENT";
        else if(slot!=15 && slot!=phase9::RegularSlot(school.conference))game.stage="CONFERENCE TOURNAMENT";
        calendar->push_back(std::move(game));
      }
      if (status < 3) {
        upcoming.push_back({int(kernel::R32(table+20)) + int(word&511), (word>>9)&2047,
                            kernel::R32(ids + 4*(atHome ? away : home)), atHome});
      } else if (status == 3) {
        const auto h = kernel::g_base[row+5], a = kernel::g_base[row+13];
        if (h == a) continue;
        auto& result = ((h > a) == atHome) ? wins : losses;
        ++result[0];
        if (conferenceRegular) ++result[1];
        ++result[atHome ? 2 : 3];
      }
    }
  }
  if (!found) return;
  for (size_t i=0; i<card.records.size(); ++i)
    card.records[i] = std::to_string(wins[i]) + "-" + std::to_string(losses[i]);
  std::stable_sort(upcoming.begin(),upcoming.end(),[](const DynastyFixture& a,const DynastyFixture& b) {
    return a.day != b.day ? a.day < b.day : a.time < b.time;
  });
  using namespace std::chrono;
  constexpr const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  for (const auto& game : upcoming) {
    const auto& schools = LeagueSchools();
    const auto opponent = std::find_if(schools.begin(),schools.end(),[&](const LeagueSchool& team) {
      return team.index == game.opponent;
    });
    if (opponent == schools.end()) continue;
    const auto date = DynastyDate(game.day);
    card.upcoming.push_back({std::string(months[unsigned(date.month())-1]) + " " +
                             std::to_string(unsigned(date.day())),opponent->name,opponent->nickname,game.home});
    if (card.upcoming.size() == 5) break;
  }
}
}

bool DrawNativeSimulationProgress(bool calendarDrawn) {
  uint32_t root=0;
  const auto valid=[](uint32_t node) {
    return node>=0x80000000u && node<0x90000000u && mem::IsCommitted(node+0x74);
  };
  for (const auto& entry : g_simulation_roots) {
    const auto candidate=entry.load();
    if (!valid(candidate) || !(kernel::R16(candidate+0x28)&1)) continue;
    const auto name=kernel::ReadCString(candidate+0x10,16);
    if (name=="SIMPROGRESS" || name=="NOTIFIEDSIMPROG") {root=candidate;break;}
  }
  static bool displayed=false;
  if (!root) {
    if (displayed && TraceStartup()) LOGR("simulation progress: hidden");
    displayed=false;
    return false;
  }
  const auto depth=kernel::R32(root+0x24);
  if (depth>=127) return false;
  std::array<bool,128> visible{};
  visible[depth]=(kernel::R16(root+0x28)&1)!=0;
  std::string caption;
  uint32_t meter=0;
  bool canStop=false;
  for (auto node=kernel::R32(root+8),count=0u;
       count<4096 && valid(node) && kernel::R32(node+0x24)>depth;
       node=kernel::R32(node+8),++count) {
    const auto level=kernel::R32(node+0x24);
    if (level>=128) continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    visible[level]=visible[level-1] && (kernel::R16(node+0x28)&1);
    if (!visible[level]) continue;
    if (name=="CAPTION" || name=="DESTNDATE") caption=NativeUiText(node);
    if (name=="METERBAR") meter=node;
    if (name=="CANCEL") canStop=true;
  }
  if (caption.empty() || !meter) return false;
  const float progress=std::clamp(kernel::R16(meter+0x2E)/227.f,0.f,1.f);
  // Simulation can temporarily hide the native calendar while advancing its data.
  // Keep the last stable presentation underneath the modal until it finishes.
  const auto calendarRoot=g_calendar_root.load();
  if (!calendarDrawn && g_calendar_snapshot && calendarRoot==g_calendar_snapshot_root &&
      valid(calendarRoot) && (kernel::R16(calendarRoot+0x28)&1))
    DrawDynastyCalendar(*g_calendar_snapshot);
  if (!DrawSimulationProgress(caption,progress,canStop)) return false;
  SuppressNativeUiScene(root);
  if (TraceStartup()) {
    static std::string last;
    const auto key=caption+std::to_string(int(progress*4));
    if (!displayed || key!=last) {
      last=key;
      LOGR("simulation progress: %s progress=%.3f stoppable=%d",caption.c_str(),progress,int(canStop));
      gpu::g_capture=true;
    }
  }
  displayed=true;
  return true;
}

bool DrawNativeStartingPitcher() {
  if (!overhaul::Active()) return false;
  const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
  const auto root=g_starting_pitcher_root.load();
  if (!FeScene(root,"STARTINGPITCHER")) return false;
  const auto depth=kernel::R32(root+0x24);
  if(depth>=127) return false;
  std::array<StartingPitcherSide,2> sides;
  std::array<bool,128> shown{};
  std::array<int,128> side{},row{},column{},kind{};
  side.fill(-1);row.fill(-1);column.fill(-1);kind.fill(0);shown[depth]=true;
  std::array<uint32_t,2> objects{};
  for(auto node=kernel::R32(root+8),n=0u;n<4096 && valid(node) && kernel::R32(node+0x24)>depth;
      node=kernel::R32(node+8),++n) {
    const auto level=kernel::R32(node+0x24);if(level>=128)continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    shown[level]=shown[level-1] && (kernel::R16(node+0x28)&1);
    side[level]=side[level-1];row[level]=row[level-1];column[level]=column[level-1];kind[level]=kind[level-1];
    if(name=="AWAYSIDE")side[level]=0;
    if(name=="HOMESIDE")side[level]=1;
    const int index=side[level];if(index<0)continue;
    auto& out=sides[index];
    if(name=="SPGRID") {
      kind[level]=1;
      out.first=int(kernel::R32(node+0x74));
      out.selected=int(kernel::R32(node+0x6C))-out.first;
      out.active=(kernel::R16(node+0x28)&2)!=0;
    }
    if(name=="PITCHERINFOGRID")kind[level]=2;
    int x=-1,y=-1;
    if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){column[level]=x;row[level]=y;}
    if(!shown[level])continue;
    if(name=="CITY")out.team.name=NativeUiText(node);
    if(name=="AWAYREADY" || name=="HOMEREADY")out.ready=true;
    if(name=="SVALUE")out.stamina=NativeUiText(node);
    if(kind[level]==1 && name=="TB1" && row[level]>=0 && row[level]<6 && column[level]>=0 && column[level]<4)
      out.rotation[row[level]][column[level]]=NativeUiText(node);
    if(kind[level]==2 && row[level]>=0 && row[level]<5) {
      if(name=="TL1")out.pitches[row[level]]=NativeUiText(node);
      if(name=="NUMBER")out.ratings[row[level]]=NativeUiText(node);
    }
    if(name=="FACE") {
      objects[index]=kernel::R32(node+0x80);
      for(int i=0;i<4;++i){auto bits=kernel::R32(node+0x84+4*i);std::memcpy(&out.uv[i],&bits,4);}
    }
  }
  if(sides[0].team.name.empty() || sides[1].team.name.empty()) return false;
  struct PortraitCache {
    uint32_t root=0,object=0,texture=0;
    std::string player;
    std::chrono::steady_clock::time_point readyAt{};
  };
  static std::array<PortraitCache,2> cache;
  for(int i=0;i<2;++i) {
    auto& out=sides[i];const auto* school=SchoolFor(out.team.name);
    if(school) {out.team.name=school->name;out.team.nickname=school->nickname;out.team.rank=RankFor(school);out.color=school->color;}
    const auto selected=std::clamp(out.selected,0,5);
    auto& saved=cache[i];const auto& player=out.rotation[selected][1];
    if(saved.root!=root || saved.object!=objects[i] || saved.player!=player ||
       (saved.texture && (!valid(saved.texture) || (kernel::R32(saved.texture)&0x70000)!=0x40000)))
      saved={root,objects[i],0,player,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)};
    // A newly bound FACE initially contains the common placeholder. The native
    // render pass replaces it on the next frame; do not show that placeholder.
    if(!saved.texture && std::chrono::steady_clock::now()>=saved.readyAt && valid(objects[i])) {
      std::vector<std::pair<uint32_t,int>> queue{{objects[i],0}};std::unordered_set<uint32_t> visited;
      for(size_t n=0;n<queue.size() && n<256;++n) {
        const auto [object,level]=queue[n];if(!valid(object) || !visited.insert(object).second)continue;
        gpu::TextureDesc desc;
        if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 &&
           !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 &&
           desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)) {
          saved.texture=object;break;
        }
        if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto next=kernel::R32(object+offset);if(valid(next) && !visited.count(next))queue.emplace_back(next,level+1);}
      }
    }
    out.portrait=saved.texture;
  }
  if(!DrawStartingPitcher(sides))return false;
  // This screen's native render path also produces the live portrait textures.
  // Let it run beneath the opaque replacement rather than bypassing that work.
  if(TraceStartup()) {
    static std::string last;
    std::string key;
    for(const auto& s:sides)key+=s.team.name+std::to_string(s.selected)+std::to_string(s.first)+s.stamina+std::to_string(s.portrait)+std::to_string(s.ready);
    if(key!=last){last=key;for(int i=0;i<2;++i){const auto& s=sides[i];LOGR("starting pitcher: side=%d team=%s row=%d first=%d active=%d ready=%d player=%s stamina=%s portrait=%08X",i,s.team.name.c_str(),s.selected,s.first,int(s.active),int(s.ready),s.rotation[std::clamp(s.selected,0,5)][1].c_str(),s.stamina.c_str(),s.portrait);}gpu::g_capture=true;}
  }
  return true;
}

bool DrawNativeStartingLineup() {
  if (!overhaul::Active()) return false;
  const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
  const auto root=g_starting_lineup_root.load();
  if (!FeScene(root,"STARTINGLINEUP")) return false;
  const auto depth=kernel::R32(root+0x24);
  if(depth>=127) return false;
  std::array<StartingLineupSide,2> sides;
  std::array<bool,128> shown{};
  std::array<int,128> side{},row{},column{},kind{};
  side.fill(-1);row.fill(-1);column.fill(-1);kind.fill(0);shown[depth]=true;
  std::array<uint32_t,2> objects{};
  for(auto node=kernel::R32(root+8),n=0u;n<4096 && valid(node) && kernel::R32(node+0x24)>depth;
      node=kernel::R32(node+8),++n) {
    const auto level=kernel::R32(node+0x24);if(level>=128)continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    shown[level]=shown[level-1] && (kernel::R16(node+0x28)&1);
    side[level]=side[level-1];row[level]=row[level-1];column[level]=column[level-1];kind[level]=kind[level-1];
    if(name=="AWAYSIDE")side[level]=0;
    if(name=="HOMESIDE")side[level]=1;
    const int index=side[level];if(index<0)continue;
    auto& out=sides[index];
    if(name=="LINEUP") {
      kind[level]=1;
      out.first=int(kernel::R32(node+0x74));
      out.selected=int(kernel::R32(node+0x6C))-out.first;
      out.active=(kernel::R16(node+0x28)&2)!=0;
    }
    if(name=="BATTERINFOGRID")kind[level]=2;
    int x=-1,y=-1;
    if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){column[level]=x;row[level]=y;}
    if(!shown[level])continue;
    if(name=="CITY")out.team.name=NativeUiText(node);
    if(name=="AWAYREADY" || name=="HOMEREADY")out.ready=true;
    if(name=="DONE")out.done=(kernel::R16(node+0x28)&2)!=0;
    if(name=="RHP" || name=="LHP")out.versus=NativeUiText(node);
    if(kind[level]==1 && (name=="TB1" || name=="NUMBER") && row[level]>=0 && row[level]<8 && column[level]>=0 && column[level]<6)
      out.order[row[level]][column[level]]=NativeUiText(node);
    if(kind[level]==2 && name=="NUMBER" && row[level]>=0 && row[level]<3)out.ratings[row[level]]=NativeUiText(node);
    if(name=="WARNING")return false; // Preserve native validation dialogs.
    // The native sprite name at +4E reflects the selected hot-zone item.
    // The authored entries are sqbl, sqga, sqre (cold, neutral, hot).
    static const std::array<std::string,9> zoneNames{"UL","UM","UR","CL","CM","CR","LL","LM","LR"};
    for(int z=0;z<9;++z)if(name==zoneNames[z] && kernel::R32(node+0xA8)==3){const auto sprite=kernel::ReadCString(node+0x4E,5);out.zones[z]=sprite=="sqbl"?0:sprite=="sqre"?2:1;}
    if(name=="FACE") {
      objects[index]=kernel::R32(node+0x80);
      for(int i=0;i<4;++i){auto bits=kernel::R32(node+0x84+4*i);std::memcpy(&out.uv[i],&bits,4);}
    }
  }
  if(sides[0].team.name.empty() || sides[1].team.name.empty()) return false;
  struct PortraitCache {
    uint32_t root=0,object=0,texture=0;
    std::string player;
    std::chrono::steady_clock::time_point readyAt{};
  };
  static std::array<PortraitCache,2> cache;
  for(int i=0;i<2;++i) {
    auto& out=sides[i];const auto* school=SchoolFor(out.team.name);
    if(school) {out.team.name=school->name;out.team.nickname=school->nickname;out.team.rank=RankFor(school);out.color=school->color;}
    const auto selected=std::clamp(out.selected,0,7);
    auto& saved=cache[i];const auto& player=out.order[selected][2];
    if(saved.root!=root || saved.object!=objects[i] || saved.player!=player ||
       (saved.texture && (!valid(saved.texture) || (kernel::R32(saved.texture)&0x70000)!=0x40000)))
      saved={root,objects[i],0,player,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)};
    // A newly bound FACE initially contains the common placeholder. The native
    // render pass replaces it on the next frame; do not show that placeholder.
    if(!saved.texture && std::chrono::steady_clock::now()>=saved.readyAt && valid(objects[i])) {
      std::vector<std::pair<uint32_t,int>> queue{{objects[i],0}};std::unordered_set<uint32_t> visited;
      for(size_t n=0;n<queue.size() && n<256;++n) {
        const auto [object,level]=queue[n];if(!valid(object) || !visited.insert(object).second)continue;
        gpu::TextureDesc desc;
        if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 &&
           !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 &&
           desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)) {
          saved.texture=object;break;
        }
        if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto next=kernel::R32(object+offset);if(valid(next) && !visited.count(next))queue.emplace_back(next,level+1);}
      }
    }
    out.portrait=saved.texture;
  }
  for(int i=0;i<2;++i)g_pregame_teams[i]=sides[i].team;
  if(!DrawStartingLineup(sides))return false;
  // This screen's native render path also produces the live portrait textures.
  // Let it run beneath the opaque replacement rather than bypassing that work.
  if(TraceStartup()) {
    static std::string last;
    std::string key;
    for(const auto& s:sides)key+=s.team.name+std::to_string(s.selected)+std::to_string(s.first)+s.versus+std::to_string(s.portrait)+std::to_string(s.ready);
    if(key!=last){last=key;for(int i=0;i<2;++i){const auto& s=sides[i];LOGR("starting lineup: side=%d team=%s row=%d first=%d active=%d ready=%d player=%s versus=%s portrait=%08X",i,s.team.name.c_str(),s.selected,s.first,int(s.active),int(s.ready),s.order[std::clamp(s.selected,0,7)][2].c_str(),s.versus.c_str(),s.portrait);}gpu::g_capture=true;}
  }
  return true;
}

bool DrawNativeFinalPlaySim() {
  if(!overhaul::Active())return false;
  const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
  const auto root=g_final_play_sim_root.load();
  if(!FeScene(root,"FPLAYORSIM"))return false;
  const auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
  FinalPlaySimCard card;auto& sides=card.sides;
  for(int i=0;i<2;++i)sides[i].team=g_pregame_teams[i];
  std::array<bool,128> shown{};shown[depth]=true;
  std::array<int,128> side{},kind{};side.fill(-1);
  std::array<uint32_t,2> objects{};
  for(auto node=kernel::R32(root+8),n=0u;n<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
    const auto level=kernel::R32(node+0x24);if(level>=128)continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    shown[level]=shown[level-1] && (kernel::R16(node+0x28)&1);
    side[level]=side[level-1];kind[level]=kind[level-1];
    if(name=="HOME" || name=="HOMETEXT" || name=="HOMESCORE" || name=="HOMEMETER2" || name=="PORTRAITRIGHT")side[level]=1;
    if(name=="AWAY" || name=="AWAYTEXT" || name=="AWAYSCORE" || name=="AWAYMETER2" || name=="PORTRAITLEFT")side[level]=0;
    if(name=="HOMETEXT" || name=="AWAYTEXT")kind[level]=1;
    if(name=="HOMESCORE" || name=="AWAYSCORE")kind[level]=2;
    if(name=="HOMEMETER2" || name=="AWAYMETER2")kind[level]=3;
    if(name=="CELL")card.selected=std::clamp(int(kernel::R32(node+0x70)),0,2);
    if(!shown[level])continue;
    if(name=="PLAY" || name=="MANAGE" || name=="SIM"){
      auto value=NativeUiText(node);if(!value.empty())card.actions[name=="PLAY"?0:name=="MANAGE"?1:2]=value;
    }
    const int index=side[level];if(index<0)continue;auto& out=sides[index];
    const auto value=(name=="PLAYERNAME" || name=="ERAVAL" || name=="NUMBER")?NativeUiText(node):std::string{};
    if(kind[level]==1){if(name=="PLAYERNAME")card.players[index]=value;if(name=="ERAVAL")card.era[index]=value;if(name=="NUMBER")card.wins[index]=value;}
    if(kind[level]==2 && name=="NUMBER")card.records[index]=value;
    if(kind[level]==3 && name=="METERBLOCK")card.stamina[index]=std::clamp(1.f-kernel::R16(node+0x2E)/96.f,0.f,1.f);
    if(name=="STARTERFACE"){
      objects[index]=kernel::R32(node+0x80);
      for(int k=0;k<4;++k){auto bits=kernel::R32(node+0x84+4*k);std::memcpy(&out.uv[k],&bits,4);}
    }

  }
  struct PortraitCache {
    uint32_t root=0,object=0,texture=0;
    std::string player;
    std::chrono::steady_clock::time_point readyAt{};
  };
  static std::array<PortraitCache,2> cache;
  for(int i=0;i<2;++i) {
    auto& out=sides[i];const auto* school=SchoolFor(out.team.name);
    if(school) {out.team.name=school->name;out.team.nickname=school->nickname;out.team.rank=RankFor(school);out.color=school->color;}
    
    auto& saved=cache[i];const auto& player=card.players[i];
    if(saved.root!=root || saved.object!=objects[i] || saved.player!=player ||
       (saved.texture && (!valid(saved.texture) || (kernel::R32(saved.texture)&0x70000)!=0x40000)))
      saved={root,objects[i],0,player,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)};
    // A newly bound FACE initially contains the common placeholder. The native
    // render pass replaces it on the next frame; do not show that placeholder.
    if(!saved.texture && std::chrono::steady_clock::now()>=saved.readyAt && valid(objects[i])) {
      std::vector<std::pair<uint32_t,int>> queue{{objects[i],0}};std::unordered_set<uint32_t> visited;
      for(size_t n=0;n<queue.size() && n<256;++n) {
        const auto [object,level]=queue[n];if(!valid(object) || !visited.insert(object).second)continue;
        gpu::TextureDesc desc;
        if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 &&
           !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 &&
           desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)) {
          saved.texture=object;break;
        }
        if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto next=kernel::R32(object+offset);if(valid(next) && !visited.count(next))queue.emplace_back(next,level+1);}
      }
    }
    out.portrait=saved.texture;
  }

  if(!DrawFinalPlaySim(card))return false;g_action_navigation=2;
  if(TraceStartup()){
    static std::string last;const auto key=std::to_string(card.selected)+card.players[0]+card.players[1]+std::to_string(sides[0].portrait)+std::to_string(sides[1].portrait);
    if(last!=key){last=key;LOGR("final play sim: selected=%d away=%s home=%s pitcher=%s/%s era=%s/%s record=%s/%s stamina=%.3f/%.3f",card.selected,sides[0].team.name.c_str(),sides[1].team.name.c_str(),card.players[0].c_str(),card.players[1].c_str(),card.era[0].c_str(),card.era[1].c_str(),card.wins[0].c_str(),card.wins[1].c_str(),card.stamina[0],card.stamina[1]);gpu::g_capture=true;}
  }
  return true;
}

bool DrawNativeCoach() {
  if(!overhaul::Active())return false;
  const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
  const auto root=g_coach_root.load();
  if(!FeVisibleScene(root,"FSIMPERATBAT") ||
     (!(kernel::R16(root+0x28)&2) && !FeVisibleScene(g_stat_playerbar_root.load(),"PLAYERINFOBAR")))return false;
  const auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
  CoachCard card;card.teams=g_pregame_teams;
  std::array<bool,128> shown{};shown[depth]=true;
  std::array<int,128> player{},kind{},row{},column{},base{},score{};
  player.fill(-1);row.fill(-1);column.fill(-1);base.fill(-1);score.fill(-1);
  std::array<uint32_t,2> objects{};
  std::array<std::string,10> commands;std::array<bool,10> enabled{};

  for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
    const auto l=kernel::R32(node+0x24);if(l>=128)continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);
    player[l]=player[l-1];kind[l]=kind[l-1];row[l]=row[l-1];column[l]=column[l-1];base[l]=base[l-1];score[l]=score[l-1];
    if(!shown[l])continue;
    if(name=="SUBPITCHER" && l==depth+1)return false; // Leave native substitution interactions unobstructed.
    if(name=="LEFTPLAYER")player[l]=0;
    if(name=="RIGHTPLAYER")player[l]=1;
    if(name=="BASE0")base[l]=0;if(name=="BASE1")base[l]=1;if(name=="BASE2")base[l]=2;if(name=="BASE3")base[l]=3;
    if(name=="HOMESCORE")score[l]=1;if(name=="AWAYSCORE")score[l]=0;
    if(name=="PITCHLIST" || name=="BATTERLIST" || name=="STEALLIST"){
      kind[l]=1;card.selected=int(kernel::R32(node+0x6C));
    }
    if(name=="BATSTATS" || name=="BATATTRIBS")kind[l]=2;
    if(name=="CELL1" || name=="CELL2")kind[l]=2;
    if(name=="LINESCOREGRID"){kind[l]=3;card.lines=true;}
    int x=-1,y=-1;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){column[l]=x;row[l]=y;if(kind[l]==1 && y>=0 && y<10)enabled[y]=(kernel::R16(node+0x28)&2)!=0;}
    if(kind[l]==1 && row[l]>=0 && row[l]<10){
      const auto text=NativeUiText(node);if(name!="BN" && name!="BP" && name!="BH" && !text.empty()){
        // Only TB widgets have this vtable family; named command nodes are unique.
        static const std::array<const char*,20> names{"PITCHBATTER","PITCHAROUND","WALKBATTER","SUBPITCHER","DEFENSIVESUB","SIMOPTIONS","SWINGAWAY","SACBUNT","BUNTFORHIT","HITANDRUN","RUNANDHIT","STEAL","PINCHHIT","PINCHRUN","STEAL2ND","STEAL3RD","STEALHOME","DOUBLESTEAL","CANCEL","DEFENSIVESUB"};
        for(auto command:names)if(name==command)commands[row[l]]=text;
      }
    }
    const int pi=player[l];
    if(pi>=0){auto& out=card.players[pi];
      if(name=="PITCHING")out.pitching=true;
      if(name=="PLAYERNAME")out.name=NativeUiText(node);
      if((kind[l]==2 || kind[l]==4 || kind[l]==5) && row[l]>=0 && row[l]<6 && column[l]>=0 && column[l]<2){
        if(name=="NUMBER" || name=="TX1" || name=="FOR" || name=="SO" || name=="WALK" || name=="HR" || name=="RBI" || name=="HIT" || name=="SPD" || name=="BNT" || name=="CON" || name=="POW" || name=="BB" || name=="ER" || name=="HITS" || name=="TL1")
          out.stats[row[l]][column[l]]=NativeUiText(node);
      }
      if(name=="FACE"){objects[pi]=kernel::R32(node+0x80);for(int k=0;k<4;++k){auto bits=kernel::R32(node+0x84+4*k);std::memcpy(&out.uv[k],&bits,4);}}
    }
    if(kind[l]==3 && row[l]>=0 && row[l]<3 && column[l]>=0 && column[l]<16 && (name=="TEXT" || name=="TX1"))card.lineScore[row[l]][column[l]]=NativeUiText(node);
    if(base[l]>=0 && name=="PLAYERNAME")card.runners[base[l]]=NativeUiText(node);
    if(score[l]>=0 && name=="SCORE")card.scores[score[l]]=NativeUiText(node);
    if(name=="INNING")card.inning=NativeUiText(node);
    if(name=="OUTPIPS"){auto sprite=kernel::ReadCString(node+0x4E,5);if(sprite.size()==4 && sprite.rfind("out",0)==0)card.outs=std::clamp(sprite[3]-'0',0,3);}
    if(name=="ARROW"){auto sprite=kernel::ReadCString(node+0x4E,5);card.batting=sprite=="gyup"?0:1;}
    if(name=="RESULT")card.result=NativeUiText(node);
    if(name=="PLAYBYPLAYTEXT")card.playByPlay=NativeUiText(node);
    if(name=="PBPTEXTSMALL" && card.result.empty())card.result=NativeUiText(node);
  }
  if(!card.players[0].name.empty())card.batting=card.players[0].pitching?1:0;
  for(int i=0;i<10;++i)if(!commands[i].empty()){card.commands.push_back(commands[i]);card.enabled.push_back(enabled[i]);}
  for(int i=0;i<2;++i)if(const auto* school=SchoolFor(card.teams[i].name)){card.teams[i].name=school->name;card.teams[i].nickname=school->nickname;card.teams[i].rank=RankFor(school);card.colors[i]=school->color;}
  struct PortraitCache {
    uint32_t root=0,object=0,texture=0;
    std::string player;
    std::chrono::steady_clock::time_point readyAt{};
  };
  static std::array<PortraitCache,2> cache;
  for(int i=0;i<2;++i) {
    auto& out=card.players[i];
    auto& saved=cache[i];const auto& player=card.players[i].name;
    if(saved.root!=root || saved.object!=objects[i] || saved.player!=player ||
       (saved.texture && (!valid(saved.texture) || (kernel::R32(saved.texture)&0x70000)!=0x40000)))
      saved={root,objects[i],0,player,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)};
    // A newly bound FACE initially contains the common placeholder. The native
    // render pass replaces it on the next frame; do not show that placeholder.
    if(!saved.texture && std::chrono::steady_clock::now()>=saved.readyAt && valid(objects[i])) {
      std::vector<std::pair<uint32_t,int>> queue{{objects[i],0}};std::unordered_set<uint32_t> visited;
      for(size_t n=0;n<queue.size() && n<256;++n) {
        const auto [object,level]=queue[n];if(!valid(object) || !visited.insert(object).second)continue;
        gpu::TextureDesc desc;
        if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 &&
           !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 &&
           desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)) {
          saved.texture=object;break;
        }
        if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto next=kernel::R32(object+offset);if(valid(next) && !visited.count(next))queue.emplace_back(next,level+1);}
      }
    }
    out.portrait=saved.texture;
  }


  if(!DrawCoach(card))return false;g_action_navigation=3;
  if(TraceStartup()){
    static std::string last;auto key=card.inning+std::to_string(card.outs)+std::to_string(card.selected)+card.scores[0]+card.scores[1]+card.result+card.playByPlay+std::to_string(card.lines);
    for(const auto& p:card.players)key+=p.name+std::to_string(p.portrait);
    if(last!=key){last=key;LOGR("coach: row=%d commands=%zu inning=%s outs=%d batting=%d score=%s/%s players=%s/%s lines=%d result=%s",card.selected,card.commands.size(),card.inning.c_str(),card.outs,card.batting,card.scores[0].c_str(),card.scores[1].c_str(),card.players[0].name.c_str(),card.players[1].name.c_str(),int(card.lines),card.result.c_str());gpu::g_capture=true;}
  }
  return true;
}

bool DrawNativeSimGame() {
  if(!overhaul::Active())return false;
  const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
  const auto root=g_sim_game_root.load();
  if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="FSIMPERINNING")return false;
  const auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
  SimGameCard card;card.teams=g_pregame_teams;
  std::array<bool,128> shown{};shown[depth]=true;
  std::array<int,128> player{},kind{},row{},column{};player.fill(-1);row.fill(-1);column.fill(-1);
  std::array<uint32_t,2> objects{};
  std::array<std::string,3> choices;std::string intervene;
  bool choosing=false;
  for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
    const auto l=kernel::R32(node+0x24);if(l>=128)continue;const auto name=kernel::ReadCString(node+0x10,16);
    shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);
    player[l]=player[l-1];kind[l]=kind[l-1];row[l]=row[l-1];column[l]=column[l-1];
    if(!shown[l])continue;
    if(name=="LEFTPLAYER")player[l]=0;if(name=="RIGHTPLAYER")player[l]=1;
    if(name=="CELL1" || name=="CELL2")kind[l]=1;
    if(name=="BOXSCOREGRID")kind[l]=2;
    if(name=="INTERVENEOPTION"){kind[l]=3;choosing=true;card.selected=std::clamp(int(kernel::R32(node+0x70)),0,2);}
    int x=-1,y=-1;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){column[l]=x;row[l]=y;}
    if(name=="SIMSPEED"){const auto value=NativeUiText(node);if(!value.empty())card.speed=value;}
    if(name=="INTERVENE")intervene=NativeUiText(node);
    if(kind[l]==3 && column[l]>=0 && column[l]<3 && (name=="PLAY" || name=="PERATBAT" || name=="CONTINUE"))choices[column[l]]=NativeUiText(node);
    if(kind[l]==2 && row[l]>=0 && row[l]<3 && column[l]>=0 && column[l]<13 && (name=="TEXT" || name=="TX1"))card.lineScore[row[l]][column[l]]=NativeUiText(node);
    const int pi=player[l];if(pi<0)continue;auto& out=card.players[pi];
    if(name=="PLAYERNAME")out.name=NativeUiText(node);
    if(name=="POSITION")card.positions[pi]=NativeUiText(node);
    if(kind[l]==1 && row[l]>=0 && row[l]<5 && column[l]>=0 && column[l]<2 &&
       (name=="NUMBER" || name=="TX1" || name=="TL1" || name=="BB" || name=="HR" || name=="ER" || name=="SO" || name=="HITS"))out.stats[row[l]][column[l]]=NativeUiText(node);
    if(name=="METERBLOCK")card.stamina[pi]=std::clamp(1.f-kernel::R16(node+0x2E)/100.f,0.f,1.f);
    if(name=="FACE"){objects[pi]=kernel::R32(node+0x80);for(int k=0;k<4;++k){auto bits=kernel::R32(node+0x84+4*k);std::memcpy(&out.uv[k],&bits,4);}}
  }
  if(choosing){for(const auto& value:choices)card.actions.push_back(value);}else if(!intervene.empty())card.actions.push_back(intervene);
  for(int i=0;i<2;++i)if(const auto* school=SchoolFor(card.teams[i].name)){card.teams[i].name=school->name;card.teams[i].nickname=school->nickname;card.teams[i].rank=RankFor(school);card.colors[i]=school->color;}
  struct PortraitCache {
    uint32_t root=0,object=0,texture=0;
    std::string player;
    std::chrono::steady_clock::time_point readyAt{};
  };
  static std::array<PortraitCache,2> cache;
  for(int i=0;i<2;++i) {
    auto& out=card.players[i];
    auto& saved=cache[i];const auto& player=card.players[i].name;
    if(saved.root!=root || saved.object!=objects[i] || saved.player!=player ||
       (saved.texture && (!valid(saved.texture) || (kernel::R32(saved.texture)&0x70000)!=0x40000)))
      saved={root,objects[i],0,player,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)};
    // A newly bound FACE initially contains the common placeholder. The native
    // render pass replaces it on the next frame; do not show that placeholder.
    if(!saved.texture && std::chrono::steady_clock::now()>=saved.readyAt && valid(objects[i])) {
      std::vector<std::pair<uint32_t,int>> queue{{objects[i],0}};std::unordered_set<uint32_t> visited;
      for(size_t n=0;n<queue.size() && n<256;++n) {
        const auto [object,level]=queue[n];if(!valid(object) || !visited.insert(object).second)continue;
        gpu::TextureDesc desc;
        if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 &&
           !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 &&
           desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)) {
          saved.texture=object;break;
        }
        if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto next=kernel::R32(object+offset);if(valid(next) && !visited.count(next))queue.emplace_back(next,level+1);}
      }
    }
    out.portrait=saved.texture;
  }



  if(!DrawSimGame(card))return false;
  if(TraceStartup()){
    static std::string last;auto key=card.speed+std::to_string(card.selected)+std::to_string(card.actions.size());
    for(const auto& line:card.lineScore)for(const auto& value:line)key+=value+"|";
    for(const auto& p:card.players){key+=p.name+std::to_string(p.portrait);for(const auto& stat:p.stats)key+=stat[0]+stat[1];}
    if(last!=key){last=key;LOGR("sim game: speed=%s actions=%zu selected=%d score=%s/%s pitchers=%s/%s stamina=%.3f/%.3f",card.speed.c_str(),card.actions.size(),card.selected,card.lineScore[1][10].c_str(),card.lineScore[2][10].c_str(),card.players[0].name.c_str(),card.players[1].name.c_str(),card.stamina[0],card.stamina[1]);gpu::g_capture=true;}
  }
  return true;
}




bool DrawNativeRecruitList() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 const auto root=g_recruit_list_root.load();if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="TOP100")return false;
 auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 RecruitListCard card;std::array<bool,128> shown{},grid{};shown[depth]=true;std::array<int,128> row{},col{};row.fill(-1);col.fill(-1);
 for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);grid[l]=grid[l-1];row[l]=row[l-1];col[l]=col[l-1];
  if(name=="CELL"){grid[l]=true;card.first=int(kernel::R32(node+0x74));card.selected=int(kernel::R32(node+0x6C))-card.first;}
  int x=-1,y=-1;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){col[l]=x;row[l]=y;}
  if(!shown[l])continue;
  if(name=="RECRUITPOINTS1")card.points=NativeUiText(node);
  if(!grid[l] && name=="TL1")card.filter=NativeUiText(node);
  if(grid[l] && row[l]>=0 && row[l]<10 && col[l]>=0 && col[l]<5 && name!="CELL" && name!="VSCROLL" && !(x>=0 && y>=0)){
   auto v=NativeUiText(node);if(!v.empty())card.rows[row[l]][col[l]]=v;
  }
  if(grid[l] && row[l]>=0 && row[l]<10 && name=="METERBAR")card.interest[row[l]]=std::clamp(kernel::R16(node+0x2E)/107.f,0.f,1.f);
 }
 const auto title=g_recruit_title_root.load();if(valid(title) && (kernel::R16(title+0x28)&1)){
  auto td=kernel::R32(title+0x24);for(auto node=kernel::R32(title+8),n=0u;n<256 && valid(node) && kernel::R32(node+0x24)>td;node=kernel::R32(node+8),++n)if(kernel::ReadCString(node+0x10,16)=="TOP100")card.title=NativeUiText(node);
 }
 if(card.title.empty())card.title="RECRUITING PROSPECTS";
 if(!DrawRecruitList(card))return false;
 if(TraceStartup()){static std::string last;auto key=card.title+card.filter+card.points+std::to_string(card.first)+"/"+std::to_string(card.selected);for(auto& r:card.rows)for(auto& v:r)key+=v;for(auto v:card.interest)key+=std::to_string(v);if(key!=last){last=key;LOGR("recruit list: title=%s filter=%s first=%d selected=%d points=%s",card.title.c_str(),card.filter.c_str(),card.first,card.selected,card.points.c_str());for(int r=0;r<10;++r)LOGR("recruit row %d: %s | %s | %s | %s | %s interest=%.3f",r,card.rows[r][0].c_str(),card.rows[r][1].c_str(),card.rows[r][2].c_str(),card.rows[r][3].c_str(),card.rows[r][4].c_str(),card.interest[r]);gpu::g_capture=true;}}
 return true;
}
bool DrawNativeLeagueTable() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 uint32_t root=0;bool standings=false;
 for(auto candidate:{g_top25_root.load(),g_standings_root.load()})if(valid(candidate) && (kernel::R16(candidate+0x28)&3)==3){
  auto name=kernel::ReadCString(candidate+0x10,16);if(name=="FTOP25RANK" || name=="STANDINGS"){root=candidate;standings=name=="STANDINGS";break;}
 }
 if(!root)return false;auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 LeagueTableCard card;card.standings=standings;std::array<bool,128> shown{},grid{};shown[depth]=true;
 std::array<int,128> row{},col{};row.fill(-1);col.fill(-1);
 for(auto node=kernel::R32(root+8),n=0u;n<16384 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
  shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);grid[l]=grid[l-1];row[l]=row[l-1];col[l]=col[l-1];
  if(name=="CELL" || name=="TOP25RANKCELL"){grid[l]=true;card.first=int(kernel::R32(node+0x74));card.selected=int(kernel::R32(node+0x6C));card.columnFirst=int(kernel::R32(node+0x70));}
  int x=-1,y=-1;if(grid[l] && std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){col[l]=x;row[l]=y;}
  if(!shown[l])continue;
  if(!grid[l] && name=="CONFNAMETL")card.conference=NativeUiText(node);
  if(grid[l] && row[l]>=0 && row[l]<32 && col[l]>=0 && col[l]<16 && x<0 &&
     name!="CELL" && name!="TOP25RANKCELL" && name!="VSCROLL"){
   auto value=NativeUiText(node);if(!value.empty() && std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32 && c<127;}))card.rows[row[l]][col[l]]=value;
  }
 }
 // Read the manager's team for the red persistent row highlight.
 auto home=g_dynasty_home_root.load();if(valid(home)){
  auto hd=kernel::R32(home+0x24);
  for(auto node=kernel::R32(home+8),n=0u;n<4096 && valid(node) && kernel::R32(node+0x24)>hd;node=kernel::R32(node+8),++n)
   if(kernel::ReadCString(node+0x10,16)=="TEAMNAME"){card.managedTeam=NativeUiText(node);break;}
 }
 if(!DrawLeagueTable(card))return false;
 if(TraceStartup()){static std::string last;auto key=std::to_string(standings)+card.conference+std::to_string(card.first)+"/"+std::to_string(card.selected)+"/"+std::to_string(card.columnFirst);for(auto& r:card.rows)for(auto& v:r)key+=v+"|";
  if(key!=last){last=key;LOGR("league table: standings=%d conference=%s first=%d selected=%d columnFirst=%d",standings,card.conference.c_str(),card.first,card.selected,card.columnFirst);
   for(int i=0;i<32;++i)if(!card.rows[i][1].empty())LOGR("league row %d: %s | %s | %s | %s | %s | %s | %s | %s",i,card.rows[i][0].c_str(),card.rows[i][1].c_str(),card.rows[i][2].c_str(),card.rows[i][3].c_str(),card.rows[i][4].c_str(),card.rows[i][5].c_str(),card.rows[i][6].c_str(),card.rows[i][7].c_str());gpu::g_capture=true;}
 }
 return true;
}

void MapDynastyPickerConfirm(int port,input::Pad& pad) {
 if(port!=0 || !overhaul::Active())return;
 static bool queued=false;
 const auto root=g_dynasty_school_root.load();
 if(!root || !mem::IsCommitted(root+0x98) || (kernel::R16(root+0x28)&3)!=3){queued=false;return;}
 if(pad.analog[1]){queued=false;return;}
 const bool settling=g_picker_changing.load() || (g_picker_expected.load() && g_picker_expected.load()!=g_picker_displayed.load());
 if(settling){queued=queued || pad.analog[0]!=0;pad.analog[0]=0;}
 else if(queued){pad.analog[0]=255;queued=false;}
}
int StatisticsMenuSelection() {
 auto root=g_statistics_menu_root.load(),grid=g_statistics_grid.load();
 if(!root || !grid || !mem::IsCommitted(root+0x98) || !mem::IsCommitted(grid+0x98) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="STATSMENU" || kernel::ReadCString(grid+0x10,16)!="FRANCHISE")return -1;
 const auto pending=g_statistics_target.load();return pending>=0?pending:std::clamp(int(kernel::R32(grid+0x6C)),0,5);
}
void MapStatisticsMenuPad(int port,input::Pad& pad) {
 if(port!=0 || !overhaul::Active())return;
 const auto root=g_statistics_menu_root.load(),grid=g_statistics_grid.load();
 const bool active=root>=0x80000000u && root<0x90000000u && mem::IsCommitted(root+0x98) && (kernel::R16(root+0x28)&3)==3 &&
   grid>=0x80000000u && grid<0x90000000u && mem::IsCommitted(grid+0x98) && kernel::ReadCString(root+0x10,16)=="STATSMENU" && kernel::ReadCString(grid+0x10,16)=="FRANCHISE" && !SubmenuPopupVisible();
 static uint32_t lastGrid=0;static int held=0;static bool confirm=false;static double repeatAt=0,pulseEnd=0,nextPulse=0;
 const auto now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 if(!active){g_statistics_target=-1;lastGrid=0;held=0;confirm=false;return;}
 if(lastGrid!=grid){lastGrid=grid;g_statistics_target=-1;held=0;confirm=false;pulseEnd=nextPulse=0;}
 const auto selected=std::clamp(int(kernel::R32(grid+0x6C)),0,5);
 static int observed=-1;static double progressed=0;if(selected!=observed){observed=selected;progressed=now;}
 if(g_statistics_target.load()>=0 && now-progressed>1){g_statistics_target=-1;confirm=false;pulseEnd=nextPulse=0;}
 int direction=0;
 if(pad.buttons&1)direction=1;else if(pad.buttons&2)direction=2;else if(pad.buttons&4)direction=4;else if(pad.buttons&8)direction=8;
 if(!direction){if(std::abs(int(pad.ly))>16000)direction=pad.ly>0?1:2;else if(std::abs(int(pad.lx))>16000)direction=pad.lx>0?8:4;}
 pad.buttons&=~uint16_t(15);pad.lx=pad.ly=0;
 if(pad.analog[1]){g_statistics_target=-1;confirm=false;return;}
 if(direction && (direction!=held || now>=repeatAt)){
  progressed=now;
  int from=g_statistics_target.load();if(from<0)from=selected;
  int target=from;
  if(direction==1 || direction==2)target=(from+3)%6;
  if(direction==4)target=from/3*3+(from%3+2)%3;
  if(direction==8)target=from/3*3+(from%3+1)%3;
  g_statistics_target=target;repeatAt=now+(direction!=held?.35:.18);
 }
 held=direction;
 int target=g_statistics_target.load();
 if(dev::Env("MVP06_TEST_STATISTICS_MENU")){static int a=-2,b=-2;if(a!=selected || b!=target){a=selected;b=target;LOGR("statistics navigation: actual=%d target=%d",selected,target);}}
 if(target>=0 && target!=selected){
  confirm=confirm || pad.analog[0]!=0;pad.analog[0]=0;
  if(now>=nextPulse){pulseEnd=now+.055;nextPulse=now+.14;}
  if(now<pulseEnd)pad.buttons|=target>selected?2:1;
 }else{
  g_statistics_target=-1;pulseEnd=0;
  if(confirm){pad.analog[0]=255;confirm=false;}
  if(pad.analog[0] && TraceStartup())LOGR("statistics confirm: native row=%d",selected);
 }
}

bool DrawNativeStatisticsMenu() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 auto root=g_statistics_menu_root.load();if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="STATSMENU")return false;
 auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 StatisticsMenuCard card;std::array<std::string,4> grades,coach;std::string scouting;std::array<bool,128> shown{},menu{};shown[depth]=true;std::array<int,128> row{},rating{};row.fill(-1);rating.fill(-1);std::vector<int> stars;int cover=0;bool covered=false;

 for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
  shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);row[l]=row[l-1];rating[l]=rating[l-1];menu[l]=menu[l-1];if(!shown[l])continue;
  if(name=="FRANCHISE"){menu[l]=true;card.selected=std::clamp(int(kernel::R32(node+0x6C)),0,5);g_statistics_grid=node;}
  int y=-1;if(menu[l] && std::sscanf(name.c_str(),"X0Y%d",&y)==1)row[l]=y;
  if(row[l]>=0 && row[l]<6 && (name=="BATTING" || name=="PITCHING" || name=="FIELDING" || name=="BASERUNNING" || name=="LEADERS" || name=="CAREERSTATS")){card.menu[row[l]]=NativeUiText(node);card.enabled[row[l]]=(kernel::R16(node+0x28)&2)!=0;}
  if(name=="DATE")card.date=NativeUiText(node);
  if(name=="TEAMNAME")card.school.team.name=NativeUiText(node);if(name=="TEAMNAME1")card.school.team.nickname=NativeUiText(node);
  if(name=="CONFERENCERANKN")card.school.standing=NativeUiText(node);if(name=="NATIONARANKNO")card.school.team.rank=NativeUiText(node);
  const char* categories[]={"PITCHING","BATTING","FIELDING","RUNNING"};for(int i=0;i<4;++i)if(name==categories[i])rating[l]=i;
  if(name=="RATING" && rating[l]>=0)grades[rating[l]]=NativeUiText(node);
  if(name=="RECORDVALUE")card.record=NativeUiText(node);
  if(name.starts_with("SMALLSTAR"))stars.push_back(int16_t(kernel::R16(node+0x2A))+kernel::R16(node+0x2E)/2);
  if(name=="COVER" && kernel::R16(node+0x2E)){cover=int16_t(kernel::R16(node+0x2A));covered=true;}
 }
 if(card.school.team.name.empty())return false;
 if(!stars.empty())card.school.prestige=int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
 const auto* school=SchoolFor(card.school.team.name,card.school.team.nickname);card.school.team.rank=RankFor(school,card.school.team.rank);if(school){card.school.color=school->color;card.school.location=school->location;}

 auto bar=g_dynasty_rightbar_root.load();
 if(valid(bar) && (kernel::R16(bar+0x28)&1)){
  auto bd=kernel::R32(bar+0x24);
  if(bd<127){shown.fill(false);shown[bd]=true;rating.fill(-1);std::array<bool,128> staff{};
   for(auto node=kernel::R32(bar+8),n=0u;n<2048 && valid(node) && kernel::R32(node+0x24)>bd;node=kernel::R32(node+8),++n){
    auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
    shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);staff[l]=staff[l-1];rating[l]=rating[l-1];if(!shown[l])continue;
    if(name=="STAFF")staff[l]=true;
    const char* categories[]={"PITCHING","BATTING","FIELDING","RUNNING"};for(int i=0;i<4;++i)if(name==categories[i]){
     if(staff[l])coach[i]=NativeUiText(node);else rating[l]=i;
    }
    if(name=="RATING" && rating[l]>=0)grades[rating[l]]=NativeUiText(node);
    if(name=="SCOUTINGLEVEL1")scouting=NativeUiText(node);
    if(name=="RECORDVALUE")card.record=NativeUiText(node);
   }
  }
 }
 if(card.menu[0].empty())return false;
 const auto nativeSelected=card.selected;const auto pending=g_statistics_target.load();if(pending>=0 && pending<6)card.selected=pending;
 if(!DrawStatisticsMenu(card))return false;
 if(TraceStartup()){static std::string last;auto key=card.school.team.name+card.date+card.record+scouting+std::to_string(card.selected);if(key!=last){last=key;LOGR("statistics menu: team=%s row=%d date=%s record=%s scouting=%s",card.school.team.name.c_str(),card.selected,card.date.c_str(),card.record.c_str(),scouting.c_str());LOGR("statistics native selection: %d",nativeSelected);gpu::g_capture=true;}}
 return true;
}
bool TeamManagementAvailable(int i){return i>=0 && i<10 && (g_team_manage_enabled.load()&(1u<<i));}
int TeamManagementSelection() {
 auto root=g_team_manage_root.load(),grid=g_team_manage_grid.load();
 if(!g_team_manage_presented.load() || !root || !grid || !mem::IsCommitted(root+0x98) || !mem::IsCommitted(grid+0x98) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="FOPTIONS" || kernel::ReadCString(grid+0x10,16)!="ROSTERSGRID")return -1;
 const auto pending=g_team_manage_target.load();return pending>=0?pending:std::clamp(int(kernel::R32(grid+0x6C)),0,9);
}
void MapTeamManagementPad(int port,input::Pad& pad) {
 if(port!=0 || !overhaul::Active())return;
 const auto root=g_team_manage_root.load(),grid=g_team_manage_grid.load();
 const bool active=g_team_manage_presented.load() && root>=0x80000000u && root<0x90000000u && mem::IsCommitted(root+0x98) && (kernel::R16(root+0x28)&3)==3 &&
   grid>=0x80000000u && grid<0x90000000u && mem::IsCommitted(grid+0x98) && kernel::ReadCString(root+0x10,16)=="FOPTIONS" && kernel::ReadCString(grid+0x10,16)=="ROSTERSGRID" && !SubmenuPopupVisible();
 static uint32_t lastGrid=0;static int held=0;static bool confirm=false;static double repeatAt=0,pulseEnd=0,nextPulse=0;
 const auto now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 if(!active){g_team_manage_target=-1;lastGrid=0;held=0;confirm=false;return;}
 if(lastGrid!=grid){lastGrid=grid;g_team_manage_target=-1;held=0;confirm=false;pulseEnd=nextPulse=0;}
 const auto selected=std::clamp(int(kernel::R32(grid+0x6C)),0,9);
 static int observed=-1;static double progressed=0;if(selected!=observed){observed=selected;progressed=now;}
 if(g_team_manage_target.load()>=0 && now-progressed>1){g_team_manage_target=-1;confirm=false;pulseEnd=nextPulse=0;}
 int direction=0;
 if(pad.buttons&1)direction=1;else if(pad.buttons&2)direction=2;else if(pad.buttons&4)direction=4;else if(pad.buttons&8)direction=8;
 if(!direction){if(std::abs(int(pad.ly))>16000)direction=pad.ly>0?1:2;else if(std::abs(int(pad.lx))>16000)direction=pad.lx>0?8:4;}
 pad.buttons&=~uint16_t(15);pad.lx=pad.ly=0;
 if(pad.analog[1]){g_team_manage_target=-1;confirm=false;return;}
 if(direction && (direction!=held || now>=repeatAt)){
  progressed=now;
  int from=g_team_manage_target.load();if(from<0)from=selected;
  int target=from;
  if(direction==1 || direction==2)target=(from+5)%10;
  if(direction==4)target=from/5*5+(from%5+4)%5;
  if(direction==8)target=from/5*5+(from%5+1)%5;
  const auto enabled=g_team_manage_enabled.load();
  if(!(enabled&(1u<<target))){
    int row=target/5,col=target%5;bool found=false;
    for(int i=1;i<5;++i){int candidate=row*5+(col+(direction==4?-i:i)+5)%5;if(enabled&(1u<<candidate)){target=candidate;found=true;break;}}
    if(!found)target=from;
  }
  g_team_manage_target=target;repeatAt=now+(direction!=held?.35:.18);
 }
 held=direction;
 int target=g_team_manage_target.load();
 if(dev::Env("MVP06_TEST_TEAM_MANAGEMENT")){static int a=-2,b=-2;if(a!=selected || b!=target){a=selected;b=target;LOGR("team management navigation: actual=%d target=%d",selected,target);}}
 if(target>=0 && target!=selected){
  confirm=confirm || pad.analog[0]!=0;pad.analog[0]=0;
  if(now>=nextPulse){pulseEnd=now+.04;nextPulse=now+.10;}
  if(now<pulseEnd)pad.buttons|=target>selected?2:1;
 }else{
  g_team_manage_target=-1;pulseEnd=0;
  if(confirm){pad.analog[0]=255;confirm=false;}
  if(pad.analog[0] && TraceStartup())LOGR("team management confirm: native row=%d",selected);
 }
}

namespace {
bool FeValid(uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0xA0);}
bool FeVisibleScene(uint32_t p,const char* name){
 if(!FeValid(p) || !(kernel::R16(p+0x28)&1) || kernel::ReadCString(p+0x10,16)!=name)return false;
 // Freed FE allocations can retain their name and visible bits after a new
 // screen reuses the surrounding heap. A live scene must still own a native
 // virtual table; a committed page alone is not evidence of a live object.
 const auto vtable=kernel::R32(p);
 return vtable>=0x005F0000u && vtable<0x006B0000u && mem::IsCommitted(vtable) &&
        kernel::R32(vtable)>=0x00010000u && kernel::R32(vtable)<0x005F0000u;
}
bool FeScene(uint32_t p,const char* name){return FeVisibleScene(p,name) && (kernel::R16(p+0x28)&2);}
bool FeClean(const std::string& s){return !s.empty() && s.size()<512 && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32 && c!=127;});}
template<class F> void FeWalk(uint32_t root,F visit){
 if(!FeValid(root) || !(kernel::R16(root+0x28)&1))return;auto d=kernel::R32(root+0x24);if(d>=127)return;
 std::array<bool,128> show{};show[d]=true;std::array<int,128> x{},y{},kind{};x.fill(-1);y.fill(-1);
 for(auto n=kernel::R32(root+8),count=0u;count<16384 && FeValid(n) && kernel::R32(n+0x24)>d;n=kernel::R32(n+8),++count){
  auto l=kernel::R32(n+0x24);if(!l || l>=128)continue;auto name=kernel::ReadCString(n+0x10,16);
  show[l]=show[l-1] && (kernel::R16(n+0x28)&1);x[l]=x[l-1];y[l]=y[l-1];kind[l]=kind[l-1];
  if(name=="CELL1" || name=="POSITIONGRP" || name=="BATTER")kind[l]=1;
  if(name=="CELL2" || name=="ROSTERGRP" || name=="PITCHER")kind[l]=2;
  if(name=="BATTERINFOGRID" || name=="PITCHERINFOGRID")kind[l]=3;if(name=="STATS")kind[l]=4;if(name=="HEADINGS")kind[l]=7;
  if(name=="VSLHP")kind[l]=5;if(name=="VSRHP")kind[l]=6;
  int cx,cy;if(std::sscanf(name.c_str(),"X%dY%d",&cx,&cy)==2){x[l]=cx;y[l]=cy;}
  if(show[l])visit(n,name,x[l],y[l],kind[l]);
 }
}
uint32_t FePortrait(uint32_t object){
 if(!FeValid(object))return 0;std::vector<std::pair<uint32_t,int>> queue{{object,0}};std::unordered_set<uint32_t> seen;
 for(size_t i=0;i<queue.size() && i<256;++i){auto [v,level]=queue[i];if(!FeValid(v) || !seen.insert(v).second)continue;gpu::TextureDesc desc;
  if((kernel::R32(v)&0x70000)==0x40000 && gpu::Describe(v,desc) && !desc.cube && desc.depth==1 && !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 && desc.width>=128 && desc.width<=256 && desc.height>=128 && desc.height<=256 && mem::IsCommitted(desc.data))return v;
  if(level<3)for(unsigned off=0;off<0x200;off+=4){auto p=kernel::R32(v+off);if(FeValid(p) && !seen.count(p))queue.emplace_back(p,level+1);}
 }
 return 0;
}
}
bool DrawNativeRecruitProfile(){
 auto root=g_recruit_profile_root.load();if(!overhaul::Active() || !FeScene(root,"RECRUITPROFILE"))return false;
 RecruitProfileCard card;uint32_t object=0;auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 std::array<bool,128> shown{};shown[depth]=true;std::array<int,128> xx{},yy{};xx.fill(-1);yy.fill(-1);
 std::array<std::string,128> group{};
 for(auto n=kernel::R32(root+8),i=0u;i<4096 && FeValid(n) && kernel::R32(n+0x24)>depth;n=kernel::R32(n+8),++i){
  auto d=kernel::R32(n+0x24);if(!d || d>=128)continue;auto name=kernel::ReadCString(n+0x10,16);
  shown[d]=shown[d-1] && (kernel::R16(n+0x28)&1);xx[d]=xx[d-1];yy[d]=yy[d-1];group[d]=group[d-1];
  if(name=="VITALS" || name=="PITCHERRATINGS" || name=="BATTERRATINGS")group[d]=name;
  int x,y;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){xx[d]=x;yy[d]=y;}
  auto marker=kernel::R32(n+0x6C);std::string v=(marker==0xBB55BB50u || marker==0xBB55BB54u)?NativeUiText(n):std::string();if(!FeClean(v))v.clear();
  if(name=="NAME")card.name=v;if(name=="POSITION")card.position=v;if(name=="RANKNUMBER")card.rank=v;if(name=="OVERALLNUMBER")card.overall=v;if(name=="STATUSVAL")card.status=v;
  if(name=="NUMBERYEARS")card.eligibility=v;if(name=="HEIGHT")card.height=v;if(name=="WEIGHT")card.weight=v;if(name=="HOMESTATE")card.home=v;if(name=="JUNIORCOLLEGE")card.from=v;
  if(name=="TL1" && group[d]=="VITALS"){if(yy[d]==1)card.bats=v;if(yy[d]==4)card.throws=v;}
  if(name=="ACADEMICVAL")card.academic=v;if(name=="ATTITUDEVAL")card.attitude=v;if(name=="INFO")card.needs=v;
  if(name.size()==3 && name.substr(0,2)=="YR" && name[2]>='1' && name[2]<='5')card.years[name[2]-'1']=v;
  if(shown[d]){
   if(name=="BATTERPITCHER" && !v.empty())card.page=v;
   if(name=="METERBAR")card.interest=std::clamp(kernel::R16(n+0x2E)/107.f,0.f,1.f);
   if(name.size()==5 && name.substr(0,4)=="LOCK" && name[4]>='1' && name[4]<='5')card.locked[name[4]-'1']=true;
   if(name.size()==3 && name.substr(0,2)=="NA" && name[2]>='1' && name[2]<='5')card.unavailable[name[2]-'1']=true;
   if((group[d]=="BATTERRATINGS" || group[d]=="PITCHERRATINGS") && yy[d]>=0 && yy[d]<6 && !v.empty()){
    if(xx[d]==0)card.labels[yy[d]]=v;if(xx[d]==1)card.ratings[yy[d]]=v;
   }
   if(name=="FACE"){object=kernel::R32(n+0x80);for(int j=0;j<4;++j){auto raw=kernel::R32(n+0x84+j*4);std::memcpy(&card.uv[j],&raw,4);}}
  }
 }
 if(card.name.empty())return false;
 // Hidden native grids keep the previous recruit's text until visited. Cache
 // only the visible page, keyed by recruit identity, never publish stale fields.
 static std::unordered_map<std::string,RecruitProfileCard> known;
 auto& saved=known[card.name+"/"+card.rank];
 if(card.page=="VITALS"){
  saved.height=card.height;saved.weight=card.weight;saved.bats=card.bats;saved.throws=card.throws;saved.home=card.home;saved.from=card.from;
 }else{card.height=saved.height;card.weight=saved.weight;card.bats=saved.bats;card.throws=saved.throws;card.home=saved.home;card.from=saved.from;}
 bool ratings=false;for(auto& v:card.ratings)ratings|=!v.empty();
 if(ratings){saved.labels=card.labels;saved.ratings=card.ratings;}else{card.labels=saved.labels;card.ratings=saved.ratings;}
 static std::string lastName;static uint32_t lastObject=0,face=0,epoch=0;static double ready=0;
 if(card.name!=lastName || object!=lastObject || epoch!=g_recruit_portrait_epoch.load()){lastName=card.name;lastObject=object;epoch=g_recruit_portrait_epoch.load();face=0;ready=PickerNow()+.05;}
 if(!face && PickerNow()>=ready)face=FePortrait(object);card.portrait=face;
 if(!DrawRecruitProfile(card))return false;
 if(TraceStartup()){static std::string last;auto key=card.name+card.page+card.status+std::to_string(card.portrait);if(last!=key){last=key;LOGR("recruit profile: name=%s position=%s rank=%s ovr=%s page=%s status=%s portrait=%08X vitals=%s/%s/%s/%s",card.name.c_str(),card.position.c_str(),card.rank.c_str(),card.overall.c_str(),card.page.c_str(),card.status.c_str(),card.portrait,card.height.c_str(),card.weight.c_str(),card.bats.c_str(),card.throws.c_str());for(int j=0;j<6;++j)LOGR("recruit attribute: %s=%s",card.labels[j].c_str(),card.ratings[j].c_str());gpu::g_capture=true;}}
 return true;
}

bool DrawNativeRosterEditor(){
 if(!overhaul::Active() || FeScene(g_stat_playerbar_root.load(),"PLAYERINFOBAR"))return false;auto root=g_roster_editor_root.load();if(!FeScene(root,"CONFLINEUP2"))return false;
 RosterEditorCard card;card.school=g_managed_school;bool title=false;uint32_t faceObject=0;
 FeWalk(g_recruit_title_root.load(),[&](uint32_t n,const std::string& name,int,int,int){
  if(name=="PITCHINGROTTITL"){card.kind=0;card.title="PITCHING ROTATION";title=true;}
  if(name=="BATTINGORDERTIT"){card.kind=1;card.title="BATTING ORDER";title=true;}
  if(name=="DEFENSETITLE"){card.kind=2;card.title="DEFENSIVE ALIGNMENT";title=true;}
 });if(!title)return false;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int kind){
  if(name=="CELL1" || name=="CELL2"){int k=name=="CELL1"?0:1;card.first[k]=std::clamp(int(kernel::R32(n+0x74)),0,31);card.selected[k]=std::clamp(int(kernel::R32(n+0x6C)),0,31)-card.first[k];if(kernel::R16(n+0x28)&8)card.active=k;}
  if(name=="LINEUPTL")card.mode=NativeUiText(n);
  if(name=="TEAMNAME"){auto team=NativeUiText(n);if(FeClean(team)){card.school.team.name=team;if(auto school=SchoolFor(team)){card.school.team.nickname=school->nickname;card.school.color=school->color;}}}
  if((kind==1 || kind==2) && x>=0 && x<4 && y>=0 && y<32 && name=="TB1"){auto v=NativeUiText(n);if(FeClean(v))card.rows[kind-1][y][x]=v;}

 });
 FeWalk(g_batpitch_info_root.load(),[&](uint32_t n,const std::string& name,int x,int y,int kind){
  if(name=="NAME")card.player=NativeUiText(n);
  if(name=="FACE"){faceObject=kernel::R32(n+0x80);for(int i=0;i<4;++i){auto raw=kernel::R32(n+0x84+i*4);std::memcpy(&card.uv[i],&raw,4);}}
  if(kind==3 && y>=0 && y<6){if(x==1 && name=="NUMBER")card.attributes[y]=NativeUiText(n);if(x==0 && (name=="TL1" || name=="POW" || name=="CON" || name=="SPD" || name=="STAT"))card.attributeLabels[y]=NativeUiText(n);}
  if(kind==4 && y>=0 && y<3){if(x==1 && name=="TX1")card.stats[y]=NativeUiText(n);if(x==0 && (name=="AVG" || name=="RBI" || name=="HR" || name=="WL" || name=="ERA" || name=="SO"))card.statLabels[y]=NativeUiText(n);}
  if((kind==5 || kind==6) && name.size()==2 && (name[0]=='U' || name[0]=='C' || name[0]=='L') && (name[1]=='L' || name[1]=='M' || name[1]=='R')){
   int row=name[0]=='U'?0:name[0]=='C'?1:2,col=name[1]=='L'?0:name[1]=='M'?1:2;
   const auto sprite=kernel::ReadCString(n+0x4E,5);card.zones[kind-5][row*3+col]=sprite=="sqbl"?0:sprite=="sqre"?2:1;
  }
 });
 struct Face {uint32_t object=0,texture=0;std::string player;double ready=0;};static Face face;
 if(face.object!=faceObject || face.player!=card.player)face={faceObject,0,card.player,PickerNow()+.05};
 if(!face.texture && PickerNow()>=face.ready)face.texture=FePortrait(faceObject);card.portrait=face.texture;
 if(!DrawRosterEditor(card))return false;g_team_manage_presented=false;
 if(TraceStartup()){static std::string last;std::string key=card.title+card.mode+card.player+std::to_string(card.active)+std::to_string(card.selected[0])+std::to_string(card.selected[1])+std::to_string(card.first[0])+std::to_string(card.first[1])+std::to_string(card.portrait);for(auto& list:card.rows)for(auto& row:list)for(auto& v:row)key+=v;
  if(key!=last){last=key;LOGR("roster editor: kind=%d title=%s active=%d row=%d/%d first=%d/%d mode=%s player=%s portrait=%08X",card.kind,card.title.c_str(),card.active,card.selected[0],card.selected[1],card.first[0],card.first[1],card.mode.c_str(),card.player.c_str(),card.portrait);for(int k=0;k<2;++k)for(int r=0;r<10;++r){std::string line;for(auto& v:card.rows[k][r])line+=v+" | ";LOGR("roster list %d row %d: %s",k,r,line.c_str());}for(int i=0;i<6;++i)LOGR("roster attribute %s=%s",card.attributeLabels[i].c_str(),card.attributes[i].c_str());gpu::g_capture=true;}}
 return true;
}
bool DrawNativeRosterStatus(){
 if(!overhaul::Active() || FeScene(g_stat_playerbar_root.load(),"PLAYERINFOBAR"))return false;
 auto root=g_roster_status_root.load();if(!FeScene(root,"FPLAYERSTATUS"))return false;
 RosterStatusCard card;card.school=g_managed_school;uint32_t faceObject=0;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  if(name=="CELL"){card.first=std::clamp(int(kernel::R32(n+0x74)),0,31);card.selected=std::clamp(int(kernel::R32(n+0x6C)),0,31)-card.first;}
  if(name=="ACTIVEVAL")card.active=NativeUiText(n);if(name=="INACTIVEVAL")card.inactive=NativeUiText(n);
  if(x>=0 && x<7 && y>=0 && y<32 && (name=="TB1" || name=="PRICE" || name=="PLACE" || name=="LF" || name=="DELOSSANTOS1")){auto v=NativeUiText(n);if(FeClean(v))card.rows[y][x]=v;}
 });
 FeWalk(g_stat_summary_root.load(),[&](uint32_t n,const std::string& name,int,int y,int kind){
  if(name=="NAME")card.player=NativeUiText(n);
  if(name=="FACE"){faceObject=kernel::R32(n+0x80);for(int i=0;i<4;++i){auto raw=kernel::R32(n+0x84+i*4);std::memcpy(&card.uv[i],&raw,4);}}
  if(name=="TX1" && y>=0 && y<8){if(kind==7)card.labels[y]=NativeUiText(n);if(kind==4)card.values[y]=NativeUiText(n);}
 });
 struct Face {uint32_t object=0,texture=0;std::string name;double ready=0;};static Face face;
 if(face.object!=faceObject || face.name!=card.player)face={faceObject,0,card.player,PickerNow()+.05};if(!face.texture && PickerNow()>=face.ready)face.texture=FePortrait(faceObject);card.portrait=face.texture;
 if(card.active.empty() || !DrawRosterStatus(card))return false;g_team_manage_presented=false;
 if(TraceStartup()){static std::string last;std::string key=card.active+card.inactive+card.player+std::to_string(card.selected)+std::to_string(card.first)+std::to_string(card.portrait);for(auto& row:card.rows)for(auto& v:row)key+=v;
  if(key!=last){last=key;LOGR("roster status: active=%s inactive=%s row=%d first=%d player=%s portrait=%08X",card.active.c_str(),card.inactive.c_str(),card.selected,card.first,card.player.c_str(),card.portrait);for(int r=0;r<7;++r){std::string line;for(auto& v:card.rows[r])line+=v+" | ";LOGR("roster status row %d: %s",r,line.c_str());}gpu::g_capture=true;}}
 return true;
}

bool DrawNativeManagementDetail(){
 if(!overhaul::Active() || FeScene(g_stat_playerbar_root.load(),"PLAYERINFOBAR"))return false;ManagementDetailCard card;card.school=g_managed_school;auto root=g_pos_rank_root.load();
 if(!FeScene(root,"POSRANKING")){root=g_team_goals_root.load();card.goals=true;if(!FeScene(root,"FTEAMGOALS"))return false;}
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int kind){
  if(name=="POSITIONNAME")card.position=NativeUiText(n);if(name=="ROSTERVIEW")card.view=NativeUiText(n);
  if(name=="GRID"){int k=kind==2?1:0;card.selections[k]=std::clamp(int(kernel::R32(n+0x6C)),0,31);if(kernel::R16(n+0x28)&8)card.active=k;}
  if(card.goals && name=="CELL")card.selected=std::clamp(int(kernel::R32(n+0x6C)),0,3);
  if(card.goals && name=="TX1" && y<0){auto v=NativeUiText(n);if(FeClean(v)){if(int16_t(kernel::R16(n+0x2C))>320)card.help=v;else card.description=v;}}
  if(name=="BUTTONS")card.button=std::clamp(int(kernel::R32(n+0x6C)),0,1);
  if(name=="ACCEPT")card.buttons[0]=NativeUiText(n);if(name=="DECLINE")card.buttons[1]=NativeUiText(n);
  if(x>=0 && x<5 && y>=0 && y<32 && (name=="TB1" || (card.goals && name=="TX1"))){auto v=NativeUiText(n);if(FeClean(v))card.rows[kind==2?1:0][y][x]=v;}
  if(card.goals && y>=0 && y<4 && name=="CHECKBOX")card.completed[y]=kernel::ReadCString(n+0x4E,5)=="chek";
  if(card.goals && y>=0 && y<4 && name=="SL1"){auto sprite=kernel::ReadCString(n+0x4E,5);card.icons[y]=sprite=="ICOT"?1:sprite=="ICOR"?3:sprite=="ICOF"?2:0;}
 });
 static uint32_t detailCacheRoot=0;static ManagementDetailCard detailCache;
 if(detailCacheRoot!=root){detailCacheRoot=root;detailCache={};}
 if(card.goals && card.rows[0][0][3].empty()){
  if(FeScene(g_team_manage_root.load(),"FOPTIONS"))return false;
  if(detailCache.rows[0][0][3].empty())return false;card=detailCache;
 }else detailCache=card;
 if(!DrawManagementDetail(card))return false;g_team_manage_presented=false;
 if(TraceStartup()){static std::string last;std::string key=std::to_string(card.goals)+card.position+card.view+std::to_string(card.active)+std::to_string(card.selections[0])+std::to_string(card.selections[1])+std::to_string(card.selected);for(auto& list:card.rows)for(auto& row:list)for(auto& v:row)key+=v;
  if(key!=last){last=key;LOGR("management detail: goals=%d position=%s view=%s active=%d row=%d/%d goal=%d description=%s help=%s",card.goals,card.position.c_str(),card.view.c_str(),card.active,card.selections[0],card.selections[1],card.selected,card.description.c_str(),card.help.c_str());for(int k=0;k<2;++k)for(int r=0;r<(card.goals?4:10);++r){std::string line;for(auto& v:card.rows[k][r])line+=v+" | ";LOGR("management detail list %d row %d: %s",k,r,line.c_str());}gpu::g_capture=true;}}
 return true;
}

bool DrawNativeDepthChart() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 auto root=g_depth_chart_root.load();if(!valid(root) || !(kernel::R16(root+0x28)&1) || kernel::ReadCString(root+0x10,16)!="DEPTHCHART")return false;
 auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 DepthChartCard card;card.school=g_managed_school;
 const char* names[]={"LF","CF","RF","SS","B2","B3","SP","B1","C","RP"};
 const char* labels[]={"LF","CF","RF","SS","2B","3B","SP","1B","C","RP"};
 for(int i=0;i<10;++i)card.positions[i].label=labels[i];
 std::array<bool,128> shown{};shown[depth]=true;
 std::array<int,128> pos{},col{},row{},kind{};pos.fill(-1);col.fill(-1);row.fill(-1);kind.fill(0);
 std::string debug;
 for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
  shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);pos[l]=pos[l-1];col[l]=col[l-1];row[l]=row[l-1];kind[l]=kind[l-1];if(!shown[l])continue;
  for(int i=0;i<10;++i)if(name==names[i])pos[l]=i;
  if(name=="NUMPLAYERS")card.total=NativeUiText(node);
  if(name=="LEFTOVERLAY")card.overlay=true;
  if(name=="PLAYERGRID"){kind[l]=1;card.first=std::clamp(int(kernel::R32(node+0x74)),0,29);card.selected=std::clamp(int(kernel::R32(node+0x6C)),0,29);}
  if(name=="BATTING" || name=="PITCHING"){kind[l]=2;card.actions=true;card.action=std::clamp(int(kernel::R32(node+0x6C)),0,3);}
  int x,y;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){col[l]=x;row[l]=y;}
  if(pos[l]>=0){auto& item=card.positions[pos[l]];
   if(name=="NUM")item.count=NativeUiText(node);
   // Native FE focus is bit 3; frame child pointers include every state.
   if(name=="FC2")item.selected=(kernel::R16(node+0x28)&8)!=0;
   if(row[l]>=0 && row[l]<3 && col[l]>=0 && col[l]<3 && (name=="TB1" || name=="TL1"))item.rows[row[l]][col[l]]=NativeUiText(node);
  }
  if(kind[l]==1 && row[l]>=0 && row[l]<30 && col[l]>=0 && col[l]<3 && (name=="TB1" || name=="TL1"))card.players[row[l]][col[l]]=NativeUiText(node);
  if(kind[l]==2 && row[l]>=0 && row[l]<4 && (name=="PITCHROT" || name=="BATTINGORDER" || name=="DEFENSIVE" || name=="PLAYERSTATUS"))card.commands[row[l]]=NativeUiText(node);
 }
 static uint32_t cachedRoot=0;static DepthChartCard cached;
 if(cachedRoot!=root){cachedRoot=root;cached={};}
 if(!card.total.empty())cached=card;
 else if(card.overlay && !cached.total.empty()){card.total=cached.total;card.positions=cached.positions;}
 else return false;
 for(int i=0;i<10;++i)if(card.positions[i].selected)debug+=card.positions[i].label;

 if(!DrawDepthChart(card))return false;
 if(TraceStartup()){static std::string last;auto key=debug+std::to_string(card.overlay)+std::to_string(card.actions)+std::to_string(card.selected)+std::to_string(card.action);if(key!=last){last=key;LOGR("depth chart: total=%s overlay=%d actions=%d row=%d first=%d action=%d position=%s",card.total.c_str(),card.overlay,card.actions,card.selected,card.first,card.action,debug.c_str());gpu::g_capture=true;}}
 return true;
}

namespace {
template<class F> void WalkRosterPresentation(uint32_t root,F visit,bool includeHidden=false){
 if(!FeValid(root))return;auto depth=kernel::R32(root+0x24);if(depth>=127)return;
 std::array<bool,128> shown{};shown[depth]=true;std::array<int,128> xs{},ys{},sides{};std::array<std::string,128> groups{};xs.fill(-1);ys.fill(-1);
 for(auto node=kernel::R32(root+8),i=0u;i<16384 && FeValid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++i){
  auto level=kernel::R32(node+0x24);if(!level || level>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
  shown[level]=shown[level-1] && (kernel::R16(node+0x28)&1);xs[level]=xs[level-1];ys[level]=ys[level-1];sides[level]=sides[level-1];groups[level]=groups[level-1];
  if(name=="OPPROSTERGRP")sides[level]=1;if(name=="USERROSTERGRP")sides[level]=0;
  if(name=="TOPINFO" || name=="BATTERSTATS" || name=="PITCHERSTATS" || name=="BCAREERSTATS" || name=="PCAREERSTATS" || name=="BATTING" || name=="PITCHING" || name=="BATTERRATINGS")groups[level]=name;
  if(name=="KEYBOARD" || name=="NAME")groups[level]=name;
  int x,y;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){xs[level]=x;ys[level]=y;}
  if(shown[level] || includeHidden)visit(node,name,xs[level],ys[level],sides[level],groups[level]);
 }
}
uint32_t BrowserPortrait(uint32_t object,const std::string& identity,int slot=0){
 struct Cache {uint32_t object=0,texture=0;std::string identity;double ready=0;};static std::array<Cache,2> caches;auto& cache=caches[slot];
 if(cache.object!=object || cache.identity!=identity)cache={object,0,identity,PickerNow()+.05};
 if(!cache.texture && object && PickerNow()>=cache.ready)cache.texture=FePortrait(object);return cache.texture;
}
}
bool DrawNativeRosterBrowser(){
 if(!overhaul::Active())return false;auto root=g_edit_list_root.load();bool trade=false;
 if(!FeScene(root,"EDITPLAYER")){root=g_trade_root.load();trade=true;if(!FeScene(root,"TRADEPLAYERS"))return false;}
 RosterBrowserCard card;card.trade=trade;card.team=g_managed_school.team.name;card.listNames=trade?std::array<std::string,2>{"EXTRA PLAYERS","CURRENT TEAM"}:std::array<std::string,2>{"CURRENT TEAM","AVAILABLE PLAYERS"};
 card.rows[0].resize(trade?10:7);if(trade)card.rows[1].resize(10);
 WalkRosterPresentation(root,[&](uint32_t n,const std::string& name,int x,int y,int side,const std::string&){
  if(name=="GRID" || name=="PLAYERGRID"){card.first[side]=std::max(0,int(kernel::R32(n+0x74)));card.selected[side]=std::clamp(int(kernel::R32(n+0x6C))-card.first[side],0,trade?9:6);if(kernel::R16(n+0x28)&8)card.active=side;}
  auto marker=kernel::R32(n+0x6C);if(marker!=0xBB55BB50u && marker!=0xBB55BB54u)return;auto value=NativeUiText(n);if(!FeClean(value))return;
  const int column=trade?x:x-1;
  if(column>=0 && column<(trade?3:5) && y>=0 && y<int(card.rows[side].size()) && name=="TB1")card.rows[side][y][column]=value;
  if(name=="TEAMNAME" || name=="TEAMNAMES"){card.team=value;card.listNames[side]=value;}if(name=="CONFERENCE")card.conference=value;
  // This fixed grid is the native Extra pool, not another selectable school.
  // Its legacy name string collides with an expanded league school label.
  if(trade && name=="USERTEAMNAME")card.listNames[0]="EXTRA PLAYERS";
  if(trade && name=="OPPTEAMNAME")card.listNames[1]=value;
  if(trade && dev::Env("MVP06_TRACE_FE") && (name=="USERTEAMNAME" || name=="OPPTEAMNAME" || (name=="TB1" && y==0))){
   static std::unordered_set<std::string> seen;
   const auto key=std::to_string(root)+name+std::to_string(side)+std::to_string(x)+value;
   if(seen.insert(key).second)LOGR("trade native field: %s side=%d col=%d row=%d value=%s",name.c_str(),side,x,y,value.c_str());
  }
 });
 uint32_t faceObject=0;std::array<std::string,6> labels{},values{};
 FeWalk(g_batpitch_info_root.load(),[&](uint32_t n,const std::string& name,int x,int y,int kind){
  if(name=="NAME")card.player=NativeUiText(n);
  if(name=="FACE"){faceObject=kernel::R32(n+0x80);for(int j=0;j<4;++j){auto raw=kernel::R32(n+0x84+j*4);std::memcpy(&card.uv[j],&raw,4);}}
  if(kind==3 && y>=0 && y<6){auto v=NativeUiText(n);if(FeClean(v)){if(x==0)labels[y]=v;if(x==1)values[y]=v;}}
 });
 for(int i=0;i<6;++i)if(!labels[i].empty()&&!values[i].empty())card.attributes.emplace_back(labels[i]+(i<3?" LHP":" RHP"),values[i]);
 if(!trade && !card.rows[0].empty()){
  const auto& selected=card.rows[0][card.selected[0]];if(!selected[1].empty())card.player=selected[1];if(!selected[4].empty())card.team=selected[4];
 }
 if(card.team.empty())card.team="TEAM ROSTER";card.portrait=BrowserPortrait(faceObject,card.player);
 return DrawRosterBrowser(card);
}
bool DrawNativeTextKeyboard(){
 if(!overhaul::Active())return false;auto root=g_editor_keyboard_root.load();if(!FeScene(root,"TKEYBOARD"))return false;
 TextKeyboardCard card;std::array<std::string,32> characters;
 for(auto& row:card.keyOwner)for(int i=0;i<13;++i)row[i]=i;
 WalkRosterPresentation(root,[&](uint32_t n,const std::string& name,int x,int y,int,const std::string& group){
  if(name=="KEYBOARD"){
   const auto a=kernel::R32(n+0x6C),b=kernel::R32(n+0x70),stride=kernel::R32(n+0x50),table=kernel::R32(n+0x9C);
   card.x=std::clamp(int(b),0,12);card.y=std::clamp(int(a),0,4);
   // Resolve the actual selected native cell, using the same indexing as
   // the grid's GetCurrentCell method (A7850 / A7880).
   if(a<64 && b<64 && stride>0 && stride<=64 && FeValid(table)){
    const auto entry=table+4*(a+stride*b);if(mem::IsCommitted(entry)){
     const auto cell=kernel::R32(entry);if(FeValid(cell)){int x,y;auto key=kernel::ReadCString(cell+0x10,16);if(std::sscanf(key.c_str(),"X%dY%d",&x,&y)==2 && x>=0&&x<13&&y>=0&&y<5){card.x=x;card.y=y;}}
    }
   }
  }
  if(group=="KEYBOARD" && x>=0 && x<13 && y>=0 && y<5){
   if(name=="L")card.keyOwner[y][x]=std::max(0,x-1);if(name=="R")card.keyOwner[y][x]=std::min(12,x+1);
   if(name=="LARROW")card.keys[y][x]="<";if(name=="RARROW")card.keys[y][x]=">";
  }
  auto marker=kernel::R32(n+0x6C);if(marker!=0xBB55BB50u && marker!=0xBB55BB54u)return;auto v=NativeUiText(n);if(!FeClean(v))return;
  if(name=="TITLE" || name=="PROMPTTEXT")card.title=v;if(name=="ENTEREDTEXT")card.entered=v;
  if(group=="KEYBOARD" && x>=0 && x<13 && y>=0 && y<5)card.keys[y][x]=v;
  if(group=="NAME" && x>=0 && x<32)characters[x]=v;
 });
 // Entered text is stored in TX/NAME controls even when their native artwork
 // is hidden. Its text type also differs from the keyboard's TB key labels.
 WalkRosterPresentation(root,[&](uint32_t n,const std::string& name,int x,int,int,const std::string& group){
  if(name=="ENTEREDTEXT"){auto value=NativeUiText(n);if(FeClean(value))card.entered=value;}
  if(group=="NAME" && name=="SPOT" && x>=0 && x<32){auto value=NativeUiText(n);if(FeClean(value))characters[x]=value;}
 },true);
 if(card.entered.empty())for(auto& ch:characters)if(ch!="_" && ch!="*")card.entered+=ch;
 for(auto& row:card.keyOwner)for(int x=0;x<13;++x){int owner=x;for(int i=0;i<13 && row[owner]!=owner;++i)owner=row[owner];row[x]=owner;}
 return DrawTextKeyboard(card);
}
std::string MissingTutorialVideo(){
 const auto root=g_submenu_root.load();
 if(!FeScene(root,"TUTORIALS"))return {};
 const int selected=SelectedSubmenuRow();if(selected<0 || selected>2)return {};
 const unsigned movie=17u-unsigned(selected);g_tutorial_movie=movie;
 const auto filename=kernel::ReadCString(kernel::R32(0x006A5E08+movie*4),32)+".vp6";
 const auto relative=std::filesystem::path("data/frontend/movies")/filename;
 return kernel::mods::IsFile(kernel::mods::GameDir()/relative)?std::string():relative.generic_string();
}
bool DrawNativeStadiumSelection(){
 if(!overhaul::Active())return false;auto root=g_stadium_select_root.load();
 if(!FeScene(root,"STADIUM"))return false;
 StadiumSelectionCard card;
 FeWalk(root,[&](uint32_t n,const std::string& name,int,int,int){
  if(name=="MENU"){
   card.timeSelected=kernel::R32(n+0x6C)==1;
   // The grid rebinds its visible cells as focus changes. Filter the menu's
   // complete draw tree so its time spinner cannot survive a cell rebind.
   // The venue preview is a sibling; native focus and callbacks still run.
   SuppressNativeUiScene(n);
  }
  const auto marker=kernel::R32(n+0x6C);if(marker!=0xBB55BB50u && marker!=0xBB55BB54u)return;
  auto text=NativeUiText(n);if(!FeClean(text))return;
  if(name=="NAME")card.venue=text;if(name=="GAMETIME")card.time=text;
  if(name=="TEXTLIST"){if(!card.wind.empty())card.wind+="  /  ";card.wind+=text;}
 });
 return DrawStadiumSelection(card);
}
bool DrawNativeBallparkEditor(){
 if(!overhaul::Active())return false;auto root=g_utility_roots[6].load();if(!FeScene(root,"BALLPARKHUD"))return false;
 BallparkEditorCard card;bool hud=false;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  if(name=="HUD")hud=true;if(name=="WALLEDITOR" && kernel::R32(n+0x6C)!=0xBB55BB50u && kernel::R32(n+0x6C)!=0xBB55BB54u)card.wall=true;
  if(name=="OPTIONS")card.selected=std::clamp(int(kernel::R32(n+0x6C)),0,4);
  auto marker=kernel::R32(n+0x6C);if(marker!=0xBB55BB50u && marker!=0xBB55BB54u)return;
  auto value=NativeUiText(n);if(!FeClean(value))return;
  if(x==0 && y>=0 && y<5){if(name=="TL1")card.values[y]=value;else if(name=="STADIUMSTYLE" || name=="LOCATION" || name=="FIELDSURFACE" || name=="WALLEDITOR")card.labels[y]=value;}
  if(name=="WALLADJUST")card.mode=value;if(name=="POINTS")card.points=value;if(name=="DISTANCE")card.measurement=value;if(name=="WALLDIVIDER")card.divider=value;
 });
 if(dev::Env("MVP06_UI_SCREEN_AUDIT")){static std::string logged;auto key=card.mode+"|"+card.divider+"|"+card.measurement;
  if(card.wall && key!=logged){logged=key;std::string hex;for(unsigned char c:card.measurement){char b[4];std::snprintf(b,sizeof b,"%02X",c);hex+=b;}
   LOGR("ballpark wall: mode=%s divider=%s distance=%s hex=%s",card.mode.c_str(),card.divider.c_str(),card.measurement.c_str(),hex.c_str());}}
 card.hidden=!hud&&!card.wall;return DrawBallparkEditor(card);
}
bool DrawNativeBallparkLoading(){
 if(!overhaul::Active())return false;const auto root=g_ballpark_loading_root.load();
 // LOADOUT is released while the frontend heap is rebuilt. Keep the host
 // card through that handoff; SAVESYS/MAINMENU/TEAMSELECT clear this latch.
 if(!root)return false;
 if(FeVisibleScene(root,"LOADOUT") || FeVisibleScene(root,"LOADINGOUT"))SuppressNativeUiScene(root);
 UtilityMenuCard card;card.loading=true;return DrawUtilityMenu(card);
}
bool DrawNativeUtilityMenu(){
 if(!overhaul::Active())return false;
 const char* names[]={"THUB","TROSTERMENU","FOPTIONS","TELIMSETUP","CBPMENU","INGAMEREPLAY"};
 const char* titles[]={"TOURNAMENT CENTRAL","TEAM ROSTERS","TEAM MANAGEMENT","TOURNAMENT SETUP","BALLPARK STUDIO","INSTANT REPLAY"};
 // Child menus take precedence over their still-visible parent shell.
 const int order[]={5,3,1,2,4,0};
 for(auto index:order){auto root=g_utility_roots[index].load();
  // Several utility screens keep the scene container disabled while their
  // child grid owns input. Visibility, rather than the container's enabled
  // bit, determines whether that screen is being presented.
  if(!FeValid(root) || !(kernel::R16(root+0x28)&1))continue;
  const auto actual=kernel::ReadCString(root+0x10,16);
  if(actual!=names[index] && !(index==3&&actual=="TSELECTSCHOOLS") && !(index==0&&actual=="FMGRDECISION"))continue;
  // Tournament Central reuses the Dynasty manager class, but loads THUB's
  // controls. Identify that layout before presenting it as a tournament hub.
  if(index==0 && actual=="FMGRDECISION"){
   bool tournament=false;FeWalk(root,[&](uint32_t,const std::string& name,int,int,int){tournament|=name=="PLAYTOURNAMENT" || name=="TMENU";});if(!tournament)continue;
  }
  bool focused=(kernel::R16(root+0x28)&2)!=0;
  FeWalk(root,[&](uint32_t n,const std::string&,int,int,int){focused|=(kernel::R16(n+0x28)&8)!=0;});
  if(dev::Env("MVP06_UI_SCREEN_AUDIT")){static std::array<uint32_t,6> last{};const auto state=kernel::R16(root+0x28)|(focused?0x10000u:0);if(last[index]!=state){last[index]=state;LOGR("utility menu native: %s root=%08X flags=%04X focus=%d",names[index],root,kernel::R16(root+0x28),int(focused));}}
  if(!focused && index!=5)continue;
  UtilityMenuCard card;card.title=titles[index];card.slots=index==3;card.replay=index==5;card.teamSwitch=index==0;card.subtitle=card.slots?"CHOOSE THE PROGRAM IN EACH TOURNAMENT SLOT":"SELECT AN ACTION TO CONTINUE";
  std::string teamName,date;
  std::array<std::string,16> labels{},details{};std::array<bool,16> enabled{};bool found=false,replayControls=false;
  if(actual=="TSELECTSCHOOLS"){
   struct Item{int x,y,score;std::string text;};std::vector<Item> teams,arrows;std::array<int,128> seedAt{};
   FeWalk(root,[&](uint32_t n,const std::string& name,int,int,int){
    const int x=int16_t(kernel::R16(n+0x2A)),y=int16_t(kernel::R16(n+0x2C));
    const auto level=kernel::R32(n+0x24);seedAt[level]=level?seedAt[level-1]:0;int seed;
    if(std::sscanf(name.c_str(),"TEAM%d",&seed)==1 && seed>=1&&seed<=8)seedAt[level]=seed;
    const auto marker=kernel::R32(n+0x6C);const auto flags=kernel::R16(n+0x28);
    if(name=="TEAM" && (marker==0xBB55BB50u||marker==0xBB55BB54u)){auto value=NativeUiText(n);if(FeClean(value))teams.push_back({x,y,seedAt[level],value});}
    if((name=="NEXT"||name=="PREV") && (flags&2))arrows.push_back({x,y,(flags&8)?100:10,{}});
   });
   std::sort(teams.begin(),teams.end(),[](const Item& a,const Item& b){return a.x!=b.x?a.x<b.x:a.y<b.y;});
   int focusScore=-1;
   for(int i=0;i<int(teams.size())&&i<16;++i){const auto& team=teams[i];labels[i]=team.text;enabled[i]=true;found=true;
    if(team.score>=1&&team.score<=8)details[i]="SEED "+std::to_string(team.score);
    for(const auto& arrow:arrows)if(std::abs(arrow.y-team.y)<=3 && std::abs(arrow.x-team.x)<190 && arrow.score>focusScore){focusScore=arrow.score;card.selected=i;}
   }
  }
  FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
   if(name=="MENU" || name=="TMENU" || name=="OPTIONS" || name=="CELL8TEAMS" || name=="CELL4TEAMS" || name=="CELL2TEAMS")card.selected=std::clamp(int(kernel::R32(n+0x6C)),0,15);
   if(card.replay){
    // The native transport is sprite based. Its containing group's visible
    // state owns Hide independently of the text-control implementation.
    if(name=="CONTROLS")replayControls=true;
    if(name=="CAMERANAME"){auto text=NativeUiText(n);if(FeClean(text))card.subtitle=text;}
    return;
   }
   auto marker=kernel::R32(n+0x6C);if(marker!=0xBB55BB50u && marker!=0xBB55BB54u)return;auto value=NativeUiText(n);if(!FeClean(value))return;
   if(name=="TEAMNAME")teamName=value;if(name=="DATE")date=value;
   if(x==0 && y>=0 && y<16 && (card.slots?name=="NAME":true)){
    labels[y]=value;enabled[y]=(kernel::R16(n+0x28)&2)!=0;found=true;
   }
  });
  if(card.replay){SuppressNativeUiScene(root);card.hidden=!replayControls;return DrawUtilityMenu(card);}
  if(!teamName.empty())card.subtitle=teamName+((index==0||index==1)?"  /  TOURNAMENT":date.empty()?std::string():"  /  "+date);
  if(index==0 && !teamName.empty())if(const auto* school=SchoolFor(teamName)){
    g_managed_school.team.name=school->name;g_managed_school.team.nickname=school->nickname;
    g_managed_school.team.rank=RankFor(school);g_managed_school.color=school->color;g_managed_school.location=school->location;
  }
  int count=16;while(count && labels[count-1].empty())--count;
  for(int i=0;i<count;++i){auto value=labels[i];if(card.slots){const auto* school=SchoolFor(value);if(!school)for(auto& s:LeagueSchools())if(s.abbreviation==value){school=&s;break;}if(school)value=school->name;}
   card.labels.push_back(value);card.values.push_back(details[i]);card.enabled.push_back(enabled[i]);}
  if(!found){card.labels={"PREPARING MENU"};card.enabled={false};}
  return DrawUtilityMenu(card);
 }
 return false;
}
bool ReplayUiActive(){return overhaul::Active() && FeVisibleScene(g_utility_roots[5].load(),"INGAMEREPLAY");}
bool DrawNativePlayerInformation(){
 if(!overhaul::Active())return false;auto root=g_stat_playerbar_root.load();if(!FeVisibleScene(root,"PLAYERINFOBAR"))return false;
 // Mini Games and Showdown retain their existing presentation in this pass.
 bool allowed=FeScene(g_edit_list_root.load(),"EDITPLAYER") || FeScene(g_trade_root.load(),"TRADEPLAYERS") || FeScene(g_roster_editor_root.load(),"CONFLINEUP2") || FeScene(g_roster_status_root.load(),"FPLAYERSTATUS") || FeVisibleScene(g_award_watch_root.load(),"AWARDWATCH") || FeVisibleScene(g_coach_root.load(),"FSIMPERATBAT");
 const char* scenes[]={"PLAYERSTATS","LEADERS","CAREERSTATS","INDCAREERSTATS"};for(int i=0;i<4;++i)allowed|=FeVisibleScene(g_stat_table_roots[i].load(),scenes[i]);if(!allowed)return false;
 PlayerInformationCard card;std::array<std::array<std::string,2>,12> identity{},stats{};uint32_t faceObject=0;
 WalkRosterPresentation(root,[&](uint32_t n,const std::string& name,int x,int y,int,const std::string& group){
  if(name=="BCAREERSTATS")card.page="CAREER BATTING";if(name=="PCAREERSTATS")card.page="CAREER PITCHING";
  if(name=="BATTERSTATS")card.page="BATTING STATISTICS";if(name=="PITCHERSTATS")card.page="PITCHING STATISTICS";
  if(name=="BATTERRATINGS")card.page="FIELDING / RUNNING";if(name=="BATTING")card.page="BATTING ATTRIBUTES";if(name=="PITCHING")card.page="PITCHING ATTRIBUTES";
  if(name=="FACE" || name=="LARGEFACE"){faceObject=kernel::R32(n+0x80);for(int j=0;j<4;++j){auto raw=kernel::R32(n+0x84+j*4);std::memcpy(&card.uv[j],&raw,4);}}
  auto marker=kernel::R32(n+0x6C);if(marker!=0xBB55BB50u && marker!=0xBB55BB54u)return;auto v=NativeUiText(n);if(!FeClean(v))return;
  if(name=="PLAYERNAME")card.name=v;if(name=="POSITION")card.position=v;if(name=="NUMBER" && group=="TOPINFO")card.number=v;
  if((name=="TX1" || name=="TEXT" || name=="STATSTITLE") && y<0 && group!="TOPINFO" && card.page.empty())card.page=v;
  if(x>=0 && x<2 && y>=0 && y<12){if(group=="TOPINFO")identity[y][x]=v;else stats[y][x]=v;}
 });
 for(auto& row:identity)if(!row[0].empty()&&!row[1].empty())card.identity.emplace_back(row[0],row[1]);
 for(auto& row:stats)if(!row[0].empty()&&!row[1].empty())card.stats.emplace_back(row[0],row[1]);
 // Its container can stay visible behind a table while all modal content is
 // hidden. Only present a populated native information page.
 if(card.name.empty() || card.identity.empty() || card.stats.empty())return false;
 card.portrait=BrowserPortrait(faceObject,card.name,1);bool underlay=DrawNativeRosterBrowser();
 if(!underlay)underlay=DrawNativeStatisticsTable();
 if(!underlay)underlay=DrawNativeAwardWatch();
 if(!underlay)underlay=DrawNativeCoach();
 return DrawPlayerInformation(card,!underlay);
}
int CreateEditAuditStage(){if(FeScene(g_create_editor_root.load(),"CREATEAPLAYER"))return 3;if(FeScene(g_edit_list_root.load(),"EDITPLAYER"))return 2;if(FeScene(g_create_edit_root.load(),"CPMENU"))return 1;return 0;}
bool PlayerEditorLoading(){return g_player_editor_loading.load() || PickerNow()<g_editor_transition_until.load();}
bool DrawNativeCreateEdit() {
 static CreateEditCard completed;static bool haveCard=false;
 if(PlayerEditorLoading() && haveCard){auto card=completed;card.loading=true;return DrawCreateEdit(card);}
 auto root=g_create_edit_root.load();if(!overhaul::Active() || !FeScene(root,"CPMENU"))return false;
 CreateEditCard card;card.school=g_managed_school;std::vector<int> stars;int cover=0;bool covered=false;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int kind){
  if(name=="MENU")card.selected=std::clamp(int(kernel::R32(n+0x6C)),0,1);
  if(name=="CREATE" || name=="EDIT")card.labels[name=="EDIT"]=NativeUiText(n);
  if(name=="TEAMNAME")card.school.team.name=NativeUiText(n);if(name=="TEAMNAME1")card.school.team.nickname=NativeUiText(n);
  if(name=="CONFERENCERANKN")card.school.standing=NativeUiText(n);if(name=="NATIONARANKNO")card.school.team.rank=NativeUiText(n);
  if(name.starts_with("SMALLSTAR"))stars.push_back(int16_t(kernel::R16(n+0x2A))+kernel::R16(n+0x2E)/2);
  if(name=="COVER" && kernel::R16(n+0x2E)){cover=int16_t(kernel::R16(n+0x2A));covered=true;}
 });
 if(card.labels[0].empty() || card.labels[1].empty())return false;
 if(!stars.empty())card.school.prestige=int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
 const auto* school=SchoolFor(card.school.team.name,card.school.team.nickname);card.school.team.rank=RankFor(school,card.school.team.rank);if(school)card.school.color=school->color;
 if(!DrawCreateEdit(card))return false;completed=card;haveCard=true;g_action_navigation=4;
 if(TraceStartup()){static std::string last;auto key=card.school.team.name+std::to_string(card.selected);if(key!=last){last=key;LOGR("create edit menu: team=%s row=%d",card.school.team.name.c_str(),card.selected);gpu::g_capture=true;}}
 return true;
}

bool DrawNativeTeamManagement() {
 struct Presented {bool value=false;~Presented(){g_team_manage_presented=value;}} presented;
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 auto root=g_team_manage_root.load();if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="FOPTIONS")return false;
 auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 TeamManagementCard card;std::array<bool,128> shown{},menu{};shown[depth]=true;std::array<int,128> row{},rating{};row.fill(-1);rating.fill(-1);std::vector<int> stars;int cover=0;bool covered=false;

 for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
  shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);row[l]=row[l-1];rating[l]=rating[l-1];menu[l]=menu[l-1];if(!shown[l])continue;
  if(name=="ROSTERSGRID"){menu[l]=true;card.selected=std::clamp(int(kernel::R32(node+0x6C)),0,9);g_team_manage_grid=node;}
  int y=-1;if(menu[l] && std::sscanf(name.c_str(),"X0Y%d",&y)==1)row[l]=y;
  if(row[l]>=0 && row[l]<10 && (name=="DEPTHCHART" || name=="PITCHING" || name=="BATTING" || name=="DEFENSIVE" || name=="CREATEPLAYER" || name=="AUTODOROSTERS" || name=="POSRANKING" || name=="TEAMGOALS" || name=="CHALLENGEITEMS" || name=="STATUS")){card.menu[row[l]]=NativeUiText(node);card.enabled[row[l]]=(kernel::R16(node+0x28)&2)!=0;}
  if(name=="DATE")card.date=NativeUiText(node);
  if(name=="TEAMNAME")card.school.team.name=NativeUiText(node);if(name=="TEAMNAME1")card.school.team.nickname=NativeUiText(node);
  if(name=="CONFERENCERANKN")card.school.standing=NativeUiText(node);if(name=="NATIONARANKNO")card.school.team.rank=NativeUiText(node);
  const char* categories[]={"PITCHING","BATTING","FIELDING","RUNNING"};for(int i=0;i<4;++i)if(name==categories[i])rating[l]=i;
  if(name=="RATING" && rating[l]>=0)card.grades[rating[l]]=NativeUiText(node);
  if(name=="RECORDVALUE")card.record=NativeUiText(node);
  if(name.starts_with("SMALLSTAR"))stars.push_back(int16_t(kernel::R16(node+0x2A))+kernel::R16(node+0x2E)/2);
  if(name=="COVER" && kernel::R16(node+0x2E)){cover=int16_t(kernel::R16(node+0x2A));covered=true;}
 }
 if(card.school.team.name.empty())return false;
 if(!stars.empty())card.school.prestige=int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
 const auto* school=SchoolFor(card.school.team.name,card.school.team.nickname);card.school.team.rank=RankFor(school,card.school.team.rank);if(school){card.school.color=school->color;card.school.location=school->location;}

 auto bar=g_dynasty_rightbar_root.load();
 if(valid(bar) && (kernel::R16(bar+0x28)&1)){
  auto bd=kernel::R32(bar+0x24);
  if(bd<127){shown.fill(false);shown[bd]=true;rating.fill(-1);std::array<bool,128> staff{};
   for(auto node=kernel::R32(bar+8),n=0u;n<2048 && valid(node) && kernel::R32(node+0x24)>bd;node=kernel::R32(node+8),++n){
    auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
    shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);staff[l]=staff[l-1];rating[l]=rating[l-1];if(!shown[l])continue;
    if(name=="STAFF")staff[l]=true;
    const char* categories[]={"PITCHING","BATTING","FIELDING","RUNNING"};for(int i=0;i<4;++i)if(name==categories[i]){
     if(staff[l])card.coach[i]=NativeUiText(node);else rating[l]=i;
    }
    if(name=="RATING" && rating[l]>=0)card.grades[rating[l]]=NativeUiText(node);
    if(name=="SCOUTINGLEVEL1")card.scouting=NativeUiText(node);
    if(name=="RECORDVALUE")card.record=NativeUiText(node);
   }
  }
 }
 if(card.menu[0].empty())return false;
 unsigned enabled=0;for(int i=0;i<10;++i)if(card.enabled[i])enabled|=1u<<i;g_team_manage_enabled=enabled;
 const auto nativeSelected=card.selected;const auto pending=g_team_manage_target.load();if(pending>=0 && pending<10)card.selected=pending;
 g_managed_school=card.school;
 if(!DrawTeamManagement(card))return false;
 presented.value=true;
 if(TraceStartup()){static std::string last;auto key=card.school.team.name+card.date+card.record+card.scouting+std::to_string(card.selected);if(key!=last){last=key;LOGR("team management menu: team=%s row=%d date=%s record=%s card.scouting=%s",card.school.team.name.c_str(),card.selected,card.date.c_str(),card.record.c_str(),card.scouting.c_str());LOGR("team management native selection: %d enabled=%03X",nativeSelected,g_team_manage_enabled.load());gpu::g_capture=true;}}
 return true;
}

bool DrawNativeStatisticsTable() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 // Player Info now composes its themed modal over this same table.
 StatisticsTableCard card;uint32_t root=0;
 const char* expected[]={"PLAYERSTATS","LEADERS","CAREERSTATS","INDCAREERSTATS"};
 for(int i=0;i<4;++i){auto r=g_stat_table_roots[i].load();if(FeVisibleScene(r,expected[i])){root=r;card.kind=i;break;}}
 if(!root)return false;
 const auto clean=[](const std::string& v){return v.find_first_not_of(" ")!=std::string::npos && v.size()<256 && std::all_of(v.begin(),v.end(),[](unsigned char c){return c>=32 && c<127;});};
 auto walk=[&](uint32_t r,auto visit){
  if(!valid(r))return;auto d=kernel::R32(r+0x24);if(d>=127)return;
  std::array<bool,128> shown{};shown[d]=true;
  std::array<int,128> xs{},ys{},groups{};xs.fill(-1);ys.fill(-1);
  for(auto n=kernel::R32(r+8),count=0u;count<16384 && valid(n) && kernel::R32(n+0x24)>d;n=kernel::R32(n+8),++count){
   auto l=kernel::R32(n+0x24);if(l>=128 || !l)continue;auto name=kernel::ReadCString(n+0x10,16);
   shown[l]=shown[l-1] && (kernel::R16(n+0x28)&1);xs[l]=xs[l-1];ys[l]=ys[l-1];groups[l]=groups[l-1];
   if(name=="PLAYERGRP")groups[l]=1;
   if(name=="HEADINGS")groups[l]=2;if(name=="STATS")groups[l]=3;
   int x,y;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){xs[l]=x;ys[l]=y;}
   if(shown[l])visit(n,name,xs[l],ys[l],groups[l]);
  }
 };
 walk(root,[&](uint32_t n,const std::string& name,int x,int y,int group){
  if(name=="CELL" || name=="FRANCHISE" || name=="TOURNAMENT" || name=="GRID"){
   int first=int(kernel::R32(n+0x74)),selected=int(kernel::R32(n+0x6C));
   if(card.kind==2)card.selections[std::clamp(group,0,1)]=selected-first;
   else {card.first=first;card.selected=selected-first;}
  }
  if(name.starts_with("X") || name=="CELL" || name=="GRID" || name=="VSCROLL")return;
  auto v=NativeUiText(n);if(!clean(v))return;
  if(name=="TEAMNAMES")card.team=v;if(name=="CONFERENCE" || name=="LEAGUETL")card.conference=v;
  if(name=="STATSSTATE" || name=="STATTL")card.mode=v;
  if(name=="NOLEADERS")card.empty=v;
  if(x>=0 && x<16 && y>=0 && y<32){if(card.kind==2){if(y<10)card.lists[std::clamp(group,0,1)][y]=v;}else card.rows[y][x]=v;}
 });
 card.title=card.kind==1?"LEADERS":card.kind==2?"CAREER STATS":card.kind==3?"PLAYER CAREER STATS":"PLAYER STATS";
 if(card.kind==0 || card.kind==3){
  auto title=g_recruit_title_root.load();walk(title,[&](uint32_t n,const std::string& name,int,int,int){if(name=="TITLE"){auto v=NativeUiText(n);if(clean(v))card.title=v;}});
  walk(g_stat_summary_root.load(),[&](uint32_t n,const std::string& name,int,int y,int group){
   auto v=NativeUiText(n);if(!clean(v))return;if(name=="NAME")card.player=v;
   if(name=="TX1" && y>=0 && y<8){if(group==2)card.labels[y]=v;if(group==3)card.values[y]=v;}
  });
 }
 uint32_t faceObject=0;
 // The stats summary owns the live FACE; the trade-block panel is a hidden sibling.
 if(card.kind==0 || card.kind==3)walk(g_stat_summary_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="FACE"){faceObject=kernel::R32(n+0x80);for(int i=0;i<4;++i){auto raw=kernel::R32(n+0x84+i*4);std::memcpy(&card.uv[i],&raw,4);}}});
 if(!faceObject && (card.kind==0 || card.kind==3))walk(g_stat_face_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="FACE" || name=="LARGEFACE"){faceObject=kernel::R32(n+0x80);for(int i=0;i<4;++i){auto raw=kernel::R32(n+0x84+i*4);std::memcpy(&card.uv[i],&raw,4);}}});
 if(!faceObject && (card.kind==0 || card.kind==3))walk(g_stat_playerbar_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="FACE" || name=="LARGEFACE"){faceObject=kernel::R32(n+0x80);for(int i=0;i<4;++i){auto raw=kernel::R32(n+0x84+i*4);std::memcpy(&card.uv[i],&raw,4);}}});
 struct StatsFace {uint32_t object=0,texture=0;std::string player;double ready=0;};static StatsFace face;
 if(face.object!=faceObject || face.player!=card.player)face={faceObject,0,card.player,PickerNow()+.05};
 if(faceObject && !face.texture && PickerNow()>=face.ready){
  std::vector<std::pair<uint32_t,int>> queue{{faceObject,0}};std::unordered_set<uint32_t> seen;
  for(size_t i=0;i<queue.size() && i<256;++i){auto [object,level]=queue[i];if(!valid(object) || !seen.insert(object).second)continue;gpu::TextureDesc desc;
   if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 && !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 && desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)){face.texture=object;break;}
   if(level<3)for(unsigned o=0;o<0x200;o+=4){auto next=kernel::R32(object+o);if(valid(next) && !seen.count(next))queue.emplace_back(next,level+1);}
  }
 }
 card.portrait=face.texture;
 if(card.kind==3){card.mode="CAREER TOTALS";if(card.team.empty())card.team=card.rows[1][1];}
 const LeagueSchool* statsSchool=SchoolFor(card.team);
 if(!statsSchool)for(const auto& school:LeagueSchools())if(school.abbreviation==card.team){statsSchool=&school;break;}
 if(statsSchool){card.team=statsSchool->name;card.color=statsSchool->color;}
 if(card.kind==0 && card.selected>=1 && card.selected<32 && !card.rows[card.selected][1].empty())card.player=card.rows[card.selected][1];
 auto summaryTitle=card.title;for(auto& c:summaryTitle)c=char(std::toupper(static_cast<unsigned char>(c)));
 const auto hasHeading=[&](const char* heading){return std::find(card.rows[0].begin(),card.rows[0].end(),heading)!=card.rows[0].end();};
 if(card.kind==0 && hasHeading("TC") && hasHeading("PO")){card.title="FIELDING STATS";summaryTitle=card.title;}
 if(card.kind==0 && (summaryTitle.find("FIELDING")!=std::string::npos || summaryTitle.find("BASERUNNING")!=std::string::npos)){
  card.labels={};card.values={};int item=0;
  for(int col=2;col<16 && item<8;++col){const auto& label=card.rows[0][col];if(label.empty() || label=="POS")continue;
   card.labels[item]=label;if(card.selected>=1 && card.selected<32)card.values[item]=card.rows[card.selected][col];++item;}
 }
 if(!DrawStatisticsTable(card))return false;
 if(TraceStartup()){static std::string last;std::string key=card.title+card.team+card.mode+std::to_string(card.selected)+std::to_string(card.first)+std::to_string(card.portrait);for(auto& row:card.rows)for(auto& v:row)key+=v+"|";if(last!=key){last=key;LOGR("statistics table: kind=%d title=%s team=%s selected=%d first=%d mode=%s",card.kind,card.title.c_str(),card.team.c_str(),card.selected,card.first,card.mode.c_str());LOGR("statistics portrait: object=%08X texture=%08X player=%s",faceObject,card.portrait,card.player.c_str());for(int i=0;i<13;++i){std::string line;for(auto& v:card.rows[i])line+=v+" | ";LOGR("statistics row %d: %s",i,line.c_str());}gpu::g_capture=true;}}
 return true;
}
bool DrawNativeDynastyOptions(){
 if(!overhaul::Active())return false;auto root=g_dynasty_options_root.load();if(!FeScene(root,"FGENOPTIONS"))return false;
 DynastyOptionsCard card;std::vector<int> stars;int cover=0;bool covered=false;

 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  auto marker=kernel::R32(n+0x6C);auto v=(marker==0xBB55BB50u || marker==0xBB55BB54u)?NativeUiText(n):std::string();
  if(x==0 && y>=0 && y<5 && FeClean(v)){card.menu[y]=v;card.enabled[y]=(kernel::R16(n+0x28)&2)!=0;}
  if(name=="CELL"){auto i=kernel::R32(n+0x6C);if(i<5)card.selected=int(i);}
  if(name=="DATE")card.date=v;if(name=="TEAMNAME")card.school.team.name=v;if(name=="TEAMNAME1")card.school.team.nickname=v;
  if(name=="CONFERENCERANKN")card.school.standing=v;if(name=="NATIONARANKNO")card.school.team.rank=v;if(name=="RECORDVALUE")card.record=v;
  if(name.starts_with("SMALLSTAR"))stars.push_back(int16_t(kernel::R16(n+0x2A))+kernel::R16(n+0x2E)/2);
  if(name=="COVER" && kernel::R16(n+0x2E)){cover=int16_t(kernel::R16(n+0x2A));covered=true;}

 });
 if(card.menu[0].empty())return false;
 if(!stars.empty())card.school.prestige=int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
 const auto* school=SchoolFor(card.school.team.name,card.school.team.nickname);card.school.team.rank=RankFor(school,card.school.team.rank);if(school){card.school.color=school->color;card.school.location=school->location;}
 FeWalk(g_dynasty_rightbar_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="RECORDVALUE")card.record=NativeUiText(n);});
 if(!DrawDynastyOptions(card))return false;
 if(TraceStartup()){static std::string last;auto key=card.school.team.name+card.date+card.record+std::to_string(card.selected);if(last!=key){last=key;LOGR("dynasty options: team=%s row=%d record=%s menu=%s|%s|%s|%s|%s",card.school.team.name.c_str(),card.selected,card.record.c_str(),card.menu[0].c_str(),card.menu[1].c_str(),card.menu[2].c_str(),card.menu[3].c_str(),card.menu[4].c_str());gpu::g_capture=true;}}
 return true;
}

bool DrawNativeStatsLanding() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 auto root=g_stats_landing_root.load();if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="INFOMENU")return false;
 auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 StatsLandingCard card;std::array<bool,128> shown{},menu{};shown[depth]=true;std::array<int,128> row{},rating{};row.fill(-1);rating.fill(-1);std::vector<int> stars;int cover=0;bool covered=false;

 for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
  shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);row[l]=row[l-1];rating[l]=rating[l-1];menu[l]=menu[l-1];if(!shown[l])continue;
  if(name=="FRANCHISE"){menu[l]=true;card.selected=std::clamp(int(kernel::R32(node+0x6C)),0,3);}
  int y=-1;if(menu[l] && std::sscanf(name.c_str(),"X0Y%d",&y)==1)row[l]=y;
  // Diagnostic layout access on a disposable preseason campaign only. This is
  // never enabled by normal launches and does not certify award eligibility.
  if(dev::Env("MVP06_TEST_AWARD_LAYOUT")&&menu[l]&&row[l]==3)kernel::W16(node+0x28,kernel::R16(node+0x28)|2);

  if(row[l]>=0 && row[l]<4 && (name=="TOP25RANK" || name=="STANDINGS" || name=="STATISTICS" || name=="AWARDS")){card.menu[row[l]]=NativeUiText(node);card.enabled[row[l]]=(kernel::R16(node+0x28)&2)!=0;}
  if(name=="DATE")card.date=NativeUiText(node);
  if(name=="TEAMNAME")card.school.team.name=NativeUiText(node);if(name=="TEAMNAME1")card.school.team.nickname=NativeUiText(node);
  if(name=="CONFERENCERANKN")card.school.standing=NativeUiText(node);if(name=="NATIONARANKNO")card.school.team.rank=NativeUiText(node);
  const char* categories[]={"PITCHING","BATTING","FIELDING","RUNNING"};for(int i=0;i<4;++i)if(name==categories[i])rating[l]=i;
  if(name=="RATING" && rating[l]>=0)card.grades[rating[l]]=NativeUiText(node);
  if(name=="RECORDVALUE")card.record=NativeUiText(node);
  if(name.starts_with("SMALLSTAR"))stars.push_back(int16_t(kernel::R16(node+0x2A))+kernel::R16(node+0x2E)/2);
  if(name=="COVER" && kernel::R16(node+0x2E)){cover=int16_t(kernel::R16(node+0x2A));covered=true;}
 }
 if(card.school.team.name.empty())return false;
 if(!stars.empty())card.school.prestige=int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
 const auto* school=SchoolFor(card.school.team.name,card.school.team.nickname);card.school.team.rank=RankFor(school,card.school.team.rank);if(school){card.school.color=school->color;card.school.location=school->location;}

 auto bar=g_dynasty_rightbar_root.load();
 if(valid(bar) && (kernel::R16(bar+0x28)&1)){
  auto bd=kernel::R32(bar+0x24);
  if(bd<127){shown.fill(false);shown[bd]=true;rating.fill(-1);std::array<bool,128> staff{};
   for(auto node=kernel::R32(bar+8),n=0u;n<2048 && valid(node) && kernel::R32(node+0x24)>bd;node=kernel::R32(node+8),++n){
    auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);
    shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);staff[l]=staff[l-1];rating[l]=rating[l-1];if(!shown[l])continue;
    if(name=="STAFF")staff[l]=true;
    const char* categories[]={"PITCHING","BATTING","FIELDING","RUNNING"};for(int i=0;i<4;++i)if(name==categories[i]){
     if(staff[l])card.coach[i]=NativeUiText(node);else rating[l]=i;
    }
    if(name=="RATING" && rating[l]>=0)card.grades[rating[l]]=NativeUiText(node);
    if(name=="SCOUTINGLEVEL1")card.scouting=NativeUiText(node);
    if(name=="RECORDVALUE")card.record=NativeUiText(node);
   }
  }
 }
 if(!DrawStatsLanding(card))return false;
 if(TraceStartup()){static std::string last;auto key=card.school.team.name+card.date+card.record+card.scouting+std::to_string(card.selected);if(key!=last){last=key;LOGR("stats landing: team=%s row=%d date=%s record=%s scouting=%s",card.school.team.name.c_str(),card.selected,card.date.c_str(),card.record.c_str(),card.scouting.c_str());gpu::g_capture=true;}}
 return true;
}
bool DrawNativeRecruiting() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 auto root=g_recruiting_root.load();if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="FWATCHLIST")return false;
 auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 RecruitingCard card;std::array<bool,128> shown{};shown[depth]=true;std::array<int,128> row{};row.fill(-1);std::vector<int> stars;int cover=0;bool covered=false;
 for(auto node=kernel::R32(root+8),n=0u;n<4096 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  auto l=kernel::R32(node+0x24);if(l>=128)continue;auto name=kernel::ReadCString(node+0x10,16);shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);row[l]=row[l-1];if(!shown[l])continue;
  if(name=="CELL")card.selected=std::clamp(int(kernel::R32(node+0x6C)),0,3);
  int y=-1;if(std::sscanf(name.c_str(),"X0Y%d",&y)==1)row[l]=y;
  if(row[l]>=0 && row[l]<4 && (name=="TOP100" || name=="ALL" || name=="INTERESTED" || name=="TARGETED"))card.menu[row[l]]=NativeUiText(node);
  if(name=="TEAMNAME")card.school.team.name=NativeUiText(node);if(name=="TEAMNAME1")card.school.team.nickname=NativeUiText(node);
  if(name=="CONFERENCERANKN")card.school.standing=NativeUiText(node);if(name=="NATIONARANKNO")card.school.team.rank=NativeUiText(node);
  if(name=="RECRUITPOINTS1")card.points=NativeUiText(node);
  if(name.starts_with("SMALLSTAR"))stars.push_back(int16_t(kernel::R16(node+0x2A))+kernel::R16(node+0x2E)/2);
  if(name=="COVER" && kernel::R16(node+0x2E)){cover=int16_t(kernel::R16(node+0x2A));covered=true;}
 }
 if(card.school.team.name.empty())return false;
 if(!stars.empty())card.school.prestige=int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
 const auto* school=SchoolFor(card.school.team.name,card.school.team.nickname);card.school.team.rank=RankFor(school,card.school.team.rank);if(school){card.school.color=school->color;card.school.location=school->location;}
 if(!DrawRecruiting(card))return false;
 if(TraceStartup()){static std::string last;auto key=card.school.team.name+card.points+std::to_string(card.selected);if(key!=last){last=key;LOGR("recruiting: team=%s row=%d points=%s rank=%s",card.school.team.name.c_str(),card.selected,card.points.c_str(),card.school.team.rank.c_str());gpu::g_capture=true;}}
 return true;
}
bool DrawNativeBoxScore() {
 if(!overhaul::Active())return false;
 const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
 const auto root=g_box_score_root.load();if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="BOXSCORES")return false;
 const auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 BoxScoreCard card;card.teams=g_pregame_teams;
 std::array<bool,128> shown{};shown[depth]=true;std::array<int,128> kind{},row{},col{};row.fill(-1);col.fill(-1);
 for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
  const auto l=kernel::R32(node+0x24);if(l>=128)continue;const auto name=kernel::ReadCString(node+0x10,16);
  shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);kind[l]=kind[l-1];row[l]=row[l-1];col[l]=col[l-1];
  if(name=="CELL"){kind[l]=1;card.first=int(kernel::R32(node+0x74));card.selected=int(kernel::R32(node+0x6C))-card.first;card.total=std::clamp(int(kernel::R32(node+0x50)),0,10000);}
  if(name=="FIRSTCELL")kind[l]=2;if(name=="BOXSCORE")kind[l]=3;
  int x=-1,y=-1;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){col[l]=x;row[l]=y;}
  if(!shown[l] || row[l]<0 || col[l]<0)continue;
  if(name.rfind("X",0)!=0 && name!="VSCROLL" && name!="CELL" && name!="FIRSTCELL" && name!="BOXSCORE"){
   auto value=NativeUiText(node);if(value.empty())continue;
   if(kind[l]==3 && row[l]<3 && col[l]<13)card.score[row[l]][col[l]]=value;
   if((kind[l]==1 || kind[l]==2) && row[l]<11 && col[l]<9)card.rows[row[l]][kind[l]==2?0:col[l]]=value;
  }
 }
 if(!DrawBoxScore(card))return false;
 if(TraceStartup()){static std::string last;auto key=std::to_string(card.first)+"/"+std::to_string(card.selected);for(auto& r:card.rows)for(auto& v:r)key+=v+"|";if(key!=last){last=key;LOGR("box score: first=%d selected=%d",card.first,card.selected);for(int r=0;r<11;++r)LOGR("box row %d: %s | %s | %s | %s | %s | %s | %s | %s | %s",r,card.rows[r][0].c_str(),card.rows[r][1].c_str(),card.rows[r][2].c_str(),card.rows[r][3].c_str(),card.rows[r][4].c_str(),card.rows[r][5].c_str(),card.rows[r][6].c_str(),card.rows[r][7].c_str(),card.rows[r][8].c_str());gpu::g_capture=true;}}
 return true;
}
bool DrawNativeFinalScore() {
  if(!overhaul::Active())return false;
  const auto valid=[](uint32_t p){return p>=0x80000000u && p<0x90000000u && mem::IsCommitted(p+0x98);};
  const auto root=g_final_score_root.load();
  if(!valid(root) || (kernel::R16(root+0x28)&3)!=3 || kernel::ReadCString(root+0x10,16)!="FRESULTS")return false;
  const auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
  FinalScoreCard card;card.teams=g_pregame_teams;
  std::array<bool,128> shown{};shown[depth]=true;
  std::array<int,128> player{},kind{},row{},column{};player.fill(-1);row.fill(-1);column.fill(-1);
  std::array<uint32_t,2> objects{};
  std::array<int,128> resultSide{};resultSide.fill(-1);
  std::array<std::string,3> choices;std::string intervene;
  bool choosing=false;
  for(auto node=kernel::R32(root+8),n=0u;n<8192 && valid(node) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
    const auto l=kernel::R32(node+0x24);if(l>=128)continue;const auto name=kernel::ReadCString(node+0x10,16);
    shown[l]=shown[l-1] && (kernel::R16(node+0x28)&1);
    player[l]=player[l-1];kind[l]=kind[l-1];row[l]=row[l-1];column[l]=column[l-1];
    resultSide[l]=resultSide[l-1];
    if(!shown[l])continue;
    if(name=="HOME")resultSide[l]=1;if(name=="AWAY")resultSide[l]=0;
    if(name=="AWAYSCORE")card.scores[0]=NativeUiText(node);
    if(name=="HOMESCORE")card.scores[1]=NativeUiText(node);
    if(name=="WIN" || name=="WINLOSS"){auto value=NativeUiText(node);if(!value.empty())card.outcome=value;}
    if((name=="WPLPINFO" || name=="WPLPLABEL") && resultSide[l]>=0)card.results[resultSide[l]]+=NativeUiText(node)+" ";
    if(name=="RESULTSOPTION"){kind[l]=4;card.selected=std::clamp(int(kernel::R32(node+0x70)),0,2);}
    if(kind[l]==4 && (name=="BOXSCORE" || name=="ADVANCE" || name=="REVENUE"))card.actions.push_back(NativeUiText(node));
    if(name=="LEFTPLAYER")player[l]=0;if(name=="RIGHTPLAYER")player[l]=1;
    if(name=="CELL1" || name=="CELL2")kind[l]=1;
    if(name=="BOXSCOREGRID")kind[l]=2;
    if(name=="INTERVENEOPTION"){kind[l]=3;choosing=true;card.selected=std::clamp(int(kernel::R32(node+0x70)),0,2);}
    int x=-1,y=-1;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){column[l]=x;row[l]=y;}
    if(name=="SIMSPEED"){const auto value=NativeUiText(node);if(!value.empty())card.speed=value;}
    if(name=="INTERVENE")intervene=NativeUiText(node);
    if(kind[l]==3 && column[l]>=0 && column[l]<3 && (name=="PLAY" || name=="PERATBAT" || name=="CONTINUE"))choices[column[l]]=NativeUiText(node);
    if(kind[l]==2 && row[l]>=0 && row[l]<3 && column[l]>=0 && column[l]<13 && (name=="TEXT" || name=="TX1"))card.lineScore[row[l]][column[l]]=NativeUiText(node);
    const int pi=player[l];if(pi<0)continue;auto& out=card.players[pi];
    if(name=="PLAYERNAME")out.name=NativeUiText(node);
    if(name=="POSITION")card.positions[pi]=NativeUiText(node);
    if(kind[l]==1 && row[l]>=0 && row[l]<5 && column[l]>=0 && column[l]<2 &&
       (name=="NUMBER" || name=="TX1" || name=="TL1" || name=="BB" || name=="HR" || name=="ER" || name=="SO" || name=="HITS"))out.stats[row[l]][column[l]]=NativeUiText(node);
    if(name=="METERBLOCK")card.stamina[pi]=std::clamp(1.f-kernel::R16(node+0x2E)/100.f,0.f,1.f);
    if(name=="FACE"){objects[pi]=kernel::R32(node+0x80);for(int k=0;k<4;++k){auto bits=kernel::R32(node+0x84+4*k);std::memcpy(&out.uv[k],&bits,4);}}
  }
  std::reverse(card.actions.begin(),card.actions.end());
  for(int i=0;i<2;++i)if(const auto* school=SchoolFor(card.teams[i].name)){card.teams[i].name=school->name;card.teams[i].nickname=school->nickname;card.teams[i].rank=RankFor(school);card.colors[i]=school->color;}
  for(int i=0;i<2;++i)if(card.results[i].empty()){
    const int away=std::atoi(card.scores[0].c_str()),home=std::atoi(card.scores[1].c_str());
    const bool won=i==0?away>home:home>away;
    card.results[i]=(away==home?std::string():won?"WP: ":"LP: ")+card.players[i].name;
  }
  struct PortraitCache {
    uint32_t root=0,object=0,texture=0;
    std::string player;
    std::chrono::steady_clock::time_point readyAt{};
  };
  static std::array<PortraitCache,2> cache;
  for(int i=0;i<2;++i) {
    auto& out=card.players[i];
    auto& saved=cache[i];const auto& player=card.players[i].name;
    if(saved.root!=root || saved.object!=objects[i] || saved.player!=player ||
       (saved.texture && (!valid(saved.texture) || (kernel::R32(saved.texture)&0x70000)!=0x40000)))
      saved={root,objects[i],0,player,std::chrono::steady_clock::now()+std::chrono::milliseconds(50)};
    // A newly bound FACE initially contains the common placeholder. The native
    // render pass replaces it on the next frame; do not show that placeholder.
    if(!saved.texture && std::chrono::steady_clock::now()>=saved.readyAt && valid(objects[i])) {
      std::vector<std::pair<uint32_t,int>> queue{{objects[i],0}};std::unordered_set<uint32_t> visited;
      for(size_t n=0;n<queue.size() && n<256;++n) {
        const auto [object,level]=queue[n];if(!valid(object) || !visited.insert(object).second)continue;
        gpu::TextureDesc desc;
        if((kernel::R32(object)&0x70000)==0x40000 && gpu::Describe(object,desc) && !desc.cube && desc.depth==1 &&
           !gpu::Format(desc.format).depth && !gpu::Format(desc.format).compressed && gpu::Format(desc.format).bits==32 &&
           desc.width>=64 && desc.width<=256 && desc.height>=64 && desc.height<=256 && mem::IsCommitted(desc.data)) {
          saved.texture=object;break;
        }
        if(level<3)for(unsigned offset=0;offset<0x200;offset+=4){auto next=kernel::R32(object+offset);if(valid(next) && !visited.count(next))queue.emplace_back(next,level+1);}
      }
    }
    out.portrait=saved.texture;
  }



  if(!DrawFinalScore(card))return false;
  if(TraceStartup()){
    static std::string last;auto key=card.scores[0]+"/"+card.scores[1]+card.outcome+std::to_string(card.selected)+std::to_string(card.actions.size());
    for(const auto& line:card.lineScore)for(const auto& value:line)key+=value+"|";
    for(const auto& p:card.players){key+=p.name+std::to_string(p.portrait);for(const auto& stat:p.stats)key+=stat[0]+stat[1];}
    if(last!=key){last=key;LOGR("final score: speed=%s actions=%zu selected=%d score=%s/%s pitchers=%s/%s stamina=%.3f/%.3f",card.speed.c_str(),card.actions.size(),card.selected,card.scores[0].c_str(),card.scores[1].c_str(),card.players[0].name.c_str(),card.players[1].name.c_str(),card.stamina[0],card.stamina[1]);gpu::g_capture=true;}
  }
  return true;
}

void ResetActionNavigation(){g_action_navigation=0;}
void MapPlaySimPad(int port, input::Pad& pad) {
 if(port || !pad.connected || !overhaul::Active() || SubmenuPopupVisible())return;
 auto live=[](uint32_t root,const char* name){return root>=0x80000000u && root<0x90000000u && mem::IsCommitted(root+0x98) && (kernel::R16(root+0x28)&3)==3 && kernel::ReadCString(root+0x10,16)==name;};
 const auto owner=g_action_navigation.load();
 // The final Play/Coach/Sim grid is already horizontal. Only the multi-game
 // list needs rotated axes; Coach gets horizontal aliases for its vertical list.
 const bool multi=owner==1 && live(g_play_sim_root.load(),"FPLAYORSIMMULTI");
 const bool coach=owner==3 && live(g_coach_root.load(),"FSIMPERATBAT");
 const bool createEdit=owner==4 && live(g_create_edit_root.load(),"CPMENU");
 // A held while entering this menu belongs to its parent. Arm only after
 // release, otherwise the loading cover can hide a menu that never activated.
 static uint32_t confirmRoot=0;static bool confirmWasDown=false,confirmArmed=false;
 if(!createEdit)confirmRoot=0;
 else{
  const auto root=g_create_edit_root.load();const bool down=pad.analog[0]!=0;
  if(confirmRoot!=root){confirmRoot=root;confirmWasDown=down;confirmArmed=!down;}
  else{if(!down)confirmArmed=true;if(confirmArmed && down && !confirmWasDown)g_editor_transition_until=PickerNow()+10;confirmWasDown=down;}
 }
 if(!multi && !coach && !createEdit)return;
 auto original=pad.buttons;pad.buttons&=~uint16_t(15);
 if(original&4)pad.buttons|=1;if(original&8)pad.buttons|=2;
 if(multi){if(original&1)pad.buttons|=4;if(original&2)pad.buttons|=8;
  auto horizontal=pad.lx;pad.lx=int16_t(std::clamp(-int(pad.ly),-32767,32767));pad.ly=int16_t(std::clamp(-int(horizontal),-32767,32767));
 }else{pad.buttons|=original&3;if(std::abs(int(pad.lx))>16000){pad.ly=int16_t(std::clamp(-int(pad.lx),-32767,32767));pad.lx=0;}}
}

bool DrawNativePlaySim() {
  if (!overhaul::Active()) return false;
  const auto root=g_play_sim_root.load();
  const auto valid=[](uint32_t node){return node>=0x80000000u && node<0x90000000u && mem::IsCommitted(node+0x74);};
  if (!valid(root) || (kernel::R16(root+0x28)&3)!=3 ||
      kernel::ReadCString(root+0x10,16)!="FPLAYORSIMMULTI") return false;
  const auto depth=kernel::R32(root+0x24);
  if (depth>=127) return false;
  PlaySimCard card;
  std::array<bool,128> shown{};
  std::array<int,128> row{},side{},column{};
  row.fill(-1);side.fill(-1);column.fill(-1);shown[depth]=true;
  std::array<PlaySimCard,8> games;
  uint32_t gridDepth=128;
  int selectedRow=0;
  unsigned gridRows=0;
  for (auto node=kernel::R32(root+8),n=0u;
       n<4096 && valid(node) && kernel::R32(node+0x24)>depth;
       node=kernel::R32(node+8),++n) {
    const auto level=kernel::R32(node+0x24);
    if (level>=128) continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    shown[level]=shown[level-1] && (kernel::R16(node+0x28)&1);
    row[level]=row[level-1];side[level]=side[level-1];column[level]=column[level-1];
    // Offscreen game rows remain bound and must count toward game navigation.
    if (name=="GAMEGRID") {gridDepth=level;selectedRow=int(kernel::R32(node+0x6C));gridRows=kernel::R32(node+0x50);}
    if (level==gridDepth+1) {
      int index=-1;
      if (std::sscanf(name.c_str(),"X0Y%d",&index)==1 && index>=0 && index<8) row[level]=index;
    }
    if (shown[level] && name=="DATE") card.date=NativeUiText(node);
    const int index=row[level];
    if (index<0 || index>=8) continue;
    auto& game=games[index];
    if (level>gridDepth+1 && name=="X0Y0") column[level]=0;
    if (level>gridDepth+1 && name=="X1Y0") column[level]=1;
    if (name=="AWAY") side[level]=0;
    if (name=="HOME") side[level]=1;
    if (name=="TEAM" && side[level]>=0) game.teams[side[level]].name=NativeUiText(node);
    if (name=="TIME") game.time=NativeUiText(node);
    if (shown[level] && name=="SCORE" && column[level]>=0)
      game.scores[column[level]]=NativeUiText(node);
    // The native grids store vertical row at +6C, horizontal column at +70.
    if (name=="CELL") game.selected=int(kernel::R32(node+0x70));
    if (shown[level] && name=="PLAY") {
      const auto text=NativeUiText(node);
      if (!text.empty()) game.actions[0]=text;
    }
    if (shown[level] && name=="SIM") {
      const auto text=NativeUiText(node);
      if (!text.empty()) game.actions[1]=text;
    }
  }
  if (TraceStartup()) {
    static std::string previous;
    const auto state=std::to_string(kernel::R16(root+0x28))+card.date+games[0].teams[0].name+games[0].teams[1].name+std::to_string(selectedRow)+std::to_string(games[0].selected);
    if (state!=previous) {
      previous=state;
      LOGR("play sim binding: flags=%u date=%s gridDepth=%u rows=%u row=%d away=%s home=%s time=%s action=%d",
           kernel::R16(root+0x28),card.date.c_str(),gridDepth,gridRows,selectedRow,games[0].teams[0].name.c_str(),games[0].teams[1].name.c_str(),games[0].time.c_str(),games[0].selected);
    }
  }
  if (selectedRow<0 || selectedRow>=8 || card.date.empty()) return false;
  int count=0;
  for (unsigned i=0;i<std::min(8u,gridRows);++i)
    if (!games[i].teams[0].name.empty() && !games[i].teams[1].name.empty()) ++count;
  auto selected=games[selectedRow];
  if (count && (selected.teams[0].name.empty() || selected.teams[1].name.empty())) return false;
  selected.date=card.date;selected.game=selectedRow;selected.games=count;
  for (int i=0;i<2;++i) {
    auto& team=selected.teams[i];
    const auto* school=SchoolFor(team.name);
    if (!school) for (const auto& entry:LeagueSchools())
      if (_stricmp(entry.abbreviation.c_str(),team.name.c_str())==0) {school=&entry;break;}
    if (school) {
      team.name=school->name;team.nickname=school->nickname;team.rank=RankFor(school);
      selected.colors[i]=school->color;
      DynastyHomeCard record;
      ReadDynastySchedule(record,*school);
      selected.records[i]=record.records[0];
    }
  }
  if (!DrawPlaySim(selected)) return false;
  g_action_navigation=1;
  SuppressNativeUiScene(root);
  if (TraceStartup()) {
    static std::string last;
    const auto key=selected.date+selected.teams[0].name+selected.teams[1].name+std::to_string(selected.selected)+selected.scores[0]+selected.scores[1];
    if (key!=last) {
      last=key;
      LOGR("play sim: %s %s at %s time=%s action=%d game=%d/%d",selected.date.c_str(),
           selected.teams[0].name.c_str(),selected.teams[1].name.c_str(),selected.time.c_str(),selected.selected,selected.game+1,selected.games);
      gpu::g_capture=true;
    }
  }
  return true;
}

bool DrawNativeDynastyCalendar() {
  const auto root = g_calendar_root.load(), gameRoot = g_calendar_game_root.load();
  const auto valid = [](uint32_t p) {
    return p >= 0x80000000u && p < 0x90000000u && mem::IsCommitted(p+0x74);
  };
  if (!valid(root) || !valid(gameRoot) || !(kernel::R16(root+0x28)&1) ||
      !(kernel::R16(gameRoot+0x28)&1) || kernel::ReadCString(root+0x10,16)!="CALENDAR" ||
      kernel::ReadCString(gameRoot+0x10,16)!="GAMECALENDAR" || !phase9::DynastyMenuStable()) return false;
  const auto world = kernel::R32(0x6c6540);
  if (!world || !mem::IsCommitted(world+8)) return false;
  const auto users = kernel::R32(world+8);
  if (!users || !mem::IsCommitted(users+11) || !kernel::R32(users)) return false;
  const auto id = kernel::R32(users+8);
  const auto& schools = LeagueSchools();
  const auto user = std::find_if(schools.begin(),schools.end(),[&](const LeagueSchool& s){return s.index==id;});
  if (user==schools.end()) return false;

  DynastyCalendarCard card;
  card.team.name = user->name;
  card.team.nickname = user->nickname;
  card.team.rank = RankFor(&*user);
  card.color = user->color;
  const auto depth=kernel::R32(root+0x24);
  if (depth>=127) return false;
  std::array<bool,128> visible{};
  visible[depth]=true;
  for (auto node=kernel::R32(root+8), count=0u;
       count<4096 && valid(node) && kernel::R32(node+0x24)>depth;
       node=kernel::R32(node+8),++count) {
    const auto level=kernel::R32(node+0x24);
    if (level>=128) continue;
    const auto name=kernel::ReadCString(node+0x10,16);
    visible[level]=visible[level-1] && (kernel::R16(node+0x28)&1);
    if (!visible[level]) continue;
    if (name=="MONTH") card.month=NativeUiText(node);
    if (name=="YEAR") card.year=NativeUiText(node);
    if (name=="CELLHIGHLIGHTS") {
      // This FEL grid stores week first and weekday second.
      const auto week=kernel::R32(node+0x6c), weekday=kernel::R32(node+0x70);
      if (week<6 && weekday<7) card.selected=int(week*7+weekday);
    }
  }
  if (card.month.size()<3 || card.year.empty() || card.selected<0) return false;
  std::string prefix=card.month.substr(0,3);
  for (auto& ch : prefix) ch=char(std::toupper(static_cast<unsigned char>(ch)));
  const char* months[]={"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE","JULY","AUGUST",
                        "SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
  unsigned month=0;
  for (unsigned i=0;i<12;++i)
    if (std::string_view(months[i]).starts_with(prefix)) {month=i+1;break;}
  if (!month) return false;
  card.month=months[month-1];
  const int year=std::atoi(card.year.c_str());
  using namespace std::chrono;
  if (year<1900 || year>2100) return false;
  const auto first=std::chrono::year{year}/month/1;
  const auto offset=weekday{sys_days{first}}.c_encoding();
  const auto last=unsigned(year_month_day_last{first.year(),month_day_last{first.month()}}.day());
  for (unsigned day=1;day<=last;++day){card.days[offset+day-1].number=int(day);card.days[offset+day-1].available=DynastyDateAvailable(unsigned(year),month,day);}
  DynastyHomeCard season;
  std::vector<DynastyFixture> fixtures;
  ReadDynastySchedule(season,*user,&fixtures);
  card.record=season.records[0];
  std::stable_sort(fixtures.begin(),fixtures.end(),[](const auto& a,const auto& b){
    return a.day!=b.day ? a.day<b.day : a.time<b.time;
  });
  for (const auto& fixture : fixtures) {
    const auto date=DynastyDate(fixture.day);
    if (int(date.year())!=year || unsigned(date.month())!=month) continue;
    auto cell=std::find_if(card.days.begin(),card.days.end(),[&](const auto& day){return day.number==int(unsigned(date.day()));});
    if (cell==card.days.end() || cell->games++) continue;
    const auto opponent=std::find_if(schools.begin(),schools.end(),[&](const auto& s){return s.index==fixture.opponent;});
    if (opponent==schools.end()) {cell->opponent="Opponent TBD";cell->rank="NR";}
    else {
      cell->opponent=opponent->name;
      cell->nickname=opponent->nickname;
      cell->rank=RankFor(&*opponent);
    }
    cell->home=fixture.home;
    cell->result=fixture.result;
    cell->stage=fixture.stage;
  }
  if (!DrawDynastyCalendar(card)) return false;
  g_calendar_snapshot=card;
  g_calendar_snapshot_root=root;
  if (TraceStartup()) {
    static std::string last;
    const auto& day=card.days[card.selected];
    const auto key=card.month+card.year+std::to_string(card.selected)+day.opponent+day.result;
    if (key!=last) {
      last=key;
      LOGR("dynasty calendar: %s %s cell=%d day=%d %s %s opponent=%s %s games=%zu",
           card.month.c_str(),card.year.c_str(),card.selected,day.number,card.team.name.c_str(),
           card.team.rank.c_str(),day.opponent.c_str(),day.rank.c_str(),fixtures.size());
      gpu::g_capture=true;
    }
  }
  return true;
}

void MapDashboardPad(int port,input::Pad& pad) {
 if(port!=0 || !overhaul::Active())return;
 const auto root=g_dynasty_home_root.load();
 const bool active=root && mem::IsCommitted(root+0x74) && (kernel::R16(root+0x28)&3)==3 &&
  kernel::ReadCString(root+0x10,16)=="FMGRDECISION" && phase9::DynastyMenuStable() && !SubmenuPopupVisible();
 static int held=0,observed=-1;static bool confirm=false;
 static double repeatAt=0,pulseEnd=0,nextPulse=0,progressAt=0;
 if(!active){held=0;confirm=false;observed=-1;pulseEnd=nextPulse=0;return;}
 const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 const int native=std::clamp(g_dynasty_home_row.load(),0,4);
 if(native!=observed){observed=native;progressAt=now;}
 int section=std::clamp(g_dashboard_section.load(),0,4),direction=0;
 if(pad.buttons&1)direction=-1;else if(pad.buttons&2)direction=1;
 if(!direction && std::abs(int(pad.ly))>16000)direction=pad.ly>0?-1:1;
 pad.buttons&=~uint16_t(15);pad.lx=pad.ly=0;
 if(direction && (direction!=held || now>=repeatAt)){
  section=(section+direction+5)%5;g_dashboard_section=section;
  repeatAt=now+(direction!=held?.35:.18);progressAt=now;confirm=false;
 }
 held=direction;
 // The approved home has five native rows, with no separate Overview row.
 // Back belongs to the native Quit action advertised by its footer.
 if(pad.analog[1]){g_dashboard_section=native;confirm=false;pulseEnd=nextPulse=0;return;}
 const int target=section;
 confirm=confirm || pad.analog[0]!=0;pad.analog[0]=0;
 if(native!=target){
  if(now-progressAt>1.5){g_dashboard_section=native;confirm=false;pulseEnd=nextPulse=0;return;}
  if(now>=nextPulse){pulseEnd=now+.055;nextPulse=now+.14;}
  if(now<pulseEnd)pad.buttons|=target>native?2:1;
 }else{
  pulseEnd=nextPulse=0;
  if(confirm){pad.analog[0]=255;confirm=false;}
 }
}

bool DrawNativeDynastyHome() {
  const auto root = g_dynasty_home_root.load();
  if (!root || !mem::IsCommitted(root + 0x50)) return false;
  const auto flags = kernel::R16(root+0x28)&3;
  const bool stable = phase9::DynastyMenuStable();
  if (TraceStartup()) {
    static int last = -1;
    const int state = flags + 4*int(stable);
    if (state != last) {
      last = state;
      LOGR("dynasty home gate: root=%08X flags=%u stable=%d",root,flags,int(stable));
    }
  }
  if (!(flags&1) || !stable) return false;
  const auto scene = kernel::ReadCString(root+0x10,16);
  if (scene != "FMGRDECISION" && scene != "DYNASTYHOME") return false;
  const auto depth = kernel::R32(root+0x24);
  if (depth >= 127) return false;
  DynastyHomeCard card;card.section=std::clamp(g_dashboard_section.load(),0,4);
  std::array<bool,128> shown{}, inMenu{};
  std::array<int,128> rows{}, ratings{};
  rows.fill(-1);
  ratings.fill(-1);
  shown[depth] = true;
  card.menu.resize(5);
  std::vector<int> stars;
  int cover = 0;
  bool covered = false;
  for (auto node=kernel::R32(root+8), count=0u;
       node && count<4096 && mem::IsCommitted(node+0x74) && kernel::R32(node+0x24)>depth;
       node=kernel::R32(node+8),++count) {
    const auto level = kernel::R32(node+0x24);
    if (level >= 128) continue;
    const auto name = kernel::ReadCString(node+0x10,16);
    shown[level] = shown[level-1] && (kernel::R16(node+0x28)&1);
    inMenu[level] = inMenu[level-1] || name == "MAINMENU";
    rows[level] = rows[level-1];
    ratings[level] = ratings[level-1];
    // Hidden legacy artwork remains a native data source for the dashboard.
    if (name == "MAINMENU") card.selected = g_dynasty_home_row.load();
    if (inMenu[level]) {
      int row;
      if (std::sscanf(name.c_str(),"X0Y%d",&row)==1) rows[level] = row;
      row = rows[level];
      if (row >= 0 && size_t(row) < card.menu.size() &&
          (name=="SCHEDULE" || name=="RECRUIT" || name=="INFOMENU" || name=="TEAMMANAGEMENT" || name=="OPTIONS"))
        card.menu[row] = NativeUiText(node);
    }
    if (name=="DATE") card.date = NativeUiText(node);
    if (name=="TEAMNAME") card.school.team.name = NativeUiText(node);
    if (name=="TEAMNAME1") card.school.team.nickname = NativeUiText(node);
    if (name=="CONFERENCERANKN") card.school.standing = NativeUiText(node);
    if (name=="NATIONARANKNO") card.school.team.rank = NativeUiText(node);
    if (name=="RECORDVALUE") card.school.record = NativeUiText(node);
    const char* categories[] = {"OVERALL","PITCHING","BATTING","FIELDING","RUNNING"};
    for (int i=0;i<5;++i)
      if (name==categories[i]) ratings[level] = i;
    if (name=="RATING" && ratings[level]>=0)
      card.school.team.grades[ratings[level]] = NativeUiText(node);
    if (name.starts_with("SMALLSTAR"))
      stars.push_back(int16_t(kernel::R16(node+0x2A)) + kernel::R16(node+0x2E)/2);
    if (name=="COVER" && kernel::R16(node+0x2E)) {
      cover = int16_t(kernel::R16(node+0x2A));
      covered = true;
    }
  }
  if (card.school.team.name.empty() || card.selected < 0 || size_t(card.selected) >= card.menu.size())
    return false;
  // Read the native phase action; postseason changes the second menu entry
  // from Recruiting to Playoff Tree and exposes the native Start simulation.
  const auto nav=g_dynasty_navbar_root.load();
  if(nav && mem::IsCommitted(nav+0x74) && (kernel::R16(nav+0x28)&1)){
    const auto navDepth=kernel::R32(nav+0x24);std::array<bool,128> navShown{};
    if(navDepth<128){navShown[navDepth]=true;
      for(auto node=kernel::R32(nav+8),n=0u;node && n<2048 && mem::IsCommitted(node+0x74) && kernel::R32(node+0x24)>navDepth;node=kernel::R32(node+8),++n){
        const auto level=kernel::R32(node+0x24);if(!level||level>=128)continue;
        navShown[level]=navShown[level-1] && (kernel::R16(node+0x28)&1);if(!navShown[level])continue;
        const auto value=NativeUiText(node);if(value.size()>128)continue;
        std::string plain;for(unsigned char c:value)if(c>=32&&c<127)plain+=char(std::toupper(c));
        if(plain.starts_with("SIM ") || plain.starts_with("SIMULATE "))card.advance=plain;
      }
    }
  }
  if(card.advance.find("TOURNAMENT")!=std::string::npos || card.advance.find("NCAA")!=std::string::npos)card.menu[1]="Playoff Tree";
  const char* labels[] = {"Schedule","Recruiting","Baseball America Stats","Team Management","Options"};
  for (size_t i=0;i<card.menu.size();++i)
    if (card.menu[i].empty()) card.menu[i] = labels[i];
  if (!stars.empty())
    card.school.prestige = int(std::count_if(stars.begin(),stars.end(),[&](int x){return !covered || x<cover;}));
  const auto* school = SchoolFor(card.school.team.name,card.school.team.nickname);
  card.school.team.rank = RankFor(school,card.school.team.rank);
  std::vector<std::string> updates;
  if (school) {
    card.school.color = school->color;
    card.school.location = school->location;
    std::vector<DynastyFixture> fixtures;
    ReadDynastySchedule(card,*school,&fixtures);
    const auto conference=LeagueConferenceLabels(school->conference);
    card.school.team.conference=conference[0];card.conferenceAbbreviation=conference[1];
    card.homeLogo=school->asset;
    if(!card.upcoming.empty())if(auto opponent=SchoolFor(card.upcoming[0].opponent,card.upcoming[0].nickname))card.awayLogo=opponent->asset;
    std::stable_sort(fixtures.begin(),fixtures.end(),[](const auto& a,const auto& b){return a.day!=b.day?a.day<b.day:a.time<b.time;});
    for(auto it=fixtures.rbegin();it!=fixtures.rend() && updates.size()<3;++it){
      if(it->result.empty())continue;
      const auto& schools=LeagueSchools();
      auto opponent=std::find_if(schools.begin(),schools.end(),[&](const auto& candidate){return candidate.index==it->opponent;});
      if(opponent==schools.end())continue;
      auto date=DynastyDate(it->day);char label[32];std::snprintf(label,sizeof(label),"%u/%u",unsigned(date.month()),unsigned(date.day()));
      updates.push_back(std::string(label)+"  "+it->result+"  "+(it->home?"vs ":"at ")+opponent->name);
    }
  }
  if (card.records[0].empty()) card.records[0] = card.school.record;
  // The live dynasty service bar owns updated grades. The dormant home
  // tournament card contains authoring placeholders, so never use those.
  const auto bar=g_dynasty_rightbar_root.load();
  if(bar && mem::IsCommitted(bar+0x98) && (kernel::R16(bar+0x28)&1)){
    const auto bd=kernel::R32(bar+0x24);
    if(bd<127){std::array<bool,128> staff{};std::array<int,128> category{};category.fill(-1);
      for(auto node=kernel::R32(bar+8),n=0u;node && n<2048 && mem::IsCommitted(node+0x98) && kernel::R32(node+0x24)>bd;node=kernel::R32(node+8),++n){
        const auto l=kernel::R32(node+0x24);if(!l||l>=128)continue;const auto name=kernel::ReadCString(node+0x10,16);
        staff[l]=staff[l-1]||name=="STAFF";category[l]=category[l-1];
        const char* labels[]={"PITCHING","BATTING","FIELDING","RUNNING"};
        for(int i=0;i<4;++i)if(name==labels[i] && !staff[l])category[l]=i+1;
        if(name=="RATING" && category[l]>0 && !staff[l])card.school.team.grades[category[l]]=NativeUiText(node);
      }
    }
  }
  g_managed_school=card.school;
  if (!game::BindNativeDashboard(root,card,updates) && !DrawDynastyHome(card)) return false;
  if (TraceStartup()) {
    static std::string previous;
    const auto key = card.school.team.name + card.date + std::to_string(card.section)+std::to_string(card.selected);
    if (key != previous) {
      previous = key;
      LOGR("dynasty home: %s date=%s rank=%s row=%d upcoming=%zu record=%s",
           card.school.team.name.c_str(),card.date.c_str(),card.school.team.rank.c_str(),
           card.selected,card.upcoming.size(),card.records[0].c_str());
      gpu::g_capture = true;
    }
  }
  return true;
}

bool DrawNativeDynastySchool() {
  if(!overhaul::Active())return false;
  static std::optional<DynastySchoolCard> completed;
  // Native refreshes may wait for the GPU. Never block its presentation thread;
  // keep displaying the last complete card until the transaction is finished.
  const std::unique_lock<std::recursive_mutex> pickerLock(g_dynasty_picker_mutex, std::try_to_lock);
  if (!pickerLock.owns_lock()) return completed && DrawDynastySchool(*completed);
  const auto valid = [](uint32_t p) {
    return p >= 0x80000000u && p < 0x90000000u && mem::IsCommitted(p + 0x50);
  };
  const auto root = g_dynasty_school_root.load();
  if (!valid(root)) return false;
  const auto scene = kernel::ReadCString(root + 0x10, 16);
  if (scene != "FSELECTSCHOOL" && scene != "SELECTSCHOOL") return false;
  const auto depth = kernel::R32(root + 0x24);
  // Spinner refreshes temporarily clear scene input/visibility bits. The scene
  // identity owns this replacement until native navigation destroys/replaces it.
  if (depth >= 127) return false;
  auto retained=[&](){if(!completed)return false;SuppressNativeUiScene(root);return DrawDynastySchool(*completed);};

  DynastySchoolCard card;
  std::array<bool,128> shown{};
  std::array<int,128> ratings{};
  ratings.fill(-1);
  shown[depth] = true;
  std::vector<int> stars;
  int cover = 0;
  bool covered = false, dynasty = false;
  for (auto node = kernel::R32(root + 8), count = 0u;
       count < 4096 && valid(node) && kernel::R32(node + 0x24) > depth;
       node = kernel::R32(node + 8), ++count) {
    const auto level = kernel::R32(node + 0x24);
    if (level >= 128) continue;
    shown[level] = shown[level-1] && (kernel::R16(node + 0x28) & 1);
    ratings[level] = ratings[level-1];
    if (!shown[level]) continue;
    const auto name = kernel::ReadCString(node + 0x10, 16);
    const char* categories[] = {"OVERALL", "PITCHING", "BATTING", "FIELDING", "RUNNING"};
    for (int i=0; i<5; ++i)
      if (name == categories[i]) ratings[level] = i;
    if (name == "DYNASTY") dynasty = true;
    if (name == "TEAMNAME") card.team.name = NativeUiText(node);
    if (name == "TEAMNAME1") card.team.nickname = NativeUiText(node);
    if (name == "TEAM") card.team.conference = NativeUiText(node);
    if (name == "CONFERENCERANKN") card.standing = NativeUiText(node);
    if (name == "NATIONARANKNO") card.team.rank = NativeUiText(node);
    if (name == "RECORDTX1") card.recordLabel = NativeUiText(node);
    if (name == "RECORDTX2") card.record = NativeUiText(node);
    if (name == "RATING" && ratings[level] >= 0)
      card.team.grades[ratings[level]] = NativeUiText(node);
    if (name.starts_with("SMALLSTAR"))
      stars.push_back(int16_t(kernel::R16(node + 0x2A)) + kernel::R16(node + 0x2E)/2);
    if (name == "COVER" && kernel::R16(node + 0x2E)) {
      cover = int16_t(kernel::R16(node + 0x2A));
      covered = true;
    }
  }
  if (!dynasty || card.team.name.empty()) return retained();
  if (!stars.empty())
    card.prestige = int(std::count_if(stars.begin(), stars.end(), [&](int x) { return !covered || x < cover; }));

  const auto* school = SchoolFor(card.team.name, card.team.nickname);
  const auto expected=g_picker_expected.load();
  if(completed && (g_picker_changing.load() || PickerNow()<g_picker_ready_after.load() || (expected && (!school || school->index!=expected))))
    return retained();
  card.team.rank = RankFor(school, card.team.rank);
  if (school) {
    card.location = school->location;
    card.color = school->color;
    card.seasonRecords = LeagueSeasonRecords(school->index);
    for (const auto& peer : LeagueSchools()) {
      if (peer.conference != school->conference) continue;
      const bool selected = peer.index == school->index;
      if (selected) card.selected = card.schools.size();
      card.schools.push_back({peer.name, peer.nickname, selected ? card.team.rank : RankFor(&peer)});
    }
  }
  if (card.schools.empty())
    card.schools.push_back({card.team.name, card.team.nickname, card.team.rank});
  completed = card;
  if(school)g_picker_displayed=school->index;
  if (!DrawDynastySchool(card)) return false;
  SuppressNativeUiScene(root);
  if (TraceStartup()) {
    static std::string previous;
    const auto key = card.team.name + card.team.conference + card.team.rank;
    if (previous != key) {
      previous = key;
      LOGR("dynasty selector: %s %s / %s rank=%s schools=%zu prestige=%d", card.team.name.c_str(),
           card.team.nickname.c_str(), card.team.conference.c_str(), card.team.rank.c_str(),
           card.schools.size(), card.prestige);
      if (!dev::Env("MVP06_TEST_SCHOOL_ORDER")) gpu::g_capture = true;
    }
  }
  return true;
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
  for (auto& team : sides)
    if (!team.name.empty()) team.rank = RankFor(SchoolFor(team.name, team.nickname), team.rank);
  std::string contextTitle;
  FeWalk(g_recruit_title_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="TITLE"){auto value=NativeUiText(n);if(FeClean(value))contextTitle=value;}});
  if(contextTitle.empty() || !_stricmp(contextTitle.c_str(),"TEAM SELECT")){
    const char* titles[]={"EXHIBITION","DYNASTY MODE","TOURNAMENT","ONLINE PLAY","MINI GAMES","HOME RUN SHOWDOWN","COACH MODE","SCENARIO EDITOR"};
    contextTitle=titles[std::min(7u,g_setup_mode.load())];
  }
  PublishTeamSelect(sides,players,contextTitle);
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
  auto valid = [](uint32_t p) { return FeValid(p); };
  bool popup = false;
  if(g_popup_state_known && g_owned_popup.load())g_submenu_popup=g_owned_popup.load();
  // Scene allocations are freed while entering Ballpark/gameplay. Their old
  // addresses and flags can survive while their child links are reused.
  const bool livePopup=FeVisibleScene(g_submenu_popup,"POPUP") &&
      mem::IsCommitted(g_submenu_popup+0x5CC);
  if (livePopup && kernel::R32(g_submenu_popup+0x5CC) &&
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
    popup=g_popup_open && livePopup && kernel::R32(g_submenu_popup+0x5CC)!=0;
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
  const auto rootName=kernel::ReadCString(g_submenu_root+0x10,16);
  if(!FeVisibleScene(g_submenu_root,rootName.c_str()))return;
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
    std::string libraryTitle;
    if(kind==1)FeWalk(g_recruit_title_root.load(),[&](uint32_t n,const std::string& name,int,int,int){
      if(name=="TITLE"){auto text=NativeUiText(n);if(FeClean(text))libraryTitle=text;}
    });
    PublishLibrary(row,entries,message,album,libraryTitle);
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
        // Scenario runner rows store the native player ID at +0x14
        // (getter 00330AB0; formatter 005436E0). Zero means no runner.
        // The native empty formatter leaves the FEL placeholder unchanged.
        if(valid(object) && kernel::R32(object)==0x64634C &&
           kernel::R32(object+8)>=0x4F && kernel::R32(object+8)<=0x51 &&
           kernel::R32(object+0x14)==0)row.value="EMPTY";
        if(valid(object) && row.value.empty()){auto pcValue=PcOptionValue(kernel::R32(object+8));if(!pcValue.empty())row.value=std::move(pcValue);}
        if(valid(object) && dev::Env("MVP06_UI_SCREEN_AUDIT") && (row.value.find("SOME TEXT")!=std::string::npos || row.value.find("some text")!=std::string::npos)){
          static std::unordered_set<uint32_t> inspected;
          if(inspected.insert(object).second)LOGR("scenario option native: label=%s object=%08X vtable=%08X fields=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
            row.label.c_str(),object,kernel::R32(object),kernel::R32(object+4),kernel::R32(object+8),kernel::R32(object+12),kernel::R32(object+16),kernel::R32(object+20),kernel::R32(object+24),kernel::R32(object+28),kernel::R32(object+32));
        }
        // Scenario runner option getter 00330AB0 returns object+0x14.
        // Its native formatter 005436E0 treats zero as an empty base, but
        // the fallback FEL string is the untranslated "some text" marker.
        if(valid(object) && kernel::R32(object)==0x64634C && kernel::R32(object+0x14)==0)
          row.value="EMPTY";
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
  if(kernel::ReadCString(g_submenu_root+0x10,16)=="GAMEMODES" && selected<8)g_setup_mode=selected;
  PublishSubmenu(g_submenu_category, int(selected), labels);
}
} // namespace game

X86_EXTERN_FUNC(sub_00533D40);
X86_EXTERN_FUNC(__imp__sub_0054CC70);
X86_EXTERN_FUNC(__imp__sub_0054CCD0);
X86_EXTERN_FUNC(__imp__sub_0054CE10);
X86_EXTERN_FUNC(__imp__sub_0054CD30);
namespace {
// Keep the native spinner and its selected school ID authoritative. A sorted
// overlay alone cannot change the order in which the native spinner advances.
using SchoolStep = void (*)(X86Context&, uint8_t*);
bool DynastyPicker(uint32_t self) {
  return overhaul::Active() && self >= 0x80000000u &&
         mem::IsCommitted(self + 0x214) && kernel::R32(self) == 0x6412C8;
}
struct PickerChange {
 uint32_t self;
 explicit PickerChange(uint32_t at):self(at){g_picker_changing=true;}
 ~PickerChange(){g_picker_expected=kernel::R32(self+0x210);g_picker_ready_after=PickerNow();g_picker_changing=false;}
};
void SeekSchool(X86Context& ctx, uint8_t* base, unsigned target, SchoolStep step) {
  const auto input=ctx;const auto self=input.ecx;
  const auto root=g_dynasty_school_root.load();uint32_t label=0;
  if(root && mem::IsCommitted(root+0x24)){
    auto depth=kernel::R32(root+0x24);bool inTeams=false;unsigned groupDepth=0;
    for(auto node=kernel::R32(root+8),n=0u;n<4096 && node>=0x80000000u && node<0x90000000u && mem::IsCommitted(node+0x100) && kernel::R32(node+0x24)>depth;node=kernel::R32(node+8),++n){
      auto level=kernel::R32(node+0x24);auto name=kernel::ReadCString(node+0x10,16);
      if(inTeams && level<=groupDepth)inTeams=false;
      if(name=="TEAMSG"){inTeams=true;groupDepth=level;}
      if(inTeams && name=="TEAMNAME"){label=node;break;}
    }
  }
  // Set the native spinner by school ID, then refresh details once. Walking
  // through intermediate schools repeatedly rebuilds logos and stalls the GPU.
  if(label && mem::IsCommitted(label+0xE0)){
    ctx=input;ctx.ecx=label+0xE0;auto vtable=kernel::R32(ctx.ecx);
    if(mem::IsCommitted(vtable+0x28)){
      X86_PUSH32(target);X86_PUSH32(0);x86_icall(ctx,base,kernel::R32(vtable+0x1C));
      ctx.ecx=label+0xE0;X86_PUSH32(0);x86_icall(ctx,base,kernel::R32(vtable+0x28));
      if(ctx.eax==target){
        X86_MEM32(self+0x210)=target;ctx.ecx=self;X86_PUSH32(0);sub_00533D40(ctx,base);
        ctx.esp=input.esp+8;ctx.eax&=0xFFFFFF00u;ctx.cf=ctx.of=ctx.sf=0;ctx.zf=ctx.pf=1;
        if(dev::Env("MVP06_TEST_SCHOOL_ORDER"))LOGR("school refresh audit: target=%u refreshes=1",target);
        return;
      }
    }
  }
  for(size_t n=0;n<LeagueSchools().size();++n){ctx=input;step(ctx,base);if(kernel::R32(self+0x210)==target)return;}
  LOGE("dynasty selector: school %u is absent from the native spinner",target);
}

void StepSchool(X86Context& ctx, uint8_t* base, int direction, SchoolStep step) {
  const std::lock_guard<std::recursive_mutex> pickerLock(g_dynasty_picker_mutex);
  if (!DynastyPicker(ctx.ecx)) return step(ctx, base);
  PickerChange changing(ctx.ecx);
  const auto selected = kernel::R32(ctx.ecx + 0x210);
  const auto conference = kernel::R32(ctx.ecx + 0x214);
  std::vector<unsigned> peers;
  for (const auto& school : LeagueSchools())
    if (school.conference == conference) peers.push_back(school.index);
  const auto found = std::find(peers.begin(), peers.end(), selected);
  if (found == peers.end() || peers.empty()) return step(ctx, base);
  const auto index = size_t(found - peers.begin());
  const auto next = (index + peers.size() + direction) % peers.size();
  const auto self = ctx.ecx;
  const auto stepStarted=PickerNow();
  SeekSchool(ctx, base, peers[next], step);
  if(dev::Env("MVP06_TEST_SCHOOL_ORDER"))LOGR("school step timing: conf=%u ms=%.2f",conference,(PickerNow()-stepStarted)*1000);
  if (dev::Env("MVP06_TEST_SCHOOL_ORDER"))
    LOGR("school order audit: conf=%u from=%u target=%u actual=%u direction=%d",
         conference, selected, peers[next], kernel::R32(self + 0x210), direction);
}
uint32_t PickerConferenceNode() {
  const auto root = g_dynasty_school_root.load();
  if (!root || !mem::IsCommitted(root + 0x24)) return {};
  const auto depth = kernel::R32(root + 0x24);
  if (depth >= 127) return {};
  std::array<bool, 128> shown{};
  shown[depth] = true;
  uint32_t label = 0;
  auto node = kernel::R32(root + 8);
  for (unsigned n = 0; n < 4096 && node && mem::IsCommitted(node + 0x50) &&
       kernel::R32(node + 0x24) > depth; ++n, node = kernel::R32(node + 8)) {
    const auto level = kernel::R32(node + 0x24);
    if (level >= shown.size()) continue;
    shown[level] = shown[level - 1] && (kernel::R16(node + 0x28) & 1);
    if (shown[level] && kernel::ReadCString(node + 0x10, 16) == "TEAM")
      label = node;
  }
  return label;
}
std::string PickerConference() {
  const auto node = PickerConferenceNode();
  return node ? game::NativeUiText(node) : std::string{};
}
void StepConference(X86Context& ctx, uint8_t* base, int direction, SchoolStep fallback) {
  const std::lock_guard<std::recursive_mutex> pickerLock(g_dynasty_picker_mutex);
  if (!DynastyPicker(ctx.ecx) || LeagueSchools().empty()) return fallback(ctx, base);
  PickerChange changing(ctx.ecx);
  const auto input = ctx;
  const auto self = input.ecx;
  const auto initial = kernel::R32(self + 0x214);
  static std::vector<std::pair<unsigned, std::string>> conferences;
  static std::vector<unsigned> nativeOrder;
  auto advance = [&] {
    ctx = input;
    __imp__sub_0054CE10(ctx, base);
  };
  if (conferences.empty()) {
    // Read the active database's localized labels rather than assuming its IDs
    // are alphabetical. IDs remain unchanged for schedules and standings.
    for (unsigned n = 0; n < 64; ++n) {
      const auto id = kernel::R32(self + 0x214);
      if (std::any_of(conferences.begin(), conferences.end(),
                      [&](const auto& entry) { return entry.first == id; })) break;
      conferences.emplace_back(id, PickerConference());
      nativeOrder.push_back(id);
      advance();
    }
    std::stable_sort(conferences.begin(), conferences.end(), [](const auto& a, const auto& b) {
      return _stricmp(a.second.c_str(), b.second.c_str()) < 0;
    });
  }
  const auto found = std::find_if(conferences.begin(), conferences.end(),
                                [&](const auto& entry) { return entry.first == initial; });
  if (found == conferences.end()) { ctx = input; return fallback(ctx, base); }
  const auto next = (size_t(found - conferences.begin()) + conferences.size() + direction) % conferences.size();
  const auto target = conferences[next].first;
  // Position the conference spinner immediately before the target, then use
  // one normal native transition to rebind all of its dependent school lists.
  // Avoid running every intervening conference's expensive detail refresh.
  const auto native = std::find(nativeOrder.begin(), nativeOrder.end(), target);
  const auto node = PickerConferenceNode();
  if (node && native != nativeOrder.end()) {
    const auto preceding = nativeOrder[(size_t(native - nativeOrder.begin()) + nativeOrder.size() - 1) % nativeOrder.size()];
    ctx = input;
    ctx.ecx = node + 0xE0;
    const auto method = kernel::R32(kernel::R32(ctx.ecx) + 0x1C);
    X86_PUSH32(preceding);
    X86_PUSH32(0);
    x86_icall(ctx, base, method);
  }
  for (size_t n = 0; n < conferences.size(); ++n) {
    advance();
    if (kernel::R32(self + 0x214) == target) break;
  }
  // Start each conference at its first displayed school. Do not restore a
  // cursor offset from a differently sized conference's remembered list.
  for (const auto& school : LeagueSchools()) if (school.conference == target) {
    if (kernel::R32(self + 0x210) != school.index) {
      ctx = input;
      SeekSchool(ctx, base, school.index, __imp__sub_0054CC70);
    }
    break;
  }
  LOGR("dynasty selector conference: %u %s team=%u", target,
       PickerConference().c_str(), kernel::R32(self + 0x210));
}
}
X86_FUNC(sub_0054CC70) { StepSchool(ctx, base, 1, __imp__sub_0054CC70); }
X86_FUNC(sub_0054CCD0) { StepSchool(ctx, base, -1, __imp__sub_0054CCD0); }
X86_FUNC(sub_0054CE10) { StepConference(ctx, base, -1, __imp__sub_0054CE10); }
X86_FUNC(sub_0054CD30) { StepConference(ctx, base, 1, __imp__sub_0054CD30); }

// The movie manager owns its completion callback. Route EA through its normal
// stop/completed request (-1), rather than returning with a pending callback.
X86_EXTERN_FUNC(__imp__sub_002C31C0);
X86_FUNC(sub_002C31C0) {
 if(dev::Env("MVP06_UI_SCREEN_AUDIT"))LOGR("tutorial movie path: %s",kernel::ReadCString(kernel::R32(ctx.esp+4),260).c_str());
 __imp__sub_002C31C0(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_0053B010);
X86_FUNC(sub_0053B010){
 // DemoMovie reads its movie parameter from the FE manager before requesting
 // playback. Recover only an unset tutorial parameter from its native menu.
 const auto movie=g_tutorial_movie.load(),manager=kernel::R32(0x006C680C);
 if(overhaul::Active() && movie>=15 && movie<=17 && manager>=0x80000000u && manager<0x90000000u &&
    mem::IsCommitted(manager+0x400) && kernel::R32(manager+0x3FC)==~0u){
  kernel::W32(manager+0x3FC,movie);
  LOGR("tutorial movie: bound native selection to movie %u",movie);
 }
 __imp__sub_0053B010(ctx,base);
}
X86_EXTERN_FUNC(__imp__sub_000C0230);
X86_FUNC(sub_000C0230) {
  game::WorkshopMovie(ctx);
  if (!overhaul::Active()) return __imp__sub_000C0230(ctx, base);
  uint32_t movie = kernel::R32(ctx.esp + 4);
  if(movie==9 || movie==11)game::EndStartupLoading();
  if (TraceStartup() || dev::Env("MVP06_UI_SCREEN_AUDIT"))
    LOGR("startup: movie request %d callback %08X caller=%08X ready=%u", int(movie),
         kernel::R32(ctx.ecx + 0x18),kernel::R32(ctx.esp),unsigned(kernel::R32(0x006F78B0)&255));
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
  if(timelineOwner=="SERIESSUMMARY"||timelineOwner=="DAYSGAMES")g_series_summary_root=ctx.ecx;
  if(timelineOwner=="TLINESCORES")g_series_scores_root=ctx.ecx;
  if(timelineOwner=="LINESCOREPOPUP")g_linescore_popup_root=ctx.ecx;
  if(timelineOwner=="FHIRESTAFF")g_hire_staff_root=ctx.ecx;
  if(timelineOwner=="TEAMSMATCHUP")game::StadiumIntroTimeline(timelineName.c_str());
  if(timelineOwner=="PITCHERINFO")game::PitcherCardTimeline(timelineName.c_str());
  if(timelineOwner=="PLAYERGRP")game::BatterCardTimeline(timelineName.c_str());
  if (ctx.ecx == g_dynasty_home_root.load()) {
    // Dynasty Home animates individual menu entries instead of moving its FEL grid cursor.
    const auto prefix = timelineName.starts_with("DOWNMENU") ? 8u :
                        timelineName.starts_with("UPMENU") ? 6u : 0u;
    if (prefix && timelineName.size() == prefix+1 &&
        timelineName[prefix] >= '1' && timelineName[prefix] <= '5')
      g_dynasty_home_row = timelineName[prefix] - '1';
  }
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

// Observe the native validator without changing eligibility or optimizing data.
// The on-disk rotation can be valid while an in-memory roster differs.
X86_EXTERN_FUNC(__imp__sub_00093300);
X86_FUNC(sub_00093300) {
  const auto team=kernel::R32(ctx.esp+4);
  const auto database=ctx.ecx;
  __imp__sub_00093300(ctx,base);
  if(dev::Env("MVP06_UI_SCREEN_AUDIT") && ctx.eax){
    const auto table=kernel::R32(database+0x34);
    const auto records=kernel::R32(table+4);
    const auto record=records+team*0x2D8u;
    if(team<1024 && mem::IsCommitted(record+0x1B7))
      LOGR("roster validation: team=%u flags=%08X record=%08X rotation=%u,%u,%u",team,ctx.eax,record,
           kernel::R32(record+0x1B4)&255u,(kernel::R32(record+0x1B4)>>8)&255u,(kernel::R32(record+0x1B4)>>16)&255u);
  }
}

// The whole-record sweep (0009C290) also visits retired school IDs kept for
// save compatibility. They are absent from the active team table and have no
// rotation, so no player can manage them. Only those records report clear;
// every active team still runs the complete native check.
static bool RetiredRosterRecord(uint32_t database,uint32_t team){
  if(!overhaul::Active() || !database || team>=1024)return false;
  const auto active=kernel::R32(database+8);
  if(!active || !mem::IsCommitted(active))return false;
  const auto count=kernel::R32(active);
  for(uint32_t i=0;i<count && i<1024;++i)if(kernel::R32(active+4+i*8+4)==team)return false;
  const auto table=kernel::R32(database+0x34);
  const auto record=kernel::R32(table+4)+team*0x2D8u;
  return mem::IsCommitted(record+0x1B7) && (kernel::R32(record+0x1B4)&0xFFFFFFu)==0;
}
X86_EXTERN_FUNC(__imp__sub_00098300);
X86_FUNC(sub_00098300) {
  const auto team=kernel::R32(ctx.esp+4);
  const auto database=kernel::R32(0x6C6540u);
  if(RetiredRosterRecord(database,team)){
    if(dev::Env("MVP06_UI_SCREEN_AUDIT"))LOGR("roster validation: retired team=%u skipped",team);
    ctx.eax=0;ctx.esp+=8;return; // ret 0x04 after the pushed return address
  }
  __imp__sub_00098300(ctx,base);
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
  // Disable the decorative EA Trax toast everywhere; playback and updates continue.
  if(kernel::ReadCString(ctx.ecx+0x10,16)=="SPORTSTRAX"){ctx.esp+=4;return;}
  if((game::FeScene(g_create_editor_root.load(),"CREATEAPLAYER")||game::FeScene(g_award_watch_root.load(),"AWARDWATCH")||game::FeScene(g_dynasty_home_root.load(),"FMGRDECISION"))){
    const auto name=kernel::ReadCString(ctx.ecx+0x10,16);
    if(name=="COMMONBACK"||name=="BACKRAY"||name.starts_with("MBACKGROUND")||name=="MMVPBRAND"||name=="BACKGROUND"||name=="LOGOANIM"||name=="FTICKER"||name=="FTICKERMATTE"||name=="FEARBAR"||name.starts_with("MESSENGERBAR")){ctx.esp+=4;return;}
  }
  if(game::NativeUiRenderSuppressed(ctx.ecx,0x000BBAC0)) {
    if(TraceStartup()){static std::unordered_set<uint32_t> seen;if(seen.insert(ctx.ecx).second)LOGR("native scene draw bypassed: %s",kernel::ReadCString(ctx.ecx+0x10,16).c_str());}
    ctx.esp+=4;return;
  }
  game::RefreshNativeDashboard(ctx,ctx.ecx);
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

X86_EXTERN_FUNC(__imp__sub_00550530);
X86_FUNC(sub_00550530) {
 const bool themed=overhaul::Active();
 // Follow the actual initializer, not A: roster-limit notifications may reject
 // Create Player without ever opening the editor.
 struct Loading {bool active;Loading(bool on):active(on){if(active)g_player_editor_loading=true;}~Loading(){if(active){g_player_editor_loading=false;g_editor_transition_until=0;}}} loading(themed);
 if(themed && TraceStartup())LOGR("player editor: initialization started");
 __imp__sub_00550530(ctx,base);
 if(themed && TraceStartup())LOGR("player editor: initialization completed");
}


namespace game {
bool DrawNativeTournamentTree(){
 const auto root=g_playoff_root.load();
 auto valid=[](uint32_t p){return p>=0x80000000u&&p<0x90000000u&&mem::IsCommitted(p+0xB0);};
 if(!valid(root)||!(kernel::R16(root+0x28)&1)||kernel::ReadCString(root+0x10,16)!="PLAYOFFTREE")return false;
 const auto depth=kernel::R32(root+0x24);if(depth>=127)return false;
 TournamentTreeCard card;
 std::array<bool,128> visible{},teams{},finals{},inGame{};
 std::array<int,128> match{},side{};match.fill(-1);side.fill(-1);visible[depth]=true;
 int selectedRow=-1,selectedCol=-1;
 for(auto n=kernel::R32(root+8),count=0u;count<6000&&valid(n)&&kernel::R32(n+0x24)>depth;n=kernel::R32(n+8),++count){
  const auto l=kernel::R32(n+0x24);if(l>=128||l<=depth)continue;
  const auto name=kernel::ReadCString(n+0x10,16);
  visible[l]=visible[l-1]&&(kernel::R16(n+0x28)&1);
  teams[l]=teams[l-1];finals[l]=finals[l-1];inGame[l]=inGame[l-1];match[l]=match[l-1];side[l]=side[l-1];
  if(!visible[l])continue;
  if(name=="CONFERENCELOGO"){
   const auto object=kernel::R32(n+0x80);
   for(int i=0;i<4;++i){auto bits=kernel::R32(n+0x84+4*i);std::memcpy(&card.logoUv[i],&bits,4);}
   const float w=kernel::R16(n+0x2e),h=kernel::R16(n+0x30);if(h>0&&w>0)card.logoAspect=w/h;
   static std::unordered_map<uint32_t,uint32_t> textures;
   if(auto it=textures.find(object);it!=textures.end()&&valid(it->second))card.logo=it->second;
   if(!card.logo&&valid(object)){
    std::vector<std::pair<uint32_t,int>> queue{{object,0}};std::unordered_set<uint32_t> seen;
    for(size_t i=0;i<queue.size()&&i<256;++i){auto [item,level]=queue[i];if(!valid(item)||!seen.insert(item).second)continue;
     gpu::TextureDesc desc;
     if((kernel::R32(item)&0x70000)==0x40000&&gpu::Describe(item,desc)&&!desc.cube&&desc.depth==1&&!gpu::Format(desc.format).depth&&mem::IsCommitted(desc.data)){card.logo=item;textures[object]=item;break;}
     if(level<3&&mem::IsCommitted(item+0x200))for(unsigned off=0;off<0x200;off+=4){auto ptr=kernel::R32(item+off);if(valid(ptr)&&!seen.count(ptr))queue.push_back({ptr,level+1});}
    }
   }
  }
  if(name=="SCHED")card.title=NativeUiText(n);
  if(name=="SERIES") {const auto v=NativeUiText(n);if(v=="Best of 3")card.format=v;else if(!v.empty())card.series=v;}
  if(name=="FINALSGRP"){finals[l]=true;card.matches.emplace_back();match[l]=int(card.matches.size()-1);card.matches.back().selected=true;}
  if(name=="TEAMS"){teams[l]=true;selectedCol=int(kernel::R32(n+0x6c));selectedRow=int(kernel::R32(n+0x70));}
  if(teams[l]&&name.size()>=4&&name[0]=='X'&&name[2]=='Y'&&name[1]>='0'&&name[1]<='9'){
   const int col=name[1]-'0',row=std::atoi(name.c_str()+3);
   card.matches.emplace_back();match[l]=int(card.matches.size()-1);auto& m=card.matches.back();
   m.x=int16_t(kernel::R16(n+0x2a));m.y=int16_t(kernel::R16(n+0x2c));m.selected=col==selectedCol&&row==selectedRow;
  }
  if(match[l]<0||size_t(match[l])>=card.matches.size())continue;
  auto& m=card.matches[match[l]];
  if(name=="TEAM1")side[l]=0;if(name=="TEAM2")side[l]=1;
  if(name=="GAME")inGame[l]=true;
  if(name=="TX1"){const auto value=NativeUiText(n);if(inGame[l])m.number=value;else if(value.size()<=2)m.letter=value;}
  if(side[l]>=0){auto& t=m.teams[side[l]];
   if(name=="TEAM"||name=="BRACKETTEXT") {auto v=NativeUiText(n);if(!v.empty())t.name=v;}
   if(name=="NUMBER")t.seed=NativeUiText(n);
   if(name=="SCORENUMBER")t.score=NativeUiText(n);
   if(name=="STAT")t.record=NativeUiText(n);
   if(name=="TEAMLOGO"){const auto sprite=kernel::ReadCString(n+0x4e,5);if(sprite.size()==4&&sprite[1]>='0'&&sprite[1]<='9')t.logo=sprite.substr(1);}
  }
 }
 for(auto& m:card.matches)if(m.letter.size()==1&&m.letter[0]>='A'&&m.letter[0]<='Z')m.number=std::to_string(m.letter[0]-'A'+1);
 for(auto& m:card.matches)for(auto& t:m.teams)for(const auto& school:LeagueSchools())if(t.name==school.abbreviation||t.name==school.name){t.color=0xff000000u|((school.color&255)<<16)|(school.color&0xff00)|((school.color>>16)&255);if(t.logo.empty()){char code[12];std::snprintf(code,sizeof(code),"%03u",school.asset);t.logo=code;}break;}
 if(card.title.empty()||card.matches.empty())return false;
 // Stock initialization leaves SCHED at its first caption (Regional) while
 // restoring a Super Regional's active two-team best-of-three body. The live
 // body determines the displayed format until the native selector refreshes.
 if(card.title=="Regional"&&card.matches.size()==1&&card.format=="Best of 3")card.title="Super Regional";
 // FEL keeps its native match positions, participants, result strings and
 // focused cell. The overlay changes presentation only, including all formats.
 if(TraceStartup()) {static std::string last;std::string key=card.title+card.series;for(const auto& m:card.matches)key+=m.letter+m.teams[0].name+m.teams[1].name+std::to_string(m.selected);
  if(key!=last){last=key;LOGR("tournament tree: %s / %s matches=%u",card.title.c_str(),card.series.c_str(),unsigned(card.matches.size()));for(const auto& m:card.matches)LOGR("tournament match: %s game=%s position=%.0f,%.0f selected=%u %s (%s) / %s (%s)",m.letter.c_str(),m.number.c_str(),m.x,m.y,m.selected,m.teams[0].name.c_str(),m.teams[0].score.c_str(),m.teams[1].name.c_str(),m.teams[1].score.c_str());gpu::g_capture=true;}
 }
 if(!DrawTournamentTree(card))return false;
 // Native TL rendering refreshes the first-open round/site captions. Keep
 // that update alive beneath the fully opaque host canvas.
 return true;
}
}

namespace game {
namespace {
void ServiceIdentity(uint32_t n,const std::string& name,RecruitProfileCard& card,uint32_t& object){
 auto value=NativeUiText(n);if(!FeClean(value))value.clear();
 if(name=="NAME")card.name=value;if(name=="POSITION")card.position=value;
 if(name=="RANKNUMBER"||name=="RANKNUM")card.rank=value;if(name=="OVERALLNUMBER")card.overall=value;
 if(name=="STATUSVAL")card.status=value;
 if(name=="METERBAR")card.interest=std::clamp(kernel::R16(n+0x2e)/107.f,0.f,1.f);
 if(name=="FACE"){object=kernel::R32(n+0x80);for(int i=0;i<4;++i){auto v=kernel::R32(n+0x84+4*i);std::memcpy(&card.uv[i],&v,4);}}
}
uint32_t ServicePortrait(uint32_t object,const std::string& name){
 struct Face {uint32_t object=0,texture=0;std::string name;};static std::unordered_map<uint32_t,Face> faces;static uint32_t epoch=0;
 if(epoch!=g_recruit_portrait_epoch.load()){epoch=g_recruit_portrait_epoch.load();faces.clear();}
 auto& f=faces[object];if(f.object!=object||f.name!=name)f={object,0,name};
 if(!f.texture)f.texture=FePortrait(object);return f.texture;
}
}
bool DrawNativeRecruitActions(){
 auto root=g_recruit_actions_root.load();if(!overhaul::Active()||!FeScene(root,"RECRUITACTIONS"))return false;
 RecruitActionsCard card;card.school=g_managed_school;card.enabled.fill(true);uint32_t object=0;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  ServiceIdentity(n,name,card.player,object);
  if(name=="RECRUITPOINTS1"){auto v=NativeUiText(n);if(FeClean(v))card.points=v;}
  if(name=="CELL")card.selected=std::clamp(int(kernel::R32(n+0x6c)),0,7);
  if(y>=0&&y<7){auto v=NativeUiText(n);if(x==1&&FeClean(v))card.labels[y]=v;if(x==2&&name=="TX1"&&FeClean(v))card.costs[y]=v;
   if(x==0&&name=="CHECKBOX")card.checked[y]=kernel::ReadCString(n+0x4e,5)=="chek";
  }
 });
 if(card.player.name.empty()||card.points.empty())return false;
 card.player.portrait=ServicePortrait(object,card.player.name);
 const auto points=std::atoi(card.points.c_str());for(int i=0;i<7;++i)card.enabled[i]=card.checked[i]||std::atoi(card.costs[i].c_str())<=points;
 if(TraceStartup()){static std::string last;std::string state=card.player.name+" points="+card.points+" selected="+std::to_string(card.selected)+" checked=";for(bool checked:card.checked)state+=checked?'1':'0';if(state!=last){last=state;LOGR("recruit actions: %s",state.c_str());}}
 if(!DrawRecruitActions(card))return false;return true;
}
bool DrawNativeRecruitOffer(){
 auto root=g_recruit_offer_root.load();if(!overhaul::Active()||!FeScene(root,"RECRUITOFFER"))return false;
 RecruitOfferCard card;card.school=g_managed_school;uint32_t object=0;
 FeWalk(root,[&](uint32_t n,const std::string& name,int,int,int){
  ServiceIdentity(n,name,card.player,object);auto v=NativeUiText(n);if(!FeClean(v))v.clear();
  if(name=="CELL")card.selected=std::clamp(int(kernel::R32(n+0x6c)),0,2);
  if(name=="ASKPLAYVAL")card.requestTime=v;if(name=="ASKSCHOLVAL")card.requestScholarship=v;
  if(name=="SALARY")card.scholarship=v;if(name=="TERM")card.time=v;
  if(name=="AVAILNEXTVAL")card.next=v;if(name=="AVAILTHISVAL")card.current=v;if(name=="RESIGNPLAYERST")card.button=v;
 });
 if(card.player.name.empty()||card.scholarship.empty())return false;card.player.portrait=ServicePortrait(object,card.player.name);
 if(TraceStartup()){static std::string last;auto state=card.player.name+" scholarship="+card.scholarship+" time="+card.time+" current="+card.current+" next="+card.next+" selected="+std::to_string(card.selected);if(state!=last){last=state;LOGR("recruit offer: %s",state.c_str());}}
 if(!DrawRecruitOffer(card))return false;return true;
}
bool DrawNativeInbox(){
 auto root=g_mail_root.load();if(!overhaul::Active()||!FeScene(root,"FEMAIL"))return false;
 InboxCard card;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  if(name=="CELL"&&(kernel::R16(n+0x28)&8)){card.first=std::max(0,int(kernel::R32(n+0x74)));card.selected=std::clamp(int(kernel::R32(n+0x6c))-card.first,0,8);}
  auto v=NativeUiText(n);if(!FeClean(v))v.clear();if(name=="DATE")card.date=v;
  if(y>=0&&y<9){auto& m=card.messages[y];if(name=="EMAILFROM")m.sender=v;if(name=="EMAILITEM"){m.subject=v;
    bool unreadStyle=true,distinctStyles=false;for(unsigned i=0;i<4;++i){unreadStyle&=kernel::R32(n+0x90+4*i)==kernel::R32(0x6dcdd4+4*i);distinctStyles|=kernel::R32(0x6dcdd4+4*i)!=kernel::R32(0x6dce3c+4*i);}m.unread=unreadStyle&&distinctStyles&&!v.empty();
   }if(x==3&&name=="TX1")m.date=v;
  }
 });
 // The stock mailbox row builder selects these text styles from the record's
 // new/read state after applying sorting and filters. Attachment icons are
 // independent of unread state and are deliberately not used here.
 for(auto& m:card.messages)if(m.sender.empty()&&m.date.empty()&&(m.subject=="No items to show."||m.subject=="No Items To Show."))m={};
 if(TraceStartup()){static std::string last;std::string state;for(const auto& m:card.messages)if(!m.subject.empty())state+=m.subject+(m.unread?" [new]|":" [read]|");if(state!=last){last=state;LOGR("inbox rows: %s",state.c_str());}}
 if(!DrawInbox(card))return false;SuppressNativeUiScene(root);return true;
}
bool DrawNativeMailReader(){
 auto root=g_mail_reader_root.load();if(!overhaul::Active()||!FeScene(root,"FEMAILITEM"))return false;
 MailReaderCard card;
 FeWalk(root,[&](uint32_t n,const std::string& name,int,int y,int){
  auto v=NativeUiText(n);if(name!="TEXT"&&!FeClean(v))v.clear();
  if(name=="FROM")card.sender=v;if(name=="SUBJECT")card.subject=v;if(name=="SENT")card.date=v;if(name=="HOWOLD")card.age=v;
  if(name=="COUNTTEXT")card.index=v;if(name=="REPLY")card.hotlink=v;
  if(name=="BODY")card.first=std::max(0,int(kernel::R32(n+0x74)));
  if(name=="TEXT"&&y>=0&&y<8)card.lines[y]=v;
 });
 if(card.subject.empty())return false;if(!DrawMailReader(card))return false;SuppressNativeUiScene(root);return true;
}
}


namespace game {
bool DrawNativeMailFilters(){
 const auto root=g_mail_filters_root.load();if(!overhaul::Active()||!FeScene(root,"FEMAILFREQUENCY"))return false;
 MailFiltersCard card;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  if(name=="CELL"){card.first=std::clamp(int(kernel::R32(n+0x74)),0,18);card.selected=std::clamp(int(kernel::R32(n+0x6c)),0,18);}
  if(y<0||y>=19)return;auto v=NativeUiText(n);if(!FeClean(v))return;
  if(x==0&&name=="TB1")card.labels[y]=v;if(x==1&&name=="TL")card.values[y]=v;
 });
 if(!DrawMailFilters(card))return false;SuppressNativeUiScene(root);return true;
}
bool DrawNativeOffseason(){
 if(!overhaul::Active()||FeScene(g_stat_playerbar_root.load(),"PLAYERINFOBAR"))return false;
 static constexpr const char* names[]={"FRETIREMENT","FPLAYERPROGRESS","FINCOMING","FPROMOTEDEMOTE"};
 OffseasonCard card;card.school=g_managed_school;uint32_t root=0;
 for(int i=0;i<4;++i)if(FeScene(g_offseason_roots[i].load(),names[i])){root=g_offseason_roots[i].load();card.kind=i;break;}
 if(!root)return false;std::array<uint32_t,2> objects{};
 auto walk=[&](uint32_t r){
  if(!FeValid(r)||!(kernel::R16(r+0x28)&1))return;const auto depth=kernel::R32(r+0x24);if(depth>=127)return;
  std::array<bool,128> show{};std::array<int,128> table{},side{},xs{},ys{};xs.fill(-1);ys.fill(-1);show[depth]=true;
  for(auto n=kernel::R32(r+8),count=0u;count<12000&&FeValid(n)&&kernel::R32(n+0x24)>depth;n=kernel::R32(n+8),++count){
   auto d=kernel::R32(n+0x24);if(!d||d>=128)continue;const auto name=kernel::ReadCString(n+0x10,16);
   show[d]=show[d-1]&&(kernel::R16(n+0x28)&1);table[d]=table[d-1];side[d]=side[d-1];xs[d]=xs[d-1];ys[d]=ys[d-1];
   if(name=="VSLHP")side[d]=0;if(name=="VSRHP")side[d]=1;
   if(name=="CELL"||name=="PROGRESSION")table[d]=1;
   if(name=="LEFTPLAYERS")table[d]=6;if(name=="RIGHTPLAYERS")table[d]=7;
   if(name=="LEFTINFO")table[d]=8;if(name=="RIGHTINFO")table[d]=9;
   if(name=="BATTERINFOGRID")table[d]=2;if(name=="PITCHERINFOGRID")table[d]=3;
   if(name=="HEADINGS")table[d]=4;if(name=="STATS")table[d]=5;
   int x,y;if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2){xs[d]=x;ys[d]=y;}
   if(!show[d])continue;if(name=="PITCHERINFOGRID")card.pitcher=true;const int t=table[d],xx=xs[d],yy=ys[d];auto v=NativeUiText(n);if(!FeClean(v))v.clear();
   if((name=="CELL"||name=="PROGRESSION"||name=="GRID")&&(t==1||t==6||t==7)){
    const int k=t==7?1:0;card.first[k]=std::clamp(int(kernel::R32(n+0x74)),0,1000);card.selected[k]=std::clamp(int(kernel::R32(n+0x6c))-card.first[k],0,7);if(kernel::R16(n+0x28)&8)card.active=k;
   }
   if((t==1||t==6||t==7)&&xx>=0&&xx<7&&yy>=0&&yy<8){
    const int k=t==7?1:0;if(name=="TB1")card.rows[k][yy][xx]=v;
    if(card.kind==1&&name=="SL1"){auto sprite=kernel::ReadCString(n+0x4e,5);card.changes[yy]=sprite=="b01n"?1:sprite=="b02n"?-1:0;}
   }
   if((t==2||t==3)&&yy>=0&&yy<6&&!v.empty()){
    const int k=t==2?yy/3:card.kind==2?0:yy/2,row=t==2?yy%3:card.kind==2?yy:yy%2;if(k<2){if(xx==0)card.ratingLabels[k][row]=v;if(xx==1)card.ratings[k][row]=v;if(xx==2)card.deltas[k][row]=v;}
   }
   if(card.kind==1&&t==3&&name=="SVALUE"){card.ratingLabels[0][2]="STAMINA";card.ratings[0][2]=v;}
   if(yy>=0&&yy<8&&name=="TX1"){if(t==4)card.statLabels[yy]=v;if(t==5)card.stats[yy]=v;}
   if(name=="PLAYERCOUNT")card.count=v;
   const int k=t==9?1:0;auto& player=card.players[k];
   if(name=="NAME"||name=="PLAYERNAME")player.name=v;
   if(name=="POSVAL")player.position=v;if(name=="YEARVAL")player.year=v;if(name=="OVERALLVAL")player.overall=v;
   if(name=="FACE"){objects[k]=kernel::R32(n+0x80);for(int j=0;j<4;++j){auto raw=kernel::R32(n+0x84+4*j);std::memcpy(&player.uv[j],&raw,4);}}
  }
 };
 walk(root);if(card.kind==0)walk(g_stat_summary_root.load());
 if(card.kind!=3){auto& player=card.players[0];const auto first=player.name.find(" - "),last=player.name.rfind(" - ");
  if(first!=std::string::npos&&last>first){player.position=player.name.substr(0,first)+"  "+player.name.substr(last+3);player.name=player.name.substr(first+3,last-first-3);}
  const auto& selected=card.rows[0][card.selected[0]];player.year=selected[3];if(card.kind==2)player.overall=selected[2];
 }
 for(int k=0;k<2;++k)if(objects[k])card.players[k].portrait=ServicePortrait(objects[k],card.players[k].name);
 if(!DrawOffseason(card))return false;
 if(TraceStartup()){static std::string last;auto state=std::to_string(card.kind)+" player="+card.players[0].name+" selected="+std::to_string(card.active)+"/"+std::to_string(card.selected[0])+"/"+std::to_string(card.selected[1])+" count="+card.count;for(auto& list:card.rows)for(auto& row:list)for(auto& v:row)state+="|"+v;if(state!=last){last=state;LOGR("offseason view: %s",state.c_str());gpu::g_capture=true;}}
 return true;
}
}


namespace game {
namespace {
std::string PostseasonLogo(const std::string& name){
 for(const auto& school:LeagueSchools())if(name==school.abbreviation||name==school.name){char code[12];std::snprintf(code,sizeof(code),"%03u",school.asset);return code;}
 return {};
}
}
bool DrawNativeHireStaff(){
 const auto root=g_hire_staff_root.load();if(!overhaul::Active()||!FeScene(root,"FHIRESTAFF"))return false;
 HireStaffCard card;int group=0;std::vector<std::pair<int,std::string>> labels;
 FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
  if(name=="CURCOACH")group=1;if(name=="SELCOACH")group=2;if(name=="COACHATTRIBS")group=3;
  auto v=NativeUiText(n);if(!FeClean(v))v.clear();
  if(name=="POSITIONTL")card.role=v;
  if(name=="UNSIGNED"){card.first=std::max(0,int(kernel::R32(n+0x74)));card.selected=std::clamp(int(kernel::R32(n+0x6c))-card.first,0,2);}
  if((name=="LF"||name=="TX1")&&group==0&&x>=0&&x<3&&y>=0&&y<3)card.pool[y][x]=v;
  if(group==1||group==2){auto i=group-1;if(name=="NAME")card.names[i]=v;if(name=="LEVELVAL")card.levels[i]=v;if(name=="OVERALLVAL")card.overall[i]=v;}
  if(group==3){
   static const char* attrs[]={"POWER","CONTACT","BUNTING","DISCIPLINE","CONTROL","MOVEMENT","VELOCITY","STAMINA","PICKOFF","FIELDING","ASTRENGTH","BASERUNNING","RECOVERY","DURABILITY"};
   if(std::find(std::begin(attrs),std::end(attrs),name)!=std::end(attrs)&&!v.empty())labels.emplace_back(int16_t(kernel::R16(n+0x2c)),v);
   if(name=="TX1"&&x>=0&&x<2&&y>=0&&y<5)card.values[x][y]=v;
  }
 });
 std::stable_sort(labels.begin(),labels.end(),[](const auto& a,const auto& b){return a.first<b.first;});for(size_t i=0;i<labels.size()&&i<5;++i)card.labels[i]=labels[i].second;
 if(card.role.empty()||card.names[0].empty())return false;
 if(TraceStartup()){static std::string last;auto k=card.role+card.names[1]+std::to_string(card.selected);if(k!=last){last=k;LOGR("hire staff: role=%s current=%s selected=%s row=%d",card.role.c_str(),card.names[0].c_str(),card.names[1].c_str(),card.selected);}}
 if(!DrawHireStaff(card))return false;SuppressNativeUiScene(root);return true;
}
bool DrawNativePostseasonScores(){
 if(!overhaul::Active())return false;
 auto visible=[](uint32_t r){return FeValid(r)&&(kernel::R16(r+0x28)&1);};
 if(TraceStartup()){static std::string last;const auto r=g_linescore_popup_root.load();std::string k=std::to_string(r);if(FeValid(r))k+=" "+kernel::ReadCString(r+0x10,16)+" flags="+std::to_string(kernel::R16(r+0x28));if(k!=last){last=k;LOGR("postseason root: %s",k.c_str());}}
 auto read=[&](uint32_t root,PostseasonScoresCard& card){
  std::string debug;
  FeWalk(root,[&](uint32_t n,const std::string& name,int x,int y,int){
   auto v=NativeUiText(n);if(!FeClean(v))v.clear();
   if(name=="TITLE1")card.title=v;if(name=="ROUNDTL")card.round=v;if(name=="DATE")card.date=v;if(name=="NOGAMES")card.emptyMessage=v;
   if(name=="LHSGRID"||name=="GAMEGRID"){card.popup=false;card.selected=std::clamp(int(kernel::R32(n+0x6c))-int(kernel::R32(n+0x74)),0,2);}
   if(y<0||y>=3)return;auto& g=card.games[y];if(name=="GAMENUM")g.label=v;
   if(name=="TX1"&&x>=0&&x<2)g.names[x]=v;
   if(name=="TEXT"&&x>=2&&x<14)g.headings[x-2]=v;
   if(name=="TEXT"&&x>=14&&x<38)g.values[(x-14)/12][(x-14)%12]=v;
   if(x==38&&name=="TX1")g.date=v;
   if(name=="TEAM"&&(x==6||x==1))g.names[x==6?0:1]=v;
   if(name=="TIME"&&x==4)g.label=v;
   if(name=="SP"&&(x==7||x==2))g.details[x==7?0:1]=v;
   if(name=="LOGOS1"||name=="LOGOS2"){
    const auto sprite=kernel::ReadCString(n+0x4e,5);if(sprite.size()==4){int i=name=="LOGOS1"?0:1;g.logos[i]=sprite.substr(1);auto asset=std::strtoul(g.logos[i].c_str(),nullptr,10);for(const auto& school:LeagueSchools())if(school.asset==asset){g.names[i]=school.abbreviation;break;}}
   }
   if(!v.empty()&&debug.size()<2000&&(name=="TEAM"||name=="GAMENUM"||name=="TIME"||name=="TX1"||name=="TEXT"))debug+=name+"="+v+"|";
  });
  bool any=false;for(auto& g:card.games)for(int i=0;i<2;++i){if(g.logos[i].empty())g.logos[i]=PostseasonLogo(g.names[i]);any|=!g.names[i].empty();}
  if(TraceStartup()){static std::unordered_map<uint32_t,std::string> last;auto key=std::to_string(any)+debug;if(last[root]!=key){last[root]=key;LOGR("postseason scores: root=%s data=%u %s",kernel::ReadCString(root+0x10,16).c_str(),any,debug.c_str());}}
  return any||!card.emptyMessage.empty();
 };
 PostseasonScoresCard card;uint32_t root=0;
 auto popup=g_linescore_popup_root.load();
 if(visible(popup)){card.popup=true;if(read(popup,card))root=popup;}
 // Dialogs are constructed before being populated. An empty dormant dialog
 // must not steal the series screen's presentation.
 if(!root){card={};auto summary=g_series_summary_root.load();auto scores=g_series_scores_root.load();
  if(visible(summary)&&read(summary,card))root=summary;
  else if(visible(scores)){card={};if(read(scores,card))root=scores;}
 }
 if(!root)return false;
 if(!card.popup)FeWalk(g_recruit_title_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="TITLE"){auto v=NativeUiText(n);if(FeClean(v))card.title=v;}});
 if(card.popup)DrawNativeTournamentTree();
 if(!DrawPostseasonScores(card))return false;SuppressNativeUiScene(root);return true;
}
}




namespace game {
namespace {
bool AuthoredNativeEditor(){
 static const bool authored=[] {
  std::vector<uint8_t> bytes;
  if(!kernel::mods::Load(kernel::mods::GameDir()/"data/frontend/frontend_big/fes_createaplayer.fel",bytes))return false;
  const std::string fel(bytes.begin(),bytes.end());
  return fel.find("RT:M27FORM,")!=std::string::npos;
 }();
 return authored;
}
}
bool DrawNativePlayerEditor(){
 const auto root=g_create_editor_root.load();
 if(!overhaul::Active()||!FeScene(root,"CREATEAPLAYER"))return false;
 // Text entry is a native modal. It must retain its own drawing and focus.
 if(FeScene(g_editor_keyboard_root.load(),"TKEYBOARD"))return false;
 PlayerEditorCard card;card.school=g_managed_school;uint32_t grid=0;
 static constexpr const char* names[]={"GENERAL","APPEARANCE","BATTER/FIELDER","HITTENDANCY","EQUIPMENT","PITCHER RATINGS","FACESHAPE","BODYBUILD","HOTCOLDLEFTP","HOTCOLDRIGHTP"};
 static constexpr const char* titles[]={"GENERAL INFORMATION","APPEARANCE","BATTER / FIELDER RATINGS","BATTER TENDENCIES","EQUIPMENT","PITCHER RATINGS","FACE SHAPE","BODY BUILD","HOT / COLD ZONES  /  VS LHP","HOT / COLD ZONES  /  VS RHP"};
 FeWalk(root,[&](uint32_t n,const std::string& name,int,int,int){
  for(int i=0;i<10;++i)if(name==names[i]){grid=n;card.grid=name;card.page=titles[i];}
  if(name=="NAMETX")card.player=NativeUiText(n);if(name=="OVERALL")card.overall=NativeUiText(n);
 });
 if(!grid)return true;
 card.first=std::max(0,int(kernel::R32(grid+0x74)));card.selected=std::clamp(int(kernel::R32(grid+0x6c))-card.first,0,5);card.total=std::clamp(int(kernel::R32(grid+0x50)),0,128);
 card.paired=card.grid=="FACESHAPE"||card.grid=="BODYBUILD";
 card.ratings=card.grid=="BATTER/FIELDER"||card.grid=="HITTENDANCY"||card.grid=="PITCHER RATINGS";
 FeWalk(grid,[&](uint32_t n,const std::string& name,int,int y,int){
  y-=card.first;if(y<0||y>=6)return;
  if(name=="STARS"){auto sprite=kernel::ReadCString(n+0x4e,5);if(sprite.size()==4&&sprite.starts_with("prt")&&sprite[3]>='1'&&sprite[3]<='5')card.values[y]=std::string(1,sprite[3])+" / 5";return;}
  auto v=NativeUiText(n);if(!FeClean(v))return;
  if(card.grid.starts_with("HOTCOLD")){if(name=="HOTCOLD")card.values[y]=v;else if(name=="TOPLEFT"||name=="TOP"||name=="TOPRIGHT"||name=="LEFT"||name=="CENTER"||name=="RIGHT"||name=="BOTTOMLEFT"||name=="BOTTOM"||name=="BOTTOMRIGHT"){card.labels[y]=v;card.enabled[y]=true;}return;}
  if(name=="FNAMETB"||name=="LNAMETB"||name.ends_with("TL")){card.values[y]=v;return;}
  if(card.paired){if(name=="TB1"||name=="EYEBROW"||name=="EYESHAPE"||name=="NOSTRIL"||name=="NOSETIP"||name=="LIPS"||name=="JAWCHIN1")card.values[y]=v;else card.labels[y]=v;card.enabled[y]=true;return;}
  if(name.ends_with("TB")||name=="TB1"||name=="DITTY"){card.labels[y]=v;card.enabled[y]=(kernel::R16(n+0x28)&2)!=0;}
 });
 if(card.grid.starts_with("HOTCOLD")){
  // Offscreen rows retain their native text-list selection. Read all nine,
  // rather than replacing scrolled-out values with a neutral placeholder.
  std::array<int,128> rowAt{};rowAt.fill(-1);const auto depth=kernel::R32(grid+0x24);
  for(auto n=kernel::R32(grid+8),count=0u;count<4096&&FeValid(n)&&kernel::R32(n+0x24)>depth;n=kernel::R32(n+8),++count){
   const auto level=kernel::R32(n+0x24);if(!level||level>=128)continue;rowAt[level]=rowAt[level-1];auto name=kernel::ReadCString(n+0x10,16);int x,y;
   if(std::sscanf(name.c_str(),"X%dY%d",&x,&y)==2)rowAt[level]=y;
   if(name=="HOTCOLD"&&rowAt[level]>=0&&rowAt[level]<9){auto value=NativeUiText(n);if(FeClean(value))card.zones[rowAt[level]]=value;}
  }
 }
 card.nameField=card.grid=="GENERAL"&&card.first+card.selected<2;
 FeWalk(g_recruit_title_root.load(),[&](uint32_t n,const std::string& name,int,int,int){if(name=="EDIT")card.edit=true;if(name=="TITLE"){auto v=NativeUiText(n);if(FeClean(v))card.title=v;}});
 if(card.title.empty())card.title=card.edit?"EDIT PLAYER":"CREATE PLAYER";
 bool any=false;for(auto& label:card.labels)any|=!label.empty();if(!any)return false;
 if(TraceStartup()){static std::string last;std::string k=card.page+" row="+std::to_string(card.first+card.selected);for(int i=0;i<6;++i)k+="|"+card.labels[i]+"="+card.values[i];if(k!=last){last=k;LOGR("player editor view: %s",k.c_str());gpu::g_capture=true;}}
 // Retain the restored host form when its matching native FEL is not installed.
 return AuthoredNativeEditor()&&!card.paired&&!card.grid.starts_with("HOTCOLD")?true:DrawPlayerEditor(card);
}
}

namespace game {
bool NativeMenuPresentation(){
 const bool editor=overhaul::Active()&&!PlayerEditorLoading()&&FeScene(g_create_editor_root.load(),"CREATEAPLAYER");
 bool general=false,appearance=false,bespoke=false,zones=false;
 if(editor)FeWalk(g_create_editor_root.load(),[&](uint32_t,const std::string& name,int,int,int){if(name=="GENERAL")general=true;if(name=="APPEARANCE"||name=="FACESHAPE")appearance=true;if(name=="FACESHAPE"||name=="BODYBUILD"||name.starts_with("HOTCOLD"))bespoke=true;if(name.starts_with("HOTCOLD"))zones=true;});
 const bool nativeEditor=editor&&AuthoredNativeEditor()&&!bespoke;
 if(nativeEditor){general=false;appearance=false;}
 gpu::g_menu_model_offset.store(editor?(zones?26.f:72.f)/854.f:0.f);
 gpu::g_menu_model_offset_y.store(general?-24.f/480.f:0.f);
 gpu::g_menu_model_scale.store(appearance?.88f:1.f);
 return nativeEditor;
}
}

namespace game {
bool DrawNativeAwardWatch(){
 const auto root=g_award_watch_root.load();if(!overhaul::Active()||!FeVisibleScene(root,"AWARDWATCH") ||
   (!(kernel::R16(root+0x28)&2) && !FeVisibleScene(g_stat_playerbar_root.load(),"PLAYERINFOBAR")))return false;
 AwardWatchCard card;uint32_t grid=0;
 FeWalk(root,[&](uint32_t n,const std::string& name,int,int,int){
  if(name=="AWARDTOP10CELL"||name=="AWARDTEAMCELL")grid=n;
  if(name=="NAMETL")card.award=NativeUiText(n);
  if(name=="AWARDGROUP"||name=="AWARDGROUP2")FeWalk(n,[&](uint32_t t,const std::string& key,int,int,int){if(key=="TEXTLIST"){auto v=NativeUiText(t);if(name=="AWARDGROUP")card.group=v;else card.group2=v;}});
  if(name=="TEAMNAMES")card.champion=NativeUiText(n);
 });
 if(grid){
  card.first=std::max(0,int(kernel::R32(grid+0x74)));card.selected=std::clamp(int(kernel::R32(grid+0x6c))-card.first,0,9);card.total=std::clamp(int(kernel::R32(grid+0x50)),0,1000);
  FeWalk(grid,[&](uint32_t n,const std::string& name,int x,int y,int){
   if(name!="TX1")return;auto v=NativeUiText(n);if(!FeClean(v)||x<0||x>=5)return;
   if(y<0){card.headings[x]=v;return;}y-=card.first;if(y<0||y>=10)return;
   card.rows[y][x]=v;card.columns=std::max(card.columns,x+1);
  });
 }
 if(TraceStartup()){static std::string last;std::string key=card.award+"|"+card.group+"|"+card.group2+"|"+std::to_string(card.selected);for(auto& row:card.rows)for(auto& cell:row)key+="|"+cell;if(key!=last){last=key;LOGR("award redesign: %s",key.c_str());}}
 if(!DrawAwardWatch(card))return false;SuppressNativeUiScene(root);return true;
}
}
