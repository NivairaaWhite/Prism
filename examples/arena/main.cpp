// Prism Arena: a top-down survival shooter. The platformer demo's counterpart: no gravity, circle
// bodies, collision layers, projectiles, line-of-sight AI, scene switching, pause and slow-motion.
//
//   Windowed (needs SDL2 build):  prism_arena
//   Headless (always available):  prism_arena --headless --autopilot --frames 3600 --shot 1800 out.png
//   Custom arena:                 prism_arena --map my_arena.txt        ('#' wall, '.' floor, 'P' start, 'S' spawn)
//   Self-check:                   prism_arena --headless --autopilot --verify-determinism
//
// Controls: WASD/arrows = move | mouse = aim | hold left click = fire | Space/Shift = dash (brief invulnerability)
//           P = pause | Enter/click = start & retry | T = title (game over screen) | F1/F2 = debug | Esc = quit
//
// Structure: arena_data.hpp (enemy/pickup tables, tuning, wave planner, map validation: plain data),
//            arena_art.hpp (procedural sprites), this file (scenes: Title -> Play -> GameOver -> Play ...).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "arena/arena_art.hpp"
#include "arena/arena_data.hpp"
#include "common/harness.hpp"
#include "prism/prism.hpp"

using namespace prism;
using namespace arena;

namespace {

// ───────────────────────────── components ─────────────────────────────

struct Enemy {
    int type = 0;
    float hp = 1.f;
    float timer = 0.f;      // state timer (Charge) / shot cooldown (Kite)
    int state = 0;          // Charge: 0 approach, 1 wind-up, 2 dash, 3 recover
    Vec2 dash_dir{};
    float flash = 0.f;      // seconds of white hit-flash left
    float strafe = 1.f;     // Kite: which way it circles
};
struct Bullet {
    bool friendly = true;
    float damage = 1.f;
    int pierce = 0;                  // extra enemies it may pass through
    Entity last_hit = NULL_ENTITY;   // never hits the same enemy twice
};
struct Pickup {
    int kind = PK_Health;
};
struct SpawnWarning {
    int type = 0;
    float time = 0.9f;
};

// ───────────────────────────── shared state ─────────────────────────────

struct Art {
    TextureId floor[art::kFloorVariants]{}, wall = 0, player = 0, enemy[kEnemyTypes]{};
    TextureId bullet_player = 0, bullet_enemy = 0, pickup[PK_Count]{}, marker = 0;

    // Textures are cached by the engine, so this is cheap to call from every scene's on_enter.
    void load(Engine& e) {
        for (int i = 0; i < art::kFloorVariants; ++i)
            floor[i] = e.cached_texture("arena/floor" + std::to_string(i), [i] { return art::make_floor_tile(i); });
        wall = e.cached_texture("arena/wall", [] { return art::make_wall_tile(); });
        player = e.cached_texture("arena/player", [] { return art::make_player_sheet(); });
        for (int t = 0; t < kEnemyTypes; ++t)
            enemy[t] = e.cached_texture("arena/enemy" + std::to_string(t), [t] { return art::make_enemy_sheet(t); });
        bullet_player = e.cached_texture("arena/bullet_p", [] { return art::make_bullet(true); });
        bullet_enemy = e.cached_texture("arena/bullet_e", [] { return art::make_bullet(false); });
        for (int k = 0; k < PK_Count; ++k)
            pickup[k] = e.cached_texture("arena/pickup" + std::to_string(k), [k] { return art::make_pickup(k); });
        marker = e.cached_texture("arena/marker", [] { return art::make_spawn_marker(); });
    }
};

// Lives for the whole app run and is shared by all three scenes.
struct Session {
    ArenaMap map;
    Tuning tuning;
    float difficulty = 1.f;
    uint64_t seed = 1;
    bool autopilot = false;
    Art art;

    int runs = 0;                       // runs started so far
    int peak_score = 0, peak_wave = 0;  // best over every run (live: includes the run in progress)
    int last_score = 0, last_wave = 0, last_kills = 0;
    int total_kills = 0;
};
using SessionPtr = std::shared_ptr<Session>;

std::unique_ptr<Scene> make_title(SessionPtr s);
std::unique_ptr<Scene> make_play(SessionPtr s);
std::unique_ptr<Scene> make_game_over(SessionPtr s);

// ───────────────────────────── helpers ─────────────────────────────

float ui_scale(const Renderer& r) { return std::max(1.f, std::floor(static_cast<float>(r.height()) / 360.f)); }

void text_shadow(Renderer& r, const std::string& s, Vec2 p, Color c, float scale) {
    r.draw_text(s, p + Vec2{scale, scale}, Color{0, 0, 0, 0.65f * c.a}, scale);
    r.draw_text(s, p, c, scale);
}
void text_center(Renderer& r, const std::string& s, float y, Color c, float scale) {
    Vec2 sz = r.measure_text(s, scale);
    text_shadow(r, s, {(static_cast<float>(r.width()) - sz.x) * 0.5f, y}, c, scale);
}

Color hex(uint32_t rgb, float a = 1.f) { return Color::from_hex(rgb, a); }

void burst(Engine& e, Vec2 pos, int count, uint32_t color, float speed, float size, float life, bool circle = true) {
    ParticleBurst b;
    b.position = pos;
    b.count = count;
    b.speed_min = speed * 0.25f;
    b.speed_max = speed;
    b.life_min = life * 0.5f;
    b.life_max = life;
    b.size_start = size;
    b.size_end = 0.f;
    b.color_start = hex(color);
    b.color_end = hex(color, 0.f);
    b.gravity = 0.f;  // top-down: nothing falls
    b.drag = 3.f;
    b.circle = circle;
    e.particles().emit(b);
}

// Floor + wall tilemaps and the camera limits for the loaded map. `solid` adds the wall colliders.
Rect build_arena(Engine& e, Session& S, bool solid) {
    World& w = e.world();
    const ArenaMap& m = S.map;

    Tilemap ground(m.cols, m.nrows, TILE);
    uint16_t ids[art::kFloorVariants];
    for (int i = 0; i < art::kFloorVariants; ++i) {
        TileDef d;
        d.texture = S.art.floor[i];
        ids[i] = ground.define(d);
    }
    for (int y = 0; y < m.nrows; ++y)
        for (int x = 0; x < m.cols; ++x) ground.set(x, y, ids[(x * 7 + y * 13 + (x * y) % 5) % art::kFloorVariants]);
    ground.z_index = -20;
    Entity fe = w.create();
    w.set_name(fe, "Floor");
    w.add(fe, std::move(ground));

    TileDef wall;
    wall.texture = S.art.wall;
    wall.solid = true;
    Tilemap walls = Tilemap::from_ascii(m.rows, TILE, {0, 0}, {{'#', wall}});
    walls.z_index = -10;
    if (solid) walls.build_colliders(w, layer::WALL, 0.f);  // frictionless: sliding along walls feels right top-down
    const Rect bounds = walls.bounds();
    Entity we = w.create();
    w.set_name(we, "Walls");
    w.add(we, std::move(walls));

    Camera2D& cam = e.camera();
    cam.zoom = clampf(static_cast<float>(e.renderer().height()) / 720.f, 0.5f, 2.f);  // same field of view at any resolution
    cam.use_bounds = true;
    cam.bounds = bounds;
    cam.snap_to(bounds.center());
    return bounds;
}

void bind_keys(Engine& e) {
    Input& in = e.input();
    in.bind("left", {Key::A, Key::Left});
    in.bind("right", {Key::D, Key::Right});
    in.bind("up", {Key::W, Key::Up});
    in.bind("down", {Key::S, Key::Down});
    in.bind("dash", {Key::Space, Key::LShift});
    in.bind("confirm", {Key::Enter, Key::Space});
}

// ───────────────────────────── title ─────────────────────────────

class TitleScene : public Scene {
public:
    explicit TitleScene(SessionPtr s) : S(std::move(s)) {}

