#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/transform.hpp>
#include <cstdint>
#include <expected>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>
#include <epix/common.hpp>
#endif

#include <epix/sprite_render/mesh2d_pipeline.hpp>

namespace epix::sprite_render {
/** @brief Resource controlling global 2D wireframing (Bevy `Wireframe2dConfig`,
 *  `bevy_sprite_render/src/mesh2d/wireframe2d.rs:436-446`).
 *
 *  Epix has no `bevy_color`, so colours are linear `glm::vec4` here, exactly as
 *  `ColorMaterial` carries them. */
EPIX_EXPORT struct Wireframe2dConfig {
    /** @brief Whether to show wireframes for all meshes (Bevy `global`). */
    bool global = false;
    /** @brief The colour used for globally enabled wireframes (Bevy
     *  `default_color`, `Color::WHITE`). */
    glm::vec4 default_color{1.0f, 1.0f, 1.0f, 1.0f};
};

/** @brief Enables wireframe rendering for the entity it is attached to,
 *  ignoring `Wireframe2dConfig::global` (Bevy `Wireframe2d`, `:157-163`). */
EPIX_EXPORT struct Wireframe2d {};

/** @brief Disables wireframe rendering for the entity it is attached to,
 *  ignoring `Wireframe2dConfig::global` (Bevy `NoWireframe2d`, `:428-434`). */
EPIX_EXPORT struct NoWireframe2d {};

/** @brief Per-entity wireframe colour, overriding
 *  `Wireframe2dConfig::default_color` (Bevy `Wireframe2dColor`, `:411-421`). */
EPIX_EXPORT struct Wireframe2dColor {
    /** @brief The wireframe colour. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

/** @brief Extracted wireframe colour (Bevy `ExtractedWireframeColor`,
 *  `:423-426`). Bevy never constructs it either; it is kept so the surface
 *  matches. */
EPIX_EXPORT struct ExtractedWireframeColor {
    /** @brief The extracted colour. */
    glm::vec4 color{0.0f};
};

/** @brief The material a wireframe draws with (Bevy `Wireframe2dMaterial`,
 *  `:448-452`). */
EPIX_EXPORT struct Wireframe2dMaterial {
    /** @brief The wireframe colour, in linear RGBA. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

/** @brief The prepared wireframe material: the 16 bytes the fragment shader's
 *  push constant carries (Bevy `RenderWireframeMaterial`, `:454-456`). */
EPIX_EXPORT struct RenderWireframeMaterial {
    /** @brief The colour handed to `SetWireframe2dPushConstants`. */
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

/** @brief Links an entity to the wireframe material it draws with (Bevy
 *  `Mesh2dWireframe(pub Handle<Wireframe2dMaterial>)`, `:458-468`). */
EPIX_EXPORT struct Mesh2dWireframe {
    /** @brief The material asset. */
    assets::Handle<Wireframe2dMaterial> handle;
};
}  // namespace epix::sprite_render

namespace epix::assets {
/** @brief Bevy's `AsAssetId for Mesh2dWireframe` (`wireframe2d.rs:462-468`):
 *  the component is an asset reference, which `AssetChanged` keys on. */
template <>
struct AsAssetId<epix::sprite_render::Mesh2dWireframe> {
    using Asset = epix::sprite_render::Wireframe2dMaterial;

    static AssetId<Asset> as_asset_id(const epix::sprite_render::Mesh2dWireframe& wireframe) noexcept {
        return wireframe.handle.id();
    }
};
static_assert(AsAssetIdImpl<epix::sprite_render::Mesh2dWireframe>);
}  // namespace epix::assets

namespace epix::render {
/** @brief `RenderAsset<RenderWireframeMaterial>` (Bevy
 *  `wireframe2d.rs:470-484`): Bevy converts the colour to linear here, while
 *  Epix colours are already linear `glm::vec4`, so preparation copies it. */
template <>
struct RenderAsset<epix::sprite_render::RenderWireframeMaterial> {
    using SourceAsset    = epix::sprite_render::Wireframe2dMaterial;
    using ExtractedAsset = epix::sprite_render::Wireframe2dMaterial;
    using Param          = std::tuple<>;

    static std::expected<epix::sprite_render::RenderWireframeMaterial,
                         PrepareAssetError<epix::sprite_render::Wireframe2dMaterial>>
    prepare_asset(epix::sprite_render::Wireframe2dMaterial&& source_asset,
                  assets::AssetId<epix::sprite_render::Wireframe2dMaterial>,
                  Param,
                  const epix::sprite_render::RenderWireframeMaterial*) {
        return epix::sprite_render::RenderWireframeMaterial{.color = source_asset.color};
    }

    /** @brief Bevy's `RenderWireframeMaterial` does not override
     *  `RenderAsset::asset_usage`, so it keeps the default, which includes the
     *  main world: `Mesh2dWireframe` holds the handle there. */
    static assets::RenderAssetUsages asset_usage(const epix::sprite_render::Wireframe2dMaterial&) noexcept {
        return static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                      assets::RenderAssetUsages::RENDER_WORLD);
    }
};
}  // namespace epix::render

namespace epix::sprite_render {

/** @brief Main-entity keyed wireframe materials (Bevy
 *  `RenderWireframeInstances`, `:486-487`). */
EPIX_EXPORT struct RenderWireframeInstances {
    /** @brief The prepared materials, keyed by main-world entity. */
    render::sync_world::MainEntityHashMap<assets::AssetId<Wireframe2dMaterial>> instances;
};

/** @brief Entities whose wireframe pipeline must be re-specialized (Bevy
 *  `WireframeEntitiesNeedingSpecialization`, `:489-493`). */
EPIX_EXPORT struct WireframeEntitiesNeedingSpecialization {
    /** @brief The main-world entities. */
    std::vector<ecs::Entity> entities;
};

/** @brief Last specialization tick per main entity (Bevy
 *  `WireframeEntitySpecializationTicks`, `:495-498`). */
EPIX_EXPORT struct WireframeEntitySpecializationTicks {
    /** @brief Tick per entity. */
    render::sync_world::MainEntityHashMap<ecs::Tick> entities;
};

/** @brief Data that must be identical to batch wireframe items together (Bevy
 *  `Wireframe2dBatchSetKey`, `:240-260`).
 *
 *  Field order is the comparison order: pipeline, material asset, draw function,
 *  then the vertex and index slabs, exactly as Bevy derives `Ord` over them. */
EPIX_EXPORT struct Wireframe2dBatchSetKey {
    /** @brief The cached render pipeline. */
    render::CachedPipelineId pipeline;
    /** @brief The wireframe material asset. */
    assets::UntypedAssetId asset_id;
    /** @brief The draw function. */
    render::phase::DrawFunctionId draw_function;
    /** @brief The mesh's vertex slab. */
    render::mesh::SlabId vertex_slab;
    /** @brief The mesh's index slab, when the mesh is indexed. */
    std::optional<render::mesh::SlabId> index_slab;

    /** @brief Bevy `PhaseItemBatchSetKey::indexed`. */
    bool indexed() const noexcept { return index_slab.has_value(); }
    auto operator<=>(const Wireframe2dBatchSetKey&) const = default;
};

/** @brief Data that must be identical to bin wireframe items together (Bevy
 *  `Wireframe2dBinKey`, `:268-275`): the mesh asset only. */
EPIX_EXPORT struct Wireframe2dBinKey {
    /** @brief The wireframe mesh asset. */
    assets::UntypedAssetId asset_id;
    auto operator<=>(const Wireframe2dBinKey&) const = default;
};

/** @brief The 2D wireframe render phase item (Bevy `Wireframe2dPhaseItem`,
 *  `:165-238`). */
EPIX_EXPORT struct Wireframe2dPhaseItem {
    /** @brief The render entity and its main-world entity. */
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    /** @brief Batch-set key. */
    Wireframe2dBatchSetKey batch_set_key_value;
    /** @brief Bin key. */
    Wireframe2dBinKey bin_key_value;
    /** @brief The instance range this item covers. */
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    /** @brief Dynamic-offset or indirect-parameter index. */
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    /** @brief The draw function comes from the *batch set* key, as Bevy's does
     *  (`:186`). */
    render::phase::DrawFunctionId draw_function() const noexcept { return batch_set_key_value.draw_function; }
    /** @brief The pipeline also comes from the batch set key (`:214`). */
    render::CachedPipelineId cached_pipeline() const noexcept { return batch_set_key_value.pipeline; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }

    /** @brief Bevy `BinnedPhaseItem` associated types. */
    using BinKey      = Wireframe2dBinKey;
    using BatchSetKey = Wireframe2dBatchSetKey;

    /** @brief Bevy `BinnedPhaseItem::new` with the trait's parameter order. */
    static Wireframe2dPhaseItem create(BatchSetKey batch_set_key,
                                       BinKey bin_key,
                                       std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                                       std::pair<std::uint32_t, std::uint32_t> batch_range,
                                       render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key), batch_range,
                extra_index};
    }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Wireframe2dPhaseItem>);
static_assert(render::phase::BinnedPhaseItem<Wireframe2dPhaseItem>);

/** @brief Fragment push constants carrying the wireframe colour (Bevy
 *  `SetWireframe2dPushConstants`, `:277-309`).
 *
 *  Bevy declares `PushConstantRange { FRAGMENT, 0..16 }` and writes the
 *  material's `[f32; 4]`; Epix writes the same 16 bytes to the fragment stage. */
EPIX_EXPORT struct SetWireframe2dPushConstants {};

}  // namespace epix::sprite_render

