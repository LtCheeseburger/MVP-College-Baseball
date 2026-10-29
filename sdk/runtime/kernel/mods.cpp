#include "kernel/mods.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "core/log.h"
#include "kernel/inflate.h"

namespace fs = std::filesystem;

namespace kernel::mods {

namespace {

constexpr int kFormat = 1;  // docs/MVPMOD.md
constexpr int kLayout = 1;  // docs/LOOSE_ASSETS.md

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// mod.json and mods.json: a small JSON reader, enough for what the launcher and Workshop write
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json* Get(const char* key) const {
        for (auto& [k, v] : fields)
            if (k == key) return &v;
        return nullptr;
    }
    std::string Str(const char* key, const std::string& fallback = {}) const {
        const Json* v = Get(key);
        return v && v->type == String ? v->s : fallback;
    }
    double Num(const char* key, double fallback) const {
        const Json* v = Get(key);
        return v && v->type == Number ? v->num : fallback;
    }
    bool Flag(const char* key) const {
        const Json* v = Get(key);
        return v && v->type == Bool && v->b;
    }
    std::vector<std::string> Strings(const char* key) const {
        std::vector<std::string> out;
        if (const Json* v = Get(key); v && v->type == Array)
            for (const Json& i : v->items)
                if (i.type == String) out.push_back(i.s);
        return out;
    }
};

struct Parser {
    const std::string& t;
    size_t p = 0;
    bool bad = false;

    void Space() {
        while (p < t.size() && std::isspace(uint8_t(t[p]))) p++;
    }
    bool Eat(char c) {
        Space();
        if (p < t.size() && t[p] == c) {
            p++;
            return true;
        }
        return false;
    }
    std::string Text() {
        std::string out;
        if (!Eat('"')) {
            bad = true;
            return out;
        }
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= t.size()) break;
            c = t[p++];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned v = p + 4 <= t.size() ? unsigned(std::strtoul(t.substr(p, 4).c_str(), nullptr, 16)) : 0;
                    p += 4;
                    if (v < 0x80) {
                        out += char(v);
                    } else if (v < 0x800) {
                        out += char(0xC0 | v >> 6);
                        out += char(0x80 | (v & 0x3F));
                    } else {
                        out += char(0xE0 | v >> 12);
                        out += char(0x80 | (v >> 6 & 0x3F));
                        out += char(0x80 | (v & 0x3F));
                    }
                    break;
                }
                default: out += c;
            }
        }
        if (p >= t.size()) bad = true;
        p++;
        return out;
    }
    Json Value(int depth = 0) {
        Json v;
        Space();
        if (p >= t.size() || depth > 32) {
            bad = true;
            return v;
        }
        char c = t[p];
        if (c == '{') {
            p++;
            v.type = Json::Object;
            if (Eat('}')) return v;
            do {
                std::string k = Text();
                if (!Eat(':')) bad = true;
                v.fields.push_back({k, Value(depth + 1)});
            } while (!bad && Eat(','));
            if (!Eat('}')) bad = true;
        } else if (c == '[') {
            p++;
            v.type = Json::Array;
            if (Eat(']')) return v;
            do v.items.push_back(Value(depth + 1));
            while (!bad && Eat(','));
            if (!Eat(']')) bad = true;
        } else if (c == '"') {
            v.type = Json::String;
            v.s = Text();
        } else if (t.compare(p, 4, "true") == 0 || t.compare(p, 5, "false") == 0) {
            v.type = Json::Bool;
            v.b = t[p] == 't';
            p += v.b ? 4 : 5;
        } else if (t.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            char* end = nullptr;
            v.type = Json::Number;
            v.num = std::strtod(t.c_str() + p, &end);
            if (end == t.c_str() + p) bad = true;
            p = size_t(end - t.c_str());
        }
        return v;
    }
};

bool ParseJson(const std::string& text, Json& out) {
    Parser ps{text};
    out = ps.Value();
    ps.Space();
    return !ps.bad && ps.p == text.size() && out.type == Json::Object;
}

bool ReadDisk(const fs::path& p, std::vector<uint8_t>& out, size_t limit) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::error_code ec;
    uint64_t size = fs::file_size(p, ec);
    if (ec) return false;
    out.resize(size_t(std::min<uint64_t>(size, limit)));
    in.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    return in.gcount() == std::streamsize(out.size());
}

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