    void on_enter(Engine& e) override {
        bind_keys(e);
        S->art.load(e);
        build_arena(e, *S, false);
    }
    void on_update(Engine& e, float dt) override {
        t_ += dt;
        // Slow pan across the arena as a living backdrop.
        const Vec2 c = e.camera().bounds.center();
        e.camera().position = c + Vec2{std::cos(t_ * 0.21f) * 330.f, std::sin(t_ * 0.33f) * 160.f};

        Input& in = e.input();
        const bool go = in.action_pressed("confirm") || in.mouse_pressed(MouseButton::Left) || (S->autopilot && t_ > 0.6f);
        if (go && !leaving_) {
            leaving_ = true;
            e.set_scene(make_play(S));
        }
    }
    void on_render(Engine&, Renderer& r) override {
        const float ui = ui_scale(r), h = static_cast<float>(r.height());
        r.fill_rect({{0, 0}, {static_cast<float>(r.width()), h}}, Color{0.03f, 0.04f, 0.09f, 0.62f});
        const float bob = std::sin(t_ * 2.f) * 4.f * ui;
        text_center(r, "PRISM ARENA", h * 0.20f + bob, hex(0x4DD2FF), 8.f * ui);
        text_center(r, "SURVIVE THE WAVES", h * 0.20f + 72.f * ui, hex(0xE8F0FF), 2.f * ui);
        if (std::fmod(t_, 1.2f) < 0.8f) text_center(r, "PRESS ENTER OR CLICK TO START", h * 0.58f, hex(0xFFE14D), 2.f * ui);
        text_center(r, "WASD MOVE   MOUSE AIM   CLICK FIRE   SPACE DASH   P PAUSE", h * 0.70f, Color{1, 1, 1, 0.75f}, 1.f * ui);
        if (S->peak_score > 0)
            text_center(r, "BEST " + std::to_string(S->peak_score) + "  (WAVE " + std::to_string(S->peak_wave) + ")", h * 0.80f, hex(0xA9B8FF), 2.f * ui);
    }

private:
    SessionPtr S;
    float t_ = 0.f;
    bool leaving_ = false;
};

// ───────────────────────────── play ─────────────────────────────

class PlayScene : public Scene {
public:
    explicit PlayScene(SessionPtr s) : S(std::move(s)), T(S->tuning) {}

    void on_enter(Engine& e) override {
        bind_keys(e);
        S->art.load(e);
        ++S->runs;
        rng_ = Rng(S->seed * 0x9E3779B97F4A7C15ull + static_cast<uint64_t>(S->runs));
        hp_ = T.player_hp;
        e.physics().config.gravity = {0.f, 0.f};  // (also the engine default for this game; explicit for clarity)

        build_arena(e, *S, true);
        World& w = e.world();

        player_ = w.create();
        w.set_name(player_, "Player");
        w.add(player_, Transform(S->map.player));
        w.add(player_, Velocity{});
        Collider col = Collider::circle(T.player_radius);
        col.layer = layer::PLAYER;
        col.mask = layer::WALL | layer::ENEMY | layer::PICKUP | layer::ENEMY_BULLET;
        w.add(player_, col);
        RigidBody rb = RigidBody::Dynamic(1.5f);
        rb.friction = 0.f;
        rb.gravity_scale = 0.f;
        w.add(player_, rb);
        Sprite spr{Color{1, 1, 1, 1}, {32, 32}, 20};
        spr.texture = S->art.player;
        spr.src = {0, 0, art::kPlayerPx, art::kPlayerPx};
        w.add(player_, spr);
        w.add(player_, PlayerTag{});
        w.add(player_, CameraTarget{});
        e.camera().snap_to(S->map.player);

        // Registered here, removed by the engine when the scene exits: no unsubscribe bookkeeping.
        e.events().subscribe<TriggerEvent>([this, &e](const TriggerEvent& ev) { on_trigger(e, ev); });
        e.events().subscribe<PhysicsFault>([this, &e](const PhysicsFault& f) {  // a NaN somewhere: don't let it spread
            if (f.entity != player_) e.world().destroy(f.entity);
        });
    }

    // ---- simulation (fixed 60 Hz, frozen while paused) ----
    void on_fixed_update(Engine& e, float dt) override {
        banner_t_ = std::max(0.f, banner_t_ - dt);
        toast_t_ = std::max(0.f, toast_t_ - dt);
        invuln_ = std::max(0.f, invuln_ - dt);
        fire_cd_ -= dt;
        dash_cd_ = std::max(0.f, dash_cd_ - dt);
        dash_time_ = std::max(0.f, dash_time_ - dt);
        dash_buffer_ = std::max(0.f, dash_buffer_ - dt);
        rapid_t_ = std::max(0.f, rapid_t_ - dt);
        spread_t_ = std::max(0.f, spread_t_ - dt);
        combo_timer_ -= dt;
        if (combo_timer_ <= 0.f) chain_ = 0;

        if (!dead_) {
            update_player(e, dt);
            contact_damage(e);
        }
        update_enemies(e, dt);
        update_markers(e, dt);
        update_waves(e, dt);
    }

    // ---- per-frame: input edges, aim, feedback (runs even while paused) ----
    void on_update(Engine& e, float dt) override {
        Input& in = e.input();
        if (S->autopilot && !e.paused) drive_bot(e);  // a bot, like a player, does nothing while the game is paused
        if (in.pressed(Key::P) && !dead_) e.paused = !e.paused;
        if (in.action_pressed("dash")) dash_buffer_ = 0.15f;

        update_aim(e);

        // Hit-stop and death slow-motion both ride on the engine's time scale.
        float target = dead_ ? 0.25f : 1.f;
        if (hitstop_ > 0.f) {
            hitstop_ -= dt;
            target = 0.03f;
        }
        e.time_scale = target;

        // Pickups blink when about to expire.
        e.world().view<Pickup, Lifetime, Sprite>().each([](Entity, Pickup&, Lifetime& l, Sprite& s) {
            s.visible = l.remaining > 2.f || std::fmod(l.remaining, 0.25f) < 0.15f;
        });

        S->peak_score = std::max(S->peak_score, score_);
        S->peak_wave = std::max(S->peak_wave, wave_);

        if (dead_ && !leaving_) {
            death_t_ -= dt;
            if (death_t_ <= 0.f) {
                leaving_ = true;
                S->last_score = score_;
                S->last_wave = wave_;
                S->last_kills = kills_;
                e.set_scene(make_game_over(S));
            }
        }
    }

