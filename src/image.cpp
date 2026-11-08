#include "prism/image.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace prism {

// ───────────────────────────── Image ─────────────────────────────

void Image::fill(uint32_t c) {
    uint8_t r = c >> 24, g = c >> 16, b = c >> 8, a = c;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
}

void Image::set_pixel(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    uint8_t* p = px(x, y);
    p[0] = c >> 24;
    p[1] = c >> 16;
    p[2] = c >> 8;
    p[3] = c;
}

uint32_t Image::get_pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const uint8_t* p = px(x, y);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Image::fill_rect(int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) set_pixel(i, j, c);
}

uint64_t Image::hash() const {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };
    for (int v : {width, height})
        for (int s = 0; s < 32; s += 8) mix(static_cast<uint8_t>(v >> s));
    for (uint8_t b : pixels) mix(b);
    return h;
}

// ───────────────────────────── checksums ─────────────────────────────

namespace {

const std::array<uint32_t, 256>& crc_table() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> tab{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tab[n] = c;
        }
        return tab;
    }();
    return t;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* d, size_t n) {
    const auto& t = crc_table();
    for (size_t i = 0; i < n; ++i) crc = t[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t adler32(const uint8_t* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + d[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}

void put_chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    put32be(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = crc32_update(0xFFFFFFFFu, out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    put32be(out, crc);
}

// ───────────────────────────── inflate (RFC 1951) ─────────────────────────────
// Canonical-Huffman decoder in the style of zlib's reference "puff".

struct BitReader {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool err = false;

    uint32_t bits(int need) {
        uint32_t val = buf;
        while (cnt < need) {
            if (pos >= n) {
                err = true;
                return 0;
            }
            val |= static_cast<uint32_t>(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = val >> need;
        cnt -= need;
        return val & ((1u << need) - 1u);
    }
};

struct Huffman {
    short count[16];
    short symbol[320];
};

// Returns 0 if complete, >0 if incomplete, <0 if over-subscribed.
int build_huffman(Huffman& h, const short* length, int n) {
    for (int l = 0; l < 16; ++l) h.count[l] = 0;
    for (int s = 0; s < n; ++s) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int l = 1; l < 16; ++l) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = offs[l] + h.count[l];
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = static_cast<short>(s);
    return left;
}

int decode_symbol(BitReader& s, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= static_cast<int>(s.bits(1));
        if (s.err) return -1;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

constexpr short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                 8193, 12289, 16385, 24577};
constexpr short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

const char* inflate_codes(BitReader& s, std::vector<uint8_t>& out, size_t max_out, const Huffman& lit,
                          const Huffman& dist) {
    for (;;) {
        int sym = decode_symbol(s, lit);
        if (sym < 0) return "invalid literal/length code or truncated stream";
        if (sym < 256) {
            if (out.size() >= max_out) return "output too large";
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return nullptr;
        } else {
            sym -= 257;
            if (sym >= 29) return "invalid length symbol";
            int len = kLenBase[sym] + static_cast<int>(s.bits(kLenExtra[sym]));
            int ds = decode_symbol(s, dist);
            if (ds < 0 || ds >= 30) return "invalid distance code";
            size_t d = static_cast<size_t>(kDistBase[ds]) + s.bits(kDistExtra[ds]);
            if (s.err) return "truncated stream";
            if (d > out.size()) return "distance too far back";
            if (out.size() + static_cast<size_t>(len) > max_out) return "output too large";
            size_t from = out.size() - d;
            for (int i = 0; i < len; ++i) out.push_back(out[from + i]);
        }
    }
}

const char* inflate_raw(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t max_out) {
    BitReader s{data, size};
    static const Huffman* fixed_lit = nullptr;
    static const Huffman* fixed_dist = nullptr;
    if (!fixed_lit) {
        static Huffman fl, fd;
        short l[288];
        int i = 0;
        for (; i < 144; ++i) l[i] = 8;
        for (; i < 256; ++i) l[i] = 9;
        for (; i < 280; ++i) l[i] = 7;
        for (; i < 288; ++i) l[i] = 8;
        build_huffman(fl, l, 288);
        for (i = 0; i < 30; ++i) l[i] = 5;
        build_huffman(fd, l, 30);
        fixed_lit = &fl;
        fixed_dist = &fd;
    }

    int last;
    do {
        last = static_cast<int>(s.bits(1));
        int type = static_cast<int>(s.bits(2));
        if (s.err) return "truncated stream";
        const char* e = nullptr;
        if (type == 0) {
            s.buf = 0;
            s.cnt = 0;
            if (s.pos + 4 > s.n) return "truncated stored block";
            unsigned len = s.p[s.pos] | (s.p[s.pos + 1] << 8);
            unsigned nlen = s.p[s.pos + 2] | (s.p[s.pos + 3] << 8);
            s.pos += 4;
            if (len != (~nlen & 0xFFFFu)) return "stored block length mismatch";
            if (s.pos + len > s.n) return "truncated stored block";
            if (out.size() + len > max_out) return "output too large";
            out.insert(out.end(), s.p + s.pos, s.p + s.pos + len);
            s.pos += len;
        } else if (type == 1) {
            e = inflate_codes(s, out, max_out, *fixed_lit, *fixed_dist);
        } else if (type == 2) {
            static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int nlen = static_cast<int>(s.bits(5)) + 257;
            int ndist = static_cast<int>(s.bits(5)) + 1;
            int ncode = static_cast<int>(s.bits(4)) + 4;
            if (s.err) return "truncated stream";
            if (nlen > 286 || ndist > 30) return "bad dynamic block counts";
            short lengths[320];
            int idx = 0;
            for (; idx < ncode; ++idx) lengths[order[idx]] = static_cast<short>(s.bits(3));
            for (; idx < 19; ++idx) lengths[order[idx]] = 0;
            Huffman lencode;
            if (build_huffman(lencode, lengths, 19) != 0) return "bad code-length code";
            idx = 0;
            while (idx < nlen + ndist) {
                int sym = decode_symbol(s, lencode);
                if (sym < 0) return "bad code-length symbol or truncated stream";
                if (sym < 16) {
                    lengths[idx++] = static_cast<short>(sym);
                } else {
                    int len = 0;
                    if (sym == 16) {
                        if (idx == 0) return "repeat with no previous length";
                        len = lengths[idx - 1];
                        sym = 3 + static_cast<int>(s.bits(2));
                    } else if (sym == 17) {
                        sym = 3 + static_cast<int>(s.bits(3));
                    } else {
                        sym = 11 + static_cast<int>(s.bits(7));
                    }
                    if (s.err) return "truncated stream";
                    if (idx + sym > nlen + ndist) return "too many code lengths";
                    while (sym--) lengths[idx++] = static_cast<short>(len);
                }
            }
            if (lengths[256] == 0) return "missing end-of-block code";
            Huffman lit, dist;
            int err = build_huffman(lit, lengths, nlen);
            if (err && (err < 0 || nlen != lit.count[0] + lit.count[1])) return "bad literal/length code";
            err = build_huffman(dist, lengths + nlen, ndist);
            if (err && (err < 0 || ndist != dist.count[0] + dist.count[1])) return "bad distance code";
            e = inflate_codes(s, out, max_out, lit, dist);
        } else {
            return "invalid block type";
        }
        if (e) return e;
    } while (!last);
    return nullptr;
}

}  // namespace

bool inflate_zlib(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t max_out,
                  std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (size < 2) return fail("zlib stream too short");
    if ((data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) return fail("bad zlib header");
    if (data[1] & 0x20) return fail("zlib preset dictionary not supported");
    out.clear();
    const char* e = inflate_raw(data + 2, size - 2, out, max_out);
    if (e) return fail(e);
    return true;
}

// ───────────────────────────── PNG ─────────────────────────────

std::vector<uint8_t> encode_png(const Image& img) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    put32be(ihdr, static_cast<uint32_t>(img.width));
    put32be(ihdr, static_cast<uint32_t>(img.height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, deflate, adaptive filter, no interlace
    put_chunk(out, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    const size_t stride = static_cast<size_t>(img.width) * 4;
    raw.reserve((stride + 1) * img.height);
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), img.pixels.begin() + y * stride, img.pixels.begin() + (y + 1) * stride);
    }

    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool final = pos + len == raw.size();
        z.push_back(final ? 1 : 0);
        z.push_back(len & 0xFF);
        z.push_back(len >> 8);
        z.push_back(~len & 0xFF);
        z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    } while (pos < raw.size());
    put32be(z, adler32(raw.data(), raw.size()));
    put_chunk(out, "IDAT", z);
    put_chunk(out, "IEND", {});
    return out;
}

bool decode_png(const uint8_t* d, size_t n, Image& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 8 || std::memcmp(d, sig, 8) != 0) return fail("not a PNG file");

    auto be32 = [&](size_t p) {
        return (uint32_t(d[p]) << 24) | (uint32_t(d[p + 1]) << 16) | (uint32_t(d[p + 2]) << 8) | d[p + 3];
    };

    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, interlace = 0;
    bool have_ihdr = false, have_iend = false;
    std::vector<uint8_t> idat;
    std::vector<uint8_t> palette;  // RGB triples
    std::vector<uint8_t> trns;

    size_t p = 8;
    while (p + 12 <= n && !have_iend) {
        uint32_t len = be32(p);
        if (len > n || p + 12 + len > n) return fail("truncated chunk");
        const uint8_t* type = d + p + 4;
        const uint8_t* body = d + p + 8;
        uint32_t crc = crc32_update(0xFFFFFFFFu, type, 4 + len) ^ 0xFFFFFFFFu;
        if (crc != be32(p + 8 + len)) return fail("chunk CRC mismatch");

        if (!std::memcmp(type, "IHDR", 4)) {
            if (len != 13) return fail("bad IHDR");
            w = be32(p + 8);
            h = be32(p + 12);
            depth = body[8];
            ctype = body[9];
            interlace = body[12];
            have_ihdr = true;
        } else if (!std::memcmp(type, "PLTE", 4)) {
            palette.assign(body, body + len);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            trns.assign(body, body + len);
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), body, body + len);
        } else if (!std::memcmp(type, "IEND", 4)) {
            have_iend = true;
        }
        p += 12 + len;
    }
    if (!have_ihdr) return fail("missing IHDR");
    if (!have_iend) return fail("missing IEND");
    if (w == 0 || h == 0 || w > 16384 || h > 16384 || uint64_t(w) * h > (64ull << 20))
        return fail("unsupported image dimensions");
    if (depth != 8) return fail("only 8-bit PNGs are supported");
    if (interlace != 0) return fail("interlaced PNGs are not supported");

    int channels;
    switch (ctype) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return fail("unsupported PNG color type");
    }
    if (ctype == 3 && palette.empty()) return fail("palette PNG without PLTE");

    const size_t stride = static_cast<size_t>(w) * channels;
    const size_t expected = (stride + 1) * h;
    std::vector<uint8_t> raw;
    std::string zerr;
    if (!inflate_zlib(idat.data(), idat.size(), raw, expected, &zerr)) return fail("inflate: " + zerr);
    if (raw.size() != expected) return fail("decompressed size mismatch");

    // Undo scanline filters in place.
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = &raw[y * (stride + 1)];
        uint8_t ft = row[0];
        uint8_t* cur = row + 1;
        const uint8_t* prev = y ? &raw[(y - 1) * (stride + 1) + 1] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            int a = i >= static_cast<size_t>(channels) ? cur[i - channels] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= static_cast<size_t>(channels)) ? prev[i - channels] : 0;
            int v = cur[i];
            switch (ft) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: {
                    int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
                    v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: return fail("bad scanline filter");
            }
            cur[i] = static_cast<uint8_t>(v);
        }
    }

    out = Image(static_cast<int>(w), static_cast<int>(h));
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* src = &raw[y * (stride + 1) + 1];
        uint8_t* dst = out.px(0, static_cast<int>(y));
        for (uint32_t x = 0; x < w; ++x, dst += 4) {
            const uint8_t* s = src + x * channels;
            switch (ctype) {
                case 0: dst[0] = dst[1] = dst[2] = s[0]; dst[3] = 255; break;
                case 2: dst[0] = s[0]; dst[1] = s[1]; dst[2] = s[2]; dst[3] = 255; break;
                case 3: {
                    size_t i = s[0];
                    if (i * 3 + 2 >= palette.size()) return fail("palette index out of range");
                    dst[0] = palette[i * 3];
                    dst[1] = palette[i * 3 + 1];
                    dst[2] = palette[i * 3 + 2];
                    dst[3] = i < trns.size() ? trns[i] : 255;
                    break;
                }
                case 4: dst[0] = dst[1] = dst[2] = s[0]; dst[3] = s[1]; break;
                default: dst[0] = s[0]; dst[1] = s[1]; dst[2] = s[2]; dst[3] = s[3]; break;
            }
        }
    }
    return true;
}

bool save_png(const std::string& path, const Image& img) {
    if (img.empty()) return false;
    std::vector<uint8_t> bytes = encode_png(img);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

bool load_png(const std::string& path, Image& out, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes;
    uint8_t buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + got);
    std::fclose(f);
    return decode_png(bytes.data(), bytes.size(), out, error);
}

}  // namespace prism
