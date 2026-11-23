#include "core/dev_tools.h"
#include "batter_info_ui.h"
#include "overhaul.h"
#include <sstream>
#include "app/app.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "ui_style.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <unordered_map>
namespace game {
namespace {
std::mutex mutex;
BatterCard state;
bool exiting = false;
} // namespace
void BatterCardTimeline(const char *name) {
  std::lock_guard lock(mutex);
  if (std::string(name) == "OVEXIT")
    exiting = true;
  else if (std::string(name) == "SHOW")
    exiting = false;
}
void PublishBatterCard(const BatterCard &value) {
  std::lock_guard lock(mutex);
  state = value;
}
void DrawBatterCard() {
  BatterCard c;
  {
    std::lock_guard lock(mutex);
    c = state;
    if (exiting)
      return;
  }
  if (!c.visible || !gpu::g_ui_image)
    return;
  if (dev::Env("MVP06_TRACE_FE")) {
    static std::string captured;
    auto key = c.team + c.name + c.number;
    if (key != captured) {
      captured = key;
      gpu::g_capture = true;
    }
  }
  static auto root = overhaul::Root();
  static auto colors = [&] {
    std::unordered_map<std::string, unsigned> r;
    std::istringstream f(overhaul::Text(root / "team-colors.tsv"));
    std::string line;
    while (std::getline(f, line)) {
      auto t = line.find('\t');
      if (t != std::string::npos)
        r[line.substr(0, t)] = std::strtoul(line.c_str() + t + 1, nullptr, 16);
    }
    return r;
  }();
  unsigned rgb = 0x104BAB;
  if (auto i = colors.find(c.team); i != colors.end())
    rgb = i->second;
  auto *d = ImGui::GetBackgroundDrawList();
  auto screen = ImGui::GetIO().DisplaySize;
  float s = std::min(screen.x / 2560.f, screen.y / 1440.f);
  auto p = [&](float x, float y) {
    return ImVec2((screen.x - 1320 * s) / 2 + x * s,
                  (screen.y - 1440 * s) / 2 + (1050 + y) * s);
  };
  ImU32 primary = IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255),
        white = IM_COL32(245, 247, 250, 255);
  d->AddRectFilled(p(0, 0), p(1320, 228), IM_COL32(10, 14, 20, 250));
  d->AddRectFilled(p(0, 0), p(6, 228), primary);
  d->AddRectFilledMultiColor(p(6,0),p(230,228),primary,IM_COL32(14,20,30,255),IM_COL32(9,13,20,255),primary);
  if(auto material=gpu::g_ui_image(root/"pause-v2/team-surface.png"))d->AddImage((ImTextureID)material,p(6,0),p(230,228),{0,0},{.5f,1},IM_COL32(255,255,255,24));
  if (auto logo = gpu::g_ui_image(overhaul::TeamLogo(c.team,{},c.logo)))
    d->AddImage((ImTextureID)logo, p(35, 22), p(199, 186));
  auto text = [&](UiFont font, float size, float x, float y,
                  const std::string &value, float width) {
    UiText(d, font, size * s, p(x, y), white, value.c_str(), width * s);
  };
  d->AddRectFilledMultiColor(p(230, 0), p(1320, 83), primary,
                             IM_COL32(14, 19, 26, 255),
                             IM_COL32(14, 19, 26, 255), primary);
  const bool generic=c.name==c.position+" #"+c.number;
  if(generic)text(UiFont::TeamDisplay,51,270,9,c.name,900);
  else {text(UiFont::TeamDisplay,45,258,12,"#"+c.number,98);text(UiFont::TeamDisplay,51,380,9,c.name,790);}
  text(UiFont::Heading, 44, 1220, 15, c.position, 78);
  d->AddLine(p(252, 84), p(1295, 84), IM_COL32(255, 255, 255, 55), s);
  if (!c.rank.empty()) {
    auto rank = c.rank == "0" ? "UNRANKED" : "TEAM RANK #" + c.rank;
    UiText(d, UiFont::Prompt, 24 * s, p(22, 197), IM_COL32(220, 228, 239, 255),
           rank.c_str(), 202 * s);
  }
  for (int i = 0; i < 3; ++i) {
    float x = 270 + i * 345;
    text(UiFont::Heading, 29, x, 105, c.labels[i], 320);
    text(UiFont::TeamDisplay, 55, x, 146, c.values[i], 275);
    if (i)
      d->AddLine(p(x - 24, 107), p(x - 24, 202), IM_COL32(255, 255, 255, 40),
                 s);
  }
  d->AddRect(p(0, 0), p(1320, 228), IM_COL32(156, 167, 179, 145), 0, 0, s);
  d->AddRectFilled(p(230, 224), p(1320, 228), primary);
}
} // namespace game
