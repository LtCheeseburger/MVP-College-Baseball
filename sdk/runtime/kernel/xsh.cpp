#include "kernel/xsh.h"

#include "kernel/mods.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

#include "core/log.h"

namespace fs = std::filesystem;

namespace kernel::xsh {

namespace {

// xsh.meta as MVP Workshop writes it (XshFolder.cs): "XSHM", version 1, head, then per image in file order its
// directory index, kind (0 DDS, 1 raw), file name, 16-byte block header, pad length and the bytes up to the next block
struct Image {
    uint32_t dir_index = 0;
    bool dds = false;
    std::string file;
    uint8_t header[16] = {};
    uint32_t pad = 0;
    std::vector<uint8_t> rest;
};

struct Meta {
    std::vector<uint8_t> head;
    std::vector<Image> images;
};

uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
void Put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (i * 8));
}
void Put16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}

// through kernel::mods, so a mod's image in one slot is used with the game's xsh.meta and other images
bool ReadAll(const fs::path& p, std::vector<uint8_t>& out, size_t limit = SIZE_MAX) { return mods::Load(p, out, limit); }

uint64_t FileSize(const fs::path& p, std::error_code& ec) {
    mods::Node n;
    if (!mods::Stat(p, n) || n.dir) {
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return 0;
    }
    ec.clear();
    return n.size;
}

bool ReadMeta(const fs::path& dir, Meta& m) {
    std::vector<uint8_t> d;
    if (!ReadAll(dir / "xsh.meta", d)) return false;
    size_t p = 0;
    auto need = [&](size_t n) { return p + n <= d.size(); };
    if (!need(16) || std::memcmp(d.data(), "XSHM", 4) != 0 || U32(&d[4]) != 1) return false;
    p = 8;
    uint32_t n = U32(&d[p]);
    p += 4;
    if (!need(n)) return false;
    m.head.assign(d.begin() + p, d.begin() + p + n);
    p += n;
    if (!need(4)) return false;
    uint32_t count = U32(&d[p]);
    p += 4;
    for (uint32_t i = 0; i < count; i++) {
        Image img;
        if (!need(7)) return false;
        img.dir_index = U32(&d[p]);
        img.dds = d[p + 4] == 0;
        uint16_t len = U16(&d[p + 5]);
        p += 7;
        if (!need(len + 16 + 8)) return false;
        img.file.assign(reinterpret_cast<const char*>(&d[p]), len);
        p += len;
        std::memcpy(img.header, &d[p], 16);
        p += 16;
        img.pad = U32(&d[p]);
        uint32_t rest = U32(&d[p + 4]);
        p += 8;
        if (!need(rest)) return false;
        img.rest.assign(d.begin() + p, d.begin() + p + rest);
        p += rest;
        m.images.push_back(std::move(img));
    }
    return p == d.size() && m.head.size() >= 16;
}

size_t DxtSize(int w, int h, int dxt) { return size_t((w + 3) / 4) * ((h + 3) / 4) * (dxt == 1 ? 8 : 16); }

// one level's bytes in an SHPX type (0x60 DXT1, 0x61 DXT3, 0x7D BGRA); DXT5 counts as DXT3 for size
size_t LevelBytes(int type, int w, int h) {
    switch (type) {
        case 0x60: return DxtSize(w, h, 1);
        case 0x61:
        case 0x62: return DxtSize(w, h, 3);
        case 0x7D: return size_t(w) * h * 4;
        default: return 0;
    }
}

size_t ChainBytes(int type, int w, int h, int mips) {
    size_t total = 0;
    for (int m = 0; m <= mips; m++) total += LevelBytes(type, std::max(1, w >> m), std::max(1, h >> m));
    return total;
}

bool PowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }

// The format of a DDS header: 0x60/0x61/0x62 for DXT1/3/5, 0x7D for 32-bit BGRA, 0 for others
int DdsType(const std::vector<uint8_t>& f, int& w, int& h, int& levels, size_t& at) {
    if (f.size() < 128 || std::memcmp(f.data(), "DDS ", 4) != 0) return 0;
    h = int(U32(&f[12]));
    w = int(U32(&f[16]));
    levels = (U32(&f[8]) & 0x20000) ? std::max(1, int(U32(&f[28]))) : 1;
    at = 128;
    uint32_t pf = U32(&f[80]);
    if (pf & 4) {
        if (std::memcmp(&f[84], "DX10", 4) == 0) {
            if (f.size() < 148) return 0;
            at = 148;
            switch (U32(&f[128])) {
                case 71: case 72: return 0x60;
                case 74: case 75: return 0x61;
                case 77: case 78: return 0x62;
                case 87: case 91: return 0x7D;
                default: return 0;
            }
        }
        if (std::memcmp(&f[84], "DXT1", 4) == 0) return 0x60;
        if (std::memcmp(&f[84], "DXT2", 4) == 0 || std::memcmp(&f[84], "DXT3", 4) == 0) return 0x61;
        if (std::memcmp(&f[84], "DXT4", 4) == 0 || std::memcmp(&f[84], "DXT5", 4) == 0) return 0x62;
        return 0;
    }
    return (pf & 0x40) && U32(&f[88]) == 32 && U32(&f[92]) == 0x00FF0000 && U32(&f[100]) == 0x000000FF ? 0x7D : 0;
}

