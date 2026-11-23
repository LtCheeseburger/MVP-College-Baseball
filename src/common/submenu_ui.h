#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace input { struct Pad; }
namespace game {
int SubmenuCategory(const char *scene);
uint32_t SubmenuLabelId(int category, const char *name);
void OpenSubmenu(int category, const char *scene = "");
bool NestedSubmenuActive();
struct SettingsRow {
  std::string label, value;
  float fraction = -1;
  bool enabled = true;
};
bool SettingsMenuActive();
int LibraryMenuKind();
void PublishLibrary(int selected, const std::vector<SettingsRow> &rows,
                    const std::string &message, const std::string &album,
                    const std::string &title = "");
void DrawLibrary(double elapsed);
void PublishSettings(int selected, const std::vector<SettingsRow> &rows);
void DrawSettings(double elapsed);
void PublishSubmenu(int category, int selected,
                    const std::vector<uint32_t> &labels);
void DrawSubmenu(double elapsed, bool returningToMain = false, int mainRow = 0);
void SetSubmenuPopupVisible(bool visible);
void PollNativeSubmenu();
int SelectedSubmenuRow();
int SubmenuRowCount();
int RulesSettingsRow();
bool SubmenuPopupVisible();
bool LegacyOnlineNotice();
std::string MissingTutorialVideo();
bool HandleTutorialNoticePad(int port, input::Pad &pad);
// Read-only UI state for the isolated native navigation acceptance driver.
bool InspectPopup(std::string &text, std::vector<std::string> &choices, int &selected);
std::string UiString(uint32_t id);
void PublishPopup(const std::string &text,
                  const std::vector<std::string> &choices, int selected);
void DrawNotification();
} // namespace game
