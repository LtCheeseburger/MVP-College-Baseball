#include "kernel/modvfs.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "kernel/mods.h"
#include "kernel/xsh.h"

namespace fs = std::filesystem;

namespace kernel::modvfs {

namespace {

// a change to one BIG entry: a game patch, or (archive-mode installs) a mod's member
struct Replacement {
    std::string archive;  // upper case, backslashes, relative to the game folder
    std::string entry;    // upper case
    std::vector<uint8_t> bytes;
    Patch patch = nullptr;  // the game's own edit of the original entry, bytes unused
};

std::string Upper(std::string s) {
    for (char& c : s) c = c == '/' ? '\\' : char(std::toupper(uint8_t(c)));
    return s;
}

uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
void PutBE32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

// AddPatch runs from static constructors, before logging starts
std::vector<Replacement>& Patches() {
    static std::vector<Replacement> list;
    return list;
}

// RefPack (EA's LZ77) as the archives use it
std::vector<uint8_t> Unpack(const uint8_t* d, size_t size) {
    std::vector<uint8_t> out;
    size_t p = 2 + (d[0] & 0x80 ? 4 : 3) * (d[0] & 1 ? 2 : 1);
    while (p < size) {
        uint8_t b0 = d[p];
        size_t lit, n = 0, off = 0;
        if (b0 < 0x80) {
            lit = b0 & 3;
            n = ((b0 & 0x1C) >> 2) + 3;
            off = ((b0 & 0x60) << 3) + d[p + 1] + 1;
            p += 2;
        } else if (b0 < 0xC0) {
            lit = d[p + 1] >> 6;
            n = (b0 & 0x3F) + 4;
            off = ((d[p + 1] & 0x3F) << 8) + d[p + 2] + 1;
            p += 3;
        } else if (b0 < 0xE0) {
            lit = b0 & 3;
            n = ((b0 & 0x0C) << 6) + d[p + 3] + 5;
            off = ((b0 & 0x10) << 12) + (d[p + 1] << 8) + d[p + 2] + 1;
            p += 4;
        } else if (b0 < 0xFC) {
            lit = ((b0 & 0x1F) << 2) + 4;
            p += 1;
        } else {
            lit = b0 & 3;
            p += 1;
            if (p + lit > size) break;
            out.insert(out.end(), d + p, d + p + lit);
            break;
        }
        if (p + lit > size || off > out.size() + lit) break;
        out.insert(out.end(), d + p, d + p + lit);
        p += lit;
        for (size_t i = 0; i < n; i++) out.push_back(out[out.size() - off]);
    }
    return out;
}

// RefPack made of literal runs only: what the game reads, without a compressor
std::vector<uint8_t> PackLiteral(const std::vector<uint8_t>& u) {
    std::vector<uint8_t> out = {0x10, 0xFB, uint8_t(u.size() >> 16), uint8_t(u.size() >> 8), uint8_t(u.size())};
    size_t p = 0;
    while (u.size() - p > 3) {
        size_t n = std::min<size_t>(112, (u.size() - p) / 4 * 4);
        out.push_back(uint8_t(0xE0 + ((n - 4) >> 2)));
        out.insert(out.end(), u.begin() + p, u.begin() + p + n);
        p += n;
    }
    out.push_back(uint8_t(0xFC | (u.size() - p)));
    out.insert(out.end(), u.begin() + p, u.end());
    return out;
}

// RefPack with matches (greedy, hash chains over a 128 KB window): loose members go back to the game at about the
// size the disc had, which matters for archives it loads whole into its heap
std::vector<uint8_t> Pack(const std::vector<uint8_t>& u) {
    constexpr size_t kWindow = 131072, kMaxLen = 1028, kChain = 16, kGood = 64, kHash = 1 << 16;
    std::vector<uint8_t> out = {0x10, 0xFB, uint8_t(u.size() >> 16), uint8_t(u.size() >> 8), uint8_t(u.size())};
    std::vector<int32_t> head(kHash, -1), prev(u.size(), -1);
    auto hash = [&](size_t i) { return (uint32_t(u[i]) << 16 ^ uint32_t(u[i + 1]) << 8 ^ u[i + 2]) * 2654435761u >> 16; };
    auto insert = [&](size_t i) {
        if (i + 2 >= u.size()) return;
        uint32_t h = hash(i);
        prev[i] = head[h];
        head[h] = int32_t(i);
    };
    size_t lit = 0, p = 0;  // literals start at lit
    while (p < u.size()) {
        size_t best = 0, best_off = 0;
        if (p + 2 < u.size()) {
            size_t limit = std::min(kMaxLen, u.size() - p);
            int32_t c = head[hash(p)];
            for (size_t n = 0; c >= 0 && n < kChain && p - size_t(c) <= kWindow; n++, c = prev[c]) {
                size_t len = 0;
                while (len < limit && u[c + len] == u[p + len]) len++;
                size_t off = p - size_t(c);
                // each form's reach: 3+ within 1 KB, 4+ within 16 KB, 5+ within 128 KB
                bool fits = off <= 1024 ? len >= 3 : off <= 16384 ? len >= 4 : len >= 5;
                if (fits && len > best) {
                    best = len;
                    best_off = off;
                    if (len >= kGood) break;
                }
            }
        }
        if (!best) {
            insert(p++);
            continue;
        }
        // literals before the match: runs of 4..112, the last 0..3 ride in the match
        while (p - lit > 3) {
            size_t n = std::min<size_t>(112, (p - lit) / 4 * 4);
            out.push_back(uint8_t(0xE0 + ((n - 4) >> 2)));
            out.insert(out.end(), u.begin() + lit, u.begin() + lit + n);
            lit += n;
        }
        size_t l = p - lit, o = best_off - 1, n = best;
        if (best_off <= 1024 && n <= 10) {
            out.push_back(uint8_t((o >> 3 & 0x60) | (n - 3) << 2 | l));
            out.push_back(uint8_t(o));
        } else if (best_off <= 16384 && n <= 67) {
            out.push_back(uint8_t(0x80 | (n - 4)));
            out.push_back(uint8_t(l << 6 | o >> 8));
            out.push_back(uint8_t(o));
        } else {
            out.push_back(uint8_t(0xC0 | (o >> 16 & 1) << 4 | ((n - 5) >> 8 & 3) << 2 | l));
            out.push_back(uint8_t(o >> 8));
            out.push_back(uint8_t(o));
            out.push_back(uint8_t(n - 5));
        }
        out.insert(out.end(), u.begin() + lit, u.begin() + p);
        for (size_t i = 0; i < n; i++) insert(p + i);
        p += n;
        lit = p;
    }
    while (u.size() - lit > 3) {
        size_t n = std::min<size_t>(112, (u.size() - lit) / 4 * 4);
        out.push_back(uint8_t(0xE0 + ((n - 4) >> 2)));
        out.insert(out.end(), u.begin() + lit, u.begin() + lit + n);
        lit += n;
    }
    out.push_back(uint8_t(0xFC | (u.size() - lit)));
    out.insert(out.end(), u.begin() + lit, u.end());
    return out;
}

// the game's own patches; mods come from kernel::mods
const std::vector<Replacement>& Replacements() { return Patches(); }

// the original archive, its header patched to point replaced entries at new payloads appended after the old end
class BigView : public Virtual {
public:
    uint64_t Size() const override { return orig_size_ + tail_.size(); }

