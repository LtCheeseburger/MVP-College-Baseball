StageLiveTextures g_stage_live;
CommitLiveTextures g_commit_live;
LiveTexturesApplied g_live_applied;
std::string g_pending_live;

bool ReadLivePlan(const fs::path& generation, std::vector<LiveTexture>& entries, std::set<std::string>& packs, std::string& error) {
    const auto manifest = generation / "live-textures.json";
    if (!fs::exists(manifest)) return true; // Older Workshop builds retain unopened-only behavior.
    std::vector<uint8_t> bytes; Json json;
    if (!ReadDisk(manifest, bytes, 16 * 1024 * 1024 + 1) || bytes.size() > 16 * 1024 * 1024 ||
        !ParseJson(std::string(bytes.begin(), bytes.end()), json) || json.Num("format", 0) != 1) { error = "Invalid live texture manifest"; return false; }
    const auto problems = json.Strings("problems");
    if (!problems.empty()) { error = problems.front(); return false; }
    auto rows = json.Get("mappings");
    if (!rows || rows->type != Json::Array) { error = "Missing live texture mappings"; return false; }
    std::set<uint64_t> keys;
    for (auto& row : rows->items) {
        auto pack = row.Str("packPath"), file = row.Str("file");
        auto path = fs::u8path(file);
        if (!pack.ends_with(".xsh") || fs::path(pack).is_absolute() || pack.find("..") != std::string::npos || pack.find_first_of(":\\") != std::string::npos ||
            !file.starts_with("live-textures/") || !file.ends_with(".dds") || path.is_absolute() || path.generic_string() != file || file.find("..") != std::string::npos || file.find_first_of(":\\") != std::string::npos) { error = "Unsafe live texture path"; return false; }
        auto image = generation / path;
        if (!fs::is_regular_file(image) || fs::file_size(image) > 512ull * 1024 * 1024) { error = "Missing or oversized live texture"; return false; }
        const auto rowKeys = row.Strings("keys");
        if (rowKeys.empty()) { error = "Missing live texture keys"; return false; }
        for (auto& key : rowKeys) {
            if (key.size() != 16 || key.find_first_not_of("0123456789abcdef") != std::string::npos) { error = "Invalid live texture key"; return false; }
            uint64_t value = std::stoull(key, nullptr, 16);
            if (!keys.insert(value).second) { error = "Ambiguous live texture ownership"; return false; }
            entries.push_back({value, image});
        }
        packs.insert(Lower(pack));
    }
    return true;
}
bool LiveCovered(const std::string& path, const std::set<std::string>& packs) {
    for (auto& pack : packs) if (path.starts_with(pack + "/") && (path.ends_with(".dds") || path == pack + "/xsh.meta")) return true;
    return false;
}
