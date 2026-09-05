#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <format>
#include <optional>
#include <ranges>
#include <vector>
#endif
#include <epix/render/render_resource.hpp>
#include <epix/render/schedule.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/view.hpp>

namespace epix::render {
/** @brief Schedule sentinel for the extract phase that copies data from
 * the main world into the render world. */
EPIX_EXPORT inline struct ExtractScheduleT {
} ExtractSchedule;
template <std::copyable T>
void extract_fn(
    epix::ecs::Commands cmd,
    epix::app::Extract<std::optional<epix::ecs::Res<T>>> main_resource,
    std::optional<epix::ecs::ResMut<T>> target_resource) {
    if (!main_resource) return;
    if (!target_resource) {
        cmd.insert_resource(main_resource->get());
    } else if (main_resource->is_modified()) {
        target_resource->get_mut() = main_resource->get();
    }
}

/** @brief Marker component indicating an entity has a custom rendering
 * process and should be skipped by standard render pipelines. */
EPIX_EXPORT struct CustomRendered {};

/**
 * @brief Trait specialization pattern for component extraction (Bevy
 * `ExtractComponent`). Specialize `ExtractComponent<C>` for a component and
 * define `QueryData` (read-only query item) and `Out` (the extracted
 * component/bundle), plus the static `extract_component` function.
 */
template <typename C>
struct ExtractComponent;

/** @brief Concept satisfied by valid `ExtractComponent` specializations. */
template <typename C>
concept ExtractComponentImpl = requires {
    typename ExtractComponent<C>::QueryData;
    typename ExtractComponent<C>::QueryFilter;
    requires ecs::query_filter<typename ExtractComponent<C>::QueryFilter>;
    typename ExtractComponent<C>::Out;
    requires std::movable<typename ExtractComponent<C>::Out>;
    requires std::same_as<decltype(ExtractComponent<C>::extract_component(
                              std::declval<typename ExtractComponent<C>::QueryData>())),
                          std::optional<typename ExtractComponent<C>::Out>>;
};

/** @brief System that runs `ExtractComponent<C>::extract_component` for every
 * synchronized main-world entity and inserts the result into the render world
 * at the synced render entity (Bevy `extract_sync_component`). Only entities
 * with a `RenderEntity` mapping (i.e. synced via SyncWorldPlugin) are
 * extracted. */
template <ExtractComponentImpl C>
void extract_component_system(
    ecs::Commands cmd,
    ecs::Local<std::size_t> previous_len,
    app::Extract<ecs::Query<ecs::Item<const sync_world::RenderEntity&, typename ExtractComponent<C>::QueryData>>>
        query) {
    std::vector<std::pair<ecs::Entity, typename ExtractComponent<C>::Out>> values;
    values.reserve(*previous_len);
    for (auto&& [render_entity, item] : query.iter()) {
        auto out = ExtractComponent<C>::extract_component(item);
        if (out) {
            values.emplace_back(render_entity.id(), std::move(*out));
        } else {
            // Bevy removes the previously-extracted component when extraction
            // returns None (extract_component.rs:208-212).
            cmd.entity(render_entity.id()).template remove<typename ExtractComponent<C>::Out>();
        }
    }
    *previous_len = values.size();
    cmd.try_insert_batch(std::move(values));
}

/** @brief System that extracts component C only for entities visible to at
 * least one view (Bevy extract_visible_components, extract_component.rs:219-236). */
template <ExtractComponentImpl C>
void extract_visible_components_system(ecs::Commands cmd, ecs::Local<std::size_t> previous_len,
                                       app::Extract<ecs::Query<ecs::Item<const sync_world::RenderEntity&,
                                                                         const ::epix::camera::ViewVisibility&,
                                                                         typename ExtractComponent<C>::QueryData>,
                                                               typename ExtractComponent<C>::QueryFilter>> query) {
    std::vector<std::pair<ecs::Entity, typename ExtractComponent<C>::Out>> values;
    values.reserve(*previous_len);
    for (auto&& [render_entity, view_visibility, item] : query.iter()) {
        if (!view_visibility.get()) continue;
        auto out = ExtractComponent<C>::extract_component(item);
        if (out) {
            values.emplace_back(render_entity.id(), std::move(*out));
        } else {
            cmd.entity(render_entity.id()).template remove<typename ExtractComponent<C>::Out>();
        }
    }
    *previous_len = values.size();
    cmd.try_insert_batch(std::move(values));
}

/** @brief Plugin that synchronizes a component's presence with the render world
 * (Bevy `SyncComponentPlugin<C>`). Adds `SyncToRenderWorld` as a required
 * component of `C` and records removals for render-entity resync. */
template <typename C>
struct SyncComponentPlugin {
    void attach(app::App& app) {
        app.world_mut().register_required_components_with<C>([] { return sync_world::SyncToRenderWorld{}; });
        // Bevy on_remove hook (sync_component.rs:35-40): record removed
        // components so entity_sync_system clears derived render data.
        app.world_mut().init_resource<sync_world::PendingSyncEntity>();
        // Epix component hooks are registered on the component metadata; this
        // is the direct counterpart of Bevy's register_component_hooks().
        app.world_mut().registrator().register_on_remove_hook<C>(sync_world::record_component_removed);
    }
};

/** @brief Plugin that registers the extract system for component C (Bevy
 * ExtractComponentPlugin<C>). only_extract_visible selects
 * extract_visible_components (Bevy ExtractComponentPlugin::extract_visible()). */
template <ExtractComponentImpl C>
struct ExtractComponentPlugin {
   private:
    /** @brief Only extract entities visible to at least one view when true. */
    bool only_extract_visible = false;