    size_t Read(uint64_t pos, void* dst, size_t len) override {
        auto* out = static_cast<uint8_t*>(dst);
        size_t done = 0;
        uint64_t total = Size();
        while (done < len && pos < total) {
            size_t n;
            if (pos < head_.size()) {
                n = std::min<uint64_t>(len - done, head_.size() - pos);
                std::memcpy(out + done, head_.data() + pos, n);
            } else if (pos < orig_size_) {
                n = std::min<uint64_t>(len - done, orig_size_ - pos);
                std::lock_guard<std::mutex> lock(lock_);
                _fseeki64(fp_, int64_t(pos), SEEK_SET);
                n = std::fread(out + done, 1, n, fp_);
                if (!n) break;
            } else {
                if (reads_++ < 4) LOGR("mods: read of replaced data at %llu (%zu bytes)", (unsigned long long)pos, len - done);
                n = std::min<uint64_t>(len - done, total - pos);
                std::memcpy(out + done, tail_.data() + (pos - orig_size_), n);
            }
            done += n;
            pos += n;
        }
        return done;
    }

    ~BigView() override {
        if (fp_) std::fclose(fp_);
    }

    bool Build(const fs::path& host, const std::vector<const Replacement*>& reps) {
        fp_ = std::fopen(host.string().c_str(), "rb");
        if (!fp_) return false;
        _fseeki64(fp_, 0, SEEK_END);
        orig_size_ = uint64_t(_ftelli64(fp_));
        uint8_t hdr[16];
        _fseeki64(fp_, 0, SEEK_SET);
        if (std::fread(hdr, 1, 16, fp_) != 16 || std::memcmp(hdr, "BIGF", 4) != 0) return false;
        uint32_t count = BE32(hdr + 8), header_size = BE32(hdr + 12);
        if (header_size < 16 || header_size > orig_size_) return false;
        head_.resize(header_size);
        _fseeki64(fp_, 0, SEEK_SET);
        if (std::fread(head_.data(), 1, header_size, fp_) != header_size) return false;

        size_t p = 16;
        for (uint32_t i = 0; i < count && p + 8 < head_.size(); i++) {
            size_t dir_pos = p;
            p += 8;
            size_t e = p;
            while (e < head_.size() && head_[e]) e++;
            std::string name = Upper(std::string(reinterpret_cast<char*>(head_.data()) + p, e - p));
            p = e + 1;
            for (const Replacement* r : reps) {
                if (r->entry != name) continue;
                std::vector<uint8_t> patched;
                const std::vector<uint8_t>* bytes = &r->bytes;
                if (r->patch) {
                    if (!PatchEntry(BE32(&head_[dir_pos]), BE32(&head_[dir_pos + 4]), r->patch, patched)) {
                        LOGE("mods: %s entry %s left as it was, the patch did not apply",
                             host.filename().string().c_str(), name.c_str());
                        continue;
                    }
                    bytes = &patched;
                }
                while ((orig_size_ + tail_.size()) % 128) tail_.push_back(0);  // keep entries 128 byte aligned
                PutBE32(&head_[dir_pos], uint32_t(orig_size_ + tail_.size()));
                PutBE32(&head_[dir_pos + 4], uint32_t(bytes->size()));
                tail_.insert(tail_.end(), bytes->begin(), bytes->end());
                LOGR("mods: %s entry %s replaced (%zu bytes)", host.filename().string().c_str(), name.c_str(), bytes->size());
            }
        }
        uint32_t total = uint32_t(Size());
        std::memcpy(&head_[4], &total, 4);  // declared size is little endian
        return true;
    }

private:
    // the original entry, unpacked, patched and packed again when it was packed
    bool PatchEntry(uint32_t offset, uint32_t size, Patch patch, std::vector<uint8_t>& out) {
        if (uint64_t(offset) + size > orig_size_ || size < 5) return false;
        std::vector<uint8_t> raw(size);
        _fseeki64(fp_, int64_t(offset), SEEK_SET);
        if (std::fread(raw.data(), 1, size, fp_) != size) return false;
        bool packed = raw[1] == 0xFB;
        out = packed ? Unpack(raw.data(), raw.size()) : raw;
        if (out.empty() || !patch(out)) return false;
        if (packed) out = PackLiteral(out);
        return true;
    }