    void on_render(Engine& e, Renderer& r) override {
        World& w = e.world();
        const Camera2D& cam = e.camera();
        const float ui = ui_scale(r), W = static_cast<float>(r.width()), H = static_cast<float>(r.height());
        auto* ptf = w.get<Transform>(player_);

        // Laser sight: from the muzzle to the first wall.
        if (ptf && !dead_ && !e.paused) {
            Vec2 from = ptf->position;
            float len = 700.f;
            if (auto hit = e.physics().raycast(w, from, aim_, len, layer::WALL, player_)) len = hit->distance;
            r.draw_line(cam.world_to_screen(from + aim_ * 16.f), cam.world_to_screen(from + aim_ * len), Color{1.f, 0.35f, 0.35f, 0.28f});
        }

        // Crosshair at the mouse.
        if (!dead_) {
            const Vec2 m = e.input().mouse_pos();
            const Color c{1, 1, 1, 0.9f};
            const float g = 4.f * ui, l = 5.f * ui, t = std::max(1.f, ui);
            r.fill_rect({{m.x - g - l, m.y - t * 0.5f}, {m.x - g, m.y + t * 0.5f}}, c);
            r.fill_rect({{m.x + g, m.y - t * 0.5f}, {m.x + g + l, m.y + t * 0.5f}}, c);
            r.fill_rect({{m.x - t * 0.5f, m.y - g - l}, {m.x + t * 0.5f, m.y - g}}, c);
            r.fill_rect({{m.x - t * 0.5f, m.y + g}, {m.x + t * 0.5f, m.y + g + l}}, c);
        }

        // Damage vignette.
        if (invuln_ > 0.4f && !dead_) {
            const float a = (invuln_ - 0.4f) * 0.8f;
            const float e_ = 14.f * ui;
            const Color c{0.9f, 0.1f, 0.15f, a};
            r.fill_rect({{0, 0}, {W, e_}}, c);
            r.fill_rect({{0, H - e_}, {W, H}}, c);
            r.fill_rect({{0, e_}, {e_, H - e_}}, c);
            r.fill_rect({{W - e_, e_}, {W, H - e_}}, c);
        }

        // ---- HUD ----
        const float m = 10.f * ui, bw = 150.f * ui, bh = 9.f * ui;
        const float frac = clampf(hp_ / T.player_hp, 0.f, 1.f);
        r.fill_rect({{m - ui, m - ui}, {m + bw + ui, m + bh + ui}}, Color{0, 0, 0, 0.7f});
        r.fill_rect({{m, m}, {m + bw * frac, m + bh}}, Color::lerp(hex(0xFF4060), hex(0x4DFF88), frac));
        text_shadow(r, "HP " + std::to_string(static_cast<int>(std::ceil(std::max(0.f, hp_)))), {m + bw + 6.f * ui, m + 1.f * ui}, Color{1, 1, 1, 1}, 1.f * ui);
        const float dfrac = 1.f - clampf(dash_cd_ / T.dash_cooldown, 0.f, 1.f);
        r.fill_rect({{m - ui, m + bh + 5.f * ui - ui}, {m + bw * 0.5f + ui, m + bh + 9.f * ui + ui}}, Color{0, 0, 0, 0.7f});
        r.fill_rect({{m, m + bh + 5.f * ui}, {m + bw * 0.5f * dfrac, m + bh + 9.f * ui}}, dfrac >= 1.f ? hex(0x4DD2FF) : hex(0x3A6C80));
        text_shadow(r, "DASH", {m + bw * 0.5f + 6.f * ui, m + bh + 4.f * ui}, Color{1, 1, 1, dfrac >= 1.f ? 0.95f : 0.45f}, 1.f * ui);

        std::string score = "SCORE " + std::to_string(score_);
        Vec2 ssz = r.measure_text(score, 2.f * ui);
        text_shadow(r, score, {W - ssz.x - m, m}, hex(0xFFE14D), 2.f * ui);
        if (combo_ > 1) {
            std::string cs = "COMBO X" + std::to_string(combo_);
            Vec2 csz = r.measure_text(cs, 1.f * ui);
            text_shadow(r, cs, {W - csz.x - m, m + 18.f * ui}, hex(0xFF9F1C), 1.f * ui);
        }
        float py = m + 30.f * ui;
        auto power = [&](const char* name, float t, uint32_t color) {
            if (t <= 0.f) return;
            std::string s = std::string(name) + " " + std::to_string(static_cast<int>(std::ceil(t)));
            Vec2 sz = r.measure_text(s, 1.f * ui);
            text_shadow(r, s, {W - sz.x - m, py}, hex(color), 1.f * ui);
            py += 11.f * ui;
        };
        power("RAPID", rapid_t_, kPickups[PK_Rapid].color);
        power("SPREAD", spread_t_, kPickups[PK_Spread].color);

        const int left = static_cast<int>(w.view<Enemy>().count() + w.view<SpawnWarning>().count()) + static_cast<int>(queue_.size() - qpos_);
        text_center(r, "WAVE " + std::to_string(wave_) + (left > 0 ? "   ENEMIES " + std::to_string(left) : ""), m, Color{1, 1, 1, 0.9f}, 1.5f * ui);

        if (banner_t_ > 0.f && wave_ > 0) {
            const float a = clampf(banner_t_ / 0.5f, 0.f, 1.f);
            text_center(r, "WAVE " + std::to_string(wave_), H * 0.30f, Color{0.3f, 0.85f, 1.f, a}, 5.f * ui);
        }
        if (toast_t_ > 0.f) text_center(r, toast_, H * 0.72f - (1.f - clampf(toast_t_, 0.f, 1.f)) * 10.f * ui, Color{1, 1, 1, clampf(toast_t_ * 2.f, 0.f, 1.f)}, 2.f * ui);
        if (in_intermission_ && wave_ > 0 && !dead_) text_center(r, "WAVE CLEARED  +" + std::to_string(50 * wave_), H * 0.42f, hex(0x4DFF88, clampf(intermission_t_, 0.f, 1.f)), 2.f * ui);

        if (e.paused) {
            r.fill_rect({{0, 0}, {W, H}}, Color{0, 0, 0, 0.45f});
            text_center(r, "PAUSED", H * 0.40f, Color{1, 1, 1, 1}, 6.f * ui);
            text_center(r, "P TO RESUME", H * 0.40f + 56.f * ui, Color{1, 1, 1, 0.8f}, 2.f * ui);
        }
    }

private:
    // ───────── player ─────────

