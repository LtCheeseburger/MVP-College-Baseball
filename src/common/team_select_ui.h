#pragma once
#include <array>
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
  std::array<std::string, 5> grades;
};
std::array<TeamSelectSide,2> SnapshotTeamSelect();
void ResetTeamSelect();
void PublishTeamSelect(const std::array<TeamSelectSide, 2>& sides, const std::vector<TeamSelectController>& players);
int NativeTeamControllerLane(int port);
void MapTeamSelectPad(int port, input::Pad& pad, double elapsed);
void PollNativeTeamSelect();
void DrawTeamSelect(double elapsed);
}
