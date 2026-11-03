#include "core/dev_tools.h"
#include "pause_menu_ui.h"
#include "overhaul.h"
#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_ops.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "kernel/memory.h"
#include "matchup_loading_ui.h"
#include "pause_art.h"
#include "submenu_ui.h"
#include "ui_style.h"
#include "xdk/hle.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace game {
std::string NativeUiText(uint32_t);
std::string UiString(uint32_t);
namespace {
std::mutex mutex;
struct RenderRecord {
  uint32_t vtable, name, render;
};
using RenderMap = std::unordered_map<uint32_t, RenderRecord>;
std::unordered_set<std::string> pauseFamilies;
std::atomic<bool> pauseContext{false};
std::array<std::atomic<uint32_t>, 64> nativeSceneRoots{};
std::atomic<bool> nativeDrawTreesDirty{false};
std::atomic<std::shared_ptr<const RenderMap>> renderNodes{
    std::make_shared<const RenderMap>()};
bool FilterNativeRender(uint32_t target, uint32_t self) {
  // Only FEGroup::Render is pure drawing. Derived Render methods may update
  // native modal state and must retain their original behavior.
  if (target != 0x000BBAC0)
    return false;
  if (nativeDrawTreesDirty.exchange(false)) {
    auto snapshot = renderNodes.load();
    for (auto &entry : nativeSceneRoots)
      if (auto root = entry.load()) {
        auto it = snapshot->find(root);
        if (it == snapshot->end() || !mem::IsCommitted(root + 0x24) ||
            kernel::R32(root + 0x10) != it->second.name) {
          entry = 0;
          continue;
        }
        auto vt = kernel::R32(root);
        if (vt < 0x500000 || vt > 0x800000 || !mem::IsCommitted(vt + 4)) {
          entry = 0;
          continue;
        }
        SuppressNativeUiScene(root);
      }
  }
  auto snapshot = renderNodes.load();
  auto it = snapshot->find(self);
  if (it == snapshot->end()) {
    if (pauseContext.load() && self >= 0x80000000 && self < 0x90000000 &&
        mem::IsCommitted(self + 0x28) && kernel::R32(self + 0x24) == 0) {
      auto name = kernel::ReadCString(self + 0x10, 16), base = name;
      while (!base.empty() && std::isdigit((unsigned char)base.back()))
        base.pop_back();
      if (pauseFamilies.count(base))
        return true;
    }
    return false;
  }
  return mem::IsCommitted(self + 0x14) &&
         kernel::R32(self) == it->second.vtable &&
         kernel::R32(self + 0x10) == it->second.name;
}

struct Scene {
  uint32_t root;
  std::string name;
  std::unordered_map<std::string, std::string> tags;
  std::unordered_map<uint32_t, std::string> types;
  std::unordered_set<uint32_t> hidden;
};
std::vector<Scene> scenes;
std::unordered_map<uint32_t, std::string> nativeTypes;
bool showInfo = false;
std::chrono::steady_clock::time_point infoCaptureAt;
std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>>
    defaultIds;
std::unordered_map<std::string, std::unordered_map<int, std::string>>
    defaultRows;
std::filesystem::path Assets() { return overhaul::Root().parent_path().parent_path(); }
bool Valid(uint32_t p) {
  return p >= 0x10000 && p < 0xF0000000 && mem::IsCommitted(p) &&
         mem::IsCommitted(p + 0x220);
}
struct Text {
  std::string name, value;
  float x, y, w, h;
  int row;
  std::string grid, kind;
  int column = -1;
};
std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
Registry() {
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      result;
  for (auto file : {"fes_popup.fel",
                    "fes_navbar.fel",
                    "fes_ingamepausemenu.fel",
                    "fes_ingameoptionmenu.fel",
                    "fes_ingamerosteroptions.fel",
                    "fes_ingamerosterbatteroptions.fel",
                    "fes_ingamerosterpitcheroptions.fel",
                    "fes_ingameroster.fel",
                    "fes_ingameboxscore.fel",
                    "fes_ingamebullpen.fel",
                    "fes_ingamepitchersinbullpen.fel",
                    "fes_ingamebattinglineup.fel",
                    "fes_ingamesettings.fel",
                    "fes_ingametuning.fel",
                    "fes_ingametuningselect.fel",
                    "fes_ingamevisualoptions.fel",
                    "fes_ingamecontrolmenu.fel",
                    "fes_ingamecontrolcfg.fel",
                    "fes_ingamecontrollerinfo.fel",
                    "fes_ingamepauseinfo.fel",
                    "fes_ingamestandings.fel",
                    "fes_defensivepositions.fel"}) {
    std::istringstream input(overhaul::Text(Assets() / "frontend/ingame_big" / file));
    std::string line, scene;
    int row = -1;
    while (std::getline(input, line)) {
      auto start = line.find_first_not_of(' ');
      if (start == std::string::npos || line.size() < start + 4 ||
          line[start + 2] != ':')
        continue;
      auto tag = line.substr(start, 2);
      auto end = line.find(',', start + 3);
      auto name = line.substr(start + 3, end - start - 3);
      if (tag == "SC") {
        row = -1;
        scene = name;
        if (!scene.empty() && std::isdigit((unsigned char)scene.back()))
          scene.clear();
      } else if (!scene.empty() &&
                 (tag == "TX" || tag == "TB" || tag == "TL" || tag == "SH" ||
                  tag == "SL" || tag == "RT" || tag == "GG" || tag == "GR" ||
                  tag == "FC" || tag == "FR" || tag == "BU" || tag == "SG")) {
        if (tag == "GR" && name.rfind("X0Y", 0) == 0)
          row = std::atoi(name.c_str() + 3);
        if (tag == "TB" && row >= 0 && row < 16)
          defaultRows[scene][row] = name;
        auto &known = result[scene][name];
        if (known.empty())
          known = tag;
        else if (known != tag)
          known = "?";
        if (tag == "TX" || tag == "TB" || tag == "TL") {
          std::stringstream fields(line.substr(start));
          std::string field;
          for (int i = 0; i <= 14 && std::getline(fields, field, ','); ++i)
            if (i == 14) {
              int id = std::atoi(field.c_str());
              if (id >= 0)
                defaultIds[scene][name] = uint32_t(id);
            }
        }
      }
    }
  }
  for (const auto &[name, tags] : result)
    pauseFamilies.insert(name);
  return result;
}
std::string Title(const std::string &name) {
  static const std::unordered_map<std::string, std::string> titles{
      {"PAUSEMENU", "GAME PAUSED"},
      {"OPTIONMENU", "GAME OPTIONS"},
      {"IGROSTEROPT", "ROSTER OPTIONS"},
      {"INGAMEBOXSCORE", "BOX SCORE"},
      {"INGAMESETTINGS", "GAME SETTINGS"},
      {"IGPAUSEINFO", "GAME INFORMATION"},
      {"INGAMEROSTER", "ROSTERS"},
      {"IGROSTERBOPT", "BATTER OPTIONS"},
      {"IGROSTERPOPT", "PITCHER OPTIONS"},
      {"BATTINGLINEUP", "BATTING ORDER"},
      {"BULLPEN", "BULLPEN"},
      {"PITCHERINBULL", "BULLPEN PITCHERS"},
      {"DEFENSIVEPOS", "DEFENSIVE ALIGNMENT"},
      {"INGAMETUNING", "GAMEPLAY TUNING"},
      {"TUNINGSELECT", "GAMEPLAY TUNING"},
      {"INGAMECTRLRCFG", "CONTROLLER SETTINGS"},
      {"GAMECONTROLS", "GAME CONTROLS"},
      {"IGSTANDINGS", "STANDINGS"},
      {"POPUP", "CONFIRM"},
      {"INGAMECTRLRMENU", "SELECT CONTROLLER"}};
  auto i = titles.find(name);
  return i == titles.end() ? "GAME PAUSED" : i->second;
}
} // namespace
bool NativeUiRenderSuppressed(uint32_t root, uint32_t target) {
  return FilterNativeRender(target, root);
}
bool PauseMenuThemeActive() {
  std::lock_guard lock(mutex);
  return pauseContext;
}
void ClearNativeUiSuppression() {
  for (auto &root : nativeSceneRoots)
    root = 0;
  nativeDrawTreesDirty = false;
  renderNodes.store(std::make_shared<const RenderMap>(),
                    std::memory_order_release);
}
void SuppressNativeUiScene(uint32_t root) {
  if (!Valid(root))
    return;
  auto rootVtable = kernel::R32(root);
  if (rootVtable < 0x500000 || rootVtable > 0x800000 ||
      !mem::IsCommitted(rootVtable + 4))
    return;
  bool registered = false;
  for (auto &entry : nativeSceneRoots)
    if (entry.load() == root) {
      registered = true;
      break;
    }
  if (!registered)
    for (auto &entry : nativeSceneRoots)
      if (!entry.load()) {
        entry = root;
        nativeDrawTreesDirty = true;
        break;
      }
  x86_set_ui_render_filter(FilterNativeRender);
  auto previous = renderNodes.load();
  auto first = kernel::R32(root + 8);
  if (previous->count(root) && previous->at(root).vtable == kernel::R32(root) &&
      previous->at(root).name == kernel::R32(root + 0x10) && Valid(first) &&
      kernel::R32(first + 0x24) > kernel::R32(root + 0x24) &&
      previous->count(first))
    return;
  auto updated = std::make_shared<RenderMap>(*previous);
  auto insert = [&](uint32_t node) {
    auto vt = kernel::R32(node);
    if (mem::IsCommitted(vt + 4))
      (*updated)[node] = {vt, kernel::R32(node + 0x10), kernel::R32(vt + 4)};
  };
  insert(root);
  auto depth = kernel::R32(root + 0x24);
  for (auto node = kernel::R32(root + 8), n = 0u;
       n < 8192 && Valid(node) && kernel::R32(node + 0x24) > depth;
       node = kernel::R32(node + 8), ++n)
    insert(node);
  if (dev::Env("MVP06_TRACE_FE"))
    LOGR("native UI draw filter: %s nodes=%zu render=%08X",
         kernel::ReadCString(root + 0x10, 16).c_str(),
         updated->size() - previous->size(),
         kernel::R32(kernel::R32(root) + 4));
  renderNodes.store(updated, std::memory_order_release);
}
void BindPauseMenuTheme(const char *name, uint32_t root) {
  static auto registry = Registry();
  std::lock_guard lock(mutex);
  std::string sceneName = name;
  nativeDrawTreesDirty = true;
  if (sceneName == "MAINMENU" || sceneName == "LOADING1" ||
      sceneName == "TEAMSELECT" || sceneName == "HUDRIGHT" ||
      sceneName == "HUDLEFT") {
    ClearNativeUiSuppression();
    scenes.clear();
    showInfo = false;
    pauseContext = false;
    return;
  }
  if (sceneName == "PAUSEMENU") {
    pauseContext = true;
    showInfo = false;
  }
  if (sceneName == "POPUP") {
    if (pauseContext)
      SuppressNativeUiScene(root);
    return; // Existing compact notification owns this modal.
  }
  if (pauseContext && (sceneName == "CNTRLSELECT" ||
                       sceneName == "OPPONENTPAUSE" || sceneName == "FTICKER" ||
                       sceneName == "FTICKERMATTE" || sceneName == "LOGOANIM"))
    SuppressNativeUiScene(root);
  if (!pauseContext)
    return;
  auto i = registry.find(name);
  if (i == registry.end())
    return;
  scenes.erase(std::remove_if(scenes.begin(), scenes.end(),
                              [&](const Scene &s) { return s.name == name; }),
               scenes.end());
  SuppressNativeUiScene(root);
  scenes.push_back({root, name, i->second, {}, {}});
}
void PauseMenuThemeTimeline(const char *owner, const char *name) {
  std::lock_guard lock(mutex);
  if (std::string(owner) == "IGPAUSEINFO") {
    if (std::string(name) == "INFOON") {
      showInfo = true;
      infoCaptureAt =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    }
    if (std::string(name) == "INFOOFF")
      showInfo = false;
  }
}
void DrawPauseMenuTheme() {
  std::lock_guard lock(mutex);
  Scene *active = nullptr, *fallback = nullptr;
  std::vector<uint32_t> fallbackNodes;
  std::vector<uint32_t> nodes;
  for (auto si = scenes.rbegin(); si != scenes.rend(); ++si) {
    auto &scene = *si;
    scene.types.insert(nativeTypes.begin(), nativeTypes.end());
    auto root = scene.root;
    if (!Valid(root) || kernel::ReadCString(root + 0x10, 16) != scene.name)
      continue;
    SuppressNativeUiScene(root);
    auto depth = kernel::R32(root + 0x24);
    if (depth >= 127)
      continue;
    std::vector<uint32_t> candidate;
    bool focused = scene.name == "POPUP" && (kernel::R16(root + 0x28) & 2);
    std::array<bool, 128> shown{};
    shown[depth] = (kernel::R16(root + 0x28) & 1) != 0;
    for (auto node = kernel::R32(root + 8), n = 0u;
         n < 4096 && Valid(node) && kernel::R32(node + 0x24) > depth;
         node = kernel::R32(node + 8), ++n) {
      auto level = kernel::R32(node + 0x24);
      if (level >= 128)
        continue;
      auto name = kernel::ReadCString(node + 0x10, 16);
      auto v = kernel::R32(node);
      if (auto tag = scene.tags.find(name);
          tag != scene.tags.end() && tag->second != "?")
        scene.types.emplace(v, tag->second);
      auto type = scene.types.find(v);
      bool leaf = type != scene.types.end() &&
                  (type->second == "TX" || type->second == "TB" ||
                   type->second == "TL" || type->second == "SH" ||
                   type->second == "SL" || type->second == "RT");
      shown[level] = shown[level - 1] && bool(kernel::R16(node + 0x28) & 1);
      if (shown[level] && type != scene.types.end() && type->second == "GG" &&
          (kernel::R16(node + 0x28) & 2))
        focused = true;
      candidate.push_back(node);
    }
    if (!fallback && (kernel::R16(root + 0x28) & 1) && scene.name != "NAVBAR" &&
        scene.name != "IGPAUSEINFO") {
      fallback = &scene;
      fallbackNodes = candidate;
    }
    if (focused) {
      active = &scene;
      nodes = std::move(candidate);
      break;
    }
  }
  if (!active && fallback) {
    active = fallback;
    nodes = std::move(fallbackNodes);
  }
  if (SubmenuPopupVisible())
    return;
  static bool previouslyActive = false;
  if (!active) {
    if (previouslyActive && dev::Env("MVP06_TRACE_FE"))
      gpu::g_capture = true;
    previouslyActive = false;
    return;
  }
  nativeTypes.insert(active->types.begin(), active->types.end());
  previouslyActive = true;
  auto depth = kernel::R32(active->root + 0x24);
  std::array<bool, 128> shown{};
  std::array<std::string, 128> gridNames;
  std::array<int, 128> columns{};
  std::array<int, 128> rows;
  rows.fill(-1);
  shown[depth] = true;
  std::vector<Text> texts, controllers;
  std::array<bool, 16> enabled;
  enabled.fill(true);
  std::vector<std::string> options;
  int selected = 0;
  uint32_t menuGrid = 0;
  uint32_t selectedCell = 0;
  std::string wind, focusedGrid;
  float gridY = 0, gridHeight = 480;
  std::array<std::array<std::string, 3>, 2> scores;
  for (auto node : nodes) {
    auto level = kernel::R32(node + 0x24);
    if (level >= 128)
      continue;
    gridNames[level] = gridNames[level - 1];
    columns[level] = columns[level - 1];
    auto name = kernel::ReadCString(node + 0x10, 16);
    auto it = active->types.find(kernel::R32(node));
    if (it == active->types.end()) {
      shown[level] = shown[level - 1] && (kernel::R16(node + 0x28) & 1);
      rows[level] = rows[level - 1];
      continue;
    }
    auto tag = it->second;
    bool leaf = tag == "TX" || tag == "TB" || tag == "TL" || tag == "SH" ||
                tag == "SL" || tag == "RT";
    shown[level] = shown[level - 1] && bool(kernel::R16(node + 0x28) & 1);
    rows[level] = rows[level - 1];
    if (tag == "GG") {
      gridNames[level] = name;
      rows[level] = -1;
      columns[level] = -1;
    }
    if (tag == "GR" && name.size() > 3 && name[0] == 'X')
      if (auto at = name.find('Y'); at != std::string::npos) {
        rows[level] = std::atoi(name.c_str() + at + 1);
        columns[level] = std::atoi(name.c_str() + 1);
      }
    if (menuGrid && tag == "GR" && name.rfind("X0Y", 0) == 0 &&
        rows[level] >= 0 && rows[level] < 16 &&
        kernel::R32(node + 0x24) == kernel::R32(menuGrid + 0x24) + 1)
      enabled[rows[level]] = (kernel::R16(node + 0x28) & 2) != 0;
    if (tag == "GG" && shown[level] && (kernel::R16(node + 0x28) & 2) &&
        (!menuGrid || name == "MENUOPTIONS")) {
      menuGrid = node;
      focusedGrid = name;
      gridY = int16_t(kernel::R16(node + 0x2C));
      gridHeight = kernel::R16(node + 0x30);
      selected = kernel::R32(node + 0x6C) +
                 kernel::R32(node + 0x4C) * kernel::R32(node + 0x70);
      auto table = kernel::R32(node + 0x9C);
      auto count =
          uint64_t(kernel::R32(node + 0x4C)) * kernel::R32(node + 0x50);
      if (selected >= 0 && uint64_t(selected) < count && count <= 4096 &&
          table >= 0x10000 && mem::IsCommitted(table + selected * 4 + 3))
        selectedCell = kernel::R32(table + selected * 4);
    }
    if (!shown[level])
      continue;
    if (tag == "SH" && name.rfind("CONTROLLER", 0) == 0 && name.back() != 'C' &&
        ((kernel::R16(node + 0x28) & 1) || active->hidden.count(node)))
      controllers.push_back({name, "", float(int16_t(kernel::R16(node + 0x2A))),
                             float(int16_t(kernel::R16(node + 0x2C))),
                             float(kernel::R16(node + 0x2E)),
                             float(kernel::R16(node + 0x30)), -1});
    if (tag == "TX" || tag == "TB" || tag == "TL") {
      if (!(kernel::R16(node + 0x28) & 1) && !active->hidden.count(node))
        continue;
      auto value = NativeUiText(node);
      if (value.empty()) {
        auto ids = defaultIds.find(active->name);
        if (ids != defaultIds.end())
          if (auto id = ids->second.find(name); id != ids->second.end())
            value = UiString(id->second);
      }
      if (value.empty())
        continue;
      for (auto &c : value)
        c = char(std::toupper((unsigned char)c));
      texts.push_back({name, value, float(int16_t(kernel::R16(node + 0x2A))),
                       float(int16_t(kernel::R16(node + 0x2C))),
                       float(kernel::R16(node + 0x2E)),
                       float(kernel::R16(node + 0x30)), rows[level]});
      texts.back().grid = gridNames[level];
      texts.back().column = columns[level];
      texts.back().kind = tag;
      if (active->tags.count(name) && active->tags.at(name) == "TB" &&
          rows[level] >= 0 && rows[level] < 16) {
        if (options.size() <= size_t(rows[level]))
          options.resize(rows[level] + 1);
        options[rows[level]] = value;
      }
      for (int side = 0; side < 2; ++side)
        for (int stat = 0; stat < 3; ++stat)
          if (name == std::string(stat == 0   ? "SCORE"
                                  : stat == 1 ? "HITS"
                                              : "ERRORS") +
                          char('1' + side))
            scores[side][stat] = value;
      if (name == "SPEED")
        wind = value;
    }
  }
  if (active->tags.count("MENUOPTIONS"))
    for (const auto &[row, name] : defaultRows[active->name]) {
      if (options.size() <= size_t(row))
        options.resize(row + 1);
      if (options[row].empty())
        options[row] = UiString(defaultIds[active->name][name]);
    }
  for (auto &value : options)
    for (auto &c : value)
      c = char(std::toupper((unsigned char)c));
  if (texts.empty())
    return;
  static auto assets = Assets() / "overhaul/startup";
  auto *d = ImGui::GetForegroundDrawList();
  auto display = ImGui::GetIO().DisplaySize;
  float s = std::min(display.x / 1920.f, display.y / 1080.f);
  auto screenPoint = [&](float x, float y) {
    return ImVec2((display.x - 1920 * s) / 2 + x * s,
                  (display.y - 1080 * s) / 2 + y * s);
  };
  auto p = [&](float x, float y) {
    if (showInfo) {
      x = x * .88f - 330;
      y *= .75f;
    }
    return screenPoint(x, y);
  };
  auto panel = [&](float x, float y, float w, float h, bool selected = false) {
    PausePanel(d, p(x, y), p(x + w, y + h), s, selected);
  };
  auto text = [&](float x, float y, float size, const std::string &value,
                  float width, ImU32 color = IM_COL32_WHITE) {
    UiText(d, UiFont::Heading, size * s, p(x, y), color, value.c_str(),
           width * s);
  };
  if (!options.empty() && active->tags.count("MENUOPTIONS")) {
    constexpr float x = 510, w = 900;
    float y = 365, rh = 44, h = rh * options.size() + 16;
    text(x, 138, 34, Title(active->name), 700);
    d->AddRectFilled(p(x, 178), p(x + 64, 181), IM_COL32(228, 20, 39, 255));
    panel(x, 190, w, 164);
    d->AddRectFilled(p(x + 630, 191), p(x + w - 2, 352),
                     IM_COL32(5, 7, 9, 255));
    for (int i = 0; i < 3; ++i)
      d->AddLine(p(x + 630 + i * 90, 191), p(x + 630 + i * 90, 352),
                 IM_COL32(100, 105, 110, 200), s);
    auto sides = SnapshotMatchupLoading();
    std::string inning, outs, balls, strikes;
    for (const auto &t : texts) {
      if (t.name == "INNING")
        inning = t.value;
      if (t.name == "NUMBER")
        outs = t.value;
      if (t.name == "BALLS")
        balls = t.value;
      if (t.name == "STRIKES")
        strikes = t.value;
    }
    text(x + 24, 199, 22,
         "INNING " + inning + "    " + balls + "-" + strikes + "    " + outs +
             " OUT",
         580, IM_COL32(178, 190, 204, 255));
    static auto logos = [&] {
      std::unordered_map<std::string, std::string> r;
      std::istringstream f(overhaul::Text(assets / "base-logos/teams.tsv"));
      std::string line;
      while (std::getline(f, line)) {
        auto t = line.find('\t');
        if (t != std::string::npos) {
          auto id = line.substr(t + 1);
          if (!id.empty() && id.back() == '\r')
            id.pop_back();
          r[line.substr(0, t)] = id;
        }
      }
      return r;
    }();
    text(x + 657, 194, 24, "R", 70);
    text(x + 747, 194, 24, "H", 70);
    text(x + 837, 194, 24, "E", 70);
    static auto colors = [&] {
      std::unordered_map<std::string, unsigned> r;
      std::istringstream f(overhaul::Text(assets / "team-colors.tsv"));
      std::string line;
      while (std::getline(f, line)) {
        auto t = line.find('\t');
        if (t != std::string::npos)
          r[line.substr(0, t)] =
              std::strtoul(line.c_str() + t + 1, nullptr, 16);
      }
      return r;
    }();
    for (int side = 0; side < 2; ++side) {
      float sy = 230 + side * 60;
      auto team = sides[side];
      unsigned rgb = side ? 0x682A0C : 0x0A2B5A;
      if (auto i = colors.find(team.name); i != colors.end())
        rgb = i->second;
      ImU32 teamColor =
          IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
      d->AddRectFilledMultiColor(p(x + 1, sy), p(x + 630, sy + 59), teamColor,
                                 IM_COL32(16, 21, 29, 255),
                                 IM_COL32(16, 21, 29, 255), teamColor);
      if (auto surface = gpu::g_ui_image(assets / "pause-v2/team-surface.png"))
        d->AddImage((ImTextureID)surface, p(x + 1, sy), p(x + 630, sy + 59),
                    {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 55));
      d->AddRectFilled(p(x + 1, sy), p(x + 4, sy + 59), teamColor);
      d->AddLine(p(x + 5, sy + 59), p(x + 630, sy + 59),
                 IM_COL32(255, 255, 255, 30), s);
      std::string label = team.nickname.empty() ? team.name : team.nickname;
      if (!team.rank.empty() && team.rank != "0")
        label = "#" + team.rank + " " + label;
      d->AddRectFilled(p(x + 18, sy + 3), p(x + 80, sy + 57),
                       IM_COL32(222, 228, 235, 255), 3 * s);
      if (auto i = logos.find(team.name); i != logos.end())
        if (auto logo =
                gpu::g_ui_image(assets / ("base-logos/" + i->second + ".png")))
          d->AddImage((ImTextureID)logo, p(x + 24, sy + 5), p(x + 74, sy + 55));
      text(x + 100, sy + 12, 30, label, 505);
      for (int stat = 0; stat < 3; ++stat)
        text(x + 657 + stat * 90, sy + 4, 40, scores[side][stat], 100);
    }
    panel(x, y, w, h);
    for (size_t i = 0; i < options.size(); ++i) {
      float ry = y + 8 + i * rh;
      if (int(i) == selected)
        panel(x + 12, ry + 2, w - 24, rh - 4, true);
      d->AddLine(p(x + 28, ry + rh), p(x + w - 28, ry + rh),
                 IM_COL32(100, 105, 110, 85), s);
      text(x + 82, ry + 9, 28, options[i], w - 125,
           i < enabled.size() && !enabled[i] ? IM_COL32(145, 148, 152, 255)
                                             : IM_COL32_WHITE);
      PauseIcon(d, p(x + 46, ry + rh / 2), 25 * s,
                active->name == "PAUSEMENU" ? int(i) : 3,
                i < enabled.size() && !enabled[i] ? IM_COL32(145, 148, 152, 255)
                                                  : IM_COL32_WHITE);
    }
    panel(x, y + h + 8, w, 45);
    text(x + 28, y + h + 14, 27, showInfo ? "Y  HIDE INFO" : "Y  SHOW INFO",
         400, IM_COL32(235, 195, 36, 255));
    text(x + 710, y + h + 14, 27, wind, 175);
  } else if (active->name == "INGAMECTRLRMENU") {
    panel(510, 145, 900, 60);
    text(760, 154, 38, "SELECT CONTROLLER", 640);
    auto sides = SnapshotMatchupLoading();
    std::unordered_map<std::string, std::string> logos;
    std::istringstream file(overhaul::Text(assets / "base-logos/teams.tsv"));
    std::string line;
    while (std::getline(file, line)) {
      auto tab = line.find('\t');
      if (tab != std::string::npos) {
        auto id = line.substr(tab + 1);
        if (!id.empty() && id.back() == '\r')
          id.pop_back();
        logos[line.substr(0, tab)] = id;
      }
    }
    for (int side = 0; side < 2; ++side) {
      float x = 510 + side * 510;
      panel(x, 240, 390, 470);
      d->AddRectFilled(p(x + 1, 241), p(x + 389, 245),
                       side ? IM_COL32(213, 91, 21, 255)
                            : IM_COL32(34, 113, 204, 255));
      d->AddRectFilled(p(x + 115, 258), p(x + 275, 418),
                       IM_COL32(225, 230, 235, 255), 4 * s);
      if (auto i = logos.find(sides[side].name); i != logos.end())
        if (auto logo =
                gpu::g_ui_image(assets / ("base-logos/" + i->second + ".png")))
          d->AddImage((ImTextureID)logo, p(x + 120, 263), p(x + 270, 413));
      text(x + 28, 430, 34, sides[side].name, 335);
      text(x + 28, 476, 27, sides[side].nickname, 335);
      if (!sides[side].rank.empty())
        text(x + 28, 520, 23, "NATIONAL RANK  #" + sides[side].rank, 335);
    }
    std::array<bool, 3> occupied{};
    for (const auto &c : controllers) {
      int lane = c.x < 240 ? 0 : c.x > 360 ? 2 : 1;
      occupied[lane] = true;
      float x = lane == 0 ? 635 : lane == 2 ? 1145 : 890;
      if (auto icon =
              gpu::g_ui_image(assets / "team-select-controller-cutout.png"))
        d->AddImage((ImTextureID)icon, p(x, 580), p(x + 140, 680));
      text(x + 48, 675, 24, "P" + c.name.substr(10), 100);
    }
    if (!occupied[0])
      text(650, 620, 32, "CPU", 200);
    if (!occupied[2])
      text(1160, 620, 32, "CPU", 200);
    text(665, 755, 27, "LEFT / RIGHT  ASSIGN CONTROLLER", 750);
  } else if (active->name == "INGAMEBOXSCORE") {
    constexpr float bx = 380, bw = 1160;
    panel(bx, 180, bw, 690);
    text(bx + 30, 198, 36, "BOX SCORE", 1000);
    std::unordered_map<int, std::string> names;
    std::unordered_map<int, std::array<std::string, 9>> cells;
    std::array<std::array<std::string, 13>, 3> innings;
    std::string club;
    for (const auto &t : texts) {
      if (t.grid == "FIRSTCELL" && t.name == "NAME" && t.row >= 0 &&
          t.row < 100) {
        if (t.value.find("----") == std::string::npos &&
            t.value.find("WWWWW") == std::string::npos &&
            t.value != "PLAYER NAME") {
          if (t.row == 1)
            club = t.value;
          else
            names[t.row] = t.value;
        }
      }
      if (t.grid == "CELL" && t.column >= 1 && t.column < 9 && t.row >= 0)
        cells[t.row][t.column] = t.value;
      if (t.grid == "SCOREBOARD" && t.column >= 0 && t.column < 13 &&
          t.row >= 0 && t.row < 3)
        innings[t.row][t.column] = t.value;
    }
    text(bx + 30, 251, 27, club, 600, IM_COL32(190, 202, 218, 255));
    const char *headers[] = {"PLAYER", "POS", "AB", "R",  "H",
                             "RBI",    "BB",  "SO", "AVG"};
    float cx[] = {bx + 30,  bx + 452, bx + 532, bx + 607, bx + 682,
                  bx + 757, bx + 832, bx + 907, bx + 982};
    for (int c = 0; c < 9; ++c)
      text(cx[c], 298, 24, headers[c], c ? 90 : 400,
           IM_COL32(174, 187, 204, 255));
    std::vector<int> keys;
    for (auto &[r, n] : names)
      keys.push_back(r);
    std::sort(keys.begin(), keys.end());
    int count = 0;
    for (int r : keys) {
      if (count >= 10)
        break;
      float yy = 334 + count++ * 36;
      if (r == selected)
        panel(bx + 14, yy - 2, bw - 28, 34, true);
      text(cx[0], yy + 2, 26, names[r], 395);
      for (int c = 1; c < 9; ++c)
        text(cx[c], yy + 2, 26, cells[r][c], 86);
      d->AddLine(p(bx + 28, yy + 33), p(bx + bw - 28, yy + 33),
                 IM_COL32(255, 255, 255, 25), s);
    }
    float yy = 735;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 13; ++c) {
        if (r == 0 && c == 0)
          continue;
        auto value = innings[r][c];
        if (c == 0 && r > 0) {
          auto teams = SnapshotMatchupLoading();
          value = teams[r - 1].name;
        }
        text(bx + 30 + (c ? 290 + (c - 1) * 65 : 0), yy + r * 34, r ? 26 : 22,
             value, c ? 60 : 280,
             r ? IM_COL32_WHITE : IM_COL32(174, 187, 204, 255));
      }
  } else if (menuGrid && focusedGrid != "MENUOPTIONS") {
    struct SettingRow {
      int id;
      float y = 0;
      std::string label, value;
    };
    std::unordered_map<int, SettingRow> fields;
    for (const auto &t : texts)
      if (t.grid == focusedGrid && t.row >= 0) {
        auto &row = fields[t.row];
        row.id = t.row;
        row.y = t.y;
        if (t.kind == "TL" || t.name == "VALUE" || t.name == "NUMBER")
          row.value = t.value;
        else if (t.kind == "TB" || t.kind == "TX")
          row.label = t.value;
      }
    std::vector<SettingRow> ordered;
    for (auto &[id, row] : fields)
      if (!row.label.empty())
        ordered.push_back(row);
    std::sort(ordered.begin(), ordered.end(),
              [](auto &a, auto &b) { return a.id < b.id; });
    bool scrollable = ordered.size() > 8;
    if (scrollable) {
      int first = std::max(0, selected - 7);
      ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                                   [&](const auto &row) {
                                     return row.id < first ||
                                            row.id >= first + 8;
                                   }),
                    ordered.end());
    }
    float height = 118 + std::max(size_t(1), ordered.size()) * 52;
    height = std::min(height, 680.f);
    panel(470, 220, 980, height);
    std::string heading = Title(active->name);
    if (active->name == "INGAMESETTINGS") {
      if (focusedGrid == "GAMERULES")
        heading = "GAMEPLAY RULES";
      if (focusedGrid == "GAMEPLAY")
        heading = "GAMEPLAY OPTIONS";
      if (focusedGrid == "CONTROLLER")
        heading = "CONTROLLER OPTIONS";
      if (focusedGrid == "AUDIO")
        heading = "AUDIO OPTIONS";
    }
    text(500, 238, 36, heading, 870);
    int index = 0;
    for (auto &row : ordered) {
      if (index >= 10)
        break;
      float yy = 302 + index++ * 52;
      if (row.id == selected)
        panel(484, yy, 952, 48, true);
      text(505, yy + 8, 29, row.label, 610);
      text(1140, yy + 8, 29, row.value, 230);
      text(1096, yy + 8, 28, "<", 30);
      text(1394, yy + 8, 28, ">", 28);
      d->AddLine(p(495, yy + 49), p(1425, yy + 49), IM_COL32(255, 255, 255, 30),
                 s);
    }
    if (scrollable)
      text(505, 220 + height - 27, 20, "UP / DOWN  SCROLL", 500,
           IM_COL32(174, 187, 204, 255));
  } else {
    panel(480, 132, 960, 790);
    panel(480, 132, 960, 60);
    text(515, 143, 36, Title(active->name), 860);
    auto q = [&](float x, float y) {
      return p(960 + (x - 320) * 1.4f, 550 + (y - 240) * 1.4f);
    };
    if (Valid(selectedCell)) {
      float x = int16_t(kernel::R16(selectedCell + 0x2A)),
            y = int16_t(kernel::R16(selectedCell + 0x2C));
      d->AddRectFilled(q(x, y),
                       q(x + kernel::R16(selectedCell + 0x2E),
                         y + kernel::R16(selectedCell + 0x30)),
                       IM_COL32(150, 0, 8, 225));
    }
    if (!controllers.empty())
      if (auto icon =
              gpu::g_ui_image(assets / "team-select-controller-cutout.png"))
        for (const auto &t : controllers)
          d->AddImage((ImTextureID)icon, q(t.x, t.y),
                      q(t.x + t.w, t.y + t.h * .75f));
    for (const auto &t : texts) {
      auto pos = q(t.x, t.y);
      float size = std::clamp(t.h * 1.4f, 18.f, 32.f) * s;
      UiText(d, UiFont::Heading, size, pos, IM_COL32(240, 240, 240, 255),
             t.value.c_str(), std::max(30.f, t.w * 1.4f) * s);
    }
  }
  // The pause information pane and footer are separate native scenes. Theme
  // their leaves too, without touching their timelines or gameplay controls.
  if (showInfo) {
    auto panel = [&](float x, float y, float w, float h) {
      auto a = screenPoint(x, y), b = screenPoint(x + w, y + h);
      PausePanel(d, a, b, s);
    };
    panel(920, 55, 960, 570);
    panel(50, 638, 850, 280);
    panel(920, 635, 960, 365);
  }
  for (auto &extra : scenes) {
    if (extra.name != "NAVBAR" && !(showInfo && extra.name == "IGPAUSEINFO"))
      continue;
    extra.types.insert(nativeTypes.begin(), nativeTypes.end());
    if (!Valid(extra.root) ||
        kernel::ReadCString(extra.root + 0x10, 16) != extra.name)
      continue;
    auto baseDepth = kernel::R32(extra.root + 0x24);
    if (baseDepth >= 127)
      continue;
    std::vector<uint32_t> extraNodes;
    for (auto node = kernel::R32(extra.root + 8), n = 0u;
         n < 4096 && Valid(node) && kernel::R32(node + 0x24) > baseDepth;
         node = kernel::R32(node + 8), ++n) {
      auto name = kernel::ReadCString(node + 0x10, 16);
      if (auto tag = extra.tags.find(name);
          tag != extra.tags.end() && tag->second != "?")
        extra.types.emplace(kernel::R32(node), tag->second);
      extraNodes.push_back(node);
    }
    std::array<bool, 128> visible{};
    visible[baseDepth] = true;
    for (auto node : extraNodes) {
      auto level = kernel::R32(node + 0x24);
      if (level >= 128)
        continue;
      auto type = extra.types.find(kernel::R32(node));
      std::string tag = type == extra.types.end() ? "" : type->second;
      bool leaf = tag == "TX" || tag == "TB" || tag == "TL" || tag == "SH" ||
                  tag == "SL" || tag == "RT";
      visible[level] = visible[level - 1] && bool(kernel::R16(node + 0x28) & 1);
      auto name = kernel::ReadCString(node + 0x10, 16);
      float x = int16_t(kernel::R16(node + 0x2A)),
            y = int16_t(kernel::R16(node + 0x2C)), w = kernel::R16(node + 0x2E),
            h = kernel::R16(node + 0x30);
      if (extra.name == "IGPAUSEINFO" && visible[level] && x >= 0 && x < 640 &&
          y >= 0 && y < 480) {
        if ((tag == "TX" || tag == "TL" || tag == "TB") &&
            ((kernel::R16(node + 0x28) & 1) || extra.hidden.count(node))) {
          auto value = NativeUiText(node);
          for (auto &c : value)
            c = char(std::toupper((unsigned char)c));
          UiText(d, UiFont::Heading, std::clamp(h * 2.25f, 20.f, 34.f) * s,
                 screenPoint(x * 3, y * 2.25f), IM_COL32_WHITE, value.c_str(),
                 w * 3 * s);
        }
        if (name == "BASEBALLDIAM") {
          auto a = screenPoint((x + w / 2) * 3, y * 2.25f),
               b = screenPoint((x + w) * 3, (y + h / 2) * 2.25f),
               c = screenPoint((x + w / 2) * 3, (y + h) * 2.25f),
               e = screenPoint(x * 3, (y + h / 2) * 2.25f);
          d->AddQuadFilled(a, b, c, e, IM_COL32(20, 25, 29, 255));
          d->AddQuad(a, b, c, e, IM_COL32(180, 184, 187, 255), 2 * s);
        }
        if (name == "BP" &&
            ((kernel::R16(node + 0x28) & 1) || extra.hidden.count(node)))
          d->AddCircleFilled(screenPoint((x + w / 2) * 3, (y + h / 2) * 2.25f),
                             8 * s, IM_COL32(245, 200, 30, 255));
        if (tag == "SL" && name == "LOGO") {
          auto id = kernel::ReadCString(node + 0x4E, 5);
          std::string number;
          for (char c : id)
            if (c >= '0' && c <= '9')
              number += c;
          if (!number.empty())
            if (auto logo = gpu::g_ui_image(
                    assets / ("base-logos/" + number + ".png"))) {
              float size = std::min(w * 3, h * 2.25f);
              d->AddImage((ImTextureID)logo, screenPoint(x * 3, y * 2.25f),
                          screenPoint(x * 3 + size, y * 2.25f + size));
            }
        }
      }
    }
  }
  if (showInfo)
    UiText(d, UiFont::Heading, 29 * s, screenPoint(50, 1020), IM_COL32_WHITE,
           "START  RESUME       A  SELECT       B  BACK", 900 * s);
  else
    text(520, 965, 29, "START  RESUME       A  SELECT       B  BACK", 900);
  if (showInfo && dev::Env("MVP06_TRACE_FE") &&
      infoCaptureAt.time_since_epoch().count() != 0 &&
      std::chrono::steady_clock::now() > infoCaptureAt) {
    infoCaptureAt = {};
    gpu::g_capture = true;
  }
  if (dev::Env("MVP06_TRACE_FE")) {
    static std::string last;
    auto key =
        active->name + std::to_string(selected) + std::to_string(showInfo);
    if (last != key) {
      last = key;
      LOGR("pause theme: %s selected=%d options=%zu texts=%zu",
           active->name.c_str(), selected, options.size(), texts.size());
      gpu::g_capture = true;
    }
  }
}
} // namespace game
