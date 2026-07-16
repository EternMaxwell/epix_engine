#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <functional>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <vector>
#endif

#include <epix/ecs/type_id.hpp>

namespace epix::ecs {

struct Table;
struct SparseSets;
struct Tick;
struct TableRow;
struct Entity;

EPIX_EXPORT struct ComponentsRegistrator;
namespace internal {
struct RequiredComponents;
}

EPIX_EXPORT struct RequiredComponentsRegistrator {
    RequiredComponentsRegistrator(ComponentsRegistrator&, internal::RequiredComponents&) {}
};

namespace internal {

using RequiredComponentConstructor = std::shared_ptr<std::function<void(Table&, SparseSets&, Tick, TableRow, Entity)>>;

struct RequiredComponent {
    RequiredComponentConstructor constructor;
    std::uint16_t inheritance_depth = 0;
};

struct RequiredComponents {
    std::unordered_map<TypeId, RequiredComponent> components;

    void register_dynamic(TypeId type_id, std::uint32_t inheritance_depth, RequiredComponentConstructor constructor) {
        auto it = components.find(type_id);
        if (it == components.end() || inheritance_depth < it->second.inheritance_depth)
            components[type_id] = RequiredComponent{.constructor       = std::move(constructor),
                                                    .inheritance_depth = static_cast<std::uint16_t>(inheritance_depth)};
    }

    template <typename C, typename F>
    void register_id(TypeId type_id, std::uint32_t inheritance_depth, F&& constructor)
        requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>;
    template <typename R>
    void remove_range(R&& range)
        requires std::ranges::view<R> && std::same_as<std::ranges::range_value_t<R>, TypeId>;
    void merge(const RequiredComponents& other);
};

}  // namespace internal
}  // namespace epix::ecs
