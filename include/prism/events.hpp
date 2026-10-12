#pragma once
// Minimal type-safe publish/subscribe bus. emit() dispatches immediately and is safe
// against handlers subscribing/unsubscribing while it runs.
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ecs.hpp"

namespace prism {

class EventBus {
public:
    using Token = uint32_t;

    template <class E>
    Token subscribe(std::function<void(const E&)> fn) {
        Slot<E>& s = slot<E>();
        Token t = ++next_token_;
        s.handlers.emplace_back(t, std::move(fn));
        return t;
    }

    template <class E>
    void unsubscribe(Token t) {
        if (Slot<E>* s = find_slot<E>()) {
            auto& h = s->handlers;
            for (auto it = h.begin(); it != h.end(); ++it)
                if (it->first == t) {
                    h.erase(it);
                    return;
                }
        }
    }

    template <class E>
    void emit(const E& event) {
        Slot<E>* s = find_slot<E>();
        if (!s) return;
        auto copy = s->handlers;  // handlers may (un)subscribe during dispatch
        for (auto& h : copy) h.second(event);
    }

    void clear() { slots_.clear(); }

private:
    struct ISlot {
        virtual ~ISlot() = default;
    };
    template <class E>
    struct Slot : ISlot {
        std::vector<std::pair<Token, std::function<void(const E&)>>> handlers;
    };

    template <class E>
    Slot<E>* find_slot() {
        uint32_t id = detail::type_id<E>();
        return id < slots_.size() ? static_cast<Slot<E>*>(slots_[id].get()) : nullptr;
    }
    template <class E>
    Slot<E>& slot() {
        uint32_t id = detail::type_id<E>();
        if (id >= slots_.size()) slots_.resize(static_cast<size_t>(id) + 1);
        if (!slots_[id]) slots_[id] = std::make_unique<Slot<E>>();
        return *static_cast<Slot<E>*>(slots_[id].get());
    }

    std::vector<std::unique_ptr<ISlot>> slots_;
    Token next_token_ = 0;
};

}  // namespace prism
