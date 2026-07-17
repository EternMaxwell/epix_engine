#pragma once

#ifndef EPIX_CXX_MODULE
#include <cassert>
#include <cstddef>
#include <epix/common.hpp>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/component/detail/queued_registration.hpp>
#include <epix/ecs/component/detail/registry_state.hpp>
#include <epix/ecs/component/hooks.hpp>
#include <epix/ecs/component/ids.hpp>
#include <epix/ecs/core/type_id.hpp>

namespace epix::ecs {
EPIX_EXPORT struct ComponentsRegistrator;

EPIX_EXPORT struct Components {
    std::size_t num_queued() const;
    std::size_t num_registered() const;
    std::size_t length() const;

    std::optional<std::reference_wrapper<const internal::ComponentInfo>> get_info(TypeId id) const;
    std::optional<meta::type_index> get_index(TypeId id) const;
    template <typename T = void>
    std::optional<TypeId> get_valid_id(meta::type_index index = meta::type_id<T>{}) const {
        auto it = m_type_ids.find(index);
        if (it == m_type_ids.end()) return std::nullopt;
        return it->second;
    }
    template <typename T = void>
    std::optional<TypeId> get_id(meta::type_index index = meta::type_id<T>{}) const {
        return get_valid_id(index).or_else([&] -> std::optional<TypeId> {
            std::shared_lock lock(*m_mutex);
            auto it = m_queued.components.find(index);
            if (m_queued.components.end() == it) return std::nullopt;
            return it->second.id;
        });
    }

    std::optional<std::reference_wrapper<const RequiredComponents>> get_required_components(TypeId id) const;
    std::optional<std::reference_wrapper<RequiredComponents>> get_required_components_mut(TypeId id);
    std::optional<std::reference_wrapper<const std::vector<TypeId>>> get_required_by(TypeId id) const;
    std::optional<std::reference_wrapper<std::vector<TypeId>>> get_required_by_mut(TypeId id);
    bool is_valid(TypeId id) const;

    /**
     * Register a runtime required-component relationship and propagate its metadata
     * to every component that transitively requires `requiree`.
     */
    std::expected<void, RequiredComponentsError> register_required_components(TypeId requiree,
                                                                              TypeId required,
                                                                              RequiredComponentConstructor constructor);

    /** Typed convenience overload for runtime required-component registration. */
    template <typename R, typename F>
    std::expected<void, RequiredComponentsError> register_required_components(TypeId requiree,
                                                                              TypeId required,
                                                                              F&& constructor)
        requires std::invocable<F> && std::same_as<R, std::invoke_result_t<F>>;

    void register_required_by(TypeId requiree, const RequiredComponents& required_components);

    friend struct ComponentsRegistrator;
    friend struct ComponentsQueuedRegistrator;

   private:
    std::optional<std::reference_wrapper<internal::ComponentInfo>> get_info_mut(TypeId id);
    void register_component_inner(TypeId id, meta::type_index type, StorageType storage_type);

    std::vector<std::optional<internal::ComponentInfo>> m_infos;
    std::unordered_map<meta::type_index, TypeId> m_type_ids;
    mutable QueuedComponents m_queued;
    std::unique_ptr<std::shared_mutex> m_mutex = std::make_unique<std::shared_mutex>();
};

}  // namespace epix::ecs
