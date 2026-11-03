#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The MVP 27 UI overhaul: code in the exe, switched on only when the mvp27-ui mod is enabled (MVP27_PLAN.md
// Phase 2). Off, every hook calls the original and no overhaul file is opened.
namespace overhaul {

constexpr const char* kModId = "mvp27-ui";

// decided once, on the first call after kernel::mods::Init: mvp27-ui is on and serves data/overhaul/overhaul.json
bool Active();

// <game>/data/overhaul/startup, the overhaul's art; paths under it go through the mod layer
std::filesystem::path Root();

// a file under the game folder as the game sees it (mods on top of the disk)
bool Load(const std::filesystem::path& path, std::vector<uint8_t>& out);
std::string Text(const std::filesystem::path& path);
// a disk path for APIs that only open files (Media Foundation): the file itself, or a copy in %TEMP% when a mod
// supplies it; empty when there is none
std::filesystem::path DiskFile(const std::filesystem::path& path);
// Release only copies created by DiskFile, after the file-only reader has closed.
void ReleaseDiskFile(const std::filesystem::path& path);

}  // namespace overhaul
