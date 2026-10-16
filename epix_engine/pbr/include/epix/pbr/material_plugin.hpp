#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <epix/core_graph/core3d.hpp>
#include <epix/ecs.hpp>
#include <epix/render/erased_render_asset.hpp>
#include <epix/render/render_phase.hpp>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_instance.hpp>

namespace epix::render::erased_render_asset {

using ::epix::render::render_resource::AsBindGroup;
using ::epix::render::render_resource::AsBindGroupImpl;
using ::epix::render::render_resource::RetryBindGroupNextUpdate;

/** @brief Preparing one 3D material's GPU data (Bevy's `ErasedRenderAsset for
 *  `MeshMaterial3d<M>`, `material.rs:1611-1861`).
 *
 *  It lives beside `MaterialPlugin` rather than in `material.hpp` because the last thing
 *  Bevy's prepare does is resolve the phase draw-function ids by name - `id::<DrawMaterial>()`
 *  and its siblings - and those sequences are declared in `mesh_instance.hpp`, which includes
 *  `material.hpp`. Putting the two here is what keeps that a one-way dependency; the file is
 *  the port's counterpart of the half of Bevy's `material.rs` that needs the mesh commands.
 *
 *  The bind group is allocated through the per-material-type allocator and the result
 *  recorded in the untyped `RenderMaterialBindings`, which is what the mesh builder and the
 *  material draw command read - a prepared material holds only a binding *id*, never the
 *  group. Re-preparing a material frees and reallocates rather than updating in place, as
 *  Bevy does (its own TODO).
 *
 *  One member is still filled only in part: Bevy resolves the six shader handles here too,
 *  and the port has not ported the shader labels or their loads, so `shaders` stays empty
 *  (PBR-66). */
template <typename M>
    requires std::derived_from<M, ::epix::pbr::Material<M>> && AsBindGroupImpl<M>
struct ErasedRenderAsset<::epix::pbr::MeshMaterial3d<M>> {
    using SourceAsset    = M;
    using ExtractedAsset = M;
    using ErasedAsset    = ::epix::pbr::PreparedMaterial;
    using Param          = std::tuple<
        ecs::Res<wgpu::Device>, ecs::Res<PipelineServer>, ecs::Res<::epix::pbr::DefaultOpaqueRendererMethod>,
        ecs::ResMut<::epix::pbr::MaterialBindGroupAllocators>, ecs::ResMut<::epix::pbr::RenderMaterialBindings>,
        // The phases a material can be drawn in, in Bevy's order (`material.rs:1644-1654`).
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::Opaque3d>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::AlphaMask3d>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::Transmissive3d>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::Transparent3d>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::Opaque3dPrepass>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::AlphaMask3dPrepass>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::Opaque3dDeferred>>,
        ecs::Res<phase::DrawFunctions<core_graph::core_3d::AlphaMask3dDeferred>>,
        typename AsBindGroup<M>::Param>;

