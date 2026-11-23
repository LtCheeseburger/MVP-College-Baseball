#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace game {
struct StadiumSelectionCard {std::string venue,time,wind;bool timeSelected=false;};
bool DrawStadiumSelection(const StadiumSelectionCard& card);
struct RosterBrowserCard {
  bool trade=false;
  std::string team, conference, player;
  std::array<std::string,2> listNames;
  std::array<std::vector<std::array<std::string,6>>,2> rows;
  std::array<int,2> selected{}, first{};
  int active=0;
  uint32_t portrait=0;
  std::array<float,4> uv{0,0,1,1};
  std::vector<std::pair<std::string,std::string>> attributes;
};
struct PlayerInformationCard {
  std::string name, position, number, page;
  std::vector<std::pair<std::string,std::string>> identity, stats;
  uint32_t portrait=0;
  std::array<float,4> uv{0,0,1,1};
};
struct TextKeyboardCard {
 std::string title,entered;
 std::array<std::array<std::string,13>,5> keys{};
 std::array<std::array<int,13>,5> keyOwner{};
 int x=0,y=0;
};
bool DrawTextKeyboard(const TextKeyboardCard&);
bool DrawNativeTextKeyboard();
struct UtilityMenuCard {
 std::string title,subtitle;
 std::vector<std::string> labels,values;
 std::vector<bool> enabled;
 int selected=0;
 bool slots=false,replay=false,hidden=false,teamSwitch=false,loading=false;
};
bool DrawUtilityMenu(const UtilityMenuCard&);
bool DrawNativeUtilityMenu();
struct BallparkEditorCard {
 std::array<std::string,5> labels,values;
 std::string mode,measurement,divider,points;
 int selected=0;
 bool wall=false,hidden=false;
};
bool DrawBallparkEditor(const BallparkEditorCard&);
bool DrawNativeBallparkEditor();
bool DrawNativeBallparkLoading();
bool DrawRosterBrowser(const RosterBrowserCard&);
bool DrawPlayerInformation(const PlayerInformationCard&,bool backdrop);
bool DrawNativeRosterBrowser();
bool DrawNativePlayerInformation();
}
