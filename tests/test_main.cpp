// Prism test suite. Self-contained (no framework). Run: make test
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "prism/prism.hpp"

#ifndef PRISM_TEST_DATA_DIR
#define PRISM_TEST_DATA_DIR "tests/data"
#endif

using namespace prism;

// ───────────────────────────── tiny harness ─────────────────────────────

struct TestCase {
    const char* name;
    void (*fn)();
};
static std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
static int g_failures = 0, g_checks = 0;

#define TEST(name)                          \
    static void name();                     \
    static Registrar reg_##name(#name, name); \
    static void name()

#define CHECK(...)                                                               \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(__VA_ARGS__)) {                                                    \
            ++g_failures;                                                        \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                           \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        double _a = (a), _b = (b);                                                                      \
        if (!(std::fabs(_a - _b) <= (eps))) {                                                           \
            ++g_failures;                                                                               \
            std::printf("    FAIL %s:%d  %s = %.4f, expected %.4f +/- %.4f\n", __FILE__, __LINE__, #a, _a, _b, \
                        double(eps));                                                                   \
        }                                                                                               \
    } while (0)

// ───────────────────────────── math / rng ─────────────────────────────

TEST(math_vec2_and_helpers) {
    Vec2 a{3, 4};
    CHECK_NEAR(a.length(), 5.0, 1e-6);
    CHECK_NEAR(a.normalized().length(), 1.0, 1e-6);
    CHECK(Vec2{0, 0}.normalized() == Vec2(0, 0));
    CHECK_NEAR(Vec2(1, 0).cross(Vec2(0, 1)), 1.0, 1e-6);
    CHECK((a + Vec2{1, 1}) == Vec2(4, 5));
    CHECK_NEAR(approach(0.f, 10.f, 3.f), 3.0, 1e-6);
    CHECK_NEAR(approach(9.f, 10.f, 3.f), 10.0, 1e-6);  // never overshoots
    CHECK_NEAR(approach(5.f, 0.f, 9.f), 0.0, 1e-6);
    // damp is frame-rate independent: one step of dt equals two steps of dt/2
    float one = damp(0.f, 100.f, 5.f, 0.2f);
    float two = damp(damp(0.f, 100.f, 5.f, 0.1f), 100.f, 5.f, 0.1f);
    CHECK_NEAR(one, two, 1e-3);
    Rect r{{0, 0}, {10, 10}};
    CHECK(r.overlaps(Rect{{5, 5}, {15, 15}}));
    CHECK(!r.overlaps(Rect{{10, 0}, {20, 10}}));  // touching edges do not overlap
    CHECK(r.contains({10, 10}));
}

TEST(math_rng_is_deterministic) {
    Rng a(42), b(42), c(43);
    bool same = true, differs = false;
    for (int i = 0; i < 1000; ++i) {
        uint32_t x = a.next_u32(), y = b.next_u32(), z = c.next_u32();
        same &= (x == y);
        differs |= (x != z);
    }
    CHECK(same);
    CHECK(differs);
    Rng r(7);
    bool in_range = true;
    double sum = 0;
    for (int i = 0; i < 20000; ++i) {
        float v = r.next01();
        in_range &= (v >= 0.f && v < 1.f);
        sum += v;
        int k = r.range_int(3, 6);
        in_range &= (k >= 3 && k <= 6);
    }
    CHECK(in_range);
    CHECK_NEAR(sum / 20000.0, 0.5, 0.02);
}

// ───────────────────────────── ECS ─────────────────────────────

struct CompA { int v = 0; };
struct CompB { float f = 0; };

TEST(ecs_stale_handles_are_detected) {
    World w;
    Entity e1 = w.create();
    w.add(e1, CompA{11});
    CHECK(w.alive(e1));
    w.destroy(e1);
    CHECK(!w.alive(e1));
    CHECK(w.get<CompA>(e1) == nullptr);

    Entity e2 = w.create();  // reuses the slot
    CHECK(e2.index == e1.index);
    CHECK(e2.generation != e1.generation);
    CHECK(w.alive(e2));
    CHECK(!w.alive(e1));                    // the old handle must NOT alias the new entity
    CHECK(w.get<CompA>(e2) == nullptr);     // and the new entity inherited no components
    CHECK(w.get<CompA>(e1) == nullptr);
    CHECK(w.size() == 1);
}

TEST(ecs_matches_reference_model_under_random_ops) {
    World w;
    Rng rng(99);
    std::vector<Entity> live;
    std::map<uint64_t, int> model_a;  // entity pack -> value
    for (int step = 0; step < 6000; ++step) {
        int op = rng.range_int(0, 9);
        if (op < 3 || live.empty()) {
            live.push_back(w.create());
        } else if (op < 6) {
            Entity e = live[rng.range_int(0, int(live.size()) - 1)];
            int v = rng.range_int(0, 1000);
            w.add(e, CompA{v});
            model_a[e.pack()] = v;
        } else if (op < 8) {
            Entity e = live[rng.range_int(0, int(live.size()) - 1)];
            w.remove<CompA>(e);
            model_a.erase(e.pack());
        } else {
            int i = rng.range_int(0, int(live.size()) - 1);
            Entity e = live[i];
            w.destroy(e);
            model_a.erase(e.pack());
            live.erase(live.begin() + i);
        }
    }
    CHECK(w.size() == live.size());
    size_t visited = 0;
    bool all_match = true;
    w.view<CompA>().each([&](Entity e, CompA& a) {
        ++visited;
        auto it = model_a.find(e.pack());
        all_match &= (it != model_a.end() && it->second == a.v);
    });
    CHECK(all_match);
    CHECK(visited == model_a.size());
    for (Entity e : live) CHECK(w.has<CompA>(e) == (model_a.count(e.pack()) == 1));
}

TEST(ecs_destroy_during_iteration_is_deferred) {
    World w;
    std::vector<Entity> es;
    for (int i = 0; i < 100; ++i) {
        Entity e = w.create();
        w.add(e, CompA{i});
        es.push_back(e);
    }
    int visited = 0;
    bool alive_in_loop = true;
    w.view<CompA>().each([&](Entity e, CompA& a) {
        ++visited;
        alive_in_loop &= w.alive(e);
        if (a.v % 2 == 0) w.destroy(e);
    });
    CHECK(visited == 100);  // nothing skipped or visited twice
    CHECK(alive_in_loop);
    CHECK(w.size() == 50);
    for (int i = 0; i < 100; ++i) CHECK(w.alive(es[i]) == (i % 2 == 1));
}

TEST(ecs_view_visits_only_the_intersection) {
    World w;
    int both = 0;
    for (int i = 0; i < 30; ++i) {
        Entity e = w.create();
        if (i % 2 == 0) w.add(e, CompA{i});
        if (i % 3 == 0) w.add(e, CompB{float(i)});
        if (i % 6 == 0) ++both;
    }
    int n = 0;
    bool consistent = true;
    w.view<CompA, CompB>().each([&](Entity, CompA& a, CompB& b) {
        ++n;
        consistent &= (a.v == int(b.f));
    });
    CHECK(n == both);
    CHECK(consistent);
    int missing = 0;
    w.view<CompA, Name>().each([&](Entity, CompA&, Name&) { ++missing; });
    CHECK(missing == 0);  // a type with no pool at all
}

