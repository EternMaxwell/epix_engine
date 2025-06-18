#pragma once

#include <entt/entity/registry.hpp>

#include "epix/common.h"
#include "epix/utils/core.h"

namespace epix::app {
struct Entity {
   private:
    entt::entity id = entt::null;

   public:
    EPIX_API Entity(entt::entity id) noexcept;
    EPIX_API Entity() noexcept;

    EPIX_API operator entt::entity() const noexcept;
    EPIX_API operator bool() const noexcept;
    EPIX_API Entity& operator=(entt::entity id) noexcept;
    EPIX_API bool operator!() const noexcept;
    EPIX_API bool operator==(const Entity& other) const noexcept;
    EPIX_API bool operator!=(const Entity& other) const noexcept;
    EPIX_API bool operator==(const entt::entity& other) const noexcept;
    EPIX_API bool operator!=(const entt::entity& other) const noexcept;
    EPIX_API size_t index() const noexcept;
    EPIX_API size_t hash_code() const noexcept;
};
struct Parent;
struct Children;
}  // namespace epix::app