    static std::expected<::epix::pbr::PreparedMaterial, PrepareAssetError<M>> prepare_asset(
        M&& material, const assets::AssetId<M>& material_id, Param& params, const ::epix::pbr::PreparedMaterial*) {
        auto&& [device, pipeline_server, default_opaque_render_method, bind_group_allocators,
                render_material_bindings, opaque_draw_functions, alpha_mask_draw_functions,
                transmissive_draw_functions, transparent_draw_functions, opaque_prepass_draw_functions,
                alpha_mask_prepass_draw_functions, opaque_deferred_draw_functions, alpha_mask_deferred_draw_functions,
                material_param] = params;

        using ::epix::pbr::Material;
        using ::epix::pbr::MeshPipelineKey;
        using ::epix::pbr::OpaqueRendererMethod;
        using ::epix::pbr::RenderPhaseType;

        const bool shadows_enabled = M::enable_shadows();
        const bool prepass_enabled = M::enable_prepass();

        const OpaqueRendererMethod render_method = material.opaque_render_method() == OpaqueRendererMethod::Auto
                                                       ? default_opaque_render_method->method()
                                                       : material.opaque_render_method();

        // The one mesh-pipeline bit a material contributes itself.
        const bool reads_view_transmission_texture = material.reads_view_transmission_texture();
        MeshPipelineKey mesh_pipeline_key_bits     = MeshPipelineKey::NONE;
        if (reads_view_transmission_texture) {
            mesh_pipeline_key_bits |= MeshPipelineKey::READS_VIEW_TRANSMISSION_TEXTURE;
        }

        RenderPhaseType render_phase_type = RenderPhaseType::Opaque;
        switch (material.alpha_mode().type) {
            case render::AlphaMode::Type::Blend:
            case render::AlphaMode::Type::Premultiplied:
            case render::AlphaMode::Type::Add:
            case render::AlphaMode::Type::Multiply: render_phase_type = RenderPhaseType::Transparent; break;
            case render::AlphaMode::Type::Opaque:
            case render::AlphaMode::Type::AlphaToCoverage:
                render_phase_type =
                    reads_view_transmission_texture ? RenderPhaseType::Transmissive : RenderPhaseType::Opaque;
                break;
            case render::AlphaMode::Type::Mask: render_phase_type = RenderPhaseType::AlphaMask; break;
        }

        // Bevy looks each id up under the name the phase's own `DrawFunctions` registered it
        // with (`material.rs:1662-1672`), which is why the sequences are the lookup key and
        // why a material can be prepared only once the phase plugins have run. `id` throws
        // where Bevy panics, for the same reason: a phase whose sequence is missing cannot
        // draw the material that asked for it.
        const auto draw_opaque = opaque_draw_functions->read()
                                     ->template id<::epix::pbr::DrawMaterial<core_graph::core_3d::Opaque3d>>();
        const auto draw_alpha_mask = alpha_mask_draw_functions->read()
                                         ->template id<::epix::pbr::DrawMaterial<core_graph::core_3d::AlphaMask3d>>();
        const auto draw_transmissive =
            transmissive_draw_functions->read()
                ->template id<::epix::pbr::DrawMaterial<core_graph::core_3d::Transmissive3d>>();
        const auto draw_transparent =
            transparent_draw_functions->read()
                ->template id<::epix::pbr::DrawMaterial<core_graph::core_3d::Transparent3d>>();
        const auto draw_opaque_prepass =
            opaque_prepass_draw_functions->read()
                ->template id<::epix::pbr::DrawPrepass<core_graph::core_3d::Opaque3dPrepass>>();
        const auto draw_alpha_mask_prepass =
            alpha_mask_prepass_draw_functions->read()
                ->template id<::epix::pbr::DrawPrepass<core_graph::core_3d::AlphaMask3dPrepass>>();
        const auto draw_opaque_deferred =
            opaque_deferred_draw_functions->read()
                ->template id<::epix::pbr::DrawPrepass<core_graph::core_3d::Opaque3dDeferred>>();
        const auto draw_alpha_mask_deferred =
            alpha_mask_deferred_draw_functions->read()
                ->template id<::epix::pbr::DrawPrepass<core_graph::core_3d::AlphaMask3dDeferred>>();

        const BindGroupLayoutDescriptor material_layout =
            BindGroupLayoutDescriptor()
                .set_label(std::string(AsBindGroup<M>::label()))
                .set_entries(AsBindGroup<M>::layout_entries());
        const wgpu::BindGroupLayout bind_group_layout = pipeline_server->get_bind_group_layout(material_layout);

        auto unprepared = AsBindGroup<M>::unprepared_bind_group(device.get(), bind_group_layout, material,
                                                                material_param);
        if (!unprepared) {
            auto error = std::move(unprepared.error());
            if (std::holds_alternative<RetryBindGroupNextUpdate>(error)) {
                return std::unexpected(PrepareAssetError<M>{RetryNextUpdate<M>{std::move(material)}});
            }
            return std::unexpected(PrepareAssetError<M>{std::move(error)});
        }

        // Bevy unwraps here: a material whose type was never registered cannot be prepared.
        ::epix::pbr::MaterialBindGroupAllocator* allocator =
            bind_group_allocators->get(::epix::meta::type_id<M>());
        assert(allocator != nullptr);

        const assets::UntypedAssetId untyped_id{material_id};
        ::epix::pbr::MaterialBindingId binding{};
        const auto existing = render_material_bindings->bindings.find(untyped_id);
        if (existing != render_material_bindings->bindings.end()) {
            allocator->free(existing->second);
            binding          = allocator->allocate_unprepared(std::move(*unprepared), material_layout);
            existing->second = binding;
        } else {
            binding = allocator->allocate_unprepared(std::move(*unprepared), material_layout);
            render_material_bindings->bindings.insert_or_assign(untyped_id, binding);
        }

        auto properties                             = std::make_shared<::epix::pbr::MaterialProperties>();
        properties->render_method                   = render_method;
        properties->alpha_mode                      = material.alpha_mode();
        properties->mesh_pipeline_key_bits          = mesh_pipeline_key_bits;
        properties->depth_bias                      = material.depth_bias();
        properties->reads_view_transmission_texture = reads_view_transmission_texture;
        properties->render_phase_type               = render_phase_type;
        // Bevy builds the erased key from the material's own bind-group data
        // (`material.rs:1761`), which is what makes the specialization key material-specific.
        properties->material_key = ::epix::pbr::ErasedMaterialKey(AsBindGroup<M>::bind_group_data(material));
        properties->material_layout                 = material_layout;
        properties->bindless                        = ::epix::pbr::material_uses_bindless_resources<M>(device.get());
        properties->shadows_enabled                 = shadows_enabled;
        properties->prepass_enabled                 = prepass_enabled;
        properties->draw_functions                  = {
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::MainPassOpaqueDrawFunction{}}),
              draw_opaque},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::MainPassAlphaMaskDrawFunction{}}),
              draw_alpha_mask},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::MainPassTransmissiveDrawFunction{}}),
              draw_transmissive},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::MainPassTransparentDrawFunction{}}),
              draw_transparent},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::PrepassOpaqueDrawFunction{}}),
              draw_opaque_prepass},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::PrepassAlphaMaskDrawFunction{}}),
              draw_alpha_mask_prepass},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::DeferredOpaqueDrawFunction{}}),
              draw_opaque_deferred},
            {ecs::intern(render::phase::DrawFunctionLabel{::epix::pbr::DeferredAlphaMaskDrawFunction{}}),
              draw_alpha_mask_deferred},
        };

        return ::epix::pbr::PreparedMaterial{.binding = binding, .properties = std::move(properties)};
    }
};

}  // namespace epix::render::erased_render_asset

