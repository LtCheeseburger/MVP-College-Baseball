#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace input {struct Pad;}
namespace game {
struct TeamSelectController {std::string label; int lane = -1;};
struct TeamSelectSide {
  enum class Step { Team, Uniform, Difficulty, Ready };
  std::string name, nickname, conference, rank, controller;
  Step step = Step::Team;
  std::string uniform, difficulty, difficultyDescription;
  std::string uniformPreview;
  std::array<std::string,4> uniformParts{};
  int uniformRow = -1;
  std::array<std::string, 5> grades;
};
struct DynastySchoolRow {
  std::string name, nickname, rank;
};
struct DynastySchoolCard {
  TeamSelectSide team;
  std::string location, standing, record, recordLabel;
  // Archived real-world records, 2024-2026; empty entries are unavailable.
  std::array<std::string,3> seasonRecords{};
  unsigned color = 0x343C46;
  int prestige = -1;
  std::vector<DynastySchoolRow> schools;
  size_t selected = 0;
};
struct DynastyHomeGame {
  std::string date, opponent, nickname;
  bool home;
};
struct DynastyHomeCard {
  unsigned homeLogo=0,awayLogo=0;
  int section=0;
  DynastySchoolCard school;
  std::string date, advance, conferenceAbbreviation;
  std::vector<std::string> menu;
  int selected = -1;
  std::vector<DynastyHomeGame> upcoming;
  std::array<std::string,4> records;
};
struct DynastyCalendarDay {
  int number = 0;
  bool available = true;
  std::string opponent, nickname, rank, result, stage;
  bool home = false;
  unsigned games = 0;
};
struct DynastyCalendarCard {
  TeamSelectSide team;
  std::string month, year, record;
  unsigned color = 0x343C46;
  std::array<DynastyCalendarDay,42> days;
  int selected = -1;
};
bool DrawNativeDynastyCalendar();
struct PlaySimCard {
  std::array<TeamSelectSide,2> teams;
  std::array<unsigned,2> colors{0x343C46,0x343C46};
  std::array<std::string,2> records;
  std::array<std::string,2> scores;
  std::string date, time;
  std::array<std::string,2> actions{"PLAY / COACH","QUICK SIM"};
  int selected = 0, game = 0, games = 1;
};
bool DrawNativePlaySim();
struct StartingPitcherSide {
  TeamSelectSide team;
  unsigned color=0x343C46;
  std::array<std::array<std::string,4>,6> rotation;
  std::array<std::string,5> pitches,ratings;
  std::string stamina;
  uint32_t portrait=0;
  std::array<float,4> uv{0,0,1,1};
  int selected=0,first=0;
  bool active=false,ready=false;
};
struct StartingLineupSide {
  TeamSelectSide team;
  unsigned color=0x343C46;
  std::array<std::array<std::string,6>,8> order;
  std::array<std::string,3> ratings;
  std::string versus;
  uint32_t portrait=0;
  std::array<float,4> uv{0,0,1,1};
  std::array<uint32_t,9> zones{};
  int selected=0,first=0;
  bool active=false,ready=false,done=false;
};
struct FinalPlaySimCard {
  std::array<StartingLineupSide,2> sides;
  std::array<std::string,2> players,era,records,wins;
  std::array<float,2> stamina{0,0};
  std::array<std::string,3> actions{"PLAY","COACH","SIM"};
  int selected=0;
};
struct CoachPlayer {
  std::string name;
  bool pitching=false;
  std::array<std::array<std::string,2>,6> stats;
  uint32_t portrait=0;
  std::array<float,4> uv{0,0,1,1};
};
struct CoachCard {
  std::array<TeamSelectSide,2> teams;
  std::array<unsigned,2> colors{0x343C46,0x343C46};
  std::array<std::string,2> scores;
  std::array<CoachPlayer,2> players;
  std::vector<std::string> commands;
  std::vector<bool> enabled;
  std::array<std::string,4> runners;
  std::string inning,result,playByPlay;
  std::array<std::array<std::string,16>,3> lineScore;
  int selected=0,outs=0,batting=0;
  bool lines=false;
};
struct SimGameCard {
  std::array<TeamSelectSide,2> teams;
  std::array<unsigned,2> colors{0x343C46,0x343C46};
  std::array<CoachPlayer,2> players;
  std::array<std::string,2> positions;
  std::array<float,2> stamina{0,0};
  std::array<std::array<std::string,13>,3> lineScore;
  std::string speed;
  std::vector<std::string> actions;
  int selected=0;
};
struct FinalScoreCard : SimGameCard {
  std::array<std::string,2> scores,results;
  std::string outcome;
};
struct BoxScoreCard {
 std::array<TeamSelectSide,2> teams;
 std::array<std::array<std::string,13>,3> score;
 std::array<std::array<std::string,9>,11> rows;
 int first=0,selected=0,total=0;
};
struct LeagueTableCard {
 bool standings=false;
 std::string conference,managedTeam;
 std::array<std::array<std::string,16>,32> rows;
 int first=0,selected=0,columnFirst=0;
};
bool DrawNativeLeagueTable();
bool DrawLeagueTable(const LeagueTableCard&);
struct StatisticsMenuCard { DynastySchoolCard school; std::string date,record; std::array<std::string,6> menu; std::array<bool,6> enabled{true,true,true,true,true,true}; int selected=0; };
bool DrawNativeStatisticsMenu();
bool DrawStatisticsMenu(const StatisticsMenuCard&);
void MapStatisticsMenuPad(int port,input::Pad& pad);
void MapDynastyPickerConfirm(int port,input::Pad& pad);
int StatisticsMenuSelection();
struct StatsLandingCard { DynastySchoolCard school; std::string date,record,scouting; std::array<std::string,4> menu,grades,coach; std::array<bool,4> enabled{true,true,true,true}; int selected=0; };
bool DrawNativeStatsLanding();
bool DrawStatsLanding(const StatsLandingCard&);
struct DynastyOptionsCard { DynastySchoolCard school; std::array<std::string,5> menu; std::array<bool,5> enabled{true,true,true,true,true}; std::string date,record; int selected=0; };
bool DrawNativeDynastyOptions();
bool DrawDynastyOptions(const DynastyOptionsCard&);
struct RecruitingCard { DynastySchoolCard school; std::array<std::string,4> menu; std::string points; int selected=0; };
struct RecruitListCard {
 std::string title,filter,points;
 std::array<std::array<std::string,5>,10> rows;
 std::array<float,10> interest{};
 int selected=0,first=0;
};
struct RecruitProfileCard {
 std::string name,position,rank,overall,status,page,eligibility,height,weight,bats,throws,home,from,attitude,academic,needs;
 std::array<std::string,5> years;
 std::array<std::string,6> labels,ratings;
 std::array<bool,5> locked{},unavailable{};
 float interest=0; uint32_t portrait=0; std::array<float,4> uv{0,0,1,1};
};
bool DrawRecruitProfile(const RecruitProfileCard&);
bool DrawNativeRecruitProfile();
bool DrawNativeRecruitList();
bool DrawRecruitList(const RecruitListCard& card);
bool DrawNativeRecruiting();
bool DrawRecruiting(const RecruitingCard& card);
bool DrawNativeBoxScore();
bool DrawBoxScore(const BoxScoreCard& card);
bool DrawNativeFinalScore();
bool DrawFinalScore(const FinalScoreCard& card);
bool DrawNativeSimGame();
bool DrawSimGame(const SimGameCard& card);
bool DrawNativeCoach();
bool DrawCoach(const CoachCard& card);
bool DrawNativeFinalPlaySim();
bool DrawFinalPlaySim(const FinalPlaySimCard& card);
bool DrawNativeStartingLineup();
bool DrawStartingLineup(const std::array<StartingLineupSide,2>& sides);
bool DrawNativeStartingPitcher();
bool DrawStartingPitcher(const std::array<StartingPitcherSide,2>& sides);
bool DrawPlaySim(const PlaySimCard& card);
void MapPlaySimPad(int port, input::Pad& pad);
void ResetActionNavigation();
bool DrawNativeSimulationProgress(bool calendarDrawn);
bool DrawSimulationProgress(const std::string& caption, float progress, bool canStop);
bool DrawDynastyCalendar(const DynastyCalendarCard& card);
void MapDashboardPad(int port,input::Pad&);
bool DrawNativeDynastyHome();
bool DrawDynastyHome(const DynastyHomeCard& card);
bool DrawNativeDynastySchool();
bool DrawDynastySchool(const DynastySchoolCard& card);
std::array<TeamSelectSide,2> SnapshotTeamSelect();
void ResetTeamSelect();
void PublishTeamSelect(const std::array<TeamSelectSide, 2>& sides, const std::vector<TeamSelectController>& players, const std::string& title = "TEAM SELECT");
int NativeTeamControllerLane(int port);
void MapTeamSelectPad(int port, input::Pad& pad, double elapsed);
void PollNativeTeamSelect();
void DrawTeamSelect(double elapsed);
}