TEST(ecs_add_during_iteration_does_not_corrupt) {
    World w;
    for (int i = 0; i < 50; ++i) w.add(w.create(), CompA{i});
    int visited = 0;
    w.view<CompA>().each([&](Entity, CompA&) {
        ++visited;
        Entity n = w.create();  // forces pool growth mid-iteration
        w.add(n, CompA{-1});
    });
    CHECK(visited == 50);  // new entities are not visited in this pass
    CHECK(w.size() == 100);
}

TEST(ecs_names) {
    World w;
    Entity e = w.create();
    w.set_name(e, "hero");
    CHECK(w.name(e) == "hero");
    CHECK(w.find_by_name("hero") == e);
    CHECK(!w.find_by_name("nobody").valid());
    w.destroy(e);
    CHECK(!w.find_by_name("hero").valid());
}

// ───────────────────────────── events & input ─────────────────────────────

struct Ping { int n; };

TEST(events_subscribe_emit_unsubscribe) {
    EventBus bus;
    int total = 0, other = 0;
    auto t = bus.subscribe<Ping>([&](const Ping& p) { total += p.n; });
    bus.subscribe<Ping>([&](const Ping& p) { other += p.n * 2; });
    bus.emit(Ping{3});
    CHECK(total == 3 && other == 6);
    bus.unsubscribe<Ping>(t);
    bus.emit(Ping{5});
    CHECK(total == 3 && other == 16);
    // subscribing from inside a handler must be safe and not run for the current emit
    int late = 0;
    bus.subscribe<Ping>([&](const Ping&) { bus.subscribe<Ping>([&](const Ping&) { ++late; }); });
    bus.emit(Ping{1});
    CHECK(late == 0);
}

TEST(input_edges_and_actions) {
    Input in;
    in.bind("jump", {Key::Space, Key::W});
    in.bind("left", {Key::A, Key::Left});
    in.bind("right", {Key::D});
    in.set_key(Key::W, true);
    CHECK(in.pressed(Key::W) && in.down(Key::W));
    CHECK(in.action_pressed("jump") && in.action_down("jump"));
    in.set_key(Key::W, true);  // repeat: no new edge after begin_frame
    in.begin_frame();
    CHECK(!in.pressed(Key::W) && in.down(Key::W));
    in.set_key(Key::W, false);
    CHECK(in.released(Key::W) && !in.down(Key::W));
    in.begin_frame();
    in.set_key(Key::Left, true);
    CHECK_NEAR(in.axis("left", "right"), -1.0, 1e-6);
    in.set_key(Key::D, true);
    CHECK_NEAR(in.axis("left", "right"), 0.0, 1e-6);
    CHECK(!in.action_down("does-not-exist"));
}

// ───────────────────────────── images / PNG ─────────────────────────────

static int fixture_sample(int x, int y, int c) { return (x * 13 + y * 29 + c * 71 + ((x * y) >> 3)) & 0xFF; }

static std::string fixture(const char* name) { return std::string(PRISM_TEST_DATA_DIR) + "/" + name; }

TEST(png_encode_decode_roundtrip) {
    Image img(37, 23);
    Rng rng(5);
    for (auto& b : img.pixels) b = uint8_t(rng.next_u32());
    auto bytes = encode_png(img);
    Image out;
    std::string err;
    CHECK(decode_png(bytes.data(), bytes.size(), out, &err));
    CHECK(out.width == 37 && out.height == 23);
    CHECK(out.pixels == img.pixels);
    // big enough to need several stored deflate blocks (> 65535 bytes)
    Image big(300, 250, 0x11223344u);
    big.set_pixel(299, 249, 0xAABBCCDDu);
    auto bb = encode_png(big);
    Image back;
    CHECK(decode_png(bb.data(), bb.size(), back, &err));
    CHECK(back.hash() == big.hash());
}

TEST(png_decodes_independent_encoder_output) {
    std::string err;
    Image im;
    // RGBA, dynamic Huffman, all five filter types
    CHECK(load_png(fixture("rgba.png"), im, &err));
    CHECK(im.width == 200 && im.height == 150);
    bool ok = im.width == 200;
    for (int y = 0; y < im.height && ok; ++y)
        for (int x = 0; x < im.width && ok; ++x) {
            const uint8_t* p = im.px(x, y);
            ok = p[0] == fixture_sample(x, y, 0) && p[1] == fixture_sample(x, y, 1) &&
                 p[2] == fixture_sample(x, y, 2) && p[3] == fixture_sample(x, y, 3);
        }
    CHECK(ok);

    CHECK(load_png(fixture("rgb.png"), im, &err));
    ok = im.width == 97 && im.height == 61;
    for (int y = 0; y < im.height && ok; ++y)
        for (int x = 0; x < im.width && ok; ++x) {
            const uint8_t* p = im.px(x, y);
            ok = p[0] == fixture_sample(x, y, 0) && p[1] == fixture_sample(x, y, 1) &&
                 p[2] == fixture_sample(x, y, 2) && p[3] == 255;
        }
    CHECK(ok);

    CHECK(load_png(fixture("gray.png"), im, &err));
    ok = im.width == 80 && im.height == 40;
    for (int y = 0; y < im.height && ok; ++y)
        for (int x = 0; x < im.width && ok; ++x) {
            const uint8_t* p = im.px(x, y);
            int g = fixture_sample(x, y, 0);
            ok = p[0] == g && p[1] == g && p[2] == g && p[3] == 255;
        }
    CHECK(ok);

    CHECK(load_png(fixture("graya.png"), im, &err));
    ok = im.width == 50 && im.height == 33;
    for (int y = 0; y < im.height && ok; ++y)
        for (int x = 0; x < im.width && ok; ++x) {
            const uint8_t* p = im.px(x, y);
            int g = fixture_sample(x, y, 0);
            ok = p[0] == g && p[2] == g && p[3] == fixture_sample(x, y, 1);
        }
    CHECK(ok);

    CHECK(load_png(fixture("palette.png"), im, &err));
    const uint8_t pal[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}};
    const uint8_t alpha[4] = {255, 128, 0, 255};
    ok = im.width == 64 && im.height == 16;
    for (int y = 0; y < im.height && ok; ++y)
        for (int x = 0; x < im.width && ok; ++x) {
            const uint8_t* p = im.px(x, y);
            int i = (x + y) % 4;
            ok = p[0] == pal[i][0] && p[1] == pal[i][1] && p[2] == pal[i][2] && p[3] == alpha[i];
        }
    CHECK(ok);

    CHECK(load_png(fixture("stored.png"), im, &err));  // zlib level 0 => stored blocks
    ok = im.width == 40 && im.height == 30;
    for (int y = 0; y < im.height && ok; ++y)
        for (int x = 0; x < im.width && ok; ++x) ok = im.px(x, y)[1] == fixture_sample(x, y, 1);
    CHECK(ok);
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::vector<uint8_t> v;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
        std::fclose(f);
    }
    return v;
}

