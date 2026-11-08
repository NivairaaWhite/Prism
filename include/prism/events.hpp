#pragma once
// Minimal type-safe publish/subscribe bus. emit() dispatches immediately and is safe
// against handlers subscribing/unsubscribing while it runs.
//
// Lifetime safety: tokens are handed out in increasing order, so mark() + unsubscribe_since()
// remove "everything subscribed after this point" in one call. The Engine uses that to drop a
// scene's handlers when the scene exits, so a handler can never outlive the scene it captured.
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

    // Position in the subscription order. Handlers subscribed afterwards have larger tokens.
    Token mark() const { return next_token_; }

    // Remove every handler (of any event type) subscribed after `m = mark()` was taken.
    // Unsubscribing a token that is already gone is always a harmless no-op.
    void unsubscribe_since(Token m) {
        for (auto& s : slots_)
            if (s) s->purge_after(m);
    }

    size_t handler_count() const {
        size_t n = 0;
        for (const auto& s : slots_)
            if (s) n += s->count();
        return n;
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
        virtual void purge_after(Token m) = 0;
        virtual size_t count() const = 0;
    };
    template <class E>
    struct Slot : ISlot {
        std::vector<std::pair<Token, std::function<void(const E&)>>> handlers;
        void purge_after(Token m) override {
            std::erase_if(handlers, [m](const auto& h) { return h.first > m; });
        }
        size_t count() const override { return handlers.size(); }
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