struct ZipEntry {
    uint32_t local = 0, csize = 0, usize = 0;
    uint16_t method = 0;
};

struct Mod {
    fs::path path;  // the .mvpmod, or the folder holding mod.json
    bool zip = false;
    fs::file_time_type time{};
    std::string id, name, version;
    bool affects_saves = false;
    Json meta;
    std::map<std::string, ZipEntry> entries;  // zip: name as stored -> entry
    std::vector<std::string> files;           // names under files/, as stored ('/' separated)

    bool OpenZip() {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        in.seekg(0, std::ios::end);
        uint64_t size = uint64_t(in.tellg());
        size_t tail = size_t(std::min<uint64_t>(size, 65535 + 22));
        std::vector<uint8_t> t(tail);
        in.seekg(std::streamoff(size - tail));
        in.read(reinterpret_cast<char*>(t.data()), std::streamsize(tail));
        size_t eocd = SIZE_MAX;
        for (size_t i = tail >= 22 ? tail - 22 : 0; i != SIZE_MAX && tail >= 22; i--)
            if (U32(&t[i]) == 0x06054B50) {
                eocd = i;
                break;
            }
        if (eocd == SIZE_MAX) return false;
        uint32_t count = U16(&t[eocd + 10]), cd_size = U32(&t[eocd + 12]), cd_off = U32(&t[eocd + 16]);
        if (count == 0xFFFF || cd_off == 0xFFFFFFFF) {
            LOGE("mods: %s is a zip64 archive, not supported", path.filename().string().c_str());
            return false;
        }
        if (uint64_t(cd_off) + cd_size > size) return false;
        std::vector<uint8_t> cd(cd_size);
        in.seekg(cd_off);
        in.read(reinterpret_cast<char*>(cd.data()), std::streamsize(cd_size));
        size_t at = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (at + 46 > cd.size() || U32(&cd[at]) != 0x02014B50) return false;
            uint16_t flags = U16(&cd[at + 8]), nlen = U16(&cd[at + 28]), elen = U16(&cd[at + 30]), clen = U16(&cd[at + 32]);
            if (at + 46 + nlen > cd.size()) return false;
            ZipEntry e;
            e.method = U16(&cd[at + 10]);
            e.csize = U32(&cd[at + 20]);
            e.usize = U32(&cd[at + 24]);
            e.local = U32(&cd[at + 42]);
            std::string n(reinterpret_cast<const char*>(&cd[at + 46]), nlen);
            std::replace(n.begin(), n.end(), '\\', '/');
            at += 46 + nlen + elen + clen;
            if (n.empty() || n.back() == '/') continue;
            if (flags & 1) {
                LOGW("mods: %s: %s is encrypted, skipped", path.filename().string().c_str(), n.c_str());
                continue;
            }
            if (e.method != 0 && e.method != 8) {
                LOGW("mods: %s: %s uses zip method %u (only stored and deflate), skipped",
                     path.filename().string().c_str(), n.c_str(), e.method);
                continue;
            }
            entries[n] = e;
        }
        for (auto& [n, e] : entries)
            if (n.compare(0, 6, "files/") == 0 && n.size() > 6) files.push_back(n);
        return true;
    }

    void ListFolder() {
        std::error_code ec;
        fs::path root = path / "files";
        if (!fs::is_directory(root, ec)) return;
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            files.push_back("files/" + fs::relative(it->path(), root, ec).generic_string());
        }
    }

    // name as stored ("files/...", "mod.json")
    bool Read(const std::string& n, std::vector<uint8_t>& out, size_t limit = SIZE_MAX) const {
        if (!zip) return ReadDisk(path / fs::u8path(n), out, limit);
        auto it = entries.find(n);
        if (it == entries.end()) return false;
        const ZipEntry& e = it->second;
        std::ifstream in(path, std::ios::binary);
        uint8_t h[30];
        in.seekg(e.local);
        if (!in.read(reinterpret_cast<char*>(h), 30) || U32(h) != 0x04034B50) return false;
        in.seekg(std::streamoff(e.local) + 30 + U16(h + 26) + U16(h + 28));
        std::vector<uint8_t> raw(e.csize);
        if (!in.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()))) return false;
        if (e.method == 0) {
            out = std::move(raw);
        } else if (!Inflate(raw.data(), raw.size(), out, e.usize)) {
            LOGE("mods: %s: %s is damaged", path.filename().string().c_str(), n.c_str());
            return false;
        }
        if (out.size() > limit) out.resize(limit);
        return true;
    }

    uint64_t SizeOf(const std::string& n) const {
        if (zip) {
            auto it = entries.find(n);
            return it == entries.end() ? 0 : it->second.usize;
        }
        std::error_code ec;
        uint64_t s = fs::file_size(path / fs::u8path(n), ec);
        return ec ? 0 : s;
    }
};

