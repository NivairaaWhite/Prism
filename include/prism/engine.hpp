#pragma once
// Prism Engine: ties the subsystems together and runs the main loop.
//
// Frame structure (Engine::step):
//   1. poll window events / apply scripted input
//   2. zero or more fixed steps (default 60 Hz): Scene::on_fixed_update -> physics -> lifetimes
//   3. Scene::on_update(dt)  (variable rate: put input handling & camera logic here)
//   4. animations, particles, camera
//   5. render (sprites are interpolated between the last two fixed steps)
//
// Engine::step(0) advances exactly one fixed step per frame, which makes runs fully
// deterministic: the basis for the headless tests and replays.
//
// Scene lifecycle: set_scene() takes effect at the start of the next step. By default every
// scene starts from a clean slate (see EngineConfig::reset_world_on_scene_change): the previous
// scene's entities, particles, camera state and physics settings are discarded, and any event
// handlers subscribed while it was active are removed, so nothing can dangle across a switch.
// time_scale and paused go back to normal too. The very first scene does not trigger a reset: whatever the host set up beforehand is kept.
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "animation.hpp"
#include "camera.hpp"
#include "components.hpp"
#include "ecs.hpp"
#include "events.hpp"
#include "image.hpp"
#include "input.hpp"
#include "particles.hpp"
#include "physics.hpp"
#include "platform.hpp"  // plain main() everywhere (see the note in that header)
#include "renderer.hpp"
#include "tilemap.hpp"

namespace prism {

class Engine;

struct EngineConfig {
    std::string title = "Prism";
    int width = 1280, height = 720;
    Backend backend = Backend::Auto;  // Auto: SDL window if available, else headless software
    bool vsync = true;
    float fixed_hz = 60.f;
    float max_frame_dt = 0.1f;     // clamp for long frames (spiral-of-death protection)
    int max_steps_per_frame = 8;
    float target_fps = 0.f;        // run() sleeps to this rate when > 0 and vsync is off
    bool quit_on_escape = true;
    uint64_t seed = 1;
    Color clear_color = Color::from_hex(0x12121C);
    float camera_follow_lambda = 6.f;  // smoothing for CameraTarget following; 0 disables

    // Physics settings every scene starts with (restored by reset_world()). A scene may change
    // engine.physics().config in on_enter; the change lasts until that scene exits.
    PhysicsConfig physics{};
    // true: a scene change calls reset_world() and drops the old scene's event handlers.
    // false: the world, camera and handlers persist across scenes (the scene cleans up itself).
    bool reset_world_on_scene_change = true;
};

// Game logic lives in Scenes. All callbacks are optional.
class Scene {
public:
    virtual ~Scene() = default;
    virtual void on_enter(Engine&) {}
    virtual void on_exit(Engine&) {}
    virtual void on_fixed_update(Engine&, float /*dt*/) {}
    virtual void on_update(Engine&, float /*dt*/) {}
    virtual void on_render(Engine&, Renderer&) {}  // HUD / overlays, drawn in screen space
};

struct ScriptedKey {  // for headless runs and replays
    uint64_t frame;
    Key key;
    bool down;
};

class Engine {
public:
    explicit Engine(EngineConfig cfg = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- subsystems ----
    World& world() { return world_; }
    Physics& physics() { return physics_; }
    Input& input() { return input_; }
    EventBus& events() { return events_; }
    Camera2D& camera() { return camera_; }
    ParticleSystem& particles() { return particles_; }
    Renderer& renderer() { return *renderer_; }
    Rng& rng() { return rng_; }
    const EngineConfig& config() const { return config_; }

    TextureId add_texture(const Image& img) { return renderer_->create_texture(img); }
    // Create-once texture lookup: `make` runs only the first time `key` is requested, so scenes
    // can be re-entered any number of times without uploading (and leaking) duplicates.
    TextureId cached_texture(const std::string& key, const std::function<Image()>& make);

    // ---- scenes ----
    // The switch takes effect at the start of the next step.
    void set_scene(std::unique_ptr<Scene> scene);
    Scene* scene() { return scene_.get(); }
    // Destroy all entities and start the camera, particles and physics settings from scratch.
    // Scenes can call this to restart themselves. Input bindings, textures, the RNG and the
    // simulation clock are kept.
    void reset_world();

    // ---- main loop ----
    void run();                   // until quit() / window closed
    bool step(float dt = 0.f);    // one frame; dt <= 0 => exactly one fixed step. false once quitting
    void quit() { quit_ = true; }
    bool quitting() const { return quit_; }

    // ---- time ----
    // Simulation speed multiplier (0 = frozen, 0.25 = slow-motion, 2 = fast-forward) and a pause
    // switch. They scale fixed steps, animation, particles and the camera; Scene::on_update still
    // runs every frame with the *unscaled* frame time, so menus and unpausing keep working.
    // Both stay deterministic: step(0) with a given scale always produces the same run.
    float time_scale = 1.f;
    bool paused = false;
    float effective_time_scale() const;  // 0 while paused; non-finite/negative values count as 1/0

    uint64_t frame() const { return frame_; }
    double time() const { return sim_time_; }  // simulated seconds
    float fixed_dt() const { return static_cast<float>(fixed_dt_); }
    float fps() const { return fps_; }         // wall-clock frames per second

    // ---- debugging / testing ----
    bool debug_overlay = false;    // F1: stats
    bool debug_colliders = false;  // F2: collider outlines + contacts
    void set_input_script(std::vector<ScriptedKey> script);
    void request_screenshot(const std::string& png_path);  // saved during the next render
    bool capture_frame(Image& out) { return renderer_->capture(out); }

    // ---- helpers ----
    Vec2 screen_to_world(Vec2 s) const { return camera_.screen_to_world(s); }
    Entity spawn_static_rect(Vec2 pos, Vec2 size, Color color, int z = 0);
    Entity spawn_dynamic_rect(Vec2 pos, Vec2 size, Color color, float mass = 1.f);

private:
    void apply_scene_change();
    void fixed_step();
    void update_camera(float dt);
    void render();
    void draw_world(Renderer& r);
    void draw_debug_colliders(Renderer& r);
    void draw_debug_overlay(Renderer& r);

    EngineConfig config_;
    std::unique_ptr<Renderer> renderer_;  // declared first => destroyed last
    World world_;
    Physics physics_;
    Input input_;
    EventBus events_;
    Camera2D camera_;
    ParticleSystem particles_;
    Rng rng_;

    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pending_scene_;
    bool has_pending_scene_ = false;
    EventBus::Token scene_event_mark_ = 0;  // handlers with larger tokens belong to scene_
    std::unordered_map<std::string, TextureId> texture_cache_;

    struct DrawItem {
        int z;
        uint32_t index;
        bool tilemap;
        Entity e;
    };
    std::vector<DrawItem> draw_items_;

    std::vector<ScriptedKey> script_;
    size_t script_pos_ = 0;
    std::string screenshot_path_;

    double fixed_dt_;
    double acc_ = 0.0;
    float alpha_ = 0.f;
    double sim_time_ = 0.0;
    uint64_t frame_ = 0;
    bool quit_ = false;
    bool camera_initialized_ = false;

    float fps_ = 0.f;
    double fps_timer_ = 0.0;
    int fps_frames_ = 0;
    double last_wall_ = -1.0;
};

}  // namespace prism
