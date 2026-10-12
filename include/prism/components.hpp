#pragma once
#include "ecs.hpp"
#include "types.hpp"

namespace prism {

// Position in world units (pixels, +Y is down). prev_position is the position at the
// start of the last fixed step; the renderer blends prev->position for smooth motion
// when the render rate and physics rate differ. Use teleport() to move instantly.
struct Transform {
    Vec2 position{0, 0};
    float rotation = 0.f;  // reserved: stored and integrated, not yet used by the renderer
    Vec2 scale{1.f, 1.f};
    Vec2 prev_position{0, 0};

    Transform() = default;
    explicit Transform(Vec2 p) : position(p), prev_position(p) {}
    Transform(Vec2 p, float rot, Vec2 s) : position(p), rotation(rot), scale(s), prev_position(p) {}
    void teleport(Vec2 p) { position = prev_position = p; }
};

struct Velocity {
    Vec2 linear{0, 0};
    float angular = 0.f;
};

enum class Shape : uint8_t { Box, Circle };

struct Collider {
    Shape shape = Shape::Box;
    Vec2 half_extents{16.f, 16.f};  // Box
    float radius = 16.f;            // Circle
    Vec2 offset{0, 0};              // from Transform::position
    bool is_trigger = false;        // reports overlaps, never resolves them
    bool one_way = false;           // solid only when approached from above
    uint32_t layer = 1;
    uint32_t mask = 0xFFFFFFFFu;    // layers this collider interacts with

    static Collider box(Vec2 half) {
        Collider c;
        c.half_extents = half;
        return c;
    }
    static Collider circle(float r) {
        Collider c;
        c.shape = Shape::Circle;
        c.radius = r;
        return c;
    }
    static Collider sensor_box(Vec2 half) {
        Collider c = box(half);
        c.is_trigger = true;
        return c;
    }
};

enum class BodyType : uint8_t { Static, Kinematic, Dynamic };

struct RigidBody {
    BodyType type = BodyType::Dynamic;
    float mass = 1.f;
    float gravity_scale = 1.f;
    float friction = 0.4f;
    float restitution = 0.f;
    float linear_damping = 0.f;
    float ignore_one_way_timer = 0.f;  // >0: fall through one-way platforms (seconds)

    // Written by the physics step each frame:
    bool on_ground = false;
    bool on_ceiling = false;
    bool on_wall_left = false;   // something solid on our left
    bool on_wall_right = false;  // something solid on our right
    Entity ground_entity = NULL_ENTITY;
    Vec2 ground_velocity{0, 0};  // velocity of what we stand on (moving platforms)

    static RigidBody Dynamic(float mass = 1.f) {
        RigidBody rb;
        rb.mass = mass;
        return rb;
    }
    static RigidBody Static(float friction = 0.6f) {
        RigidBody rb;
        rb.type = BodyType::Static;
        rb.mass = 0.f;
        rb.gravity_scale = 0.f;
        rb.friction = friction;
        return rb;
    }
    static RigidBody Kinematic() {
        RigidBody rb;
        rb.type = BodyType::Kinematic;
        rb.mass = 0.f;
        rb.gravity_scale = 0.f;
        rb.friction = 0.6f;
        return rb;
    }
};

struct Sprite {
    Color color{1, 1, 1, 1};  // fill color, or tint when a texture is set
    Vec2 size{32, 32};
    int z_index = 0;
    TextureId texture = 0;  // 0 = flat-colored rectangle
    IRect src{};            // region of the texture; w==0 means the whole texture
    bool flip_x = false;
    bool flip_y = false;
    bool visible = true;
};

// Destroys the entity after `remaining` seconds of simulation time.
struct Lifetime {
    float remaining = 1.f;
};

struct PlayerTag {};

// The engine's camera eases toward the (interpolated) position of the entity with this tag.
struct CameraTarget {
    Vec2 offset{0, 0};
};

}  // namespace prism