namespace game {
struct StatisticsTableCard { std::string title,team,conference,mode,player,empty; unsigned color=0xA94B08; uint32_t portrait=0; std::array<float,4> uv{0,0,1,1}; int kind=0,selected=0,first=0; std::array<int,2> selections{}; std::array<std::array<std::string,16>,32> rows{}; std::array<std::array<std::string,10>,2> lists{}; std::array<std::string,8> labels{},values{}; };
bool DrawNativeStatisticsTable();
bool DrawStatisticsTable(const StatisticsTableCard&);
}

namespace game {
struct TeamManagementCard { DynastySchoolCard school; std::string date,record,scouting; std::array<std::string,10> menu; std::array<bool,10> enabled{true,true,true,true,true,true,true,true,true,true}; std::array<std::string,4> grades,coach; int selected=0; };
struct CreateEditCard { DynastySchoolCard school; bool loading=false; int selected=0; std::array<std::string,2> labels; };
bool DrawCreateEdit(const CreateEditCard&);
bool PlayerEditorLoading();
int CreateEditAuditStage();
bool DrawNativeCreateEdit();
bool DrawNativeTeamManagement(); bool DrawTeamManagement(const TeamManagementCard&);
void MapTeamManagementPad(int port,input::Pad&); int TeamManagementSelection(); bool TeamManagementAvailable(int);
}

