#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/pbr/light_markers.hpp>
#include <epix/pbr/mesh_pipeline_key.hpp>
#include <epix/render/alpha.hpp>
#include <epix/camera.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/mesh_material.hpp>
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


/** @brief The meshes whose material or visibility changed (Bevy
 *  `Or<(Changed<ViewVisibility>, Changed<MeshMaterial3d<M>>)>`). */
template <typename M>
using ChangedMeshMaterialFilter =
    ecs::Or<ecs::Added<camera::ViewVisibility>, ecs::Modified<camera::ViewVisibility>,
            ecs::Added<MeshMaterial3d<M>>, ecs::Modified<MeshMaterial3d<M>>>;

/** @brief Fills @ref RenderMaterialInstances from the scene's meshes (Bevy
 *  `extract_mesh_materials`, `bevy_pbr/src/material.rs:698-718`).
 *
 *  A **visible** mesh records which material it uses and the tick it changed at;
 *  an invisible one has its record removed here rather than in the sweep, because
 *  becoming invisible is exactly what this query observes. */
template <typename M>
void extract_mesh_materials(ecs::ResMut<RenderMaterialInstances> material_instances,
                            app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&,
                                                                 const MeshMaterial3d<M>&>,
                                                    ChangedMeshMaterialFilter<M>>> changed_meshes_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (auto&& [entity, view_visibility, material] : changed_meshes_query.iter()) {
        const render::sync_world::MainEntity main_entity{entity};
        if (view_visibility.get()) {
            // `insert_or_assign` rather than `operator[]`: the record's untyped asset
            // id has no default, so the map cannot default-construct one.
            material_instances->instances.insert_or_assign(
                main_entity, RenderMaterialInstance{.asset_id = assets::UntypedAssetId{material.id()},
                                                    .last_change_tick = last_change_tick});
        } else {
            material_instances->instances.erase(main_entity);
        }
    }
}

/** @brief The first of two sweep phases: drops the records of meshes whose
 *  `MeshMaterial3d<M>` was removed (Bevy `early_sweep_material_instances`,
 *  `material.rs:743-763`).
 *
 *  A material of one type replaced by a material of another in the same frame is
 *  common, and the removed-component event still fires for the old type - so an
 *  entry updated *this* frame is left alone, which is what the tick comparison
 *  checks. Running once per material type is why the tick is bumped by the second
 *  phase instead. */
template <typename M>
void early_sweep_material_instances(ecs::ResMut<RenderMaterialInstances> material_instances,
                                    app::Extract<ecs::RemovedComponents<MeshMaterial3d<M>>> removed_materials_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (const ecs::Entity entity : removed_materials_query.read()) {
        const render::sync_world::MainEntity main_entity{entity};
        const auto found = material_instances->instances.find(main_entity);
        if (found != material_instances->instances.end() && found->second.last_change_tick != last_change_tick) {
            material_instances->instances.erase(found);
        }
    }
}


/** @brief The second sweep phase: drops the records of meshes whose `Mesh3d` was
 *  removed, then advances the registry's change tick for the next frame (Bevy
 *  `late_sweep_material_instances`, `material.rs:772-800`).
 *
 *  It runs after every `early_sweep_material_instances`, which is what lets the
 *  tick be bumped exactly once per frame. */
EPIX_EXPORT EPIX_API void late_sweep_material_instances(
    ecs::ResMut<RenderMaterialInstances> material_instances,
    app::Extract<ecs::RemovedComponents<mesh::Mesh3d>> removed_meshes_query);


/** @brief A material's own bind-group data type, or `std::monostate` when it has
 *  none (Bevy's `Material` associated default). */
template <typename M, typename = void>
struct MaterialData {
    using type = std::monostate;
};
template <typename M>
struct MaterialData<M, std::void_t<typename M::Data>> {
    using type = typename M::Data;
};

/** @brief The bind group a 3D material is bound at (Bevy `MATERIAL_BIND_GROUP`,
 *  `material.rs:136`). */
inline constexpr std::uint32_t MATERIAL_BIND_GROUP_INDEX = 2;

/** @brief Whether an opaque or mask material goes through the forward or the
 *  deferred renderer (Bevy `OpaqueRendererMethod`, `material.rs:157-163`).
 *
 *  `Auto` defers to the `DefaultOpaqueRendererMethod` resource, which is part of
 *  the material plugin and therefore lands with it. */
EPIX_EXPORT enum class OpaqueRendererMethod {
    Forward,  /**< Draw it in the forward pass. */
    Deferred, /**< Draw it in the deferred pass. */
    Auto,     /**< Follow the `DefaultOpaqueRendererMethod` resource. */
};

/** @brief The specialization key of one 3D material (Bevy `MaterialPipelineKey`,
 *  `material.rs:269`).
 *
 *  Bevy's key also carries the prepared material pipeline; the port's 2D
 *  counterpart drops that parameter and this follows it. */
