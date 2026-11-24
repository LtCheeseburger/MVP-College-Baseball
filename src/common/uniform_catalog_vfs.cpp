#include "uniforms.h"

#include <filesystem>
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/mods.h"

namespace game::uniforms {

std::vector<Piece> LoadPieces(uint32_t team) {
    std::vector<Piece> pieces;
    const auto path = kernel::ResolvePath("D:\\data\\uniforms", 0);
    if (path.empty()) return pieces;
    const auto folder = kernel::mods::Find(path, std::to_string(team));
    if (folder.empty()) return pieces;
    for (const auto& child : kernel::mods::List(folder)) {
        auto extension = std::filesystem::path(child.name).extension().string();
        for (char& c : extension) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (child.dir || extension != ".toml") continue;
        std::vector<uint8_t> bytes;
        std::string error;
        if (!kernel::mods::Load(folder / child.name, bytes, 4096)) {
            LOGR("uniforms: cannot read %s (limit 4096 bytes)", child.name.c_str());
            continue;
        }
        auto piece = ParsePiece(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), error);
        if (!piece || piece->team != team) {
            LOGR("uniforms: skipping %s: %s", child.name.c_str(), piece ? "team differs from folder" : error.c_str());
            continue;
        }
        // Validation of the texture tags happens at resource load; the original loader also supports packed BIGs.
        pieces.push_back(std::move(*piece));
        if (pieces.size() == 128) break;
    }
    return pieces;
}
}  // namespace game::uniforms