    FILE* fp_ = nullptr;
    std::mutex lock_;
    int reads_ = 0;
    uint64_t orig_size_ = 0;
    std::vector<uint8_t> head_, tail_;
};

// a whole file replaced by a mod
class BytesView : public Virtual {
public:
    explicit BytesView(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    uint64_t Size() const override { return bytes_.size(); }
    size_t Read(uint64_t pos, void* dst, size_t len) override {
        if (pos >= bytes_.size()) return 0;
        size_t n = std::min<uint64_t>(len, bytes_.size() - pos);
        std::memcpy(dst, bytes_.data() + pos, n);
        return n;
    }

private:
    std::vector<uint8_t> bytes_;
};

// docs/LOOSE_ASSETS.md: an archive extracted to a folder ("x.big" -> "x_big/") is served to the game as the archive
std::string ReadText(const fs::path& p) {
    std::vector<uint8_t> d;
    if (!mods::Load(p, d)) return {};
    return std::string(d.begin(), d.end());
}

// _big.json is written by setup and Workshop, so only its own keys are read: a string array or a number after "key":
std::vector<std::string> JsonStrings(const std::string& s, const char* key) {
    std::vector<std::string> out;
    size_t p = s.find("\"" + std::string(key) + "\"");
    if (p == std::string::npos || (p = s.find('[', p)) == std::string::npos) return out;
    for (p++; p < s.size() && s[p] != ']'; p++) {
        if (s[p] != '"') continue;
        std::string v;
        for (p++; p < s.size() && s[p] != '"'; p++) {
            if (s[p] == '\\' && p + 1 < s.size()) {
                char c = s[++p];
                v += c == 'n' ? '\n' : c == 't' ? '\t' : c;  // member names hold no \u escapes
            } else {
                v += s[p];
            }
        }
        out.push_back(std::move(v));
    }
    return out;
}

long JsonNumber(const std::string& s, const char* key, long fallback) {
    size_t p = s.find("\"" + std::string(key) + "\"");
    if (p == std::string::npos || (p = s.find(':', p)) == std::string::npos) return fallback;
    return std::strtol(s.c_str() + p + 1, nullptr, 10);
}

// "foo.big" <-> "foo_big"
std::string FolderName(const std::string& file) {
    size_t dot = file.rfind('.');
    return dot == std::string::npos ? file : file.substr(0, dot) + "_" + file.substr(dot + 1);
}

class LooseBig : public Virtual {
public:
    uint64_t Size() const override { return size_; }

