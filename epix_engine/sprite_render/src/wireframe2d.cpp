#include <epix/sprite_render/wireframe2d.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/core_graph.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
/** @brief Embedded path of the wireframe fragment shader. Bevy embeds
 *  `wireframe2d.wgsl` under its crate path; Epix keeps the sprite module's
 *  `sprite/` prefix. */
constexpr std::string_view kWireframe2dShaderPath = "sprite/wireframe2d.slang";

/** @brief Slang representation of Bevy's 12-line `wireframe2d.wgsl`: only the
 *  fragment stage is replaced (the vertex stage stays mesh2d's), and the colour
 *  arrives as a 16-byte fragment push constant. */
constexpr std::string_view kWireframe2dShader = R"slang(
import epix.mesh2d_vertex_output;

struct PushConstants {
    float4 color;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

[shader("fragment")]
float4 fragment(epix::mesh2d_vertex_output::VertexOutput mesh) : SV_Target {
    return push_constants.color;
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

/** @brief Bevy `get_wireframe_material` (`wireframe2d.rs:651-665`): a
 *  `Wireframe2dColor` allocates a material, everything else reuses the global
 *  one, which already carries `Wireframe2dConfig::default_color`. */
epix::assets::Handle<epix::sprite_render::Wireframe2dMaterial> get_wireframe_material(
    std::optional<glm::vec4> maybe_color,
    epix::ecs::ResMut<epix::assets::Assets<epix::sprite_render::Wireframe2dMaterial>>& wireframe_materials,
    const epix::sprite_render::GlobalWireframeMaterial& global_material) {
    if (maybe_color) {
        return wireframe_materials->emplace(epix::sprite_render::Wireframe2dMaterial{.color = *maybe_color});
    }
    return global_material.handle;
}
}  // namespace

std::size_t std::hash<epix::sprite_render::Wireframe2dBatchSetKey>::operator()(
    const epix::sprite_render::Wireframe2dBatchSetKey& key) const noexcept {
    std::size_t hash = std::hash<epix::render::CachedPipelineId>{}(key.pipeline);
    const auto combine = [&hash](std::size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); };
    combine(std::hash<epix::assets::UntypedAssetId>{}(key.asset_id));
    combine(std::hash<epix::render::phase::DrawFunctionId>{}(key.draw_function));
    combine(std::hash<epix::render::mesh::SlabId>{}(key.vertex_slab));
    if (key.index_slab) combine(std::hash<epix::render::mesh::SlabId>{}(*key.index_slab));
    return hash;
}

std::size_t std::hash<epix::sprite_render::Wireframe2dBinKey>::operator()(
    const epix::sprite_render::Wireframe2dBinKey& key) const noexcept {
    return std::hash<epix::assets::UntypedAssetId>{}(key.asset_id);
}

std::expected<epix::render::RenderPipelineDescriptor, epix::render::SpecializedMeshPipelineError>
epix::sprite_render::Wireframe2dPipeline::specialize(const Mesh2dPipelineKey& key,
                                                     const mesh::MeshVertexBufferLayoutRef& layout) const {
    // Bevy `SpecializedMeshPipeline for Wireframe2dPipeline` (`:336-356`):
    // everything is inherited from mesh2d and only five things change.
    auto descriptor = mesh_pipeline.specialize(key, layout);
    if (!descriptor) return descriptor;

    descriptor->label = "wireframe_2d_pipeline";
    descriptor->push_constant_ranges.push_back(render::PushConstantRange{
        .stages = wgpu::ShaderStage::eFragment, .start = 0, .end = static_cast<std::uint32_t>(sizeof(glm::vec4))});
    if (descriptor->fragment) {
        descriptor->fragment->shader = shader;
    }
    // `PolygonMode` is a wgpu-native primitive-state extension, chained through
    // the wrapper's extras struct; the descriptor owns the chain.
    descriptor->primitive.setNextInChain(wgpu::PrimitiveStateExtras().setPolygonMode(wgpu::PolygonMode::eLine));
    if (descriptor->depth_stencil) {
        // Push the wireframe lines off the surface to avoid z-fighting, as
        // Bevy's `bias.slope_scale = 1.0` does.
        descriptor->depth_stencil->setDepthBiasSlopeScale(1.0f);
    }
    return descriptor;
}