EPIX_EXPORT template <typename M>
struct MaterialPipelineKey {
    /** @brief The mesh half of the key. */
    MeshPipelineKey mesh_key{};
    /** @brief The material's own bind-group data (Bevy `M::Data`). */
    typename MaterialData<M>::type bind_group_data{};

    bool operator==(const MaterialPipelineKey&) const noexcept = default;
};

/** @brief A 3D material (Bevy `Material`, `bevy_pbr/src/material.rs:138-273`).  A
 *  material type derives from this and overrides only what it needs; the pipeline
 *  calls the derived members through static dispatch.
 *
 *  Bevy's `ShaderRef` (default, handle or path) translates to an optional
 *  source-relative shader path loaded under `embedded://`, as the 2D counterpart
 *  does: `nullopt` means the default mesh shader, which is `ShaderRef::Default`.
 *
 *  The meshlet shaders (`meshlet_mesh_fragment_shader` and its two siblings) are
 *  absent: meshlets are a Bevy subsystem this port does not have. */
EPIX_EXPORT template <typename M>
struct Material {
    /** @brief This material's vertex shader path, or nothing for the default mesh
     *  vertex shader. */
    static std::optional<std::string_view> vertex_shader() { return std::nullopt; }
    /** @brief This material's fragment shader path, or nothing for the default mesh
     *  fragment shader. */
    static std::optional<std::string_view> fragment_shader() { return std::nullopt; }
    /** @brief This material's alpha mode (Bevy `alpha_mode`, which defaults to
     *  `AlphaMode::Opaque`). */
    render::AlphaMode alpha_mode() const { return render::AlphaMode::opaque(); }
    /** @brief Which renderer draws this material (Bevy `opaque_render_method`,
     *  which defaults to `Forward`). */
    OpaqueRendererMethod opaque_render_method() const { return OpaqueRendererMethod::Forward; }
    /** @brief A view-depth bias forcing a specific render order (Bevy
     *  `depth_bias`). */
    float depth_bias() const { return 0.0f; }
    /** @brief Whether this material reads the view transmission texture (Bevy
     *  `reads_view_transmission_texture`). */
    bool reads_view_transmission_texture() const { return false; }
    /** @brief Whether the prepass is enabled for this material. Bevy's default is
     *  `true`, which is easy to get wrong in either direction. */
    static bool enable_prepass() { return true; }
    /** @brief Whether shadows are enabled for this material (Bevy `enable_shadows`,
     *  default `true`). */
    static bool enable_shadows() { return true; }
    /** @brief This material's prepass vertex shader path. */
    static std::optional<std::string_view> prepass_vertex_shader() { return std::nullopt; }
    /** @brief This material's prepass fragment shader path. */
    static std::optional<std::string_view> prepass_fragment_shader() { return std::nullopt; }
    /** @brief This material's deferred vertex shader path. */
    static std::optional<std::string_view> deferred_vertex_shader() { return std::nullopt; }
    /** @brief This material's deferred fragment shader path. */
    static std::optional<std::string_view> deferred_fragment_shader() { return std::nullopt; }
    /** @brief Customize the specialized pipeline (Bevy `Material::specialize`,
     *  `material.rs:265-272`, whose prepared pipeline parameter the port's 2D
     *  counterpart also drops). */
    static void specialize(render::RenderPipelineDescriptor&,
                           const mesh::MeshVertexBufferLayoutRef&,
                           const MaterialPipelineKey<M>&) {}
};


/** @brief The extraction systems that record specialization ticks, which the sweep
 *  then consumes (Bevy `MaterialExtractEntitiesNeedingSpecializationSystems`,
 *  `material.rs:392`). */
EPIX_EXPORT enum class MaterialExtractEntitiesNeedingSpecializationSystems {
    Extract, /**< The per-material pass that records specialization ticks. */
};
/** @brief The extraction systems of one material type, which the sweeps order
 *  around (Bevy `MaterialExtractionSystems`, `material.rs:396`). */
EPIX_EXPORT enum class MaterialExtractionSystems {
    Extract, /**< The per-material extraction pass this ordering set marks. */
};

/** @brief Entities whose material changed and that need re-specialization (Bevy
 *  `EntitiesNeedingSpecialization<M>`, `material.rs:596-610`), the 3D counterpart
 *  of the 2D resource of the same name. */
EPIX_EXPORT template <typename M>
struct EntitiesNeedingSpecialization {
    std::vector<ecs::Entity> entities;
};

/** @brief Ticks saying when an entity's pipeline was last specialized (Bevy
 *  `EntitySpecializationTickPair`, `material.rs:944-952`). Bevy keeps **two**
 *  ticks for a subtle reason: a specialization can be invalidated either by the
 *  system's own tick moving or by the material registry's, and the two are
 *  advanced by different systems. */
