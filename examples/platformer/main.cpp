// Prism platformer demo.
//
// Exercises most of the engine: tilemap + merged colliders, sprite sheets & animation,
// triggers, one-way platforms, a kinematic moving platform, rigid bodies (boxes/circles),
// particles, camera follow/bounds/shake, input actions, and the debug overlays.
//
//   Windowed (needs SDL2 build):   prism_demo
//   Headless (always available):   prism_demo --headless --autopilot --frames 1800 --shot 900 out.png
//
// Controls: A/D or arrows = move | Space/W/Up = jump | S/Down + jump = drop through plank
//           left click = spawn crate | right click = spawn ball | R = reset | F1/F2 = debug | Esc = quit
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "prism/prism.hpp"

using namespace prism;

namespace {

// ───────────────────────────── procedural art ─────────────────────────────

uint32_t shade(uint32_t c, int d) {  // add d to each RGB channel (clamped)
    auto ch = [&](int shift) { return static_cast<uint8_t>(std::clamp(int((c >> shift) & 0xFF) + d, 0, 255)); };
    return rgba(ch(24), ch(16), ch(8), c & 0xFF);
}

Image make_stone_tile() {
    Image im(16, 16, rgba(0x6B, 0x72, 0x80));
    Rng rng(11);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) im.set_pixel(x, y, shade(rgba(0x6B, 0x72, 0x80), rng.range_int(-7, 7)));
    for (int x = 0; x < 16; ++x) {
        im.set_pixel(x, 0, rgba(0x8A, 0x92, 0xA3));
        im.set_pixel(x, 7, rgba(0x4B, 0x51, 0x5E));
        im.set_pixel(x, 15, rgba(0x4B, 0x51, 0x5E));
    }
    for (int y = 1; y < 7; ++y) im.set_pixel(0, y, rgba(0x4B, 0x51, 0x5E));
    for (int y = 8; y < 15; ++y) im.set_pixel(8, y, rgba(0x4B, 0x51, 0x5E));
    return im;
}

Image make_dirt_tile() {
    Image im(16, 16);
    Rng rng(23);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) im.set_pixel(x, y, shade(rgba(0x6A, 0x45, 0x2C), rng.range_int(-9, 9)));
    for (int i = 0; i < 9; ++i) {
        int x = rng.range_int(0, 14), y = rng.range_int(0, 14);
        im.fill_rect(x, y, 2, 2, rgba(0x4F, 0x31, 0x1E));
    }
    return im;
}

Image make_grass_tile() {
    Image im = make_dirt_tile();
    Rng rng(5);
    for (int x = 0; x < 16; ++x) {
        int depth = 3 + rng.range_int(0, 2);
        for (int y = 0; y < depth; ++y)
            im.set_pixel(x, y, shade(rgba(0x4C, 0xB0, 0x4A), y == 0 ? 22 : -y * 14 + rng.range_int(-6, 6)));
        im.set_pixel(x, depth, rgba(0x2F, 0x6B, 0x2E));
    }
    return im;
}

Image make_plank_tile() {  // only the top 5 rows are opaque: a thin one-way platform
    Image im(16, 16, 0);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 16; ++x) {
            uint32_t c = rgba(0xB0, 0x76, 0x3F);
            if (y == 0) c = rgba(0xD2, 0x98, 0x5C);
            if (y == 4) c = rgba(0x7A, 0x4B, 0x28);
            if (x == 0 || x == 8) c = rgba(0x7A, 0x4B, 0x28);
            im.set_pixel(x, y, c);
        }
    im.set_pixel(3, 2, rgba(0x55, 0x33, 0x1B));
    im.set_pixel(12, 2, rgba(0x55, 0x33, 0x1B));
    return im;
}