struct Hit {
    const Mod* mod;
    std::string entry;  // name in the mod
    uint64_t size;
};

std::string g_game;  // lower case, '/' separated, no trailing slash
std::vector<std::unique_ptr<Mod>> g_mods;
std::unordered_map<std::string, Hit> g_files;                         // rel -> the winning mod file
std::unordered_map<std::string, std::map<std::string, Child>> g_dirs; // rel dir -> children mods add, by lower name
std::unordered_set<std::string> g_hidden;                             // rel paths removed by a mod
std::unordered_set<std::string> g_owned;                              // rel dirs a mod replaces completely (xsh.meta)
std::unordered_set<std::string> g_packed;                             // rel paths to RefPack (mod.json "refpack")

// the path relative to the game folder, lower case with '/'; false outside it or with no mods
bool Rel(const fs::path& p, std::string& rel) {
    if (g_mods.empty()) return false;
    std::string s = Lower(p.lexically_normal().generic_string());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    if (s.size() < g_game.size() || s.compare(0, g_game.size(), g_game) != 0) return false;
    if (s.size() == g_game.size()) {
        rel.clear();
        return true;
    }
    if (s[g_game.size()] != '/') return false;
    rel = s.substr(g_game.size() + 1);
    return true;
}

std::string Parent(const std::string& rel) {
    size_t s = rel.rfind('/');
    return s == std::string::npos ? std::string() : rel.substr(0, s);
}

// a disk path a mod hides: removed, inside a folder a mod replaces, or under a file a mod puts in a folder's place
bool Hidden(const std::string& rel) {
    if (g_hidden.count(rel)) return true;
    for (size_t s = rel.find('/'); s != std::string::npos; s = rel.find('/', s + 1)) {
        std::string up = rel.substr(0, s);
        if (g_hidden.count(up) || g_owned.count(up) || g_files.count(up)) return true;
    }
    return false;
}

bool DiskStat(const fs::path& p, Node& out) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    if (ec || !fs::exists(st)) return false;
    out.dir = fs::is_directory(st);
    out.size = out.dir ? 0 : fs::file_size(p, ec);
    out.time = fs::last_write_time(p, ec);
    return true;
}

// semver "1.2.3" against ">=1.0.0" and the like; a bare version means at least that one
bool VersionOk(const std::string& have, const std::string& want) {
    size_t i = 0;
    while (i < want.size() && std::strchr("<>= ", want[i])) i++;
    std::string op = want.substr(0, i);
    op.erase(std::remove(op.begin(), op.end(), ' '), op.end());
    auto parts = [](const std::string& v) {
        int a[3] = {};
        std::sscanf(v.c_str(), "%d.%d.%d", &a[0], &a[1], &a[2]);
        return std::vector<int>{a[0], a[1], a[2]};
    };
    auto h = parts(have), w = parts(want.substr(i));
    if (op.empty() || op == ">=") return h >= w;
    if (op == ">") return h > w;
    if (op == "=" || op == "==") return h == w;
    if (op == "<=") return h <= w;
    if (op == "<") return h < w;
    return false;
}