namespace game {
struct DepthChartCard {
 DynastySchoolCard school; std::string total;
 struct Position { std::string label,count; std::array<std::array<std::string,3>,3> rows; bool selected=false; };
 std::array<Position,10> positions;
 bool overlay=false,actions=false; int first=0,selected=0,action=0;
 std::array<std::array<std::string,3>,30> players;
 std::array<std::string,4> commands;
};
bool DrawNativeDepthChart(); bool DrawDepthChart(const DepthChartCard&);
}

namespace game {
struct RosterEditorCard {
 DynastySchoolCard school; int kind=0,active=0; std::array<int,2> selected{},first{};
 std::string title,mode,player; uint32_t portrait=0;std::array<float,4> uv{0,0,1,1};
 std::array<std::array<std::array<std::string,4>,32>,2> rows{};
 std::array<std::string,6> attributes{},attributeLabels{};std::array<std::string,3> stats{},statLabels{};
 std::array<std::array<uint32_t,9>,2> zones{};
};
struct ManagementDetailCard {
 DynastySchoolCard school; bool goals=false; int active=0,selected=0;std::array<int,2> selections{};
 std::string position,view,description,help;
 std::array<std::array<std::array<std::string,5>,32>,2> rows{};
 std::array<bool,4> completed{};std::array<int,4> icons{};
 std::array<std::string,2> buttons{};int button=-1;
};
bool DrawNativeRosterEditor(); bool DrawRosterEditor(const RosterEditorCard&);
bool DrawNativeManagementDetail();bool DrawManagementDetail(const ManagementDetailCard&);
}

namespace game {
struct RosterStatusCard { DynastySchoolCard school;std::string active,inactive,player;int first=0,selected=0;uint32_t portrait=0;std::array<float,4> uv{0,0,1,1};std::array<std::array<std::string,7>,32> rows{};std::array<std::string,8> labels{},values{}; };
bool DrawNativeRosterStatus();bool DrawRosterStatus(const RosterStatusCard&);
}

namespace game { bool DrawNativeTournamentTree(); }

