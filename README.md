# Prism

A small, dependency-light **2D game engine in C++20**: an ECS core, a sub-stepped rigid-body
physics engine, a renderer abstraction (SDL2 window *or* a built-in CPU rasterizer), a tilemap,
sprite animation, particles, a camera, and a from-scratch PNG decoder. The whole engine runs
**headless and deterministically**, which is how it is tested.

```
include/prism/   public headers (prism.hpp pulls in everything)
src/             engine sources
examples/hello/       smallest possible program
examples/platformer/  side-scrolling platformer (gravity, one-way planks, coins)
examples/arena/       top-down survival shooter (no gravity, layers, AI, pathfinding, waves)
examples/common/      harness.hpp: the CLI + headless runner both games share
tests/           68 tests + fixtures          tools/make_fixtures.py regenerates the PNGs
```

Two games on one engine on purpose: the platformer and the arena want opposite things from
physics (gravity vs none, boxes vs circles, one-way floors vs projectiles) and both are built from
the same ~15 headers. All art is procedural; there are no asset files.

## Build

```sh
# Make (no other tooling needed; headless/software backend)
make            # builds build/prism_demo, prism_arena, prism_hello, prism_tests
make test       # unit tests
make smoke      # a bot plays each game headlessly; every run is repeated to prove determinism
make check      # test + smoke

# Make with the SDL2 window backend (needs SDL2 dev headers + pkg-config)
make SDL=1

# CMake (auto-detects SDL2; builds headless if it is absent)
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build
```

Run a game: `build/prism_demo` or `build/prism_arena` (window; needs the SDL build). Anywhere,
including CI:

```sh
build/prism_arena --headless --autopilot --frames 3600 --shot 1800 shot.png
build/prism_demo  --headless --autopilot --frames 1800 --shot 900  shot.png
```

**Platformer controls:** A/D or arrows move · Space/W/Up jump · S/Down + jump drops through a
plank · left click spawns a crate, right click a bouncy ball · P pause · R reset · F1 stats · F2
collider/contact overlay · Esc quit.

**Arena controls:** WASD/arrows move · mouse aims · hold left click to fire · Space/Shift dash
(brief invulnerability) · P pause · Enter/click start and retry · T title (on the game-over
screen) · F1/F2 debug · Esc quit. Five enemy types (chasers, tanks, line-of-sight gunners,
splitters, telegraphed chargers), four pickups, endless waves, combo scoring.

**Flags every game accepts** (`examples/common/harness.hpp`):

| flag | meaning |
|---|---|
| `--headless` | software renderer, no window; runs `--frames N` frames then exits |
| `--autopilot` | a bot plays through the same `Input` a human uses |
| `--shot F PATH` | save a PNG at frame F (repeatable; warns if F is never reached) |
| `--press F KEY` | tap a key at frame F in headless mode, e.g. `--press 600 p` |
| `--size WxH`, `--seed N` | resolution and RNG seed |
| `--verify-determinism` | run twice from scratch; exit 3 unless both final frames match |
| `--colliders`, `--stats` | start with the F2 / F1 overlays on |

Arena extras: `--map FILE` (custom arena, see below), `--difficulty PCT`, `--no-title`,
`--expect-wave N`, `--expect-score N`. Numbers are parsed strictly (`--frames abc` is an error,
not silently 0). Exit codes: 0 ok · 1 fatal · 2 an `--expect-*` check failed · 3 not
deterministic · 64 bad command line.

### Windows (MSYS2)

Open **MSYS2 UCRT64** from the Start menu (not "MSYS2 MSYS", not Command Prompt), then:

```sh
cd /d/Prism                      # D:\Prism  (C:\Prism is /c/Prism)
bash scripts/build_msys2.sh      # installs packages, builds with SDL2, runs all checks
./build/prism_demo.exe           # platformer
./build/prism_arena.exe          # arena shooter
```

or by hand:

```sh
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,SDL2}
rm -rf build                     # once, if an older/failed build folder exists
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build
```

`make SDL=1` works in the same shell too, but those `.exe` files only run from inside the MSYS2
shell (it supplies `SDL2.dll` through `PATH`). The CMake-built `.exe` files also start by
double-click: `SDL2.dll` is copied next to them and the compiler runtime is linked statically
(`-DPRISM_STATIC_RUNTIME=OFF` turns that off). They keep a console window so command-line flags
and error messages are visible; `-DPRISM_WINDOWS_GUI=ON` builds them without one.

**Troubleshooting**

