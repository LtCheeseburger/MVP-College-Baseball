#pragma once
#include "imgui.h"
namespace game {
void PausePanel(ImDrawList *, ImVec2 min, ImVec2 max, float scale,
                bool selected = false);
void PauseIcon(ImDrawList *, ImVec2 center, float size, int index,
               ImU32 color = IM_COL32_WHITE);
} // namespace game
