#pragma once
#include <cstdint>
namespace input {
struct Pad;
}
namespace game {
void StartupReady();
bool StartupMainMenuReady();
void StartupTimeline(const char *name);
void StartupMainMenuSelection(int row);
void PollNativeMainMenu();
void StartupNativeScene(const char *name, uint32_t scene);
void StartupPad(int, input::Pad &);
void BeginLicensing();
void StartupScreen(const char *);
void StartupUi(double);
bool StartupSubmenuActive();
bool ReplayUiActive();
bool DrawNativeStadiumSelection();
void PopupOpened(uint32_t scene);
void PopupClosed(uint32_t scene);
} // namespace game
