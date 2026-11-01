// Included by mods.cpp: uses its validated Mod parser and mount index builders.
namespace {
fs::path g_reload_request, g_reload_mod;
std::string g_reload_sha, g_reload_revision;
std::unordered_set<std::string> g_opened;
std::unordered_set<std::string> g_listed;
#include "workshop_live.inc"

std::string GameRelative(const fs::path& path) {
    auto text = Lower(fs::absolute(path).lexically_normal().generic_string());
    if (text.size() <= g_game.size() || text.compare(0, g_game.size(), g_game) || text[g_game.size()] != '/') return {};
    return text.substr(g_game.size() + 1);
}
bool Related(const std::string& a, const std::string& b) {
    return a == b || (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == '/') ||
        (b.size() > a.size() && b.compare(0, a.size(), a) == 0 && b[a.size()] == '/');
}
std::string EscapeReload(const std::string& text) {
    std::string out;
    for (unsigned char c : text) { if (c == '"' || c == '\\') out += '\\'; if (c >= 32) out += char(c); }
    return out;
}
void Acknowledge(const std::string& revision, const char* status, const std::string& message) {
    auto temporary = g_reload_request.parent_path() / "reload.ack.tmp";
    auto target = g_reload_request.parent_path() / "reload.ack.json";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << "{\"format\":1,\"revision\":\"" << revision << "\",\"status\":\"" << status
            << "\",\"messages\":[\"" << EscapeReload(message) << "\"]}";
        out.flush();
        if (!out) { LOGE("mods: Workshop acknowledgement write failed"); return; }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        LOGE("mods: Workshop acknowledgement rename failed (%lu)", GetLastError());
    LOGR("mods: Workshop reload %s: %s", status, message.c_str());
}
bool SafeGeneration(const fs::path& request, const std::string& revision, const std::string& relative, fs::path& target) {
    if (revision.size() != 32 || revision.find_first_not_of("0123456789abcdef") != std::string::npos || relative != "generations/" + revision) return false;
    auto root = fs::absolute(request.parent_path()).lexically_normal();
    target = root / "generations" / revision;
    // Reject junctions and symlinks in every generation component, including payloads.
    for (auto path : {root, root / "generations", target}) {
        auto attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(target, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        auto attr = GetFileAttributesW(it->path().c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return !error;
}
bool ReadPayloads(const Mod& mod, std::map<std::string, std::vector<uint8_t>>& out) {
    uint64_t total = 0;
    for (const auto& name : mod.files) {
        if (name.compare(0, 6, "files/")) continue;
        auto relative = Lower(name.substr(6));
        auto path = fs::u8path(relative);
        if (relative.empty() || path.is_absolute() || path.generic_string() != relative || relative.find('\\') != std::string::npos || relative.find_first_of(":*?\"<>|") != std::string::npos) return false;
        for (auto& part : path) if (part == ".." || part == ".") return false;
        auto size = mod.SizeOf(name); total += size;
        if (size > 512ull * 1024 * 1024 || total > 2ull * 1024 * 1024 * 1024 || out.count(relative)) return false;
        if (!mod.Read(name, out[relative]) || out[relative].size() != size) return false;
    }
    return true;
}
void Reindex() {
    g_files.clear(); g_dirs.clear(); g_hidden.clear(); g_owned.clear(); g_packed.clear();
    for (auto& mod : g_mods) {
        for (auto& file : mod->files) AddFile(*mod, file);
        for (auto& removed : mod->meta.Strings("remove")) Remove(*mod, removed);
        for (auto packed : mod->meta.Strings("refpack")) { std::replace(packed.begin(), packed.end(), '\\', '/'); g_packed.insert(Lower(packed)); }
    }
}
} // namespace

void ConfigureLiveTextures(StageLiveTextures stage, CommitLiveTextures commit, LiveTexturesApplied applied) {
    auto guard = Lock(); g_stage_live = std::move(stage); g_commit_live = std::move(commit); g_live_applied = std::move(applied);
}
void RecordOpen(const fs::path& path) {
    auto guard = Lock();
    auto relative = GameRelative(path);
    if (!relative.empty()) g_opened.insert(relative);
}

void RecordList(const fs::path& path) {
    auto guard = Lock();
    auto full = Lower(fs::absolute(path).lexically_normal().generic_string());
    auto relative = GameRelative(path);
    if (full == g_game || !relative.empty()) g_listed.insert(relative);
}

void ConfigureReload(const fs::path& request, const fs::path& mod, const std::string& sha) {
    auto guard = Lock();
    g_reload_request = fs::absolute(request); g_reload_mod = fs::absolute(mod); g_reload_sha = sha; g_reload_revision.clear();
}

void PollReload() {
    auto guard = Lock();
    if (g_reload_request.empty()) return;
    if (!g_pending_live.empty()) {
        if (g_live_applied && g_live_applied(g_pending_live)) {
            Acknowledge(g_pending_live, "applied", "Live textures committed at a render-frame boundary; VFS generation is active");
            g_pending_live.clear();
        } else return;
    }
    std::string revision;
    try {
        std::vector<uint8_t> bytes; Json request;
        if (!ReadDisk(g_reload_request, bytes, 1024 * 1024 + 1) || bytes.size() > 1024 * 1024 || !ParseJson(std::string(bytes.begin(), bytes.end()), request)) return;
        revision = request.Str("revision");
        if (revision == g_reload_revision) return;
        if (revision.size() != 32 || revision.find_first_not_of("0123456789abcdef") != std::string::npos) return;
        g_reload_revision = revision;
        fs::path target;
        if (request.Num("format", 0) != 1 || !SafeGeneration(g_reload_request, revision, request.Str("mod"), target)) {
            Acknowledge(revision, "rejected", "Unsafe generation path or unsupported reload protocol"); return;
        }
        auto current = std::find_if(g_mods.begin(), g_mods.end(), [&](auto& item) { return fs::equivalent(item->path, g_reload_mod); });
        if (current == g_mods.end()) { Acknowledge(revision, "rejected", "The initial Workshop mod was not mounted"); return; }
        if (fs::file_size(target / "mod.json") > 1024 * 1024) { Acknowledge(revision, "rejected", "Oversized mod manifest"); return; }
        auto candidate = OpenMod(target, g_reload_sha);
        if (!candidate || candidate->id != (*current)->id || !candidate->meta.Strings("conflicts").empty() ||
            (candidate->meta.Get("requires") && !candidate->meta.Get("requires")->items.empty())) {
            Acknowledge(revision, "rejected", "Invalid Workshop mod, changed mod id, or unsupported dependency/conflict metadata"); return;
        }
        std::map<std::string, std::vector<uint8_t>> before, after;
        if (!ReadPayloads(**current, before) || !ReadPayloads(*candidate, after)) { Acknowledge(revision, "rejected", "Unreadable, oversized, duplicate or unsafe mod payload"); return; }
        std::set<std::string> changed;
        std::set<std::string> topology;
        for (auto& [path, data] : before) if (!after.count(path)) topology.insert(path);
        for (auto& [path, data] : after) if (!before.count(path)) topology.insert(path);
        for (auto& [path, data] : before) if (!after.count(path) || after[path] != data) changed.insert(path);
        for (auto& [path, data] : after) if (!before.count(path) || before[path] != data) changed.insert(path);
        for (const char* field : {"remove", "refpack"}) {
            auto old = (*current)->meta.Strings(field), next = candidate->meta.Strings(field);
            for (auto& values : {old, next}) for (auto& value : values) {
                auto path = fs::u8path(value);
                if (value.empty() || path.is_absolute() || path.generic_string() != value || value.find_first_of(":*?\"<>|\\") != std::string::npos) { Acknowledge(revision, "rejected", "Unsafe mod metadata path"); return; }
                for (auto& part : path) if (part == "." || part == "..") { Acknowledge(revision, "rejected", "Unsafe mod metadata traversal"); return; }
            }
            if (old != next) {
                for (auto& path : old) { changed.insert(Lower(path)); if (!std::strcmp(field,"remove")) topology.insert(Lower(path)); }
                for (auto& path : next) { changed.insert(Lower(path)); if (!std::strcmp(field,"remove")) topology.insert(Lower(path)); }
            }
        }
        std::vector<LiveTexture> live; std::set<std::string> livePacks; std::string liveError;
        bool validLive = g_stage_live && ReadLivePlan(target, live, livePacks, liveError);
        if (g_stage_live && !liveError.empty()) { Acknowledge(revision, "restart-required", liveError); return; }
        if (!validLive) livePacks.clear();
        for (auto& path : topology) if (!LiveCovered(path, livePacks)) for (auto& listed : g_listed) if (listed.empty() || Related(path, listed)) {
            Acknowledge(revision, "restart-required", path + " changes a directory already enumerated by the game; the entire old layer remains active"); return;
        }
        for (auto& path : changed) if (!LiveCovered(path, livePacks)) for (auto& opened : g_opened) if (Related(path, opened)) {
            Acknowledge(revision, "restart-required", path + " belongs to an asset/archive already opened by the game; the entire old layer remains active"); return;
        }
        if (validLive && !g_stage_live(live, revision, liveError)) { Acknowledge(revision, "rejected", liveError); return; }
        // Readers hold the same recursive lock for complete open/read transactions.
        // The changed paths have never been opened, so there are no guest/GPU caches to invalidate.
        auto old = std::move(*current); *current = std::move(candidate);
        auto oldFiles = std::move(g_files); auto oldDirs = std::move(g_dirs); auto oldHidden = std::move(g_hidden);
        auto oldOwned = std::move(g_owned); auto oldPacked = std::move(g_packed);
        try { Reindex(); }
        catch (...) {
            *current = std::move(old); g_files = std::move(oldFiles); g_dirs = std::move(oldDirs);
            g_hidden = std::move(oldHidden); g_owned = std::move(oldOwned); g_packed = std::move(oldPacked); throw;
        }
        g_reload_mod = target;
        if (validLive) { g_commit_live(revision); g_pending_live = revision; }
        else Acknowledge(revision, "applied", std::to_string(changed.size()) + " unopened file(s) reloaded; already-open nontexture assets require restarting");
    } catch (const std::exception& exception) {
        if (!revision.empty()) Acknowledge(revision, "rejected", exception.what());
        else LOGE("mods: Workshop reload: %s", exception.what());
    }
}