void epix::sprite_render::init_wireframe_2d_pipeline(ecs::Commands commands,
                                                     ecs::Res<Mesh2dPipeline> mesh_2d_pipeline,
                                                     ecs::Res<assets::AssetServer> asset_server) {
    // Bevy `init_wireframe_2d_pipeline` (`:325-334`).
    commands.insert_resource(Wireframe2dPipeline{
        .mesh_pipeline = mesh_2d_pipeline.get(),
        .shader        = asset_server->load<shader::Shader>("embedded://sprite/wireframe2d.slang"),
    });
}

std::expected<void, epix::render::graph::NodeRunError> epix::sprite_render::Wireframe2dNode::run(
    render::graph::GraphContext& graph,
    render::graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const ecs::World& world) const {
    // Bevy `ViewNode for Wireframe2dNode` (`:360-409`): draw the view's
    // wireframe phase after the main pass, preserving the colour attachment.
    auto&& [camera, extracted_view, target, depth] = view;
    const auto phases = world.get_resource<render::phase::ViewBinnedRenderPhases<Wireframe2dPhaseItem>>();
    if (!phases) return {};
    const auto phase = phases->get().phases.find(extracted_view.retained_view_entity);
    if (phase == phases->get().phases.end()) return {};

    const auto color_attachments = std::array{target.get_color_attachment()};
    const auto depth_attachment  = depth.get_attachment(wgpu::StoreOp::eStore);
    const auto viewport          = camera.viewport;
    const auto view_entity       = graph.view_entity();
    const auto* world_ptr        = std::addressof(world);
    const auto* phase_ptr        = std::addressof(phase->second);
    render_context.add_command_buffer_generation_task(
        [color_attachments, depth_attachment, viewport, view_entity, world_ptr, phase_ptr](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("wireframe_2d"));
            auto render_pass = encoder.beginRenderPass(wgpu::RenderPassDescriptor()
                                                           .setLabel("wireframe_2d")
                                                           .setColorAttachments(color_attachments)
                                                           .setDepthStencilAttachment(depth_attachment));
            if (viewport) {
                const auto& vp = *viewport;
                render_pass.setViewport(static_cast<float>(vp.physical_position.x),
                                        static_cast<float>(vp.physical_position.y),
                                        static_cast<float>(vp.physical_size.x),
                                        static_cast<float>(vp.physical_size.y), vp.depth.first, vp.depth.second);
            }
            if (!phase_ptr->is_empty()) phase_ptr->render(render_pass, *world_ptr, view_entity);
            render_pass.end();
            return encoder.finish();
        });
    return {};
}

void epix::sprite_render::setup_global_wireframe_material(ecs::Commands commands,
                                                          ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
                                                          ecs::Res<Wireframe2dConfig> config) {
    // Bevy `setup_global_wireframe_material` (`:555-566`): build the handle the
    // global path reuses.
    commands.insert_resource(
        GlobalWireframeMaterial{.handle = materials->emplace(Wireframe2dMaterial{.color = config->default_color})});
}

void epix::sprite_render::global_color_changed(ecs::Res<Wireframe2dConfig> config,
                                               ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
                                               ecs::Res<GlobalWireframeMaterial> global_material) {
    // Bevy `global_color_changed` (`:568-577`).
    if (auto asset = materials->get_mut(global_material->handle.id())) {
        asset->get().color = config->default_color;
    }
}

