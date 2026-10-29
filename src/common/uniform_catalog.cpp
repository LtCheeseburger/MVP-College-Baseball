#include "uniforms.h"

#include <algorithm>
#include <toml++/toml.hpp>

namespace game::uniforms {
namespace {
bool Token(std::string_view s, size_t max) {
    return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
bool PackName(std::string_view s) {
    return s.ends_with(".xsh") && Token(s.substr(0, s.size() - 4), 40);
}
}  // namespace

std::optional<Piece> ParsePiece(std::string_view text, std::string& error) {
    error.clear();
    try {
        const auto t = toml::parse(text);
        Piece p;
        auto fail = [&](const char* why) -> std::optional<Piece> { error = why; return {}; };
        if (t["format"].value<int64_t>() != 1) return fail("format must be 1");
        p.id = t["id"].value_or(std::string{});
        p.name = t["name"].value_or(std::string{});
        if (!Token(p.id, 48) || p.id.starts_with("stock-")) return fail("id must be a unique lowercase token");
        if (p.name.empty() || p.name.size() > 24 || !std::all_of(p.name.begin(), p.name.end(),
                [](unsigned char c) { return c >= 32 && c <= 126; }))
            return fail("name must be 1-24 printable ASCII characters");
        const auto team = t["team"].value<int64_t>();
        if (!team || *team < 0 || *team > 255) return fail("team must be a roster index from 0 to 255");
        p.team = uint32_t(*team);
        const auto part = t["part"].value_or(std::string{});
        if (part == "hat") p.part = Part::Hat;
        else if (part == "jersey") p.part = Part::Jersey;
        else if (part == "pants") p.part = Part::Pants;
        else return fail("part must be hat, jersey or pants");
        const auto donor = t["donor"].value_or(std::string{});
        const auto variant = donor.size() == 1 ? kVariants.find(donor[0]) : std::string_view::npos;
        if (variant == std::string_view::npos) return fail("donor must be one of abcdghijklmnop");
        p.variant = uint32_t(variant);
        p.texture = t["texture"].value_or(std::string{});
        if (!PackName(p.texture)) return fail("texture must be a lowercase .xsh member name without a path");
        if (t.contains("font")) {
            auto font = t["font"].value<std::string>();
            if (!font || !PackName(*font) || p.part != Part::Jersey)
                return fail("font is only valid for jerseys and must name a .xsh member");
            p.font = *font;
        }
        return p;
    } catch (const toml::parse_error&) {
        error = "invalid TOML";
        return {};
    }
}
}  // namespace game::uniforms