Image make_crate_tex() {
    Image im(16, 16, rgba(0xC9, 0x8F, 0x4E));
    for (int i = 0; i < 16; ++i) {
        im.set_pixel(i, i, rgba(0x8A, 0x5A, 0x2B));
        im.set_pixel(15 - i, i, rgba(0x8A, 0x5A, 0x2B));
    }
    for (int i = 0; i < 16; ++i)
        for (int t = 0; t < 2; ++t) {
            uint32_t edge = rgba(0x6B, 0x42, 0x1F);
            im.set_pixel(i, t, edge);
            im.set_pixel(i, 15 - t, edge);
            im.set_pixel(t, i, edge);
            im.set_pixel(15 - t, i, edge);
        }
    return im;
}

Image make_ball_tex() {
    const int n = 24;
    Image im(n, n, 0);
    const float c = (n - 1) * 0.5f, r = n * 0.5f - 0.5f;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = x - c, dy = y - c, d = std::sqrt(dx * dx + dy * dy);
            if (d > r) continue;
            float light = 1.f - clampf((dx * 0.5f + dy * 0.7f) / r * 0.5f + 0.5f, 0.f, 1.f);  // light from top-left
            int v = static_cast<int>(lerpf(-45.f, 55.f, light));
            im.set_pixel(x, y, d > r - 1.5f ? rgba(0x8E, 0x1F, 0x2B) : shade(rgba(0xE0, 0x45, 0x4F), v));
        }
    return im;
}

Image make_coin_sheet() {  // 6 frames, 12x12: a coin turning around its vertical axis
    Image im(72, 12, 0);
    const float rx_of[6] = {5.5f, 4.6f, 3.0f, 1.0f, 3.0f, 4.6f};
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x) {
                float dx = (x - 5.5f) / std::max(rx_of[f], 0.6f), dy = (y - 5.5f) / 5.5f, d = dx * dx + dy * dy;
                if (d > 1.f) continue;
                uint32_t c = d > 0.66f ? rgba(0xC4, 0x8A, 0x10) : rgba(0xFF, 0xD2, 0x3F);
                if (dx < -0.3f && dy < -0.2f && d < 0.5f) c = rgba(0xFF, 0xF0, 0x9A);
                im.set_pixel(f * 12 + x, y, c);
            }
    return im;
}

// Player sprite sheet: 8 frames of 14x22 -> idle(0,1) run(2..5) jump(6) fall(7)
Image make_player_sheet() {
    const uint32_t skin = rgba(0xF1, 0xC2, 0x7D), shirt = rgba(0x2E, 0xA8, 0xFF), shirt_d = rgba(0x1D, 0x7F, 0xCC),
                   pants = rgba(0x2C, 0x3E, 0x70), shoes = rgba(0x1B, 0x1B, 0x2F), hair = rgba(0x3B, 0x2A, 0x20);
    Image im(14 * 8, 22, 0);
    for (int f = 0; f < 8; ++f) {
        const int ox = f * 14;
        int bob = (f == 1) ? 1 : 0;
        int la = 3, lb = 8, ya = 16, yb = 16, arm_l = 0, arm_r = 0;
        if (f >= 2 && f <= 5) {  // run cycle
            int p = f - 2;
            la = (p == 0 ? 1 : p == 1 ? 4 : p == 2 ? 6 : 4);
            lb = (p == 0 ? 8 : p == 1 ? 6 : p == 2 ? 3 : 6);
            ya = (p == 1 ? 15 : 16);
            yb = (p == 3 ? 15 : 16);
            arm_l = (p == 0 || p == 3) ? 2 : -2;
            arm_r = -arm_l;
        } else if (f == 6) {  // jump: tucked legs, arms up
            la = 2; lb = 8; ya = 15; yb = 14; arm_l = -4; arm_r = -4;
        } else if (f == 7) {  // fall: legs apart, arms out
            la = 1; lb = 10; ya = 16; yb = 16; arm_l = -1; arm_r = -1;
        }
        auto rect = [&](int x, int y, int w, int h, uint32_t c) { im.fill_rect(ox + x, y, w, h, c); };
        rect(la, ya + bob, 3, 5, pants);
        rect(la, ya + 5 + bob, 4, 1, shoes);
        rect(lb, yb + bob, 3, 5, pants);
        rect(lb, yb + 5 + bob, 4, 1, shoes);
        rect(3, 8 + bob, 8, 8, shirt);
        rect(3, 15 + bob, 8, 1, shirt_d);
        rect(1, 9 + bob + arm_l, 2, 5, shirt_d);
        rect(11, 9 + bob + arm_r, 2, 5, shirt_d);
        rect(3, 1 + bob, 8, 7, skin);
        rect(3, 1 + bob, 8, 2, hair);
        rect(2, 1 + bob, 1, 5, hair);
        rect(8, 4 + bob, 2, 2, rgba(0x20, 0x20, 0x30));  // eye (facing right)
    }
    return im;
}