void epix::sprite_render::wireframe_color_changed(
    ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
    ecs::Query<ecs::Item<ecs::Mut<Mesh2dWireframe>&, const Wireframe2dColor&>,
               ecs::Filter<ecs::With<Wireframe2d>, ecs::Modified<Wireframe2dColor>>>
        colors_changed) {
    // Bevy `wireframe_color_changed` (`:579-592`): a changed colour allocates a
    // new material, which also marks `Mesh2dWireframe` asset-changed so the
    // pipeline is re-specialized.
    for (auto&& [handle, wireframe_color] : colors_changed.iter()) {
        handle.get_mut().handle = materials->emplace(Wireframe2dMaterial{.color = wireframe_color.color});
    }
}

void epix::sprite_render::apply_wireframe_material(
    ecs::Commands commands,
    ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const Wireframe2dColor&>>,
               ecs::Filter<ecs::With<Wireframe2d>, ecs::Without<Mesh2dWireframe>>>
        wireframes,
    ecs::Query<ecs::Item<ecs::Entity>, ecs::Filter<ecs::With<NoWireframe2d>, ecs::With<Mesh2dWireframe>>>
        no_wireframes,
    ecs::RemovedComponents<Wireframe2d> removed_wireframes,
    ecs::Res<GlobalWireframeMaterial> global_material) {
    // Bevy `apply_wireframe_material` (`:594-619`). `EntityCommands::remove`
    // already skips despawned entities, which is what Bevy's `get_entity` guard
    // does.
    for (auto entity : removed_wireframes.read()) {
        commands.entity(entity).remove<Mesh2dWireframe>();
    }
    for (auto&& [entity] : no_wireframes.iter()) {
        commands.entity(entity).remove<Mesh2dWireframe>();
    }

    std::vector<std::pair<ecs::Entity, Mesh2dWireframe>> material_to_spawn;
    for (auto&& [entity, maybe_color] : wireframes.iter()) {
        const auto color = maybe_color ? std::optional<glm::vec4>{maybe_color->get().color} : std::nullopt;
        material_to_spawn.emplace_back(
            entity, Mesh2dWireframe{.handle = get_wireframe_material(color, materials, global_material.get())});
    }
    if (!material_to_spawn.empty()) commands.try_insert_batch(material_to_spawn);
}

void epix::sprite_render::apply_global_wireframe_material(
    ecs::Commands commands,
    ecs::Res<Wireframe2dConfig> config,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const Wireframe2dColor&>>,
               ecs::Filter<ecs::With<mesh::Mesh2d>, ecs::Without<Wireframe2d>, ecs::Without<NoWireframe2d>,
                           ecs::Without<Mesh2dWireframe>>>
        meshes_without_material,
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::With<mesh::Mesh2d>, ecs::Without<Wireframe2d>, ecs::Without<NoWireframe2d>,
                           ecs::With<Mesh2dWireframe>>>
        meshes_with_global_material,
    ecs::Res<GlobalWireframeMaterial> global_material,
    ecs::ResMut<assets::Assets<Wireframe2dMaterial>> materials) {
    // Bevy `apply_global_wireframe_material` (`:623-649`). Only the material
    // handle is added, never `Wireframe2d`, so global and explicit wireframes
    // stay distinguishable.
    if (config->global) {
        std::vector<std::pair<ecs::Entity, Mesh2dWireframe>> material_to_spawn;
        for (auto&& [entity, maybe_color] : meshes_without_material.iter()) {
            const auto color = maybe_color ? std::optional<glm::vec4>{maybe_color->get().color} : std::nullopt;
            material_to_spawn.emplace_back(
                entity, Mesh2dWireframe{.handle = get_wireframe_material(color, materials, global_material.get())});
        }
        if (!material_to_spawn.empty()) commands.try_insert_batch(material_to_spawn);
    } else {
        for (auto&& [entity] : meshes_with_global_material.iter()) {
            commands.entity(entity).remove<Mesh2dWireframe>();
        }
    }
}

