#include "overhaul.h"
#include "core/log.h"
#include "app/app.h"
#include "kernel/mods.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cctype>
#include <cstring>
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
        bool on = (kernel::mods::Enabled(kModId) || kernel::mods::Enabled(kCombinedModId)) &&
                  kernel::mods::FromMod(kernel::mods::GameDir() / "data/overhaul/overhaul.json");
        LOGR("overhaul: %s", on ? "on" : "off");
        return on;
    }();
    return active;
}

fs::path Root() { return kernel::mods::GameDir() / "data/overhaul/startup"; }

fs::path ControllerIcon(const std::string& label) {
 std::string key=label;for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));
 if(key=="white")key="lb";if(key=="black")key="rb";
 if(key=="l")key="lt";if(key=="r")key="rt";
 return Root()/"controller-buttons"/((app::Get().button_icons==1?"playstation-":"xbox-")+key+".png");
}
fs::path UiAsset(const fs::path& path) {
 const auto file=path.filename().string();constexpr const char* prefix="controller-reference-";
 if(file.rfind(prefix,0)==0 && path.extension()==".png")return ControllerIcon(file.substr(std::strlen(prefix),file.size()-std::strlen(prefix)-4));
 return path;
}

bool Load(const fs::path& path, std::vector<uint8_t>& out) {
    if (!Active()) { out.clear(); return false; }
    return kernel::mods::Load(path, out);
}

std::string Text(const fs::path& path) {
    std::vector<uint8_t> bytes;
    if (!Load(path, bytes)) return {};
    return std::string(bytes.begin(), bytes.end());
}

fs::path TeamLogo(const std::string& school,const std::string& nickname,const std::string& hint) {
    auto normalize=[](const std::string& value){std::string out;for(unsigned char c:value)if(std::isalnum(c))out+=char(std::toupper(c));return out;};
    static const auto aliases=[&]{std::unordered_map<std::string,std::string> map;
        for(auto file:{"teams.tsv","logo-aliases.tsv"}) {std::istringstream stream(Text(Root()/"base-logos"/file));std::string line;
            while(std::getline(stream,line)){auto tab=line.find('\t');if(tab==std::string::npos)continue;auto id=line.substr(tab+1);if(!id.empty()&&id.back()=='\r')id.pop_back();map[normalize(line.substr(0,tab))]=id;}}
        return map;}();
    for(const auto& key:{normalize(school),normalize(nickname)})if(!key.empty())if(auto it=aliases.find(key);it!=aliases.end())return Root()/"base-logos"/(it->second+".png");
    return hint.empty()?fs::path{}:Root()/"base-logos"/(hint+".png");
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
