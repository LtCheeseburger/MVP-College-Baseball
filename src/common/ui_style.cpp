#include "ui_style.h"
#include "overhaul.h"
#include "app/app.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <filesystem>
namespace game {
namespace {
ImFont *fonts[7]{};
}
void InitializeUi(void *) {
  auto root = overhaul::Root() / "fonts";
  const char *files[] = {"Inter_18pt-ExtraBoldItalic.ttf",
                         "BarlowCondensed-ExtraBold.ttf",
                         "Inter_18pt-Regular.ttf", "BarlowCondensed-Medium.ttf",
                         "BarlowCondensed-ExtraBoldItalic.ttf",
                         "BarlowCondensed-ExtraBold.ttf", "Inter_18pt-ExtraBoldItalic.ttf"};
  for (int i = 0; i < 7; i++) {
    std::vector<uint8_t> file;
    if (overhaul::Load(root / files[i], file)) {
      static const ImWchar glyphs[] = {0x20, 0xFF, 0x2000, 0x214F, 0};
      static const ImWchar displayGlyphs[] = {0x20, 0xFF, 0};
      static const ImWchar versusGlyphs[] = {'S', 'S', 'V', 'V', 0};
      // the atlas owns and frees its copy
      void *data = IM_ALLOC(file.size());
      std::memcpy(data, file.data(), file.size());
      fonts[i] = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
          data, int(file.size()), i == 5 ? 128.f : i == 6 ? 180.f : 48.f, nullptr,
          i == 5 ? displayGlyphs : i == 6 ? versusGlyphs : glyphs);
    }
  }
}
ImFont *Font(UiFont role) {
  auto f = fonts[int(role)];
  return f ? f : ImGui::GetFont();
}
void UiText(ImDrawList *draw, UiFont role, float size, ImVec2 pos, ImU32 color,
            const char *text, float maxWidth) {
  auto font = Font(role);
  if (maxWidth > 0) {
    float width = font->CalcTextSizeA(size, 1e6f, 0, text).x;
    if (width > maxWidth)
      size *= maxWidth / width;
  }
  draw->AddText(font, size, pos, color, text);
}
} // namespace game