EPIX_EXPORT struct EntitySpecializationTickPair {
    /** @brief The standard system tick. */
    ecs::Tick system_tick;
    /** @brief The `RenderMaterialInstances` tick, advanced by
     *  `late_sweep_material_instances`. */
    ecs::Tick material_instances_tick;
};

/** @brief When each entity's pipeline was last specialized (Bevy
 *  `EntitySpecializationTicks`, `material.rs:904-908`). */
EPIX_EXPORT struct EntitySpecializationTicks {
    render::sync_world::MainEntityHashMap<EntitySpecializationTickPair> entities;
};

/** @brief Records, for every entity the main world queued, when its pipeline was
 *  last specialized (Bevy `extract_entities_needs_specialization<M>`,
 *  `material.rs:786-805`).
 *
 *  Both ticks are stored because either can invalidate a specialization: the
 *  system tick moves with the frame, and the registry tick moves when a material
 *  instance is swept. */
EPIX_EXPORT template <typename M>
void extract_entities_needs_specialization(
    app::Extract<ecs::Res<EntitiesNeedingSpecialization<M>>> entities_needing_specialization,
    ecs::ResMut<EntitySpecializationTicks> entity_specialization_ticks,
    ecs::Res<RenderMaterialInstances> render_material_instances,
    ecs::SystemChangeTick ticks) {
    for (const ecs::Entity entity : entities_needing_specialization->entities) {
        entity_specialization_ticks->entities.insert_or_assign(
            render::sync_world::MainEntity{entity},
            EntitySpecializationTickPair{.system_tick = ticks.this_run(),
                                        .material_instances_tick = render_material_instances->current_change_tick});
    }
}

/** @brief Queues the entities whose mesh or material changed so they are
 *  re-specialized (Bevy `check_entities_needing_specialization<M>`,
 *  `bevy_pbr/src/material.rs:969-990`), the 3D counterpart of the 2D system of the
 *  same name. Bevy fills its list from a parallel local; the result is the same
 *  collection. */
EPIX_EXPORT template <typename M>
void check_entities_needing_specialization(
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::Or<ecs::Added<mesh::Mesh3d>, ecs::Modified<mesh::Mesh3d>,
                                   assets::AssetChanged<mesh::Mesh3d>, ecs::Added<MeshMaterial3d<M>>,
                                   ecs::Modified<MeshMaterial3d<M>>, assets::AssetChanged<MeshMaterial3d<M>>>,
                           ecs::With<MeshMaterial3d<M>>>>
        needs_specialization,
    ecs::ResMut<EntitiesNeedingSpecialization<M>> entities_needing_specialization) {
    entities_needing_specialization->entities.clear();
    for (auto&& [entity] : needs_specialization.iter()) {
        entities_needing_specialization->entities.push_back(entity);
    }
}

/** @brief Queues the light entities whose `NotShadowCaster` changed (Bevy
 *  `check_light_entities_needing_specialization<M>`,
 *  `bevy_pbr/src/render/light.rs:1686-1705`).
 *
 *  Bevy defines it beside the lights rather than beside the materials; the port
 *  keeps it here, next to the resource it fills. Only a removed `NotShadowCaster`
 *  whose entity still exists needs re-specialization - a removal whose entity is
 *  gone has nothing left to specialize. */
EPIX_EXPORT template <typename M>
void check_light_entities_needing_specialization(
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::With<MeshMaterial3d<M>>, ecs::Modified<NotShadowCaster>>>
        needs_specialization,
    ecs::Query<ecs::Item<ecs::Entity>, ecs::Filter<ecs::With<MeshMaterial3d<M>>>> mesh_materials,
    ecs::ResMut<EntitiesNeedingSpecialization<M>> entities_needing_specialization,
    ecs::RemovedComponents<NotShadowCaster> removed_components) {
    for (auto&& [entity] : needs_specialization.iter()) {
        entities_needing_specialization->entities.push_back(entity);
    }
    for (const ecs::Entity removed : removed_components.read()) {
        if (mesh_materials.get(removed).has_value()) {
            entities_needing_specialization->entities.push_back(removed);
        }
    }
}


/** @brief Registers one 3D material type (Bevy `MaterialPlugin<M>`,
 *  `bevy_pbr/src/material.rs:339-404`).
 *
 *  What is here is the half the port has: the material asset is registered, and
 *  the registry plus the three extraction and sweep systems are wired in Bevy's
 *  order - extraction inside `MaterialExtractionSystems`, the early sweep after it
 *  and before the late sweep, which is what lets a material of one type be replaced
 *  by another in the same frame.
 *
 *  What is **not** here, and lands with its prerequisites: `EntitiesNeedingSpecialization`
 *  and its three systems, `add_material_bind_group_allocator`, the erased
 *  render-asset plugin, the PostUpdate mesh-change systems, and the whole
 *  `finish` half (`PreparedMaterial`, `MaterialPipeline`, the specialized-pipeline
 *  caches). The registry resource is inserted here rather than by Bevy's
 *  `MaterialsPlugin`, which is global and unported. */
