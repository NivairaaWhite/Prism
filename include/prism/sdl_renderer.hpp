#pragma once
// SDL2 window + SDL_Renderer backend. Only compiled when PRISM_HAS_SDL is defined.
#ifdef PRISM_HAS_SDL
#include <SDL2/SDL.h>

#include <vector>

#include "renderer.hpp"

namespace prism {

class SdlRenderer final : public Renderer {
public:
    SdlRenderer(const std::string& title, int width, int height, bool vsync);
    ~SdlRenderer() override;
    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;

    const char* name() const override { return "sdl2"; }
    int width() const override { return width_; }
    int height() const override { return height_; }

    TextureId create_texture(const Image& img) override;
    void destroy_texture(TextureId id) override;

    void begin_frame(Color clear) override;
    void end_frame() override;

    void fill_rect(Rect r, Color c) override;
    void draw_line(Vec2 a, Vec2 b, Color c) override;
    void fill_circle(Vec2 center, float radius, Color c) override;
    void draw_texture(TextureId tex, IRect src, Rect dst, Color tint, bool flip_x, bool flip_y) override;
    bool capture(Image& out) override;

    bool poll_events(Input& input) override;
    void set_title(const std::string& title) override;

private:
    Vec2 to_logical(int window_x, int window_y) const;
    void set_color(Color c);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int width_, height_;
    std::vector<SDL_Texture*> textures_;  // id = index + 1
};

}  // namespace prism
#endif
