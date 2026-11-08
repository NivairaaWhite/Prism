#pragma once
// Renderer: the engine's drawing + window abstraction. All coordinates are screen-space
// pixels (origin top-left). The world->screen transform is done by the engine/camera.
//
// Backends:
//   SoftwareRenderer - pure CPU framebuffer; no window; works anywhere (CI, servers, tests).
//   SdlRenderer      - SDL2 window + SDL_Renderer (compiled when SDL2 is available).
#include <memory>
#include <string>

#include "image.hpp"
#include "input.hpp"
#include "types.hpp"

namespace prism {

enum class Backend { Auto, SDL, Software };

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual const char* name() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;

    virtual TextureId create_texture(const Image& img) = 0;
    virtual void destroy_texture(TextureId id) = 0;

    virtual void begin_frame(Color clear) = 0;
    virtual void end_frame() = 0;  // present

    virtual void fill_rect(Rect r, Color c) = 0;
    virtual void draw_line(Vec2 a, Vec2 b, Color c) = 0;
    virtual void fill_circle(Vec2 center, float radius, Color c) = 0;
    // src.w == 0 => whole texture. `tint` multiplies texel color and alpha.
    virtual void draw_texture(TextureId tex, IRect src, Rect dst, Color tint, bool flip_x, bool flip_y) = 0;

    // Read back the current frame (valid between begin_frame and end_frame). Returns false if unsupported.
    virtual bool capture(Image& out) = 0;

    // Window-system hook. Returns false when the user asked to close the window.
    virtual bool poll_events(Input& /*input*/) { return true; }
    virtual void set_title(const std::string& /*title*/) {}

    // ---- helpers built on the primitives above ----
    void draw_rect(Rect r, Color c, float thickness = 1.f);  // outline
    void draw_circle(Vec2 center, float radius, Color c, int segments = 28);  // outline
    void init_font();                                          // call once after construction
    void draw_text(const std::string& text, Vec2 pos, Color c, float scale = 2.f);
    Vec2 measure_text(const std::string& text, float scale = 2.f) const;

protected:
    TextureId font_tex_ = 0;
};

// Throws std::runtime_error if the requested backend is unavailable.
std::unique_ptr<Renderer> create_renderer(Backend backend, const std::string& title, int width, int height, bool vsync);

bool sdl_backend_available();

}  // namespace prism
