#pragma once
// Prism core value types: Vec2, Rect, Color, Rng and small math helpers.
// No dependencies beyond the C++ standard library.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism {

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float signf(float v) { return v < 0.f ? -1.f : (v > 0.f ? 1.f : 0.f); }

// Move `v` toward `target` by at most `step` (never overshoots).
inline float approach(float v, float target, float step) {
    return v < target ? std::min(v + step, target) : std::max(v - step, target);
}

// Frame-rate independent exponential smoothing. Higher lambda = snappier.
inline float damp(float a, float b, float lambda, float dt) {
    return lerpf(a, b, 1.f - std::exp(-lambda * dt));
}

struct Vec2 {
    float x = 0.f, y = 0.f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

    constexpr float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    constexpr float cross(const Vec2& o) const { return x * o.y - y * o.x; }
    constexpr float length_sq() const { return x * x + y * y; }
    float length() const { return std::sqrt(length_sq()); }
    float distance_to(const Vec2& o) const { return (*this - o).length(); }
    constexpr Vec2 perp() const { return {-y, x}; }
    Vec2 abs() const { return {std::abs(x), std::abs(y)}; }
    Vec2 normalized() const {
        float l = length();
        return l > 1e-6f ? (*this / l) : Vec2{0.f, 0.f};
    }
    static Vec2 from_angle(float radians) { return {std::cos(radians), std::sin(radians)}; }
    static constexpr Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
    static Vec2 damp(const Vec2& a, const Vec2& b, float lambda, float dt) {
        return lerp(a, b, 1.f - std::exp(-lambda * dt));
    }
};

constexpr Vec2 operator*(float s, const Vec2& v) { return v * s; }

// Axis-aligned rectangle in whatever space the caller is using.
struct Rect {
    Vec2 min, max;

    static constexpr Rect from_center(Vec2 c, Vec2 half) { return {c - half, c + half}; }
    constexpr Vec2 center() const { return (min + max) * 0.5f; }
    constexpr Vec2 size() const { return max - min; }
    constexpr bool overlaps(const Rect& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y;
    }
    constexpr bool contains(Vec2 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    constexpr Rect expanded(float m) const { return {{min.x - m, min.y - m}, {max.x + m, max.y + m}}; }
};

// Integer rectangle, used for texture source regions (pixels).
struct IRect {
    int x = 0, y = 0, w = 0, h = 0;
};

using TextureId = uint32_t;  // 0 = "no texture"

struct Color {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr Color from_u8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
    }
    // 0xRRGGBB
    static constexpr Color from_hex(uint32_t rgb, float a = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
    }
    constexpr Color with_alpha(float na) const { return {r, g, b, na}; }
    static constexpr Color lerp(const Color& a, const Color& b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }
};

// PCG32: small, fast, deterministic across platforms.
class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        next_u32();
        state_ += seed;
        next_u32();
    }
    uint32_t next_u32() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }
    float next01() { return static_cast<float>(next_u32() >> 8) * (1.0f / 16777216.0f); }  // [0,1)
    float range(float a, float b) { return a + (b - a) * next01(); }
    int range_int(int a, int b) {  // inclusive
        return a + static_cast<int>(next_u32() % static_cast<uint32_t>(b - a + 1));
    }

private:
    uint64_t state_, inc_;
};

}  // namespace prism