   public:

    /** @brief Returns a plugin configured to extract only visible entities
     * (Bevy ExtractComponentPlugin::extract_visible()). */
    static ExtractComponentPlugin extract_visible() {
        ExtractComponentPlugin plugin;
        plugin.only_extract_visible = true;
        return plugin;
    }

    void attach(app::App& app) {
        // Bevy auto-registers SyncComponentPlugin so entities with C are
        // synced to the render world (extract_component.rs:188).
        app.add_plugins(SyncComponentPlugin<C>{});
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        if (only_extract_visible) {
            render_app->get().add_systems(ExtractSchedule, into(extract_visible_components_system<C>)
                                                               .set_name(std::format("extract visible components '{}'",
                                                                                     meta::type_id<C>().short_name())));
        } else {
            render_app->get().add_systems(
                ExtractSchedule, into(extract_component_system<C>)
                                     .set_name(std::format("extract component '{}'", meta::type_id<C>().short_name())));
        }
    }
};

/**
 * @brief Trait specialization pattern for high-performance instance extraction
 * (Bevy `ExtractInstance`). Specialize `ExtractInstance<EI>` with
 * `QueryData` and the static `extract` function.
 */
template <typename EI>
struct ExtractInstance;

/** @brief Concept satisfied by valid `ExtractInstance` specializations
 * (Bevy ExtractInstance has QueryData + QueryFilter). */
template <typename EI>
concept ExtractInstanceImpl = requires {
    typename ExtractInstance<EI>::QueryData;
    typename ExtractInstance<EI>::QueryFilter;
    requires ecs::query_filter<typename ExtractInstance<EI>::QueryFilter>;
    requires std::same_as<decltype(ExtractInstance<EI>::extract(
                              std::declval<typename ExtractInstance<EI>::QueryData>())),
                          std::optional<EI>>;
};

/** @brief Resource storing all extracted instances of type `EI` keyed by main
 * entity (Bevy `ExtractedInstances<EI>`). */
template <ExtractInstanceImpl EI>
struct ExtractedInstances {
    sync_world::MainEntityHashMap<EI> instances;