| symptom | cause / fix |
|---|---|
| ``undefined reference to `SDL_main' `` | Building with 0.3.0 on MinGW. Use 0.3.1 or later (see below). Never rename `main` to `SDL_main` by hand. |
| `cd: Prism: No such file or directory` | You are in the wrong folder. `cd /d/Prism` (drive letter in lower case, forward slashes). |
| `No such file or directory` for `./build/prism_demo.exe` | The build did not finish. Scroll up for the first `FAILED:` or `error:` line. |
| `Fatal: Window creation failed ... (no usable display? try --headless)` | No display (remote shell, CI). Add `--headless`. |
| `cmake` says SDL2 backend: OFF | `pacman -S mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-pkgconf`, then `rm -rf build` and configure again. |
| Odd errors after switching shells or compilers | Delete `build/`: a build folder is tied to the compiler it was configured with. |

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

**Scene lifecycle** (`engine.hpp`): `set_scene()` takes effect at the start of the next step. By
default each scene starts from a clean slate: the previous scene's entities, particles, camera
state and physics settings (restored to `EngineConfig::physics`) are discarded, event handlers
subscribed while it was active are removed (so a handler capturing `this` can never outlive its
scene), and `time_scale`/`paused` return to normal. Whatever the host set up *before* the first
scene is left alone. Opt out with `EngineConfig::reset_world_on_scene_change = false`. A scene can
restart itself with `engine.reset_world()`.

**Time control:** `engine.time_scale` (slow-motion, hit-stop, fast-forward) and `engine.paused`
scale fixed steps, animation, particles and camera; `Scene::on_update` still runs every frame with
the *unscaled* dt, so menus and unpausing keep working. Both are deterministic.

**Resources:** `engine.cached_texture(key, make)` uploads an image once however often a scene is
re-entered.

**Robustness contract:** non-finite values never reach the simulation. A NaN/inf frame time acts
like `step(0)`; a NaN position or velocity on a body is repaired by physics (last good position /
zero velocity) and reported as a `PhysicsFault` event plus `physics.faults()`; a collider with
non-finite geometry sits the step out; an invalid `EngineConfig` throws `std::invalid_argument`
naming the problem; `Animator` tolerates zero columns and edited clip lists.

**Also included:** `Tilemap` (ASCII loader, merged static colliders, culled drawing), `Animator`
(sprite-sheet clips), `ParticleSystem`, `Camera2D` (smooth follow, bounds, trauma shake),
`EventBus`, action-mapped `Input`, PNG load/save (`image.hpp`, including its own inflate).

## Make your own game

Copy `examples/arena/` (data-driven) or `examples/platformer/`. A game is a `GameSpec` plus scenes:

```cpp
int main(int argc, char** argv) {
    harness::GameSpec spec;
    spec.binary = "my_game";
    spec.make_scene = [](const harness::CommonOptions& o) { return std::make_unique<MyScene>(o.autopilot); };
    return harness::run_game(argc, argv, spec);   // gets every flag above, headless runs, determinism check
}
```

Arena specifics, all plain data in `examples/arena/arena_data.hpp`:

- **New enemy:** add a row to `kEnemies` (stats, behaviour `Chase`/`Kite`/`Charge`, wave cost,
  first wave, split rules) and a case in `arena_art.hpp`. Waves, spawning and scoring pick it up.
- **New arena:** write an ASCII file (`#` wall, `.` floor, `P` player start, `S` enemy spawn) and
  run `prism_arena --map my_arena.txt`. It is validated on load with the exact row and column of
  any mistake: ragged rows, unknown tiles, open borders, missing/duplicate `P`, and spawns that
  cannot be reached from the start. See `examples/arena/maps/crossroads.txt`.

## Testing

`make test` runs 68 tests (~51,000 checks): ECS vs a randomized reference model, PNG decoding of
files from an independent encoder (all colour types and all five scanline filters) plus
mutation-fuzzing of the deflate stream, physics checked against closed-form expectations
(Coulomb stopping distance, restitution ratio, no tunnelling), event semantics, rendering pixel
checks, whole-engine determinism, scene-lifecycle and non-finite-input regressions, the CLI
parser, and the arena's data layer (table consistency, wave-planner properties, map validation,
pathfinding). The suite and both games' bots also run clean under AddressSanitizer + UBSan
(including `float-cast-overflow` and `float-divide-by-zero`).

`make smoke` / `ctest` additionally run a bot through each game (`--expect-coins 20`,
`--expect-wave 4 --expect-score 800`) with `--verify-determinism`, a custom map, and check that a
malformed map is refused.

## What changed in 0.3.2

**Fixed: two unit tests failed on Windows** (`engine_screenshot_roundtrip`, `arena_map_file_loading`).
They wrote scratch files to a hard-coded `/tmp/...`, which does not exist on Windows. They now use the
system temp folder (`std::filesystem::temp_directory_path()`) and delete what they create (the
screenshot test used to leave its PNG behind).

**Removed leftovers** (found with compiler warnings, cppcheck and a gcov run of every program, which
found no unused functions, variables or branches in the engine itself):

- `examples/hello`: `int main(int argc, char** argv)` with `(void)argc; (void)argv;` is now `int main()`
  (the arguments were only there for the old `SDL_main` signature).
- `src/renderer.cpp`: `(void)title; (void)vsync;` became `[[maybe_unused]]` on the parameters.
- `include/prism/tilemap.hpp` no longer includes `<map>` (only `tilemap.cpp` uses it), so every file
  that includes the header compiles slightly less.
- `tests/test_main.cpp`: an `Entity` variable that existed only to be `(void)`-ed.

## What changed in 0.3.1

