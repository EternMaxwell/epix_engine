#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/meta.hpp>
#include <vector>
#endif

#include <epix/common.hpp>
#include <epix/ecs/component/hooks.hpp>
#include <epix/ecs/component/required_component.hpp>
#include <epix/ecs/storage/storage_type.hpp>
#include <epix/ecs/type_id.hpp>

namespace epix::ecs {

EPIX_EXPORT struct Components;
EPIX_EXPORT struct ComponentsRegistrator;

namespace internal {
struct ComponentInfo {
    ComponentInfo(TypeId id, ::epix::meta::type_index index, StorageType storage_type)
        : _id(id), _index(index), _storage_type(storage_type) {}

    TypeId type_id() const noexcept { return _id; }
    ::epix::meta::type_index type_index() const noexcept { return _index; }
    StorageType storage_type() const noexcept { return _storage_type; }
    const ComponentHooks& hooks() const noexcept { return _hooks; }
    ComponentHooks& hooks_mut() noexcept { return _hooks; }
    const RequiredComponents& required_components() const noexcept { return _required_components; }
    RequiredComponents& required_components_mut() noexcept { return _required_components; }
    const std::vector<TypeId>& required_by() const noexcept { return _required_by; }
    std::vector<TypeId>& required_by_mut() noexcept { return _required_by; }

    template <typename T>
    void update_hooks() noexcept {
        _hooks.update_from_component<T>();
    }
    friend struct epix::ecs::Components;
    friend struct epix::ecs::ComponentsRegistrator;

   private:
    TypeId _id;
    ::epix::meta::type_index _index;
    StorageType _storage_type;
    ComponentHooks _hooks;
    RequiredComponents _required_components;
    std::vector<TypeId> _required_by;
};
}  // namespace internal
}  // namespace epix::ecs
