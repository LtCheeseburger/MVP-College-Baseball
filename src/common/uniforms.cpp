#include "uniforms.h"

#include <algorithm>
#include <stdexcept>

namespace game::uniforms {
namespace {
size_t Step(size_t value, int direction, size_t count) {
    return count ? (direction < 0 ? (value + count - 1) % count : (value + 1) % count) : 0;
}
constexpr TextureSlot kHat[] = {{1, "hat"}, {2, "hlm1"}, {3, "hlm2"}, {15, "hhlm"}};
constexpr TextureSlot kJersey[] = {{0, "tors"}, {4, "tors"}, {5, "tors"}, {17, "tors"}, {18, "tors"},
                                  {28, "merf"}, {29, "merk"}, {30, "mslv"}, {37, "torf"}};
constexpr TextureSlot kPants[] = {{6, "lega"}, {7, "legb"}, {8, "legc"}, {9, "legd"},
                                 {10, "lesa"}, {11, "lesd"}, {20, "llod"}};
}  // namespace

std::span<const TextureSlot> Slots(Part part) {
    switch (part) {
        case Part::Hat: return kHat;
        case Part::Jersey: return kJersey;
        case Part::Pants: return kPants;
    }
    return {};
}

std::string VariantName(uint32_t variant) {
    switch (variant) {
        case 0: return "Home";
        case 1: return "Away";
        case 2: return "Alternate";
        case 3: return "Alternate 2";
        default: return "Uniform " + std::to_string(variant + 1);
    }
}

Wardrobe MakeWardrobe(uint32_t team, std::span<const Preset> presets, std::span<const Piece> additions) {
    Wardrobe result;
    for (const auto& preset : presets) {
        if (preset.variant >= kVariants.size() || std::any_of(result.presets.begin(), result.presets.end(),
                [&](const Preset& p) { return p.variant == preset.variant; })) continue;
        result.presets.push_back(preset);
        for (unsigned i = 0; i < 3; ++i)
            result.pieces[i].push_back({"stock-" + std::to_string(preset.variant), preset.name,
                                       Part(i), team, preset.variant, {}, {}});
    }
    // Only offer a custom piece when its donor actually exists for this team.
    for (const auto& piece : additions) {
        if (piece.team != team || Index(piece.part) >= 3 || piece.texture.empty()) continue;
        if (std::none_of(result.presets.begin(), result.presets.end(),
                [&](const Preset& p) { return p.variant == piece.variant; })) continue;
        auto& list = result.pieces[Index(piece.part)];
        if (std::none_of(list.begin(), list.end(), [&](const Piece& p) { return p.id == piece.id; }))
            list.push_back(piece);
    }
    return result;
}

Selection::Selection(uint32_t team, Wardrobe wardrobe, uint32_t default_variant) : wardrobe_(std::move(wardrobe)) {
    if (wardrobe_.presets.empty()) throw std::invalid_argument("uniform wardrobe has no presets");
    outfit_.team = team;
    auto it = std::find_if(wardrobe_.presets.begin(), wardrobe_.presets.end(),
                           [&](const Preset& p) { return p.variant == default_variant; });
    ApplyPreset(it == wardrobe_.presets.end() ? 0 : size_t(it - wardrobe_.presets.begin()));
}

void Selection::ApplyPreset(size_t index) {
    preset_ = index % wardrobe_.presets.size();
    outfit_.preset = wardrobe_.presets[preset_].variant;
    for (size_t i = 0; i < 3; ++i) {
        selected_[i] = preset_;  // MakeWardrobe puts stock pieces in preset order.
        outfit_.pieces[i] = wardrobe_.pieces[i][preset_];
    }
}

void Selection::MoveRow(int direction) { row_ = unsigned(Step(row_, direction, 4)); }

void Selection::Cycle(int direction) {
    if (!row_) return ApplyPreset(Step(preset_, direction, wardrobe_.presets.size()));
    size_t part = row_ - 1;
    selected_[part] = Step(selected_[part], direction, wardrobe_.pieces[part].size());
    outfit_.pieces[part] = wardrobe_.pieces[part][selected_[part]];
}

bool Selection::IsCustom() const {
    for (const auto& piece : outfit_.pieces)
        if (!piece.texture.empty() || piece.variant != outfit_.preset) return true;
    return false;
}

std::array<std::string, 4> Selection::Labels() const {
    return {"Preset: " + (IsCustom() ? std::string("Custom") : wardrobe_.presets[preset_].name),
            "Hat: " + outfit_.pieces[0].name, "Jersey: " + outfit_.pieces[1].name,
            "Pants: " + outfit_.pieces[2].name};
}
}  // namespace game::uniforms