    void clear() noexcept { instances.clear(); }
    void insert(ecs::Entity entity, EI value) { instances.emplace(sync_world::MainEntity{entity}, std::move(value)); }
    bool contains(ecs::Entity entity) const { return instances.contains(sync_world::MainEntity{entity}); }
    const EI* get(ecs::Entity entity) const {
        if (auto it = instances.find(sync_world::MainEntity{entity}); it != instances.end()) return &it->second;
        return nullptr;
    }
    EI* get_mut(ecs::Entity entity) {
        if (auto it = instances.find(sync_world::MainEntity{entity}); it != instances.end()) return &it->second;
        return nullptr;
    }
    /** @brief Lazily iterate the extracted main-entity/instance pairs. This
     * is the C++ range counterpart of Bevy's `Deref<Target =
     * MainEntityHashMap<EI>>`. */
    auto iter() const noexcept { return std::views::all(instances); }
    /** @brief Lazily iterate mutable extracted main-entity/instance pairs. */
    auto iter_mut() noexcept { return std::views::all(instances); }
};

template <ExtractInstanceImpl EI>
void extract_all_instances(ecs::ResMut<ExtractedInstances<EI>> extracted_instances,
                           app::Extract<ecs::Query<ecs::Item<ecs::Entity, typename ExtractInstance<EI>::QueryData>,
                                                   typename ExtractInstance<EI>::QueryFilter>> query);

template <ExtractInstanceImpl EI>
void extract_visible_instances(
    ecs::ResMut<ExtractedInstances<EI>> extracted_instances,
    app::Extract<ecs::Query<
        ecs::Item<ecs::Entity, const ::epix::camera::ViewVisibility&, typename ExtractInstance<EI>::QueryData>,
        typename ExtractInstance<EI>::QueryFilter>> query);

/** @brief Plugin that extracts instances of `EI` into `ExtractedInstances<EI>`
 * each frame (Bevy `ExtractInstancesPlugin<EI>`). */
template <ExtractInstanceImpl EI>
struct ExtractInstancesPlugin {
   private:
    /** @brief Only extract entities marked visible when true. */
    bool only_extract_visible = false;

   public:

    /** @brief Construct the visible-only extractor (Bevy
     * `ExtractInstancesPlugin::extract_visible`). */
    static ExtractInstancesPlugin extract_visible() {
        ExtractInstancesPlugin plugin;
        plugin.only_extract_visible = true;
        return plugin;
    }

