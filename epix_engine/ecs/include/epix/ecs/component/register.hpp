#pragma once

#ifndef EPIX_CXX_MODULE
#include <cassert>
#include <cstddef>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <format>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/component/component.hpp>
#include <epix/ecs/component/hooks.hpp>
#include <epix/ecs/core/type_id.hpp>
#include <epix/ecs/storage/storage_type.hpp>

namespace epix::ecs {

EPIX_EXPORT struct Components;
EPIX_EXPORT struct ComponentIds;
EPIX_EXPORT struct ComponentsRegistrator;

namespace internal {
bool is_resource_component(const Components& components, TypeId component_id);

template <typename T>
struct ResourceComponentRegistration {
    static void register_required_components(TypeId component_id, RequiredComponentsRegistrator& registrator);
};
}  // namespace internal

EPIX_EXPORT struct ComponentsQueuedRegistrator {
   public:
    ComponentsQueuedRegistrator(const Components& components, const ComponentIds& ids)
        : m_components(&components), m_ids(&ids) {}

    template <typename T>
    TypeId queue_register_component() const;
    template <typename T>
        requires std::movable<T>
    TypeId queue_register_resource() const;

   private:
    TypeId register_arbitrary_component(
        meta::type_index type_index,
        StorageType storage_type,
        void (*func)(ComponentsRegistrator&, TypeId, meta::type_index, StorageType)) const;

    const Components* m_components;
    const ComponentIds* m_ids;
};

EPIX_EXPORT struct ComponentsRegistrator {
    ComponentsRegistrator(Components& components, ComponentIds& ids) : m_components(&components), m_ids(&ids) {}
    ComponentsQueuedRegistrator as_queued() const { return ComponentsQueuedRegistrator(*m_components, *m_ids); }

    void apply_queued_registrations();

    template <typename T>
    TypeId register_component() {
        return register_component_checked(meta::type_id<T>{}, storage_type_of<T>(),
                                          Component<T>::register_required_components,
                                          &ComponentHooks::update_from_component<T>);
    }

    /** Register a resource component with IsResource as a required component. */
    template <typename T>
        requires std::movable<T>
    TypeId register_resource() {
        auto id = register_component_checked(meta::type_id<T>{}, storage_type_of<T>(),
                                             internal::ResourceComponentRegistration<T>::register_required_components,
                                             &ComponentHooks::update_from_component<T>);
        if (!internal::is_resource_component(*m_components, id)) {
            throw std::logic_error(
                std::format("Cannot register component {} as a resource because it was already registered as a "
                            "normal component.",
                            meta::type_id<T>().name()));
        }
        return id;
    }

    operator Components&() { return *m_components; }
    operator const Components&() const { return *m_components; }

    friend struct ComponentsQueuedRegistrator;

   private:
    TypeId register_component_checked(meta::type_index type_index,
                                      StorageType storage_type,
                                      void (*register_required_components)(TypeId, RequiredComponentsRegistrator&),
                                      ComponentHooks& (*update_from_component)(ComponentHooks&));
    void register_component_unchecked(meta::type_index type_index,
                                      TypeId id,
                                      StorageType storage_type,
                                      void (*register_required_components)(TypeId, RequiredComponentsRegistrator&),
                                      ComponentHooks& (*update_from_component)(ComponentHooks&));

    Components* m_components;
    ComponentIds* m_ids;
    std::vector<TypeId> m_recurse_stack;
};

template <typename T>
TypeId ComponentsQueuedRegistrator::queue_register_component() const {
    if (auto id = m_components->get_id<T>()) return *id;
    return register_arbitrary_component(
        meta::type_id<T>{}, storage_type_of<T>(),
        [](ComponentsRegistrator& registrator, TypeId id, meta::type_index type_index, StorageType storage_type) {
            registrator.register_component_unchecked(type_index, id, storage_type,
                                                     Component<T>::register_required_components,
                                                     &ComponentHooks::update_from_component<T>);
        });
}
template <typename T>
    requires std::movable<T>
TypeId ComponentsQueuedRegistrator::queue_register_resource() const {
    if (auto id = m_components->get_id<T>()) return *id;
    return register_arbitrary_component(
        meta::type_id<T>{}, storage_type_of<T>(),
        [](ComponentsRegistrator& registrator, TypeId id, meta::type_index type_index, StorageType storage_type) {
            registrator.register_component_unchecked(
                type_index, id, storage_type, internal::ResourceComponentRegistration<T>::register_required_components,
                &ComponentHooks::update_from_component<T>);
        });
}
}  // namespace epix::ecs
