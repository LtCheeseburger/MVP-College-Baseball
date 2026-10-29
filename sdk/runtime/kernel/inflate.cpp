#include "kernel/inflate.h"

#include <cstring>

namespace kernel {

namespace {

// canonical Huffman decoding as in zlib's puff: counts per length, symbols ordered by code
struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
};

struct Bits {
    const uint8_t* in;
    size_t len, pos = 0;
    uint32_t buf = 0, have = 0;
    bool bad = false;

    uint32_t Get(int n) {
        while (have < uint32_t(n)) {
            if (pos >= len) {
                bad = true;
                return 0;
            }
            buf |= uint32_t(in[pos++]) << have;
            have += 8;
        }
        uint32_t v = buf & ((1u << n) - 1);
        buf >>= n;
        have -= n;
        return v;
    }

    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; l++) {
            code |= int(Get(1));
            if (bad) return -1;
            int c = h.count[l];
            if (code - c < first) return h.symbol[index + (code - first)];
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        return -1;
    }
};

// false when the lengths over-subscribe; an incomplete code is allowed (single-symbol distance codes)
bool Build(Huffman& h, const uint8_t* lengths, int n) {
    std::memset(h.count, 0, sizeof(h.count));
    for (int s = 0; s < n; s++) h.count[lengths[s]]++;
    if (h.count[0] == n) return true;
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return false;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = uint16_t(offs[l] + h.count[l]);
    for (int s = 0; s < n; s++)
        if (lengths[s]) h.symbol[offs[lengths[s]]++] = uint16_t(s);
    return true;
}

constexpr uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        int sym = b.Decode(lit);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = kLenBase[sym] + b.Get(kLenExtra[sym]);
            int d = b.Decode(dist);
            if (d < 0 || d >= 30) return false;
            size_t back = kDistBase[d] + b.Get(kDistExtra[d]);
            if (b.bad || back > out.size()) return false;
            size_t from = out.size() - back;
            for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
        }
    }
}

}  // namespace

bool Inflate(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out, size_t size) {
    out.clear();
    out.reserve(size);
    Bits b{in, in_len};
    bool last;
    do {
        last = b.Get(1);
        uint32_t type = b.Get(2);
        if (b.bad) return false;
        if (type == 0) {
            b.buf = b.have = 0;  // stored: from the next byte boundary
            if (b.pos + 4 > in_len) return false;
            uint32_t len = in[b.pos] | in[b.pos + 1] << 8, nlen = in[b.pos + 2] | in[b.pos + 3] << 8;
            b.pos += 4;
            if ((len ^ 0xFFFF) != nlen || b.pos + len > in_len) return false;
            out.insert(out.end(), in + b.pos, in + b.pos + len);
            b.pos += len;
        } else if (type == 1) {
            static Huffman lit, dist;
            static bool ready = [] {
                uint8_t l[288];
                int s = 0;
                for (; s < 144; s++) l[s] = 8;
                for (; s < 256; s++) l[s] = 9;
                for (; s < 280; s++) l[s] = 7;
                for (; s < 288; s++) l[s] = 8;
                Build(lit, l, 288);
                for (s = 0; s < 30; s++) l[s] = 5;
                Build(dist, l, 30);
                return true;
            }();
            (void)ready;
            if (!Codes(b, out, lit, dist)) return false;
        } else if (type == 2) {
            int nlen = int(b.Get(5)) + 257, ndist = int(b.Get(5)) + 1, ncode = int(b.Get(4)) + 4;
            if (b.bad || nlen > 286 || ndist > 30) return false;
            static constexpr uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; i++) lengths[kOrder[i]] = uint8_t(b.Get(3));
            Huffman lencode, lit, dist;
            if (!Build(lencode, lengths, 19)) return false;
            int i = 0;
            while (i < nlen + ndist) {
                int sym = b.Decode(lencode);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[i++] = uint8_t(sym);
                    continue;
                }
                uint8_t v = 0;
                int rep;
                if (sym == 16) {
                    if (i == 0) return false;
                    v = lengths[i - 1];
                    rep = 3 + int(b.Get(2));
                } else if (sym == 17) {
                    rep = 3 + int(b.Get(3));
                } else {
                    rep = 11 + int(b.Get(7));
                }
                if (b.bad || i + rep > nlen + ndist) return false;
                while (rep--) lengths[i++] = v;
            }
            if (lengths[256] == 0) return false;
            if (!Build(lit, lengths, nlen) || !Build(dist, lengths + nlen, ndist)) return false;
            if (!Codes(b, out, lit, dist)) return false;
        } else {
            return false;
        }
        if (out.size() > size) return false;
    } while (!last);
    return out.size() == size;
}

}  // namespace kernel