    void attach(app::App& app) {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        auto& render_world = render_app->get();
        render_world.world_mut().init_resource<ExtractedInstances<EI>>();
        if (only_extract_visible) {
            render_world.add_systems(ExtractSchedule, into(extract_visible_instances<EI>)
                                                          .set_name(std::format("extract visible instances '{}'",
                                                                                meta::type_id<EI>().short_name())));
        } else {
            render_world.add_systems(ExtractSchedule, into(extract_all_instances<EI>)
                                                          .set_name(std::format("extract instances '{}'",
                                                                                meta::type_id<EI>().short_name())));
        }
    }
};

template <ExtractInstanceImpl EI>
void extract_all_instances(ecs::ResMut<ExtractedInstances<EI>> extracted_instances,
                           app::Extract<ecs::Query<ecs::Item<ecs::Entity, typename ExtractInstance<EI>::QueryData>,
                                                   typename ExtractInstance<EI>::QueryFilter>> query) {
    extracted_instances->clear();
    for (auto&& [entity, item] : query.iter()) {
        if (auto value = ExtractInstance<EI>::extract(item)) {
            extracted_instances->insert(entity, std::move(*value));
        }
    }
}

template <ExtractInstanceImpl EI>
void extract_visible_instances(
    ecs::ResMut<ExtractedInstances<EI>> extracted_instances,
    app::Extract<ecs::Query<
        ecs::Item<ecs::Entity, const ::epix::camera::ViewVisibility&, typename ExtractInstance<EI>::QueryData>,
        typename ExtractInstance<EI>::QueryFilter>> query) {
    // Bevy extract_visible_instances (extract_instances.rs:122-131): only
    // entities visible to at least one view are extracted.
    extracted_instances->clear();
    for (auto&& [entity, view_visibility, item] : query.iter()) {
        if (!view_visibility.get()) continue;
        if (auto value = ExtractInstance<EI>::extract(item)) {
            extracted_instances->insert(entity, std::move(*value));
        }
    }
}

/**
 * @brief Trait specialization pattern for resource extraction with a custom
 * source type (Bevy `ExtractResource`). Specialize `ExtractResource<R>` with
 * `Source` and the static `extract_resource` function.
 */
template <typename R>
struct ExtractResource;

/** @brief Concept satisfied by valid `ExtractResource` specializations. */
template <typename R>
concept ExtractResourceImpl = requires {
    typename ExtractResource<R>::Source;
    requires std::same_as<
        decltype(ExtractResource<R>::extract_resource(std::declval<const typename ExtractResource<R>::Source&>())), R>;
};

template <ExtractResourceImpl R>
void extract_resource_system(
    ecs::Commands cmd,
    app::Extract<std::optional<ecs::Res<typename ExtractResource<R>::Source>>> source,
    std::optional<ecs::ResMut<R>> target) {
    if (!source) return;
    if (target) {
        // Bevy only overwrites the render-world copy when the source changed
        // this frame (extract_resource.rs:59-66).
        if (source->is_modified()) {
            target.value().get_mut() = ExtractResource<R>::extract_resource(source->get());
        }
    } else {
        // Bevy always (re)inserts when the render-world target is missing,
        // regardless of change, so a manually removed resource reappears
        // (extract_resource.rs:67-69).
        cmd.insert_resource(ExtractResource<R>::extract_resource(source->get()));
    }
}

/** @brief Plugin that extracts resource R from the main world into the render
 * world (Bevy ExtractResourcePlugin<R>, extract_resource.rs:25-44). When
 * ExtractResource<R> is specialized, the trait-based system runs; otherwise a
 * copyable-identity system is used (epix convenience). */
EPIX_EXPORT template <typename R>
struct ExtractResourcePlugin {
    void attach(epix::app::App& app) {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        if constexpr (requires { typename ExtractResource<R>::Source; }) {
            render_app->get().add_systems(
                ExtractSchedule, into(extract_resource_system<R>)
                                     .set_name(std::format("extract resource '{}'", meta::type_id<R>().short_name())));
        } else {
            render_app->get().add_systems(
                ExtractSchedule,
                into(extract_fn<R>).set_name(std::format("extract resource '{}'", meta::type_id<R>().short_name())));
        }
    }
};

/**
 * @brief Component storing the index of a component's uniform inside
 * `ComponentUniforms<C>` (Bevy `DynamicUniformIndex<C>`).
 */
template <typename C>
struct DynamicUniformIndex {
   private:
    std::uint32_t m_index = 0;

   public:
    explicit DynamicUniformIndex(std::uint32_t index = 0) noexcept : m_index(index) {}
    /** @brief The stored uniform index. */
    std::uint32_t index() const noexcept { return m_index; }
};

/**
 * @brief Resource holding all uniforms of a component type in a dynamic
 * uniform buffer (Bevy `ComponentUniforms<C>`).
 */
template <render_resource::ShaderWritable C>
struct ComponentUniforms {
   private:
    /** @brief Dynamic uniform buffer containing one entry per entity. */
    render_resource::DynamicUniformBuffer<C> m_uniforms;

