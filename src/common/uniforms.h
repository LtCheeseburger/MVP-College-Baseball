#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game::uniforms {

enum class Part : unsigned { Hat, Jersey, Pants };
constexpr size_t Index(Part part) { return static_cast<size_t>(part); }
constexpr std::string_view kVariants = "abcdghijklmnop";

// A stock piece uses variant. An added piece uses texture, with variant supplying the original cut/lettering
// metadata. Team here is the roster record index, NOT the logo/asset id used in uNNNx.xsh filenames.
struct Piece {
    std::string id;
    std::string name;
    Part part = Part::Hat;
    uint32_t team = 0;
    uint32_t variant = 0;
    std::string texture;
    std::string font;  // jersey only; empty inherits the donor's lettering
    bool operator==(const Piece&) const = default;
};

struct Preset {
    uint32_t variant;
    std::string name;
};

struct Wardrobe {
    std::vector<Preset> presets;
    std::array<std::vector<Piece>, 3> pieces;
};

struct Outfit {
    uint32_t team = 0;
    uint32_t preset = 0;
    std::array<Piece, 3> pieces;
};

// Owned by one pregame control/side. No settings or save serialization. Reconstructed for each matchup and
// whenever the team changes, so the home and away sides can also wear different combinations of the same team.
class Selection {
public:
    Selection(uint32_t team, Wardrobe wardrobe, uint32_t default_variant);
    void MoveRow(int direction);
    void Cycle(int direction);
    void ApplyPreset(size_t index);
    unsigned Row() const { return row_; }
    const Wardrobe& Choices() const { return wardrobe_; }
    const Outfit& Current() const { return outfit_; }
    bool IsCustom() const;
    std::array<std::string, 4> Labels() const;

private:
    Wardrobe wardrobe_;
    Outfit outfit_;
    size_t preset_ = 0;
    std::array<size_t, 3> selected_{};
    unsigned row_ = 0;
};

Wardrobe MakeWardrobe(uint32_t team, std::span<const Preset> presets, std::span<const Piece> additions);
std::string VariantName(uint32_t variant);

// The original uniform object's 39 material texture slots. The hat group includes every batting helmet;
// pants includes socks/short pants and the distant-player leg texture. Empty/gear/lettering slots are untouched.
struct TextureSlot { uint32_t slot; const char* tag; };
std::span<const TextureSlot> Slots(Part part);

// Parse one versioned authoring record. No paths outside models.big, control characters, oversized strings,
// cross-team sources or unsupported donor variants reach the original engine's fixed filename buffers.
std::optional<Piece> ParsePiece(std::string_view text, std::string& error);
std::vector<Piece> LoadPieces(uint32_t team);

// Runtime FEL patch, also exercised without the game by the portable tests.
bool PatchLayout(std::vector<uint8_t>& bytes);

}  // namespace game::uniforms
