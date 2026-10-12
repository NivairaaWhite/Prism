#pragma once
// Prism ECS
//  * Entities are generational handles: a stale handle to a recycled slot is detected.
//  * Components live in sparse-set pools: dense contiguous arrays, O(1) add/remove/lookup.
//  * world.view<A, B>().each(...) iterates the smallest pool and checks the rest.
//  * destroy() during iteration is deferred until the outermost iteration ends.
//    add<T>() during iteration is allowed, but references to T obtained earlier may
//    be invalidated by it. remove<T>() during iteration is not allowed (use destroy).
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace prism {

struct Entity {
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0;

    constexpr bool valid() const { return index != 0xFFFFFFFFu; }
    constexpr uint64_t pack() const { return (static_cast<uint64_t>(generation) << 32) | index; }
    constexpr bool operator==(const Entity&) const = default;
};
inline constexpr Entity NULL_ENTITY{};

namespace detail {
inline uint32_t next_type_id() {
    static uint32_t n = 0;
    return n++;
}
template <class T>
uint32_t type_id() {
    static const uint32_t id = next_type_id();
    return id;
}
}  // namespace detail

class IPool {
public:
    virtual ~IPool() = default;
    virtual void remove(uint32_t index) = 0;
    virtual bool contains(uint32_t index) const = 0;
    virtual size_t size() const = 0;
    virtual uint32_t entity_at(size_t dense) const = 0;
};

template <class T>
class Pool final : public IPool {
public:
    static constexpr uint32_t NONE = 0xFFFFFFFFu;

    bool contains(uint32_t i) const override { return i < sparse_.size() && sparse_[i] != NONE; }
    T* find(uint32_t i) { return contains(i) ? &data_[sparse_[i]] : nullptr; }
    const T* find(uint32_t i) const { return contains(i) ? &data_[sparse_[i]] : nullptr; }

    T& emplace(uint32_t i, T&& value) {
        if (T* existing = find(i)) {
            *existing = std::move(value);
            return *existing;
        }
        if (i >= sparse_.size()) sparse_.resize(static_cast<size_t>(i) + 1, NONE);
        sparse_[i] = static_cast<uint32_t>(data_.size());
        entities_.push_back(i);
        data_.push_back(std::move(value));
        return data_.back();
    }

    void remove(uint32_t i) override {
        if (!contains(i)) return;
        uint32_t d = sparse_[i];
        uint32_t last = static_cast<uint32_t>(data_.size() - 1);
        if (d != last) {
            data_[d] = std::move(data_[last]);
            entities_[d] = entities_[last];
            sparse_[entities_[d]] = d;
        }
        data_.pop_back();
        entities_.pop_back();
        sparse_[i] = NONE;
    }

    size_t size() const override { return data_.size(); }
    uint32_t entity_at(size_t d) const override { return entities_[d]; }
    T& at(size_t d) { return data_[d]; }

private:
    std::vector<uint32_t> sparse_;    // entity index -> dense index
    std::vector<uint32_t> entities_;  // dense index  -> entity index
    std::vector<T> data_;             // dense component storage
};

struct Name {
    std::string value;
};

template <class... Ts>
class View;

class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Entity create() {
        uint32_t idx;
        if (!free_list_.empty()) {
            idx = free_list_.back();
            free_list_.pop_back();
        } else {
            idx = static_cast<uint32_t>(generation_.size());
            generation_.push_back(0);
            alive_.push_back(0);
        }
        alive_[idx] = 1;
        ++alive_count_;
        return Entity{idx, generation_[idx]};
    }

    // Destroys the entity and all its components. If called while iterating a
    // view, destruction is deferred until the outermost iteration finishes.
    void destroy(Entity e) {
        if (!alive(e)) return;
        if (iterating_ > 0) {
            pending_destroy_.push_back(e);
            return;
        }
        destroy_now(e);
    }

    bool alive(Entity e) const {
        return e.index < generation_.size() && alive_[e.index] && generation_[e.index] == e.generation;
    }

    size_t size() const { return alive_count_; }

    void clear() {
        for (uint32_t i = 0; i < generation_.size(); ++i)
            if (alive_[i]) destroy_now(Entity{i, generation_[i]});
        pending_destroy_.clear();
    }

    template <class T>
    T& add(Entity e, T component) {
        assert(alive(e) && "add() on a dead entity");
        return pool<T>().emplace(e.index, std::move(component));
    }

    template <class T, class... Args>
    T& emplace(Entity e, Args&&... args) {
        return add<T>(e, T{std::forward<Args>(args)...});
    }

    template <class T>
    T* get(Entity e) {
        if (!alive(e)) return nullptr;
        Pool<T>* p = find_pool<T>();
        return p ? p->find(e.index) : nullptr;
    }
    template <class T>
    const T* get(Entity e) const {
        if (!alive(e)) return nullptr;
        const Pool<T>* p = find_pool<T>();
        return p ? p->find(e.index) : nullptr;
    }

    template <class T>
    bool has(Entity e) const {
        return get<T>(e) != nullptr;
    }

    template <class T>
    void remove(Entity e) {
        assert(iterating_ == 0 && "remove<T>() during iteration; use destroy() instead");
        if (!alive(e)) return;
        if (Pool<T>* p = find_pool<T>()) p->remove(e.index);
    }

    template <class... Ts>
    View<Ts...> view() {
        return View<Ts...>(this);
    }

    // Convenience: world.each<A, B>(fn)  ==  world.view<A, B>().each(fn)
    template <class... Ts, class Fn>
    void each(Fn&& fn) {
        view<Ts...>().each(std::forward<Fn>(fn));
    }

    // Visit every live entity.
    template <class Fn>
    void each_entity(Fn&& fn) {
        IterGuard guard(this);
        const uint32_t n = static_cast<uint32_t>(generation_.size());
        for (uint32_t i = 0; i < n; ++i)
            if (alive_[i]) fn(Entity{i, generation_[i]});
    }

    void set_name(Entity e, const std::string& name) { add<Name>(e, Name{name}); }
    const std::string& name(Entity e) const {
        static const std::string empty;
        const Name* n = get<Name>(e);
        return n ? n->value : empty;
    }
    Entity find_by_name(const std::string& name) {
        Pool<Name>* p = find_pool<Name>();
        if (!p) return NULL_ENTITY;
        for (size_t d = 0; d < p->size(); ++d)
            if (p->at(d).value == name) return Entity{p->entity_at(d), generation_[p->entity_at(d)]};
        return NULL_ENTITY;
    }

    // Destroy anything queued while iterating. Called automatically; safe to call manually.
    void flush_destroyed() {
        if (iterating_ > 0 || pending_destroy_.empty()) return;
        std::vector<Entity> q;
        q.swap(pending_destroy_);
        for (Entity e : q) destroy_now(e);
    }

private:
    template <class... Ts>
    friend class View;

    struct IterGuard {
        World* w;
        explicit IterGuard(World* world) : w(world) { ++w->iterating_; }
        ~IterGuard() {
            if (--w->iterating_ == 0) w->flush_destroyed();
        }
    };

    void destroy_now(Entity e) {
        if (!alive(e)) return;
        for (auto& p : pools_)
            if (p) p->remove(e.index);
        alive_[e.index] = 0;
        ++generation_[e.index];
        free_list_.push_back(e.index);
        --alive_count_;
    }

    template <class T>
    Pool<T>* find_pool() const {
        uint32_t id = detail::type_id<T>();
        return id < pools_.size() ? static_cast<Pool<T>*>(pools_[id].get()) : nullptr;
    }
    template <class T>
    Pool<T>& pool() {
        uint32_t id = detail::type_id<T>();
        if (id >= pools_.size()) pools_.resize(static_cast<size_t>(id) + 1);
        if (!pools_[id]) pools_[id] = std::make_unique<Pool<T>>();
        return *static_cast<Pool<T>*>(pools_[id].get());
    }

    std::vector<uint32_t> generation_;
    std::vector<uint8_t> alive_;
    std::vector<uint32_t> free_list_;
    std::vector<std::unique_ptr<IPool>> pools_;
    std::vector<Entity> pending_destroy_;
    size_t alive_count_ = 0;
    int iterating_ = 0;
};

