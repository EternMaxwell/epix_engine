#pragma once

#include <functional>
#include <unordered_map>

#include "fwd.hpp"

namespace epix::core {
struct HookContext {
    Entity entity;
    size_t component_id;
};
struct ComponentHooks {
    std::function<void(World&, HookContext)> on_add;
    std::function<void(World&, HookContext)> on_insert;
    std::function<void(World&, HookContext)> on_replace;
    std::function<void(World&, HookContext)> on_remove;
    std::function<void(World&, HookContext)> on_despawn;

    template <typename T>
    ComponentHooks& update_from_component() {
        if constexpr (requires(World& world, HookContext ctx) { T::on_add(world, ctx); }) {
            on_add = T::on_add;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_insert(world, ctx); }) {
            on_insert = T::on_insert;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_replace(world, ctx); }) {
            on_replace = T::on_replace;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_remove(world, ctx); }) {
            on_remove = T::on_remove;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_despawn(world, ctx); }) {
            on_despawn = T::on_despawn;
        }
        return *this;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_add(F&& f) {
        if (on_add) {
            on_add = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_insert(F&& f) {
        if (on_insert) {
            on_insert = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_replace(F&& f) {
        if (on_replace) {
            on_replace = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_remove(F&& f) {
        if (on_remove) {
            on_remove = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_despawn(F&& f) {
        if (on_despawn) {
            on_despawn = std::forward<F>(f);
            return true;
        }
        return false;
    }
};
/**
 * @brief A map containing required component type id and their depth in the requirement tree.
 *
 */
using RequiredComponents = std::unordered_map<size_t, size_t>;
struct ComponentInfo {
   private:
    size_t id;
    ComponentHooks hooks;

   public:
};
}  // namespace epix::core