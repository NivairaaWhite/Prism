#include "prism/particles.hpp"

namespace prism {

void ParticleSystem::emit(const ParticleBurst& b) {
    for (int i = 0; i < b.count && items_.size() < capacity_; ++i) {
        float ang = rng_.range(b.angle_min, b.angle_max);
        float spd = rng_.range(b.speed_min, b.speed_max);
        float life = rng_.range(b.life_min, b.life_max);
        items_.push_back({b.position, Vec2::from_angle(ang) * spd, life, life, b.size_start, b.size_end, b.gravity,
                          b.drag, b.color_start, b.color_end, b.circle});
    }
}

void ParticleSystem::update(float dt) {
    for (size_t i = 0; i < items_.size();) {
        Particle& p = items_[i];
        p.life -= dt;
        if (p.life <= 0.f) {
            p = items_.back();  // swap-remove
            items_.pop_back();
            continue;
        }
        p.vel.y += p.gravity * dt;
        if (p.drag > 0.f) p.vel *= 1.f / (1.f + p.drag * dt);
        p.pos += p.vel * dt;
        ++i;
    }
}

void ParticleSystem::render(Renderer& r, const Camera2D& cam) const {
    const Rect view = cam.visible_rect().expanded(32.f);
    for (const Particle& p : items_) {
        if (!view.contains(p.pos)) continue;
        float t = 1.f - p.life / p.max_life;
        float size = lerpf(p.size0, p.size1, t) * cam.zoom;
        if (size <= 0.f) continue;
        Color c = Color::lerp(p.c0, p.c1, t);
        Vec2 s = cam.world_to_screen(p.pos);
        if (p.circle)
            r.fill_circle(s, size * 0.5f, c);
        else
            r.fill_rect({s - Vec2{size, size} * 0.5f, s + Vec2{size, size} * 0.5f}, c);
    }
}

}  // namespace prism