// ───────────────────────────── level ─────────────────────────────

constexpr float TILE = 32.f;
constexpr int LEVEL_W = 80, LEVEL_H = 24;
constexpr float LEVEL_PX_W = LEVEL_W * TILE, LEVEL_PX_H = LEVEL_H * TILE;

std::vector<std::string> make_level() {
    std::vector<std::string> g(LEVEL_H, std::string(LEVEL_W, '.'));
    auto hline = [&](int r, int c0, int c1, char ch) {
        for (int c = c0; c <= c1; ++c) g[r][c] = ch;
    };
    hline(20, 0, 79, 'G');
    for (int r = 21; r < 24; ++r) hline(r, 0, 79, 'D');
    for (int r = 20; r < 24; ++r) hline(r, 34, 41, '.');  // the pit (a moving platform ferries you across)
    for (int r = 0; r < 20; ++r) g[r][0] = g[r][79] = '#';
    hline(17, 8, 13, '=');
    hline(14, 14, 19, '=');
    hline(11, 20, 26, '=');
    hline(14, 28, 32, '=');
    hline(19, 46, 60, '#');  // staircase
    hline(18, 48, 60, '#');
    hline(17, 50, 60, '#');
    hline(16, 52, 60, '#');
    for (int r = 15; r < 20; ++r) hline(r, 66, 67, '#');  // tower
    hline(14, 62, 65, '=');
    hline(11, 68, 72, '=');
    hline(8, 74, 77, '=');

    g[19][2] = 'P';
    const int coins[][2] = {{19, 4},  {19, 6},  {16, 10}, {16, 12}, {13, 16}, {13, 18}, {10, 22}, {10, 24},
                            {13, 29}, {13, 31}, {17, 36}, {17, 39}, {18, 47}, {17, 49}, {16, 51}, {15, 54},
                            {15, 56}, {15, 58}, {13, 63}, {13, 64}, {10, 69}, {10, 71}, {7, 75},  {7, 77}};
    for (auto& c : coins) g[c[0]][c[1]] = 'C';
    g[19][20] = g[19][21] = g[18][20] = 'B';  // a little crate pyramid
    g[19][44] = 'B';
    g[8][29] = g[6][31] = 'O';  // bouncy balls dropped onto the plank
    return g;
}

struct Coin {};
struct MovingPlatform {
    Vec2 center, amplitude;
    float period, phase;
};

// ───────────────────────────── the game ─────────────────────────────

class PlatformerScene : public Scene {
public:
    explicit PlatformerScene(bool autopilot) : autopilot_(autopilot) {}

    int coins_collected() const { return coins_got_; }
    int coins_total() const { return coins_total_; }