namespace epix::render::phase {
/** @brief The wireframe push-constant command's trait specialisation (Bevy
 *  `impl<P: PhaseItem> RenderCommand<P> for SetWireframe2dPushConstants`,
 *  `:277-309`). It lives here rather than in `sprite_render` because the trait
 *  it specialises does. */
EPIX_EXPORT template <PhaseItem P>
struct RenderCommand<::epix::sprite_render::SetWireframe2dPushConstants, P> {
    using Param     = std::tuple<epix::ecs::Res<::epix::sprite_render::RenderWireframeInstances>,
                                 epix::ecs::Res<::epix::render::RenderAssets<
                                     ::epix::sprite_render::RenderWireframeMaterial>>>;
    using ViewQuery = epix::ecs::Item<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) noexcept {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   epix::ecs::Item<>,
                                                   std::optional<epix::ecs::Item<>>,
                                                   Param params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        auto&& [wireframe_instances, wireframe_assets] = params;
        const auto material_it = wireframe_instances->instances.find(item.main_entity());
        if (material_it == wireframe_instances->instances.end()) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Failure,
                .message = "No wireframe material found for entity",
            });
        }
        const auto* material = wireframe_assets->try_get(material_it->second);
        if (!material) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Failure,
                .message = "No wireframe material found for entity",
            });
        }
        // wgpu v29 renamed push constants to immediates; the range is the same
        // 16 bytes at offset 0 in the fragment stage.
        encoder.setImmediates(0, &material->color, sizeof(glm::vec4));
        return {};
    }
};
}  // namespace epix::render::phase

