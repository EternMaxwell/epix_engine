#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/core_graph.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <cstddef>
#include <optional>
#include <variant>
#endif
#include <epix/sprite_render/mesh2d_pipeline.hpp>

namespace epix::sprite_render {
/** @brief The uniform-data type a material specializes on (Bevy's
 *  `AsBindGroup::Data`, which Bevy defaults to `()`).  C++ has no
 *  associated-type default, so the type is detected instead: a material that
 *  declares `Data` opts in, and every other material keys on the unit type,
 *  which is Bevy's default behaviour. */
template <typename M, typename = void>
struct Material2dData {
    using type = std::monostate;
};
template <typename M>
struct Material2dData<M, std::void_t<typename M::Data>> {
    using type = typename M::Data;
};

/** @brief The material bind group slot of a 2D pipeline (Bevy
 *  `MATERIAL_2D_BIND_GROUP_INDEX`, `material.rs:55`). */
inline constexpr std::uint32_t MATERIAL_2D_BIND_GROUP_INDEX = 2;

/** @brief The specialization key of one 2D material (Bevy `Material2dKey`,
 *  `material.rs:390-426`). */
EPIX_EXPORT template <typename M>
struct Material2dKey {
    /** @brief The mesh2d half of the key. */
    Mesh2dPipelineKey mesh_key{};
    /** @brief The material's own bind-group data (Bevy `M::Data`). */
    typename Material2dData<M>::type bind_group_data{};

    bool operator==(const Material2dKey&) const noexcept = default;
};

/** @brief A 2D material (Bevy `Material2d`, `material.rs:131-167`).  A material
 *  type derives from this and overrides only what it needs; the pipeline calls
 *  the derived members through static dispatch.
 *
 *  Bevy's `ShaderRef` (default, handle or path) translates to an optional
 *  source-relative shader path loaded under `embedded://`, the adaptation the
 *  codebase already uses elsewhere: `nullopt` means the default mesh2d shader,
 *  which is Bevy's `ShaderRef::Default`. */
EPIX_EXPORT template <typename M>
struct Material2d {
    /** @brief This material's vertex shader path, or nothing for the default
     *  mesh2d vertex shader. */
    static std::optional<std::string_view> vertex_shader() { return std::nullopt; }
    /** @brief This material's fragment shader path, or nothing for the default
     *  mesh2d fragment shader. */
    static std::optional<std::string_view> fragment_shader() { return std::nullopt; }
    /** @brief A view-depth bias forcing a specific render order (Bevy
     *  `depth_bias`). */
    float depth_bias() const { return 0.0f; }
    /** @brief This material's alpha mode (Bevy `alpha_mode`). */
    AlphaMode2d alpha_mode() const { return AlphaMode2d{AlphaMode2dOpaque{}}; }
    /** @brief Customize the specialized pipeline (Bevy `Material2d::specialize`,
     *  `material.rs:160-166`, which takes no `self` and so is an associated
     *  function rather than a member). */
    static void specialize(render::RenderPipelineDescriptor&,
                           const mesh::MeshVertexBufferLayoutRef&,
                           const Material2dKey<M>&) {}
};

/** @brief A 2D mesh's material handle (Bevy `MeshMaterial2d<M>`,
 *  `material.rs:197-233`).  It replaces the legacy fixed colour/texture
 *  aggregates: `ColorMaterial` covers both, since its texture is optional. */
EPIX_EXPORT template <typename M>
    requires std::derived_from<M, Material2d<M>>
struct MeshMaterial2d {
    /** @brief Handle to the material asset. */
    assets::Handle<M> handle;

    MeshMaterial2d() = default;
    explicit MeshMaterial2d(assets::Handle<M> material) : handle(std::move(material)) {}

    /** @brief The material's asset id (Bevy `MeshMaterial2d::id`). */
    assets::AssetId<M> id() const noexcept { return handle.id(); }

    bool operator==(const MeshMaterial2d&) const noexcept = default;
};

}  // namespace epix::sprite_render

