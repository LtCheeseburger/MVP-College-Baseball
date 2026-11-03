#include "pause_art.h"
#include "overhaul.h"
#include "app/app.h"
#include "gpu/backend.h"
#include <algorithm>
#include <fstream>
#include <unordered_map>
namespace game {
namespace {
struct Region {
  std::string file;
  float width, height, left, top, right, bottom;
};
auto Root() { return overhaul::Root() / "pause-v2"; }
const std::filesystem::path &ArtRoot() {
  static const auto root = Root();
  return root;
}
const Region *Find(const std::string &key) {
  static auto regions = []() {
    std::unordered_map<std::string, Region> values;
    std::istringstream f(overhaul::Text(ArtRoot() / "regions.tsv"));
    std::string name;
    Region r;
    while (f >> name >> r.file >> r.width >> r.height >> r.left >> r.top >>
           r.right >> r.bottom)
      values[name] = r;
    return values;
  }();
  auto it = regions.find(key);
  return it == regions.end() ? nullptr : &it->second;
}
} // namespace
void PausePanel(ImDrawList *d, ImVec2 min, ImVec2 max, float scale,
                bool selected) {
  if (scale <= 0 || max.x <= min.x || max.y <= min.y)
    return;
  // Shared surface: one quiet frame, no stretched bitmap edges or nested
  // borders.
  d->AddRectFilledMultiColor(
      min, max,
      selected ? IM_COL32(176, 12, 24, 255) : IM_COL32(23, 28, 34, 250),
      selected ? IM_COL32(105, 7, 18, 255) : IM_COL32(10, 14, 19, 250),
      selected ? IM_COL32(105, 7, 18, 255) : IM_COL32(10, 14, 19, 250),
      selected ? IM_COL32(176, 12, 24, 255) : IM_COL32(23, 28, 34, 250));
  if (selected) {
    d->AddRectFilled(min, {min.x + 3 * scale, max.y},
                     IM_COL32(255, 61, 68, 255));
  } else {
    d->AddRect(min, max, IM_COL32(114, 126, 140, 105), 0, 0, scale);
    d->AddLine(min, {max.x, min.y}, IM_COL32(173, 185, 199, 130), scale);
  }
}
void PauseIcon(ImDrawList *d, ImVec2 center, float size, int index,
               ImU32 color) {
  if (!gpu::g_ui_image)
    return;
  auto *r = Find("icon" + std::to_string(index));
  if (!r)
    return;
  auto texture = gpu::g_ui_image(ArtRoot() / r->file);
  if (!texture)
    return;
  float width = r->right - r->left, height = r->bottom - r->top,
        factor = size / std::max(width, height);
  width *= factor;
  height *= factor;
  if (index == 0)
    center.x += size * .06f; // Optical balance for the play triangle.
  d->AddImage((ImTextureID)texture,
              {center.x - width / 2, center.y - height / 2},
              {center.x + width / 2, center.y + height / 2},
              {r->left / r->width, r->top / r->height},
              {r->right / r->width, r->bottom / r->height}, color);
}
} // namespace game
