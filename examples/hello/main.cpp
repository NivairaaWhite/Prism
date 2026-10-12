// The smallest useful Prism program: a controllable box on the ground.
#include <cstdio>

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

    void on_render(Engine&, Renderer& r) override { r.draw_text("HELLO PRISM", {16, 16}, Color{1, 1, 1, 1}, 3.f); }
};

extern "C" int SDL_main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Engine engine({.title = "Hello Prism"});
    engine.set_scene(std::make_unique<Hello>());
    if (sdl_backend_available()) {
        engine.run();
    } else {  // no window available in this build: render a couple of seconds to a PNG instead
        std::puts("No SDL2 backend in this build; rendering 2 s headless to hello.png");
        for (int i = 0; i < 120; ++i) {
            if (i == 119) engine.request_screenshot("hello.png");
            engine.step(0);
        }
    }
    return 0;
}
