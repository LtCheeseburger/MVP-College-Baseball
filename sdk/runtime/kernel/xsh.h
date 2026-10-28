#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace kernel::xsh {

// A texture pack installed as a folder ("x.xsh/" with xsh.meta and one .dds or .bin per image, docs/LOOSE_ASSETS.md)
bool IsFolder(const std::filesystem::path& dir);

// The .xsh the folder stands for. A DDS in the slot's format is used at any size; one in another format is
// logged and replaced by blank pixels of the original size (MVP Workshop converts formats when it saves).
// Size reads only the DDS headers and gives the same length Build will.
bool Size(const std::filesystem::path& dir, uint64_t& size);
bool Build(const std::filesystem::path& dir, std::vector<uint8_t>& out);

}  // namespace kernel::xsh
