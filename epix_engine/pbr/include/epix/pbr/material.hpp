#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets/id.hpp>
#include <epix/common.hpp>
#include <epix/ecs/core/tick.hpp>
#include <epix/pbr/material_bindings.hpp>
#include <epix/render/sync_world.hpp>
#include <optional>
#include <unordered_map>
#endif

namespace epix::pbr {

/** @brief The material a single mesh instance uses (Bevy
 *  `RenderMaterialInstance`, `material.rs:598-607`).
 *
 *  Untyped on purpose: the render world's instance records are not generic over
 *  the material type, so one map holds every mesh's material whatever it is. */
EPIX_EXPORT struct RenderMaterialInstance {
    /** @brief The material asset. */
    assets::UntypedAssetId asset_id;
    /** @brief The change tick at which this record was last modified, compared
     *  against `RenderMaterialInstances::current_change_tick` when sweeping. */
    ecs::Tick last_change_tick;
};

/** @brief The material a mesh with no material is treated as having (Bevy
 *  `DUMMY_MESH_MATERIAL`, `material.rs:429-430`).
 *
 *  Bevy's constant is typed `AssetId<StandardMaterial>` and only converted to an
 *  untyped id where it is used; `StandardMaterial` is not ported, so the sentinel
 *  is untyped here and stands for "no material at all". Meshes almost always have
 *  materials - the exceptions are custom pipelines that draw without one - so the
 *  sentinel is what keeps those from being mistaken for a real asset. */
EPIX_EXPORT inline const assets::UntypedAssetId DUMMY_MESH_MATERIAL = assets::UntypedAssetId::invalid();

/** @brief Every mesh's material in the render world (Bevy
 *  `RenderMaterialInstances`, `material.rs:571-596`).
 *
 *  `mesh_material` is the accessor the GPU instance builder uses: a mesh with no
 *  record falls back to `DUMMY_MESH_MATERIAL` rather than failing, which is how a
 *  mesh drawn by a custom pipeline still gets a binding. */
EPIX_EXPORT struct EPIX_API RenderMaterialInstances {
    /** @brief The material record of each entity that has one. */
    render::sync_world::MainEntityHashMap<RenderMaterialInstance> instances;
    /** @brief A monotonically increasing counter used to sweep `instances` when
     *  entities or their material components are removed. */
    ecs::Tick current_change_tick;

    /** @brief The material id for an entity, or the dummy id when it has none
     *  (Bevy `RenderMaterialInstances::mesh_material`, `material.rs:590-595`). */
    [[nodiscard]] assets::UntypedAssetId mesh_material(render::sync_world::MainEntity entity) const;
};

/** @brief Which bind group slot each material asset was bound at (Bevy
 *  `RenderMaterialBindings`, `material.rs:1602`).
 *
 *  Bevy's is a newtype over the map so that it is a distinct resource; the map is
 *  the whole of it either way. The builder reads it to turn a mesh's material into
 *  the slot its bind group was written at. */
EPIX_EXPORT struct EPIX_API RenderMaterialBindings {
    std::unordered_map<assets::UntypedAssetId, MaterialBindingId> bindings;

    /** @brief The binding id a material asset was bound at, if it was prepared
     *  (Bevy's `RenderMaterialBindings::get`). */
    [[nodiscard]] std::optional<MaterialBindingId> get(const assets::UntypedAssetId& asset_id) const;
};

}  // namespace epix::pbr
