#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <expected>
#include <format>
#include <functional>
#include <stdexcept>
#include <utility>
#endif

namespace epix::app {
/** @brief Wrapper that redirects a system parameter's data source to the extracted (main) world.
 *  Used in render systems to read data from the main world while running in a sub-world.
 *  @tparam T A type satisfying system_param. Extract<T> accesses T from the
 *  ExtractedWorld resource instead of the current world.
 *  @note Deferred parameters (commands, etc.) are not allowed inside Extract. */
EPIX_EXPORT template <ecs::system_param T>
struct Extract : public T {
   public:
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit Extract(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : T(std::forward<Args>(args)...) {}

    Extract(const Extract&)            = default;
    Extract(Extract&&)                 = default;
    Extract& operator=(const Extract&) = default;
    Extract& operator=(Extract&&)      = default;
};

struct ExtractedWorld {
    std::reference_wrapper<ecs::World> world;
};
}  // namespace epix::app
template <typename T>
struct epix::ecs::SystemParam<epix::app::Extract<T>> : epix::ecs::SystemParam<T> {
    using Base = SystemParam<T>;
    using Item = epix::app::Extract<typename Base::Item>;

    using ExtractedWorld = epix::app::ExtractedWorld;

    struct State {
        typename Base::State source;
        TypeId extracted_world;
        SystemMeta source_meta;
    };

    static constexpr bool readonly = Base::readonly;

    static State init_state(World& world) {
        const TypeId extracted_world = internal::world_registrator(world).template register_resource<ExtractedWorld>();
        auto& source_world           = world.resource_mut<ExtractedWorld>().world.get();
        return State{
            .source          = Base::init_state(source_world),
            .extracted_world = extracted_world,
            .source_meta     = SystemMeta{
                .name     = std::string(meta::type_id<T>().short_name()),
                .last_run = source_world.change_tick().relative_to(Tick::max()),
            },
        };
    }
    static void init_access(const State& state, SystemMeta& meta, FilteredAccessSet& access, const World& world) {
        // Source-world component ids never enter the render scheduler: the
        // ExtractedWorld proxy is the complete cross-world access contract.
        if constexpr (readonly) {
            access.add_unfiltered_component_read(state.extracted_world);
        } else {
            access.add_unfiltered_component_write(state.extracted_world);
        }
    }
    static void new_archetype(State&, const Archetype&, SystemMeta&) noexcept {}
    static void apply(State& state, const SystemMeta& meta, World& world) noexcept {}
    static void queue(State& state, const SystemMeta& meta, DeferredWorld deferred_world) noexcept {}
    static std::expected<void, ValidateParamError> validate_param(State& state, const SystemMeta& meta, World& world) {
        return Base::validate_param(state.source, state.source_meta, world.resource_mut<ExtractedWorld>().world);
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        auto& extracted_world = world.resource_mut<ExtractedWorld>().world.get();
        const Tick source_tick = extracted_world.increment_change_tick();
        auto item = Base::get_param(state.source, state.source_meta, extracted_world, source_tick);
        state.source_meta.last_run = source_tick;
        return Item(std::move(item));
    }
};
static_assert(epix::ecs::system_param<epix::app::Extract<epix::ecs::ResMut<int>>>);
