#include "prism/camera.hpp"

namespace prism {

void Camera2D::clamp_to_bounds() {
    if (!use_bounds) return;
    Vec2 half = viewport * (0.5f / zoom);
    Vec2 size = bounds.size();
    position.x = size.x <= half.x * 2.f ? bounds.center().x : clampf(position.x, bounds.min.x + half.x, bounds.max.x - half.x);
    position.y = size.y <= half.y * 2.f ? bounds.center().y : clampf(position.y, bounds.min.y + half.y, bounds.max.y - half.y);
}

void Camera2D::update(float dt) {
    trauma = std::max(0.f, trauma - trauma_decay * dt);
    if (trauma > 0.f) {
        float s = trauma * trauma * max_shake;
        shake_offset = {rng_.range(-1.f, 1.f) * s, rng_.range(-1.f, 1.f) * s};
    } else {
        shake_offset = {0, 0};
    }
    clamp_to_bounds();
}

}  // namespace prism