    void on_enter(Engine& e) override {
        tex_player_ = e.add_texture(make_player_sheet());
        tex_coin_ = e.add_texture(make_coin_sheet());
        tex_crate_ = e.add_texture(make_crate_tex());
        tex_ball_ = e.add_texture(make_ball_tex());
        tile_stone_ = e.add_texture(make_stone_tile());
        tile_dirt_ = e.add_texture(make_dirt_tile());
        tile_grass_ = e.add_texture(make_grass_tile());
        tile_plank_ = e.add_texture(make_plank_tile());

        e.input().bind("left", {Key::A, Key::Left});
        e.input().bind("right", {Key::D, Key::Right});
        e.input().bind("jump", {Key::Space, Key::W, Key::Up});
        e.input().bind("down", {Key::S, Key::Down});

        trigger_token_ = e.events().subscribe<TriggerEvent>([this, &e](const TriggerEvent& ev) {
            World& w = e.world();
            if (!ev.entered || !w.has<Coin>(ev.trigger) || !w.has<PlayerTag>(ev.other)) return;
            Vec2 p = w.get<Transform>(ev.trigger)->position;
            ParticleBurst b;
            b.position = p;
            b.count = 14;
            b.speed_min = 60;
            b.speed_max = 220;
            b.size_start = 5;
            b.color_start = Color::from_hex(0xFFE066);
            b.color_end = Color::from_hex(0xFF9F1C, 0.f);
            b.gravity = 500;
            e.particles().emit(b);
            w.destroy(ev.trigger);
            ++coins_got_;
            e.camera().add_trauma(0.10f);
        });
        build_level(e);
    }

    void on_exit(Engine& e) override { e.events().unsubscribe<TriggerEvent>(trigger_token_); }

    void on_fixed_update(Engine& e, float dt) override {
        const float t = static_cast<float>(e.time());
        e.world().view<MovingPlatform, Transform, Velocity>().each(
            [&](Entity, MovingPlatform& mp, Transform& tf, Velocity& v) {
                Vec2 target = mp.center + mp.amplitude * std::sin(TAU * t / mp.period + mp.phase);
                v.linear = (target - tf.position) / dt;  // follow the path exactly, riders see the true velocity
            });
    }

    void on_update(Engine& e, float dt) override {
        Input& in = e.input();
        if (autopilot_) drive_autopilot(e);
        if (in.pressed(Key::R)) {
            build_level(e);
            return;
        }

        if (in.mouse_pressed(MouseButton::Left)) spawn_crate(e, e.screen_to_world(in.mouse_pos()));
        if (in.mouse_pressed(MouseButton::Right)) spawn_ball(e, e.screen_to_world(in.mouse_pos()));

        World& w = e.world();
        auto* rb = w.get<RigidBody>(player_);
        auto* vel = w.get<Velocity>(player_);
        auto* tf = w.get<Transform>(player_);
        auto* spr = w.get<Sprite>(player_);
        auto* anim = w.get<Animator>(player_);
        if (!rb || !vel || !tf || !spr || !anim) return;

        // Fell into the pit.
        if (tf->position.y > LEVEL_PX_H + 200.f) {
            tf->teleport(spawn_);
            vel->linear = {0, 0};
            e.camera().add_trauma(0.4f);
            return;
        }

        // ---- horizontal movement (relative to whatever we stand on) ----
        const float dir = in.axis("left", "right");
        const float max_speed = 270.f;
        const float accel = rb->on_ground ? 2600.f : 1500.f;
        const float base = rb->on_ground ? rb->ground_velocity.x : 0.f;
        float rel = vel->linear.x - base;
        rel = approach(rel, dir * max_speed, (dir != 0.f ? accel : (rb->on_ground ? 3200.f : 400.f)) * dt);
        vel->linear.x = rel + base;
        if (dir != 0.f) facing_ = dir;

        // ---- jumping: coyote time, input buffering, variable height, drop-through ----
        coyote_ = rb->on_ground ? 0.10f : coyote_ - dt;
        jump_buffer_ = in.action_pressed("jump") ? 0.12f : jump_buffer_ - dt;
        if (jump_buffer_ > 0.f) {
            bool on_plank = rb->on_ground && w.get<Collider>(rb->ground_entity) &&
                            w.get<Collider>(rb->ground_entity)->one_way;
            if (on_plank && in.action_down("down")) {
                rb->ignore_one_way_timer = 0.25f;
                jump_buffer_ = 0.f;
            } else if (coyote_ > 0.f) {
                vel->linear.y = -590.f;
                jump_buffer_ = coyote_ = 0.f;
                dust(e, tf->position + Vec2{0, 22}, 6, 90.f);
            }
        }
        if (in.action_released("jump") && vel->linear.y < -160.f) vel->linear.y *= 0.45f;  // short hop
        rb->gravity_scale = vel->linear.y > 0.f ? 1.5f : 1.f;                              // snappier fall

        // ---- landing feedback ----
        if (rb->on_ground && !was_grounded_ && air_vy_ > 380.f) {
            dust(e, tf->position + Vec2{0, 22}, 8, 130.f);
            e.camera().add_trauma(clampf((air_vy_ - 380.f) / 1200.f, 0.f, 0.35f));
        }
        if (!rb->on_ground) air_vy_ = vel->linear.y;
        was_grounded_ = rb->on_ground;

        // ---- animation ----
        spr->flip_x = facing_ < 0.f;
        if (!rb->on_ground)
            anim->play(vel->linear.y < 0.f ? "jump" : "fall");
        else if (std::fabs(rel) > 25.f)
            anim->play("run");
        else
            anim->play("idle");
    }

