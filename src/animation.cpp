#include "prism/animation.hpp"

#include <cmath>

namespace prism {

void Animator::advance(float dt) {
    if (clips.empty()) return;
    if (current < 0) play(clips.front().name);
    const AnimClip& c = clips[current];
    if (!finished) time += dt;
    const int n = std::max(1, c.last - c.first + 1);
    int idx = static_cast<int>(std::floor(time * c.fps));
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
