#pragma once
// Prism Arena: procedural pixel art (no asset files). Everything is drawn at half resolution and
// shown at 2x, matching the platformer's look. Row N of kEnemies in arena_data.hpp uses
// make_enemy_sheet(N).
//
// The renderer has no rotation, so the player is drawn once per aiming direction (16 frames);
// choosing the frame nearest the aim angle looks like smooth rotation.
#include <algorithm>
#include <cmath>

#include "arena/arena_data.hpp"
#include "prism/image.hpp"

namespace arena::art {

using prism::Image;
using prism::PI;
using prism::rgba;
using prism::TAU;

constexpr int kPlayerFrames = 16;  // aiming directions
constexpr int kPlayerPx = 16;      // frame size in art pixels (shown at 2x)
constexpr int kEnemyPx = 24;
constexpr int kEnemyFrames = 4;    // 0,1 = walk, 2 = hit flash, 3 = wind-up
constexpr int kPickupPx = 12;

enum EnemyFrame { EF_Walk0 = 0, EF_Walk1 = 1, EF_Flash = 2, EF_Windup = 3 };

inline uint32_t rgb(uint32_t hex, uint8_t a = 255) {
    return rgba(static_cast<uint8_t>(hex >> 16), static_cast<uint8_t>(hex >> 8), static_cast<uint8_t>(hex), a);
}
inline uint32_t scale(uint32_t c, float k) {  // multiply RGB, keep alpha
    auto ch = [&](int sh) { return static_cast<uint8_t>(std::clamp(static_cast<int>(((c >> sh) & 0xFF) * k), 0, 255)); };
    return rgba(ch(24), ch(16), ch(8), static_cast<uint8_t>(c & 0xFF));
}
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int sh) {
        float x = static_cast<float>((a >> sh) & 0xFF), y = static_cast<float>((b >> sh) & 0xFF);
        return static_cast<uint8_t>(std::clamp(static_cast<int>(x + (y - x) * t), 0, 255));
    };
    return rgba(ch(24), ch(16), ch(8), static_cast<uint8_t>(a & 0xFF));
}

// Alpha-blend a colour over what is already there (used for shadows and glows).
inline void blend(Image& im, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= im.width || y >= im.height) return;
    const uint32_t a = c & 0xFF;
    if (a == 255) return im.set_pixel(x, y, c);
    uint32_t d = im.get_pixel(x, y);
    const uint32_t da = d & 0xFF;
    const uint32_t oa = a + da * (255 - a) / 255;
    if (oa == 0) return;
    auto ch = [&](int sh) {
        uint32_t s = (c >> sh) & 0xFF, t = (d >> sh) & 0xFF;
        return static_cast<uint8_t>((s * a + t * da * (255 - a) / 255) / oa);
    };
    im.set_pixel(x, y, rgba(ch(24), ch(16), ch(8), static_cast<uint8_t>(oa)));
}

inline void disc(Image& im, float cx, float cy, float r, uint32_t c) {
    for (int y = static_cast<int>(std::floor(cy - r)); y <= static_cast<int>(std::ceil(cy + r)); ++y)
        for (int x = static_cast<int>(std::floor(cx - r)); x <= static_cast<int>(std::ceil(cx + r)); ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) blend(im, x, y, c);
        }
}
inline void ellipse(Image& im, float cx, float cy, float rx, float ry, uint32_t c) {
    for (int y = static_cast<int>(std::floor(cy - ry)); y <= static_cast<int>(std::ceil(cy + ry)); ++y)
        for (int x = static_cast<int>(std::floor(cx - rx)); x <= static_cast<int>(std::ceil(cx + rx)); ++x) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.f) blend(im, x, y, c);
        }
}
inline void ring(Image& im, float cx, float cy, float r_out, float r_in, uint32_t c) {
    for (int y = static_cast<int>(std::floor(cy - r_out)); y <= static_cast<int>(std::ceil(cy + r_out)); ++y)
        for (int x = static_cast<int>(std::floor(cx - r_out)); x <= static_cast<int>(std::ceil(cx + r_out)); ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 <= r_out * r_out && d2 >= r_in * r_in) blend(im, x, y, c);
        }
}
inline void thick_line(Image& im, float x0, float y0, float x1, float y1, float half_w, uint32_t c) {
    const float len = std::max(1.f, std::hypot(x1 - x0, y1 - y0));
    for (float t = 0.f; t <= len; t += 0.4f) {
        float k = t / len;
        disc(im, x0 + (x1 - x0) * k, y0 + (y1 - y0) * k, half_w, c);
    }
}

