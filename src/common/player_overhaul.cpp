#include "player_overhaul.h"

#include "core/log.h"
#include "kernel/mods.h"
#include "kernel/modvfs.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace game::player_overhaul {
namespace {
constexpr char kMod[] = "show26-player-overhaul";
constexpr std::string_view kSchema = "show26-player-overhaul-v1";
constexpr size_t kMemberLimit = 8 * 1024 * 1024;

struct Model {
    const char* ord;
    const char* orl;
    std::vector<uint8_t> data;
    std::vector<uint8_t> link;
};

std::array<Model, 3> models{{
    {"mebody.ord", "mebody.orl", {}, {}},
    {"lobody.ord", "lobody.orl", {}, {}},
    {"hihead.ord", "hihead.orl", {}, {}},
}};

bool ReadOwned(const std::filesystem::path& path, std::vector<uint8_t>& bytes, size_t limit) {
    return kernel::mods::FromMod(path, kMod) && kernel::mods::Load(path, bytes, limit);
}

bool HasSplitElfHeader(const std::vector<uint8_t>& data, const std::vector<uint8_t>& link) {
    if (data.size() < 52 || link.empty()) return false;
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F' ||
        data[4] != 1 || data[5] != 1 || data[16] != 1 || data[17] != 0) return false;
    const size_t sections = size_t(data[32]) | size_t(data[33]) << 8 |
                            size_t(data[34]) << 16 | size_t(data[35]) << 24;
    const size_t count = size_t(data[48]) | size_t(data[49]) << 8;
    return data[46] == 40 && data[47] == 0 && count != 0 && sections >= data.size() &&
           sections - data.size() <= link.size() && count * 40 <= link.size() - (sections - data.size());
}

template<size_t Index, bool Link>
bool Replace(std::vector<uint8_t>& bytes) {
    bytes = Link ? models[Index].link : models[Index].data;
    return true;
}

template<size_t Index>
void MountModel(const std::filesystem::path& root) {
    auto& model = models[Index];
    const auto ord = root / model.ord;
    const auto orl = root / model.orl;
    if (!kernel::mods::FromMod(ord, kMod) && !kernel::mods::FromMod(orl, kMod)) return;
    if (!ReadOwned(ord, model.data, kMemberLimit) || !ReadOwned(orl, model.link, kMemberLimit) ||
        !HasSplitElfHeader(model.data, model.link)) {
        LOGW("player overhaul: incomplete or invalid model pair %s, using base archive entries", model.ord);
        return;
    }
    kernel::modvfs::AddPatch("GEOMETRY.BIG", model.ord, Replace<Index, false>);
    kernel::modvfs::AddPatch("GEOMETRY.BIG", model.orl, Replace<Index, true>);
    LOGR("player overhaul: registered GEOMETRY.BIG/%s + %s", model.ord, model.orl);
}
}

void Install() {
    auto guard = kernel::mods::Lock();
    if (!kernel::mods::Enabled(kMod)) return;
    const auto root = kernel::mods::GameDir() / "data/player-overhaul";
    std::vector<uint8_t> marker;
    if (!ReadOwned(root / "schema.txt", marker, 64) ||
        !std::equal(marker.begin(), marker.end(), kSchema.begin(), kSchema.end())) {
        LOGW("player overhaul: disabled, missing or incompatible schema");
        return;
    }
    MountModel<0>(root / "geometry");
    MountModel<1>(root / "geometry");
    MountModel<2>(root / "geometry");
}
}