namespace game {
struct TournamentTreeCard {
 struct Team { std::string name,seed,score,record,logo; uint32_t color=0xff180c55; };
 struct Match { std::array<Team,2> teams; std::string letter,number; float x=0,y=0; bool selected=false; };
 std::string title,series,format;
 uint32_t logo=0;std::array<float,4> logoUv{0,0,1,1};float logoAspect=1;
 std::vector<Match> matches;
};
bool DrawTournamentTree(const TournamentTreeCard&);
}

namespace game {
struct RecruitActionsCard {
 RecruitProfileCard player;DynastySchoolCard school;std::string points;
 std::array<std::string,7> labels,costs;std::array<bool,7> checked{},enabled{};int selected=0;
};
struct RecruitOfferCard {
 RecruitProfileCard player;DynastySchoolCard school;std::string requestScholarship,requestTime,scholarship,time,current,next,button;int selected=0;
};
struct InboxCard {
 struct Message {std::string sender,subject,date;bool unread=false;};
 std::string date;std::array<Message,9> messages;int selected=0,first=0;
};
struct MailReaderCard {std::string sender,subject,date,age,index,hotlink;std::array<std::string,8> lines;int first=0;};
bool DrawNativeRecruitActions();bool DrawRecruitActions(const RecruitActionsCard&);
bool DrawNativeRecruitOffer();bool DrawRecruitOffer(const RecruitOfferCard&);
bool DrawNativeInbox();bool DrawInbox(const InboxCard&);
bool DrawNativeMailReader();bool DrawMailReader(const MailReaderCard&);
}

namespace game {
struct MailFiltersCard {std::array<std::string,19> labels,values;int selected=0,first=0;};
struct OffseasonCard {
 struct Player {std::string name,position,year,overall;uint32_t portrait=0;std::array<float,4> uv{0,0,1,1};};
 DynastySchoolCard school;int kind=0,active=0;bool pitcher=false;std::array<int,2> selected{},first{};
 std::array<Player,2> players;std::string count;
 std::array<std::array<std::array<std::string,7>,8>,2> rows;
 std::array<int,8> changes{};
 std::array<std::array<std::string,6>,2> ratings,deltas;
 std::array<std::array<std::string,6>,2> ratingLabels;
 std::array<std::string,8> statLabels,stats;
};
bool DrawNativeMailFilters();bool DrawMailFilters(const MailFiltersCard&);
bool DrawNativeOffseason();bool DrawOffseason(const OffseasonCard&);
}

namespace game {
struct HireStaffCard {
 std::string role;std::array<std::string,2> names,levels,overall;
 std::array<std::string,5> labels;std::array<std::array<std::string,5>,2> values;
 std::array<std::array<std::string,3>,3> pool{};int selected=0,first=0;
};
struct PostseasonScoresCard {
 struct Game {std::string label,date;std::array<std::string,2> names,logos,details;std::array<std::string,12> headings;std::array<std::array<std::string,12>,2> values;};
 std::string title,round,date,emptyMessage;std::array<Game,3> games;int selected=0;bool popup=false;
};
bool DrawNativeHireStaff();bool DrawHireStaff(const HireStaffCard&);
bool DrawNativePostseasonScores();bool DrawPostseasonScores(const PostseasonScoresCard&);
}


namespace game {
struct PlayerEditorCard {
 std::string title,page,grid,player,overall;DynastySchoolCard school;
 std::array<std::string,6> labels,values;std::array<bool,6> enabled{};
 std::array<std::string,9> zones{}; bool paired=false;
 int first=0,selected=0,total=0;bool ratings=false,nameField=false,edit=false;
};
bool DrawNativePlayerEditor();bool DrawPlayerEditor(const PlayerEditorCard&);
}

namespace game { bool NativeMenuPresentation(); }

namespace game { bool NativeMenuPresentation(); }

namespace game { uint32_t DashboardText(uint32_t id); bool BindNativeDashboard(uint32_t root,const DynastyHomeCard& card,const std::vector<std::string>& updates); }

struct X86Context;
namespace game {void RefreshNativeDashboard(X86Context& ctx,uint32_t root);}

namespace game {
struct AwardWatchCard {
 std::string award,group,group2,champion;std::array<std::string,5> headings{};
 std::array<std::array<std::string,5>,10> rows{};int selected=0,first=0,total=0,columns=0;
};
bool DrawNativeAwardWatch();bool DrawAwardWatch(const AwardWatchCard&);
}