namespace epix::assets {
/** @brief A `MeshMaterial2d<M>` names the material asset it holds, which is what
 *  lets `AssetChanged<MeshMaterial2d<M>>` observe material swaps (Bevy's
 *  `impl AsAssetId for MeshMaterial2d<M>`). */
template <typename M>
    requires std::derived_from<M, sprite_render::Material2d<M>>
struct AsAssetId<sprite_render::MeshMaterial2d<M>> {
    using Asset = M;

    static AssetId<Asset> as_asset_id(const sprite_render::MeshMaterial2d<M>& material) noexcept {
        return material.id();
    }
};
}  // namespace epix::assets

namespace epix::sprite_render {

/** @brief The material properties a prepared material exposes to its pipeline
 *  (Bevy `Material2dProperties`, `material.rs`). */
EPIX_EXPORT struct Material2dProperties {
    /** @brief View-depth bias (Bevy `depth_bias`). */
    float depth_bias = 0.0f;
    /** @brief How the material's output is composited. */
    AlphaMode2d alpha_mode = AlphaMode2d{AlphaMode2dOpaque{}};
    /** @brief Key bits the material contributes to the mesh2d key. */
    Mesh2dPipelineKey mesh_pipeline_key_bits{};
    /** @brief The draw function queued for this material. */
    render::phase::DrawFunctionId draw_function_id{};
};

/** @brief A prepared 2D material: its bind group, the data it specialized on
 *  and its pipeline properties (Bevy `PreparedMaterial2d`, `material.rs`). */
EPIX_EXPORT template <typename M>
struct PreparedMaterial2d {
    /** @brief The material's prepared bind group. */
    render::render_resource::PreparedBindGroup bindings;
    /** @brief The bind group handed to the shader. */
    wgpu::BindGroup bind_group;
    /** @brief The bind-group data this material specialized with. */
    typename Material2dData<M>::type key{};
    /** @brief What the pipeline needs to know about this material. */
    Material2dProperties properties;
};

/** @brief The material asset each main-world mesh renders with (Bevy
 *  `RenderMaterial2dInstances<M>`, `material.rs:321-328`). */
EPIX_EXPORT template <typename M>
struct RenderMaterial2dInstances {
    render::sync_world::MainEntityHashMap<assets::AssetId<M>> instances;
};

/** @brief Entities whose material changed and that need re-specialization
 *  (Bevy `EntitiesNeedingSpecialization<M>`, `material.rs:596-610`). */
EPIX_EXPORT template <typename M>
struct EntitiesNeedingSpecialization {
    std::vector<ecs::Entity> entities;
};

/** @brief The last specialization tick of each main entity (Bevy
 *  `EntitySpecializationTickPair<M>`, `material.rs:612-628`). */
EPIX_EXPORT template <typename M>
struct EntitySpecializationTickPair {
    render::sync_world::MainEntityHashMap<ecs::Tick> entities;
};

/** @brief Cached specialized pipelines for one material in one view (Bevy
 *  `SpecializedMaterial2dViewPipelineCache<M>`, `material.rs:639-652`). */
EPIX_EXPORT template <typename M>
struct SpecializedMaterial2dViewPipelineCache {
    render::sync_world::MainEntityHashMap<std::pair<ecs::Tick, render::CachedPipelineId>> map;
};

/** @brief Per-view specialized pipeline caches for one material (Bevy
 *  `SpecializedMaterial2dPipelineCache<M>`, `material.rs:629-638`). */
EPIX_EXPORT template <typename M>
struct SpecializedMaterial2dPipelineCache {
    render::sync_world::MainEntityHashMap<SpecializedMaterial2dViewPipelineCache<M>> map;
};

EPIX_EXPORT template <typename M, std::size_t I>
struct SetMaterial2dBindGroup {
    template <render::phase::PhaseItem P>
    struct Command {
        void prepare(const epix::ecs::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const P& item,
            std::tuple<>,
            std::optional<epix::ecs::Item<>> entity_item,
            std::tuple<epix::ecs::Res<render::RenderAssets<PreparedMaterial2d<M>>>,
                       epix::ecs::Res<RenderMaterial2dInstances<M>>>
                params,
            const wgpu::RenderPassEncoder& encoder) {
            (void)entity_item;
            auto&& [materials, instances] = params;

            // Bevy returns `RenderCommandResult::Skip` from both missing arms
            // (`material.rs:534-539`), which aborts the remaining command list and
            // suppresses the draw; success here would draw with no material bind
            // group set (or a stale one from the previous item).
            const auto instance = instances->instances.find(item.main_entity());
            if (instance == instances->instances.end()) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }
            const auto* prepared = materials->try_get(instance->second);
            if (!prepared) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Skip, .message = {}});
            }

            encoder.setBindGroup(I, prepared->bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

}  // namespace epix::sprite_render

