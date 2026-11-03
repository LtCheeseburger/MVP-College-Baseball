#pragma once
#include <array>
#include <string>
#include <cstdint>
namespace game {
struct PitcherCard {
  std::string name,number,position,team,teamLogo,rank,stamina;
  std::array<std::string,5> pitches,ratings;
  uint32_t portrait=0;
  std::array<float,4> portraitUv{0,0,1,1};
  bool visible=false;
};
void PublishPitcherCard(const PitcherCard& card);
void DrawPitcherCard();
void PitcherCardTimeline(const char* name);
void PollNativePitcherInfo();
}
