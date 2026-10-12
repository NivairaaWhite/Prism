#pragma once
#include <vector>

#include "camera.hpp"
#include "renderer.hpp"

namespace prism {

struct ParticleBurst {
    Vec2 position{0, 0};
    int count = 12;
    float speed_min = 40.f, speed_max = 180.f;
    float angle_min = 0.f, angle_max = TAU;  // radians; 0 = +X, PI/2 = down (+Y is down)
    float life_min = 0.3f, life_max = 0.8f;
    float size_start = 6.f, size_end = 0.f;
    Color color_start{1, 1, 1, 1};
    Color color_end{1, 1, 1, 0};
    float gravity = 300.f;
    float drag = 0.f;  // 1/s velocity damping
    bool circle = false;
};

// Fixed-capacity CPU particle pool. Deterministic (own RNG). Extra particles beyond the
// capacity are dropped.
class ParticleSystem {
public:
    explicit ParticleSystem(size_t capacity = 4096, uint64_t seed = 7) : capacity_(capacity), rng_(seed) {
        items_.reserve(capacity);
    }
    void emit(const ParticleBurst& b);
    void update(float dt);
    void render(Renderer& r, const Camera2D& cam) const;
    void clear() { items_.clear(); }
    size_t alive() const { return items_.size(); }

private:
    struct Particle {
        Vec2 pos, vel;
        float life, max_life, size0, size1, gravity, drag;
        Color c0, c1;
        bool circle;
    };
    size_t capacity_;
    Rng rng_;
    std::vector<Particle> items_;
};

}  // namespace prism