**Fixed: the SDL2 build failed to link on Windows (MSYS2 / MinGW)** with
``undefined reference to `SDL_main'``. Cause: on MinGW, `sdl2.pc` (and `sdl2-config`) add
`-Dmain=SDL_main`, `-lmingw32 -lSDL2main` and `-mwindows`. That renames the game's `main` to
`SDL_main`, which in C++ is a *mangled* symbol (`_Z8SDL_mainiPPc`), while `SDL2main`'s `WinMain`
looks for the plain C name. Prism does not need any of it, so every game keeps a plain `main`:

- `include/prism/platform.hpp` (new): drops a stray `-Dmain=SDL_main` and defines
  `SDL_MAIN_HANDLED`; included by `engine.hpp`, `prism.hpp` and `sdl_renderer.hpp`.
  `SdlRenderer` calls `SDL_SetMainReady()` as SDL requires in that mode.
- `CMakeLists.txt`: uses SDL2's own CMake target when available (never `SDL2main`), otherwise
  `pkg-config` with `-Dmain=`, `SDL2main`, `mingw32` and `-mwindows` filtered out. Also sets
  `SDL2_NO_MWINDOWS` (SDL2's CMake target otherwise makes a console-less exe).
- `Makefile`: same filtering for `make SDL=1`; `.exe` target names on Windows; the baked-in test-data
  path is the native `D:/Prism/...` form (MSYS2's `/d/Prism` cannot be opened by a native `.exe`);
  `$(AR)` instead of a hard-coded `ar`.
- `sdl_renderer.hpp` accepts both `<SDL2/SDL.h>` and `<SDL.h>` include layouts.

**Added:** `scripts/build_msys2.sh`; CMake options `PRISM_STATIC_RUNTIME` (default ON) and
`PRISM_WINDOWS_GUI` (default OFF); `SDL2.dll` is copied next to the executables on Windows.

**Verified in 0.3.1** (the first time the SDL path was exercised at all):

- Linux, g++ 13: `make check`, `make SDL=1 check`, and CMake + `ctest` with SDL2 2.30. The SDL window
  was started on a virtual display, driven with keyboard and mouse, and shut down with Esc.
- Windows: cross-compiled with MinGW-w64 (g++ 13) against the official SDL2 2.32.10 MinGW package, by
  all three routes (CMake via SDL2's config, CMake via pkg-config only, `make SDL=1`), then the
  `.exe` files were **run** under Wine: 68 tests / 0 failures, both games' bots with the determinism
  re-run, custom and malformed maps, and a real SDL window with keyboard and mouse input. The frame
  hashes are identical to Linux's. The `.exe` files import only `SDL2.dll` and system DLLs.

**Confirmed on a real machine:** native MSYS2 UCRT64 with GCC 16.2.0 and SDL2 2.32.10 (CMake + Ninja):
it builds and links, both games open on the `sdl2` backend, and 4 of the 5 `ctest` checks passed; the
fifth (two unit tests that wrote to `/tmp`) is fixed in 0.3.2.

**Not verified:** MSVC, macOS, and GPU-accelerated rendering on real hardware (the Linux/Wine checks
above used SDL's software renderer).

## What changed in 0.3

**Fixed** (each was reproduced first, and has a regression test):

- **The project did not link.** All three entry points defined `SDL_main` instead of `main`, and
  nothing links `SDL2main`, so no executable could start. (0.3.1: the converse problem on MinGW,
  where the SDL flags rename `main` again, is fixed too.)
- **Use-after-free:** a scene that captured `this` in an event handler and did not unsubscribe left
  a dangling handler after it was destroyed. The engine now removes a scene's handlers on exit.
- **Scene switches leaked.** The old scene's entities stayed in the world and its `CameraTarget`
  kept the camera, so the new scene's camera never moved. Scenes now start clean.
- **NaN/inf positions reached a float→int cast in the broadphase** (undefined behaviour) and stuck
  to the body forever. NaN/inf frame times permanently poisoned every `Animator`.
- `Animator::columns = 0` crashed with SIGFPE; a negative window size surfaced as an opaque
  `length_error`; `World::clear()` reversed index order, so "restart" was not reproducible.
- The demo's CLI used `atoi`, so `--frames abc` silently meant "default".

**Added:** the arena game; `examples/common/harness.hpp`; `time_scale`/`paused`;
`reset_world()`; `EngineConfig::physics`; `cached_texture`; `PhysicsFault`; `Physics::reset()`;
`EventBus::mark()/unsubscribe_since()`; pause (P) in the platformer.

The platformer's gameplay is unchanged: with the on-screen help text restored to the original, its
frame hash is bit-identical to the 0.2 build.

**Not verified in 0.3 (the SDL2/CMake parts were checked in 0.3.1, see above):** the SDL2 window backend (no SDL2
headers available, so `sdl_renderer.*` is untouched and the windowed code paths were never run)
and the CMake build (no CMake available; the edits are small, but `ctest` has not been run).
Everything else above was built, run and tested with g++ 13 on Linux.

## What changed from the original Prism (0.2)

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

Audio, scripting, rotation / angular dynamics (the arena fakes a rotating gun with a 16-direction
sprite sheet), scene serialization, text beyond the built-in bitmap font, GPU batching (the SDL
backend uses `SDL_Renderer`), networking.
