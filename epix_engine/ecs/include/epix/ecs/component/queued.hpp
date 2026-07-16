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
#include <type_traits>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/component/ids.hpp>
#include <epix/ecs/type_id.hpp>
#include <epix/ecs/storage/storage_type.hpp>

namespace epix::ecs {
EPIX_EXPORT struct Components;
EPIX_EXPORT struct ComponentsRegistrator;
EPIX_EXPORT struct ComponentsQueuedRegistrator;

EPIX_EXPORT struct QueuedRegistration {
    void (*registrator)(ComponentsRegistrator&, TypeId, meta::type_index, StorageType);
    TypeId id;
    meta::type_index type_index;
    StorageType storage_type;

    QueuedRegistration(TypeId id,
                       meta::type_index type_index,
                       StorageType storage_type,
                       void (*registrator)(ComponentsRegistrator&, TypeId, meta::type_index, StorageType))
        : id(id), type_index(type_index), storage_type(storage_type), registrator(registrator) {}

    TypeId register_component(ComponentsRegistrator& reg) {
        registrator(reg, id, type_index, storage_type);
        return id;
    }
};

EPIX_EXPORT struct QueuedComponents {
    friend struct Components;
    friend struct ComponentsRegistrator;
    friend struct ComponentsQueuedRegistrator;

   private:
    std::unordered_map<meta::type_index, QueuedRegistration> components;
};
}  // namespace epix::ecs