    void update_player(Engine& e, float dt) {
        World& w = e.world();
        Input& in = e.input();
        auto* tf = w.get<Transform>(player_);
        auto* vel = w.get<Velocity>(player_);
        if (!tf || !vel) return;

        Vec2 want{in.axis("left", "right"), in.axis("up", "down")};
        want = want.normalized();

        // Dash: a short burst in the move direction (or the aim direction when standing still).
        if (dash_buffer_ > 0.f && dash_cd_ <= 0.f && dash_time_ <= 0.f) {
            dash_dir_ = want.length_sq() > 0.f ? want : aim_;
            dash_time_ = T.dash_time;
            dash_cd_ = T.dash_cooldown;
            invuln_ = std::max(invuln_, T.dash_invuln);
            dash_buffer_ = 0.f;
            burst(e, tf->position, 10, 0x4DD2FF, 160.f, 6.f, 0.3f);
        }
        if (dash_time_ > 0.f) {
            vel->linear = dash_dir_ * T.dash_speed;
        } else {
            Vec2 target = want * T.player_speed;
            Vec2 d = target - vel->linear;
            const float step = T.player_accel * dt, len = d.length();
            vel->linear = len <= step ? target : vel->linear + d * (step / len);
        }

        // Fire (hold the button).
        if (in.mouse_down(MouseButton::Left) && fire_cd_ <= 0.f) {
            fire(e, tf->position);
            fire_cd_ = T.fire_interval / (rapid_t_ > 0.f ? 2.f : 1.f);
        }
    }