static std::vector<uint8_t> extract_idat(const std::vector<uint8_t>& png) {
    std::vector<uint8_t> out;
    size_t p = 8;
    while (p + 12 <= png.size()) {
        uint32_t len = (png[p] << 24) | (png[p + 1] << 16) | (png[p + 2] << 8) | png[p + 3];
        if (std::memcmp(&png[p + 4], "IDAT", 4) == 0) out.insert(out.end(), &png[p + 8], &png[p + 8] + len);
        p += 12 + len;
    }
    return out;
}

TEST(png_rejects_garbage_without_crashing) {
    auto good = read_file(fixture("rgba.png"));
    CHECK(good.size() > 100);
    Image im;
    std::string err;
    CHECK(!decode_png(good.data(), 5, im, &err));                      // too short
    CHECK(!decode_png(good.data(), good.size() / 2, im, &err));        // truncated
    auto bad_sig = good;
    bad_sig[1] = 'X';
    CHECK(!decode_png(bad_sig.data(), bad_sig.size(), im, &err));
    auto bad_crc = good;
    bad_crc[40] ^= 0xFF;  // inside IDAT payload -> CRC mismatch
    CHECK(!decode_png(bad_crc.data(), bad_crc.size(), im, &err));
    CHECK(err.find("CRC") != std::string::npos);
    CHECK(!load_png("/nonexistent/file.png", im, &err));

    // Fuzz the raw deflate stream (bypasses the CRC) to stress every inflate error path.
    auto idat = extract_idat(good);
    CHECK(idat.size() > 100);
    const size_t cap = size_t(200 * 4 + 1) * 150;
    std::vector<uint8_t> out;
    CHECK(inflate_zlib(idat.data(), idat.size(), out, cap, &err));
    CHECK(out.size() == cap);
    Rng rng(1234);
    int rejected = 0;
    for (int i = 0; i < 4000; ++i) {
        auto m = idat;
        int flips = rng.range_int(1, 3);
        for (int k = 0; k < flips; ++k) m[rng.range_int(0, int(m.size()) - 1)] ^= uint8_t(1u << rng.range_int(0, 7));
        std::vector<uint8_t> o;
        if (!inflate_zlib(m.data(), m.size(), o, cap)) ++rejected;
        CHECK(o.size() <= cap);
    }
    CHECK(rejected > 100);  // corruption is generally noticed, and never overruns the cap
    for (size_t len = 0; len + 12 < idat.size(); len += 7) {
        std::vector<uint8_t> o;
        CHECK(!inflate_zlib(idat.data(), len, o, cap));  // any truncation must be rejected
    }
    CHECK(!inflate_zlib(idat.data(), idat.size(), out, 1000, &err));  // output cap enforced
}

// ───────────────────────────── font & software renderer ─────────────────────────────

static int count_opaque(const Image& img, IRect r) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            if (img.get_pixel(x, y) & 0xFF) ++n;
    return n;
}

TEST(font_atlas_has_every_glyph) {
    Image a = make_font_atlas();
    CHECK(a.width == 96 && a.height == 48);
    CHECK(count_opaque(a, font_glyph_rect(' ')) == 0);
    bool all = true;
    for (int c = 33; c < 127; ++c) all &= count_opaque(a, font_glyph_rect(char(c))) > 0;
    CHECK(all);
    // lowercase shares uppercase glyphs
    bool same = true;
    for (char c = 'a'; c <= 'z'; ++c) {
        IRect lo = font_glyph_rect(c), up = font_glyph_rect(char(c - 32));
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 6; ++x) same &= a.get_pixel(lo.x + x, lo.y + y) == a.get_pixel(up.x + x, up.y + y);
    }
    CHECK(same);
    // distinct letters look different
    CHECK(count_opaque(a, font_glyph_rect('I')) != count_opaque(a, font_glyph_rect('W')));
}

TEST(software_renderer_primitives) {
    SoftwareRenderer r(64, 48);
    r.begin_frame({0, 0, 0, 1});
    r.fill_rect({{10, 10}, {20, 20}}, {1, 0, 0, 1});
    CHECK(r.framebuffer().get_pixel(15, 15) == 0xFF0000FFu);
    CHECK(r.framebuffer().get_pixel(9, 15) == 0x000000FFu);
    CHECK(r.framebuffer().get_pixel(20, 15) == 0x000000FFu);  // max edge is exclusive
    // alpha blend: 50% white over red
    r.fill_rect({{10, 10}, {20, 20}}, {1, 1, 1, 0.5f});
    const uint8_t* p = r.framebuffer().px(15, 15);
    CHECK(std::abs(int(p[0]) - 255) <= 1 && std::abs(int(p[1]) - 128) <= 1 && std::abs(int(p[2]) - 128) <= 1);
    // wildly out-of-range geometry must be clipped / ignored, not crash
    r.fill_rect({{-1e9f, -1e9f}, {1e9f, 1e9f}}, {0, 1, 0, 1});
    r.fill_rect({{-50, -50}, {200, 5}}, {0, 0, 1, 1});
    r.draw_line({-500, -500}, {500, 500}, {1, 1, 0, 1});
    r.draw_line({NAN, 0}, {10, 10}, {1, 1, 0, 1});
    r.fill_circle({10, 10}, 1e9f, {1, 0, 1, 1});
    CHECK(r.framebuffer().get_pixel(63, 2) == 0x0000FFFFu);

    // circle: center filled, bounding-box corner not
    r.begin_frame({0, 0, 0, 1});
    r.fill_circle({32, 24}, 10, {1, 1, 1, 1});
    CHECK(r.framebuffer().get_pixel(32, 24) == 0xFFFFFFFFu);
    CHECK(r.framebuffer().get_pixel(23, 15) == 0x000000FFu);
    CHECK(r.framebuffer().get_pixel(41, 24) == 0xFFFFFFFFu);

    // line endpoints are drawn
    r.begin_frame({0, 0, 0, 1});
    r.draw_line({5, 5}, {30, 20}, {1, 1, 1, 1});
    CHECK(r.framebuffer().get_pixel(5, 5) == 0xFFFFFFFFu);
    CHECK(r.framebuffer().get_pixel(30, 20) == 0xFFFFFFFFu);
}

TEST(software_renderer_textures) {
    SoftwareRenderer r(32, 32);
    Image t(2, 1);
    t.set_pixel(0, 0, 0xFF0000FFu);  // red | blue
    t.set_pixel(1, 0, 0x0000FFFFu);
    TextureId id = r.create_texture(t);
    r.begin_frame({0, 0, 0, 1});
    r.draw_texture(id, {}, {{0, 0}, {8, 4}}, {1, 1, 1, 1}, false, false);
    CHECK(r.framebuffer().get_pixel(1, 1) == 0xFF0000FFu);
    CHECK(r.framebuffer().get_pixel(6, 1) == 0x0000FFFFu);
    r.draw_texture(id, {}, {{10, 0}, {18, 4}}, {1, 1, 1, 1}, true, false);  // flipped
    CHECK(r.framebuffer().get_pixel(11, 1) == 0x0000FFFFu);
    CHECK(r.framebuffer().get_pixel(16, 1) == 0xFF0000FFu);
    r.draw_texture(id, {1, 0, 1, 1}, {{20, 0}, {24, 4}}, {1, 1, 1, 1}, false, false);  // sub-rect
    CHECK(r.framebuffer().get_pixel(21, 1) == 0x0000FFFFu);
    r.draw_texture(id, {}, {{0, 10}, {4, 14}}, {0, 1, 0, 1}, false, false);  // tint multiplies
    CHECK(r.framebuffer().get_pixel(1, 11) == 0x000000FFu);
    r.draw_texture(999, {}, {{0, 0}, {4, 4}}, {1, 1, 1, 1}, false, false);  // invalid id ignored
    r.destroy_texture(id);
    r.draw_texture(id, {}, {{0, 20}, {4, 24}}, {1, 1, 1, 1}, false, false);  // destroyed id ignored

    r.init_font();
    r.begin_frame({0, 0, 0, 1});
    r.draw_text("HI", {2, 2}, {1, 1, 1, 1}, 2.f);
    int lit = 0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 30; ++x)
            if (r.framebuffer().get_pixel(x, y) != 0x000000FFu) ++lit;
    CHECK(lit > 30);
    Vec2 m = r.measure_text("ABC\nDE", 2.f);
    CHECK_NEAR(m.x, 3 * 6 * 2, 1e-4);
    CHECK_NEAR(m.y, 2 * 8 * 2, 1e-4);
}