    size_t Read(uint64_t pos, void* dst, size_t len) override {
        auto* out = static_cast<uint8_t*>(dst);
        size_t done = 0;
        while (done < len && pos < size_) {
            size_t n;
            if (pos < head_.size()) {
                n = std::min<uint64_t>(len - done, head_.size() - pos);
                std::memcpy(out + done, head_.data() + pos, n);
            } else {
                // the entry holding pos, or the alignment gap before the next one
                auto it = std::upper_bound(entries_.begin(), entries_.end(), pos,
                                           [](uint64_t v, const Entry& e) { return v < e.offset; });
                const Entry* e = it == entries_.begin() ? nullptr : &*(it - 1);
                if (e && pos < e->offset + e->size) {
                    n = std::min<uint64_t>(len - done, e->offset + e->size - pos);
                    n = ReadEntry(const_cast<Entry&>(*e), pos - e->offset, out + done, n);
                    if (!n) break;
                } else {
                    uint64_t next = it == entries_.end() ? size_ : it->offset;
                    n = std::min<uint64_t>(len - done, next - pos);
                    std::memset(out + done, 0, n);
                }
            }
            done += n;
            pos += n;
        }
        return done;
    }

    ~LooseBig() override {
        if (fp_) std::fclose(fp_);
    }

    bool Build(const fs::path& folder, const std::string& label, const std::vector<const Replacement*>& reps) {
        std::string meta = ReadText(folder / "_big.json");
        std::vector<std::string> order = JsonStrings(meta, "order");
        std::vector<std::string> packed_list = JsonStrings(meta, "refpack");
        uint64_t align = uint64_t(std::max(1L, JsonNumber(meta, "align", 4)));
        std::vector<std::string> packed;
        for (auto& s : packed_list) packed.push_back(Upper(s));

        // every loose member, by upper case name with backslashes; a folder named like an archive is a nested one.
        // Walked through kernel::mods, so mod members are found and removed ones are not.
        std::vector<std::pair<std::string, fs::path>> found;
        std::function<void(const fs::path&, const std::string&)> walk = [&](const fs::path& dir, const std::string& prefix) {
            for (const mods::Child& c : mods::List(dir)) {
                fs::path p = dir / fs::u8path(c.name);
                std::string name = prefix + c.name;
                if (c.dir) {
                    // a texture pack as a folder keeps its own name
                    if (xsh::IsFolder(p)) {
                        found.push_back({Upper(name), p});
                    } else if (mods::IsFile(p / "_big.json")) {
                        size_t us = name.rfind('_');
                        if (us != std::string::npos && name.find('\\', us) == std::string::npos)
                            found.push_back({Upper(name.substr(0, us) + "." + name.substr(us + 1)), p});
                    } else {
                        walk(p, name + "\\");
                    }
                    continue;
                }
                if (prefix.empty() && name == "_big.json") continue;
                found.push_back({Upper(name), p});
            }
        };
        walk(folder, "");
        // a name the disc repeats (identical placeholder entries) is one file, listed again
        std::map<std::string, fs::path> taken;
        auto take = [&](const std::string& name) -> fs::path {
            std::string up = Upper(name);
            std::replace(up.begin(), up.end(), '/', '\\');
            if (auto it = taken.find(up); it != taken.end()) return it->second;
            for (auto& f : found)
                if (f.first == up && !f.second.empty()) return taken[up] = std::exchange(f.second, fs::path());
            return {};
        };
        // the disc's member order first (with its spelling), then members added since
        for (const std::string& name : order) {
            fs::path p = take(name);
            if (p.empty()) {
                LOGW("loose: %s member %s is missing, left out", label.c_str(), name.c_str());
                continue;
            }
            entries_.push_back({name, p});
        }
        // a mod's whole "foo.big" sorts before the game's "foo_big/" and wins: one member per name
        std::stable_sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [name, p] : found)
            if (!p.empty() && taken.emplace(name, p).second) entries_.push_back({name, p});

