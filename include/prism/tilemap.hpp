#pragma once
// Grid tilemap: stored as a component (draws at its z_index alongside sprites), with a
// helper that turns solid tiles into a small set of merged static colliders.
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "camera.hpp"
#include "components.hpp"
#include "renderer.hpp"

namespace prism {

struct TileDef {
    Color color{1, 1, 1, 1};  // fill color, or tint if a texture is set
    TextureId texture = 0;
    IRect src{};
    bool solid = false;
    bool one_way = false;  // solid from above only; drawn as a thin top strip when untextured
};

class Tilemap {
public:
    Vec2 origin{0, 0};  // world position of the top-left corner
    int z_index = -10;

    Tilemap() = default;
    Tilemap(int cols, int rows, float tile_size, Vec2 origin_ = {0, 0})
        : origin(origin_), cols_(cols), rows_(rows), tile_size_(tile_size),
          tiles_(static_cast<size_t>(cols) * rows, 0) {
        defs_.emplace_back();  // id 0 = empty
    }

    // Register a tile type; returns its id (>= 1).
    uint16_t define(TileDef def) {
        defs_.push_back(def);
        return static_cast<uint16_t>(defs_.size() - 1);
    }
    const TileDef* def(uint16_t id) const { return id < defs_.size() && id != 0 ? &defs_[id] : nullptr; }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    float tile_size() const { return tile_size_; }
    Rect bounds() const { return {origin, origin + Vec2{cols_ * tile_size_, rows_ * tile_size_}}; }

    uint16_t tile(int x, int y) const { return in_range(x, y) ? tiles_[static_cast<size_t>(y) * cols_ + x] : 0; }
    void set(int x, int y, uint16_t id) {
        if (in_range(x, y)) tiles_[static_cast<size_t>(y) * cols_ + x] = id;
    }
    bool is_solid(int x, int y) const {
        const TileDef* d = def(tile(x, y));
        return d && d->solid;
    }
    Vec2 tile_center(int x, int y) const {
        return origin + Vec2{(x + 0.5f) * tile_size_, (y + 0.5f) * tile_size_};
    }
    // Tile coordinates containing a world point (may be out of range).
    void world_to_tile(Vec2 p, int& tx, int& ty) const {
        tx = static_cast<int>(std::floor((p.x - origin.x) / tile_size_));
        ty = static_cast<int>(std::floor((p.y - origin.y) / tile_size_));
    }

    // Build a map from text rows. `legend` maps characters to tile definitions (characters not
    // in the legend are empty). Tile ids are assigned in ascending character order.
    static Tilemap from_ascii(const std::vector<std::string>& rows, float tile_size, Vec2 origin,
                              const std::unordered_map<char, TileDef>& legend);

    // Create static colliders for all solid tiles, merging runs into larger rectangles.
    std::vector<Entity> build_colliders(World& world, uint32_t layer = 1, float friction = 0.6f) const;

    void draw(Renderer& r, const Camera2D& cam) const;

private:
    bool in_range(int x, int y) const { return x >= 0 && y >= 0 && x < cols_ && y < rows_; }

    int cols_ = 0, rows_ = 0;
    float tile_size_ = 32.f;
    std::vector<uint16_t> tiles_;
    std::vector<TileDef> defs_;
};

}  // namespace prism