template <class... Ts>
class View {
public:
    // fn(Entity, Ts&...)
    template <class Fn>
    void each(Fn&& fn) {
        std::tuple<Pool<Ts>*...> pools{world_->template find_pool<Ts>()...};
        bool missing = std::apply([](auto*... p) { return ((p == nullptr) || ...); }, pools);
        if (missing) return;

        IPool* driver = nullptr;
        size_t best = static_cast<size_t>(-1);
        auto consider = [&](IPool* p) {
            if (p->size() < best) {
                best = p->size();
                driver = p;
            }
        };
        std::apply([&](auto*... p) { (consider(p), ...); }, pools);

        World::IterGuard guard(world_);
        const size_t n = driver->size();  // components added meanwhile are not visited
        for (size_t d = 0; d < n && d < driver->size(); ++d) {
            uint32_t idx = driver->entity_at(d);
            std::apply(
                [&](auto*... p) {
                    if ((p->contains(idx) && ...)) fn(Entity{idx, world_->generation_[idx]}, *p->find(idx)...);
                },
                pools);
        }
    }

    size_t count() {
        size_t c = 0;
        each([&](Entity, Ts&...) { ++c; });
        return c;
    }

private:
    friend class World;
    explicit View(World* w) : world_(w) {}
    World* world_;
};

}  // namespace prism

template <>
struct std::hash<prism::Entity> {
    size_t operator()(const prism::Entity& e) const noexcept { return std::hash<uint64_t>{}(e.pack()); }
};