    void fire(Engine& e, Vec2 from) {
        const int n = spread_t_ > 0.f ? 5 : 1;
        const float base = std::atan2(aim_.y, aim_.x);
        for (int i = 0; i < n; ++i) {
            const float off = (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * 0.13f + rng_.range(-0.02f, 0.02f);
            const Vec2 dir = Vec2::from_angle(base + off);
            spawn_bullet(e, from + dir * (T.player_radius + 5.f), dir, T.bullet_speed, T.bullet_damage, true, T.bullet_life, 0);
        }
        burst(e, from + aim_ * (T.player_radius + 8.f), 3, 0xFFF2A8, 140.f, 4.f, 0.12f, false);
        e.camera().add_trauma(0.015f);
    }

    void update_aim(Engine& e) {
        World& w = e.world();
        auto* tf = w.get<Transform>(player_);
        auto* spr = w.get<Sprite>(player_);
        auto* ct = w.get<CameraTarget>(player_);
        if (!tf || dead_) return;
        const Vec2 mouse_world = e.screen_to_world(e.input().mouse_pos());
        Vec2 d = mouse_world - tf->position;
        if (d.length_sq() > 4.f) aim_ = d.normalized();
        if (spr) spr->src = {art::player_frame_for(std::atan2(aim_.y, aim_.x)) * art::kPlayerPx, 0, art::kPlayerPx, art::kPlayerPx};
        if (spr) spr->color = invuln_ > 0.f && dash_time_ <= 0.f && std::fmod(invuln_, 0.12f) < 0.06f ? Color{1, 1, 1, 0.35f} : Color{1, 1, 1, 1};
        if (ct) {  // look-ahead toward the cursor, measured on screen so it does not feed back into the camera
            Vec2 off = (e.input().mouse_pos() - e.camera().viewport * 0.5f) * (0.22f / std::max(0.1f, e.camera().zoom));
            const float len = off.length();
            ct->offset = len > 150.f ? off * (150.f / len) : off;
        }
    }

    void contact_damage(Engine& e) {
        World& w = e.world();
        auto* ptf = w.get<Transform>(player_);
        if (!ptf || invuln_ > 0.f) return;
        std::vector<Entity> ids;
        w.view<Enemy>().each([&](Entity id, Enemy&) { ids.push_back(id); });
        for (Entity id : ids) {
            auto* en = w.get<Enemy>(id);
            auto* tf = w.get<Transform>(id);
            if (!en || !tf) continue;
            const EnemyDef& d = kEnemies[en->type];
            if ((tf->position - ptf->position).length() <= d.radius + T.player_radius + 1.5f) {
                hurt_player(e, d.contact_damage, (ptf->position - tf->position).normalized());
                return;
            }
        }
    }

    void hurt_player(Engine& e, float dmg, Vec2 dir) {
        if (dead_ || invuln_ > 0.f) return;
        World& w = e.world();
        hp_ -= dmg;
        invuln_ = T.hurt_invuln;
        e.camera().add_trauma(0.45f);
        hitstop_ = 0.05f;
        chain_ = 0;
        auto* tf = w.get<Transform>(player_);
        auto* vel = w.get<Velocity>(player_);
        if (!tf || !vel) return;
        vel->linear += dir * 320.f;
        const Vec2 pos = tf->position;
        burst(e, pos, 14, 0xFF4060, 220.f, 6.f, 0.45f);
        if (hp_ <= 0.f) die(e, pos);
    }

    void die(Engine& e, Vec2 pos) {
        dead_ = true;
        death_t_ = 1.4f;
        burst(e, pos, 60, 0x4DD2FF, 380.f, 8.f, 0.9f);
        burst(e, pos, 30, 0xFFFFFF, 260.f, 5.f, 0.6f, false);
        e.camera().add_trauma(1.f);
        if (auto* spr = e.world().get<Sprite>(player_)) spr->visible = false;
        if (auto* v = e.world().get<Velocity>(player_)) v->linear = {0, 0};
    }

    // ───────── projectiles ─────────

    void spawn_bullet(Engine& e, Vec2 pos, Vec2 dir, float speed, float damage, bool friendly, float life, int pierce) {
        World& w = e.world();
        Entity b = w.create();
        w.add(b, Transform(pos));
        w.add(b, Velocity{dir * speed, 0.f});
        Collider c = Collider::circle(friendly ? T.bullet_radius : 5.f);
        c.is_trigger = true;  // reports overlaps; never pushes anything
        c.layer = friendly ? layer::PLAYER_BULLET : layer::ENEMY_BULLET;
        c.mask = friendly ? (layer::WALL | layer::ENEMY) : (layer::WALL | layer::PLAYER);
        w.add(b, c);
        RigidBody rb = RigidBody::Dynamic();
        rb.gravity_scale = 0.f;
        w.add(b, rb);
        Sprite s{Color{1, 1, 1, 1}, {16, 16}, 15};
        s.texture = friendly ? S->art.bullet_player : S->art.bullet_enemy;
        w.add(b, s);
        w.add(b, Bullet{friendly, damage, pierce, NULL_ENTITY});
        w.add(b, Lifetime{life});
    }

    void on_trigger(Engine& e, const TriggerEvent& ev) {
        if (!ev.entered) return;
        World& w = e.world();
        if (Bullet* b = w.get<Bullet>(ev.trigger)) {
            auto* btf = w.get<Transform>(ev.trigger);
            auto* bv = w.get<Velocity>(ev.trigger);
            if (!btf || !bv) return;
            const Vec2 pos = btf->position, dir = bv->linear.normalized();
            if (b->friendly) {
                if (w.has<Enemy>(ev.other)) {
                    if (ev.other == b->last_hit) return;
                    b->last_hit = ev.other;
                    const bool spent = --b->pierce < 0;
                    const float dmg = b->damage;
                    if (spent) w.destroy(ev.trigger);  // before damage_enemy: it may spawn entities and move pools
                    damage_enemy(e, ev.other, dmg, dir);
                    return;
                }
                burst(e, pos, 4, 0x4DD2FF, 120.f, 3.f, 0.18f, false);  // wall
                w.destroy(ev.trigger);
            } else {
                if (w.has<PlayerTag>(ev.other)) {
                    const float dmg = b->damage;
                    w.destroy(ev.trigger);
                    hurt_player(e, dmg, dir);
                    return;
                }
                burst(e, pos, 4, 0xFF6A3D, 120.f, 3.f, 0.18f, false);
                w.destroy(ev.trigger);
            }
        } else if (Pickup* pk = w.get<Pickup>(ev.trigger)) {
            if (!w.has<PlayerTag>(ev.other) || dead_) return;
            const int kind = pk->kind;
            w.destroy(ev.trigger);
            collect(e, kind);
        }
    }

    // ───────── enemies ─────────

    void place_warning(Engine& e, int type) {
        World& w = e.world();
        auto* ptf = w.get<Transform>(player_);
        const Vec2 pp = ptf ? ptf->position : S->map.player;
        // Prefer spawn points that are not on top of the player; fall back to the farthest one.
        std::vector<Vec2> far;
        Vec2 farthest = S->map.spawns[0];
        for (const Vec2& s : S->map.spawns) {
            if ((s - pp).length() >= 260.f) far.push_back(s);
            if ((s - pp).length() > (farthest - pp).length()) farthest = s;
        }
        Vec2 at = far.empty() ? farthest : far[static_cast<size_t>(rng_.range_int(0, static_cast<int>(far.size()) - 1))];
        at += Vec2{rng_.range(-14.f, 14.f), rng_.range(-14.f, 14.f)};

        Entity m = w.create();
        w.add(m, Transform(at));
        Sprite s{Color{1, 1, 1, 0.f}, {40, 40}, 2};
        s.texture = S->art.marker;
        w.add(m, s);
        w.add(m, SpawnWarning{type, 0.9f});
    }

    void update_markers(Engine& e, float dt) {
        World& w = e.world();
        std::vector<Entity> ids;
        w.view<SpawnWarning>().each([&](Entity id, SpawnWarning&) { ids.push_back(id); });
        for (Entity id : ids) {
            auto* sw = w.get<SpawnWarning>(id);
            auto* tf = w.get<Transform>(id);
            auto* spr = w.get<Sprite>(id);
            if (!sw || !tf || !spr) continue;
            sw->time -= dt;
            const float k = clampf(sw->time / 0.9f, 0.f, 1.f);
            tf->scale = Vec2{1.f, 1.f} * (0.7f + 0.9f * k + 0.12f * std::sin(sw->time * 24.f));
            spr->color = Color{1, 1, 1, clampf(1.f - k, 0.f, 1.f) * 0.9f + 0.1f};
            if (sw->time <= 0.f) {
                const Vec2 at = tf->position;
                const int type = sw->type;
                w.destroy(id);
                spawn_enemy(e, type, at);
            }
        }
    }

    Entity spawn_enemy(Engine& e, int type, Vec2 pos) {
        World& w = e.world();
        const EnemyDef& d = kEnemies[type];
        Entity en = w.create();
        w.add(en, Transform(pos));
        w.add(en, Velocity{});
        Collider c = Collider::circle(d.radius);
        c.layer = layer::ENEMY;
        c.mask = layer::WALL | layer::ENEMY | layer::PLAYER | layer::PLAYER_BULLET;
        w.add(en, c);
        RigidBody rb = RigidBody::Dynamic(d.mass);
        rb.friction = 0.f;
        rb.gravity_scale = 0.f;
        rb.restitution = 0.1f;
        w.add(en, rb);
        Sprite s{Color{1, 1, 1, 1}, {static_cast<float>(art::kEnemyPx * 2), static_cast<float>(art::kEnemyPx * 2)}, 10};
        s.texture = S->art.enemy[type];
        s.src = {0, 0, art::kEnemyPx, art::kEnemyPx};
        w.add(en, s);
        Animator an;
        an.columns = art::kEnemyFrames;
        an.frame_w = an.frame_h = art::kEnemyPx;
        an.add_clip({"move", art::EF_Walk0, art::EF_Walk1, 5.f, true});
        an.add_clip({"flash", art::EF_Flash, art::EF_Flash, 1.f, false});
        an.add_clip({"windup", art::EF_Windup, art::EF_Windup, 1.f, false});
        an.time = rng_.range(0.f, 1.f);  // keep a crowd from animating in lock-step
        w.add(en, an);
        Enemy data;
        data.type = type;
        data.hp = d.hp * (0.85f + 0.15f * S->difficulty);
        data.timer = d.fire_interval * rng_.range(0.4f, 1.f);
        data.strafe = rng_.next01() < 0.5f ? -1.f : 1.f;
        w.add(en, data);
        return en;
    }

    // True when a body of `radius` can travel in a straight line from `from` to `to` without touching a wall
    // (three parallel rays: the centre and both edges).
    bool path_clear(Engine& e, Entity self, Vec2 from, Vec2 to, float radius) {
        const Vec2 d = to - from;
        const float dist = d.length();
        if (dist < 1.f) return true;
        const Vec2 dir = d / dist, side = dir.perp() * (radius * 0.9f);
        World& w = e.world();
        Physics& ph = e.physics();
        return !ph.raycast(w, from, dir, dist, layer::WALL, self) && !ph.raycast(w, from + side, dir, dist, layer::WALL, self) &&
               !ph.raycast(w, from - side, dir, dist, layer::WALL, self);
    }

    // Which way to head from `from` to reach `to`: straight there when nothing is in the way, otherwise
    // around the cover by following the flow field.
    Vec2 navigate(Engine& e, Entity self, Vec2 from, Vec2 to, float radius) {
        const Vec2 d = to - from;
        if (d.length() < 1.f) return {};
        if (d.length() < TILE * 2.f || path_clear(e, self, from, to, radius)) return d.normalized();
        const Vec2 f = flow_.direction(from);
        return f.length_sq() > 0.f ? f : d.normalized();
    }

    void update_enemies(Engine& e, float dt) {
        World& w = e.world();
        auto* ptf = w.get<Transform>(player_);
        const bool have_player = ptf && !dead_;
        const Vec2 pp = ptf ? ptf->position : Vec2{};

        if (have_player) {  // one breadth-first pass serves every enemy; redo it only when the player changes tile
            const int tx = static_cast<int>(std::floor(pp.x / TILE)), ty = static_cast<int>(std::floor(pp.y / TILE));
            if (tx != flow_.target_x() || ty != flow_.target_y()) flow_.build(S->map, tx, ty);
        }

        // Snapshot the handles first: spawning bullets adds components, which may move the pools.
        std::vector<Entity> ids;
        w.view<Enemy>().each([&](Entity id, Enemy&) { ids.push_back(id); });

        struct Shot {
            Vec2 pos, dir;
            float speed, damage;
        };
        std::vector<Shot> shots;

        for (Entity id : ids) {
            auto* en = w.get<Enemy>(id);
            auto* tf = w.get<Transform>(id);
            auto* vel = w.get<Velocity>(id);
            auto* an = w.get<Animator>(id);
            if (!en || !tf || !vel || !an) continue;
            const EnemyDef& d = kEnemies[en->type];
            en->flash = std::max(0.f, en->flash - dt);

            Vec2 desired{0, 0};
            float accel = d.accel;
            const char* clip = "move";
            const Vec2 to = have_player ? pp - tf->position : Vec2{};
            const float dist = to.length();
            const Vec2 dir = to.normalized();

            switch (d.behavior) {
                case Behavior::Chase:
                    if (have_player) desired = navigate(e, id, tf->position, pp, d.radius) * d.speed;
                    break;
                case Behavior::Kite: {
                    en->timer -= dt;
                    auto wall = have_player ? e.physics().raycast(w, tf->position, dir, dist, layer::WALL, id) : std::nullopt;
                    const bool los = have_player && !wall;
                    if (!los) desired = have_player ? navigate(e, id, tf->position, pp, d.radius) * d.speed : Vec2{};  // get back into sight
                    else if (dist < d.keep_distance - 50.f) desired = -dir * d.speed;
                    else if (dist > d.keep_distance + 50.f) desired = dir * d.speed;
                    else desired = dir.perp() * (en->strafe * d.speed * 0.6f);
                    if (los && en->timer <= 0.f && dist < 560.f) {
                        shots.push_back({tf->position + dir * (d.radius + 6.f), dir, d.bullet_speed, d.bullet_damage});
                        en->timer = d.fire_interval;
                    }
                    break;
                }
                case Behavior::Charge: {
                    en->timer -= dt;
                    switch (en->state) {
                        case 0:  // approach, then telegraph when close with a clear path
                            if (have_player) desired = navigate(e, id, tf->position, pp, d.radius) * d.speed;
                            if (have_player && dist < 230.f) {
                                if (path_clear(e, id, tf->position, pp, d.radius)) {
                                    en->state = 1;
                                    en->timer = d.windup;
                                    en->dash_dir = dir;
                                }
                            }
                            break;
                        case 1:  // wind-up: stand still, flash, aim at where the player is *now*
                            clip = "windup";
                            accel = 3000.f;
                            if (en->timer <= 0.f) {
                                en->state = 2;
                                en->timer = d.dash_time;
                            }
                            break;
                        case 2:  // dash in a straight line
                            desired = en->dash_dir * d.dash_speed;
                            accel = 6000.f;
                            if (en->timer <= 0.f) {
                                en->state = 3;
                                en->timer = d.recover;
                            }
                            break;
                        default:  // recover
                            accel = 1500.f;
                            if (en->timer <= 0.f) en->state = 0;
                            break;
                    }
                    break;
                }
            }

            Vec2 dv = desired - vel->linear;
            const float step = accel * dt, len = dv.length();
            vel->linear = len <= step ? desired : vel->linear + dv * (step / len);
            if (en->flash > 0.f) clip = "flash";
            an->play(clip);
        }

        for (const Shot& s : shots) spawn_bullet(e, s.pos, s.dir, s.speed, s.damage, false, 3.f, 0);
    }

    void damage_enemy(Engine& e, Entity id, float dmg, Vec2 dir) {
        World& w = e.world();
        auto* en = w.get<Enemy>(id);
        auto* tf = w.get<Transform>(id);
        auto* vel = w.get<Velocity>(id);
        if (!en || !tf || !vel) return;
        const EnemyDef& d = kEnemies[en->type];
        en->hp -= dmg;
        en->flash = 0.07f;
        vel->linear += dir * (120.f / std::max(0.5f, d.mass));
        const Vec2 pos = tf->position;
        const bool dead = en->hp <= 0.f;
        burst(e, pos, 3, d.color, 150.f, 4.f, 0.25f, false);
        if (dead) kill_enemy(e, id);
    }

    void kill_enemy(Engine& e, Entity id) {
        World& w = e.world();
        auto* en = w.get<Enemy>(id);
        auto* tf = w.get<Transform>(id);
        if (!en || !tf) return;
        const int type = en->type;
        const Vec2 pos = tf->position;
        const EnemyDef& d = kEnemies[type];
        w.destroy(id);  // gone before anything below adds entities

        ++kills_;
        ++S->total_kills;
        ++chain_;
        combo_timer_ = 2.5f;
        combo_ = std::min(5, 1 + chain_ / 4);
        score_ += d.score * combo_;

        burst(e, pos, static_cast<int>(8 + d.radius), d.color, 260.f, 7.f, 0.55f);
        burst(e, pos, 6, 0xFFFFFF, 200.f, 4.f, 0.3f, false);
        e.camera().add_trauma(d.radius > 14.f ? 0.28f : 0.08f);
        if (d.radius > 14.f) hitstop_ = std::max(hitstop_, 0.05f);

        if (d.split_into > 0) {
            const int alive = static_cast<int>(w.view<Enemy>().count());
            if (alive + d.split_into <= T.max_enemies * 3 / 2)  // splits ignore the spawn cap, but not a hard one
                for (int i = 0; i < d.split_into; ++i) {
                    const float a = TAU * (static_cast<float>(i) / static_cast<float>(d.split_into)) + rng_.range(0.f, 1.f);
                    Entity child = spawn_enemy(e, d.split_type, pos + Vec2::from_angle(a) * (d.radius * 0.8f));
                    if (auto* v = w.get<Velocity>(child)) v->linear = Vec2::from_angle(a) * 220.f;
                }
        }
        if (rng_.next01() < d.drop_chance) drop_pickup(e, pos);
    }

    void drop_pickup(Engine& e, Vec2 pos) {
        World& w = e.world();
        int total = 0;
        for (int k = 0; k < PK_Count; ++k) total += kPickups[k].weight;
        int pick = rng_.range_int(0, total - 1), kind = PK_Count - 1;
        for (int k = 0; k < PK_Count; ++k) {
            pick -= kPickups[k].weight;
            if (pick < 0) {
                kind = k;
                break;
            }
        }
        Entity p = w.create();
        w.add(p, Transform(pos));
        Collider c = Collider::circle(13.f);
        c.is_trigger = true;
        c.layer = layer::PICKUP;
        c.mask = layer::PLAYER;
        w.add(p, c);
        Sprite s{Color{1, 1, 1, 1}, {28, 28}, 5};
        s.texture = S->art.pickup[kind];
        s.src = {0, 0, art::kPickupPx, art::kPickupPx};
        w.add(p, s);
        Animator an;
        an.columns = 2;
        an.frame_w = an.frame_h = art::kPickupPx;
        an.add_clip({"pulse", 0, 1, 3.f, true});
        w.add(p, an);
        w.add(p, Pickup{kind});
        w.add(p, Lifetime{T.pickup_life});
    }

    void collect(Engine& e, int kind) {
        World& w = e.world();
        auto* ptf = w.get<Transform>(player_);
        const Vec2 pos = ptf ? ptf->position : Vec2{};
        const uint32_t color = kPickups[kind].color;
        burst(e, pos, 16, color, 240.f, 6.f, 0.5f);
        switch (kind) {
            case PK_Health:
                hp_ = std::min(T.player_hp, hp_ + 30.f);
                toast("+30 HP");
                break;
            case PK_Rapid:
                rapid_t_ = kPickups[kind].duration;
                toast("RAPID FIRE");
                break;
            case PK_Spread:
                spread_t_ = kPickups[kind].duration;
                toast("SPREAD SHOT");
                break;
            default: {  // NOVA: damage everything nearby and wipe enemy bullets
                std::vector<Entity> bullets, near;
                w.view<Bullet>().each([&](Entity id, Bullet& b) {
                    if (!b.friendly) bullets.push_back(id);
                });
                for (Entity id : bullets) w.destroy(id);
                w.view<Enemy, Transform>().each([&](Entity id, Enemy&, Transform& t) {
                    if ((t.position - pos).length() < 300.f) near.push_back(id);
                });
                for (Entity id : near) {
                    auto* tf = w.get<Transform>(id);
                    const Vec2 away = tf ? (tf->position - pos).normalized() : Vec2{};
                    damage_enemy(e, id, 8.f, away);
                }
                ParticleBurst ring;
                ring.position = pos;
                ring.count = 80;
                ring.speed_min = 380.f;
                ring.speed_max = 420.f;
                ring.life_min = 0.35f;
                ring.life_max = 0.45f;
                ring.size_start = 7.f;
                ring.size_end = 0.f;
                ring.color_start = hex(0xFF4DD2);
                ring.color_end = hex(0xFF4DD2, 0.f);
                ring.gravity = 0.f;
                ring.drag = 2.f;
                ring.circle = true;
                e.particles().emit(ring);
                e.camera().add_trauma(0.7f);
                toast("NOVA!");
                break;
            }
        }
    }

    void toast(const std::string& s) {
        toast_ = s;
        toast_t_ = 1.4f;
    }

    // ───────── waves ─────────

    void update_waves(Engine& e, float dt) {
        World& w = e.world();
        if (dead_) return;
        const int alive = static_cast<int>(w.view<Enemy>().count() + w.view<SpawnWarning>().count());
        const bool queue_left = qpos_ < queue_.size();

        if (!queue_left && alive == 0) {
            if (!in_intermission_) {
                in_intermission_ = true;
                intermission_t_ = wave_ == 0 ? 1.0f : T.intermission;
                if (wave_ > 0) score_ += 50 * wave_;  // clear bonus
            }
            intermission_t_ -= dt;
            if (intermission_t_ <= 0.f) {
                in_intermission_ = false;
                ++wave_;
                queue_ = plan_wave(wave_, S->difficulty, T.max_wave_units, rng_);
                qpos_ = 0;
                spawn_t_ = 0.2f;
                banner_t_ = 2.0f;
            }
        } else if (queue_left) {
            spawn_t_ -= dt;
            if (spawn_t_ <= 0.f && alive < T.max_enemies) {
                place_warning(e, queue_[qpos_++]);
                spawn_t_ = std::max(0.15f, 0.55f - 0.03f * static_cast<float>(wave_));
            }
        }
    }

    // ───────── autopilot ─────────
    // A small bot that plays through the same Input the human uses: it keeps its distance from
    // melee enemies, sidesteps incoming bullets, avoids walls, collects pickups, and aims at the
    // most dangerous visible enemy (leading its shot).

    void drive_bot(Engine& e) {
        World& w = e.world();
        Input& in = e.input();
        auto* ptf = w.get<Transform>(player_);
        auto release = [&] {
            for (Key k : {Key::W, Key::A, Key::S, Key::D, Key::Space}) in.set_key(k, false);
            in.set_mouse_button(MouseButton::Left, false);
        };
        if (!ptf || dead_) return release();
        const Vec2 p = ptf->position;

        struct Foe {
            Entity id;
            Vec2 pos, vel;
            const EnemyDef* def;
            float d;
        };
        std::vector<Foe> foes;
        w.view<Enemy, Transform, Velocity>().each([&](Entity id, Enemy& en, Transform& t, Velocity& v) {
            foes.push_back({id, t.position, v.linear, &kEnemies[en.type], (t.position - p).length()});
        });
        std::sort(foes.begin(), foes.end(), [](const Foe& a, const Foe& b) { return a.d != b.d ? a.d < b.d : a.id.pack() < b.id.pack(); });

        Vec2 m{0, 0};
        for (const Foe& f : foes) {
            if (f.d > 320.f) break;
            const Vec2 away = (p - f.pos) / std::max(f.d, 1.f);
            const float k = 1.f - f.d / 320.f;
            const float gain = f.def->behavior == Behavior::Kite ? 0.5f : 3.2f;
            m += away * (k * k * gain) + away.perp() * (k * 0.9f);  // flee, with a sideways bias so the bot circles
        }

        // Sidestep enemy bullets that are heading our way.
        w.view<Bullet, Transform, Velocity>().each([&](Entity, Bullet& b, Transform& t, Velocity& v) {
            if (b.friendly) return;
            const Vec2 rel = p - t.position, dir = v.linear.normalized();
            const float along = rel.dot(dir);
            if (along < 0.f || along > 220.f) return;
            const Vec2 off = rel - dir * along;
            const float lateral = off.length();
            if (lateral > 42.f) return;
            const Vec2 side = lateral > 0.5f ? off / lateral : dir.perp();
            m += side * (2.2f * (1.f - lateral / 42.f));
        });

        // Walls: push away from anything close in eight directions.
        for (int i = 0; i < 8; ++i) {
            const Vec2 d = Vec2::from_angle(static_cast<float>(i) * TAU / 8.f);
            if (auto hit = e.physics().raycast(w, p, d, 90.f, layer::WALL, player_))
                m -= d * ((90.f - hit->distance) / 90.f * 2.6f);
        }

        // Pickups: health when hurt, power-ups when close.
        w.view<Pickup, Transform>().each([&](Entity, Pickup& pk, Transform& t) {
            const Vec2 to = t.position - p;
            const float d = to.length();
            const bool want = pk.kind == PK_Health ? hp_ < 75.f && d < 700.f : d < 380.f;
            if (want && d > 1.f) m += to / d * 1.4f;
        });

        // Drift back toward the middle (keeps the bot out of corners).
        const Vec2 centre = e.camera().bounds.center();
        m += (centre - p).normalized() * (foes.empty() ? 1.2f : 0.3f);

        const float mag = m.length();
        const Vec2 dir = mag > 0.05f ? m / mag : Vec2{};
        in.set_key(Key::D, dir.x > 0.35f);
        in.set_key(Key::A, dir.x < -0.35f);
        in.set_key(Key::S, dir.y > 0.35f);
        in.set_key(Key::W, dir.y < -0.35f);

        // Target: nearest visible enemy, with gunners preferred (they hurt from range).
        const Foe* target = nullptr;
        float best = 1e9f;
        int checked = 0;
        for (const Foe& f : foes) {
            if (++checked > 6 || f.d > 620.f) break;
            const Vec2 to = (f.pos - p) / std::max(f.d, 1.f);
            auto wall = e.physics().raycast(w, p, to, f.d, layer::WALL, player_);
            if (wall && wall->distance < f.d - f.def->radius) continue;
            const float score = f.d - (f.def->behavior == Behavior::Kite ? 160.f : 0.f);
            if (score < best) {
                best = score;
                target = &f;
            }
        }
        if (target) {
            const Vec2 lead = target->pos + target->vel * (target->d / T.bullet_speed);
            in.set_mouse_pos(e.camera().world_to_screen(lead));
        } else {
            in.set_mouse_pos(e.camera().world_to_screen(p + (mag > 0.05f ? dir : aim_) * 200.f));
        }
        in.set_mouse_button(MouseButton::Left, target != nullptr);

        // Dash out when something is about to touch us.
        const bool danger = !foes.empty() && foes[0].d < foes[0].def->radius + T.player_radius + 22.f;
        in.set_key(Key::Space, danger && dash_cd_ <= 0.f);
    }

    // ───────── state ─────────
    SessionPtr S;
    const Tuning& T;
    Rng rng_;
    FlowField flow_;
    Entity player_;
    Vec2 aim_{1, 0}, dash_dir_{1, 0};

    float hp_ = 100.f, invuln_ = 0.f, fire_cd_ = 0.f, dash_cd_ = 0.f, dash_time_ = 0.f, dash_buffer_ = 0.f;
    float rapid_t_ = 0.f, spread_t_ = 0.f, hitstop_ = 0.f;
    int score_ = 0, kills_ = 0, chain_ = 0, combo_ = 1;
    float combo_timer_ = 0.f;
    bool dead_ = false, leaving_ = false;
    float death_t_ = 0.f;

    int wave_ = 0;
    std::vector<int> queue_;
    size_t qpos_ = 0;
    float spawn_t_ = 0.f, intermission_t_ = 0.f, banner_t_ = 0.f;
    bool in_intermission_ = false;

    std::string toast_;
    float toast_t_ = 0.f;
};

// ───────────────────────────── game over ─────────────────────────────

class GameOverScene : public Scene {
public:
    explicit GameOverScene(SessionPtr s) : S(std::move(s)) {}

