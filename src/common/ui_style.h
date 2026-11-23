#pragma once
#include "imgui.h"
namespace game {
void InitializeUi(void *);
enum class UiFont { Navigation, Heading, Body, Prompt, Settings, TeamDisplay, TeamVersus };
ImFont *Font(UiFont role);
void UiText(ImDrawList *draw, UiFont role, float size, ImVec2 pos, ImU32 color,
            const char *text, float maxWidth = 0);
void UiBrandLogo(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 tint = IM_COL32_WHITE);
void UiBroadcastPanel(ImDrawList*, ImVec2 min, ImVec2 max, float scale, bool selected=false);
bool UiControllerGlyph(ImDrawList*, ImVec2 min, ImVec2 max, const char* label, ImU32 tint=IM_COL32_WHITE);
void UiTriggerBadge(ImDrawList*, ImVec2 min, ImVec2 max, float scale, const char* label);
void UiControllerButton(ImDrawList*, ImVec2 center, float radius, float scale, const char* label, ImU32 color);
void UiMenuHighlight(ImDrawList* draw, ImVec2 min, ImVec2 max, float scale);
enum class UiBackdrop { Charcoal, Graphite, Steel };
void UiMenuBackdrop(ImDrawList* draw, ImVec2 screen, UiBackdrop style = UiBackdrop::Charcoal);
void UiPrestigeStars(ImDrawList* draw, ImVec2 firstCenter, float radius, float spacing, int filled);
} // namespace game