// ───────────────────────────── player ─────────────────────────────

inline Image make_player_sheet() {
    Image im(kPlayerPx * kPlayerFrames, kPlayerPx, 0);
    for (int f = 0; f < kPlayerFrames; ++f) {
        Image fr(kPlayerPx, kPlayerPx, 0);
        const float a = TAU * static_cast<float>(f) / kPlayerFrames;
        const float dx = std::cos(a), dy = std::sin(a);
        const float cx = 8.f, cy = 7.5f;
        ellipse(fr, cx, cy + 3.2f, 5.6f, 2.4f, rgba(0, 0, 0, 90));          // shadow
        thick_line(fr, cx + dx * 3.f, cy + dy * 3.f, cx + dx * 7.2f, cy + dy * 7.2f, 1.25f, rgb(0x0B2A40));  // barrel outline
        disc(fr, cx, cy, 5.6f, rgb(0x0B3D5C));                                // body outline
        disc(fr, cx, cy, 4.6f, rgb(0x2EA8FF));                                // body
        disc(fr, cx - 1.3f, cy - 1.5f, 2.2f, rgb(0x6CC8FF));                  // highlight (light from top-left)
        thick_line(fr, cx + dx * 3.f, cy + dy * 3.f, cx + dx * 7.f, cy + dy * 7.f, 0.8f, rgb(0xDDE8F2));   // barrel
        disc(fr, cx + dx * 2.3f, cy + dy * 2.3f, 1.4f, rgb(0xE8FBFF));        // visor
        disc(fr, cx + dx * 2.6f, cy + dy * 2.6f, 0.7f, rgb(0x12304A));
        for (int y = 0; y < kPlayerPx; ++y)
            for (int x = 0; x < kPlayerPx; ++x) im.set_pixel(f * kPlayerPx + x, y, fr.get_pixel(x, y));
    }
    return im;
}

inline int player_frame_for(float angle) {  // angle in radians, 0 = +X, clockwise on screen
    int f = static_cast<int>(std::lround(angle / (TAU / kPlayerFrames)));
    return ((f % kPlayerFrames) + kPlayerFrames) % kPlayerFrames;
}

// ───────────────────────────── enemies ─────────────────────────────