    void on_render(Engine& e, Renderer& r) override {
        std::string hud = "COINS " + std::to_string(coins_got_) + "/" + std::to_string(coins_total_);
        r.draw_text(hud, {18, 18}, Color{0, 0, 0, 0.6f}, 3.f);
        r.draw_text(hud, {16, 16}, Color::from_hex(0xFFE066), 3.f);
        if (coins_got_ == coins_total_ && coins_total_ > 0) {
            std::string msg = "ALL COINS COLLECTED - PRESS R TO PLAY AGAIN";
            Vec2 sz = r.measure_text(msg, 2.f);
            Vec2 p{(r.width() - sz.x) * 0.5f, 60.f};
            r.draw_text(msg, p + Vec2{2, 2}, Color{0, 0, 0, 0.6f}, 2.f);
            r.draw_text(msg, p, Color::from_hex(0xFFFFFF), 2.f);
        }
        if (!e.debug_overlay) {
            std::string help = "A/D MOVE   SPACE JUMP   S+SPACE DROP   CLICK: CRATE / BALL   R RESET   F1 STATS   F2 COLLIDERS";
            r.draw_text(help, {16.f, static_cast<float>(r.height()) - 20.f}, Color{1, 1, 1, 0.55f}, 1.f);
        }
    }

private:
    void build_level(Engine& e) {
        World& w = e.world();
        w.clear();
        e.particles().clear();
        coins_got_ = coins_total_ = 0;
        coyote_ = jump_buffer_ = air_vy_ = 0.f;
        was_grounded_ = false;

        // Background: sky bands + distant hills.
        const uint32_t sky[] = {0x1B2A49, 0x24386B, 0x2F4C8C, 0x3F67A8, 0x5F88BE, 0x86A9CC, 0xC9B1A0, 0xF0B07F};
        for (int i = 0; i < 8; ++i) {
            Entity b = w.create();
            w.add(b, Transform({LEVEL_PX_W * 0.5f, i * 96.f + 48.f}));
            w.add(b, Sprite{Color::from_hex(sky[i]), {LEVEL_PX_W + 800.f, 98.f}, -100});
        }
        Rng art(77);
        for (int i = 0; i < 46; ++i) {
            float hgt = art.range(90.f, 300.f);
            Entity h = w.create();
            w.add(h, Transform({i * 60.f + art.range(-10.f, 10.f), 640.f - hgt * 0.5f}));
            w.add(h, Sprite{Color::from_hex(i % 2 ? 0x31446E : 0x2A3B63), {art.range(70.f, 130.f), hgt}, -90});
        }

        // Dark chasm behind the pit so it doesn't show the sky.
        {
            Entity pit = w.create();
            w.add(pit, Transform({1216.f, 640.f + 164.f}));
            w.add(pit, Sprite{Color::from_hex(0x0A0A12), {256.f, 328.f}, -50});
        }

        // Tilemap + merged static colliders.
        TileDef stone, dirt, grass, plank;
        stone.texture = tile_stone_; stone.solid = true;
        dirt.texture = tile_dirt_;   dirt.solid = true;
        grass.texture = tile_grass_; grass.solid = true;
        plank.texture = tile_plank_; plank.solid = true; plank.one_way = true;
        auto rows = make_level();
        Tilemap tm = Tilemap::from_ascii(rows, TILE, {0, 0}, {{'#', stone}, {'D', dirt}, {'G', grass}, {'=', plank}});
        tm.z_index = -10;
        tm.build_colliders(w, 1, 0.6f);
        e.camera().use_bounds = true;
        e.camera().bounds = tm.bounds();
        Entity map = w.create();
        w.set_name(map, "Level");
        w.add(map, std::move(tm));

        // Entities from the ASCII legend.
        for (int r = 0; r < LEVEL_H; ++r)
            for (int c = 0; c < LEVEL_W; ++c) {
                Vec2 p{(c + 0.5f) * TILE, (r + 0.5f) * TILE};
                switch (rows[r][c]) {
                    case 'P': spawn_ = p; break;
                    case 'C': spawn_coin(e, p); break;
                    case 'B': spawn_crate(e, p + Vec2{0, 2}); break;
                    case 'O': spawn_ball(e, p); break;
                    default: break;
                }
            }

        // Kinematic platform that ferries the player across the pit.
        {
            Entity m = w.create();
            Vec2 center{1216.f, 652.f};
            w.add(m, Transform(center));
            w.add(m, Velocity{});
            w.add(m, Collider::box({64, 12}));
            w.add(m, RigidBody::Kinematic());
            w.add(m, Sprite{Color::from_hex(0xE8943A), {128, 24}, 8});
            w.add(m, MovingPlatform{center, {128, 0}, 6.f, 0.f});
        }

        // Player.
        player_ = w.create();
        w.set_name(player_, "Player");
        w.add(player_, Transform(spawn_));
        w.add(player_, Velocity{});
        w.add(player_, Collider::box({11, 22}));
        RigidBody rb = RigidBody::Dynamic(1.f);
        rb.friction = 0.f;  // we control horizontal velocity directly; no sticking to walls
        w.add(player_, rb);
        Sprite spr{Color{1, 1, 1, 1}, {28, 44}, 100};
        spr.texture = tex_player_;
        spr.src = {0, 0, 14, 22};
        w.add(player_, spr);
        Animator an;
        an.columns = 8;
        an.frame_w = 14;
        an.frame_h = 22;
        an.add_clip({"idle", 0, 1, 2.f, true});
        an.add_clip({"run", 2, 5, 12.f, true});
        an.add_clip({"jump", 6, 6, 1.f, false});
        an.add_clip({"fall", 7, 7, 1.f, false});
        w.add(player_, an);
        w.add(player_, PlayerTag{});
        w.add(player_, CameraTarget{{0, -30}});

        e.camera().snap_to(spawn_);
    }