// ───────────────────────────── camera ─────────────────────────────

TEST(camera_math) {
    Camera2D c;
    c.viewport = {800, 600};
    c.position = {100, 50};
    c.zoom = 2.f;
    Vec2 w{130, 70};
    Vec2 s = c.world_to_screen(w);
    CHECK_NEAR(s.x, 460, 1e-3);  // (130-100)*2 + 400
    CHECK_NEAR(s.y, 340, 1e-3);
    Vec2 back = c.screen_to_world(s);
    CHECK_NEAR(back.x, 130, 1e-3);
    CHECK_NEAR(back.y, 70, 1e-3);
    Rect v = c.visible_rect();
    CHECK_NEAR(v.min.x, 100 - 200, 1e-3);
    CHECK_NEAR(v.max.y, 50 + 150, 1e-3);

    // follow converges, and is frame-rate independent
    Camera2D a, b;
    a.position = b.position = {0, 0};
    for (int i = 0; i < 60; ++i) a.follow({100, 0}, 5.f, 1.f / 60);
    for (int i = 0; i < 120; ++i) b.follow({100, 0}, 5.f, 1.f / 120);
    CHECK_NEAR(a.position.x, b.position.x, 0.5);
    CHECK(a.position.x > 95.f);

    // bounds
    Camera2D k;
    k.viewport = {200, 100};
    k.use_bounds = true;
    k.bounds = {{0, 0}, {1000, 500}};
    k.snap_to({-300, 9999});
    CHECK_NEAR(k.position.x, 100, 1e-3);
    CHECK_NEAR(k.position.y, 450, 1e-3);
    k.bounds = {{0, 0}, {100, 50}};  // smaller than the view: centered
    k.snap_to({0, 0});
    CHECK_NEAR(k.position.x, 50, 1e-3);

    // shake decays to exactly zero
    Camera2D sh;
    sh.add_trauma(1.f);
    sh.update(0.016f);
    CHECK(sh.shake_offset.length() > 0.f);
    for (int i = 0; i < 200; ++i) sh.update(0.016f);
    CHECK(sh.trauma == 0.f && sh.shake_offset.length() == 0.f);
}

// ───────────────────────────── physics ─────────────────────────────

struct Sim {
    World w;
    Physics p;
    EventBus bus;
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) {
            w.view<Transform>().each([](Entity, Transform& t) { t.prev_position = t.position; });
            p.step(w, &bus, 1.f / 60.f);
        }
    }
    Entity add_body(Vec2 pos, Collider c, RigidBody rb, Vec2 vel = {0, 0}) {
        Entity e = w.create();
        w.add(e, Transform(pos));
        w.add(e, c);
        w.add(e, rb);
        if (rb.type != BodyType::Static) w.add(e, Velocity{vel, 0.f});
        return e;
    }
    // Static floor whose top surface is at y = top.
    Entity floor(float top = 380.f, float half_w = 3000.f) {
        return add_body({0, top + 20}, Collider::box({half_w, 20}), RigidBody::Static());
    }
    Entity box(Vec2 pos, Vec2 half, float friction = 0.4f, float restitution = 0.f, float mass = 1.f) {
        RigidBody rb = RigidBody::Dynamic(mass);
        rb.friction = friction;
        rb.restitution = restitution;
        return add_body(pos, Collider::box(half), rb);
    }
    Entity ball(Vec2 pos, float r, float friction = 0.4f, float restitution = 0.f) {
        RigidBody rb = RigidBody::Dynamic();
        rb.friction = friction;
        rb.restitution = restitution;
        return add_body(pos, Collider::circle(r), rb);
    }
    Transform& tf(Entity e) { return *w.get<Transform>(e); }
    Velocity& vel(Entity e) { return *w.get<Velocity>(e); }
    RigidBody& rb(Entity e) { return *w.get<RigidBody>(e); }
};

TEST(physics_box_comes_to_rest_on_floor) {
    Sim s;
    s.floor(380);
    Entity b = s.box({0, 100}, {16, 16});
    s.step(180);
    CHECK_NEAR(s.tf(b).position.y, 364.0, 0.6);  // floor top (380) minus half height (16)
    CHECK(std::fabs(s.vel(b).linear.y) < 8.f);
    CHECK(s.rb(b).on_ground);
    CHECK(!s.rb(b).on_ceiling);
}

TEST(physics_stack_is_stable) {
    Sim s;
    s.floor(380);
    Entity a = s.box({0, 364}, {16, 16}, 0.6f);
    Entity b = s.box({0, 328}, {16, 16}, 0.6f);
    Entity c = s.box({0, 292}, {16, 16}, 0.6f);
    s.step(300);
    CHECK_NEAR(s.tf(a).position.y, 364, 1.5);
    CHECK_NEAR(s.tf(b).position.y, 332, 1.5);  // 364 - 32
    CHECK_NEAR(s.tf(c).position.y, 300, 1.5);
    CHECK(std::fabs(s.tf(a).position.x) < 1 && std::fabs(s.tf(b).position.x) < 1 && std::fabs(s.tf(c).position.x) < 1);
    CHECK(s.vel(c).linear.length() < 10.f);
    CHECK(s.rb(c).on_ground);  // standing on the box below
    CHECK(s.rb(c).ground_entity == b);
}

TEST(physics_restitution_matches_coefficient) {
    Sim s;
    s.floor(380);
    Entity b = s.ball({0, 100}, 10, 0.f, 0.8f);
    float prev_vy = 0, ratio = -1;
    for (int i = 0; i < 240; ++i) {
        s.step();
        float vy = s.vel(b).linear.y;
        if (vy < 0 && prev_vy > 100) {
            ratio = -vy / prev_vy;
            break;
        }
        prev_vy = vy;
    }
    CHECK(ratio > 0);
    CHECK_NEAR(ratio, 0.8, 0.07);

    Sim s2;  // restitution 0: lands and stays down
    s2.floor(380);
    Entity c = s2.ball({0, 100}, 10, 0.f, 0.f);
    float min_vy = 0;
    for (int i = 0; i < 240; ++i) {
        s2.step();
        min_vy = std::min(min_vy, s2.vel(c).linear.y);
    }
    CHECK(min_vy > -20.f);
}