void epix::sprite_render::extract_wireframe_materials(
    ecs::ResMut<RenderWireframeInstances> material_instances,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&, const Mesh2dWireframe&>,
                            ecs::Filter<ecs::Or<ecs::Modified<camera::ViewVisibility>,
                                                ecs::Modified<Mesh2dWireframe>>>>>
        changed_meshes,
    app::Extract<ecs::RemovedComponents<camera::ViewVisibility>> removed_visibilities,
    app::Extract<ecs::RemovedComponents<Mesh2dWireframe>> removed_materials) {
    // Bevy `extract_wireframe_materials` (`:523-553`).
    for (auto&& [entity, view_visibility, material] : changed_meshes.iter()) {
        const auto main_entity = render::sync_world::MainEntity{entity};
        if (view_visibility.get()) {
            material_instances->instances.insert_or_assign(main_entity, material.handle.id());
        } else {
            material_instances->instances.erase(main_entity);
        }
    }
    // Removals only win when the entity was not re-added this frame, which is
    // what Bevy's `!changed_meshes_query.contains(entity)` guard expresses.
    const auto remove_instance = [&](ecs::Entity entity) {
        if (!changed_meshes.contains(entity)) {
            material_instances->instances.erase(render::sync_world::MainEntity{entity});
        }
    };
    for (auto entity : removed_visibilities.read()) remove_instance(entity);
    for (auto entity : removed_materials.read()) remove_instance(entity);
}

void epix::sprite_render::extract_wireframe_2d_camera(
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Wireframe2dPhaseItem>> wireframe_2d_phases,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::Camera&>, ecs::With<camera::Camera2d>>> cameras,
    ecs::Local<std::unordered_set<render::view::RetainedViewEntity>> live_entities) {
    // Bevy `extract_wireframe_2d_camera` (`:667-684`): one binned phase per
    // active Camera2d, always with `GpuPreprocessingMode::None`.
    live_entities->clear();
    for (auto&& [entity, camera] : cameras.iter()) {
        if (!camera.is_active) continue;
        const auto retained_view =
            render::view::RetainedViewEntity::create(render::sync_world::MainEntity{entity}, std::nullopt, 0);
        wireframe_2d_phases->prepare_for_new_frame(retained_view, render::batching::GpuPreprocessingMode::None);
        live_entities->insert(retained_view);
    }
    const auto remove_dead_views = [&live_entities](const auto& entry) { return !live_entities->contains(entry.first); };
    std::erase_if(wireframe_2d_phases->phases, remove_dead_views);
}

void epix::sprite_render::extract_wireframe_entities_needing_specialization(
    app::Extract<ecs::Res<WireframeEntitiesNeedingSpecialization>> entities_needing_specialization,
    ecs::ResMut<WireframeEntitySpecializationTicks> entity_specialization_ticks,
    ecs::Query<ecs::Item<const render::view::ExtractedView&>> views,
    ecs::ResMut<SpecializedWireframePipelineCache> specialized_wireframe_pipeline_cache,
    app::Extract<ecs::RemovedComponents<mesh::Mesh2d>> removed_meshes,
    ecs::SystemChangeTick ticks) {
    // Bevy `extract_wireframe_entities_needing_specialization` (`:686-708`).
    for (auto entity : entities_needing_specialization->entities) {
        entity_specialization_ticks->entities.insert_or_assign(render::sync_world::MainEntity{entity},
                                                               ticks.this_run());
    }
    for (auto entity : removed_meshes.read()) {
        const auto main_entity = render::sync_world::MainEntity{entity};
        for (auto&& [view] : views.iter()) {
            if (auto cache = specialized_wireframe_pipeline_cache->map.find(view.retained_view_entity);
                cache != specialized_wireframe_pipeline_cache->map.end()) {
                cache->second.map.erase(main_entity);
            }
        }
    }
}

