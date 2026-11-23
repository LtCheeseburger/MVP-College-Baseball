#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

// .mvpmod mods (docs/MVPMOD.md): the game's files as the game sees them, game/ on disk with enabled mods on top.
// Every reader of game files (files.cpp, modvfs, xsh) goes through Stat/List/Load; paths outside the game folder,
// or any path with no mods enabled, are plain disk lookups.
namespace kernel::mods {

// Reads mods_dir\mods.json, opens the enabled mods plus `extra` (--mod) and builds the index. Once, before Boot.
// none (--no-mods) skips mods.json; xbe_sha1 is the release mods must name.
void Init(const std::filesystem::path& game_dir, const std::filesystem::path& mods_dir,
          const std::vector<std::filesystem::path>& extra, bool none, const std::string& xbe_sha1);
bool Active();
// a mod with this id is on (after Init)
bool Enabled(const std::string& id);
bool Required(const std::string& id);
// the game folder Init was given; mod paths are relative to it
std::filesystem::path GameDir();
// Serializes complete VFS transactions against Workshop mount publication.
std::unique_lock<std::recursive_mutex> Lock();
// Render callbacks must never wait for a game-thread mount transaction: that
// thread can be waiting for the renderer while opening native resources.
std::unique_lock<std::recursive_mutex> TryLock();
void RecordOpen(const std::filesystem::path& path);
void RecordList(const std::filesystem::path& path);
void ConfigureReload(const std::filesystem::path& request, const std::filesystem::path& mod, const std::string& sha);
void PollReload();
struct LiveTexture { uint64_t key; std::filesystem::path file; };
using StageLiveTextures = std::function<bool(const std::vector<LiveTexture>&, const std::string&, std::string&)>;
using CommitLiveTextures = std::function<void(const std::string&)>;
using LiveTexturesApplied = std::function<bool(const std::string&)>;
void ConfigureLiveTextures(StageLiveTextures stage, CommitLiveTextures commit, LiveTexturesApplied applied);

struct Node {
    bool dir = false;
    uint64_t size = 0;
    std::filesystem::file_time_type time{};
};
bool Stat(const std::filesystem::path& p, Node& out);
bool Exists(const std::filesystem::path& p);
bool IsFile(const std::filesystem::path& p);
bool IsDir(const std::filesystem::path& p);

struct Child {
    std::string name;  // the disk's spelling when it has one, else the mod's
    bool dir = false;
};
// sorted by name, case-insensitive
std::vector<Child> List(const std::filesystem::path& dir);
// a child of dir by name, any case; empty when there is none
std::filesystem::path Find(const std::filesystem::path& dir, const std::string& name);

bool Load(const std::filesystem::path& p, std::vector<uint8_t>& out, size_t limit = SIZE_MAX);

// a mod supplies this file (not a folder)
bool FromMod(const std::filesystem::path& p);
bool FromMod(const std::filesystem::path& p, const std::string& id);
// a mod asks for this added archive member to be RefPack-compressed (mod.json "refpack")
bool Packed(const std::filesystem::path& p);

// the active set as written beside each save: {"mods":[{"id","version","affectsSaves"}...]}
std::string SaveRecord();

}  // namespace kernel::mods
