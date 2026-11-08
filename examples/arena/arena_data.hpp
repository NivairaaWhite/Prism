#pragma once
// Prism Arena: game data. Everything here is plain data or a pure function with no dependency on
// the engine's World/Renderer, so it can be tested in isolation and changed without touching
// game logic:
//   * to add an enemy:  add a row to kEnemies (and a row to the art in arena_art.hpp)
//   * to add a pickup:  add a PickupKind + a row to kPickups, handle it in PlayScene::collect
//   * to add a map:     write an ASCII file and pass --map file.txt  ('#' wall, '.' floor,
//                       'P' player start, 'S' enemy spawn point; validated on load)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "prism/types.hpp"

namespace arena {

using prism::Rng;
using prism::Vec2;

constexpr float TILE = 32.f;

// Collision layers. Each entity's collider has one layer and a mask of the layers it reacts to.
namespace layer {
constexpr uint32_t WALL = 1, PLAYER = 2, ENEMY = 4, PLAYER_BULLET = 8, ENEMY_BULLET = 16, PICKUP = 32;
}

// ───────────────────────────── enemies ─────────────────────────────

enum class Behavior : uint8_t {
    Chase,   // run straight at the player
    Kite,    // hold a distance, strafe, shoot when there is a clear line of sight
    Charge,  // approach, telegraph, dash in a straight line, recover
};

struct EnemyDef {
    const char* name;
    Behavior behavior;
    float radius, hp, speed, accel, mass, contact_damage;
    int score;
    int cost;      // what it costs in a wave's budget
    int min_wave;  // first wave it can appear in
    int weight;    // relative likelihood once allowed
    uint32_t color;  // 0xRRGGBB, used for particles
    float drop_chance = 0.f;
    // Kite
    float keep_distance = 0.f, fire_interval = 0.f, bullet_speed = 0.f, bullet_damage = 0.f;
    // Charge
    float windup = 0.f, dash_speed = 0.f, dash_time = 0.f, recover = 0.f;
    // Death: spawns `split_into` enemies of type `split_type`
    int split_into = 0, split_type = 0;
};

// The index of a row is the enemy's type id (also the index of its art in arena_art.hpp).
inline constexpr EnemyDef kEnemies[] = {
    {.name = "DRONE", .behavior = Behavior::Chase, .radius = 10, .hp = 2, .speed = 150, .accel = 900, .mass = 0.6f,
     .contact_damage = 8, .score = 10, .cost = 1, .min_wave = 1, .weight = 10, .color = 0xFF4D5E, .drop_chance = 0.06f},
    {.name = "BRUTE", .behavior = Behavior::Chase, .radius = 18, .hp = 14, .speed = 70, .accel = 500, .mass = 5.f,
     .contact_damage = 20, .score = 60, .cost = 5, .min_wave = 3, .weight = 4, .color = 0xB05CFF, .drop_chance = 0.25f},
    {.name = "GUNNER", .behavior = Behavior::Kite, .radius = 12, .hp = 5, .speed = 110, .accel = 700, .mass = 1.f,
     .contact_damage = 6, .score = 35, .cost = 3, .min_wave = 2, .weight = 6, .color = 0x4DFF88, .drop_chance = 0.15f,
     .keep_distance = 230, .fire_interval = 1.7f, .bullet_speed = 300, .bullet_damage = 10},
    {.name = "SPLITTER", .behavior = Behavior::Chase, .radius = 15, .hp = 7, .speed = 90, .accel = 600, .mass = 2.f,
     .contact_damage = 12, .score = 40, .cost = 4, .min_wave = 4, .weight = 4, .color = 0xFF9F1C, .drop_chance = 0.10f,
     .split_into = 3, .split_type = 0},
    {.name = "DASHER", .behavior = Behavior::Charge, .radius = 12, .hp = 6, .speed = 100, .accel = 800, .mass = 1.2f,
     .contact_damage = 18, .score = 50, .cost = 4, .min_wave = 5, .weight = 3, .color = 0xFFE14D, .drop_chance = 0.15f,
     .windup = 0.55f, .dash_speed = 520, .dash_time = 0.35f, .recover = 0.7f},
};
constexpr int kEnemyTypes = static_cast<int>(sizeof(kEnemies) / sizeof(kEnemies[0]));

// ───────────────────────────── pickups ─────────────────────────────

enum PickupKind : int { PK_Health, PK_Rapid, PK_Spread, PK_Nova, PK_Count };

struct PickupDef {
    const char* name;
    uint32_t color;
    float duration;  // seconds of effect (0 = instant)
    int weight;
};
inline constexpr PickupDef kPickups[PK_Count] = {
    {"HEALTH", 0x4DFF88, 0.f, 6},
    {"RAPID", 0xFFE14D, 10.f, 4},
    {"SPREAD", 0x4DD2FF, 10.f, 4},
    {"NOVA", 0xFF4DD2, 0.f, 2},
};

// ───────────────────────────── tuning ─────────────────────────────

struct Tuning {
    float player_radius = 11.f, player_hp = 100.f, player_speed = 230.f, player_accel = 2600.f;
    float fire_interval = 0.14f, bullet_speed = 780.f, bullet_damage = 1.f, bullet_life = 1.1f, bullet_radius = 4.5f;
    float dash_speed = 560.f, dash_time = 0.14f, dash_cooldown = 0.9f, dash_invuln = 0.3f;
    float hurt_invuln = 0.7f;
    int max_enemies = 60;       // alive + telegraphed; further spawns wait their turn
    int max_wave_units = 36;    // upper bound on how many enemies one wave contains
    float intermission = 2.5f;  // calm between waves
    float pickup_life = 10.f;
};

// ───────────────────────────── waves ─────────────────────────────

// Spend a wave's budget on enemy types, returned in spawn order. Deterministic for a given Rng
// state. Guarantees: never empty, only types whose min_wave has been reached, and the sum of the
// costs never exceeds the budget (except for the single forced fallback unit when even the
// cheapest allowed type does not fit).
inline int wave_budget(int wave, float difficulty) {
    const float w = static_cast<float>(std::max(1, wave));
    return std::max(1, static_cast<int>((4.f + 3.f * w + 0.25f * w * w) * std::max(0.1f, difficulty)));
}

inline std::vector<int> plan_wave(int wave, float difficulty, int max_units, Rng& rng) {
    int budget = wave_budget(wave, difficulty);
    std::vector<int> plan;
    const int limit = std::max(1, max_units);
    while (static_cast<int>(plan.size()) < limit) {
        int total = 0;
        int candidates[kEnemyTypes];
        int n = 0;
        for (int t = 0; t < kEnemyTypes; ++t)
            if (kEnemies[t].min_wave <= wave && kEnemies[t].cost <= budget && kEnemies[t].weight > 0) {
                candidates[n++] = t;
                total += kEnemies[t].weight;
            }
        if (n == 0) break;
        int pick = rng.range_int(0, total - 1);
        int chosen = candidates[n - 1];
        for (int i = 0; i < n; ++i) {
            pick -= kEnemies[candidates[i]].weight;
            if (pick < 0) {
                chosen = candidates[i];
                break;
            }
        }
        plan.push_back(chosen);
        budget -= kEnemies[chosen].cost;
    }
    if (plan.empty()) plan.push_back(0);
    return plan;
}

// ───────────────────────────── maps ─────────────────────────────

struct ArenaMap {
    std::vector<std::string> rows;
    int cols = 0, nrows = 0;
    Vec2 player{};
    std::vector<Vec2> spawns;
};

inline const std::vector<std::string>& default_map_rows() {
    static const std::vector<std::string> rows = {
        "############################################",
        "#..........................................#",
        "#.S...................S..................S.#",
        "#..........................................#",
        "#...##................................##...#",
        "#..........................................#",
        "#........##......................##........#",
        "#........##......................##........#",
        "#........##......................##........#",
        "#..........................................#",
        "#.................########.................#",
        "#..........................................#",
        "#.............#..............#.............#",
        "#.............#..............#.............#",
        "#.S...........#.......P......#...........S.#",
        "#.............#..............#.............#",
        "#..........................................#",
        "#.................########.................#",
        "#..........................................#",
        "#........##......................##........#",
        "#........##......................##........#",
        "#........##......................##........#",
        "#..........................................#",
        "#...##................................##...#",
        "#..........................................#",
        "#.S...................S..................S.#",
        "#..........................................#",
        "############################################",
    };
    return rows;
}

// Validates the rows and fills `out`. Returns "" on success, otherwise a message that says which
// row/column is wrong, so a hand-written map fails loudly instead of misbehaving mid-game.
inline std::string parse_map(const std::vector<std::string>& rows, ArenaMap& out) {
    constexpr int kMin = 12, kMax = 200;
    const int nrows = static_cast<int>(rows.size());
    if (nrows < kMin || nrows > kMax) return "map must have " + std::to_string(kMin) + ".." + std::to_string(kMax) + " rows (has " + std::to_string(nrows) + ")";
    const int cols = static_cast<int>(rows[0].size());
    if (cols < kMin || cols > kMax) return "map must be " + std::to_string(kMin) + ".." + std::to_string(kMax) + " columns wide (row 1 has " + std::to_string(cols) + ")";

    ArenaMap m;
    m.rows = rows;
    m.cols = cols;
    m.nrows = nrows;
    int players = 0, px = 0, py = 0;
    for (int y = 0; y < nrows; ++y) {
        if (static_cast<int>(rows[y].size()) != cols)
            return "row " + std::to_string(y + 1) + " is " + std::to_string(rows[y].size()) + " columns wide, expected " + std::to_string(cols);
        for (int x = 0; x < cols; ++x) {
            const char c = rows[y][x];
            const std::string where = "row " + std::to_string(y + 1) + ", column " + std::to_string(x + 1);
            if (c != '#' && c != '.' && c != 'P' && c != 'S')
                return where + ": unknown tile '" + std::string(1, c) + "' (use # . P S)";
            const bool border = x == 0 || y == 0 || x == cols - 1 || y == nrows - 1;
            if (border && c != '#') return where + ": the border must be solid wall so nothing can leave the arena";
            const Vec2 centre{(x + 0.5f) * TILE, (y + 0.5f) * TILE};
            if (c == 'P') {
                ++players;
                px = x;
                py = y;
                m.player = centre;
            } else if (c == 'S') {
                m.spawns.push_back(centre);
            }
        }
    }
    if (players != 1) return "the map needs exactly one 'P' (player start), found " + std::to_string(players);
    if (m.spawns.size() < 2) return "the map needs at least two 'S' (enemy spawn) tiles, found " + std::to_string(m.spawns.size());

    // Every spawn must be reachable from the player's start, or its enemies would be stuck
    // behind a wall and the wave could never be cleared.
    std::vector<uint8_t> seen(static_cast<size_t>(cols) * nrows, 0);
    std::vector<int> stack{py * cols + px};
    seen[static_cast<size_t>(py) * cols + px] = 1;
    while (!stack.empty()) {
        int i = stack.back();
        stack.pop_back();
        const int x = i % cols, y = i / cols;
        const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
        for (int k = 0; k < 4; ++k) {
            if (nx[k] < 0 || ny[k] < 0 || nx[k] >= cols || ny[k] >= nrows) continue;
            const size_t j = static_cast<size_t>(ny[k]) * cols + nx[k];
            if (seen[j] || rows[ny[k]][nx[k]] == '#') continue;
            seen[j] = 1;
            stack.push_back(static_cast<int>(j));
        }
    }
    for (int y = 0; y < nrows; ++y)
        for (int x = 0; x < cols; ++x)
            if (rows[y][x] == 'S' && !seen[static_cast<size_t>(y) * cols + x])
                return "the spawn at row " + std::to_string(y + 1) + ", column " + std::to_string(x + 1) + " cannot be reached from the player start";
    out = std::move(m);
    return "";
}

// Reads a map file into rows ("\r\n" tolerated, trailing blank lines ignored).
inline std::string load_map_file(const std::string& path, std::vector<std::string>& rows) {
    std::ifstream f(path);
    if (!f) return "cannot open map file '" + path + "'";
    rows.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        rows.push_back(line);
    }
    while (!rows.empty() && rows.back().empty()) rows.pop_back();
    if (rows.empty()) return "map file '" + path + "' is empty";
    return "";
}