void epix::sprite_render::check_wireframe_entities_needing_specialization(
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::Or<ecs::Modified<mesh::Mesh2d>, assets::AssetChanged<mesh::Mesh2d>,
                                   ecs::Modified<Mesh2dWireframe>, assets::AssetChanged<Mesh2dWireframe>>>>
        needs_specialization,
    ecs::ResMut<WireframeEntitiesNeedingSpecialization> entities_needing_specialization) {
    // Bevy `check_wireframe_entities_needing_specialization` (`:710-726`).
    entities_needing_specialization->entities.clear();
    for (auto&& [entity] : needs_specialization.iter()) {
        entities_needing_specialization->entities.push_back(entity);
    }
}

void epix::sprite_render::specialize_wireframes(
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
    ecs::SystemChangeTick ticks) {
    // Bevy `specialize_wireframes` (`:728-808`).
    std::unordered_set<render::view::RetainedViewEntity> all_views;
    for (auto&& [view, visible_entities] : views.iter()) {
        all_views.insert(view.retained_view_entity);
        if (!wireframe_phases->phases.contains(view.retained_view_entity)) continue;
        const auto view_key_it = view_key_cache->keys.find(view.retained_view_entity.main_entity);
        if (view_key_it == view_key_cache->keys.end()) continue;
        const auto view_tick_it = view_specialization_ticks->ticks.find(view.retained_view_entity.main_entity);
        if (view_tick_it == view_specialization_ticks->ticks.end()) continue;
        const auto view_tick = view_tick_it->second;

        auto& view_cache = specialized_wireframe_pipeline_cache->map[view.retained_view_entity];
        for (auto&& [render_entity, visible_entity] : visible_entities.template iter<mesh::Mesh2d>()) {
            (void)render_entity;
            if (!render_wireframe_instances->instances.contains(visible_entity)) continue;
            const auto mesh_instance_it = render_mesh_instances->instances.find(visible_entity);
            if (mesh_instance_it == render_mesh_instances->instances.end()) continue;
            const auto entity_tick_it = entity_specialization_ticks->entities.find(visible_entity);
            if (entity_tick_it == entity_specialization_ticks->entities.end()) continue;

            const auto cached_it         = view_cache.map.find(visible_entity);
            const bool needs_specialization = cached_it == view_cache.map.end() ||
                                              view_tick.newer_than(cached_it->second.first, ticks.this_run()) ||
                                              entity_tick_it->second.newer_than(cached_it->second.first,
                                                                                ticks.this_run());
            if (!needs_specialization) continue;

            const auto* mesh = render_meshes->try_get(mesh_instance_it->second.mesh_asset_id);
            if (!mesh) continue;

            auto mesh_key = view_key_it->second;
            mesh_key |= Mesh2dPipelineKey::from_primitive_topology(mesh->primitive_topology());
            auto pipeline_id = pipelines->specialize(*pipeline_server, pipeline.get(), mesh_key, mesh->layout);
            if (!pipeline_id) {
                spdlog::error("[wireframe2d] Failed to specialize the wireframe pipeline for entity {:#x}.",
                              visible_entity.id().index);
                continue;
            }
            view_cache.map.insert_or_assign(visible_entity, std::pair{ticks.this_run(), *pipeline_id});
        }
    }
    std::erase_if(specialized_wireframe_pipeline_cache->map,
                  [&all_views](const auto& entry) { return !all_views.contains(entry.first); });
}

