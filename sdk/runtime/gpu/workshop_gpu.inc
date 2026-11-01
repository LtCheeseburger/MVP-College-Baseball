namespace {
std::mutex g_workshop_lock;
std::unordered_map<uint64_t, Image> g_workshop_staged, g_workshop_images;
std::string g_workshop_staged_revision, g_workshop_applied_revision;
bool g_workshop_commit = false;
}

bool StageWorkshop(const std::vector<std::pair<uint64_t, std::filesystem::path>>& textures, const std::string& revision, std::string& error) {
    std::unordered_map<uint64_t, Image> next;
    size_t total = 0;
    for (auto& [key, path] : textures) {
        Image image;
        if (!ReadDds(path, image)) { error = "Cannot decode live DDS: " + path.filename().string(); return false; }
        for (auto& level : image.levels) total += level.px.size();
        if (total > 2ull * 1024 * 1024 * 1024 || !next.emplace(key, std::move(image)).second) { error = "Oversized or ambiguous live texture plan"; return false; }
    }
    std::lock_guard lock(g_workshop_lock);
    g_workshop_staged = std::move(next); g_workshop_staged_revision = revision; g_workshop_commit = false;
    return true;
}
void CommitWorkshop(const std::string& revision) {
    std::lock_guard lock(g_workshop_lock);
    if (revision == g_workshop_staged_revision) g_workshop_commit = true;
}
bool WorkshopApplied(const std::string& revision) {
    std::lock_guard lock(g_workshop_lock); return revision == g_workshop_applied_revision;
}
bool UpdateWorkshop() {
    std::lock_guard lock(g_workshop_lock);
    if (!g_workshop_commit) return false;
    g_workshop_images = std::move(g_workshop_staged); g_workshop_applied_revision = g_workshop_staged_revision;
    g_workshop_commit = false;
    return true;
}
