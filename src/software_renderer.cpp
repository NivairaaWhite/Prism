#include "prism/software_renderer.hpp"

#include <cmath>

namespace prism {
namespace {

inline uint8_t to_u8(float v) { return static_cast<uint8_t>(std::lround(clampf(v, 0.f, 1.f) * 255.f)); }

// Coordinates this large are treated as "off screen"; protects float->int conversions.
constexpr float kMaxCoord = 1.0e6f;

inline bool sane(float v) { return std::isfinite(v) && std::abs(v) < kMaxCoord; }

inline uint8_t mix(uint8_t src, uint8_t dst, uint32_t a) {
    return static_cast<uint8_t>((src * a + dst * (255u - a) + 127u) / 255u);
}

}  // namespace

SoftwareRenderer::SoftwareRenderer(int w, int h) : fb_(w, h, 0x000000FFu) {}

TextureId SoftwareRenderer::create_texture(const Image& img) {
    textures_.push_back(img);
    return static_cast<TextureId>(textures_.size());
}

void SoftwareRenderer::destroy_texture(TextureId id) {
    if (id >= 1 && id <= textures_.size()) textures_[id - 1] = Image();
}

void SoftwareRenderer::begin_frame(Color c) {
    uint8_t r = to_u8(c.r), g = to_u8(c.g), b = to_u8(c.b);
    uint8_t* p = fb_.pixels.data();
    for (size_t i = 0, n = fb_.pixels.size(); i < n; i += 4) {
        p[i] = r;
        p[i + 1] = g;
        p[i + 2] = b;
        p[i + 3] = 255;
    }
}

void SoftwareRenderer::blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= fb_.width || y >= fb_.height || a == 0) return;
    uint8_t* d = fb_.px(x, y);
    if (a == 255) {
        d[0] = r;
        d[1] = g;
        d[2] = b;
    } else {
        d[0] = mix(r, d[0], a);
        d[1] = mix(g, d[1], a);
        d[2] = mix(b, d[2], a);
    }
    d[3] = 255;
}

void SoftwareRenderer::blend_span(int y, int x0, int x1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (y < 0 || y >= fb_.height || a == 0) return;
    x0 = std::max(x0, 0);
    x1 = std::min(x1, fb_.width);
    if (x0 >= x1) return;
    uint8_t* d = fb_.px(x0, y);
    if (a == 255) {
        for (int x = x0; x < x1; ++x, d += 4) {
            d[0] = r;
            d[1] = g;
            d[2] = b;
            d[3] = 255;
        }
    } else {
        for (int x = x0; x < x1; ++x, d += 4) {
            d[0] = mix(r, d[0], a);
            d[1] = mix(g, d[1], a);
            d[2] = mix(b, d[2], a);
            d[3] = 255;
        }
    }
}

void SoftwareRenderer::fill_rect(Rect r, Color c) {
    if (!sane(r.min.x) || !sane(r.min.y) || !sane(r.max.x) || !sane(r.max.y)) return;
    int x0 = static_cast<int>(std::lround(r.min.x)), x1 = static_cast<int>(std::lround(r.max.x));
    int y0 = static_cast<int>(std::lround(r.min.y)), y1 = static_cast<int>(std::lround(r.max.y));
    if (x1 == x0 && r.max.x > r.min.x) ++x1;  // thin shapes stay visible
    if (y1 == y0 && r.max.y > r.min.y) ++y1;
    uint8_t cr = to_u8(c.r), cg = to_u8(c.g), cb = to_u8(c.b), ca = to_u8(c.a);
    y0 = std::max(y0, 0);
    y1 = std::min(y1, fb_.height);
    for (int y = y0; y < y1; ++y) blend_span(y, x0, x1, cr, cg, cb, ca);
}