    void spawn_coin(Engine& e, Vec2 p) {
        World& w = e.world();
        Entity c = w.create();
        w.add(c, Transform(p));
        Collider col = Collider::circle(11);
        col.is_trigger = true;
        w.add(c, col);
        Sprite spr{Color{1, 1, 1, 1}, {24, 24}, 10};
        spr.texture = tex_coin_;
        spr.src = {0, 0, 12, 12};
        w.add(c, spr);
        Animator an;
        an.columns = 6;
        an.frame_w = an.frame_h = 12;
        an.add_clip({"spin", 0, 5, 10.f, true});
        an.time = std::fmod(p.x * 0.013f, 0.6f);  // de-synchronise neighbouring coins
        w.add(c, an);
        w.add(c, Coin{});
        ++coins_total_;
    }

    void spawn_crate(Engine& e, Vec2 p) {
        World& w = e.world();
        Entity c = w.create();
        w.add(c, Transform(p));
        w.add(c, Velocity{});
        w.add(c, Collider::box({14, 14}));
        RigidBody rb = RigidBody::Dynamic(1.2f);
        rb.friction = 0.5f;
        w.add(c, rb);
        float v = 0.82f + 0.18f * e.rng().next01();
        Sprite spr{Color{v, v, v, 1}, {28, 28}, 5};
        spr.texture = tex_crate_;
        spr.src = {0, 0, 16, 16};
        w.add(c, spr);
    }

