#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace kernel::modvfs {

// A read-only view of a game file with mod entries swapped in; nothing is written to disk.
class Virtual {
public:
    virtual ~Virtual() = default;
    virtual uint64_t Size() const = 0;
    virtual size_t Read(uint64_t pos, void* dst, size_t len) = 0;
};

// null when no enabled mod touches this host file
std::shared_ptr<Virtual> Open(const std::filesystem::path& host);

// An archive that is missing but extracted beside it ("x.big" -> "x_big/", docs/LOOSE_ASSETS.md), served as the
// archive with mods and patches applied; null when there is no such folder. LooseFolder only finds the folder.
std::shared_ptr<Virtual> OpenLoose(const std::filesystem::path& host);
std::filesystem::path LooseFolder(const std::filesystem::path& host);

// A game's own change to one BIG entry, made as the archive is opened: it gets the entry's bytes (RefPack unpacked
// first) and edits them in place; false leaves the entry as it is. Register before the archive is first opened.
using Patch = bool (*)(std::vector<uint8_t>& data);
void AddPatch(const char* archive, const char* entry, Patch patch);

}  // namespace kernel::modvfs