   public:
    /** @brief Access the underlying dynamic uniform buffer (Bevy
     * `ComponentUniforms::uniforms`). */
    const render_resource::DynamicUniformBuffer<C>& uniforms() const noexcept { return m_uniforms; }
    /** @brief Access the underlying dynamic uniform buffer. */
    render_resource::DynamicUniformBuffer<C>& uniforms_mut() noexcept { return m_uniforms; }
};

/**
 * @brief System that writes every render-world `C` component into
 * `ComponentUniforms<C>` and inserts a `DynamicUniformIndex<C>` on each
 * entity (Bevy `prepare_uniform_components`).
 */
/** @brief System that pushes every render-world C component into a
 * GpuArrayBuffer<C> and inserts a GpuArrayBufferIndex<C> on each entity
 * (Bevy prepare_gpu_component_array_buffers,
 * gpu_component_array_buffer.rs:43-59). */
template <render_resource::GpuArrayBufferable C>
void prepare_gpu_component_array_buffers(ecs::Commands cmd,
                                         ecs::Res<wgpu::Device> device,
                                         ecs::Res<wgpu::Queue> queue,
                                         ecs::ResMut<render_resource::GpuArrayBuffer<C>> gpu_array_buffer,
                                         ecs::Query<ecs::Item<ecs::Entity, const C&>> components) {
    gpu_array_buffer->clear();
    for (auto&& [entity, component] : components.iter()) {
        auto index = gpu_array_buffer->push(component);
        cmd.entity(entity).insert(index);
    }
    gpu_array_buffer->write_buffer(device.get(), queue.get());
}

/**
 * @brief Plugin that prepares all C components as GPU array-buffer entries
 * each frame (Bevy GpuComponentArrayBufferPlugin<C>,
 * gpu_component_array_buffer.rs:16-41).
 */
template <render_resource::GpuArrayBufferable C>
struct GpuComponentArrayBufferPlugin {
    void attach(app::App& app) {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        render_app->get().add_systems(Render, into(prepare_gpu_component_array_buffers<C>)
                                                  .in_set(RenderSystems::PrepareResources)
                                                  .set_name(std::format("prepare gpu component array buffers '{}'",
                                                                        meta::type_id<C>().short_name())));
    }

    // This is the equivalent of Bevy's Plugin::finish: the render device is
    // initialized after all plugins attach, so create the device-dependent
    // backing resource only once that phase has completed.
    void ready(app::App& app) {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        wgpu::Limits limits;
        auto device = render_app->get().world().get_resource<wgpu::Device>();
        if (device) device->get().getLimits(&limits);
        render_app->get().world_mut().insert_resource(render_resource::GpuArrayBuffer<C>(limits));
    }
};

template <render_resource::ShaderWritable C>
void prepare_uniform_components(ecs::Commands cmd,
                                ecs::Res<wgpu::Device> device,
                                ecs::Res<wgpu::Queue> queue,
                                ecs::ResMut<ComponentUniforms<C>> component_uniforms,
                                ecs::Query<ecs::Item<ecs::Entity, const C&>> components) {
    auto& uniforms = component_uniforms->uniforms_mut();
    const std::size_t count = components.iter().max_remaining();
    if (count == 0) {
        // Bevy get_writer returns None when there is no GPU buffer and
        // max_count is 0 (uniform_buffer.rs:270): nothing to upload, skip.
        return;
    }
    uniforms.clear();
    for (auto&& [entity, component] : components.iter()) {
        // push returns the byte offset (Bevy DynamicUniformBuffer::push);
        // DynamicUniformIndex stores that byte offset. `Item<const C&>` yields
        // the raw component reference (the change-detection `Ref` is unwrapped
        // by fetch), so the value is pushed directly.
        std::size_t offset = uniforms.push(component);
        cmd.entity(entity).insert(DynamicUniformIndex<C>{static_cast<std::uint32_t>(offset)});
    }
    uniforms.write_buffer(device.get(), queue.get());
}

/**
 * @brief Plugin that prepares all `C` components as GPU uniforms each frame
 * (Bevy `UniformComponentPlugin<C>`).
 */
template <render_resource::ShaderWritable C>
struct UniformComponentPlugin {
    void attach(app::App& app) {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        render_app->get().world_mut().insert_resource(ComponentUniforms<C>{});
        render_app->get().add_systems(
            Render, into(prepare_uniform_components<C>)
                        .in_set(RenderSystems::PrepareResources)
                        .set_name(std::format("prepare uniform components '{}'", meta::type_id<C>().short_name())));
    }
};

}  // namespace epix::render
