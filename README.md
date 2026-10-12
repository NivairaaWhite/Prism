# Prism

A small, dependency-light **2D game engine in C++20**: an ECS core, a sub-stepped rigid-body
physics engine, a renderer abstraction (SDL2 window *or* a built-in CPU rasterizer), a tilemap,
sprite animation, particles, a camera, and a from-scratch PNG decoder. The whole engine runs
**headless and deterministically**, which is how it is tested.

```
include/prism/   public headers (prism.hpp pulls in everything)
src/             engine sources
examples/hello/       smallest possible program
examples/platformer/  full demo game (procedural art, no asset files)
tests/           45 tests + PNG fixtures      tools/make_fixtures.py regenerates them
```

## Build

```sh
# Make (no other tooling needed; headless/software backend)
make            # builds build/prism_demo, build/prism_hello, build/prism_tests
make test

# Make with the SDL2 window backend (needs SDL2 dev headers + pkg-config)
make SDL=1

# CMake (auto-detects SDL2; builds headless if it is absent)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build
```

Run the demo: `build/prism_demo` (window, needs the SDL build). Anywhere, including CI:

```sh
build/prism_demo --headless --autopilot --frames 1800 --shot 900 shot.png
```

**Demo controls:** A/D or arrows move · Space/W/Up jump · S/Down + jump drops through a plank ·
left click spawns a crate, right click a bouncy ball · R reset · F1 stats · F2 collider/contact
overlay · Esc quit.

## Hello, Prism

```cpp
#include "prism/prism.hpp"
using namespace prism;

struct Hello : Scene {
    Entity player;
    void on_enter(Engine& e) override {
        e.spawn_static_rect({0, 200}, {2000, 80}, Color::from_hex(0x3A5A40));
        player = e.spawn_dynamic_rect({0, -100}, {40, 40}, Color::from_hex(0xFFB703));
        e.world().add(player, CameraTarget{});
        e.input().bind("left", {Key::A, Key::Left});
        e.input().bind("right", {Key::D, Key::Right});
        e.input().bind("jump", {Key::Space, Key::W, Key::Up});
    }
    void on_update(Engine& e, float) override {
        auto* vel = e.world().get<Velocity>(player);
        auto* body = e.world().get<RigidBody>(player);
        vel->linear.x = 300.f * e.input().axis("left", "right");
        if (body->on_ground && e.input().action_pressed("jump")) vel->linear.y = -620.f;
    }
};

int main() {
    Engine engine({.title = "Hello Prism"});
    engine.set_scene(std::make_unique<Hello>());
    engine.run();
}
```

(`examples/hello/main.cpp` is this program plus a headless fallback.)

## How it fits together

**Frame loop** (`Engine::step`): poll input → *n* fixed steps at 60 Hz (`Scene::on_fixed_update` →
physics → lifetimes) → `Scene::on_update(dt)` (input handling lives here) → animation, particles,
camera → render. Sprites are drawn **interpolated** between the last two fixed steps, so motion is
smooth at any refresh rate. `Engine::step(0)` advances exactly one fixed step per frame: runs are
bit-for-bit reproducible.

**ECS** (`ecs.hpp`): generational `Entity` handles (stale handles are detected), sparse-set
component pools (dense, cache-friendly), `world.view<A, B>().each(...)`. `destroy()` inside a
view is deferred to the end of the iteration; `add<T>()` inside a view is allowed (references
obtained earlier may be invalidated); `remove<T>()` inside a view asserts.

**Physics** (`physics.hpp`): boxes and circles (no rotation), static/kinematic/dynamic bodies,
uniform-grid broadphase, sub-stepped sequential impulses with restitution and Coulomb friction,
triggers, layers/masks, one-way platforms, contact flags (`on_ground`, `on_wall_left`, ...,
`ground_velocity` for moving platforms), raycasts and rectangle queries. `CollisionEvent` and
`TriggerEvent` are emitted *after* the step so handlers can freely create/destroy entities.

**Rendering** (`renderer.hpp`): `Renderer` is a small interface (rects, lines, circles, textured
quads with tint/flip, readback). `SoftwareRenderer` is a deterministic CPU rasterizer;
`SdlRenderer` adds a window and input. A built-in 5x7 bitmap font provides `draw_text`.

**Also included:** `Tilemap` (ASCII loader, merged static colliders, culled drawing), `Animator`
(sprite-sheet clips), `ParticleSystem`, `Camera2D` (smooth follow, bounds, trauma shake),
`EventBus`, action-mapped `Input`, PNG load/save (`image.hpp`, including its own inflate).

## Testing

`make test` runs 45 tests (~7,700 checks): ECS vs a randomized reference model, PNG decoding of
files from an independent encoder (all colour types and all five scanline filters) plus
mutation-fuzzing of the deflate stream, physics checked against closed-form expectations
(Coulomb stopping distance, restitution ratio, no tunnelling), event semantics, rendering pixel
checks, and whole-engine determinism. The suite also passes under AddressSanitizer + UBSan.
`ctest` additionally runs a bot through the demo level (`--expect-coins 20`).

## What changed from the original Prism

- **ECS rewritten.** The old `World::alive()` compared a value with itself (always true) and
  entity ids were bare indices, so stale handles silently aliased recycled entities. Components
  were in `unordered_map`s although the README claimed contiguous storage; `remove<T>` wouldn't
  compile when instantiated; creating entities inside `each` invalidated iterators.
- **Input and rendering no longer depend on SDL types**; SDL is an optional backend.
- Physics: broadphase added (was O(n^2)), correct inverse-mass response, friction as an impulse
  (was a flat per-step damping), circles, triggers, one-way platforms, ground detection (the old
  demo allowed infinite mid-air jumps), sub-stepping.
- Removed the README's Lua claim: nothing linked Lua.

## Not included (yet)

Audio, scripting, rotation / angular dynamics, scene serialization, text beyond the built-in
bitmap font, GPU batching (the SDL backend uses `SDL_Renderer`), networking.