    void spawn_ball(Engine& e, Vec2 p) {
        World& w = e.world();
        Entity b = w.create();
        w.add(b, Transform(p));
        w.add(b, Velocity{});
        w.add(b, Collider::circle(12));
        RigidBody rb = RigidBody::Dynamic(0.8f);
        rb.friction = 0.1f;
        rb.restitution = 0.72f;
        w.add(b, rb);
        Sprite spr{Color{1, 1, 1, 1}, {24, 24}, 6};
        spr.texture = tex_ball_;
        spr.src = {0, 0, 24, 24};
        w.add(b, spr);
    }

    void dust(Engine& e, Vec2 pos, int count, float speed) {
        ParticleBurst b;
        b.position = pos;
        b.count = count;
        b.speed_min = speed * 0.3f;
        b.speed_max = speed;
        b.angle_min = -PI * 0.95f;  // a fan pointing up (+Y is down)
        b.angle_max = -PI * 0.05f;
        b.life_min = 0.2f;
        b.life_max = 0.4f;
        b.size_start = 7.f;
        b.size_end = 1.f;
        b.color_start = Color{0.86f, 0.84f, 0.78f, 0.75f};
        b.color_end = Color{0.86f, 0.84f, 0.78f, 0.f};
        b.gravity = 120.f;
        b.circle = true;
        e.particles().emit(b);
    }

    // A tiny bot that plays by injecting key presses: heads for the left-most coin, jumps
    // when the coin is above, at walls, and at ledges/pits (found with a physics raycast).
    void drive_autopilot(Engine& e) {
        World& w = e.world();
        auto* tf = w.get<Transform>(player_);
        auto* rb = w.get<RigidBody>(player_);
        if (!tf || !rb) return;

        Vec2 target{};
        bool have = false;
        w.view<Coin, Transform>().each([&](Entity, Coin&, Transform& t) {
            if (!have || t.position.x < target.x) {
                target = t.position;
                have = true;
            }
        });

        float dir = 0.f;
        bool jump = false;
        if (have) {
            float dx = target.x - tf->position.x, dy = target.y - tf->position.y;
            dir = dx > 8.f ? 1.f : (dx < -8.f ? -1.f : 0.f);
            if (rb->on_ground) {
                if (dy < -40.f && std::fabs(dx) < 260.f) jump = true;
                if (dir != 0.f && !e.physics().raycast(w, tf->position + Vec2{dir * 44.f, 0.f}, {0, 1}, 150.f, 1u, player_))
                    jump = true;  // pit / ledge ahead
                if ((dir > 0.f && rb->on_wall_right) || (dir < 0.f && rb->on_wall_left)) jump = true;
            }
        }
        if (jump && ap_hold_ == 0) ap_hold_ = 32;  // hold the key through the ascent (full-height jump)
        bool jump_key = ap_hold_ > 0;
        if (ap_hold_ > 0) --ap_hold_;

        Input& in = e.input();
        in.set_key(Key::D, dir > 0.f);
        in.set_key(Key::A, dir < 0.f);
        in.set_key(Key::Space, jump_key);
    }

    bool autopilot_;
    int ap_hold_ = 0;
    int coins_got_ = 0, coins_total_ = 0;
    Entity player_;
    Vec2 spawn_{100, 600};
    float coyote_ = 0.f, jump_buffer_ = 0.f, air_vy_ = 0.f, facing_ = 1.f;
    bool was_grounded_ = false;
    EventBus::Token trigger_token_ = 0;
    TextureId tex_player_ = 0, tex_coin_ = 0, tex_crate_ = 0, tex_ball_ = 0;
    TextureId tile_stone_ = 0, tile_dirt_ = 0, tile_grass_ = 0, tile_plank_ = 0;
};