        for (Entry& e : entries_) {
            std::string up = Upper(e.name);
            e.pack = std::find(packed.begin(), packed.end(), up) != packed.end() || mods::Packed(e.path);
            if (xsh::IsFolder(e.path)) {
                e.xsh = true;
                if (!xsh::Size(e.path, e.size)) return false;
            } else if (mods::IsDir(e.path)) {
                auto child = std::make_shared<LooseBig>();
                if (!child->Build(e.path, label + "\\" + e.name, {})) return false;
                e.child = child;
                e.size = child->Size();
                e.pack = false;
            } else {
                mods::Node n;
                if (!mods::Stat(e.path, n)) return false;
                e.size = n.size;
            }
            for (const Replacement* r : reps) {
                if (r->entry != up) continue;
                if (r->patch) {
                    std::vector<uint8_t> data;
                    if (e.child || !LoadFile(e.path, data) || data.empty() || !r->patch(data)) {
                        LOGE("mods: %s entry %s left as it was, the patch did not apply", label.c_str(), e.name.c_str());
                        continue;
                    }
                    e.bytes = std::move(data);
                } else {
                    e.bytes = r->bytes;  // a mod's payload is the entry as stored
                    e.pack = false;
                    LOGR("mods: %s entry %s replaced (%zu bytes)", label.c_str(), e.name.c_str(), r->bytes.size());
                }
                e.child.reset();
                e.loaded = true;
                e.size = e.bytes.size();
            }
            // the 3-byte RefPack size field: anything larger goes to the game unpacked
            if (e.pack && e.size >= (1u << 24)) {
                LOGW("loose: %s entry %s too large to store packed, served unpacked", label.c_str(), e.name.c_str());
                e.pack = false;
            }
        }
        // packing is most of the cost of opening a loose archive: spread it over the cores
        std::vector<Entry*> to_pack;
        for (Entry& e : entries_)
            if (e.pack || (e.xsh && !e.loaded)) to_pack.push_back(&e);
        std::atomic<size_t> next{0};
        std::atomic<bool> failed{false};
        auto work = [&] {
            for (size_t i; (i = next++) < to_pack.size();) {
                Entry& e = *to_pack[i];
                if (!e.loaded && !(e.xsh ? xsh::Build(e.path, e.bytes) : LoadFile(e.path, e.bytes))) failed = true;
                if (e.pack) e.bytes = Pack(e.bytes);
                e.loaded = true;
                e.size = e.bytes.size();
            }
        };
        size_t threads = std::min<size_t>(to_pack.size() / 8, std::max(1u, std::thread::hardware_concurrency()) - 1);
        std::vector<std::thread> pool;
        for (size_t i = 0; i < threads; i++) pool.emplace_back(work);
        work();
        for (std::thread& t : pool) t.join();
        if (failed) return false;

