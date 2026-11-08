#include "prism/animation.hpp"

#include <cmath>

namespace prism {

void Animator::advance(float dt) {
    if (clips.empty()) return;
    if (current < 0) play(clips.front().name);
    if (current >= static_cast<int>(clips.size())) current = 0;  // clips were edited after play()
    const AnimClip& c = clips[current];
    if (!finished && std::isfinite(dt) && dt > 0.f) time += dt;
    const int n = std::max(1, c.last - c.first + 1);
    const float pos = time * c.fps;
    int idx = std::isfinite(pos) && pos > 0.f ? static_cast<int>(std::min(pos, 1.0e9f)) : 0;
    if (c.loop) {
        idx %= n;
    } else if (idx >= n) {
        idx = n - 1;
        finished = true;
    }
    frame = c.first + idx;
}

void update_animations(World& world, float dt) {
    world.view<Animator, Sprite>().each([&](Entity, Animator& a, Sprite& s) {
        a.advance(dt);
        if (!a.clips.empty()) s.src = a.source_rect();
    });
}

}  // namespace prism
