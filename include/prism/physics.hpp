#pragma once
// Prism physics: 2D rigid bodies without rotation (boxes + circles), built for platformers
// and top-down games.
//   * Broadphase: uniform spatial hash. Static-vs-static pairs are never generated.
//   * Solver: sub-stepped sequential impulses (restitution, Coulomb friction) with
//     positional correction. Kinematic bodies push dynamic ones.
//   * Triggers (sensors), layers/masks, one-way platforms, ground/wall/ceiling contact
//     flags, raycasts and AABB queries.
//   * Events (CollisionEvent / TriggerEvent) are emitted after the step completes, so
//     handlers may freely create/destroy entities.
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "components.hpp"
#include "ecs.hpp"
#include "events.hpp"

namespace prism {

struct CollisionEvent {  // a new solid contact began this step
    Entity a, b;
    Vec2 normal;  // points from a toward b
    float depth;
};

struct TriggerEvent {  // `other` entered/left the sensor volume of `trigger`
    Entity trigger, other;
    bool entered;  // false = exited (only sent while both entities are still alive)
};

struct PhysicsConfig {
    Vec2 gravity{0.f, 980.f};  // pixels/s^2, +Y down
    int substeps = 2;
    int iterations = 6;       // solver iterations per substep
    float cell_size = 64.f;   // broadphase cell size
    float slop = 0.2f;        // penetration tolerated before correction
    float correction = 0.8f;  // fraction of penetration corrected per iteration
    float contact_margin = 0.5f;  // contact "skin": touching within this distance counts
    float max_speed = 2400.f;     // velocity clamp (limits tunnelling)
    float restitution_threshold = 60.f;  // impacts slower than this don't bounce
};

struct RayHit {
    Entity entity;
    Vec2 point;
    Vec2 normal;
    float distance = 0.f;
};

struct ContactPoint {  // for debug drawing
    Vec2 point;
    Vec2 normal;
};

class Physics {
public:
    PhysicsConfig config;

    // Advance the simulation by dt seconds. `bus` may be null.
    void step(World& world, EventBus* bus, float dt);

    std::optional<RayHit> raycast(World& world, Vec2 origin, Vec2 direction, float max_distance,
                                  uint32_t layer_mask = 0xFFFFFFFFu, Entity ignore = NULL_ENTITY,
                                  bool include_triggers = false);
    std::vector<Entity> query_rect(World& world, Rect area, uint32_t layer_mask = 0xFFFFFFFFu);

    size_t body_count() const { return bodies_.size(); }
    const std::vector<ContactPoint>& debug_contacts() const { return debug_contacts_; }

private:
    struct Body {
        Entity e;
        Transform* tf = nullptr;
        Collider* col = nullptr;
        RigidBody* rb = nullptr;
        Velocity* vel = nullptr;
        float inv_mass = 0.f;
        bool movable = false;  // dynamic or kinematic (has a Velocity)
        Vec2 prev_center{};
        Rect aabb{};
    };
    struct Pair {
        uint32_t a, b;
    };
    struct PairKey {
        uint64_t a, b;
        bool operator==(const PairKey& o) const { return a == o.a && b == o.b; }
        bool operator<(const PairKey& o) const { return a != o.a ? a < o.a : b < o.b; }
    };
    struct PairKeyHash {
        size_t operator()(const PairKey& k) const noexcept {
            return std::hash<uint64_t>{}(k.a * 0x9E3779B97F4A7C15ULL ^ (k.b + 0x7F4A7C15ULL + (k.a << 6)));
        }
    };
    struct TriggerPair {
        Entity trigger, other;
    };

    static PairKey make_key(Entity x, Entity y) {
        uint64_t p = x.pack(), q = y.pack();
        return p < q ? PairKey{p, q} : PairKey{q, p};
    }

    void gather_bodies(World& world);
    void integrate(World& world, float h);
    void build_pairs();
    void solve_pair(const Pair& p, bool record_debug);

    std::vector<Body> bodies_;
    std::vector<Pair> solid_pairs_, trigger_pairs_;

    std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    std::vector<uint32_t> large_, cand_, stamp_;
    std::vector<uint8_t> is_large_;
    uint32_t epoch_ = 0;

    std::unordered_set<PairKey, PairKeyHash> contacts_prev_, contacts_now_;
    std::vector<CollisionEvent> new_contacts_;
    std::unordered_map<PairKey, TriggerPair, PairKeyHash> triggers_prev_, triggers_now_;
    std::vector<ContactPoint> debug_contacts_;
};

}  // namespace prism
