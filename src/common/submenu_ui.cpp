#include "core/dev_tools.h"
#include "submenu_ui.h"
#include "overhaul.h"
#include <sstream>
#include "app/app.h"
#include "core/log.h"
#include "core/input.h"
#include "gpu/backend.h"
#include "imgui.h"
#include "submenu_data.h"
#include "submenu_text.h"
#include "ui_style.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <unordered_map>
namespace game {
namespace {
struct Model {
  int category = 0, selected = 0;
  unsigned generation = 0;
  bool published = false;
  bool nested = false;
  bool settings = false;
  bool fromSettings = false;
  int library = 0;
  std::string message, album;
  std::vector<SettingsRow> settingsRows;
  std::string title;
  std::vector<uint32_t> labels;
};
std::mutex modelMutex;
Model model;
std::atomic<bool> nativePopupVisible{false};
std::atomic<bool> tutorialNoticeVisible{false};
std::string tutorialNoticeText;
std::string popupText;
std::vector<std::string> popupChoices;
int popupSelection = 0;
std::string MenuCopy(std::string value) {
 // Native strings use nonbreaking spaces between platform/title words.
 for(size_t at=0;(at=value.find("\xC2\xA0",at))!=std::string::npos;)value.replace(at,2," ");
 value=std::regex_replace(value,std::regex("\\s*\\([0-9]+ block\\(s\\)\\)",std::regex::icase),"");
 value=std::regex_replace(value,std::regex("Xbox hard disk",std::regex::icase),"your save library");
 value=std::regex_replace(value,std::regex("Please don.t turn off your Xbox console\\.",std::regex::icase),"Please keep the game open until saving finishes.");
 value=std::regex_replace(value,std::regex("^\\s*does not have a full 3-man pitching rotation\\.",std::regex::icase),"A team does not have a full 3-man pitching rotation.");
 value=std::regex_replace(value,std::regex("MVP[^A-Za-z0-9]*06\\s+NCAA[^A-Za-z0-9]*Baseball",std::regex::icase),"MVP27 College Baseball");
 if(value.starts_with("No MVP") && value.find("detected")!=std::string::npos){
  if(value.find("Options")!=std::string::npos)value="No saved options were found in your save library.";
  else if(value.find("Rosters")!=std::string::npos)value="No saved rosters were found in your save library.";
  else if(value.find("Dynast")!=std::string::npos)value="No saved dynasties were found in your save library.";
 }
 return value;
}
double Clock() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
float Fade(double t, double from, double duration) {
  return float(std::clamp((t - from) / duration, 0., 1.));
}
std::filesystem::path Assets() {
  return overhaul::Root();
}
std::vector<uint32_t> Defaults(int cat) {
  switch (cat) {
  case 0:
    return {std::begin(kSubmenuDefault0), std::end(kSubmenuDefault0)};
  case 1:
    return {std::begin(kSubmenuDefault1), std::end(kSubmenuDefault1)};
  case 2:
    return {std::begin(kSubmenuDefault2), std::end(kSubmenuDefault2)};
  case 3:
    return {std::begin(kSubmenuDefault3), std::end(kSubmenuDefault3)};
  case 4:
    return {std::begin(kSubmenuDefault4), std::end(kSubmenuDefault4)};
  case 5:
    return {std::begin(kSubmenuDefault5), std::end(kSubmenuDefault5)};
  case 6:
    return {std::begin(kSubmenuDefault6), std::end(kSubmenuDefault6)};
  }
  return {};
}
int Icon(uint32_t id) {
  if (id == 700 || id == 65534)
    return 0;
  if (id == 16612)
    return 1;
  if (id == 1554)
    return 2;
  if (id == 1532 || id == 1993 || id == 27110)
    return 3;
  if (id == 271)
    return 4;
  if (id == 704)
    return 5;
  if (id == 17080)
    return 6;
  return 7;
}
} // namespace
int SubmenuCategory(const char *scene) {
  if (!std::strcmp(scene, "SAVESYS") || !std::strcmp(scene, "JUKEBOX") ||
      !std::strcmp(scene, "JUKEBOX2"))
    return 5;
  if (!std::strcmp(scene, "SMARTOPTIONS") || !std::strcmp(scene, "GPOPTIONS"))
    return 5;
  if (!std::strcmp(scene, "FNEWLOAD") || !std::strcmp(scene, "TNEWLOAD") ||
      !std::strcmp(scene, "MINIGAMES"))
    return 1;
  if (!std::strcmp(scene, "ROSTER")) return 4;
  for (auto &key : kSubmenuKeys)
    if (!std::strcmp(key.scene, scene))
      return key.category;
  return -1;
}
uint32_t SubmenuLabelId(int cat, const char *name) {
  for (auto &key : kSubmenuKeys)
    if (cat == key.category && !std::strcmp(key.name, name))
      return key.id;
  return 0;
}
bool NestedSubmenuActive() {
  std::lock_guard lock(modelMutex);
  return model.nested;
}
bool SettingsMenuActive() {
  std::lock_guard lock(modelMutex);
  return model.settings;
}
int LibraryMenuKind() {
  std::lock_guard lock(modelMutex);
  return model.library;
}
void PublishLibrary(int selected, const std::vector<SettingsRow> &rows,
                    const std::string &message, const std::string &album,
                    const std::string &title) {
  PublishSettings(selected, rows);
  std::lock_guard lock(modelMutex);
  // Libraries can open from utility screens outside the main-menu model.
  // Their native title owns the operation; the old selected menu row does not.
  if(model.library==1 && !title.empty()){
    model.title=MenuCopy(title);
    for(auto& c:model.title)if(c>='a' && c<='z')c-=32;
    if(model.title.find("BALLPARK")!=std::string::npos){
      if(model.title.find("CREATE")!=std::string::npos)model.title="SAVE BALLPARK";
      else if(model.title.find("EDIT")!=std::string::npos)model.title="LOAD BALLPARK";
    }
  }
  model.message = message;
  if(model.library==1){model.message=MenuCopy(model.message);for(auto& row:model.settingsRows){row.label=MenuCopy(row.label);if(row.label.empty())row.label="Empty slot";}}
  model.album = album;
  std::string lower = album;
  for (auto &c : lower)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  if (lower.rfind("from the album:", 0) == 0) {
    model.album.erase(0, 15);
    while (!model.album.empty() && model.album.front() == ' ')
      model.album.erase(0, 1);
  }
}
void PublishSettings(int selected, const std::vector<SettingsRow> &rows) {
  std::lock_guard lock(modelMutex);
  if (dev::Env("MVP06_TRACE_FE") && model.settingsRows.size() == rows.size())
    for (size_t i = 0; i < rows.size(); ++i)
      if (model.settingsRows[i].enabled != rows[i].enabled)
        LOGR("settings availability: %s enabled=%d", rows[i].label.c_str(), int(rows[i].enabled));
  if (dev::Env("MVP06_TRACE_FE") && selected < int(rows.size())) {
    static std::string previous;
    auto key =
        std::to_string(selected) + rows[selected].label + rows[selected].value;
    if (key != previous) {
      previous = key;
      LOGR("settings selection: row=%d %s = %s rows=%u", selected,
           rows[selected].label.c_str(), rows[selected].value.c_str(),
           unsigned(rows.size()));
    }
  }
  model.selected = selected;
  model.settingsRows = rows;
}
void OpenSubmenu(int cat, const char *scene) {
  std::lock_guard lock(modelMutex);
  bool nested = !std::strcmp(scene, "FNEWLOAD") ||
                !std::strcmp(scene, "TNEWLOAD") ||
                !std::strcmp(scene, "MINIGAMES") || !std::strcmp(scene, "ROSTER");
  if (nested && !model.nested && model.selected < int(model.labels.size()))
    model.title = SubmenuText(model.labels[model.selected]);
  if (!std::strcmp(scene, "MINIGAMES"))
    model.title = "MINI GAMES";
  if (!std::strcmp(scene, "ROSTER")) model.title = "ROSTERS";
  bool settings = !std::strcmp(scene, "SMARTOPTIONS");
  int library =
      !std::strcmp(scene, "SAVESYS")                                        ? 1
      : (!std::strcmp(scene, "JUKEBOX") || !std::strcmp(scene, "JUKEBOX2")) ? 2
                                                                            : 0;
  settings = settings || library != 0;
  bool tuning = !std::strcmp(scene, "GPOPTIONS");
  model.fromSettings = model.settings;
  // Returning to the Scenario form from stadium selection reopens
  // SMARTOPTIONS without republishing Game Modes. The labels are then only
  // the unpublished Options defaults, whose first row is AUDIO OPTIONS.
  const bool keepScenario = !model.published && model.title == "SCENARIO EDITOR";
  if (settings && !keepScenario && model.selected < int(model.labels.size()))
    model.title = SubmenuText(model.labels[model.selected]);
  if (tuning)
    model.title = "GAMEPLAY TUNING";
  model.settings = settings;
  model.library = library;
  model.message.clear();
  model.album.clear();
  if (library == 2)
    model.title = "JUKEBOX";
  model.settingsRows.clear();
  nested = nested || tuning;
  model.nested = nested;
  model.category = cat;
  model.selected = 0;
  model.labels = nested ? (model.title.find("TOURNAMENT") != std::string::npos
                               ? std::vector<uint32_t>{28638, 28639}
                               : std::vector<uint32_t>{28640, 28641})
                        : Defaults(cat);
  if (!std::strcmp(scene, "MINIGAMES"))
    model.labels = {272, 273, 281, 27101, 27102, 27103};
  if (tuning)
    model.labels = {3202, 3203, 3204, 3205, 611};
  if (!std::strcmp(scene, "ROSTER")) model.labels = {9092, 3601, 13204};
  model.generation++;
  model.published = false;
  nativePopupVisible = false;
  LOGR("submenu opened: category=%d", cat);
}
void PublishSubmenu(int cat, int selected,
                    const std::vector<uint32_t> &labels) {
  std::lock_guard lock(modelMutex);
  if (cat != model.category)
    return;
  if (dev::Env("MVP06_TRACE_FE") &&
      (!model.published || model.selected != selected ||
       model.labels != labels))
    LOGR("submenu native state: category=%d row=%d rows=%u", cat, selected,
         unsigned(labels.size()));
  model.published = true;
  if (dev::Env("MVP06_TRACE_FE") && model.labels != labels) {
    for (auto id : labels)
      LOGR("submenu label: %u %s", id, SubmenuText(id));
  }
  if (!labels.empty())
    model.labels = labels;
  model.selected = std::clamp(selected, 0, int(model.labels.size()) - 1);
}
int SelectedSubmenuRow() {
  std::lock_guard lock(modelMutex);
  return model.selected;
}
int RulesSettingsRow() {
  std::lock_guard lock(modelMutex);
  for (int i = 0; i < int(model.settingsRows.size()); ++i)
    if (model.settingsRows[i].label == "RULES") return i;
  return -1;
}
int SubmenuRowCount() {
  std::lock_guard lock(modelMutex);
  if (model.settings)
    return int(model.settingsRows.size());
  return int(model.labels.size());
}
bool SubmenuPopupVisible() { return nativePopupVisible.load() || tutorialNoticeVisible.load(); }
std::string UiString(uint32_t id) {
  static const auto strings = [] {
    std::unordered_map<uint32_t, std::string> result;
    std::istringstream file(overhaul::Text(Assets() / "ui-strings.bin"));
    uint32_t key, len;
    while (file.read((char *)&key, 4) && file.read((char *)&len, 4) &&
           len < 65536) {
      std::string value(len, '\0');
      if (!file.read(value.data(), len))
        break;
      result.emplace(key, std::move(value));
    }
    return result;
  }();
  auto it = strings.find(id);
  return it == strings.end() ? std::string{} : it->second;
}
void PublishPopup(const std::string &text,
                  const std::vector<std::string> &choices, int selected) {
  std::lock_guard lock(modelMutex);
  popupText = text;
  popupChoices = choices;
  popupSelection = selected;
}
bool LegacyOnlineNotice(){
 std::lock_guard lock(modelMutex);if(!nativePopupVisible.load())return false;
 std::string text=MenuCopy(popupText);std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
 return text.find("xbox live")!=std::string::npos && text.find("sign")!=std::string::npos;
}
bool InspectPopup(std::string &text, std::vector<std::string> &choices, int &selected) {
  std::lock_guard lock(modelMutex);
  if (!nativePopupVisible.load()) return false;
  text=popupText; choices=popupChoices; selected=popupSelection;
  return true;
}
bool HandleTutorialNoticePad(int port,input::Pad& pad){
 if(port!=0 || !overhaul::Active())return false;
 static bool armed=false,release=false;
 const bool pressed=pad.analog[0] || pad.analog[1];
 auto consume=[&]{pad.buttons=0;pad.lx=pad.ly=pad.rx=pad.ry=0;for(auto& value:pad.analog)value=0;};
 if(release){if(!pressed)release=false;consume();return true;}
 if(tutorialNoticeVisible.load()){
  if(!pressed)armed=true;
  else if(armed){tutorialNoticeVisible=false;armed=false;release=true;}
  consume();return true;
 }
 if(!nativePopupVisible.load() && pad.analog[0]){
  const auto missing=MissingTutorialVideo();
  if(!missing.empty()){
   {std::lock_guard lock(modelMutex);tutorialNoticeText="This tutorial video is not installed in the supplied game data. Restore the original tutorial video to enable playback.\n\nMissing file: "+missing;}
   tutorialNoticeVisible=true;armed=false;consume();return true;
  }
 }
 return false;
}
void SetSubmenuPopupVisible(bool visible) {
  bool old = nativePopupVisible.exchange(visible);
  if (old != visible && dev::Env("MVP06_TRACE_FE")) {
    LOGR("submenu native dialog: %s", visible ? "visible" : "hidden");
    // Do not schedule a readback from a transient modal visibility change.
  }
}
void DrawNotification() {
  static float opacity = 0;
  const bool tutorialNotice=tutorialNoticeVisible.load();
  bool visible = nativePopupVisible.load() || tutorialNotice;
  float step = ImGui::GetIO().DeltaTime / .18f;
  opacity = std::clamp(opacity + (visible ? step : -step), 0.f, 1.f);
  if (opacity <= 0)
    return;
  std::string body;
  std::vector<std::string> choices;
  int selected;
  {
    std::lock_guard lock(modelMutex);
    body = popupText;
    choices = popupChoices;
    selected = popupSelection;
    if(tutorialNotice){body=tutorialNoticeText;choices={"Back"};selected=0;}
  }
  if (body.empty())
    return; // wait for the native dialog to finish populating
  const bool legacyOnline=LegacyOnlineNotice();
  if(legacyOnline){body="Legacy Xbox Live services are not supported in this build. Return to Game Modes to play offline.";choices={"Back"};selected=0;}
  else body=MenuCopy(body);
  auto *d = ImGui::GetBackgroundDrawList();
  auto size = ImGui::GetIO().DisplaySize;
  float s = std::min(size.x / 2560.f, size.y / 1440.f),
        ox = (size.x - 2560 * s) * .5f, oy = (size.y - 1440 * s) * .5f;
  auto p = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
  auto color = [&](int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, int(a * opacity));
  };
  const float textSize = 38.f, wrap = 1160.f;
  auto font = Font(UiFont::Body);
  float bodyHeight = font->CalcTextSizeA(textSize, 1e6f, wrap, body.c_str()).y;
  // Derive all bounds from the same row bottom so the final border stays
  // visible.
  float optionTop = 180 + bodyHeight + 55;
  float optionBottom = optionTop + choices.size() * 86.f;
  float h = optionBottom + 110;
  float x = 570, w = 1420, y = (1440 - h) * .5f + 16 * (1 - opacity);
  d->AddRectFilled({0, 0}, size, color(0, 0, 0, 165));
  d->AddRectFilled(p(x + 10, y + 14), p(x + w + 10, y + h + 14),
                   color(0, 0, 0, 130));
  d->AddRectFilled(p(x, y), p(x + w, y + h), color(9, 10, 12, 248));
  d->AddRectFilledMultiColor(p(x, y), p(x + w, y + 128), color(40, 40, 42),
                             color(12, 12, 14), color(12, 12, 14),
                             color(25, 25, 28));
  auto root = Assets();
  auto strip = gpu::g_ui_image
                   ? gpu::g_ui_image(overhaul::UiAsset(root / "menu-selection-imagegen.png"))
                   : 0;
  if (strip)
    d->AddImageQuad((ImTextureID)strip, p(x + 40, y + 2), p(x + 150, y + 2),
                    p(x + 105, y + 126), p(x + 2, y + 126), {.02f, .31f},
                    {.83f, .31f}, {.83f, .64f}, {.02f, .64f},
                    color(255, 255, 255));
  std::string title = "NOTIFICATION", lower = body;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  if(tutorialNotice)title="VIDEO UNAVAILABLE";
  else if(legacyOnline)title="ONLINE PLAY UNAVAILABLE";
  else if(lower.find("save complete")!=std::string::npos)title="SAVE COMPLETE";
  else if(lower.find("load complete")!=std::string::npos)title="LOAD COMPLETE";
  else if(lower.find("no saved")!=std::string::npos && lower.find("were found")!=std::string::npos)title="SAVE LIBRARY";
  else if(lower.find("want to quit")!=std::string::npos && lower.find("current game")!=std::string::npos)title="QUIT GAME";
  else if(lower.find("discard")!=std::string::npos || lower.find("without saving")!=std::string::npos ||
          ((lower.find("exit")!=std::string::npos || lower.find("quit")!=std::string::npos) &&
           (lower.find("lose")!=std::string::npos || lower.find("lost")!=std::string::npos ||
            lower.find("unsaved")!=std::string::npos)))title="DISCARD CHANGES";
  else if (lower.find("roster") != std::string::npos)
    title = "ROSTER CHANGES";
  else if (lower.find("delete") != std::string::npos)
    title = "DELETE CONFIRMATION";
  else if (lower.find("save") != std::string::npos)
    title = "SAVE CHANGES";
  else if (choices.size() > 1)
    title = "CONFIRMATION";
  UiText(d, UiFont::Heading, 64 * s, p(x + 190, y + 25), color(245, 245, 245),
         title.c_str(), (w - 260) * s);
  d->AddLine(p(x + 155, y + 127), p(x + w - 130, y + 127), color(190, 20, 32),
             2 * s);
  d->PushClipRect(p(x + 80, y + 150), p(x + w - 80, y + optionBottom + 3),
                  true);
  d->AddText(font, textSize * s, p(x + 130, y + 180), color(240, 240, 240),
             body.c_str(), nullptr, wrap * s);
  float top = y + 180 + bodyHeight + 55;
  for (int i = 0; i < int(choices.size()); i++) {
    float yy = top + i * 86;
    if (i == selected && strip)
      d->AddImage((ImTextureID)strip, p(x + 105, yy), p(x + w - 105, yy + 82),
                  {.02f, .31f}, {.83f, .64f}, color(255, 255, 255));
    else
      d->AddRectFilled(p(x + 105, yy), p(x + w - 105, yy + 82),
                       color(18, 19, 22));
    d->AddRect(p(x + 105, yy), p(x + w - 105, yy + 82),
               color(200, 200, 200, i == selected ? 245 : 100), 0, 0, s);
    if (i == selected) {
      ImVec2 arrow[] = {p(x + 136, yy + 24), p(x + 153, yy + 41),
                        p(x + 136, yy + 58)};
      d->AddPolyline(arrow, 3, color(255, 255, 255), false, 5 * s);
    }
    UiText(d, UiFont::Heading, 47 * s, p(x + 205, yy + 13),
           color(245, 245, 245), choices[i].c_str(), (w - 380) * s);
  }
  d->PopClipRect();
  d->AddRect(p(x, y), p(x + w, y + h), color(155, 158, 160), 0, 0, 2 * s);
  if (!choices.empty()) {
    auto icon = [&](const char *name, float xx) {
      auto tex = gpu::g_ui_image(overhaul::UiAsset(root / name));
      if (tex)
        d->AddImage((ImTextureID)tex, p(xx, y + h - 65), p(xx + 42, y + h - 23),
                    {0, 0}, {1, 1}, color(255, 255, 255));
    };
    icon("controller-reference-a.png", x + 120);
    icon("controller-reference-b.png", x + 360);
    UiText(d, UiFont::Prompt, 34 * s, p(x + 180, y + h - 64),
           color(240, 240, 240), "SELECT");
    UiText(d, UiFont::Prompt, 34 * s, p(x + 420, y + h - 64),
           color(240, 240, 240), "BACK");
  }
  if (dev::Env("MVP06_TRACE_FE") && opacity == 1) {
    static std::string captured;
    auto key = body + std::to_string(selected);
    if (captured != key) {
      captured = key;
      gpu::g_capture = true;
      LOGR("notification rendered: selection=%d choices=%u text=%s", selected,
           unsigned(choices.size()), body.c_str());
    }
  }
}
void DrawLibrary(double elapsed) {
  Model state;
  {
    std::lock_guard lock(modelMutex);
    state = model;
  }
  auto root = Assets();
  if (root.empty() || !gpu::g_ui_image)
    return;
  auto *d = ImGui::GetBackgroundDrawList();
  auto size = ImGui::GetIO().DisplaySize;
  float s = std::min(size.x / 2560.f, size.y / 1440.f),
        ox = (size.x - 2560 * s) / 2, oy = (size.y - 1440 * s) / 2;
  auto p = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
  float a = Fade(elapsed, 0, .3);
  a = a * a * (3 - 2 * a);
  float shift = 90 * (1 - a);
  bool files = state.library == 1;
  if(files){
    const auto tint=[&](int alpha){return IM_COL32(245,245,245,int(alpha*a));};
    UiMenuBackdrop(d,size,UiBackdrop::Graphite);
    UiBrandLogo(d,p(75,35),p(455,170));
    const bool ballparkSave=state.title=="CREATE-A-BALLPARK" || state.title=="CREATE BALLPARK";
    UiText(d,UiFont::Heading,70*s,p(1260,57),tint(255),ballparkSave?"SAVE BALLPARK":state.title.c_str(),1160*s);
    d->AddRectFilled(p(1260,167),p(2445,175),IM_COL32(237,12,40,255));
    const bool saving=ballparkSave || state.title.starts_with("SAVE ") || state.title=="SAVE";
    const bool deleting=state.title.find("DELETE")!=std::string::npos;
    UiBroadcastPanel(d,p(90,250),p(1660,1210),s);
    UiText(d,UiFont::Heading,45*s,p(135,278),tint(255),"PC SAVE LIBRARY",1450*s);
    UiText(d,UiFont::Prompt,30*s,p(135,342),tint(180),"NAME",1250*s);
    const int count=int(state.settingsRows.size()),first=std::max(0,state.selected-7);
    for(int j=0;j<std::min(8,count-first);++j){
      const int index=first+j;const auto& row=state.settingsRows[index];const float y=400+j*94;
      const bool empty=row.label.empty()||row.label=="Empty slot";
      UiBroadcastPanel(d,p(120,y),p(1630,y+82),s,index==state.selected);
      const auto number=std::to_string(index+1);
      UiText(d,UiFont::Prompt,32*s,p(150,y+20),tint(160),number.c_str(),85*s);
      UiText(d,UiFont::Heading,42*s,p(255,y+15),tint(empty?155:255),empty?(saving?"CREATE NEW SAVE":"EMPTY SLOT"):row.label.c_str(),1120*s);
    }
    UiBroadcastPanel(d,p(1710,250),p(2460,1210),s);
    UiText(d,UiFont::Heading,43*s,p(1760,294),tint(255),saving?"SAVE YOUR PROGRESS":deleting?"MANAGE SAVES":"LOAD YOUR PROGRESS",650*s);
    auto help=[&](const char* value,float y){d->AddText(Font(UiFont::Body),34*s,p(1760,y),tint(205),value,nullptr,640*s);};
    help(saving?"Choose an empty slot to create a named save, or an existing save to replace it.":deleting?"Select a saved file to review the delete confirmation.":"Select a named save to load it. Empty slots contain no saved data.",405);
    d->AddLine(p(1760,665),p(2410,665),tint(100),s);
    help("Stored locally in your active PC profile.",710);
    help("Your current game remains unchanged until you confirm an action.",845);
    d->AddRectFilled(p(0,1295),p(2560,1440),IM_COL32(5,11,15,255));
    UiControllerButton(d,p(140,1360),30*s,s,"B",IM_COL32_WHITE);
    UiText(d,UiFont::Prompt,38*s,p(190,1338),tint(255),"BACK",350*s);
    UiControllerButton(d,p(1750,1360),30*s,s,"A",IM_COL32_WHITE);
    UiText(d,UiFont::Prompt,38*s,p(1800,1338),tint(255),"SELECT SAVE",620*s);
    return;
  }
  auto image = [&](const char *file, float x, float y, float w, float h) {
    auto tex = gpu::g_ui_image(overhaul::UiAsset(root / file));
    if (tex)
      d->AddImage((ImTextureID)tex, p(x, y), p(x + w, y + h));
  };
  UiMenuBackdrop(d,size,files ? UiBackdrop::Steel : UiBackdrop::Charcoal);
  d->AddRectFilledMultiColor(p(0, 0), p(2250, 1440), IM_COL32(0, 0, 0, 100), 0,
                             0, IM_COL32(0, 0, 0, 150));
  image("menu-trim-imagegen.png", 0, 0, 420, 1440);
  UiBrandLogo(d,p(165,90),p(755,285));
  auto tint = [&](int v) { return IM_COL32(245, 245, 245, int(v * a)); };
  auto strip = gpu::g_ui_image(overhaul::UiAsset(root / "menu-selection-imagegen.png"));
  if (strip)
    d->AddImage((ImTextureID)strip, p(790 + shift, 153), p(1920 + shift, 272),
                {.02f, .31f}, {.83f, .64f}, tint(220));
  d->AddRectFilledMultiColor(p(1400 + shift, 153), p(1920 + shift, 272), 0,
                             IM_COL32(0, 0, 0, 235), IM_COL32(0, 0, 0, 235), 0);
  UiText(d, UiFont::Settings, 76 * s, p(845 + shift, 162), tint(255),
         state.title.c_str(), 1050 * s);
  float left = 385 + shift, right = 2110 + shift, top = 350;
  d->AddRectFilled(p(left, top), p(right, files ? 1190 : 1105),
                   IM_COL32(5, 6, 8, int(235 * a)));
  if (strip)
    d->AddImage((ImTextureID)strip, p(left + 2, top + 2),
                p(right - 2, top + 92), {.02f, .31f}, {.83f, .64f}, tint(245));
  std::string heading = files ? state.message : "EA SPORTS™ TRAX";
  if (files && (heading.empty() ||
                heading.find("English sentence") != std::string::npos))
    heading = "PLEASE SELECT AN MVP 27 COLLEGE BASEBALL SAVE.";
  if (files && heading.find("MVP") != std::string::npos &&
      heading.find("06") != std::string::npos)
    heading = "PLEASE SELECT AN MVP 27 COLLEGE BASEBALL SAVE.";
  UiText(d, UiFont::Heading, (files ? 44 : 56) * s, p(left + 48, top + 21),
         tint(255), heading.c_str(), (right - left - 95) * s);
  if (!files) {
    UiText(d, UiFont::Prompt, 37 * s, p(left + 48, top + 112), tint(175), "#");
    UiText(d, UiFont::Prompt, 37 * s, p(left + 185, top + 112), tint(175),
           "SONG");
    UiText(d, UiFont::Prompt, 37 * s, p(right - 220, top + 112), tint(175),
           "PLAY");
  }
  int count = int(state.settingsRows.size()), page = files ? 8 : 10;
  int first = std::max(0, state.selected - page + 1);
  float rowTop = files ? 465.f : 515.f, height = files ? 88.f : 58.f;
  static unsigned generation = ~0u;
  static float cursor = 0;
  if (generation != state.generation) {
    generation = state.generation;
    cursor = float(state.selected);
  }
  cursor +=
      (state.selected - cursor) * std::min(1.f, ImGui::GetIO().DeltaTime * 16);
  for (int j = 0; j < std::min(page, count - first); ++j) {
    auto &row = state.settingsRows[first + j];
    float y = rowTop + j * height;
    float weight = std::max(0.f, 1 - std::abs(float(first + j) - cursor));
    d->AddRectFilled(p(left + 30, y), p(right - 30, y + height - 7),
                     IM_COL32(12, 13, 16, int(225 * a)));
    if (strip && weight > 0)
      d->AddImage((ImTextureID)strip, p(left + 30, y),
                  p(right - 30, y + height - 7), {.02f, .31f}, {.83f, .64f},
                  tint(int(255 * weight)));
    d->AddRect(p(left + 30, y), p(right - 30, y + height - 7),
               tint(first + j == state.selected ? 245 : 95), 0, 0, s);
    d->AddRectFilled(p(left + 30, y), p(left + 137, y + height - 7),
                     IM_COL32(110, 110, 110, int(45 * a)));
    auto number = std::to_string(first + j + 1);
    UiText(d, UiFont::Heading, (files ? 49 : 42) * s,
           p(left + 72, y + (files ? 15 : 4)), tint(235), number.c_str());
    UiText(d, UiFont::Heading, (files ? 47 : 40) * s,
           p(left + 185, y + (files ? 16 : 5)),
           tint(first + j == state.selected ? 255 : 205), row.label.c_str(),
           (right - left - (files ? 250 : 500)) * s);
    if (!files) {
      UiText(d, UiFont::Heading, 41 * s, p(right - 210, y + 4), tint(245),
             row.value.c_str(), 100 * s);
      for (int side = 0; side < 2; ++side) {
        float x = side ? right - 62 : right - 275, dir = side ? 1.f : -1.f;
        ImVec2 v[] = {p(x - dir * 7, y + 15), p(x + dir * 3, y + 25),
                      p(x - dir * 7, y + 35)};
        d->AddPolyline(v, 3, tint(235), false, 4 * s);
      }
    }
  }
  d->AddRect(p(left, top), p(right, files ? 1190 : 1105), tint(155), 0, 0,
             2 * s);
  if (!files) {
    d->AddRectFilled(p(left, 1120), p(right, 1250),
                     IM_COL32(8, 9, 11, int(235 * a)));
    d->AddRect(p(left, 1120), p(right, 1250), tint(135), 0, 0, s);
    UiText(d, UiFont::Prompt, 33 * s, p(left + 45, 1138), tint(180),
           "FROM THE ALBUM:");
    UiText(d, UiFont::Heading, 43 * s, p(left + 45, 1180), tint(245),
           state.album.c_str(), (right - left - 450) * s);
    UiText(d, UiFont::Prompt, 35 * s, p(right - 340, 1174), tint(220),
           state.message.c_str(), 290 * s);
  }
  d->AddRectFilled(p(0, 1295), p(2560, 1440), IM_COL32(0, 0, 0, 220));
  d->AddLine(p(0, 1295), p(2560, 1295), tint(130), s);
  image("controller-reference-a.png", 100, 1320, 58, 58);
  image("controller-reference-b.png", 425, 1320, 58, 58);
  UiText(d, UiFont::Prompt, 38 * s, p(180, 1325), tint(255),
         files ? "ACCEPT" : "SELECT");
  UiText(d, UiFont::Prompt, 38 * s, p(505, 1325), tint(255), "BACK");
  if (dev::Env("MVP06_TRACE_FE")) {
    static std::string captured;
    auto key =
        std::to_string(state.generation) + ":" + std::to_string(state.selected);
    if (captured != key && elapsed > 2 && count &&
        std::abs(cursor - state.selected) < .02f) {
      captured = key;
      gpu::g_capture = true;
    }
  }
}
void DrawSettings(double elapsed) {
  if (LibraryMenuKind()) {
    DrawLibrary(elapsed);
    return;
  }
  Model state;
  {
    std::lock_guard lock(modelMutex);
    state = model;
  }
  auto root = Assets();
  if (root.empty() || !gpu::g_ui_image)
    return;
  auto *d = ImGui::GetBackgroundDrawList();
  auto size = ImGui::GetIO().DisplaySize;
  float s = std::min(size.x / 2560.f, size.y / 1440.f),
        ox = (size.x - 2560 * s) / 2, oy = (size.y - 1440 * s) / 2;
  auto p = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
  float a = Fade(elapsed, 0, .3);
  a = a * a * (3 - 2 * a);
  float slide = 100 * (1 - a);
  auto image = [&](const char *file, float x, float y, float w, float h) {
    auto t = gpu::g_ui_image(overhaul::UiAsset(root / file));
    if (t)
      d->AddImage((ImTextureID)t, p(x, y), p(x + w, y + h));
  };
  UiMenuBackdrop(d,size,UiBackdrop::Graphite);
  image("menu-trim-imagegen.png", 0, 0, 420, 1440);
  d->AddRectFilledMultiColor(p(0, 0), p(1780, 1440), IM_COL32(0, 0, 0, 95), 0,
                             0, IM_COL32(0, 0, 0, 160));
  UiBrandLogo(d,p(145+20*a,120-20*a),p(975-220*a,395-100*a));
  auto strip = gpu::g_ui_image(overhaul::UiAsset(root / "menu-selection-imagegen.png"));
  auto tint = [&](int opacity) {
    return IM_COL32(255, 255, 255, int(opacity * a));
  };
  if (strip)
    d->AddImage((ImTextureID)strip, p(790 + slide, 160), p(1780 + slide, 280),
                {.02f, .31f}, {.83f, .64f}, tint(235));
  d->AddRectFilledMultiColor(p(1400 + slide, 160), p(1780 + slide, 280), 0,
                             IM_COL32(3, 4, 5, 235), IM_COL32(3, 4, 5, 235), 0);
  UiText(d, UiFont::Settings, 76 * s, p(845 + slide, 169), tint(255),
         state.title.c_str(), 900 * s);
  constexpr int page = 8;
  int total = int(state.settingsRows.size()),
      first = std::max(0, state.selected - page + 1);
  static unsigned generation = ~0u;
  static float cursor = 0;
  if (generation != state.generation) {
    generation = state.generation;
    cursor = float(state.selected);
  }
  cursor +=
      (state.selected - cursor) * std::min(1.f, ImGui::GetIO().DeltaTime * 16);
  d->PushClipRect(p(110, 350), p(1840, 1190), true);
  for (int j = 0; j < std::min(page, total - first); ++j) {
    auto &r = state.settingsRows[first + j];
    float y = 365 + j * 100;
    ImVec2 q[] = {p(170 + slide, y), p(1770 + slide, y),
                  p(1747 + slide, y + 94), p(147 + slide, y + 94)};
    d->AddConvexPolyFilled(q, 4, IM_COL32(4, 5, 7, int(205 * a)));
    ImVec2 accent[] = {p(170 + slide, y), p(193 + slide, y),
                       p(170 + slide, y + 94), p(147 + slide, y + 94)};
    d->AddConvexPolyFilled(accent, 4, IM_COL32(175, 8, 18, int(205 * a)));
    float weight = std::max(0.f, 1 - std::abs(float(first + j) - cursor));
    if (strip && weight > 0)
      d->AddImageQuad((ImTextureID)strip, q[0], q[1], q[2], q[3], {.02f, .31f},
                      {.83f, .31f}, {.83f, .64f}, {.02f, .64f},
                      tint(int(255 * weight)));
    d->AddPolyline(q, 4, tint(first + j == state.selected ? 245 : 100), true,
                   (first + j == state.selected ? 2 : 1) * s);
    UiText(d, r.fraction >= 0 ? UiFont::Settings : UiFont::Heading, 51 * s,
           p(235 + slide, y + 18),
           tint(!r.enabled ? 90 : first + j == state.selected ? 255 : 200), r.label.c_str(),
           780 * s);
    d->AddRectFilled(p(1160 + slide, y + 12), p(1660 + slide, y + 82),
                     IM_COL32(0, 0, 0, int(150 * a)));
    if (r.fraction >= 0) {
      for (int k = 0; k < 6; ++k) {
        float x = 1170 + slide + k * 61;
        d->AddRectFilled(p(x, y + 32), p(x + 55, y + 63),
                         tint((k + .5f) / 6 <= r.fraction ? 245 : 75));
      }
      UiText(d, UiFont::Prompt, 48 * s, p(1575 + slide, y + 19), tint(255),
             r.value.c_str(), 70 * s);
    } else {
      auto font = Font(UiFont::Heading);
      float w = font->CalcTextSizeA(48 * s, 1e6f, 0, r.value.c_str()).x;
      UiText(d, UiFont::Heading, 48 * s,
             p(1410 + slide - std::min(w / s, 420.f) / 2, y + 19), tint(r.enabled ? 255 : 90),
             r.value.c_str(), 420 * s);
    }
    for (int side = 0; r.enabled && side < 2; ++side) {
      float x = (side ? 1710 : 1120) + slide, dir = side ? 1.f : -1.f;
      ImVec2 v[] = {p(x - dir * 9, y + 31), p(x + dir * 3, y + 47),
                    p(x - dir * 9, y + 63)};
      d->AddPolyline(v, 3, tint(255), false, 5 * s);
    }
  }
  d->PopClipRect();
  if (total > page) {
    std::string index =
        std::to_string(state.selected + 1) + " / " + std::to_string(total);
    UiText(d, UiFont::Prompt, 31 * s, p(1600, 1220), tint(210), index.c_str());
  }
  bool lockedRules = false, lockedSeries = false;
  for (const auto &row : state.settingsRows) {
    if (!row.enabled && (row.label == "BAT TYPE" || row.label == "BASE PATH RULES" ||
                         row.label == "STRIKE ZONE" || row.label == "10 RUN RULE"))
      lockedRules = true;
    if (!row.enabled && row.label == "SERIES LENGTH") lockedSeries = true;
  }
  if (lockedRules || lockedSeries)
    UiText(d, UiFont::Prompt, 30 * s, p(165, 1190), tint(210),
           lockedRules && lockedSeries ? "USE CUSTOM RULES TO EDIT DIMMED RULES. SINGLE KO ENABLES SERIES LENGTH."
           : lockedRules ? "CHANGE RULES TO CUSTOM TO EDIT THE DIMMED RULE SETTINGS."
                         : "SELECT A SINGLE KO TOURNAMENT FORMAT TO EDIT SERIES LENGTH.", 2000 * s);
  d->AddRectFilled(p(0, 1295), p(2560, 1440), IM_COL32(0, 0, 0, 210));
  d->AddLine(p(0, 1295), p(2560, 1295), tint(130), s);
  image("controller-reference-a.png", 100, 1320, 58, 58);
  image("controller-reference-b.png", 425, 1320, 58, 58);
  UiText(d, UiFont::Prompt, 38 * s, p(180, 1325), tint(255), "SELECT");
  UiText(d, UiFont::Prompt, 38 * s, p(505, 1325), tint(255), "BACK");
  if(state.title=="SCENARIO EDITOR") {
    UiControllerButton(d,p(1820,1350),25*s,s,"START",tint(255));
    UiText(d,UiFont::Prompt,38*s,p(1870,1325),tint(255),"PLAY SCENARIO",590*s);
  }
  if (dev::Env("MVP06_TRACE_FE")) {
    static unsigned captured = ~0u;
    static unsigned capturedEnd = ~0u;
    if (captured != state.generation && elapsed > 2 && total) {
      captured = state.generation;
      gpu::g_capture = true;
    }
    if (capturedEnd != state.generation && total > 8 &&
        state.selected == total - 1 &&
        std::abs(cursor - state.selected) < .02f) {
      capturedEnd = state.generation;
      gpu::g_capture = true;
    }
  }
}
void DrawSubmenu(double elapsed, bool returningToMain, int mainRow) {
  if (SettingsMenuActive()) {
    DrawSettings(elapsed);
    return;
  }
  Model snapshot;
  {
    std::lock_guard lock(modelMutex);
    snapshot = model;
  }
  static auto root = Assets();
  if (root.empty() || !gpu::g_ui_image)
    return;
  auto *draw = ImGui::GetBackgroundDrawList();
  auto size = ImGui::GetIO().DisplaySize;
  float s = std::min(size.x / 2560.f, size.y / 1440.f),
        ox = (size.x - 2560 * s) * .5f, oy = (size.y - 1440 * s) * .5f;
  auto p = [&](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };
  auto image = [&](const char *file, float x, float y, float w, float h,
                   float alpha = 1.f) {
    auto tex = gpu::g_ui_image(overhaul::UiAsset(root / file));
    if (tex && alpha > 0)
      draw->AddImage((ImTextureID)tex, p(x, y), p(x + w, y + h), {0, 0}, {1, 1},
                     IM_COL32(255, 255, 255, int(255 * alpha)));
  };
  if (returningToMain)
    image("menu_bg.png",0,0,2560,1440);
  else
    UiMenuBackdrop(draw,size,UiBackdrop::Graphite);
  float alpha = Fade(elapsed, 0, .35);
  alpha = 1 - std::pow(1 - alpha, 3.f);
  draw->AddRectFilledMultiColor(p(0, 0), p(1060 + 600 * alpha, 1440),
                                IM_COL32(0, 0, 0, 190), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 230));
  image("menu-trim-imagegen.png", 0, 0, 600, 1440);
  UiBrandLogo(draw,p(145,120),p(975,395));
  auto strip = gpu::g_ui_image(overhaul::UiAsset(root / "menu-selection-imagegen.png"));
  auto panel = [&](float left, float top, float right, float h, bool selected,
                   float a) {
    auto points = std::array<ImVec2, 4>{p(left, top), p(right, top),
                                        p(right - h * .22f, top + h),
                                        p(left - h * .22f, top + h)};
    if (selected && strip)
      draw->AddImageQuad((ImTextureID)strip, points[0], points[1], points[2],
                         points[3], {.02f, .31f}, {.83f, .31f}, {.83f, .64f},
                         {.02f, .64f}, IM_COL32(255, 255, 255, int(a * 255)));
    else
      draw->AddQuadFilled(points[0], points[1], points[2], points[3],
                          IM_COL32(0, 0, 0, int(a * 165)));
    draw->AddPolyline(points.data(), 4,
                      IM_COL32(220, 220, 215, int(a * (selected ? 245 : 100))),
                      true, (selected ? 2 : 1) * s);
  };
  static Model displayed, outgoing;
  static unsigned generation = ~0u;
  static double changed = 0, moved = 0;
  static float shown = 0, from = 0;
  static int target = 0;
  if (generation != snapshot.generation) {
    outgoing = displayed;
    generation = snapshot.generation;
    changed = Clock();
    shown = from = float(snapshot.selected);
    target = snapshot.selected;
  }
  displayed = snapshot;
  float transition = Fade(Clock() - changed, 0, .38);
  transition = transition * transition * (3 - 2 * transition);
  bool returning =
      (outgoing.nested && !snapshot.nested) || snapshot.fromSettings;
  if (!returningToMain && (snapshot.nested || snapshot.fromSettings ||
                           (outgoing.nested && transition < 1)))
    alpha = 1;
  const char *categories[] = {"PLAY NOW",      "GAME MODES",     "LEARN MVP",
                              "CREATION ZONE", "MANAGE ROSTERS", "OPTIONS",
                              "ESPN EXTRAS",   "QUIT GAME"};
  // Native scene changes drive this carousel; selections remain native inputs.
  if (alpha < 1) {
    float shift = -1100 * alpha;
    for (int i = 0; i < 8; ++i) {
      float y = 395 + i * 100, right = 930 - i * 28 + shift;
      panel(-80 + shift, y, right, 100,
            i == (returningToMain ? mainRow : snapshot.category), 1 - alpha);
      UiText(draw, UiFont::Navigation, 54 * s, p(175 + shift, y + 20),
             IM_COL32(255, 255, 255, int(255 * (1 - alpha))), categories[i],
             std::max(0.f, right - 175 - shift - 100) * s);
    }
  }
  UiText(draw, UiFont::Prompt, 36 * s, p(225, 420),
         IM_COL32(220, 220, 220, int(255 * alpha)),
         categories[snapshot.category]);
  if (snapshot.nested)
    UiText(draw, UiFont::Heading, 52 * s, p(225, 475),
           IM_COL32(255, 255, 255, int(255 * alpha)), snapshot.title.c_str(),
           1200 * s);
  if (target != snapshot.selected) {
    target = snapshot.selected;
    from = shown;
    moved = Clock();
  }
  float ease = Fade(Clock() - moved, 0, .22);
  ease = 1 - std::pow(1 - ease, 3.f);
  shown = from + (target - from) * ease;
  auto list = [&](const Model &state, float shift, float opacity,
                  float selected) {
    int count = int(state.labels.size());
    if (!count || opacity <= 0)
      return;
    float top = state.nested ? 565.f : 485.f;
    float height = std::min(102.f, (1220 - top) / count);
    float left = 155 + shift, right = 1370 + shift;
    for (int i = 0; i < count; ++i) {
      float offset = i * height;
      panel(left - offset * .22f, top + offset, right - offset * .22f, height,
            false, opacity);
    }
    float offset = selected * height;
    panel(left - offset * .22f, top + offset, right - offset * .22f, height,
          true, opacity);
    for (int i = 0; i < count; ++i) {
      float y = top + i * height,
            weight = std::max(0.f, 1 - std::abs(i - selected));
      float textX = 225 + shift - i * height * .22f;
      UiText(
          draw, UiFont::Navigation, 44 * s,
          p(textX + 8 * weight, y + (height - 52) * .5f),
          IM_COL32(255, 255, 255, int(255 * opacity * (.66f + .34f * weight))),
          SubmenuText(state.labels[i]),
          (right - i * height * .22f - textX - 120) * s);
      float end = right - i * height * .22f - 58;
      ImVec2 arrow[] = {p(end - 10, y + height * .5f - 10),
                        p(end, y + height * .5f),
                        p(end - 10, y + height * .5f + 10)};
      draw->AddPolyline(arrow, 3, IM_COL32(255, 255, 255, int(245 * opacity)),
                        false, 4 * s);
    }
  };
  if (transition < 1 && outgoing.category == snapshot.category &&
      outgoing.nested != snapshot.nested)
    list(outgoing, (returning ? 1 : -1) * 1500 * transition,
         alpha * (1 - transition), float(outgoing.selected));
  float incoming = (returning ? -1 : 1) * 1500 * (1 - transition);
  if (elapsed < .38 && !snapshot.nested && !outgoing.nested)
    incoming = 1500 * (1 - alpha);
  list(snapshot, incoming, alpha, shown);
  draw->AddRectFilled(p(0, 1295), p(2560, 1440),
                      IM_COL32(0, 0, 0, int(210 * alpha)));
  draw->AddLine(p(0, 1295), p(2560, 1295),
                IM_COL32(210, 210, 210, int(120 * alpha)), s);
  image("controller-reference-a.png", 103 - 28 * alpha, 1293 + 27 * alpha,
        54 + 4 * alpha, 54 + 4 * alpha);
  image("menu-select.png", 177 - 27 * alpha, 1294 + 21 * alpha, 220, 70);
  image("controller-reference-x.png", 373 + 17 * alpha, 1293 + 27 * alpha,
        54 + 4 * alpha, 54 + 4 * alpha);
  image("menu-signin.png", 447 + 18 * alpha, 1294 + 21 * alpha, 220, 70);
  image("controller-reference-b.png", 735, 1320, 58, 58, alpha);
  image("menu-back.png", 810, 1315, 220, 70, alpha);
  if (dev::Env("MVP06_TRACE_FE")) {
    static unsigned captured = ~0u;
    if (captured != snapshot.generation && elapsed > 1.2) {
      captured = snapshot.generation;
      gpu::g_capture = true;
    }
  }
}
} // namespace game
