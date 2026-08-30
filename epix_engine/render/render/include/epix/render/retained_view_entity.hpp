#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <functional>
#include <optional>
#endif

#include <epix/render/sync_world.hpp>

namespace epix::render::view {
/** @brief Stable cross-frame identifier for a render-world view (Bevy
 * `RetainedViewEntity`). Kept separate from `view.hpp` so render-phase
 * resources can use it without a circular view/phase include. */
EPIX_EXPORT struct RetainedViewEntity {
    sync_world::MainEntity main_entity;
    sync_world::MainEntity auxiliary_entity;
    std::uint32_t subview_index = 0;

    static sync_world::MainEntity placeholder_auxiliary_entity() noexcept {
        return sync_world::MainEntity{ecs::Entity::PLACEHOLDER};
    }

    RetainedViewEntity(sync_world::MainEntity main_entity,
                       std::optional<sync_world::MainEntity> auxiliary_entity = std::nullopt,
                       std::uint32_t subview_index                            = 0)
        : main_entity(main_entity),
          auxiliary_entity(auxiliary_entity.value_or(placeholder_auxiliary_entity())),
          subview_index(subview_index) {}

    bool operator==(const RetainedViewEntity&) const = default;

    static RetainedViewEntity create(sync_world::MainEntity main_entity,
                                     std::optional<sync_world::MainEntity> auxiliary_entity,
                                     std::uint32_t subview_index) {
        return RetainedViewEntity{main_entity, auxiliary_entity, subview_index};
    }
};
}  // namespace epix::render::view

template <>
struct std::hash<::epix::render::view::RetainedViewEntity> {
    std::size_t operator()(const ::epix::render::view::RetainedViewEntity& r) const noexcept {
        std::size_t h = std::hash<::epix::render::sync_world::MainEntity>{}(r.main_entity);
        h ^= std::hash<::epix::render::sync_world::MainEntity>{}(r.auxiliary_entity) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(r.subview_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
