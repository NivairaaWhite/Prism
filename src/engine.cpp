#include "prism/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace prism {
namespace {
double wall_seconds() {
    using clk = std::chrono::steady_clock;
    return std::chrono::duration<double>(clk::now().time_since_epoch()).count();
}
}  // namespace

Engine::Engine(EngineConfig cfg)
    : config_(std::move(cfg)),
      renderer_(create_renderer(config_.backend, config_.title, config_.width, config_.height, config_.vsync)),
      particles_(4096, config_.seed * 7919u + 1u),
      rng_(config_.seed),
      fixed_dt_(1.0 / std::max(1.f, config_.fixed_hz)) {
    camera_.viewport = {static_cast<float>(renderer_->width()), static_cast<float>(renderer_->height())};
    std::printf("[Prism] engine up: %dx%d, backend=%s, fixed=%.0f Hz\n", renderer_->width(), renderer_->height(),
                renderer_->name(), 1.0 / fixed_dt_);
}

Engine::~Engine() {
    if (scene_) scene_->on_exit(*this);
    scene_.reset();
}

void Engine::set_scene(std::unique_ptr<Scene> scene) {
    pending_scene_ = std::move(scene);
    has_pending_scene_ = true;
}

void Engine::apply_scene_change() {
    if (!has_pending_scene_) return;
    has_pending_scene_ = false;
    if (scene_) scene_->on_exit(*this);
    scene_ = std::move(pending_scene_);
    if (scene_) scene_->on_enter(*this);
}

void Engine::set_input_script(std::vector<ScriptedKey> script) {
    script_ = std::move(script);
    std::stable_sort(script_.begin(), script_.end(),
                     [](const ScriptedKey& a, const ScriptedKey& b) { return a.frame < b.frame; });
    script_pos_ = 0;
}

void Engine::request_screenshot(const std::string& path) { screenshot_path_ = path; }

// ───────────────────────────── main loop ─────────────────────────────

void Engine::run() {
    double last = wall_seconds();
    while (!quit_) {
        double start = wall_seconds();
        float dt = static_cast<float>(start - last);
        last = start;
        if (!step(dt)) break;
        if (config_.target_fps > 0.f && (!config_.vsync || std::string(renderer_->name()) == "software")) {
            double target = start + 1.0 / config_.target_fps;
            double now = wall_seconds();
            if (now < target) std::this_thread::sleep_for(std::chrono::duration<double>(target - now));
        }
    }
}

bool Engine::step(float frame_dt) {
    if (quit_) return false;
    apply_scene_change();

    double dt = frame_dt <= 0.f ? fixed_dt_ : std::min<double>(frame_dt, config_.max_frame_dt);

    input_.begin_frame();
    while (script_pos_ < script_.size() && script_[script_pos_].frame <= frame_) {
        input_.set_key(script_[script_pos_].key, script_[script_pos_].down);
        ++script_pos_;
    }
    if (!renderer_->poll_events(input_)) quit_ = true;
    if (config_.quit_on_escape && input_.pressed(Key::Escape)) quit_ = true;
    if (input_.pressed(Key::F1)) debug_overlay = !debug_overlay;
    if (input_.pressed(Key::F2)) debug_colliders = !debug_colliders;

    acc_ += dt;
    int steps = 0;
    while (acc_ + 1e-9 >= fixed_dt_ && steps < config_.max_steps_per_frame) {
        fixed_step();
        acc_ -= fixed_dt_;
        ++steps;
    }
    if (steps == config_.max_steps_per_frame && acc_ >= fixed_dt_) acc_ = std::fmod(acc_, fixed_dt_);  // drop backlog
    acc_ = std::max(0.0, acc_);
    alpha_ = clampf(static_cast<float>(acc_ / fixed_dt_), 0.f, 1.f);

    const float fdt = static_cast<float>(dt);
    if (scene_) scene_->on_update(*this, fdt);
    update_animations(world_, fdt);
    particles_.update(fdt);
    update_camera(fdt);

    render();
    ++frame_;

    // Wall-clock FPS (display only; never feeds the simulation).
    double now = wall_seconds();
    if (last_wall_ >= 0.0) fps_timer_ += now - last_wall_;
    last_wall_ = now;
    ++fps_frames_;
    if (fps_timer_ >= 0.5) {
        fps_ = static_cast<float>(fps_frames_ / fps_timer_);
        fps_timer_ = 0.0;
        fps_frames_ = 0;
        renderer_->set_title(config_.title + " | " + std::to_string(static_cast<int>(fps_)) +
                             " FPS | entities: " + std::to_string(world_.size()));
    }
    return !quit_;
}

void Engine::fixed_step() {
    const float dt = static_cast<float>(fixed_dt_);
    world_.view<Transform>().each([](Entity, Transform& t) { t.prev_position = t.position; });
    if (scene_) scene_->on_fixed_update(*this, dt);
    physics_.step(world_, &events_, dt);
    world_.view<Lifetime>().each([&](Entity e, Lifetime& l) {
        l.remaining -= dt;
        if (l.remaining <= 0.f) world_.destroy(e);
    });
    sim_time_ += fixed_dt_;
}

void Engine::update_camera(float dt) {
    if (config_.camera_follow_lambda > 0.f) {
        bool done = false;
        world_.view<CameraTarget, Transform>().each([&](Entity, CameraTarget& ct, Transform& t) {
            if (done) return;
            done = true;
            Vec2 p = Vec2::lerp(t.prev_position, t.position, alpha_) + ct.offset;
            if (!camera_initialized_) {
                camera_.snap_to(p);
                camera_initialized_ = true;
            } else {
                camera_.follow(p, config_.camera_follow_lambda, dt);
            }
        });
    }
    camera_.update(dt);
}