TEST(physics_friction_matches_coulomb) {
    Sim s;
    Entity floor = s.floor(380);
    s.rb(floor).friction = 0.5f;
    Entity b = s.box({0, 364}, {16, 16}, 0.5f);  // mu = sqrt(0.5 * 0.5) = 0.5
    s.step(30);
    s.vel(b).linear.x = 300.f;
    float x0 = s.tf(b).position.x;
    int n = 0;
    while (std::fabs(s.vel(b).linear.x) > 1.f && n++ < 240) s.step();
    float dist = s.tf(b).position.x - x0;
    const float expected = 300.f * 300.f / (2.f * 0.5f * 980.f);  // v^2 / (2 mu g) = 91.8
    CHECK_NEAR(dist, expected, 8.0);
    CHECK_NEAR(s.tf(b).position.y, 364, 0.6);
}

TEST(physics_triggers_fire_once_and_do_not_block) {
    Sim s;
    s.p.config.gravity = {0, 0};
    Entity zone = s.add_body({0, 0}, Collider::sensor_box({50, 50}), RigidBody::Static());
    Entity mover = s.box({-300, 0}, {10, 10});
    s.vel(mover).linear = {200, 0};
    int enters = 0, exits = 0;
    bool right_entities = true;
    s.bus.subscribe<TriggerEvent>([&](const TriggerEvent& ev) {
        (ev.entered ? enters : exits)++;
        right_entities &= (ev.trigger == zone && ev.other == mover);
    });
    s.step(300);  // 5 s: crosses the zone completely
    CHECK(enters == 1);
    CHECK(exits == 1);
    CHECK(right_entities);
    CHECK_NEAR(s.tf(mover).position.x, -300 + 200 * 5, 1.0);  // passed straight through

    // an entity destroyed while inside produces no exit event for a dead handle
    Entity inside = s.box({0, 0}, {5, 5});
    s.vel(inside).linear = {0, 0};
    s.step(5);
    CHECK(enters == 2);
    s.w.destroy(inside);
    s.step(5);
    CHECK(exits == 1);
}

TEST(physics_layers_and_masks) {
    Sim s;
    Collider plat = Collider::box({100, 10});
    plat.layer = 1;
    plat.mask = 1;
    s.add_body({0, 300}, plat, RigidBody::Static());
    RigidBody rb = RigidBody::Dynamic();
    Collider ghost = Collider::box({10, 10});
    ghost.layer = 2;
    ghost.mask = 2;
    Entity g = s.add_body({0, 100}, ghost, rb);
    Collider solid = Collider::box({10, 10});  // layer 1, mask all
    Entity c = s.add_body({40, 100}, solid, rb);
    s.step(90);
    CHECK(s.tf(g).position.y > 400);   // fell straight through
    CHECK_NEAR(s.tf(c).position.y, 280, 0.8);  // landed on the platform (top 290 - 10)
}

TEST(physics_one_way_platforms) {
    Sim s;
    s.floor(900);
    Collider plat = Collider::box({100, 10});
    plat.one_way = true;
    s.add_body({0, 300}, plat, RigidBody::Static());  // top surface at y = 290

    RigidBody rb = RigidBody::Dynamic();
    Entity above = s.add_body({-50, 150}, Collider::box({10, 10}), rb);
    Entity below = s.add_body({50, 500}, Collider::box({10, 10}), rb, {0, -900});  // jumps up through it
    float lowest_after_pass = 1e9f;
    bool passed = false;
    for (int i = 0; i < 300; ++i) {
        s.step();
        if (s.tf(below).position.y < 290) passed = true;
        if (passed) lowest_after_pass = std::min(lowest_after_pass, s.tf(below).position.y);
    }
    CHECK(passed);                                         // rose through the platform from below
    CHECK_NEAR(s.tf(above).position.y, 280, 0.8);          // landed on top
    CHECK_NEAR(s.tf(below).position.y, 280, 0.8);          // came back down and landed on top too
    CHECK(s.rb(above).on_ground && s.rb(below).on_ground);

    s.rb(above).ignore_one_way_timer = 0.4f;               // drop through
    s.step(120);
    CHECK(s.tf(above).position.y > 700);                   // now resting on the floor below (top 900)
    CHECK_NEAR(s.tf(above).position.y, 890, 1.0);
}

TEST(physics_kinematic_platform_carries_riders) {
    Sim s;
    RigidBody kin = RigidBody::Kinematic();
    Entity plat = s.add_body({0, 300}, Collider::box({100, 10}), kin, {100, 0});
    Entity rider = s.box({0, 280}, {10, 10}, 1.0f);
    s.step(60);
    CHECK_NEAR(s.tf(plat).position.x, 100, 1.0);
    CHECK_NEAR(s.tf(rider).position.x, s.tf(plat).position.x, 15.0);  // carried along by friction
    CHECK_NEAR(s.tf(rider).position.y, 280, 1.0);
    CHECK(s.rb(rider).on_ground);
    CHECK(s.rb(rider).ground_entity == plat);
    CHECK_NEAR(s.rb(rider).ground_velocity.x, 100, 0.5);
    CHECK_NEAR(s.vel(plat).linear.x, 100, 1e-4);  // the platform itself is never slowed down
}

TEST(physics_fast_bodies_do_not_tunnel) {
    for (float start_y : {0.f, -3000.f}) {
        Sim s;
        s.add_body({0, 1000}, Collider::box({300, 14}), RigidBody::Static());  // 28 px thick, top at 986
        Entity b = s.box({0, start_y}, {14, 22});
        s.step(420);
        CHECK_NEAR(s.tf(b).position.y, 986 - 22, 1.0);
    }
}

TEST(physics_circles_and_mixed_shapes) {
    Sim s;
    s.floor(600);
    s.add_body({0, 400}, Collider::circle(30), RigidBody::Static(0.f));
    Entity ball = s.ball({2, 300}, 10, 0.f);
    s.step(300);
    CHECK(s.tf(ball).position.x > 25.f);          // rolled off the bump instead of balancing
    CHECK_NEAR(s.tf(ball).position.y, 590, 1.0);  // and came to rest on the floor

    Sim t;  // ball dropped onto the corner of a box slides off
    t.floor(600);
    t.add_body({0, 500}, Collider::box({100, 10}), RigidBody::Static(0.f));
    Entity b2 = t.ball({108, 400}, 10, 0.f);
    t.step(300);
    CHECK(t.tf(b2).position.x > 115.f);
    CHECK_NEAR(t.tf(b2).position.y, 590, 1.0);

    Sim u;  // circle resting on a flat box
    u.add_body({0, 400}, Collider::box({100, 10}), RigidBody::Static());
    Entity b3 = u.ball({0, 300}, 12);
    u.step(200);
    CHECK_NEAR(u.tf(b3).position.y, 390 - 12, 0.6);
}