    void on_enter(Engine& e) override {
        bind_keys(e);
        S->art.load(e);
        build_arena(e, *S, false);
    }
    void on_update(Engine& e, float dt) override {
        t_ += dt;
        e.camera().position = e.camera().bounds.center();
        Input& in = e.input();
        const bool retry = t_ > 0.4f && (in.action_pressed("confirm") || in.pressed(Key::R) || in.mouse_pressed(MouseButton::Left));
        if (!leaving_ && (retry || (S->autopilot && t_ > 1.0f))) {
            leaving_ = true;
            e.set_scene(make_play(S));
        } else if (!leaving_ && in.pressed(Key::T)) {
            leaving_ = true;
            e.set_scene(make_title(S));
        }
    }
    void on_render(Engine&, Renderer& r) override {
        const float ui = ui_scale(r), h = static_cast<float>(r.height());
        r.fill_rect({{0, 0}, {static_cast<float>(r.width()), h}}, Color{0.08f, 0.01f, 0.03f, 0.72f});
        text_center(r, "GAME OVER", h * 0.20f, hex(0xFF4060), 7.f * ui);
        text_center(r, "SCORE " + std::to_string(S->last_score), h * 0.42f, hex(0xFFE14D), 3.f * ui);
        text_center(r, "WAVE " + std::to_string(S->last_wave) + "   KILLS " + std::to_string(S->last_kills), h * 0.53f, Color{1, 1, 1, 0.9f}, 2.f * ui);
        text_center(r, "BEST " + std::to_string(S->peak_score) + "  (WAVE " + std::to_string(S->peak_wave) + ")", h * 0.62f, hex(0xA9B8FF), 2.f * ui);
        if (std::fmod(t_, 1.2f) < 0.8f) text_center(r, "ENTER: RETRY     T: TITLE", h * 0.78f, hex(0xFFE14D), 2.f * ui);
    }

private:
    SessionPtr S;
    float t_ = 0.f;
    bool leaving_ = false;
};

std::unique_ptr<Scene> make_title(SessionPtr s) { return std::make_unique<TitleScene>(std::move(s)); }
std::unique_ptr<Scene> make_play(SessionPtr s) { return std::make_unique<PlayScene>(std::move(s)); }
std::unique_ptr<Scene> make_game_over(SessionPtr s) { return std::make_unique<GameOverScene>(std::move(s)); }

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> map_rows = default_map_rows();
    std::string map_path;
    int difficulty = 100, expect_wave = -1, expect_score = -1;
    bool skip_title = false;
    SessionPtr session;  // recreated by make_scene, so every headless run starts from scratch