void epix::sprite_render::queue_wireframes(
    ecs::Res<Wireframe2dDrawFunction> custom_draw_functions,
    ecs::Res<RenderMesh2dInstances> render_mesh_instances,
    ecs::Res<render::mesh::MeshAllocator> mesh_allocator,
    ecs::Res<SpecializedWireframePipelineCache> specialized_wireframe_pipeline_cache,
    ecs::Res<RenderWireframeInstances> render_wireframe_instances,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Wireframe2dPhaseItem>> wireframe_2d_phases,
    ecs::Query<ecs::Item<const render::view::ExtractedView&, const render::view::RenderVisibleEntities&>> views) {
    // Bevy `queue_wireframes` (`:810-874`). The skip order is Bevy's: phase,
    // view cache, material instance, cached pipeline entry, cached-entity tick,
    // mesh instance.
    for (auto&& [view, visible_entities] : views.iter()) {
        const auto phase_it = wireframe_2d_phases->phases.find(view.retained_view_entity);
        if (phase_it == wireframe_2d_phases->phases.end()) continue;
        const auto cache_it = specialized_wireframe_pipeline_cache->map.find(view.retained_view_entity);
        if (cache_it == specialized_wireframe_pipeline_cache->map.end()) continue;
        const auto& view_cache = cache_it->second;

        for (auto&& [render_entity, visible_entity] : visible_entities.template iter<mesh::Mesh2d>()) {
            const auto material_it = render_wireframe_instances->instances.find(visible_entity);
            if (material_it == render_wireframe_instances->instances.end()) continue;
            const auto pipeline_it = view_cache.map.find(visible_entity);
            if (pipeline_it == view_cache.map.end()) continue;
            const auto [current_change_tick, pipeline_id] = pipeline_it->second;
            if (phase_it->second.validate_cached_entity(visible_entity, current_change_tick)) continue;

            const auto mesh_instance_it = render_mesh_instances->instances.find(visible_entity);
            if (mesh_instance_it == render_mesh_instances->instances.end()) continue;
            const auto& mesh_instance = mesh_instance_it->second;

            const auto [vertex_slab, index_slab] = mesh_allocator->mesh_slabs(mesh_instance.mesh_asset_id);
            const Wireframe2dBatchSetKey batch_set_key{
                .pipeline      = pipeline_id,
                .asset_id      = material_it->second,
                .draw_function = custom_draw_functions->value,
                .vertex_slab   = vertex_slab.value_or(render::mesh::SlabId{}),
                .index_slab    = index_slab,
            };
            const Wireframe2dBinKey bin_key{.asset_id = mesh_instance.mesh_asset_id};
            const auto phase_type = mesh_instance.automatic_batching
                                        ? render::phase::BinnedRenderPhaseType::BatchableMesh
                                        : render::phase::BinnedRenderPhaseType::UnbatchableMesh;
            phase_it->second.add(batch_set_key, bin_key, std::pair{render_entity, visible_entity},
                                 render::phase::InputUniformIndex{}, phase_type, current_change_tick);
        }
    }
}

void epix::sprite_render::Wireframe2dPlugin::attach(app::App& app) {
    // Bevy `Plugin for Wireframe2dPlugin::build` (`:80-155`).
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>()) {
        registry->get().insert_asset_static(kWireframe2dShaderPath, shader_bytes(kWireframe2dShader));
    } else {
        spdlog::warn("[wireframe2d] EmbeddedAssetRegistry is unavailable; the wireframe shader was not registered.");
    }

    app.add_plugins(render::phase::BinnedRenderPhasePlugin<Wireframe2dPhaseItem, Mesh2dPipeline>{debug_flags})
        .add_plugins(render::RenderAssetPlugin<RenderWireframeMaterial>{});
    assets::app_register_asset<Wireframe2dMaterial>(app);

    auto& world = app.world_mut();
    world.init_resource<Wireframe2dConfig>();
    world.init_resource<WireframeEntitiesNeedingSpecialization>();

    app.add_systems(app::Startup,
                    ecs::into(setup_global_wireframe_material).set_name("setup global wireframe material"));
    app.add_systems(app::Update,
                    ecs::into(global_color_changed, wireframe_color_changed)
                        .set_names(std::array{"global wireframe color changed", "wireframe color changed"}));
    app.add_systems(app::Update,
                    ecs::into(apply_wireframe_material).set_name("apply wireframe material"));
    app.add_systems(app::Update,
                    ecs::into(apply_global_wireframe_material)
                        .after(apply_wireframe_material)
                        .set_name("apply global wireframe material"));
    app.add_systems(app::PostUpdate,
                    ecs::into(check_wireframe_entities_needing_specialization)
                        .after(assets::AssetSystems::WriteEvents)
                        .set_name("check wireframe entities needing specialization"));
}