TEST(physics_raycast) {
    Sim s;
    Entity near_box = s.add_body({100, 0}, Collider::box({20, 20}), RigidBody::Static());
    Entity far_box = s.add_body({300, 0}, Collider::box({20, 20}), RigidBody::Static());
    Entity circ = s.add_body({0, 200}, Collider::circle(25), RigidBody::Static());

    auto h = s.p.raycast(s.w, {0, 0}, {1, 0}, 1000);
    CHECK(h && h->entity == near_box);
    CHECK_NEAR(h->distance, 80, 1e-3);
    CHECK_NEAR(h->normal.x, -1, 1e-4);
    CHECK_NEAR(h->point.x, 80, 1e-3);

    auto h2 = s.p.raycast(s.w, {0, 0}, {5, 0}, 1000, 0xFFFFFFFFu, near_box);  // un-normalized dir, ignore nearest
    CHECK(h2 && h2->entity == far_box);
    CHECK_NEAR(h2->distance, 280, 1e-3);

    auto h3 = s.p.raycast(s.w, {0, 0}, {0, 1}, 1000);
    CHECK(h3 && h3->entity == circ);
    CHECK_NEAR(h3->distance, 175, 1e-3);
    CHECK_NEAR(h3->normal.y, -1, 1e-4);

    CHECK(!s.p.raycast(s.w, {0, 0}, {1, 0}, 50));      // too short
    CHECK(!s.p.raycast(s.w, {0, 0}, {-1, 0}, 1000));   // wrong way
    CHECK(!s.p.raycast(s.w, {0, 0}, {1, 0}, 1000, 0x2u));  // layer mask excludes everything
    CHECK(!s.p.raycast(s.w, {0, 0}, {0, 0}, 1000));    // zero direction

    auto inside = s.p.raycast(s.w, {100, 0}, {1, 0}, 1000);
    CHECK(inside && inside->distance == 0.f);

    auto grazing = s.p.raycast(s.w, {0, 19}, {1, 0}, 1000);  // just inside the top/bottom extent
    CHECK(grazing && grazing->entity == near_box);
    CHECK(!s.p.raycast(s.w, {0, 21}, {1, 0}, 1000));         // just outside
}

TEST(physics_query_rect) {
    Sim s;
    Entity a = s.add_body({0, 0}, Collider::box({10, 10}), RigidBody::Static());
    Entity b = s.add_body({100, 0}, Collider::circle(10), RigidBody::Static());
    s.add_body({500, 500}, Collider::box({10, 10}), RigidBody::Static());
    auto r = s.p.query_rect(s.w, {{-20, -20}, {105, 20}});
    CHECK(r.size() == 2);
    CHECK((r[0] == a && r[1] == b) || (r[0] == b && r[1] == a));
    CHECK(s.p.query_rect(s.w, {{1000, 1000}, {1100, 1100}}).empty());
}

TEST(physics_collision_begin_events_fire_once) {
    Sim s;
    s.floor(380);
    Entity b = s.box({0, 200}, {16, 16});
    int begins = 0;
    s.bus.subscribe<CollisionEvent>([&](const CollisionEvent& ev) {
        if (ev.a == b || ev.b == b) ++begins;
    });
    s.step(240);
    CHECK(begins == 1);  // resting contact must not re-fire every step
}

static std::vector<float> run_physics_scenario() {
    Sim s;
    s.floor(380);
    s.add_body({-400, 0}, Collider::box({10, 2000}), RigidBody::Static());
    s.add_body({400, 0}, Collider::box({10, 2000}), RigidBody::Static());
    Rng rng(2024);
    std::vector<Entity> es;
    for (int i = 0; i < 40; ++i) {
        Vec2 p{rng.range(-300, 300), rng.range(-600, 300)};
        es.push_back(rng.next01() < 0.5f ? s.box(p, {rng.range(6, 18), rng.range(6, 18)}, 0.5f, 0.3f)
                                         : s.ball(p, rng.range(6, 16), 0.3f, 0.4f));
    }
    s.step(600);
    std::vector<float> out;
    for (Entity e : es) {
        out.push_back(s.tf(e).position.x);
        out.push_back(s.tf(e).position.y);
        out.push_back(s.vel(e).linear.x);
        out.push_back(s.vel(e).linear.y);
    }
    return out;
}

TEST(physics_is_deterministic) {
    auto a = run_physics_scenario();
    auto b = run_physics_scenario();
    CHECK(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    bool finite = true;
    for (float v : a) finite &= std::isfinite(v);
    CHECK(finite);
}

TEST(physics_handles_a_large_pile) {
    Sim s;
    s.floor(380);
    s.add_body({-300, 0}, Collider::box({10, 3000}), RigidBody::Static());
    s.add_body({300, 0}, Collider::box({10, 3000}), RigidBody::Static());
    std::vector<Entity> es;
    for (int row = 0; row < 20; ++row)
        for (int col = 0; col < 15; ++col)
            es.push_back(s.box({-150.f + col * 22.f, -400.f - row * 22.f}, {10, 10}, 0.4f));
    auto t0 = std::chrono::steady_clock::now();
    s.step(300);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("      300 bodies x 300 steps: %.0f ms (%.2f ms/step)\n", ms, ms / 300);
    bool ok = true;
    for (Entity e : es) {
        Vec2 p = s.tf(e).position;
        ok &= std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) < 295 && p.y < 372;  // inside, above floor
    }
    CHECK(ok);
    CHECK(s.p.body_count() == 303);  // 300 boxes + floor + two walls
}

// ───────────────────────────── tilemap / particles / animation ─────────────────────────────

TEST(tilemap_colliders_cover_exactly_the_solid_tiles) {
    TileDef solid;
    solid.solid = true;
    TileDef oneway;
    oneway.solid = true;
    oneway.one_way = true;
    std::vector<std::string> rows = {
        "..........",
        "..##......",
        ".####..=..",
        "##########",
        "##########",
    };
    Tilemap tm = Tilemap::from_ascii(rows, 16.f, {100, 200}, {{'#', solid}, {'=', oneway}});
    CHECK(tm.cols() == 10 && tm.rows() == 5);
    CHECK(tm.is_solid(2, 1) && !tm.is_solid(0, 0) && !tm.is_solid(-1, 3) && !tm.is_solid(3, 99));
    int tx, ty;
    tm.world_to_tile({100 + 16 * 3 + 1, 200 + 16 * 2 + 1}, tx, ty);
    CHECK(tx == 3 && ty == 2);

    int solid_tiles = 0;
    for (const auto& r : rows)
        for (char c : r) solid_tiles += (c == '#' || c == '=');

    World w;
    auto es = tm.build_colliders(w, 1, 0.6f);
    CHECK(!es.empty() && int(es.size()) < solid_tiles);  // merged
    double area = 0;
    bool overlap = false, any_one_way = false, all_static = true;
    std::vector<Rect> rects;
    for (Entity e : es) {
        const Transform* t = w.get<Transform>(e);
        const Collider* c = w.get<Collider>(e);
        any_one_way |= c->one_way;
        all_static &= w.get<RigidBody>(e)->type == BodyType::Static;
        Rect r = Rect::from_center(t->position, c->half_extents);
        area += double(r.size().x) * r.size().y;
        for (const Rect& o : rects) overlap |= r.overlaps(o);
        rects.push_back(r);
    }
    CHECK_NEAR(area, solid_tiles * 16.0 * 16.0, 1e-3);  // exact coverage...
    CHECK(!overlap);                                    // ...without overlap
    CHECK(any_one_way && all_static);
    // the two full-width rows merge into a single collider
    int wide = 0;
    for (const Rect& r : rects) wide += (r.size().x > 159.f && r.size().y > 31.f);
    CHECK(wide == 1);
}

