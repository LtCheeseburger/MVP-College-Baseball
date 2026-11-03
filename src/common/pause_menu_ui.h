#pragma once
#include <cstdint>
namespace game {
void SuppressNativeUiScene(uint32_t root);
bool NativeUiRenderSuppressed(uint32_t root,uint32_t target);
void ClearNativeUiSuppression();
void BindPauseMenuTheme(const char *, uint32_t);
void DrawPauseMenuTheme();
bool PauseMenuThemeActive();
void PauseMenuThemeTimeline(const char *, const char *);
} // namespace game