        // BIGF: "BIGF", LE total size, BE count, BE header size; entries BE offset, BE size, name; then "L234" and 1
        head_ = {'B', 'I', 'G', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        uint64_t dir_end = 16;
        for (const Entry& e : entries_) dir_end += 8 + e.name.size() + 1;
        uint64_t pos = (dir_end + 8 + align - 1) / align * align;
        for (Entry& e : entries_) {
            e.offset = pos;
            pos = (pos + e.size + align - 1) / align * align;
        }
        size_ = entries_.empty() ? dir_end + 8 : entries_.back().offset + entries_.back().size;
        if (size_ > 0xFFFFFFFFull) {
            LOGE("loose: %s is over 4 GB as an archive", label.c_str());
            return false;
        }
        for (const Entry& e : entries_) {
            uint8_t rec[8];
            PutBE32(rec, uint32_t(e.offset));
            PutBE32(rec + 4, uint32_t(e.size));
            head_.insert(head_.end(), rec, rec + 8);
            head_.insert(head_.end(), e.name.begin(), e.name.end());
            head_.push_back(0);
        }
        head_.insert(head_.end(), {'L', '2', '3', '4', 0, 0, 0, 1});
        uint32_t total = uint32_t(size_);
        std::memcpy(&head_[4], &total, 4);
        PutBE32(&head_[8], uint32_t(entries_.size()));
        PutBE32(&head_[12], uint32_t(head_.size()));
        return true;
    }

private:
    struct Entry {
        std::string name;  // as the archive spells it
        fs::path path;     // loose file, or the nested archive's folder
        uint64_t offset = 0, size = 0;
        bool pack = false;    // packed again (RefPack) as the disc stored it
        bool loaded = false;  // served from bytes
        bool xsh = false;     // a texture pack folder, rebuilt into the .xsh
        std::vector<uint8_t> bytes;
        std::shared_ptr<LooseBig> child;
    };

    static bool LoadFile(const fs::path& p, std::vector<uint8_t>& out) { return mods::Load(p, out); }

    size_t ReadEntry(Entry& e, uint64_t at, uint8_t* dst, size_t n) {
        if (e.child) return e.child->Read(at, dst, n);
        std::lock_guard<std::mutex> lock(lock_);
        // a mod's member sits in its zip, not on disk: read it whole the first time
        if (!e.loaded && mods::FromMod(e.path)) {
            if (!mods::Load(e.path, e.bytes)) return 0;
            e.loaded = true;
        }
        if (e.loaded) {
            if (at >= e.bytes.size()) return 0;
            n = std::min<uint64_t>(n, e.bytes.size() - at);
            std::memcpy(dst, e.bytes.data() + at, n);
            return n;
        }
        if (fp_path_ != e.path) {
            if (fp_) std::fclose(fp_);
            fp_ = std::fopen(e.path.string().c_str(), "rb");
            fp_path_ = e.path;
        }
        if (!fp_) return 0;
        _fseeki64(fp_, int64_t(at), SEEK_SET);
        return std::fread(dst, 1, n, fp_);
    }

    std::vector<uint8_t> head_;
    std::vector<Entry> entries_;
    uint64_t size_ = 0;
    std::mutex lock_;
    FILE* fp_ = nullptr;  // the last loose file read
    fs::path fp_path_;
};

}  // namespace

fs::path LooseFolder(const fs::path& host) {
    if (xsh::IsFolder(host)) return host;
    if (!host.has_filename() || !mods::IsDir(host.parent_path())) return {};
    fs::path dir = mods::Find(host.parent_path(), FolderName(host.filename().string()));
    return !dir.empty() && mods::IsDir(dir) ? dir : fs::path();
}

std::shared_ptr<Virtual> OpenLoose(const fs::path& host) {
    // a whole file from a mod (a disc file, a whole .big or .xsh)
    if (mods::FromMod(host)) {
        std::vector<uint8_t> bytes;
        if (!mods::Load(host, bytes)) return nullptr;
        LOGR("mods: %s served from a mod (%zu bytes)", host.filename().string().c_str(), bytes.size());
        return std::make_shared<BytesView>(bytes);
    }
    // a texture pack outside any archive, installed as a folder of the same name
    if (xsh::IsFolder(host)) {
        std::vector<uint8_t> bytes;
        if (!xsh::Build(host, bytes)) return nullptr;
        return std::make_shared<BytesView>(bytes);
    }
    fs::path folder = LooseFolder(host);
    if (folder.empty()) return nullptr;
    // archives don't change while the game runs, and some are opened many times
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<Virtual>> built;
    std::string key = Upper(folder.string());
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = built.find(key); it != built.end()) return it->second;