inline Image make_enemy_sheet(int type) {
    const EnemyDef& def = kEnemies[type];
    const float R = def.radius * 0.5f;  // art pixels
    const uint32_t base = rgb(def.color);
    const uint32_t outline = scale(base, 0.38f);
    Image im(kEnemyPx * kEnemyFrames, kEnemyPx, 0);

    for (int f = 0; f < kEnemyFrames; ++f) {
        Image fr(kEnemyPx, kEnemyPx, 0);
        const float cx = kEnemyPx * 0.5f;
        const float bob = (f == EF_Walk1) ? -0.8f : 0.f;
        const float cy = kEnemyPx * 0.5f - 1.f + bob;
        const bool flash = f == EF_Flash, windup = f == EF_Windup;
        uint32_t body = flash ? rgb(0xFFFFFF) : (windup ? mix(base, rgb(0xFFFFFF), 0.55f) : base);
        uint32_t edge = flash ? rgb(0xFFFFFF) : (windup ? rgb(0xFFF6B0) : outline);
        const uint32_t dark = scale(base, 0.6f);

        ellipse(fr, cx, kEnemyPx * 0.5f - 1.f + R * 0.62f, R * 0.95f, R * 0.42f, rgba(0, 0, 0, 90));  // ground shadow

        switch (type) {
            case 0: {  // DRONE: round body, little legs, two eyes
                const float l = (f == EF_Walk1) ? 1.f : 0.f;
                for (int i = 0; i < 4; ++i) {
                    float ang = PI * 0.15f + i * PI * 0.23f + l * 0.15f;
                    thick_line(fr, cx + std::cos(ang) * R * 0.7f, cy + std::sin(ang) * R * 0.8f,
                               cx + std::cos(ang) * (R + 1.8f), cy + std::sin(ang) * (R + 1.5f), 0.45f, edge);
                }
                disc(fr, cx, cy, R + 0.8f, edge);
                disc(fr, cx, cy, R, body);
                disc(fr, cx - R * 0.3f, cy - R * 0.35f, R * 0.35f, flash ? body : mix(body, rgb(0xFFFFFF), 0.35f));
                disc(fr, cx - 1.7f, cy + 0.4f, 1.1f, rgb(0xFFFFFF));
                disc(fr, cx + 1.7f, cy + 0.4f, 1.1f, rgb(0xFFFFFF));
                disc(fr, cx - 1.7f, cy + 0.8f, 0.55f, rgb(0x200008));
                disc(fr, cx + 1.7f, cy + 0.8f, 0.55f, rgb(0x200008));
                break;
            }
            case 1: {  // BRUTE: heavy armoured body with spikes
                for (int i = 0; i < 8; ++i) {
                    float ang = i * TAU / 8.f + PI / 8.f;
                    thick_line(fr, cx + std::cos(ang) * R * 0.8f, cy + std::sin(ang) * R * 0.8f,
                               cx + std::cos(ang) * (R + 1.8f), cy + std::sin(ang) * (R + 1.8f), 0.9f, edge);
                }
                disc(fr, cx, cy, R + 0.9f, edge);
                disc(fr, cx, cy, R, body);
                ring(fr, cx, cy, R * 0.82f, R * 0.62f, flash ? body : dark);          // armour band
                disc(fr, cx - R * 0.3f, cy - R * 0.4f, R * 0.28f, flash ? body : mix(body, rgb(0xFFFFFF), 0.3f));
                thick_line(fr, cx - 3.2f, cy + 0.2f, cx - 1.0f, cy + 1.2f, 0.55f, flash ? edge : rgb(0xFFE14D));  // angry eyes
                thick_line(fr, cx + 3.2f, cy + 0.2f, cx + 1.0f, cy + 1.2f, 0.55f, flash ? edge : rgb(0xFFE14D));
                break;
            }
            case 2: {  // GUNNER: eye + two cannon nubs
                const float n = (f == EF_Walk1) ? 0.9f : 0.f;
                thick_line(fr, cx - R - 0.5f + n, cy + 0.5f, cx - R - 2.2f + n, cy + 0.5f, 1.0f, edge);
                thick_line(fr, cx + R + 0.5f - n, cy + 0.5f, cx + R + 2.2f - n, cy + 0.5f, 1.0f, edge);
                disc(fr, cx, cy, R + 0.8f, edge);
                disc(fr, cx, cy, R, body);
                disc(fr, cx - R * 0.35f, cy - R * 0.4f, R * 0.3f, flash ? body : mix(body, rgb(0xFFFFFF), 0.35f));
                disc(fr, cx, cy + 0.3f, 2.3f, flash ? edge : rgb(0xF4FFF8));
                disc(fr, cx, cy + 0.6f, 1.2f, flash ? body : rgb(0xFF2030));
                break;
            }
            case 3: {  // SPLITTER: a blob made of cells
                const float w = (f == EF_Walk1) ? 0.6f : -0.2f;
                disc(fr, cx, cy, R + 0.9f, edge);
                disc(fr, cx, cy, R, body);
                for (int i = 0; i < 3; ++i) {
                    float ang = i * TAU / 3.f + PI / 6.f + w * 0.3f;
                    disc(fr, cx + std::cos(ang) * R * 0.5f, cy + std::sin(ang) * R * 0.5f + w * 0.3f, R * 0.30f,
                         flash ? body : mix(body, rgb(0xFFFFFF), 0.4f));
                    disc(fr, cx + std::cos(ang) * R * 0.5f, cy + std::sin(ang) * R * 0.5f + w * 0.3f, R * 0.12f,
                         flash ? edge : dark);
                }
                break;
            }
            default: {  // DASHER: a diamond with a centre eye; spikes flex while walking
                const float s = (f == EF_Walk1) ? 1.4f : 0.6f;
                const float RR = R + 1.4f;
                for (int y = 0; y < kEnemyPx; ++y)
                    for (int x = 0; x < kEnemyPx; ++x) {
                        float dx = std::abs(x + 0.5f - cx), dy = std::abs(y + 0.5f - cy);
                        if (dx + dy <= RR + 0.8f) blend(fr, x, y, edge);
                    }
                for (int y = 0; y < kEnemyPx; ++y)
                    for (int x = 0; x < kEnemyPx; ++x) {
                        float dx = std::abs(x + 0.5f - cx), dy = std::abs(y + 0.5f - cy);
                        if (dx + dy <= RR) blend(fr, x, y, body);
                    }
                thick_line(fr, cx, cy - RR, cx, cy - RR - s, 0.5f, edge);
                thick_line(fr, cx, cy + RR, cx, cy + RR + s, 0.5f, edge);
                thick_line(fr, cx - RR, cy, cx - RR - s, cy, 0.5f, edge);
                thick_line(fr, cx + RR, cy, cx + RR + s, cy, 0.5f, edge);
                disc(fr, cx, cy, 2.0f, flash ? edge : rgb(0x2A2200));
                disc(fr, cx, cy, 0.9f, windup ? rgb(0xFF2030) : (flash ? body : rgb(0xFFF6B0)));
                break;
            }
        }
        for (int y = 0; y < kEnemyPx; ++y)
            for (int x = 0; x < kEnemyPx; ++x) im.set_pixel(f * kEnemyPx + x, y, fr.get_pixel(x, y));
    }
    return im;
}