namespace epix::render::phase {
/** @brief The material bind-group command's trait specialisation: one command
 *  type, every phase (Bevy `impl<P: PhaseItem, M: Material2d, const I: usize>
 *  RenderCommand<P> for SetMaterial2dBindGroup<M, I>`). */
EPIX_EXPORT template <typename M, std::size_t I, PhaseItem P>
    requires std::derived_from<M, ::epix::sprite_render::Material2d<M>>
struct RenderCommand<::epix::sprite_render::SetMaterial2dBindGroup<M, I>, P> {
    using Param     = std::tuple<epix::ecs::Res<::epix::render::RenderAssets<
                                     ::epix::sprite_render::PreparedMaterial2d<M>>>,
                                 epix::ecs::Res<::epix::sprite_render::RenderMaterial2dInstances<M>>>;
    using ViewQuery = std::tuple<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(
        const P& item,
        std::tuple<>,
        std::optional<epix::ecs::Item<>> entity_item,
        Param params,
        const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        auto&& [materials, instances] = params;

        // The same two `Skip` arms as the inner command (SRD-399).
        const auto instance = instances->instances.find(item.main_entity());
        if (instance == instances->instances.end()) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }
        const auto* prepared = materials->try_get(instance->second);
        if (!prepared) {
            return std::unexpected(RenderCommandError{.type = RenderCommandError::Type::Skip, .message = {}});
        }

        encoder.setBindGroup(I, prepared->bind_group, std::span<const std::uint32_t>{});
        return {};
    }
};
}  // namespace epix::render::phase

namespace epix::sprite_render {

/** @brief Bevy's `DrawMaterial2d<M>` command list (`material.rs`): the draw
 *  sequence every 2D phase of this material registers, `SetItemPipeline`, the
 *  mesh2d view and instance bind groups, the material's own bind group and
 *  `DrawMesh2d`. Bevy's version is a tuple and so needs no phase parameter;
 *  Epix's `RenderCommandSequence` carries its phase, so the phase is the second
 *  argument here. The name is what both the registration and the draw-function
 *  lookup in `prepare_asset` use, so the command list is written once. */
template <typename M, typename P>
using DrawMaterial2d =
    render::phase::RenderCommandSequence<P, render::phase::SetItemPipeline, SetMesh2dViewBindGroup<0>,
                                         SetMesh2dBindGroup<1>,
                                         SetMaterial2dBindGroup<M, MATERIAL_2D_BIND_GROUP_INDEX>, DrawMesh2d>;

/** @brief The pipeline data of one 2D material type (Bevy
 *  `Material2dPipeline<M>`, `material.rs:380-388`). */
EPIX_EXPORT template <typename M>
struct Material2dPipeline {
    /** @brief The specialization key of this pipeline (Bevy
     *  `SpecializedMeshPipeline::Key`). */
    using Key = Material2dKey<M>;
    /** @brief The mesh2d pipeline this material's pipeline is built from. */
    Mesh2dPipeline mesh2d_pipeline;
    /** @brief The material's own group layout descriptor. */
    render::BindGroupLayoutDescriptor material2d_layout;
    /** @brief The material's vertex shader, when it overrides the default. */
    std::optional<assets::Handle<shader::Shader>> vertex_shader;
    /** @brief The material's fragment shader, when it overrides the default. */
    std::optional<assets::Handle<shader::Shader>> fragment_shader;