// ───────────────────────────── pathfinding ─────────────────────────────

// A distance map from one target tile to every walkable tile (breadth-first, so one build serves
// any number of enemies). direction() says which way to step to get closer. Diagonal steps never
// cut a wall corner. Rebuild it only when the target changes tile.
class FlowField {
public:
    void build(const ArenaMap& m, int tx, int ty) {
        cols_ = m.cols;
        rows_ = m.nrows;
        solid_.assign(static_cast<size_t>(cols_) * rows_, 0);
        for (int y = 0; y < rows_; ++y)
            for (int x = 0; x < cols_; ++x) solid_[idx(x, y)] = m.rows[y][x] == '#';
        dist_.assign(solid_.size(), -1);
        tx_ = ty_ = -1;
        if (tx < 0 || ty < 0 || tx >= cols_ || ty >= rows_ || solid_[idx(tx, ty)]) return;
        tx_ = tx;
        ty_ = ty;
        std::vector<int> queue;
        queue.reserve(solid_.size());
        queue.push_back(static_cast<int>(idx(tx, ty)));
        dist_[idx(tx, ty)] = 0;
        for (size_t head = 0; head < queue.size(); ++head) {
            const int i = queue[head], x = i % cols_, y = i / cols_;
            const int nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= cols_ || ny[k] >= rows_) continue;
                const size_t j = idx(nx[k], ny[k]);
                if (solid_[j] || dist_[j] >= 0) continue;
                dist_[j] = dist_[static_cast<size_t>(i)] + 1;
                queue.push_back(static_cast<int>(j));
            }
        }
    }

    int target_x() const { return tx_; }
    int target_y() const { return ty_; }
    // Steps to the target through open tiles; -1 for walls, unreachable tiles and out-of-range.
    int at(int x, int y) const { return x < 0 || y < 0 || x >= cols_ || y >= rows_ || dist_.empty() ? -1 : dist_[idx(x, y)]; }

    // Unit vector from `pos` toward the centre of the best neighbouring tile. Zero when already on
    // the target tile, when there is no path, or when `pos` is not on an open tile.
    Vec2 direction(Vec2 pos) const {
        if (tx_ < 0) return {};
        const int x = static_cast<int>(std::floor(pos.x / TILE)), y = static_cast<int>(std::floor(pos.y / TILE));
        const int here = at(x, y);
        if (here <= 0) return {};
        int best = here, bx = 0, by = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int d = at(x + dx, y + dy);
                if (d < 0) continue;
                if (dx && dy && (at(x + dx, y) < 0 || at(x, y + dy) < 0)) continue;  // would clip a wall corner
                if (d < best) {
                    best = d;
                    bx = dx;
                    by = dy;
                }
            }
        if (best == here) return {};
        const Vec2 goal{(static_cast<float>(x + bx) + 0.5f) * TILE, (static_cast<float>(y + by) + 0.5f) * TILE};
        return (goal - pos).normalized();
    }

private:
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x); }
    int cols_ = 0, rows_ = 0, tx_ = -1, ty_ = -1;
    std::vector<uint8_t> solid_;
    std::vector<int> dist_;
};

}  // namespace arena
