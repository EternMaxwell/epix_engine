#pragma once

#include <epix/core/app.hpp>
#include <epix/core/archetype.hpp>
#include <epix/core/bundle.hpp>
#include <epix/core/component.hpp>
#include <epix/core/entities.hpp>
#include <epix/core/hierarchy.hpp>
#include <epix/core/label.hpp>
#include <epix/core/labels.hpp>
#include <epix/core/query.hpp>
#include <epix/core/schedule.hpp>
#include <epix/core/storage.hpp>
#include <epix/core/system.hpp>
#include <epix/core/tick.hpp>
#include <epix/core/ticks.hpp>
#include <epix/core/type_registry.hpp>
#include <epix/core/utils.hpp>
#include <epix/core/world.hpp>
#include <epix/meta.hpp>
#include <epix/traits.hpp>
#include <epix/utils.hpp>

namespace epix::core {
WorldId world_id(const World& w) noexcept;
const TypeRegistry& world_type_registry(const World& w) noexcept;
std::shared_ptr<TypeRegistry> world_type_registry_ptr(const World& w) noexcept;
const Components& world_components(const World& w) noexcept;
Components& world_components_mut(World& w) noexcept;
const Entities& world_entities(const World& w) noexcept;
Entities& world_entities_mut(World& w) noexcept;
const Storage& world_storage(const World& w) noexcept;
Storage& world_storage_mut(World& w) noexcept;
const Archetypes& world_archetypes(const World& w) noexcept;
Archetypes& world_archetypes_mut(World& w) noexcept;
}  // namespace epix::core
