#pragma once
// CPU image (RGBA8) plus a dependency-free PNG encoder/decoder.
//  * decode: 8-bit gray / gray+alpha / RGB / RGBA / palette (+tRNS), non-interlaced.
//  * encode: 8-bit RGBA using stored (uncompressed) deflate blocks. Valid PNG, large files.
#include <cstdint>
#include <string>
#include <vector>

#include "types.hpp"

namespace prism {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;  // RGBA, row-major, 4 bytes per pixel

    Image() = default;
    Image(int w, int h, uint32_t rgba = 0x00000000u) : width(w), height(h), pixels(static_cast<size_t>(w) * h * 4) {
        fill(rgba);
    }

    bool empty() const { return width <= 0 || height <= 0; }
    uint8_t* px(int x, int y) { return &pixels[(static_cast<size_t>(y) * width + x) * 4]; }
    const uint8_t* px(int x, int y) const { return &pixels[(static_cast<size_t>(y) * width + x) * 4]; }

    // 0xRRGGBBAA
    void fill(uint32_t rgba);
    void set_pixel(int x, int y, uint32_t rgba);  // bounds-checked
    uint32_t get_pixel(int x, int y) const;       // bounds-checked, 0 outside
    void fill_rect(int x, int y, int w, int h, uint32_t rgba);
    uint64_t hash() const;  // FNV-1a over dimensions + pixels (determinism tests)
};

// 0xRRGGBBAA helper.
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | a;
}

std::vector<uint8_t> encode_png(const Image& img);
bool decode_png(const uint8_t* data, size_t size, Image& out, std::string* error = nullptr);
bool save_png(const std::string& path, const Image& img);
bool load_png(const std::string& path, Image& out, std::string* error = nullptr);

// zlib stream -> raw bytes. Fails (returns false) on malformed input or if output would
// exceed max_out. Exposed mainly for tests.
bool inflate_zlib(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t max_out,
                  std::string* error = nullptr);

}  // namespace prism