    std::string path = Upper(host.string());
    std::vector<const Replacement*> hits;
    for (const Replacement& r : Replacements()) {
        std::string tail = "\\" + r.archive;
        if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) hits.push_back(&r);
    }
    std::shared_ptr<Virtual> view;
    for (const Replacement* r : hits)
        if (r->entry.empty() && !r->patch) view = std::make_shared<BytesView>(r->bytes);
    if (!view) {
        auto big = std::make_shared<LooseBig>();
        auto start = std::chrono::steady_clock::now();
        if (!big->Build(folder, host.filename().string(), hits)) {
            LOGE("loose: cannot serve %s from %s", host.string().c_str(), folder.string().c_str());
            return nullptr;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        LOGR("loose: %s served from %s (%llu bytes, %lld ms)", host.filename().string().c_str(),
             folder.filename().string().c_str(), (unsigned long long)big->Size(), (long long)ms);
        view = big;
    }
    built[key] = view;
    return view;
}

void AddPatch(const char* archive, const char* entry, Patch patch) {
    Replacement r;
    r.archive = Upper(archive);
    r.entry = Upper(entry);
    r.patch = patch;
    Patches().push_back(std::move(r));
}

std::shared_ptr<Virtual> Open(const fs::path& host) {
    auto& all = Replacements();
    std::string path = Upper(host.string());
    std::vector<const Replacement*> hits;
    for (const Replacement& r : all) {
        std::string tail = "\\" + r.archive;
        if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) hits.push_back(&r);
    }
    // an archive installed packed: mod members under its x_big/ path replace entries (no additions or removals)
    std::vector<Replacement> modded;
    if (mods::Active() && host.has_filename()) {
        fs::path dir = host.parent_path() / FolderName(host.filename().string());
        for (const fs::path& f : mods::ModFilesUnder(dir)) {
            Replacement r;
            // f is spelled in lower case from the game folder down: cut the folder off by length
            std::string base = dir.lexically_normal().generic_string(), entry = f.generic_string();
            while (!base.empty() && base.back() == '/') base.pop_back();
            entry = entry.substr(std::min(entry.size(), base.size() + 1));
            std::replace(entry.begin(), entry.end(), '/', '\\');
            r.entry = Upper(entry);
            if (!mods::Load(f, r.bytes)) continue;
            // the game's patch for that entry runs on the mod's bytes, and the patch itself then drops out
            for (auto it = hits.begin(); it != hits.end();)
                if ((*it)->patch && (*it)->entry == r.entry) {
                    if (!(*it)->patch(r.bytes)) LOGE("mods: %s entry %s: the patch did not apply", host.filename().string().c_str(), entry.c_str());
                    it = hits.erase(it);
                } else {
                    ++it;
                }
            modded.push_back(std::move(r));
        }
        if (!modded.empty())
            LOGW("mods: %s is installed packed: mod members replace entries, added or removed ones are ignored",
                 host.filename().string().c_str());
    }
    for (const Replacement& r : modded) hits.push_back(&r);
    if (hits.empty()) return nullptr;
    for (const Replacement* r : hits)
        if (r->entry.empty() && !r->patch) {
            LOGR("mods: %s replaced as a whole file (%zu bytes)", host.filename().string().c_str(), r->bytes.size());
            return std::make_shared<BytesView>(r->bytes);
        }
    auto view = std::make_shared<BigView>();
    if (!view->Build(host, hits)) {
        LOGE("mods: cannot build a view of %s", host.string().c_str());
        return nullptr;
    }
    return view;
}

}  // namespace kernel::modvfs
