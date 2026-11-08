#pragma once
#include <vector>

#include "renderer.hpp"

namespace prism {

// CPU rasterizer. Deterministic (integer blending), headless, and the reference backend
// used by the tests. The finished frame is available via framebuffer().
class SoftwareRenderer final : public Renderer {
public:
    SoftwareRenderer(int width, int height);

    const char* name() const override { return "software"; }
    int width() const override { return fb_.width; }
    int height() const override { return fb_.height; }

    TextureId create_texture(const Image& img) override;
    void destroy_texture(TextureId id) override;

    void begin_frame(Color clear) override;
    void end_frame() override {}

    void fill_rect(Rect r, Color c) override;
    void draw_line(Vec2 a, Vec2 b, Color c) override;
    void fill_circle(Vec2 center, float radius, Color c) override;
    void draw_texture(TextureId tex, IRect src, Rect dst, Color tint, bool flip_x, bool flip_y) override;
    bool capture(Image& out) override {
        out = fb_;
        return true;
    }

    const Image& framebuffer() const { return fb_; }

private:
    void blend_span(int y, int x0, int x1, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    Image fb_;
    std::vector<Image> textures_;  // id = index + 1; destroyed slots are emptied
};

}  // namespace prism
