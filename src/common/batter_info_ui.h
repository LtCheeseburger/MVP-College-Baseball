#pragma once
#include <array>
#include <string>
namespace game {
struct BatterCard {
  std::string name,number,position,team,logo,rank;
  std::array<std::string,3> labels,values;
  bool visible=false;
};
void PublishBatterCard(const BatterCard&);
void DrawBatterCard();
void PollNativeBatterInfo();
void BatterCardTimeline(const char*);
}