namespace epix::pbr {

/** @brief Registers one 3D material type (Bevy `MaterialPlugin<M>`,
 *  `bevy_pbr/src/material.rs:339-404`).
 *
 *  It lives with the material's `ErasedRenderAsset` specialization because it is what
 *  installs it, and that specialization resolves the phase draw-function ids by name - see
 *  the note there for why the two cannot live in `material.hpp`.
 *
 *  What is here is the half the port has: the material asset is registered, and the registry
 *  plus the three extraction and sweep systems are wired in Bevy's order - extraction inside
 *  `MaterialExtractionSystems`, the early sweep after it and before the late sweep, which is
 *  what lets a material of one type be replaced by another in the same frame.
 *
 *  What is **not** here is the shadow half (`specialize_shadows`, `queue_shadows`,
 *  `DrawFunctions<Shadow>`), which is unported. The globals every material type shares -
 *  `RenderMaterialInstances`, `MaterialBindGroupAllocators`, the two specialization caches,
 *  the material pipeline resource and the pair that builds and uploads the allocators' bind
 *  groups - live in `MaterialsPlugin` (`material.rs:276-335`), which `PbrPlugin` installs,
 *  and the bindings map lives in `PbrPlugin` itself (`lib.rs:343`), as Bevy has both. */
EPIX_EXPORT template <typename M>
    requires std::derived_from<M, Material<M>>
struct MaterialPlugin {
    void attach(app::App& app) {
        assets::app_register_asset<M>(app);
        // Bevy's `build` installs the prepass pipeline plugin beside the material plugin
        // (`material.rs:283`), which is what registers the prepass draw sequence. Like Bevy's,
        // that plugin returns early when no render sub-app exists yet, so a material plugin
        // must follow `RenderPlugin`.
        app.add_plugins(PrepassPipelinePlugin{});
        // Bevy registers the material through the erased render-asset pipeline in the same
        // plugin build: that is what prepares a material's bind group and records it in the
        // untyped bindings map. Guarded for the same reason the allocator registration is -
        // this plugin is instantiated for materials that specialize nothing, and those have
        // no bind group to prepare.
        if constexpr (render::render_resource::AsBindGroupImpl<M>) {
            app.add_plugins(render::erased_render_asset::ErasedRenderAssetPlugin<MeshMaterial3d<M>>{});
        }
        // Bevy initialises the specialization queue in `build` too; without it the
        // PostUpdate system that fills it fails validation and the queue stays empty,
        // which is what the extraction then has nothing to read.
        app.world_mut().init_resource<EntitiesNeedingSpecialization<M>>();

        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) return;

        // Bevy registers the allocator unconditionally, because every Bevy material derives
        // `AsBindGroup` and so has the label, the layout entries and the bindless members.
        // This port's `MaterialPlugin` is instantiated for materials that specialize nothing
        // - its own tests do - so the system is registered only where those pieces exist.
        // Constraining the plugin itself, as Bevy's supertrait does, is the faithful
        // alternative and is recorded as remaining work rather than assumed here.
        if constexpr (render::render_resource::AsBindGroupImpl<M>) {
            render_app->get().add_systems(
                render::RenderStartup,
                ecs::into(add_material_bind_group_allocator<M>).set_name("add material bind group allocator"));
        }

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

        // Bevy orders this set's systems after the mesh extraction and before the sweep;
        // the set has to be registered for the ordering to apply.
        render_app->get().configure_sets(app::ScheduleInfo(render::ExtractSchedule),
                                         ecs::sets(MaterialExtractEntitiesNeedingSpecializationSystems::Extract));
        render_app->get().add_systems(
            app::ScheduleInfo(render::ExtractSchedule),
            ecs::into(extract_entities_needs_specialization<M>)
                .in_set(MaterialExtractEntitiesNeedingSpecializationSystems::Extract)
                .set_name("extract entities needing material specialization"));
        render_app->get().add_systems(app::ScheduleInfo(render::ExtractSchedule),
                                      ecs::into(sweep_entities_needing_specialization<M>)
                                          .after(MaterialExtractEntitiesNeedingSpecializationSystems::Extract)
                                          .set_name("sweep entities needing material specialization"));

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

}  // namespace epix::pbr
