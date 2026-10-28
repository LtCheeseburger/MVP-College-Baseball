#include "kernel/modvfs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"

namespace fs = std::filesystem;

namespace kernel::modvfs {

namespace {

// M0 prototype: MVP06_M0_MOD names a folder holding mod.txt, one "archive|entry|payload" line per replaced BIG entry
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

std::vector<Replacement>& Replacements() {
    static std::vector<Replacement> list;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const Replacement& r : Patches()) list.push_back(r);
        const char* dir = std::getenv("MVP06_M0_MOD");
        if (!dir || !*dir) return;
        std::ifstream manifest(fs::path(dir) / "mod.txt");
        std::string line;
        while (std::getline(manifest, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            size_t a = line.find('|'), b = line.find('|', a + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            Replacement r;
            r.archive = Upper(line.substr(0, a));
            r.entry = Upper(line.substr(a + 1, b - a - 1));
            std::ifstream in(fs::path(dir) / line.substr(b + 1), std::ios::binary);
            r.bytes.assign(std::istreambuf_iterator<char>(in), {});
            LOGR("mods: %s / %s <- %zu bytes", line.substr(0, a).c_str(), line.substr(a + 1, b - a - 1).c_str(), r.bytes.size());
            list.push_back(std::move(r));
        }
    });
    return list;
}

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

}  // namespace

void AddPatch(const char* archive, const char* entry, Patch patch) {
    Replacement r;
    r.archive = Upper(archive);
    r.entry = Upper(entry);
    r.patch = patch;
    Patches().push_back(std::move(r));
}

std::shared_ptr<Virtual> Open(const fs::path& host) {
    auto& all = Replacements();
    if (all.empty()) return nullptr;
    std::string path = Upper(host.string());
    std::vector<const Replacement*> hits;
    for (const Replacement& r : all) {
        std::string tail = "\\" + r.archive;
        if (path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0) hits.push_back(&r);
    }
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
