#pragma once
#include "matchup_loading_ui.h"
namespace game {
void BindStadiumIntro(unsigned root);
void PollNativeStadiumIntro();
void PublishStadiumIntro(const std::array<MatchupSide,2>& teams,const std::string& stadium,bool visible);
void StadiumIntroTimeline(const char* name);
void DrawStadiumIntro();
}
