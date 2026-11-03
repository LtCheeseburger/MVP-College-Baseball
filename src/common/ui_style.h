#pragma once
#include "imgui.h"
namespace game {
void InitializeUi(void *);
enum class UiFont { Navigation, Heading, Body, Prompt, Settings, TeamDisplay, TeamVersus };
ImFont *Font(UiFont role);
void UiText(ImDrawList *draw, UiFont role, float size, ImVec2 pos, ImU32 color,
            const char *text, float maxWidth = 0);
} // namespace game