EPIX_EXPORT template <typename M>
    requires std::derived_from<M, Material<M>>
struct MaterialPlugin {
    void attach(app::App& app) {
        app_register_asset<M>(app);

        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) return;

        // `init_resource` rather than `insert_resource`: one registry serves every
        // material type, so a second material plugin must not wipe the first's
        // contents.
        render_app->get().world_mut().init_resource<RenderMaterialInstances>();

        // The set must be in the schedule before anything can be ordered against it:
        // the port warns and *ignores* a hierarchy or dependency whose set is absent,
        // which is how a declared ordering can end up enforcing nothing. `SetConfig`'s
        // label is private, so the documented `sets(...)` helper is the way in.
        render_app->get().configure_sets(app::ScheduleInfo(render::ExtractSchedule),
                                         ecs::sets(MaterialExtractionSystems::Extract));
        render_app->get().add_systems(
            app::ScheduleInfo(render::ExtractSchedule),
            ecs::into(extract_mesh_materials<M>).in_set(MaterialExtractionSystems::Extract).set_name(
                "extract mesh materials"));
        render_app->get().add_systems(
            app::ScheduleInfo(render::ExtractSchedule),
            // A system function is a valid ordering label here - `add_systems` takes
            // it as `SystemSetLabel` - so Bevy's `.before(late_sweep_material_instances)`
            // is expressible after all, and the early sweep really does run before the
            // late one that advances the tick.
            ecs::into(early_sweep_material_instances<M>)
                .after(MaterialExtractionSystems::Extract)
                .before(late_sweep_material_instances)
                .set_name("early sweep material instances"));
        render_app->get().add_systems(app::ScheduleInfo(render::ExtractSchedule),
                                      ecs::into(late_sweep_material_instances).set_name("late sweep material instances"));

        if (render_app) {
            // Bevy orders this set's systems after the mesh extraction and before
            // the sweep; the set has to be registered for the ordering to apply.
            render_app->get().configure_sets(app::ScheduleInfo(render::ExtractSchedule),
                                             ecs::sets(MaterialExtractEntitiesNeedingSpecializationSystems::Extract));
            render_app->get().world_mut().init_resource<EntitySpecializationTicks>();
            render_app->get().add_systems(
                app::ScheduleInfo(render::ExtractSchedule),
                ecs::into(extract_entities_needs_specialization<M>)
                    .in_set(MaterialExtractEntitiesNeedingSpecializationSystems::Extract)
                    .set_name("extract entities needing material specialization"));
        }
        app.add_systems(app::ScheduleInfo(app::PostUpdate),
                        ecs::into(check_entities_needing_specialization<M>)
                            .after(assets::AssetSystems::WriteEvents)
                            .set_name("check entities needing material specialization"));
        // Bevy adds the light half only when the material enables shadows.
        if (M::enable_shadows()) {
            app.add_systems(app::ScheduleInfo(app::PostUpdate),
                            ecs::into(check_light_entities_needing_specialization<M>)
                                .after(assets::AssetSystems::WriteEvents)
                                .set_name("check light entities needing material specialization"));
        }
    }


};


/** @brief Marks a mesh as changed when the material it renders with changed (Bevy
 *  `mark_meshes_as_changed_if_their_materials_changed<M>`, `material.rs:679-691`).
 *
 *  A material swap has to reach `Mesh3d`'s change tick, because that is what the
 *  extraction queries watch; `AssetChanged` is what catches the common case where
 *  the component is untouched and only the asset's contents changed. Both terms are
 *  needed - the component for a swap, the asset for an edit. */
EPIX_EXPORT template <typename M>
void mark_meshes_as_changed_if_their_materials_changed(
    ecs::Query<ecs::Item<ecs::Entity, ecs::Mut<mesh::Mesh3d>>,
               ecs::Filter<ecs::Or<ecs::Added<MeshMaterial3d<M>>, ecs::Modified<MeshMaterial3d<M>>,
                                                 assets::AssetChanged<MeshMaterial3d<M>>>>> changed_meshes) {
    for (auto&& [entity, mesh] : changed_meshes.iter()) {
        (void)entity;
        // Bevy writes `mesh.set_changed()`. This port's `Mut` marks modified on
        // mutable access and has no `set_changed`, so `ptr_mut()` is the accessor
        // that marks without a semantic write - the same effect.
        (void)mesh.ptr_mut();
    }
}


}  // namespace epix::pbr