// ───────────────────────────── rendering ─────────────────────────────

void Engine::render() {
    Renderer& r = *renderer_;
    r.begin_frame(config_.clear_color);
    draw_world(r);
    particles_.render(r, camera_);
    if (debug_colliders) draw_debug_colliders(r);
    if (scene_) scene_->on_render(*this, r);
    if (debug_overlay) draw_debug_overlay(r);
    if (!screenshot_path_.empty()) {
        Image img;
        if (r.capture(img) && save_png(screenshot_path_, img))
            std::printf("[Prism] screenshot saved: %s\n", screenshot_path_.c_str());
        else
            std::fprintf(stderr, "[Prism] screenshot failed: %s\n", screenshot_path_.c_str());
        screenshot_path_.clear();
    }
    r.end_frame();
}

void Engine::draw_world(Renderer& r) {
    draw_items_.clear();
    world_.view<Sprite, Transform>().each([&](Entity e, Sprite& s, Transform&) {
        if (s.visible) draw_items_.push_back({s.z_index, e.index, false, e});
    });
    world_.view<Tilemap>().each([&](Entity e, Tilemap& t) { draw_items_.push_back({t.z_index, e.index, true, e}); });
    std::sort(draw_items_.begin(), draw_items_.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.z != b.z ? a.z < b.z : a.index < b.index;
    });

    const Rect view = camera_.visible_rect();
    for (const DrawItem& it : draw_items_) {
        if (it.tilemap) {
            if (Tilemap* tm = world_.get<Tilemap>(it.e)) tm->draw(r, camera_);
            continue;
        }
        Sprite* s = world_.get<Sprite>(it.e);
        Transform* t = world_.get<Transform>(it.e);
        if (!s || !t) continue;

        Vec2 pos = Vec2::lerp(t->prev_position, t->position, alpha_);
        Vec2 size{std::abs(s->size.x * t->scale.x), std::abs(s->size.y * t->scale.y)};
        Rect wr = Rect::from_center(pos, size * 0.5f);
        if (!wr.overlaps(view)) continue;

        Vec2 a = camera_.world_to_screen(wr.min);
        Vec2 b = camera_.world_to_screen(wr.max);
        Vec2 sz = b - a;
        a = {std::round(a.x), std::round(a.y)};  // snap the corner so the size stays constant while moving
        b = a + sz;

        if (s->texture) {
            bool fx = s->flip_x != (t->scale.x < 0.f);
            bool fy = s->flip_y != (t->scale.y < 0.f);
            r.draw_texture(s->texture, s->src, {a, b}, s->color, fx, fy);
        } else {
            r.fill_rect({a, b}, s->color);
        }
    }
}

void Engine::draw_debug_colliders(Renderer& r) {
    world_.view<Collider, Transform>().each([&](Entity e, Collider& c, Transform& t) {
        Color col = Color::from_hex(0x35D07F);  // static
        if (c.is_trigger) {
            col = Color::from_hex(0xFFD23F);
        } else if (const RigidBody* rb = world_.get<RigidBody>(e)) {
            if (rb->type == BodyType::Dynamic) col = Color::from_hex(0x3FD6FF);
            else if (rb->type == BodyType::Kinematic) col = Color::from_hex(0xFF9F40);
        }
        Vec2 ctr = camera_.world_to_screen(t.position + c.offset);
        if (c.shape == Shape::Box) {
            Vec2 h = c.half_extents * camera_.zoom;
            r.draw_rect({ctr - h, ctr + h}, col);
        } else {
            r.draw_circle(ctr, c.radius * camera_.zoom, col);
        }
    });
    for (const ContactPoint& cp : physics_.debug_contacts()) {
        Vec2 s = camera_.world_to_screen(cp.point);
        r.fill_rect({s - Vec2{2, 2}, s + Vec2{2, 2}}, Color::from_hex(0xFF4060));
        r.draw_line(s, s + cp.normal * 10.f, Color::from_hex(0xFF4060));
    }
}

void Engine::draw_debug_overlay(Renderer& r) {
    std::string text = "FPS " + std::to_string(static_cast<int>(fps_)) + "\nENTITIES " + std::to_string(world_.size()) +
                       "\nBODIES " + std::to_string(physics_.body_count()) + "\nCONTACTS " +
                       std::to_string(physics_.debug_contacts().size()) + "\nPARTICLES " +
                       std::to_string(particles_.alive()) + "\nBACKEND " + renderer_->name();
    Vec2 sz = r.measure_text(text, 2.f);
    const float x = static_cast<float>(r.width()) - sz.x - 12.f;  // top-right, clear of the game's HUD
    r.fill_rect({{x - 4, 4}, {x + sz.x + 4, 12 + sz.y}}, Color{0, 0, 0, 0.55f});
    r.draw_text(text, {x, 8}, Color::from_hex(0xE8F0FF), 2.f);
}

// ───────────────────────────── helpers ─────────────────────────────

Entity Engine::spawn_static_rect(Vec2 pos, Vec2 size, Color color, int z) {
    Entity e = world_.create();
    world_.add(e, Transform(pos));
    world_.add(e, Collider::box(size * 0.5f));
    world_.add(e, RigidBody::Static());
    world_.add(e, Sprite{color, size, z});
    return e;
}

Entity Engine::spawn_dynamic_rect(Vec2 pos, Vec2 size, Color color, float mass) {
    Entity e = world_.create();
    world_.add(e, Transform(pos));
    world_.add(e, Velocity{});
    world_.add(e, Collider::box(size * 0.5f));
    world_.add(e, RigidBody::Dynamic(mass));
    world_.add(e, Sprite{color, size, 5});
    return e;
}

}  // namespace prism
