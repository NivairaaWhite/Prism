#include "prism/renderer.hpp"

#include <stdexcept>

#include "prism/font.hpp"
#include "prism/software_renderer.hpp"
#ifdef PRISM_HAS_SDL
#include "prism/sdl_renderer.hpp"
#endif

namespace prism {

void Renderer::draw_rect(Rect r, Color c, float t) {
    fill_rect({{r.min.x, r.min.y}, {r.max.x, r.min.y + t}}, c);
    fill_rect({{r.min.x, r.max.y - t}, {r.max.x, r.max.y}}, c);
    fill_rect({{r.min.x, r.min.y + t}, {r.min.x + t, r.max.y - t}}, c);
    fill_rect({{r.max.x - t, r.min.y + t}, {r.max.x, r.max.y - t}}, c);
}

void Renderer::draw_circle(Vec2 center, float radius, Color c, int segments) {
    Vec2 prev = center + Vec2{radius, 0.f};
    for (int i = 1; i <= segments; ++i) {
        Vec2 p = center + Vec2::from_angle(TAU * static_cast<float>(i) / static_cast<float>(segments)) * radius;
        draw_line(prev, p, c);
        prev = p;
    }
}

void Renderer::init_font() {
    if (!font_tex_) font_tex_ = create_texture(make_font_atlas());
}

void Renderer::draw_text(const std::string& text, Vec2 pos, Color c, float scale) {
    if (!font_tex_) return;
    Vec2 cursor = pos;
    for (char ch : text) {
        if (ch == '\n') {
            cursor = {pos.x, cursor.y + FONT_CELL_H * scale};
            continue;
        }
        if (ch != ' ')
            draw_texture(font_tex_, font_glyph_rect(ch),
                         {cursor, cursor + Vec2{FONT_CELL_W * scale, FONT_CELL_H * scale}}, c, false, false);
        cursor.x += FONT_CELL_W * scale;
    }
}

Vec2 Renderer::measure_text(const std::string& text, float scale) const {
    float line = 0.f, maxw = 0.f;
    int lines = 1;
    for (char ch : text) {
        if (ch == '\n') {
            ++lines;
            line = 0.f;
        } else {
            line += FONT_CELL_W * scale;
            maxw = std::max(maxw, line);
        }
    }
    return {maxw, lines * FONT_CELL_H * scale};
}

bool sdl_backend_available() {
#ifdef PRISM_HAS_SDL
    return true;
#else
    return false;
#endif
}

std::unique_ptr<Renderer> create_renderer(Backend backend, const std::string& title, int w, int h, bool vsync) {
    std::unique_ptr<Renderer> r;
    if (backend == Backend::SDL || backend == Backend::Auto) {
#ifdef PRISM_HAS_SDL
        r = std::make_unique<SdlRenderer>(title, w, h, vsync);
#else
        if (backend == Backend::SDL)
            throw std::runtime_error("Prism was built without the SDL2 backend (SDL2 not found at build time)");
#endif
    }
    if (!r) r = std::make_unique<SoftwareRenderer>(w, h);
    (void)title;
    (void)vsync;
    r->init_font();
    return r;
}

}  // namespace prism