void SoftwareRenderer::draw_line(Vec2 a, Vec2 b, Color c) {
    if (!sane(a.x) || !sane(a.y) || !sane(b.x) || !sane(b.y)) return;
    if ((a.x < 0 && b.x < 0) || (a.y < 0 && b.y < 0) || (a.x >= fb_.width && b.x >= fb_.width) ||
        (a.y >= fb_.height && b.y >= fb_.height))
        return;
    int x0 = static_cast<int>(std::lround(a.x)), y0 = static_cast<int>(std::lround(a.y));
    int x1 = static_cast<int>(std::lround(b.x)), y1 = static_cast<int>(std::lround(b.y));
    uint8_t cr = to_u8(c.r), cg = to_u8(c.g), cb = to_u8(c.b), ca = to_u8(c.a);
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blend_pixel(x0, y0, cr, cg, cb, ca);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void SoftwareRenderer::fill_circle(Vec2 c, float radius, Color col) {
    if (!sane(c.x) || !sane(c.y) || !sane(radius) || radius <= 0.f) return;
    uint8_t cr = to_u8(col.r), cg = to_u8(col.g), cb = to_u8(col.b), ca = to_u8(col.a);
    if (radius < 0.75f) {
        blend_pixel(static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(c.y)), cr, cg, cb, ca);
        return;
    }
    int ymin = std::max(0, static_cast<int>(std::floor(c.y - radius)));
    int ymax = std::min(fb_.height - 1, static_cast<int>(std::ceil(c.y + radius)));
    for (int y = ymin; y <= ymax; ++y) {
        float dy = (static_cast<float>(y) + 0.5f) - c.y;
        float rem = radius * radius - dy * dy;
        if (rem <= 0.f) continue;
        float half = std::sqrt(rem);
        blend_span(y, static_cast<int>(std::lround(c.x - half)), static_cast<int>(std::lround(c.x + half)), cr, cg,
                   cb, ca);
    }
}

void SoftwareRenderer::draw_texture(TextureId id, IRect src, Rect dst, Color tint, bool flip_x, bool flip_y) {
    if (id < 1 || id > textures_.size()) return;
    const Image& tex = textures_[id - 1];
    if (tex.empty()) return;
    if (!sane(dst.min.x) || !sane(dst.min.y) || !sane(dst.max.x) || !sane(dst.max.y)) return;

    if (src.w <= 0 || src.h <= 0) src = {0, 0, tex.width, tex.height};
    int sx0 = std::max(src.x, 0), sy0 = std::max(src.y, 0);
    int sx1 = std::min(src.x + src.w, tex.width), sy1 = std::min(src.y + src.h, tex.height);
    int sw = sx1 - sx0, sh = sy1 - sy0;
    if (sw <= 0 || sh <= 0) return;

    int x0 = static_cast<int>(std::lround(dst.min.x)), x1 = static_cast<int>(std::lround(dst.max.x));
    int y0 = static_cast<int>(std::lround(dst.min.y)), y1 = static_cast<int>(std::lround(dst.max.y));
    int dw = x1 - x0, dh = y1 - y0;
    if (dw <= 0 || dh <= 0) return;

    const uint32_t tr = to_u8(tint.r), tg = to_u8(tint.g), tb = to_u8(tint.b), ta = to_u8(tint.a);
    const int cx0 = std::max(x0, 0), cx1 = std::min(x1, fb_.width);
    const int cy0 = std::max(y0, 0), cy1 = std::min(y1, fb_.height);
    if (cx0 >= cx1 || cy0 >= cy1) return;

    for (int y = cy0; y < cy1; ++y) {
        int sy = static_cast<int>((static_cast<int64_t>(y - y0) * sh) / dh);
        if (flip_y) sy = sh - 1 - sy;
        const uint8_t* row = tex.px(sx0, sy0 + sy);
        for (int x = cx0; x < cx1; ++x) {
            int sx = static_cast<int>((static_cast<int64_t>(x - x0) * sw) / dw);
            if (flip_x) sx = sw - 1 - sx;
            const uint8_t* t = row + sx * 4;
            uint32_t a = (t[3] * ta + 127u) / 255u;
            if (a == 0) continue;
            blend_pixel(x, y, static_cast<uint8_t>((t[0] * tr + 127u) / 255u),
                        static_cast<uint8_t>((t[1] * tg + 127u) / 255u),
                        static_cast<uint8_t>((t[2] * tb + 127u) / 255u), static_cast<uint8_t>(a));
        }
    }
}

}  // namespace prism