    /** @brief Specialize this material's render pipeline (Bevy
     *  `SpecializedMeshPipeline for Material2dPipeline<M>`,
     *  `material.rs:446-477`): specialize the mesh2d pipeline, tell the shader
     *  which slot the material occupies, apply the material's shader overrides
     *  and lay out `[view, mesh, material]`. */
    std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> specialize(
        const Material2dKey<M>& key, const mesh::MeshVertexBufferLayoutRef& layout) const;
};

/** @brief Insert the `Material2dPipeline<M>` resource (Bevy
 *  `init_material_2d_pipeline`, `material.rs:480-500`). */
EPIX_EXPORT template <typename M>
void init_material_2d_pipeline(ecs::Commands commands,
                               ecs::Res<wgpu::Device> device,
                               ecs::Res<assets::AssetServer> asset_server,
                               ecs::Res<Mesh2dPipeline> mesh2d_pipeline);

/** @brief Queue the entities whose mesh or material changed so they are
 *  re-specialized (Bevy `check_entities_needing_specialization<M>`,
 *  `material.rs:664-689`). Bevy fills its list from a parallel local; the
 *  result is the same collection. */
EPIX_EXPORT template <typename M>
void check_entities_needing_specialization(
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::Or<ecs::Modified<mesh::Mesh2d>, assets::AssetChanged<mesh::Mesh2d>,
                                   ecs::Modified<MeshMaterial2d<M>>, assets::AssetChanged<MeshMaterial2d<M>>>,
                           ecs::With<MeshMaterial2d<M>>>>
        needs_specialization,
    ecs::ResMut<EntitiesNeedingSpecialization<M>> entities_needing_specialization);

/** @brief Track which material each visible mesh renders with (Bevy
 *  `extract_mesh_materials_2d<M>`, `material.rs:330-378`). Visibility changes
 *  add or remove the entity; a removed material component removes it too,
 *  unless the query already picked it up this frame. */
EPIX_EXPORT template <typename M>
void extract_mesh_materials_2d(
    ecs::ResMut<RenderMaterial2dInstances<M>> material_instances,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&, const MeshMaterial2d<M>&>,
                            ecs::Filter<ecs::Or<ecs::Modified<camera::ViewVisibility>,
                                                ecs::Modified<MeshMaterial2d<M>>>>>>
        changed_meshes,
    app::Extract<ecs::RemovedComponents<MeshMaterial2d<M>>> removed_materials);

/** @brief Specialize each visible 2D mesh's material pipeline for each view
 *  (Bevy `specialize_material2d_meshes<M>`, `material.rs:691-787`). */
EPIX_EXPORT template <typename M>
void specialize_material2d_meshes(
    ecs::Res<Material2dPipeline<M>> material2d_pipeline,
    ecs::ResMut<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedMeshPipelines<Material2dPipeline<M>>> pipelines,
    ecs::Res<render::RenderAssets<render::mesh::RenderMesh>> render_meshes,
    ecs::Res<render::RenderAssets<PreparedMaterial2d<M>>> render_materials,
    ecs::ResMut<RenderMesh2dInstances> render_mesh_instances,
    ecs::Res<RenderMaterial2dInstances<M>> render_material_instances,
    ecs::Res<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> transparent_render_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::Opaque2D>> opaque_render_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::AlphaMask2D>> alpha_mask_render_phases,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::RenderVisibleEntities&>>
        views,
    ecs::Res<ViewKeyCache> view_key_cache,
    ecs::Res<EntitySpecializationTickPair<M>> entity_specialization_ticks,
    ecs::Res<ViewSpecializationTicks> view_specialization_ticks,
    ecs::SystemChangeTick ticks,
    ecs::ResMut<SpecializedMaterial2dPipelineCache<M>> specialized_material_pipeline_cache);

/** @brief Queue each visible 2D mesh into the phase its material's alpha mode
 *  selects (Bevy `queue_material2d_meshes<M>`, `material.rs:789-926`). */
EPIX_EXPORT template <typename M>
void queue_material2d_meshes(
    ecs::Res<render::RenderAssets<render::mesh::RenderMesh>> render_meshes,
    ecs::Res<render::RenderAssets<PreparedMaterial2d<M>>> render_materials,
    ecs::ResMut<RenderMesh2dInstances> render_mesh_instances,
    ecs::Res<RenderMaterial2dInstances<M>> render_material_instances,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> transparent_render_phases,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::Opaque2D>> opaque_render_phases,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::AlphaMask2D>> alpha_mask_render_phases,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::RenderVisibleEntities&>>
        views,
    ecs::Res<SpecializedMaterial2dPipelineCache<M>> specialized_material_pipeline_cache);

/** @brief Adds the resources and systems one 2D material type needs (Bevy
 *  `Material2dPlugin<M>`, `material.rs:263-319`). `attach` wires the main
 *  world, `ready` the render world, as the mesh2d plugin does. */
EPIX_EXPORT template <typename M>
struct Material2dPlugin {
    void attach(app::App& app);
    void ready(app::App& app);
};

/** @brief Bevy `ColorMaterialPlugin` (`color_material.rs:11-33`): publishes the
 *  color material shader, installs `Material2dPlugin<ColorMaterial>` and
 *  inserts the default magenta material. */
EPIX_EXPORT struct ColorMaterialPlugin {
    void attach(app::App& app);

   private:
    /** @brief Publish the embedded color material shader (Bevy's
     *  `embedded_asset!(app, "color_material.wgsl")`). */
    void register_color_material_shader(ecs::World& world);
};

/** @brief Carry that queue into the render world, stamp each entity's
 *  specialization tick, and drop the cached pipelines of materials whose
 *  component was removed (Bevy `extract_entities_needs_specialization<M>`,
 *  `material.rs:568-593`). */
EPIX_EXPORT template <typename M>
void extract_entities_needs_specialization(
    app::Extract<ecs::Res<EntitiesNeedingSpecialization<M>>> entities_needing_specialization,
    ecs::ResMut<EntitySpecializationTickPair<M>> entity_specialization_ticks,
    app::Extract<ecs::RemovedComponents<MeshMaterial2d<M>>> removed_mesh_material_components,
    ecs::ResMut<SpecializedMaterial2dPipelineCache<M>> specialized_material2d_pipeline_cache,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&>, ecs::With<render::view::ExtractedView>> views,
    ecs::SystemChangeTick ticks);

}  // namespace epix::sprite_render