std::unique_ptr<Mod> OpenMod(const fs::path& path, const std::string& xbe_sha1) {
    auto m = std::make_unique<Mod>();
    m->path = path;
    std::error_code ec;
    m->zip = !fs::is_directory(path, ec);
    m->time = fs::last_write_time(m->zip ? path : path / "mod.json", ec);
    std::string label = path.filename().string();
    if (m->zip ? !m->OpenZip() : !fs::exists(path / "mod.json", ec)) {
        LOGE("mods: %s is not a readable mod, left off", label.c_str());
        return nullptr;
    }
    std::vector<uint8_t> text;
    if (!m->Read("mod.json", text) || !ParseJson(std::string(text.begin(), text.end()), m->meta)) {
        LOGE("mods: %s has no valid mod.json, left off", label.c_str());
        return nullptr;
    }
    const Json& j = m->meta;
    m->id = j.Str("id");
    m->name = j.Str("name", m->id);
    m->version = j.Str("version", "0.0.0");
    m->affects_saves = j.Flag("affectsSaves");
    const Json* game = j.Get("game");
    std::string sha = game ? Lower(game->Str("xbeSha1")) : std::string();
    const char* why = nullptr;
    if (int(j.Num("format", 0)) > kFormat) why = "needs a newer port (mod format)";
    else if (int(j.Num("format", 0)) < 1) why = "has no format number";
    else if (m->id.empty() || m->id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
        why = "has a bad id (lower case letters, digits and '-')";
    else if (int(j.Num("layout", 0)) != kLayout) why = "was made for another install layout";
    else if (sha != Lower(xbe_sha1)) why = "was made for another release of the game";
    if (why) {
        LOGE("mods: %s %s, left off", label.c_str(), why);
        return nullptr;
    }
    if (!m->zip) m->ListFolder();
    return m;
}

void AddFile(const Mod& m, const std::string& entry) {
    std::string spelled = entry.substr(6);  // after "files/"
    std::string rel = Lower(spelled);
    if (rel.find("..") != std::string::npos || rel.find(':') != std::string::npos) {
        LOGW("mods: %s: bad path %s, skipped", m.id.c_str(), spelled.c_str());
        return;
    }
    fs::path disk = fs::u8path(g_game) / fs::u8path(rel);
    std::error_code ec;
    std::string leaf = rel.substr(rel.rfind('/') + 1);
    // loose installs only: a mod ships members and DDS images, never a packed archive or a whole texture file
    bool packed_big = leaf.size() > 4 && leaf.compare(leaf.size() - 4, 4, ".big") == 0;
    if (packed_big || (fs::is_directory(disk, ec) && fs::exists(disk / "xsh.meta", ec))) {
        LOGW("mods: %s: %s is packed; ship its members or DDS images instead, skipped", m.id.c_str(), spelled.c_str());
        return;
    }
    if (leaf == "_big.json" && fs::exists(disk, ec)) {
        LOGW("mods: %s: %s belongs to the game (order and packing of an existing archive), skipped", m.id.c_str(),
             spelled.c_str());
        return;
    }
    if (leaf == "xsh.meta" && fs::exists(disk, ec) && !g_owned.count(Parent(rel))) {
        g_owned.insert(Parent(rel));
        LOGR("mods: %s replaces the texture %s as a whole", m.id.c_str(), Parent(spelled).c_str());
    }
    if (auto it = g_files.find(rel); it != g_files.end())
        LOGR("mods: %s from %s (over %s)", spelled.c_str(), m.id.c_str(), it->second.mod->id.c_str());
    g_hidden.erase(rel);
    g_files[rel] = {&m, entry, m.SizeOf(entry)};
    // every folder above it exists, and lists it
    std::string child = rel, child_spelled = spelled;
    bool dir = false;
    while (!child.empty()) {
        std::string parent = Parent(child);
        size_t cut = child_spelled.rfind('/');
        std::string name = cut == std::string::npos ? child_spelled : child_spelled.substr(cut + 1);
        auto& kids = g_dirs[parent];
        auto [it, added] = kids.try_emplace(Lower(name), Child{name, dir});
        if (!dir) it->second.dir = false;  // a file put in a folder's place
        if (!added && dir) break;
        child = parent;
        child_spelled = cut == std::string::npos ? std::string() : child_spelled.substr(0, cut);
        dir = true;
    }
}

void Remove(const Mod& m, const std::string& path) {
    std::string rel = Lower(path);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    if (rel.find("_big/") == std::string::npos) {
        LOGW("mods: %s: remove %s ignored, only archive members can be removed", m.id.c_str(), path.c_str());
        return;
    }
    g_files.erase(rel);
    if (auto it = g_dirs.find(Parent(rel)); it != g_dirs.end()) it->second.erase(rel.substr(rel.rfind('/') + 1));
    g_hidden.insert(rel);
}

}  // namespace

