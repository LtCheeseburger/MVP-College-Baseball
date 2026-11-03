#pragma once
#include <array>
#include <string>
namespace game {
struct MatchupSide { std::string name, nickname, wins, losses, grade, rank; };
void ResetMatchupLoading();
void HideMatchupLoading();
bool MatchupLoadingVisible();
void BindMatchupLoading(unsigned root);
void PollNativeMatchupLoading();
void PublishMatchupLoading(const std::array<MatchupSide,2>& sides, bool visible, bool ready);
void DrawMatchupLoading(double elapsed);
bool MatchupLoadingReady();
std::array<MatchupSide,2> SnapshotMatchupLoading();
}