/** @brief Prepare a 2D material into its render-world form (Bevy
 *  `RenderAsset for PreparedMaterial2d<M>`, `material.rs:961-1027`).
 *
 *  Builds the material's bind group against the material layout, derives the
 *  mesh2d key bits from its alpha mode, and resolves the draw function of the
 *  matching phase. A bind group that is not ready yet asks for a retry with the
 *  material handed back. */
template <typename M>
struct epix::render::RenderAsset<epix::sprite_render::PreparedMaterial2d<M>> {
    using SourceAsset    = M;
    using ExtractedAsset = M;
    using Param          = std::tuple<
        epix::ecs::Res<wgpu::Device>, epix::ecs::Res<epix::render::PipelineServer>,
        epix::ecs::Res<epix::sprite_render::Material2dPipeline<M>>,
        epix::ecs::Res<epix::render::phase::DrawFunctions<epix::core_graph::core_2d::Opaque2D>>,
        epix::ecs::Res<epix::render::phase::DrawFunctions<epix::core_graph::core_2d::AlphaMask2D>>,
        epix::ecs::Res<epix::render::phase::DrawFunctions<epix::core_graph::core_2d::Transparent2D>>,
        typename epix::render::render_resource::AsBindGroup<M>::Param>;

    std::expected<epix::sprite_render::PreparedMaterial2d<M>, epix::render::PrepareAssetError<M>> prepare_asset(
        M&& material, epix::assets::AssetId<M>, Param params,
        const epix::sprite_render::PreparedMaterial2d<M>*) {
        auto&& [device, pipeline_server, pipeline, opaque_draw_functions, alpha_mask_draw_functions,
                transparent_draw_functions, material_param] = params;

        const auto bind_group_data =
            epix::render::render_resource::AsBindGroup<M>::bind_group_data(material);
        auto prepared = epix::render::render_resource::as_bind_group<M>(
            device.get(), pipeline_server->get_bind_group_layout(pipeline->material2d_layout), material, material_param);
        if (!prepared) {
            auto error = prepared.error();
            if (std::holds_alternative<epix::render::render_resource::RetryBindGroupNextUpdate>(error)) {
                return std::unexpected(
                    epix::render::PrepareAssetError<M>{epix::render::RetryNextAssetUpdate<M>{std::move(material)}});
            }
            return std::unexpected(epix::render::PrepareAssetError<M>{std::move(error)});
        }

        // Bevy resolves the draw function from the phase the material's alpha
        // mode selects, by the name the sequence was registered under
        // (`material.rs`: `opaque_draw_functions.read().id::<DrawMaterial2d<M>>()`
        // and its two siblings), rather than storing the ids at registration
        // time.
        const auto alpha_mode = material.alpha_mode();
        epix::render::phase::DrawFunctionId draw_function_id{};
        if (std::holds_alternative<epix::sprite_render::AlphaMode2dOpaque>(alpha_mode)) {
            draw_function_id =
                opaque_draw_functions->read()
                    ->template id<
                        epix::sprite_render::DrawMaterial2d<M, epix::core_graph::core_2d::Opaque2D>>();
        } else if (std::holds_alternative<epix::sprite_render::AlphaMode2dMask>(alpha_mode)) {
            draw_function_id =
                alpha_mask_draw_functions->read()
                    ->template id<
                        epix::sprite_render::DrawMaterial2d<M, epix::core_graph::core_2d::AlphaMask2D>>();
        } else {
            draw_function_id =
                transparent_draw_functions->read()
                    ->template id<
                        epix::sprite_render::DrawMaterial2d<M, epix::core_graph::core_2d::Transparent2D>>();
        }

        // `as_bind_group` already returns a `PreparedBindGroup`, which owns both
        // the created bind group and its resources — exactly what the prepared
        // material holds. Capture the group before moving the value into
        // `bindings`.
        const auto bind_group = prepared->bind_group;
        return epix::sprite_render::PreparedMaterial2d<M>{
            .bindings   = std::move(*prepared),
            .bind_group = bind_group,
            .key        = bind_group_data,
            .properties = {.depth_bias              = material.depth_bias(),
                           .alpha_mode              = alpha_mode,
                           .mesh_pipeline_key_bits  = epix::sprite_render::alpha_mode_pipeline_key(alpha_mode),
                           .draw_function_id        = draw_function_id},
        };
    }

