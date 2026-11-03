#include "core/dev_tools.h"
#include "matchup_loading_ui.h"
#include "overhaul.h"
#include <sstream>
#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "ui_style.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace game {
namespace {
std::mutex mutex;
std::array<MatchupSide, 2> teams;
bool visible = false, ready = false;
std::filesystem::path Assets() {
  return overhaul::Root();
}
ImU32 GradeColor(const std::string &grade) {
  if (grade.empty())
    return IM_COL32(235, 235, 235, 255);
  if (grade[0] == 'A')
    return IM_COL32(255, 200, 55, 255);
  if (grade[0] == 'B' || grade[0] == 'C')
    return IM_COL32(211, 222, 232, 255);
  return IM_COL32(199, 133, 79, 255);
}
} // namespace
void ResetMatchupLoading() {
  std::lock_guard lock(mutex);
  teams = {};
  visible = ready = false;
}
void HideMatchupLoading() {
  std::lock_guard lock(mutex);
  visible = false;
}
bool MatchupLoadingVisible() {
  std::lock_guard lock(mutex);
  return visible;
}
bool MatchupLoadingReady() {
  std::lock_guard lock(mutex);
  return visible && ready;
}
std::array<MatchupSide, 2> SnapshotMatchupLoading() {
  std::lock_guard lock(mutex);
  return teams;
}
void PublishMatchupLoading(const std::array<MatchupSide, 2> &sides, bool show,
                           bool canContinue) {
  std::lock_guard lock(mutex);
  teams = sides;
  visible = show;
  ready = canContinue;
  if (dev::Env("MVP06_TRACE_FE")) {
    std::string key = std::to_string(ready) + std::to_string(visible);
    for (auto &t : teams)
      key += t.name + t.wins + t.losses + t.grade + t.rank;
    static std::string last;
    if (last != key) {
      last = key;
      for (auto &t : teams)
        LOGR("matchup loading: %s record=%s-%s overall=%s rank=%s visible=%d "
             "ready=%d",
             t.name.c_str(), t.wins.c_str(), t.losses.c_str(), t.grade.c_str(),
             t.rank.c_str(), visible, ready);
    }
  }
}
void DrawMatchupLoading(double elapsed) {
  std::array<MatchupSide, 2> state;
  bool show, canContinue;
  {
    std::lock_guard lock(mutex);
    state = teams;
    show = visible;
    canContinue = ready;
  }
  if (!show || !gpu::g_ui_image)
    return;
  static auto root = Assets();
  static auto logos = [&] {
    std::unordered_map<std::string, std::string> result;
    std::istringstream f(overhaul::Text(root / "base-logos/teams.tsv"));
    std::string line;
    while (std::getline(f, line)) {
      auto tab = line.find('\t');
      if (tab == std::string::npos)
        continue;
      auto id = line.substr(tab + 1);
      if (!id.empty() && id.back() == '\r')
        id.pop_back();
      result[line.substr(0, tab)] = id;
    }
    return result;
  }();
  static auto palettes = [&] {
    std::unordered_map<std::string, unsigned> result;
    std::istringstream f(overhaul::Text(root / "team-colors.tsv"));
    std::string line;
    while (std::getline(f, line)) {
      auto tab = line.find('\t');
      if (tab != std::string::npos)
        result[line.substr(0, tab)] =
            std::strtoul(line.c_str() + tab + 1, nullptr, 16);
    }
    return result;
  }();
  auto *d = ImGui::GetBackgroundDrawList();
  auto screen = ImGui::GetIO().DisplaySize;
  float s = std::min(screen.x / 2560.f, screen.y / 1440.f),
        ox = (screen.x - 2560 * s) / 2, oy = (screen.y - 1440 * s) / 2;
  auto p = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
  auto image = [&](const std::string &file, float x, float y, float w, float h,
                   ImU32 tint = IM_COL32_WHITE) {
    auto tex = gpu::g_ui_image(root / file);
    if (tex)
      d->AddImage((ImTextureID)tex, p(x, y), p(x + w, y + h), {0, 0}, {1, 1},
                  tint);
  };
  // Read the supplied atlas directly. Interior UVs exclude its gutters and
  // checkerboard; retaining the source avoids destructive asset processing.
  auto atlas = gpu::g_ui_image(root / "matchup-texture-atlas.png");
  auto uv = [](float x, float y) { return ImVec2(x / 1671.f, y / 941.f); };
  auto textureQuad = [&](ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 e, float u,
                         float v, float w, float h, ImU32 tint) {
    if (atlas)
      d->AddImageQuad((ImTextureID)atlas, a, b, c, e, uv(u, v), uv(u + w, v),
                      uv(u + w, v + h), uv(u, v + h), tint);
  };
  auto text = [&](UiFont font, float size, float x, float y, ImU32 color,
                  const std::string &value, float width) {
    UiText(d, font, size * s, p(x, y), color, value.c_str(), width * s);
  };
  auto centered = [&](UiFont font, float size, float x, float y, float width,
                      ImU32 color, const std::string &value) {
    auto *f = Font(font);
    float w = f->CalcTextSizeA(size * s, 100000, 0, value.c_str()).x;
    float scale = std::min(1.f, width * s / std::max(1.f, w));
    text(font, size * scale, x + (width - w * scale / s) / 2, y, color, value,
         width);
  };
  auto tracked = [&](const std::string &label, float cx, float y, float size,
                     float gap, ImU32 tint) {
    auto *f = Font(UiFont::Prompt);
    float width = 0;
    for (char c : label) {
      char ch[] = {c, 0};
      width += f->CalcTextSizeA(size * s, 10000, 0, ch).x / s + gap;
    }
    float x = cx - (width - gap) / 2;
    for (char c : label) {
      char ch[] = {c, 0};
      text(UiFont::Prompt, size, x, y, tint, ch, 100);
      x += f->CalcTextSizeA(size * s, 10000, 0, ch).x / s + gap;
    }
  };
  d->AddRectFilled({0, 0}, screen, IM_COL32(0, 0, 0, 255));
  image("team-select-stadium-imagegen.png", 0, 0, 2560, 1440);
  d->AddRectFilled(p(0, 0), p(2560, 1440), IM_COL32(0, 0, 0, 75));
  // One wide atlas region across the screen preserves the original texture's
  // proportions. Do not stretch the entire horizontal atlas into each upright
  // panel.
  textureQuad(p(0, 0), p(2560, 0), p(2560, 670), p(0, 670), 18, 16, 1080, 288,
              IM_COL32(255, 255, 255, 35));
  d->AddRectFilledMultiColor(p(0, 922), p(2560, 1440), IM_COL32(0, 0, 0, 180),
                             IM_COL32(0, 0, 0, 180), IM_COL32(0, 0, 0, 230),
                             IM_COL32(0, 0, 0, 230));
  for (int i = 0; i < 2; ++i) {
    auto it = palettes.find(state[i].name);
    unsigned rgb =
        it == palettes.end() ? (i ? 0xBD500B : 0x004AC0) : it->second;
    auto color = [&](int a) {
      return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
    };
    float top = i ? 1710 : 0, bottom = i ? 1480 : 0;
    d->AddQuadFilled(p(top, 0), p(i ? 2560 : 890, 0), p(i ? 2560 : 950, 670),
                     p(bottom, 670), color(120));
    float tx = i ? 690 : 18;
    textureQuad(p(top, 0), p(i ? 2560 : 890, 0), p(i ? 2560 : 950, 670),
                p(bottom, 670), tx, 16, i ? 408 : 400, 288, color(65));
    auto logo = logos.find(state[i].name);
    if (logo != logos.end())
      image("base-logos/" + logo->second + ".png", i ? 1800 : 250, 200, 450,
            450);
    float left = i ? 1468 : 0, right = i ? 2560 : 1130;
    d->AddQuadFilled(p(left, 670), p(right, 670), p(right, 922),
                     p(i ? 1428 : 0, 922), color(245));
    textureQuad(p(left, 670), p(right, 670), p(right, 922),
                p(i ? 1428 : 0, 922), 700, 458, 533, 114, color(85));
    d->AddLine(p(left, 670), p(right, 670), IM_COL32(225, 225, 225, 255),
               2 * s);
    d->AddLine(p(i ? 1428 : 0, 922), p(right, 922),
               IM_COL32(225, 225, 225, 255), 2 * s);
    if (!i)
      d->AddRectFilled(p(44, 673), p(66, 919), IM_COL32(250, 107, 9, 255));
    centered(UiFont::TeamDisplay, 205, i ? 1530 : 130, 665, i ? 950 : 890,
             IM_COL32_WHITE, state[i].name);
    float cx = i ? 1990 : 660;
    tracked(state[i].nickname, cx, 849, 43, 11, IM_COL32_WHITE);
    if (!state[i].nickname.empty()) {
      float nw = 0;
      for (char c : state[i].nickname) {
        char ch[] = {c, 0};
        nw += Font(UiFont::Prompt)->CalcTextSizeA(43 * s, 10000, 0, ch).x / s +
              11;
      }
      float half = nw / 2 + 28;
      if (half < 350) {
        d->AddRectFilled(p(cx - half - 150, 871), p(cx - half, 880),
                         i ? IM_COL32_WHITE : IM_COL32(255, 110, 12, 255));
        d->AddRectFilled(p(cx + half, 871), p(cx + half + 150, 880),
                         i ? IM_COL32_WHITE : IM_COL32(255, 110, 12, 255));
      }
    }
    float stats = i ? 1338 : 130;
    d->AddRectFilled(p(stats, 960), p(stats + 1090, 1180),
                     IM_COL32(3, 5, 7, 242));
    d->AddRect(p(stats, 960), p(stats + 1090, 1180),
               IM_COL32(130, 134, 139, 255), 0, 0, 2 * s);
    d->AddLine(p(stats, 1035), p(stats + 1090, 1035),
               IM_COL32(110, 114, 119, 255), s);
    const char *labels[] = {"RECORD", "OVERALL", "NATIONAL RANK"};
    std::string values[] = {
        state[i].wins.empty() ? "—" : state[i].wins + " - " + state[i].losses,
        state[i].grade.empty() ? "—" : state[i].grade,
        state[i].rank.empty()
            ? "—"
            : (state[i].rank == "0" ? "NR" : "#" + state[i].rank)};
    for (int j = 0; j < 3; ++j) {
      float cell = stats + j * 363.33f;
      if (j)
        d->AddLine(p(cell, 960), p(cell, 1173), IM_COL32(90, 94, 99, 255), s);
      tracked(labels[j], cell + 181.67f, 980, 35, 2,
              IM_COL32(220, 220, 220, 255));
      int first = d->VtxBuffer.Size;
      centered(UiFont::TeamDisplay, j == 1 ? 145 : 120, cell, 1030, 363,
               j == 1 ? GradeColor(state[i].grade) : IM_COL32_WHITE, values[j]);
      // A small vertical sheen gives the grade its gold/silver/bronze finish.
      if (j == 1)
        for (int v = first; v < d->VtxBuffer.Size; ++v) {
          auto &vert = d->VtxBuffer[v];
          float y = (vert.pos.y - p(0, 1045).y) / (112 * s);
          float light = y < .3f ? 1.25f : (y < .55f ? 1.05f : .90f);
          auto col = GradeColor(state[i].grade);
          vert.col =
              IM_COL32(std::min(255, int((col & 255) * light)),
                       std::min(255, int(((col >> 8) & 255) * light)),
                       std::min(255, int(((col >> 16) & 255) * light)), 255);
        }
    }
    d->AddRectFilled(p(stats, 1174), p(stats + 1090, 1187), color(255));
  }
  d->AddQuadFilled(p(890, 0), p(1710, 0), p(1480, 670), p(950, 670),
                   IM_COL32(2, 3, 4, 245));
  textureQuad(p(890, 0), p(1710, 0), p(1480, 670), p(950, 670), 360, 18, 390,
              280, IM_COL32(255, 255, 255, 65));
  d->AddLine(p(890, 0), p(950, 670), IM_COL32(225, 0, 25, 255), 6 * s);
  d->AddLine(p(1710, 0), p(1480, 670), IM_COL32(225, 0, 25, 255), 4 * s);
  d->AddQuadFilled(p(1130, 670), p(1468, 670), p(1428, 922), p(1130, 922),
                   IM_COL32(5, 6, 8, 250));
  centered(UiFont::TeamVersus, 190, 1140, 678, 290, IM_COL32_WHITE, "VS");
  d->AddRectFilled(p(1190, 873), p(1360, 888), IM_COL32(245, 0, 28, 255));
  image("lg27.png", 1020, 190, 520, 173);
  textureQuad(p(0, 1210), p(2560, 1210), p(2560, 1440), p(0, 1440), 18, 215,
              1080, 90, IM_COL32(160, 160, 160, 180));
  d->AddRectFilled(p(0, 1220), p(2560, 1440), IM_COL32(0, 0, 0, 145));
  d->AddLine(p(100, 1255), p(900, 1255), IM_COL32(170, 7, 25, 255), 2 * s);
  d->AddLine(p(1660, 1255), p(2460, 1255), IM_COL32(170, 7, 25, 255), 2 * s);
  d->AddLine(p(0, 1315), p(2560, 1315), IM_COL32(110, 112, 116, 120), s);
  if (canContinue) {
    constexpr float icon = 58, gap = 22, size = 44;
    const char *label = "PRESS A TO CONTINUE";
    float width =
        Font(UiFont::Prompt)->CalcTextSizeA(size * s, 100000, 0, label).x / s;
    float left = (2560 - icon - gap - width) / 2;
    image("controller-reference-a.png", left, 1242, icon, icon);
    text(UiFont::Prompt, size, left + icon + gap, 1243, IM_COL32_WHITE, label,
         width + 1);
  } else {
    int alpha = int(145 + 95 * (.5 + .5 * std::sin(elapsed * 3.2)));
    tracked("LOADING GAME", 1280, 1244, 35, 11, IM_COL32(225, 225, 225, alpha));
    for (int i = 0; i < 3; ++i) {
      float pulse = .5f + .5f * std::sin(float(elapsed) * 4 - i * .9f);
      d->AddCircleFilled(p(1248 + i * 32, 1310), 4 * s,
                         IM_COL32(235, 35, 49, int(65 + 190 * pulse)), 16);
    }
  }
  if (dev::Env("MVP06_TRACE_FE")) {
    static int stage = -1;
    int next = canContinue ? 1 : 0;
    if (elapsed > .2 && stage != next) {
      stage = next;
      gpu::g_capture = true;
    }
  }
}
} // namespace game