namespace epix::sprite_render {

/** @brief The state that holds every view's specialized wireframe pipelines
 *  (Bevy `SpecializedWireframeViewPipelineCache`, `:508-515`): material entity
 *  maps to the tick it was specialized at and the pipeline id. */
EPIX_EXPORT struct SpecializedWireframeViewPipelineCache {
    /** @brief Per-entity (tick, pipeline) pairs. */
    render::sync_world::MainEntityHashMap<std::pair<ecs::Tick, render::CachedPipelineId>> map;
};

/** @brief Per-view specialized wireframe pipelines (Bevy
 *  `SpecializedWireframePipelineCache`, `:500-506`). */
EPIX_EXPORT struct SpecializedWireframePipelineCache {
    /** @brief View to that view's cache. */
    std::unordered_map<render::view::RetainedViewEntity, SpecializedWireframeViewPipelineCache> map;
};

/** @brief The reusable material behind global wireframing (Bevy's private
 *  `GlobalWireframeMaterial`, `:517-521`). */
EPIX_EXPORT struct GlobalWireframeMaterial {
    /** @brief The handle reused while `Wireframe2dConfig::global` is enabled. */
    assets::Handle<Wireframe2dMaterial> handle;
};

/** @brief Bevy `Wireframe2dPipeline` (`:319-334`): the mesh2d pipeline with the
 *  wireframe fragment shader, drawn with `PolygonMode::Line`. */
EPIX_EXPORT struct Wireframe2dPipeline {
    /** @brief The specialization key of this pipeline (Bevy
     *  `SpecializedMeshPipeline::Key`). */
    using Key = Mesh2dPipelineKey;
    /** @brief The mesh2d pipeline this one is a variant of. */
    Mesh2dPipeline mesh_pipeline;
    /** @brief The wireframe fragment shader. */
    assets::Handle<shader::Shader> shader;

    /** @brief Build this pipeline's descriptor (Bevy's `specialize`,
     *  `:336-356`): specialize mesh2d, then push the 16-byte fragment push
     *  constant range, swap in the wireframe fragment shader, set
     *  `PolygonMode::Line` and bias `slope_scale` to 1. */
    std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> specialize(
        const Mesh2dPipelineKey& key, const mesh::MeshVertexBufferLayoutRef& layout) const;
};

/** @brief Bevy `init_wireframe_2d_pipeline` (`:325-334`). */
EPIX_EXPORT void init_wireframe_2d_pipeline(ecs::Commands commands,
                                            ecs::Res<Mesh2dPipeline> mesh_2d_pipeline,
                                            ecs::Res<assets::AssetServer> asset_server);

/** @brief Bevy's `DrawWireframe2d` command list (`:311-317`), named once here
 *  and reused for both the registration and the
 *  `DrawFunctions<Wireframe2dPhaseItem>::read()->id<DrawWireframe2d>()` lookup
 *  `queue_wireframes` performs. */
EPIX_EXPORT using DrawWireframe2d =
    render::phase::RenderCommandSequence<Wireframe2dPhaseItem, render::phase::SetItemPipeline,
                                         SetMesh2dViewBindGroup<0>, SetMesh2dBindGroup<1>,
                                         SetWireframe2dPushConstants, DrawMesh2d>;

/** @brief The graph node that draws the wireframe phase (Bevy
 *  `Wireframe2dNode`, `:358-409`). */
EPIX_EXPORT struct Wireframe2dNode {
    /** @brief Bevy's `ViewNode::ViewQuery`. */
    using ViewQuery = ecs::Item<const render::camera::ExtractedCamera&,
                                const render::view::ExtractedView&,
                                const render::view::ViewTarget&,
                                const render::view::ViewDepthTexture&>;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext&,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;
};

/** @brief Draws wireframes for 2D meshes (Bevy `Wireframe2dPlugin`,
 *  `:58-155`).
 *
 *  Bevy's `SpriteRenderPlugin` does not add this plugin (`lib.rs:61-121`), so
 *  an application opts in explicitly, exactly as Bevy's examples do. */
EPIX_EXPORT struct Wireframe2dPlugin {
    /** @brief Debugging flags for the binned phase (Bevy `debug_flags`). */
    render::RenderDebugFlags debug_flags{};
    /** @brief Register the plugin (Bevy `Plugin::build`). */
    void attach(app::App& app);
    /** @brief The render-world half (Bevy's `build` after `RenderApp`). */
    void ready(app::App& app);
};

/** @brief Bevy `setup_global_wireframe_material` (`:555-566`). */
EPIX_EXPORT void setup_global_wireframe_material(ecs::Commands commands,
                                                 ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
                                                 ecs::Res<Wireframe2dConfig> config);
/** @brief Bevy `global_color_changed` (`:568-577`). */
EPIX_EXPORT void global_color_changed(ecs::Res<Wireframe2dConfig> config,
                                      ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
                                      ecs::Res<GlobalWireframeMaterial> global_material);
/** @brief Bevy `wireframe_color_changed` (`:579-592`). */
EPIX_EXPORT void wireframe_color_changed(
    ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
    ecs::Query<ecs::Item<ecs::Mut<Mesh2dWireframe>, const Wireframe2dColor&>,
               ecs::Filter<ecs::With<Wireframe2d>, ecs::Modified<Wireframe2dColor>>> colors_changed);
/** @brief Bevy `apply_wireframe_material` (`:594-619`). */
EPIX_EXPORT void apply_wireframe_material(ecs::Commands commands,
                                          ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
                                          ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const Wireframe2dColor&>>,
                                                     ecs::Filter<ecs::With<Wireframe2d>,
                                                                 ecs::Without<Mesh2dWireframe>>>
                                              wireframes,
                                          ecs::Query<ecs::Entity,
                                                     ecs::Filter<ecs::With<NoWireframe2d>, ecs::With<Mesh2dWireframe>>>
                                              no_wireframes,
                                          ecs::RemovedComponents<Wireframe2d> removed_wireframes,
                                          ecs::Res<GlobalWireframeMaterial> global_material);
/** @brief Bevy `apply_global_wireframe_material` (`:623-649`). */
EPIX_EXPORT void apply_global_wireframe_material(
    ecs::Commands commands, ecs::Res<Wireframe2dConfig> config,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const Wireframe2dColor&>>,
               ecs::Filter<ecs::With<mesh::Mesh2d>, ecs::Without<Wireframe2d>, ecs::Without<NoWireframe2d>,
                           ecs::Without<Mesh2dWireframe>>>
        meshes_without_material,
    ecs::Query<ecs::Entity,
               ecs::Filter<ecs::With<mesh::Mesh2d>, ecs::Without<Wireframe2d>, ecs::Without<NoWireframe2d>,
                           ecs::With<Mesh2dWireframe>>>
        meshes_with_global_material,
    ecs::Res<GlobalWireframeMaterial> global_material,
    ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials);
/** @brief Bevy `extract_wireframe_materials` (`:523-553`). */
EPIX_EXPORT void extract_wireframe_materials(
    ecs::ResMut<RenderWireframeInstances> material_instances,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&, const Mesh2dWireframe&>,
                            ecs::Or<ecs::Modified<camera::ViewVisibility>, ecs::Modified<Mesh2dWireframe>>>>
        changed_meshes,
    app::Extract<ecs::RemovedComponents<camera::ViewVisibility>> removed_visibilities,
    app::Extract<ecs::RemovedComponents<Mesh2dWireframe>> removed_materials);
/** @brief Bevy `extract_wireframe_2d_camera` (`:667-684`). */
EPIX_EXPORT void extract_wireframe_2d_camera(
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Wireframe2dPhaseItem>> wireframe_2d_phases,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::Camera&>, ecs::With<camera::Camera2d>>> cameras,
    ecs::Local<std::unordered_set<render::view::RetainedViewEntity>> live_entities);
/** @brief Bevy `extract_wireframe_entities_needing_specialization` (`:686-708`). */
EPIX_EXPORT void extract_wireframe_entities_needing_specialization(
    app::Extract<ecs::Res<WireframeEntitiesNeedingSpecialization>> entities_needing_specialization,
    ecs::ResMut<WireframeEntitySpecializationTicks> entity_specialization_ticks,
    ecs::Query<const render::view::ExtractedView&> views,
    ecs::ResMut<SpecializedWireframePipelineCache> specialized_wireframe_pipeline_cache,
    app::Extract<ecs::RemovedComponents<mesh::Mesh2d>> removed_meshes,
    ecs::SystemChangeTick ticks);
/** @brief Bevy `check_wireframe_entities_needing_specialization` (`:710-726`). */
EPIX_EXPORT void check_wireframe_entities_needing_specialization(
    ecs::Query<ecs::Entity,
               ecs::Or<ecs::Modified<mesh::Mesh2d>, assets::AssetChanged<mesh::Mesh2d>,
                       ecs::Modified<Mesh2dWireframe>, assets::AssetChanged<Mesh2dWireframe>>>
        needs_specialization,
    ecs::ResMut<WireframeEntitiesNeedingSpecialization> entities_needing_specialization);
/** @brief Bevy `specialize_wireframes` (`:728-808`). */
EPIX_EXPORT void specialize_wireframes(
    ecs::Res<render::RenderAssets<render::mesh::RenderMesh>> render_meshes,
    ecs::Res<RenderMesh2dInstances> render_mesh_instances,
    ecs::Res<RenderWireframeInstances> render_wireframe_instances,
    ecs::Res<render::phase::ViewBinnedRenderPhases<Wireframe2dPhaseItem>> wireframe_phases,
    ecs::Query<ecs::Item<const render::view::ExtractedView&, const render::view::RenderVisibleEntities&>> views,
    ecs::Res<ViewKeyCache> view_key_cache,
    ecs::Res<WireframeEntitySpecializationTicks> entity_specialization_ticks,
    ecs::Res<ViewSpecializationTicks> view_specialization_ticks,
    ecs::ResMut<SpecializedWireframePipelineCache> specialized_wireframe_pipeline_cache,
    ecs::ResMut<render::SpecializedMeshPipelines<Wireframe2dPipeline>> pipelines,
    ecs::Res<Wireframe2dPipeline> pipeline,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::SystemChangeTick ticks);
/** @brief Bevy `queue_wireframes` (`:810-874`). Bevy takes the phase's
 *  `DrawFunctions` and resolves `DrawWireframe2d` from them here, rather than
 *  the plugin stashing the id in a resource. */
EPIX_EXPORT void queue_wireframes(
    ecs::Res<render::phase::DrawFunctions<Wireframe2dPhaseItem>> draw_functions,
    ecs::Res<RenderMesh2dInstances> render_mesh_instances,
    ecs::Res<render::mesh::MeshAllocator> mesh_allocator,
    ecs::Res<SpecializedWireframePipelineCache> specialized_wireframe_pipeline_cache,
    ecs::Res<RenderWireframeInstances> render_wireframe_instances,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Wireframe2dPhaseItem>> wireframe_2d_phases,
    ecs::Query<ecs::Item<const render::view::ExtractedView&, const render::view::RenderVisibleEntities&>> views);
}  // namespace epix::sprite_render

/** @brief Hash for the wireframe batch-set key (Bevy derives `Hash`). */

/** @brief Hash for the wireframe batch-set key (Bevy derives `Hash`). */
template <>
struct std::hash<epix::sprite_render::Wireframe2dBatchSetKey> {
    std::size_t operator()(const epix::sprite_render::Wireframe2dBatchSetKey& key) const noexcept;
};

/** @brief Hash for the wireframe bin key (Bevy derives `Hash`). */
template <>
struct std::hash<epix::sprite_render::Wireframe2dBinKey> {
    std::size_t operator()(const epix::sprite_render::Wireframe2dBinKey& key) const noexcept;
};