// ───────────────────────────── projectiles, pickups, markers ─────────────────────────────

inline Image make_bullet(bool friendly) {
    Image im(8, 8, 0);
    const uint32_t c = friendly ? rgb(0x4DD2FF) : rgb(0xFF6A3D);
    disc(im, 4.f, 4.f, 3.9f, (c & 0xFFFFFF00u) | 70u);   // soft outer glow
    disc(im, 4.f, 4.f, 3.0f, (c & 0xFFFFFF00u) | 170u);
    disc(im, 4.f, 4.f, 2.0f, rgb(0xFFFFFF));
    return im;
}

// Two frames side by side (the second has a brighter rim) so the Animator makes it pulse.
inline Image make_pickup(int kind) {
    const uint32_t col = rgb(kPickups[kind].color);
    Image im(kPickupPx * 2, kPickupPx, 0);
    for (int f = 0; f < 2; ++f) {
        Image fr(kPickupPx, kPickupPx, 0);
        const float c = kPickupPx * 0.5f;
        disc(fr, c, c, 5.9f, rgba(10, 14, 24, 235));
        ring(fr, c, c, 5.9f, 4.7f, f ? mix(col, rgb(0xFFFFFF), 0.5f) : col);
        auto px = [&](int x, int y, uint32_t cc) { fr.set_pixel(x, y, cc); };
        switch (kind) {
            case PK_Health:  // plus
                for (int i = 3; i <= 8; ++i) { px(i, 5, col); px(i, 6, col); px(5, i, col); px(6, i, col); }
                break;
            case PK_Rapid:  // two chevrons
                for (int i = 0; i < 3; ++i) {
                    px(3 + i, 3 + i, col); px(3 + i, 8 - i, col);
                    px(6 + i, 3 + i, col); px(6 + i, 8 - i, col);
                }
                break;
            case PK_Spread:  // three rays fanning out from one point
                thick_line(fr, 3.2f, 6.f, 8.6f, 3.0f, 0.45f, col);
                thick_line(fr, 3.2f, 6.f, 9.0f, 6.0f, 0.45f, col);
                thick_line(fr, 3.2f, 6.f, 8.6f, 9.0f, 0.45f, col);
                break;
            default:  // nova: ring with centre dot
                ring(fr, c, c, 3.2f, 2.0f, col);
                px(5, 5, rgb(0xFFFFFF)); px(6, 5, rgb(0xFFFFFF)); px(5, 6, rgb(0xFFFFFF)); px(6, 6, rgb(0xFFFFFF));
                break;
        }
        for (int y = 0; y < kPickupPx; ++y)
            for (int x = 0; x < kPickupPx; ++x) im.set_pixel(f * kPickupPx + x, y, fr.get_pixel(x, y));
    }
    return im;
}