    /** @brief Bevy's `Material2d` does not override `RenderAsset::asset_usage`, so
     *  material assets use the default (`MAIN_WORLD | RENDER_WORLD`): the handle
     *  component keeps them alive in the main world while the render world holds
     *  the prepared material. Declaring them render-world-only instead demanded a
     *  `take_gpu_data` hook that Bevy's material path never needs. */
    static assets::RenderAssetUsages asset_usage(const M&) noexcept {
        return static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                      assets::RenderAssetUsages::RENDER_WORLD);
    }
};

template <typename M>
struct std::hash<epix::sprite_render::Material2dKey<M>> {
    std::size_t operator()(const epix::sprite_render::Material2dKey<M>& key) const noexcept {
        std::size_t seed = std::hash<epix::sprite_render::Mesh2dPipelineKey>{}(key.mesh_key);
        if constexpr (requires { std::hash<typename epix::sprite_render::Material2dData<M>::type>{}; }) {
            seed ^= std::hash<typename epix::sprite_render::Material2dData<M>::type>{}(key.bind_group_data) +
                    0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

template <>
struct std::hash<epix::sprite_render::Material2dBindGroupId> {
    std::size_t operator()(const epix::sprite_render::Material2dBindGroupId& id) const noexcept {
        return id.value ? std::hash<epix::assets::UntypedAssetId>{}(*id.value) : 0;
    }
};