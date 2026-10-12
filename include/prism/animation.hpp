#pragma once
// Sprite-sheet animation. An Animator drives the `src` rectangle of the entity's Sprite
// from a regular grid of equally sized frames.
#include <string>
#include <vector>

#include "components.hpp"

namespace prism {

struct AnimClip {
    std::string name;
    int first = 0, last = 0;  // inclusive frame indices (row-major in the grid)
    float fps = 8.f;
    bool loop = true;
};

struct Animator {
    std::vector<AnimClip> clips;
    int columns = 1;  // frames per row in the sheet
    int frame_w = 16, frame_h = 16;

    int current = -1;
    float time = 0.f;
    int frame = 0;  // current frame index in the sheet
    bool finished = false;

    void add_clip(AnimClip c) { clips.push_back(std::move(c)); }
    // Switch clips; does nothing if already playing it unless `restart`.
    void play(const std::string& name, bool restart = false) {
        for (int i = 0; i < static_cast<int>(clips.size()); ++i)
            if (clips[i].name == name) {
                if (i != current || restart) {
                    current = i;
                    time = 0.f;
                    finished = false;
                }
                return;
            }
    }
    const std::string& current_name() const {
        static const std::string none;
        return current >= 0 ? clips[current].name : none;
    }
    void advance(float dt);
    IRect source_rect() const { return {(frame % columns) * frame_w, (frame / columns) * frame_h, frame_w, frame_h}; }
};

// Advances every Animator and writes the resulting source rect into the Sprite.
void update_animations(World& world, float dt);

}  // namespace prism
