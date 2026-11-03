#include "overhaul.h"
#include "core/log.h"
#include "kernel/mods.h"
#include <fstream>
#include <mutex>
#include <set>
#include <windows.h>

namespace fs = std::filesystem;

namespace overhaul {
namespace {
// The decoder can finish during global teardown; keep its registry alive until process exit.
struct Copies { std::mutex mutex; std::set<fs::path> paths; };
Copies& TempCopies() { static auto* value = new Copies; return *value; }
}

bool Active() {
    static const bool active = [] {
        bool on = kernel::mods::Enabled(kModId) &&
                  kernel::mods::FromMod(kernel::mods::GameDir() / "data/overhaul/overhaul.json");
        LOGR("overhaul: %s", on ? "on (mvp27-ui)" : "off");
        return on;
    }();
    return active;
}

fs::path Root() { return kernel::mods::GameDir() / "data/overhaul/startup"; }

bool Load(const fs::path& path, std::vector<uint8_t>& out) {
    if (!Active()) { out.clear(); return false; }
    return kernel::mods::Load(path, out);
}

std::string Text(const fs::path& path) {
    std::vector<uint8_t> bytes;
    if (!Load(path, bytes)) return {};
    return std::string(bytes.begin(), bytes.end());
}

fs::path DiskFile(const fs::path& path) {
    if (!Active()) return {};
    if (!kernel::mods::FromMod(path)) return kernel::mods::IsFile(path) ? path : fs::path{};
    std::vector<uint8_t> bytes;
    if (!Load(path, bytes)) return {};
    wchar_t temp[MAX_PATH], unique[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH || !GetTempFileNameW(temp, L"mvp", 0, unique)) return {};
    // GetTempFileName reserves a unique file; retain it and let MF detect the media type.
    fs::path copy(unique);
    std::error_code ec;
    std::ofstream f(copy, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    f.close();
    if (!f) {
        LOGE("overhaul: cannot write %s", copy.string().c_str());
        fs::remove(copy, ec);
        return {};
    }
    { auto& copies = TempCopies(); std::lock_guard lock(copies.mutex); copies.paths.insert(copy); }
    return copy;
}

void ReleaseDiskFile(const fs::path& path) {
    auto& copies = TempCopies();
    std::lock_guard lock(copies.mutex);
    if (!copies.paths.count(path)) return;
    std::error_code ec;
    fs::remove(path, ec);
    if (!ec) copies.paths.erase(path);
}

}  // namespace overhaul