TEST(particles_expire_and_respect_capacity) {
    ParticleSystem ps(50);
    ParticleBurst b;
    b.count = 100;
    b.life_min = b.life_max = 0.5f;
    ps.emit(b);
    CHECK(ps.alive() == 50);
    ps.update(0.3f);
    CHECK(ps.alive() == 50);
    ps.update(0.3f);
    CHECK(ps.alive() == 0);

    ParticleSystem a(64, 3), c(64, 3);  // same seed => same motion
    ParticleBurst d;
    d.count = 10;
    d.position = {5, 5};
    a.emit(d);
    c.emit(d);
    SoftwareRenderer ra(64, 64), rc(64, 64);
    Camera2D cam;
    cam.viewport = {64, 64};
    cam.position = {5, 5};
    for (int i = 0; i < 10; ++i) {
        a.update(0.02f);
        c.update(0.02f);
    }
    ra.begin_frame({0, 0, 0, 1});
    rc.begin_frame({0, 0, 0, 1});
    a.render(ra, cam);
    c.render(rc, cam);
    CHECK(ra.framebuffer().hash() == rc.framebuffer().hash());
    CHECK(ra.framebuffer().hash() != Image(64, 64, 0x000000FFu).hash());  // something was drawn
}

TEST(animator_sequences_frames) {
    Animator a;
    a.columns = 4;
    a.frame_w = 8;
    a.frame_h = 8;
    a.add_clip({"run", 4, 7, 10.f, true});
    a.add_clip({"die", 0, 2, 10.f, false});
    a.play("run");
    CHECK(a.current_name() == "run");
    a.advance(0.f);
    CHECK(a.frame == 4);
    a.advance(0.11f);
    CHECK(a.frame == 5);
    a.advance(0.3f);  // t = 0.41 -> index 4 -> wraps to 0
    CHECK(a.frame == 4);
    IRect r = a.source_rect();  // frame 4 = row 1, col 0
    CHECK(r.x == 0 && r.y == 8 && r.w == 8 && r.h == 8);
    a.play("run");  // already playing: no restart
    a.advance(0.f);
    CHECK(a.frame == 4);

    a.play("die");
    for (int i = 0; i < 20; ++i) a.advance(0.1f);
    CHECK(a.frame == 2 && a.finished);  // clamps on the last frame
    a.play("nope");                     // unknown clip is ignored
    CHECK(a.current_name() == "die");
}

// ───────────────────────────── engine ─────────────────────────────

static EngineConfig soft_cfg(int w = 320, int h = 180) {
    EngineConfig c;
    c.backend = Backend::Software;
    c.width = w;
    c.height = h;
    c.vsync = false;
    return c;
}

struct Counters {
    int enter = 0, exit = 0, fixed = 0, update = 0, render = 0;
};
struct CountScene : Scene {
    Counters& c;
    explicit CountScene(Counters& cc) : c(cc) {}
    void on_enter(Engine&) override { ++c.enter; }
    void on_exit(Engine&) override { ++c.exit; }
    void on_fixed_update(Engine&, float) override { ++c.fixed; }
    void on_update(Engine&, float) override { ++c.update; }
    void on_render(Engine&, Renderer&) override { ++c.render; }
};

TEST(engine_fixed_timestep_accounting) {
    Counters c;
    {
        Engine e(soft_cfg());
        e.set_scene(std::make_unique<CountScene>(c));
        for (int i = 0; i < 60; ++i) e.step(0);
        CHECK(c.fixed == 60 && c.update == 60 && c.render == 60);
        CHECK_NEAR(e.time(), 1.0, 1e-6);

        int before = c.fixed;
        for (int i = 0; i < 120; ++i) e.step(1.f / 120.f);  // render at 2x the physics rate
        CHECK(std::abs((c.fixed - before) - 60) <= 1);
        CHECK(c.update == 180);

        before = c.fixed;
        e.step(1.0f);  // a 1-second hitch is clamped to max_frame_dt (0.1 s)
        CHECK(c.fixed - before >= 5 && c.fixed - before <= 7);
    }
    CHECK(c.enter == 1 && c.exit == 1);

    EngineConfig cfg = soft_cfg();
    cfg.max_frame_dt = 100.f;
    Counters c2;
    Engine e2(cfg);
    e2.set_scene(std::make_unique<CountScene>(c2));
    e2.step(5.0f);
    CHECK(c2.fixed == cfg.max_steps_per_frame);  // spiral-of-death protection caps the catch-up
}

TEST(engine_scene_switching_order) {
    Counters a, b;
    {
        Engine e(soft_cfg());
        e.set_scene(std::make_unique<CountScene>(a));
        e.step(0);
        CHECK(a.enter == 1 && a.exit == 0);
        e.set_scene(std::make_unique<CountScene>(b));
        CHECK(a.exit == 0);  // takes effect on the next step, not mid-frame
        e.step(0);
        CHECK(a.exit == 1 && b.enter == 1 && b.update == 1 && a.update == 1);
    }
    CHECK(b.exit == 1);
}

TEST(engine_lifetime_destroys_entities) {
    Engine e(soft_cfg());
    Entity x = e.world().create();
    e.world().add(x, Lifetime{0.5f});
    for (int i = 0; i < 29; ++i) e.step(0);
    CHECK(e.world().alive(x));
    for (int i = 0; i < 3; ++i) e.step(0);
    CHECK(!e.world().alive(x));
}

TEST(engine_renders_sprites_tiles_and_text) {
    EngineConfig cfg = soft_cfg();
    cfg.clear_color = {0, 0, 0, 1};
    Engine e(cfg);
    Entity s = e.world().create();
    e.world().add(s, Transform({0, 0}));
    e.world().add(s, Sprite{{1, 0, 0, 1}, {20, 20}, 1});

    TileDef green;
    green.color = {0, 1, 0, 1};
    Entity t = e.world().create();
    e.world().add(t, Tilemap::from_ascii({"##", "##"}, 16.f, {-32, -32}, {{'#', green}}));  // covers (-32..0, -32..0)
    e.step(0);
    Image img;
    CHECK(e.capture_frame(img));
    auto px = [&](int x, int y) { return img.get_pixel(x, y); };
    CHECK(px(160, 90) == 0xFF0000FFu);   // sprite at the camera center (screen 160,90)
    CHECK(px(100, 30) == 0x000000FFu);   // empty space
    CHECK(px(140, 70) == 0x00FF00FFu);   // inside the tilemap (screen 128..160, 58..90), outside the sprite
    CHECK(px(129, 59) == 0x00FF00FFu && px(127, 59) == 0x000000FFu);  // tile edge lands exactly

    // z-order: raising the sprite above the tilemap's z puts it on top; lowering puts the tile on top
    e.world().get<Sprite>(s)->z_index = -20;
    e.step(0);
    e.capture_frame(img);
    CHECK(px(155, 85) == 0x00FF00FFu);   // overlap region now shows the tile
    e.world().get<Sprite>(s)->z_index = 5;
    e.step(0);
    e.capture_frame(img);
    CHECK(px(155, 85) == 0xFF0000FFu);

    // HUD text through Scene::on_render
    struct HudScene : Scene {
        void on_render(Engine&, Renderer& r) override { r.draw_text("SCORE 123", {200, 150}, {1, 1, 1, 1}, 1.f); }
    };
    e.set_scene(std::make_unique<HudScene>());
    e.step(0);
    e.capture_frame(img);
    int lit = 0;
    for (int y = 150; y < 158; ++y)
        for (int x = 200; x < 260; ++x) lit += (px(x, y) == 0xFFFFFFFFu);
    CHECK(lit > 40);
}

