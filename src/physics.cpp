#include "prism/physics.hpp"

#include <algorithm>
#include <cmath>

namespace prism {
namespace {

struct Manifold {
    Vec2 normal;  // from A toward B
    float depth = 0.f;
};

inline float sgn1(float v) { return v < 0.f ? -1.f : 1.f; }
inline Vec2 center_of(const Transform& t, const Collider& c) { return t.position + c.offset; }
inline float half_height(const Collider& c) { return c.shape == Shape::Box ? c.half_extents.y : c.radius; }
inline Vec2 half_vec(const Collider& c) {
    return c.shape == Shape::Box ? c.half_extents : Vec2{c.radius, c.radius};
}
inline int64_t cell_key(int x, int y) {
    return (static_cast<int64_t>(x) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(y));
}

bool box_box(Vec2 ca, Vec2 ha, Vec2 cb, Vec2 hb, float margin, Manifold& m) {
    Vec2 d = cb - ca;
    float ox = ha.x + hb.x - std::abs(d.x);
    float oy = ha.y + hb.y - std::abs(d.y);
    if (ox <= -margin || oy <= -margin) return false;
    if (ox < oy) {
        m.normal = {sgn1(d.x), 0.f};
        m.depth = ox;
    } else {
        m.normal = {0.f, sgn1(d.y)};
        m.depth = oy;
    }
    return true;
}

bool circle_circle(Vec2 ca, float ra, Vec2 cb, float rb, float margin, Manifold& m) {
    Vec2 d = cb - ca;
    float r = ra + rb, lim = r + margin;
    float d2 = d.length_sq();
    if (d2 >= lim * lim) return false;
    float dist = std::sqrt(d2);
    m.normal = dist > 1e-6f ? d / dist : Vec2{0.f, -1.f};
    m.depth = r - dist;
    return true;
}

// Box A, circle B. Normal points from the box toward the circle.
bool box_circle(Vec2 cbox, Vec2 h, Vec2 cc, float r, float margin, Manifold& m) {
    Vec2 lo = cbox - h, hi = cbox + h;
    Vec2 closest{clampf(cc.x, lo.x, hi.x), clampf(cc.y, lo.y, hi.y)};
    Vec2 diff = cc - closest;
    float d2 = diff.length_sq();
    if (d2 > 1e-12f) {
        float lim = r + margin;
        if (d2 >= lim * lim) return false;
        float dist = std::sqrt(d2);
        m.normal = diff / dist;
        m.depth = r - dist;
        return true;
    }
    float dx = h.x - std::abs(cc.x - cbox.x), dy = h.y - std::abs(cc.y - cbox.y);
    if (dx < dy) {
        m.normal = {sgn1(cc.x - cbox.x), 0.f};
        m.depth = dx + r;
    } else {
        m.normal = {0.f, sgn1(cc.y - cbox.y)};
        m.depth = dy + r;
    }
    return true;
}

bool collide(const Collider& a, Vec2 ca, const Collider& b, Vec2 cb, float margin, Manifold& m) {
    if (a.shape == Shape::Box && b.shape == Shape::Box) return box_box(ca, a.half_extents, cb, b.half_extents, margin, m);
    if (a.shape == Shape::Circle && b.shape == Shape::Circle) return circle_circle(ca, a.radius, cb, b.radius, margin, m);
    if (a.shape == Shape::Box) return box_circle(ca, a.half_extents, cb, b.radius, margin, m);
    if (!box_circle(cb, b.half_extents, ca, a.radius, margin, m)) return false;
    m.normal = -m.normal;
    return true;
}

bool ray_box(Vec2 o, Vec2 d, Vec2 lo, Vec2 hi, float max_t, float& t_out, Vec2& n_out) {
    if (o.x >= lo.x && o.x <= hi.x && o.y >= lo.y && o.y <= hi.y) {
        t_out = 0.f;
        n_out = -d;
        return true;
    }
    float tmin = 0.f, tmax = max_t;
    Vec2 n{0.f, 0.f};
    for (int axis = 0; axis < 2; ++axis) {
        float oa = axis ? o.y : o.x, da = axis ? d.y : d.x;
        float la = axis ? lo.y : lo.x, ha = axis ? hi.y : hi.x;
        if (std::abs(da) < 1e-8f) {
            if (oa < la || oa > ha) return false;
            continue;
        }
        float inv = 1.f / da, t1 = (la - oa) * inv, t2 = (ha - oa) * inv, sgn = -1.f;
        if (t1 > t2) {
            std::swap(t1, t2);
            sgn = 1.f;
        }
        if (t1 > tmin) {
            tmin = t1;
            n = axis ? Vec2{0.f, sgn} : Vec2{sgn, 0.f};
        }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    t_out = tmin;
    n_out = n;
    return true;
}

bool ray_circle(Vec2 o, Vec2 d, Vec2 c, float r, float max_t, float& t_out, Vec2& n_out) {
    Vec2 m = o - c;
    float b = m.dot(d), cc = m.length_sq() - r * r;
    if (cc <= 0.f) {
        t_out = 0.f;
        n_out = -d;
        return true;
    }
    if (b > 0.f) return false;
    float disc = b * b - cc;
    if (disc < 0.f) return false;
    float t = -b - std::sqrt(disc);
    if (t < 0.f || t > max_t) return false;
    t_out = t;
    n_out = ((o + d * t) - c) / r;
    return true;
}

}  // namespace

void Physics::gather_bodies(World& world) {
    bodies_.clear();
    world.view<Collider, Transform>().each([&](Entity e, Collider& c, Transform& t) {
        Body b;
        b.e = e;
        b.tf = &t;
        b.col = &c;
        b.rb = world.get<RigidBody>(e);
        b.vel = world.get<Velocity>(e);
        bool dyn = b.rb && b.rb->type == BodyType::Dynamic && b.vel;
        bool kin = b.rb && b.rb->type == BodyType::Kinematic && b.vel;
        b.movable = dyn || kin;
        b.inv_mass = dyn ? 1.f / (b.rb->mass > 0.f ? b.rb->mass : 1.f) : 0.f;
        bodies_.push_back(b);
    });
}

void Physics::integrate(World& world, float h) {
    const float max2 = config.max_speed * config.max_speed;
    world.view<RigidBody, Velocity, Transform>().each([&](Entity, RigidBody& rb, Velocity& v, Transform& t) {
        if (rb.type == BodyType::Static) return;
        if (rb.type == BodyType::Dynamic) {
            v.linear += config.gravity * (rb.gravity_scale * h);
            if (rb.linear_damping > 0.f) v.linear *= 1.f / (1.f + rb.linear_damping * h);
            float s2 = v.linear.length_sq();
            if (s2 > max2) v.linear *= config.max_speed / std::sqrt(s2);
        }
        t.position += v.linear * h;
        t.rotation += v.angular * h;
    });
}

void Physics::build_pairs() {
    solid_pairs_.clear();
    trigger_pairs_.clear();
    const uint32_t n = static_cast<uint32_t>(bodies_.size());
    if (n == 0) return;

    for (auto& kv : grid_) kv.second.clear();
    if (grid_.size() > 8192) grid_.clear();
    large_.clear();
    is_large_.assign(n, 0);
    if (stamp_.size() != n) {
        stamp_.assign(n, 0);
        epoch_ = 0;
    }

    const float inv_cs = 1.f / config.cell_size;
    auto to_cell = [&](float v) { return static_cast<int>(std::floor(clampf(v, -1e8f, 1e8f) * inv_cs)); };

    for (uint32_t i = 0; i < n; ++i) {
        const Rect& r = bodies_[i].aabb;
        int x0 = to_cell(r.min.x), x1 = to_cell(r.max.x), y0 = to_cell(r.min.y), y1 = to_cell(r.max.y);
        if (static_cast<int64_t>(x1 - x0 + 1) * (y1 - y0 + 1) > 256) {  // huge collider: test against everything
            large_.push_back(i);
            is_large_[i] = 1;
            continue;
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) grid_[cell_key(x, y)].push_back(i);
    }

    for (uint32_t i = 0; i < n; ++i) {
        const Body& bi = bodies_[i];
        if (!bi.movable) continue;  // static bodies never initiate pairs
        cand_.clear();
        if (++epoch_ == 0) {
            std::fill(stamp_.begin(), stamp_.end(), 0u);
            epoch_ = 1;
        }
        auto consider = [&](uint32_t j) {
            if (j == i || stamp_[j] == epoch_) return;
            stamp_[j] = epoch_;
            if (bodies_[j].movable && j < i) return;  // that pair is generated from j's side
            cand_.push_back(j);
        };
        if (is_large_[i]) {
            for (uint32_t j = 0; j < n; ++j) consider(j);
        } else {
            int x0 = to_cell(bi.aabb.min.x), x1 = to_cell(bi.aabb.max.x);
            int y0 = to_cell(bi.aabb.min.y), y1 = to_cell(bi.aabb.max.y);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    auto it = grid_.find(cell_key(x, y));
                    if (it != grid_.end())
                        for (uint32_t j : it->second) consider(j);
                }
            for (uint32_t j : large_) consider(j);
        }
        std::sort(cand_.begin(), cand_.end());  // deterministic order

        for (uint32_t j : cand_) {
            const Body& bj = bodies_[j];
            if (!bi.aabb.overlaps(bj.aabb)) continue;
            if ((bi.col->layer & bj.col->mask) == 0 || (bj.col->layer & bi.col->mask) == 0) continue;
            bool ti = bi.col->is_trigger, tj = bj.col->is_trigger;
            if (ti && tj) continue;
            if (ti || tj) {
                trigger_pairs_.push_back({i, j});
                continue;
            }
            if (bi.inv_mass == 0.f && bj.inv_mass == 0.f) continue;  // nothing can move
            solid_pairs_.push_back({i, j});
        }
    }
}

void Physics::solve_pair(const Pair& p, bool record_debug) {
    Body& a = bodies_[p.a];
    Body& b = bodies_[p.b];
    Manifold m;
    if (!collide(*a.col, center_of(*a.tf, *a.col), *b.col, center_of(*b.tf, *b.col), config.contact_margin, m)) return;

    // One-way platforms: solid only for a mover that was above the surface and is not rising.
    if (a.col->one_way != b.col->one_way) {
        const bool a_is_platform = a.col->one_way;
        const Body& plat = a_is_platform ? a : b;
        const Body& mover = a_is_platform ? b : a;
        Vec2 n_to_mover = a_is_platform ? m.normal : -m.normal;
        if (mover.rb && mover.rb->ignore_one_way_timer > 0.f) return;
        float rel_vy = (mover.vel ? mover.vel->linear.y : 0.f) - (plat.vel ? plat.vel->linear.y : 0.f);
        float mover_prev_bottom = mover.prev_center.y + half_height(*mover.col);
        float plat_prev_top = plat.prev_center.y - half_height(*plat.col);
        if (n_to_mover.y > -0.5f || rel_vy < -1.f || mover_prev_bottom > plat_prev_top + 1.5f) return;
    }

    const float inv_sum = a.inv_mass + b.inv_mass;
    if (inv_sum <= 0.f) return;
    const Vec2 n = m.normal;

    // Contact state flags (what is touching each body, and from which side).
    auto note = [](Body& self, const Body& other, Vec2 n_toward_self) {
        if (!self.rb) return;
        if (n_toward_self.y < -0.7f) {
            self.rb->on_ground = true;
            self.rb->ground_entity = other.e;
            self.rb->ground_velocity = other.vel ? other.vel->linear : Vec2{};
        } else if (n_toward_self.y > 0.7f) {
            self.rb->on_ceiling = true;
        } else if (n_toward_self.x > 0.7f) {
            self.rb->on_wall_left = true;
        } else if (n_toward_self.x < -0.7f) {
            self.rb->on_wall_right = true;
        }
    };
    note(a, b, -n);
    note(b, a, n);

    if (contacts_now_.insert(make_key(a.e, b.e)).second) new_contacts_.push_back({a.e, b.e, n, m.depth});

    if (record_debug) {
        float ext = b.col->shape == Shape::Box ? std::abs(n.x) * b.col->half_extents.x + std::abs(n.y) * b.col->half_extents.y
                                               : b.col->radius;
        debug_contacts_.push_back({center_of(*b.tf, *b.col) - n * ext, n});
    }

    // Positional correction.
    if (m.depth > config.slop) {
        float corr = (m.depth - config.slop) * config.correction / inv_sum;
        if (a.inv_mass > 0.f) a.tf->position -= n * (corr * a.inv_mass);
        if (b.inv_mass > 0.f) b.tf->position += n * (corr * b.inv_mass);
    }

    // Velocity response: restitution + Coulomb friction.
    Vec2 va = a.vel ? a.vel->linear : Vec2{};
    Vec2 vb = b.vel ? b.vel->linear : Vec2{};
    Vec2 rv = vb - va;
    float vn = rv.dot(n);
    if (vn >= 0.f) return;  // separating

    float ea = a.rb ? a.rb->restitution : 0.f, eb = b.rb ? b.rb->restitution : 0.f;
    float e = std::max(ea, eb);
    if (-vn < config.restitution_threshold) e = 0.f;
    float jn = -(1.f + e) * vn / inv_sum;
    va -= n * (jn * a.inv_mass);
    vb += n * (jn * b.inv_mass);

    rv = vb - va;
    Vec2 tangent = rv - n * rv.dot(n);
    float tl = tangent.length();
    if (tl > 1e-6f) {
        tangent = tangent / tl;
        float fa = a.rb ? a.rb->friction : 0.6f, fb = b.rb ? b.rb->friction : 0.6f;
        float mu = std::sqrt(fa * fb);
        float jt = clampf(-rv.dot(tangent) / inv_sum, -mu * jn, mu * jn);
        va -= tangent * (jt * a.inv_mass);
        vb += tangent * (jt * b.inv_mass);
    }
    if (a.inv_mass > 0.f) a.vel->linear = va;
    if (b.inv_mass > 0.f) b.vel->linear = vb;
}

void Physics::step(World& world, EventBus* bus, float dt) {
    if (dt <= 0.f) return;

    world.view<RigidBody>().each([&](Entity, RigidBody& rb) {
        rb.on_ground = rb.on_ceiling = rb.on_wall_left = rb.on_wall_right = false;
        rb.ground_entity = NULL_ENTITY;
        rb.ground_velocity = {0.f, 0.f};
        rb.ignore_one_way_timer = std::max(0.f, rb.ignore_one_way_timer - dt);
    });

    gather_bodies(world);
    contacts_now_.clear();
    new_contacts_.clear();
    debug_contacts_.clear();

    const int substeps = std::max(1, config.substeps);
    const int iterations = std::max(1, config.iterations);
    const float h = dt / static_cast<float>(substeps);

    for (int s = 0; s < substeps; ++s) {
        for (auto& b : bodies_) b.prev_center = center_of(*b.tf, *b.col);
        integrate(world, h);
        for (auto& b : bodies_)
            b.aabb = Rect::from_center(center_of(*b.tf, *b.col), half_vec(*b.col)).expanded(config.contact_margin);
        build_pairs();
        for (int it = 0; it < iterations; ++it)
            for (const Pair& p : solid_pairs_) solve_pair(p, s == substeps - 1 && it == 0);
    }

    // Trigger overlaps are evaluated once, at the final positions of the step.
    std::vector<TriggerEvent> trigger_events;
    triggers_now_.clear();
    for (const Pair& p : trigger_pairs_) {
        const Body& a = bodies_[p.a];
        const Body& b = bodies_[p.b];
        Manifold m;
        if (!collide(*a.col, center_of(*a.tf, *a.col), *b.col, center_of(*b.tf, *b.col), 0.f, m) || m.depth <= 0.f)
            continue;
        const Body& trig = a.col->is_trigger ? a : b;
        const Body& other = a.col->is_trigger ? b : a;
        PairKey k = make_key(trig.e, other.e);
        if (triggers_now_.emplace(k, TriggerPair{trig.e, other.e}).second && !triggers_prev_.count(k))
            trigger_events.push_back({trig.e, other.e, true});
    }
    std::vector<std::pair<PairKey, TriggerPair>> exits;
    for (const auto& kv : triggers_prev_)
        if (!triggers_now_.count(kv.first)) exits.push_back(kv);
    std::sort(exits.begin(), exits.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
    for (const auto& kv : exits)
        if (world.alive(kv.second.trigger) && world.alive(kv.second.other))
            trigger_events.push_back({kv.second.trigger, kv.second.other, false});
    triggers_prev_.swap(triggers_now_);

    std::vector<CollisionEvent> collisions;
    for (const auto& ev : new_contacts_)
        if (!contacts_prev_.count(make_key(ev.a, ev.b))) collisions.push_back(ev);
    contacts_prev_.swap(contacts_now_);

    if (bus) {
        for (const auto& ev : collisions) bus->emit(ev);
        for (const auto& ev : trigger_events) bus->emit(ev);
    }
}

std::optional<RayHit> Physics::raycast(World& world, Vec2 origin, Vec2 direction, float max_distance, uint32_t mask,
                                       Entity ignore, bool include_triggers) {
    Vec2 d = direction.normalized();
    if (d.length_sq() < 0.5f) return std::nullopt;
    std::optional<RayHit> best;
    world.view<Collider, Transform>().each([&](Entity e, Collider& c, Transform& t) {
        if (e == ignore || (c.layer & mask) == 0 || (c.is_trigger && !include_triggers)) return;
        float limit = best ? best->distance : max_distance;
        Vec2 ctr = center_of(t, c);
        float tt;
        Vec2 nn;
        bool hit = c.shape == Shape::Box ? ray_box(origin, d, ctr - c.half_extents, ctr + c.half_extents, limit, tt, nn)
                                         : ray_circle(origin, d, ctr, c.radius, limit, tt, nn);
        if (hit && tt <= limit) best = RayHit{e, origin + d * tt, nn, tt};
    });
    return best;
}

std::vector<Entity> Physics::query_rect(World& world, Rect area, uint32_t mask) {
    std::vector<Entity> out;
    world.view<Collider, Transform>().each([&](Entity e, Collider& c, Transform& t) {
        if ((c.layer & mask) == 0) return;
        if (Rect::from_center(center_of(t, c), half_vec(c)).overlaps(area)) out.push_back(e);
    });
    return out;
}

}  // namespace prism
