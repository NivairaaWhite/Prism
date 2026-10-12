#include "prism/tilemap.hpp"

#include <algorithm>
#include <cmath>

namespace prism {

Tilemap Tilemap::from_ascii(const std::vector<std::string>& rows, float tile_size, Vec2 origin,
                            const std::unordered_map<char, TileDef>& legend) {
    size_t width = 0;
    for (const auto& r : rows) width = std::max(width, r.size());
    Tilemap tm(static_cast<int>(width), static_cast<int>(rows.size()), tile_size, origin);

    std::map<char, uint16_t> ids;  // ordered => deterministic id assignment
    std::map<char, TileDef> sorted(legend.begin(), legend.end());
    for (const auto& [ch, def] : sorted) ids[ch] = tm.define(def);

    for (size_t y = 0; y < rows.size(); ++y)
        for (size_t x = 0; x < rows[y].size(); ++x) {
            auto it = ids.find(rows[y][x]);
            if (it != ids.end()) tm.set(static_cast<int>(x), static_cast<int>(y), it->second);
        }
    return tm;
}

std::vector<Entity> Tilemap::build_colliders(World& world, uint32_t layer, float friction) const {
    // Maximal horizontal runs per row, then identical runs in consecutive rows are fused
    // vertically. (Extending down from a narrow run would carve wide floors into strips.)
    struct Run {
        int x0, x1, y0, y1, kind;  // kind: 1 = solid, 2 = one-way
    };
    auto kind = [&](int x, int y) -> int {
        const TileDef* d = def(tile(x, y));
        return (d && d->solid) ? (d->one_way ? 2 : 1) : 0;
    };

    std::vector<Run> runs;
    std::vector<size_t> prev_open, cur_open;
    for (int y = 0; y < rows_; ++y) {
        cur_open.clear();
        for (int x = 0; x < cols_;) {
            int k = kind(x, y);
            if (!k) {
                ++x;
                continue;
            }
            int x1 = x;
            while (x1 + 1 < cols_ && kind(x1 + 1, y) == k) ++x1;
            size_t match = runs.size();
            for (size_t idx : prev_open)
                if (runs[idx].x0 == x && runs[idx].x1 == x1 && runs[idx].kind == k) {
                    match = idx;
                    break;
                }
            if (match < runs.size()) {
                runs[match].y1 = y;
                cur_open.push_back(match);
            } else {
                runs.push_back({x, x1, y, y, k});
                cur_open.push_back(runs.size() - 1);
            }
            x = x1 + 1;
        }
        prev_open.swap(cur_open);
    }

    std::vector<Entity> out;
    for (const Run& r : runs) {
        Vec2 min = origin + Vec2{r.x0 * tile_size_, r.y0 * tile_size_};
        Vec2 max = origin + Vec2{(r.x1 + 1) * tile_size_, (r.y1 + 1) * tile_size_};
        Entity e = world.create();
        world.add(e, Transform((min + max) * 0.5f));
        Collider c = Collider::box((max - min) * 0.5f);
        c.one_way = (r.kind == 2);
        c.layer = layer;
        world.add(e, c);
        world.add(e, RigidBody::Static(friction));
        out.push_back(e);
    }
    return out;
}

void Tilemap::draw(Renderer& r, const Camera2D& cam) const {
    if (cols_ == 0 || rows_ == 0) return;
    Rect view = cam.visible_rect();
    int x0 = std::max(0, static_cast<int>(std::floor((view.min.x - origin.x) / tile_size_)));
    int x1 = std::min(cols_ - 1, static_cast<int>(std::floor((view.max.x - origin.x) / tile_size_)));
    int y0 = std::max(0, static_cast<int>(std::floor((view.min.y - origin.y) / tile_size_)));
    int y1 = std::min(rows_ - 1, static_cast<int>(std::floor((view.max.y - origin.y) / tile_size_)));

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const TileDef* d = def(tile(x, y));
            if (!d) continue;
            // Edges come from one formula so neighbouring tiles share exact boundaries (no seams).
            Vec2 a = cam.world_to_screen(origin + Vec2{x * tile_size_, y * tile_size_});
            Vec2 b = cam.world_to_screen(origin + Vec2{(x + 1) * tile_size_, (y + 1) * tile_size_});
            if (d->texture)
                r.draw_texture(d->texture, d->src, {a, b}, d->color, false, false);
            else if (d->one_way)
                r.fill_rect({a, {b.x, a.y + (b.y - a.y) * 0.3f}}, d->color);
            else
                r.fill_rect({a, b}, d->color);
        }
    }
}

}  // namespace prism
