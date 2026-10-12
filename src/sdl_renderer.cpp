#include "prism/sdl_renderer.hpp"

#ifdef PRISM_HAS_SDL
#include <cmath>
#include <stdexcept>

namespace prism {
namespace {

inline uint8_t to_u8(float v) { return static_cast<uint8_t>(std::lround(clampf(v, 0.f, 1.f) * 255.f)); }
inline bool sane(float v) { return std::isfinite(v) && std::abs(v) < 1.0e6f; }

Key map_scancode(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return static_cast<Key>(static_cast<int>(Key::A) + (s - SDL_SCANCODE_A));
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return static_cast<Key>(static_cast<int>(Key::Num1) + (s - SDL_SCANCODE_1));
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return static_cast<Key>(static_cast<int>(Key::F1) + (s - SDL_SCANCODE_F1));
    switch (s) {
        case SDL_SCANCODE_0: return Key::Num0;
        case SDL_SCANCODE_LEFT: return Key::Left;
        case SDL_SCANCODE_RIGHT: return Key::Right;
        case SDL_SCANCODE_UP: return Key::Up;
        case SDL_SCANCODE_DOWN: return Key::Down;
        case SDL_SCANCODE_SPACE: return Key::Space;
        case SDL_SCANCODE_RETURN: return Key::Enter;
        case SDL_SCANCODE_ESCAPE: return Key::Escape;
        case SDL_SCANCODE_TAB: return Key::Tab;
        case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
        case SDL_SCANCODE_LSHIFT: return Key::LShift;
        case SDL_SCANCODE_RSHIFT: return Key::RShift;
        case SDL_SCANCODE_LCTRL: return Key::LCtrl;
        case SDL_SCANCODE_RCTRL: return Key::RCtrl;
        default: return Key::Unknown;
    }
}

}  // namespace

SdlRenderer::SdlRenderer(const std::string& title, int w, int h, bool vsync) : width_(w), height_(h) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest-neighbour: crisp pixels
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        std::string err = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("Window creation failed: " + err);
    }

    Uint32 flags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    renderer_ = SDL_CreateRenderer(window_, -1, flags);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);  // e.g. under Xvfb
    if (!renderer_) {
        std::string err = SDL_GetError();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw std::runtime_error("Renderer creation failed: " + err);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(renderer_, w, h);  // the game always sees w x h; the window may be resized freely
}

