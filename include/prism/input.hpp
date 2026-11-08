#pragma once
// Backend-agnostic input state. Window backends call set_key()/set_mouse_*();
// game code reads down()/pressed()/released() or named actions.
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace prism {

enum class Key : uint8_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Left, Right, Up, Down,
    Space, Enter, Escape, Tab, Backspace,
    LShift, RShift, LCtrl, RCtrl,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

enum class MouseButton : uint8_t { Left = 0, Middle, Right, Count };

class Input {
public:
    // Clear per-frame edge flags (pressed/released). Called by the engine once per frame.
    void begin_frame() {
        pressed_.fill(false);
        released_.fill(false);
        mouse_pressed_.fill(false);
        mouse_released_.fill(false);
    }

    void set_key(Key k, bool is_down) {
        auto i = static_cast<size_t>(k);
        if (k == Key::Unknown || i >= down_.size()) return;
        if (is_down && !down_[i]) pressed_[i] = true;
        if (!is_down && down_[i]) released_[i] = true;
        down_[i] = is_down;
    }
    void set_mouse_button(MouseButton b, bool is_down) {
        auto i = static_cast<size_t>(b);
        if (is_down && !mouse_down_[i]) mouse_pressed_[i] = true;
        if (!is_down && mouse_down_[i]) mouse_released_[i] = true;
        mouse_down_[i] = is_down;
    }
    void set_mouse_pos(Vec2 p) { mouse_pos_ = p; }

    bool down(Key k) const { return down_[static_cast<size_t>(k)]; }
    bool pressed(Key k) const { return pressed_[static_cast<size_t>(k)]; }
    bool released(Key k) const { return released_[static_cast<size_t>(k)]; }
    bool mouse_down(MouseButton b = MouseButton::Left) const { return mouse_down_[static_cast<size_t>(b)]; }
    bool mouse_pressed(MouseButton b = MouseButton::Left) const { return mouse_pressed_[static_cast<size_t>(b)]; }
    bool mouse_released(MouseButton b = MouseButton::Left) const { return mouse_released_[static_cast<size_t>(b)]; }
    Vec2 mouse_pos() const { return mouse_pos_; }  // in screen (logical) pixels

    // ---- action mapping ----
    void bind(const std::string& action, std::vector<Key> keys) { actions_[action] = std::move(keys); }
    bool action_down(const std::string& a) const { return any(a, down_); }
    bool action_pressed(const std::string& a) const { return any(a, pressed_); }
    bool action_released(const std::string& a) const { return any(a, released_); }
    // -1 / 0 / +1 from two actions.
    float axis(const std::string& negative, const std::string& positive) const {
        return (action_down(positive) ? 1.f : 0.f) - (action_down(negative) ? 1.f : 0.f);
    }

private:
    static constexpr size_t N = static_cast<size_t>(Key::Count);
    using Keys = std::array<bool, N>;

    bool any(const std::string& action, const Keys& state) const {
        auto it = actions_.find(action);
        if (it == actions_.end()) return false;
        for (Key k : it->second)
            if (state[static_cast<size_t>(k)]) return true;
        return false;
    }

    Keys down_{}, pressed_{}, released_{};
    std::array<bool, 3> mouse_down_{}, mouse_pressed_{}, mouse_released_{};
    Vec2 mouse_pos_{};
    std::unordered_map<std::string, std::vector<Key>> actions_;
};

}  // namespace prism