void Init(const fs::path& game_dir, const fs::path& mods_dir, const std::vector<fs::path>& extra, bool none,
          const std::string& xbe_sha1) {
    g_game = Lower(fs::absolute(game_dir).lexically_normal().generic_string());
    while (g_game.size() > 1 && g_game.back() == '/') g_game.pop_back();

    // mods.json order first; anything else found in the folder is off until the launcher turns it on
    std::vector<fs::path> wanted;
    std::error_code ec;
    if (!none && fs::is_directory(mods_dir, ec)) {
        std::map<std::string, fs::path> found;  // id -> path
        for (auto& e : fs::directory_iterator(mods_dir, ec)) {
            bool folder = e.is_directory(ec) && fs::exists(e.path() / "mod.json", ec);
            bool file = e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".mvpmod";
            if (!folder && !file) continue;
            if (auto m = OpenMod(e.path(), xbe_sha1)) {
                if (found.count(m->id)) LOGE("mods: %s: id %s is used twice, left off", e.path().filename().string().c_str(), m->id.c_str());
                else found[m->id] = e.path();
            }
        }
        std::vector<uint8_t> text;
        Json list;
        std::set<std::string> listed;
        if (ReadDisk(mods_dir / "mods.json", text, SIZE_MAX) && ParseJson(std::string(text.begin(), text.end()), list)) {
            if (const Json* order = list.Get("order"); order && order->type == Json::Array)
                for (const Json& o : order->items) {
                    std::string id = o.Str("id");
                    listed.insert(id);
                    if (!o.Flag("enabled")) continue;
                    if (auto it = found.find(id); it != found.end()) wanted.push_back(it->second);
                    else LOGW("mods: %s is on in mods.json but not in the mods folder", id.c_str());
                }
        } else if (!found.empty()) {
            LOGW("mods: no readable mods.json in %s, every mod is off", mods_dir.string().c_str());
        }
        for (auto& [id, p] : found)
            if (!listed.count(id)) LOGR("mods: %s found, off until it's enabled", id.c_str());
    }
    for (const fs::path& p : extra) wanted.push_back(p);

    std::set<std::string> ids;
    for (const fs::path& p : wanted) {
        auto m = OpenMod(p, xbe_sha1);
        if (!m) continue;
        if (ids.count(m->id)) {
            LOGE("mods: %s: id %s is already on, left off", p.filename().string().c_str(), m->id.c_str());
            continue;
        }
        ids.insert(m->id);
        g_mods.push_back(std::move(m));
    }
    // declared conflicts drop the later mod; then missing dependencies, until nothing changes
    for (size_t i = 0; i < g_mods.size(); i++) {
        for (const std::string& c : g_mods[i]->meta.Strings("conflicts")) {
            auto earlier = std::find_if(g_mods.begin(), g_mods.begin() + i, [&](auto& m) { return m->id == c; });
            auto later = std::find_if(g_mods.begin() + i + 1, g_mods.end(), [&](auto& m) { return m->id == c; });
            if (earlier != g_mods.begin() + i) {
                LOGE("mods: %s conflicts with %s, left off", g_mods[i]->id.c_str(), c.c_str());
                g_mods.erase(g_mods.begin() + i--);
                break;
            }
            if (later != g_mods.end()) {
                LOGE("mods: %s conflicts with %s, left off", (*later)->id.c_str(), g_mods[i]->id.c_str());
                g_mods.erase(later);
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < g_mods.size() && !changed; i++) {
            const Json* req = g_mods[i]->meta.Get("requires");
            if (!req || req->type != Json::Array) continue;
            for (const Json& r : req->items) {
                std::string id = r.Str("id"), want = r.Str("version");
                auto dep = std::find_if(g_mods.begin(), g_mods.end(), [&](auto& m) { return m->id == id; });
                if (dep != g_mods.end() && (want.empty() || VersionOk((*dep)->version, want))) continue;
                LOGE("mods: %s needs %s %s, left off", g_mods[i]->id.c_str(), id.c_str(),
                     dep == g_mods.end() ? "(not on)" : ("(" + (*dep)->version + " is on)").c_str());
                g_mods.erase(g_mods.begin() + i);
                changed = true;
                break;
            }
        }
    }

    for (auto& m : g_mods) {
        for (const std::string& f : m->files) AddFile(*m, f);
        for (const std::string& r : m->meta.Strings("remove")) Remove(*m, r);
        for (std::string r : m->meta.Strings("refpack")) {
            std::replace(r.begin(), r.end(), '\\', '/');
            g_packed.insert(Lower(r));
        }
    }
    if (g_mods.empty()) {
        LOGR("mods: none on%s", none ? " (--no-mods)" : "");
        return;
    }
    std::string list;
    for (auto& m : g_mods) list += (list.empty() ? "" : ", ") + m->id + " " + m->version;
    LOGR("mods: %zu on - %s (%zu files)", g_mods.size(), list.c_str(), g_files.size());
}

bool Active() { return !g_mods.empty(); }

bool Stat(const fs::path& p, Node& out) {
    std::string rel;
    if (!Rel(p, rel)) return DiskStat(p, out);
    if (auto it = g_files.find(rel); it != g_files.end()) {
        out.dir = false;
        out.size = it->second.size;
        out.time = it->second.mod->time;
        return true;
    }
    if (!rel.empty() && Hidden(rel)) return false;
    if (DiskStat(p, out)) return true;
    if (g_dirs.count(rel)) {
        out = Node{true, 0, g_mods.back()->time};
        return true;
    }
    return false;
}

bool Exists(const fs::path& p) {
    Node n;
    return Stat(p, n);
}

bool IsFile(const fs::path& p) {
    Node n;
    return Stat(p, n) && !n.dir;
}

bool IsDir(const fs::path& p) {
    Node n;
    return Stat(p, n) && n.dir;
}

std::vector<Child> List(const fs::path& dir) {
    std::map<std::string, Child> all;
    std::string rel;
    bool game = Rel(dir, rel);
    if (!game || rel.empty() || (!Hidden(rel) && !g_owned.count(rel) && !g_files.count(rel))) {
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::string name = e.path().filename().string();
            if (game && g_hidden.count(rel.empty() ? Lower(name) : rel + "/" + Lower(name))) continue;
            all[Lower(name)] = {name, e.is_directory(ec)};
        }
    }
    if (game)
        if (auto it = g_dirs.find(rel); it != g_dirs.end())
            for (auto& [low, c] : it->second) {
                auto [at, added] = all.try_emplace(low, c);
                if (!added) at->second.dir = c.dir && at->second.dir;  // the disk's spelling, a mod file over a folder
            }
    std::vector<Child> out;
    out.reserve(all.size());
    for (auto& [low, c] : all) out.push_back(c);
    return out;
}

fs::path Find(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    std::string rel;
    if (!Rel(dir, rel)) {
        if (fs::exists(dir / name, ec)) return dir / name;
        std::string want = Lower(name);
        for (auto& e : fs::directory_iterator(dir, ec))
            if (Lower(e.path().filename().string()) == want) return e.path();
        return {};
    }
    if (Exists(dir / name)) return dir / name;
    std::string want = Lower(name);
    for (const Child& c : List(dir))
        if (Lower(c.name) == want) return dir / fs::u8path(c.name);
    return {};
}

bool Load(const fs::path& p, std::vector<uint8_t>& out, size_t limit) {
    std::string rel;
    if (!Rel(p, rel)) return ReadDisk(p, out, limit);
    if (auto it = g_files.find(rel); it != g_files.end()) return it->second.mod->Read(it->second.entry, out, limit);
    if (Hidden(rel)) return false;
    return ReadDisk(p, out, limit);
}

bool FromMod(const fs::path& p) {
    std::string rel;
    return Rel(p, rel) && g_files.count(rel);
}

bool Packed(const fs::path& p) {
    std::string rel;
    return Rel(p, rel) && g_packed.count(rel);
}

}  // namespace kernel::mods