struct Options {
    bool headless = false, autopilot = false, debug_colliders = false, debug_overlay = false, print_hash = false;
    int frames = 0;
    int width = 1280, height = 720;
    int expect_coins = -1;
    uint64_t seed = 1;
    std::vector<std::pair<int, std::string>> shots;
};

void usage() {
    std::puts(
        "prism_demo [options]\n"
        "  --headless            software renderer, no window; runs --frames frames then exits\n"
        "  --autopilot           a bot plays the level\n"
        "  --frames N            frames to simulate in headless mode (default 900)\n"
        "  --shot F PATH         save a PNG screenshot at frame F (repeatable)\n"
        "  --size WxH            logical resolution (default 1280x720)\n"
        "  --colliders / --stats start with the F2 / F1 debug overlays on\n"
        "  --expect-coins N      exit with status 2 if fewer than N coins were collected\n"
        "  --hash                print a hash of the final frame (determinism checks)\n"
        "  --seed N              RNG seed");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](int n) { return i + n < argc; };
        if (a == "--headless") o.headless = true;
        else if (a == "--autopilot") o.autopilot = true;
        else if (a == "--colliders") o.debug_colliders = true;
        else if (a == "--stats") o.debug_overlay = true;
        else if (a == "--hash") o.print_hash = true;
        else if (a == "--frames" && need(1)) o.frames = std::atoi(argv[++i]);
        else if (a == "--seed" && need(1)) o.seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--expect-coins" && need(1)) o.expect_coins = std::atoi(argv[++i]);
        else if (a == "--size" && need(1)) {
            if (std::sscanf(argv[++i], "%dx%d", &o.width, &o.height) != 2 || o.width < 64 || o.height < 64) return false;
        } else if (a == "--shot" && need(2)) {
            int f = std::atoi(argv[++i]);
            o.shots.emplace_back(f, argv[++i]);
        } else return false;
    }
    return true;
}

}  // namespace

extern "C" int SDL_main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 64;
    }
    try {
        if (!o.headless && !sdl_backend_available()) {
            std::puts("[demo] this build has no SDL2 window backend; running the headless autopilot demo instead");
            o.headless = true;
            o.autopilot = true;
        }
        EngineConfig cfg;
        cfg.title = "Prism - platformer demo";
        cfg.width = o.width;
        cfg.height = o.height;
        cfg.backend = o.headless ? Backend::Software : Backend::Auto;
        cfg.vsync = !o.headless;
        cfg.seed = o.seed;
        Engine engine(cfg);

        auto scene = std::make_unique<PlatformerScene>(o.autopilot);
        PlatformerScene* game = scene.get();  // owned by the engine; valid until it is destroyed
        engine.set_scene(std::move(scene));
        engine.debug_colliders = o.debug_colliders;
        engine.debug_overlay = o.debug_overlay;

        if (!o.headless) {
            engine.run();
            return 0;
        }

        const int frames = o.frames > 0 ? o.frames : 900;
        for (int f = 0; f < frames; ++f) {
            for (const auto& s : o.shots)
                if (s.first == f) engine.request_screenshot(s.second);
            engine.step(0);  // exactly one fixed step per frame: fully deterministic
        }
        Image last;
        engine.capture_frame(last);
        std::printf("[demo] %d frames, coins %d/%d, frame hash %016llx\n", frames, game->coins_collected(),
                    game->coins_total(), static_cast<unsigned long long>(last.hash()));
        if (o.expect_coins >= 0 && game->coins_collected() < o.expect_coins) {
            std::fprintf(stderr, "[demo] FAILED: expected at least %d coins\n", o.expect_coins);
            return 2;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Fatal: %s\n", ex.what());
        return 1;
    }
    return 0;
}