// Morton order as XboxTexture.MortonIndex: low bits alternate x and y, the longer side's bits continue alone
void Swizzle(const uint8_t* src, uint8_t* dst, int w, int h) {
    int wb = 0, hb = 0;
    while ((1 << wb) < w) wb++;
    while ((1 << hb) < h) hb++;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t idx = 0, shift = 0;
            for (int i = 0; i < std::max(wb, hb); i++) {
                if (i < wb) idx |= uint32_t(x >> i & 1) << shift++;
                if (i < hb) idx |= uint32_t(y >> i & 1) << shift++;
            }
            std::memcpy(dst + idx * 4, src + (size_t(y) * w + x) * 4, 4);
        }
}

// One image's pixels and header. With pixels null only the length is worked out.
bool ImagePixels(const fs::path& dir, const Image& img, uint8_t header[16], std::vector<uint8_t>* pixels, size_t& length) {
    std::memcpy(header, img.header, 16);
    fs::path file = dir / img.file;
    std::error_code ec;
    if (!img.dds) {
        length = size_t(FileSize(file, ec));
        if (ec) return false;
        return !pixels || ReadAll(file, *pixels);
    }
    int slot = header[0], ow = U16(header + 4), oh = U16(header + 6), omips = U16(header + 14) >> 12;
    std::vector<uint8_t> f;
    if (!ReadAll(file, f, pixels ? SIZE_MAX : 148)) return false;
    uint64_t file_size = FileSize(file, ec);
    int w = 0, h = 0, levels = 0;
    size_t at = 0;
    int type = DdsType(f, w, h, levels, at);
    const char* why = nullptr;
    if (type != slot) why = "is in another format than the game's";
    else if (w <= 0 || h <= 0 || w > 4096 || h > 4096) why = "has a bad size";
    else if (slot == 0x7D && !(PowerOfTwo(w) && PowerOfTwo(h))) why = "needs power-of-two sides";
    int have = 0;
    size_t bytes = 0;
    if (!why) {
        for (int m = 0; m < std::min(levels, 13); m++) {
            size_t n = LevelBytes(type, std::max(1, w >> m), std::max(1, h >> m));
            if (at + bytes + n > file_size) break;
            bytes += n;
            have++;
        }
        if (!have) why = "is cut short";
    }
    if (why) {
        // blank pixels in the slot's own size and format
        LOGW("textures: %s %s, shown blank", file.string().c_str(), why);
        length = ChainBytes(slot, ow, oh, omips);
        if (pixels) pixels->assign(length, 0);
        return true;
    }
    Put16(header + 4, uint16_t(w));
    Put16(header + 6, uint16_t(h));
    Put16(header + 14, uint16_t((U16(header + 14) & 0x0FFF) | (have - 1) << 12));
    length = bytes;
    if (!pixels) return true;
    pixels->assign(f.begin() + at, f.begin() + at + bytes);
    if (slot == 0x7D) {
        std::vector<uint8_t> level;
        size_t p = 0;
        for (int m = 0; m < have; m++) {
            int lw = std::max(1, w >> m), lh = std::max(1, h >> m);
            level.assign(pixels->begin() + p, pixels->begin() + p + size_t(lw) * lh * 4);
            Swizzle(level.data(), pixels->data() + p, lw, lh);
            p += size_t(lw) * lh * 4;
        }
    }
    return true;
}

bool Assemble(const fs::path& dir, std::vector<uint8_t>* out, uint64_t& size) {
    Meta m;
    if (!ReadMeta(dir, m)) {
        LOGE("textures: %s has no readable xsh.meta", dir.string().c_str());
        return false;
    }
    if (out) *out = m.head;
    uint64_t pos = m.head.size();
    std::vector<uint64_t> offsets;
    std::vector<uint8_t> pixels;
    for (const Image& img : m.images) {
        uint8_t header[16];
        size_t length = 0;
        if (!ImagePixels(dir, img, header, out ? &pixels : nullptr, length)) {
            LOGE("textures: %s: cannot read %s", dir.string().c_str(), img.file.c_str());
            return false;
        }
        if (header[1] | header[2] | header[3]) {
            uint32_t bs = uint32_t(16 + length + img.pad);
            header[1] = uint8_t(bs);
            header[2] = uint8_t(bs >> 8);
            header[3] = uint8_t(bs >> 16);
        }
        offsets.push_back(pos);
        if (out) {
            out->insert(out->end(), header, header + 16);
            out->insert(out->end(), pixels.begin(), pixels.end());
            out->resize(out->size() + img.pad);
            out->insert(out->end(), img.rest.begin(), img.rest.end());
        }
        pos += 16 + length + img.pad + img.rest.size();
    }
    size = pos;
    if (!out) return true;
    Put32(out->data() + 4, uint32_t(out->size()));
    for (size_t i = 0; i < m.images.size(); i++) {
        size_t at = 16 + size_t(m.images[i].dir_index) * 8 + 4;
        if (at + 4 > m.head.size()) return false;
        Put32(out->data() + at, uint32_t(offsets[i]));
    }
    return true;
}

}  // namespace

bool IsFolder(const fs::path& dir) {
    return mods::IsFile(dir / "xsh.meta");
}

bool Size(const fs::path& dir, uint64_t& size) { return Assemble(dir, nullptr, size); }

bool Build(const fs::path& dir, std::vector<uint8_t>& out) {
    uint64_t size = 0;
    return Assemble(dir, &out, size);
}

}  // namespace kernel::xsh
