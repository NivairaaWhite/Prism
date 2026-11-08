#pragma once
#include "types.hpp"

namespace prism {

// 2D camera: world<->screen mapping, smooth follow, bounds clamping, trauma-based shake.
struct Camera2D {
    Vec2 position{0, 0};      // world point at the center of the screen
    float zoom = 1.f;
    Vec2 viewport{1280, 720};  // screen size in pixels (set by the engine)

    bool use_bounds = false;
    Rect bounds{};  // world rectangle the view is kept inside (centered if smaller than the view)

    float trauma = 0.f;         // 0..1; shake strength is trauma^2
    float trauma_decay = 1.6f;  // trauma lost per second
    float max_shake = 14.f;     // pixels at trauma == 1
    Vec2 shake_offset{0, 0};

    void snap_to(Vec2 p) {
        position = p;
        clamp_to_bounds();
    }
    // Frame-rate independent ease toward target. lambda ~ 4 (lazy) .. 15 (tight).
    void follow(Vec2 target, float lambda, float dt) {
        position = Vec2::damp(position, target, lambda, dt);
        clamp_to_bounds();
    }
    void add_trauma(float amount) { trauma = clampf(trauma + amount, 0.f, 1.f); }
    void update(float dt);

    Vec2 effective_position() const { return position + shake_offset; }
    Vec2 world_to_screen(Vec2 w) const { return (w - effective_position()) * zoom + viewport * 0.5f; }
    Vec2 screen_to_world(Vec2 s) const { return (s - viewport * 0.5f) / zoom + effective_position(); }
    Rect visible_rect() const {
        Vec2 half = viewport * (0.5f / zoom);
        return Rect::from_center(effective_position(), half);
    }

private:
    void clamp_to_bounds();
    Rng rng_{0xCA3E4Au};
};

}  // namespace prism
