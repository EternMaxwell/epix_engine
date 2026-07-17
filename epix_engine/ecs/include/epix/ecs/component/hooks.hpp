#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstdint>
#include <epix/common.hpp>
#include <type_traits>
#endif

#include <epix/ecs/core/type_id.hpp>
#include <epix/ecs/entity/entity.hpp>

namespace epix::ecs {

EPIX_EXPORT struct World;

EPIX_EXPORT struct HookContext {
    Entity entity;
    TypeId component_id;
};

EPIX_EXPORT struct ComponentHooks {
    using HookFunc = void (*)(World&, HookContext);

    HookFunc on_add     = nullptr;
    HookFunc on_insert  = nullptr;
    HookFunc on_replace = nullptr;
    HookFunc on_remove  = nullptr;
    HookFunc on_despawn = nullptr;

    template <typename T>
    ComponentHooks& update_from_component(this ComponentHooks&) noexcept;

    bool try_on_add(HookFunc func) noexcept {
        if (on_add) {
            on_add = func;
            return true;
        }
        return false;
    }
    bool try_on_insert(HookFunc func) noexcept {
        if (on_insert) {
            on_insert = func;
            return true;
        }
        return false;
    }
    bool try_on_replace(HookFunc func) noexcept {
        if (on_replace) {
            on_replace = func;
            return true;
        }
        return false;
    }
    bool try_on_remove(HookFunc func) noexcept {
        if (on_remove) {
            on_remove = func;
            return true;
        }
        return false;
    }
    bool try_on_despawn(HookFunc func) noexcept {
        if (on_despawn) {
            on_despawn = func;
            return true;
        }
        return false;
    }
};

template <typename T>
inline ComponentHooks& ComponentHooks::update_from_component(this ComponentHooks& self) noexcept {
    if constexpr (requires(World& world, HookContext ctx) { T::on_add(world, ctx); }) self.on_add = T::on_add;
    if constexpr (requires(World& world, HookContext ctx) { T::on_insert(world, ctx); }) self.on_insert = T::on_insert;
    if constexpr (requires(World& world, HookContext ctx) { T::on_replace(world, ctx); })
        self.on_replace = T::on_replace;
    if constexpr (requires(World& world, HookContext ctx) { T::on_remove(world, ctx); }) self.on_remove = T::on_remove;
    if constexpr (requires(World& world, HookContext ctx) { T::on_despawn(world, ctx); })
        self.on_despawn = T::on_despawn;
    return self;
}
}  // namespace epix::ecs