SdlRenderer::~SdlRenderer() {
    for (SDL_Texture* t : textures_)
        if (t) SDL_DestroyTexture(t);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SdlRenderer::set_color(Color c) { SDL_SetRenderDrawColor(renderer_, to_u8(c.r), to_u8(c.g), to_u8(c.b), to_u8(c.a)); }

TextureId SdlRenderer::create_texture(const Image& img) {
    if (img.empty()) return 0;
    SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!t) return 0;
    SDL_UpdateTexture(t, nullptr, img.pixels.data(), img.width * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    textures_.push_back(t);
    return static_cast<TextureId>(textures_.size());
}

void SdlRenderer::destroy_texture(TextureId id) {
    if (id >= 1 && id <= textures_.size() && textures_[id - 1]) {
        SDL_DestroyTexture(textures_[id - 1]);
        textures_[id - 1] = nullptr;
    }
}

void SdlRenderer::begin_frame(Color clear) {
    set_color({clear.r, clear.g, clear.b, 1.f});
    SDL_RenderClear(renderer_);
}

void SdlRenderer::end_frame() { SDL_RenderPresent(renderer_); }

void SdlRenderer::fill_rect(Rect r, Color c) {
    if (!sane(r.min.x) || !sane(r.min.y) || !sane(r.max.x) || !sane(r.max.y)) return;
    int x0 = static_cast<int>(std::lround(r.min.x)), x1 = static_cast<int>(std::lround(r.max.x));
    int y0 = static_cast<int>(std::lround(r.min.y)), y1 = static_cast<int>(std::lround(r.max.y));
    if (x1 == x0 && r.max.x > r.min.x) ++x1;  // same rounding rules as the software backend
    if (y1 == y0 && r.max.y > r.min.y) ++y1;
    if (x1 <= x0 || y1 <= y0) return;
    SDL_Rect d{x0, y0, x1 - x0, y1 - y0};
    set_color(c);
    SDL_RenderFillRect(renderer_, &d);
}

void SdlRenderer::draw_line(Vec2 a, Vec2 b, Color c) {
    if (!sane(a.x) || !sane(a.y) || !sane(b.x) || !sane(b.y)) return;
    set_color(c);
    SDL_RenderDrawLine(renderer_, static_cast<int>(std::lround(a.x)), static_cast<int>(std::lround(a.y)),
                       static_cast<int>(std::lround(b.x)), static_cast<int>(std::lround(b.y)));
}

void SdlRenderer::fill_circle(Vec2 c, float radius, Color col) {
    if (!sane(c.x) || !sane(c.y) || !sane(radius) || radius <= 0.f) return;
    set_color(col);
    int ymin = static_cast<int>(std::floor(c.y - radius)), ymax = static_cast<int>(std::ceil(c.y + radius));
    for (int y = ymin; y <= ymax; ++y) {
        float dy = (static_cast<float>(y) + 0.5f) - c.y;
        float rem = radius * radius - dy * dy;
        if (rem <= 0.f) continue;
        float half = std::sqrt(rem);
        int x0 = static_cast<int>(std::lround(c.x - half)), x1 = static_cast<int>(std::lround(c.x + half));
        if (x1 > x0) SDL_RenderDrawLine(renderer_, x0, y, x1 - 1, y);
    }
}

void SdlRenderer::draw_texture(TextureId id, IRect src, Rect dst, Color tint, bool flip_x, bool flip_y) {
    if (id < 1 || id > textures_.size() || !textures_[id - 1]) return;
    if (!sane(dst.min.x) || !sane(dst.min.y) || !sane(dst.max.x) || !sane(dst.max.y)) return;
    SDL_Texture* t = textures_[id - 1];

    SDL_Rect s{src.x, src.y, src.w, src.h};
    if (src.w <= 0 || src.h <= 0) {
        int tw = 0, th = 0;
        SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
        s = SDL_Rect{0, 0, tw, th};
    }
    int x0 = static_cast<int>(std::lround(dst.min.x)), x1 = static_cast<int>(std::lround(dst.max.x));
    int y0 = static_cast<int>(std::lround(dst.min.y)), y1 = static_cast<int>(std::lround(dst.max.y));
    if (x1 <= x0 || y1 <= y0) return;
    SDL_Rect d{x0, y0, x1 - x0, y1 - y0};

    SDL_SetTextureColorMod(t, to_u8(tint.r), to_u8(tint.g), to_u8(tint.b));
    SDL_SetTextureAlphaMod(t, to_u8(tint.a));
    int flip = (flip_x ? SDL_FLIP_HORIZONTAL : 0) | (flip_y ? SDL_FLIP_VERTICAL : 0);
    SDL_RenderCopyEx(renderer_, t, &s, &d, 0.0, nullptr, static_cast<SDL_RendererFlip>(flip));
}

bool SdlRenderer::capture(Image& out) {
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(renderer_, &ow, &oh) != 0 || ow <= 0 || oh <= 0) return false;
    out = Image(ow, oh);
    return SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA32, out.pixels.data(), ow * 4) == 0;
}

Vec2 SdlRenderer::to_logical(int wx, int wy) const {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    float lx = 0.f, ly = 0.f;
    SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
    return {lx, ly};
#else
    return {static_cast<float>(wx), static_cast<float>(wy)};
#endif
}

bool SdlRenderer::poll_events(Input& input) {
    bool keep_running = true;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                keep_running = false;
                break;
            case SDL_KEYDOWN:
                if (!e.key.repeat) input.set_key(map_scancode(e.key.keysym.scancode), true);
                break;
            case SDL_KEYUP:
                input.set_key(map_scancode(e.key.keysym.scancode), false);
                break;
            case SDL_MOUSEMOTION:
                input.set_mouse_pos(to_logical(e.motion.x, e.motion.y));
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool down = e.type == SDL_MOUSEBUTTONDOWN;
                input.set_mouse_pos(to_logical(e.button.x, e.button.y));
                if (e.button.button == SDL_BUTTON_LEFT) input.set_mouse_button(MouseButton::Left, down);
                else if (e.button.button == SDL_BUTTON_MIDDLE) input.set_mouse_button(MouseButton::Middle, down);
                else if (e.button.button == SDL_BUTTON_RIGHT) input.set_mouse_button(MouseButton::Right, down);
                break;
            }
            default:
                break;
        }
    }
    return keep_running;
}

void SdlRenderer::set_title(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

}  // namespace prism
#endif