TEST(engine_animated_textured_sprite) {
    EngineConfig cfg = soft_cfg();
    cfg.clear_color = {0, 0, 0, 1};
    Engine e(cfg);
    Image sheet(16, 8);
    sheet.fill_rect(0, 0, 8, 8, 0x0000FFFFu);  // frame 0: blue
    sheet.fill_rect(8, 0, 8, 8, 0xFFFF00FFu);  // frame 1: yellow
    Entity s = e.world().create();
    e.world().add(s, Transform({0, 0}));
    Sprite spr{{1, 1, 1, 1}, {16, 16}, 1};
    spr.texture = e.add_texture(sheet);
    e.world().add(s, spr);
    Animator an;
    an.columns = 2;
    an.frame_w = an.frame_h = 8;
    an.add_clip({"blink", 0, 1, 10.f, true});
    e.world().add(s, an);
    Image img;
    e.step(0);
    e.capture_frame(img);
    CHECK(img.get_pixel(160, 90) == 0x0000FFFFu);
    for (int i = 0; i < 7; ++i) e.step(0);  // t = 0.117 s -> frame 1
    e.capture_frame(img);
    CHECK(img.get_pixel(160, 90) == 0xFFFF00FFu);
    e.world().get<Sprite>(s)->flip_x = true;  // flipping must not change a uniform frame
    e.step(0);
    e.capture_frame(img);
    CHECK(img.get_pixel(160, 90) != 0x000000FFu);
}

TEST(engine_interpolates_between_fixed_steps) {
    EngineConfig cfg = soft_cfg();
    cfg.clear_color = {0, 0, 0, 1};
    Engine e(cfg);
    Entity m = e.world().create();
    e.world().add(m, Transform({0, 0}));
    e.world().add(m, Velocity{{600, 0}, 0});
    RigidBody rb = RigidBody::Dynamic();
    rb.gravity_scale = 0.f;
    e.world().add(m, rb);
    e.world().add(m, Sprite{{1, 0, 0, 1}, {4, 4}, 1});

    Image img;
    e.step(1.f / 120.f);  // no fixed step yet
    e.step(1.f / 120.f);  // first fixed step: 0 -> 10, alpha ~ 0
    e.step(1.f / 120.f);  // halfway through the next interval -> drawn at ~5
    CHECK_NEAR(e.world().get<Transform>(m)->position.x, 10, 1e-3);
    CHECK_NEAR(e.world().get<Transform>(m)->prev_position.x, 0, 1e-3);
    e.capture_frame(img);
    CHECK(img.get_pixel(165, 90) == 0xFF0000FFu);  // x = 160 + 5
    CHECK(img.get_pixel(172, 90) == 0x000000FFu);  // not at the raw simulation position (170)
    CHECK(img.get_pixel(155, 90) == 0x000000FFu);
}

TEST(engine_camera_follows_target_with_expected_lag) {
    Engine e(soft_cfg());
    Entity t = e.world().create();
    e.world().add(t, Transform({0, 0}));
    e.world().add(t, Velocity{{300, 0}, 0});
    RigidBody rb = RigidBody::Dynamic();
    rb.gravity_scale = 0.f;
    e.world().add(t, rb);
    e.world().add(t, CameraTarget{});
    for (int i = 0; i < 240; ++i) e.step(0);  // 4 s at 300 px/s
    float lag = e.world().get<Transform>(t)->position.x - e.camera().position.x;
    CHECK_NEAR(lag, 300.0 / e.config().camera_follow_lambda, 12.0);  // steady-state lag = v / lambda
    CHECK(e.camera().position.x > 1000);
}

static uint64_t run_game(uint64_t seed, bool press_keys) {
    EngineConfig cfg = soft_cfg(240, 135);
    cfg.seed = seed;
    Engine e(cfg);
    e.camera().snap_to({0, 100});  // look at the floor (top at y = 140) and where the boxes land
    e.spawn_static_rect({0, 160}, {800, 40}, Color::from_hex(0x445566), -5);
    for (int i = 0; i < 12; ++i)
        e.spawn_dynamic_rect({e.rng().range(-100, 100), e.rng().range(-200, 0)}, {14, 14}, Color::from_hex(0xFF8844));
    struct S : Scene {
        void on_update(Engine& en, float) override {
            if (en.frame() == 80) {  // burst still alive in the final frame
                ParticleBurst b;
                b.position = {0, 100};
                b.count = 30;
                en.particles().emit(b);
                en.camera().add_trauma(0.8f);
            }
            if (en.input().down(Key::D)) en.camera().position.x += 1.f;
        }
    };
    e.set_scene(std::make_unique<S>());
    if (press_keys) e.set_input_script({{10, Key::D, true}, {50, Key::D, false}});
    for (int i = 0; i < 90; ++i) e.step(0);
    Image img;
    e.capture_frame(img);
    return img.hash();
}

TEST(engine_headless_runs_are_bit_identical) {
    uint64_t a = run_game(5, true), b = run_game(5, true);
    CHECK(a == b);
    CHECK(a != Image(240, 135, 0x12121CFFu).hash());  // the frame actually contains something
    CHECK(run_game(5, false) != a);  // input changed the outcome
    CHECK(run_game(6, true) != a);   // seed changed the outcome
}

TEST(engine_screenshot_roundtrip) {
    Engine e(soft_cfg(160, 90));
    e.spawn_static_rect({0, 0}, {50, 30}, {1, 0.5f, 0, 1}, 0);
    const std::string path = "/tmp/prism_test_screenshot.png";
    std::remove(path.c_str());
    e.request_screenshot(path);
    e.step(0);
    Image shot, live;
    std::string err;
    CHECK(load_png(path, shot, &err));
    CHECK(e.capture_frame(live));
    CHECK(shot.width == 160 && shot.height == 90);
    CHECK(shot.hash() == live.hash());
}

TEST(engine_requesting_missing_sdl_backend_fails_cleanly) {
    if (sdl_backend_available()) return;  // nothing to check when SDL is compiled in
    EngineConfig cfg = soft_cfg();
    cfg.backend = Backend::SDL;
    bool threw = false;
    try {
        Engine e(cfg);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

// ───────────────────────────── runner ─────────────────────────────

extern "C" int SDL_main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed_tests = 0;
    for (const TestCase& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = g_failures;
        auto t0 = std::chrono::steady_clock::now();
        t.fn();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool ok = g_failures == before;
        std::printf("[%s] %-52s %6.0f ms\n", ok ? " ok " : "FAIL", t.name, ms);
        ++ran;
        failed_tests += !ok;
    }
    std::printf("\n%d tests, %d checks, %d failed checks (%d failing tests)\n", ran, g_checks, g_failures,
                failed_tests);
    return g_failures ? 1 : 0;
}