void epix::sprite_render::Wireframe2dPlugin::ready(app::App& app) {
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error("[wireframe2d] Wireframe2dPlugin requires render::RenderPlugin to be added before it.");
        return;
    }
    auto& render = render_app->get();

    render.world_mut().init_resource<WireframeEntitySpecializationTicks>();
    render.world_mut().init_resource<SpecializedWireframePipelineCache>();
    render.world_mut().init_resource<RenderWireframeInstances>();
    render.world_mut().init_resource<render::SpecializedMeshPipelines<Wireframe2dPipeline>>();
    // Bevy `init_resource::<DrawFunctions<Wireframe2dPhaseItem>>()` (`:117`).
    render.world_mut().init_resource<render::phase::DrawFunctions<Wireframe2dPhaseItem>>();

    // Bevy `add_render_command::<Wireframe2dPhaseItem, DrawWireframe2d>`
    // (`:118`): the sequence is `SetItemPipeline`, the view and mesh bind
    // groups, the colour push constants and `DrawMesh2d`.
    render.world_mut().insert_resource(Wireframe2dDrawFunction{
        .value = render::phase::app_add_render_commands<Wireframe2dPhaseItem, render::phase::SetItemPipeline,
                                                        SetMesh2dViewBindGroup<0>::Command,
                                                        SetMesh2dBindGroup<1>::Command,
                                                        SetWireframe2dPushConstants::Command, DrawMesh2d>(render)});

    // Bevy `add_render_graph_node::<ViewNodeRunner<Wireframe2dNode>>(Core2d,
    // Node2d::Wireframe)` with `EndMainPass -> Wireframe -> PostProcessing`
    // (`:121-129`).
    if (auto render_graph = render.world_mut().get_resource_mut<render::graph::RenderGraph>()) {
        if (auto graph = render_graph->get().get_sub_graph(core_graph::core_2d::Core2d)) {
            auto& render_world = render.world_mut();
            graph->get().add_node(core_graph::core_2d::Core2dNodes::Wireframe,
                                  render::graph::ViewNodeRunner{Wireframe2dNode{}, render_world});
            (void)graph->get().try_add_node_edge(core_graph::core_2d::Core2dNodes::EndMainPass,
                                                 core_graph::core_2d::Core2dNodes::Wireframe);
            (void)graph->get().try_add_node_edge(core_graph::core_2d::Core2dNodes::Wireframe,
                                                 core_graph::core_2d::Core2dNodes::PostProcessing);
        }
    }

    render.add_systems(render::RenderStartup,
                       ecs::into(init_wireframe_2d_pipeline)
                           .after(init_mesh_2d_pipeline)
                           .set_name("init wireframe 2d pipeline"));
    render.add_systems(render::ExtractSchedule,
                       ecs::into(extract_wireframe_2d_camera, extract_wireframe_entities_needing_specialization,
                                 extract_wireframe_materials)
                           .set_names(std::array{"extract wireframe 2d camera",
                                                 "extract wireframe entities needing specialization",
                                                 "extract wireframe materials"}));
    render.add_systems(render::Render,
                       ecs::into(specialize_wireframes)
                           .in_set(render::RenderSystems::PrepareMeshes)
                           .set_name("specialize wireframes"));
    render.add_systems(render::Render,
                       ecs::into(queue_wireframes)
                           .in_set(render::RenderSystems::QueueMeshes)
                           .set_name("queue wireframes"));
}