    harness::GameSpec spec;
    spec.binary = "prism_arena";
    spec.title = "Prism Arena";
    spec.tag = "arena";
    spec.default_frames = 3600;
    spec.add_options = [&](harness::Cli& cli) {
        cli.custom("--map", 1, "FILE", "load a custom arena ('#' wall, '.' floor, 'P' start, 'S' spawn)",
                   [&](const std::vector<std::string>& a) -> std::string {
                       std::vector<std::string> rows;
                       std::string err = load_map_file(a[0], rows);
                       ArenaMap probe;
                       if (err.empty()) err = parse_map(rows, probe);
                       if (!err.empty()) return err;
                       map_rows = std::move(rows);
                       map_path = a[0];
                       return std::string();
                   });
        cli.integer("--difficulty", &difficulty, "PCT", "enemy count/health scaling in percent (default 100)", 25, 400);
        cli.flag("--no-title", &skip_title, "start straight in the arena");
        cli.integer("--expect-wave", &expect_wave, "N", "exit with status 2 unless wave N was reached", 0, 100000);
        cli.integer("--expect-score", &expect_score, "N", "exit with status 2 unless the best score is at least N", 0, 2000000000);
    };
    spec.configure = [](EngineConfig& cfg, const harness::CommonOptions&) {
        cfg.physics.gravity = {0.f, 0.f};  // every scene of this game starts with no gravity
        cfg.camera_follow_lambda = 9.f;
        cfg.clear_color = Color::from_hex(0x0B0E17);
    };
    spec.make_scene = [&](const harness::CommonOptions& o) -> std::unique_ptr<Scene> {
        session = std::make_shared<Session>();
        ArenaMap map;
        std::string err = parse_map(map_rows, map);
        if (!err.empty()) throw std::runtime_error("invalid map: " + err);  // only reachable via the built-in map
        session->map = std::move(map);
        session->seed = o.seed;
        session->difficulty = static_cast<float>(difficulty) / 100.f;
        session->autopilot = o.autopilot;
        return skip_title ? make_play(session) : make_title(session);
    };
    spec.summary = [&](Engine&) {
        return "wave " + std::to_string(session->peak_wave) + ", best score " + std::to_string(session->peak_score) + ", runs " +
               std::to_string(session->runs) + ", kills " + std::to_string(session->total_kills);
    };
    spec.check = [&](Engine&) {
        if (expect_wave >= 0 && session->peak_wave < expect_wave) {
            std::fprintf(stderr, "[arena] expected to reach wave %d, reached %d\n", expect_wave, session->peak_wave);
            return 1;
        }
        if (expect_score >= 0 && session->peak_score < expect_score) {
            std::fprintf(stderr, "[arena] expected a best score of at least %d, got %d\n", expect_score, session->peak_score);
            return 1;
        }
        return 0;
    };
    return harness::run_game(argc, argv, spec);
}