inline Image make_spawn_marker() {  // a red targeting ring; the game pulses its size
    Image im(16, 16, 0);
    ring(im, 8.f, 8.f, 7.4f, 6.0f, rgba(255, 70, 90, 230));
    for (int i = 0; i < 4; ++i) {
        float a = i * TAU / 4.f;
        thick_line(im, 8.f + std::cos(a) * 3.f, 8.f + std::sin(a) * 3.f, 8.f + std::cos(a) * 5.f, 8.f + std::sin(a) * 5.f, 0.5f, rgba(255, 120, 130, 230));
    }
    disc(im, 8.f, 8.f, 1.0f, rgba(255, 200, 205, 240));
    return im;
}

// ───────────────────────────── tiles ─────────────────────────────

constexpr int kFloorVariants = 4;

inline Image make_floor_tile(int variant) {
    Image im(16, 16);
    prism::Rng rng(100 + static_cast<uint64_t>(variant));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            float k = 1.f + (rng.next01() - 0.5f) * 0.07f;
            uint32_t c = scale(rgb((x == 0 || y == 0) ? 0x242E4A : 0x1B2339), k);
            im.set_pixel(x, y, c);
        }
    const uint32_t mark = rgb(0x26314F);
    if (variant == 1) {  // small cross
        for (int i = 6; i <= 9; ++i) { im.set_pixel(i, 8, mark); im.set_pixel(8, i, mark); }
    } else if (variant == 2) {  // hatch
        for (int i = 3; i < 13; i += 3) { im.set_pixel(i, i, mark); im.set_pixel(i + 1, i, mark); }
    } else if (variant == 3) {  // dots
        im.set_pixel(4, 4, mark); im.set_pixel(11, 4, mark); im.set_pixel(4, 11, mark); im.set_pixel(11, 11, mark);
    }
    return im;
}

inline Image make_wall_tile() {
    Image im(16, 16);
    prism::Rng rng(7);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            float k = 1.f + (rng.next01() - 0.5f) * 0.06f;
            uint32_t c = rgb(0x3E4764);
            if (x == 0 || y == 0) c = rgb(0x6A769A);        // lit top/left bevel
            else if (x == 15 || y == 15) c = rgb(0x262C42);  // shaded bottom/right
            else if (x >= 3 && x <= 12 && y >= 3 && y <= 12) c = rgb(0x333B56);  // inset panel
            im.set_pixel(x, y, scale(c, k));
        }
    for (int cy : {2, 13})
        for (int cx : {2, 13}) im.set_pixel(cx, cy, rgb(0x8D99BD));  // rivets
    return im;
}

}  // namespace arena::art
