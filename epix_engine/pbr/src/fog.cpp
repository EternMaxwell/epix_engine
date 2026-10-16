#include <epix/pbr/fog.hpp>

namespace epix::pbr {

glm::vec3 fog_atmospheric_extinction(glm::vec3 extinction_rgb,
                                                        float extinction_alpha,
                                                        float visibility,
                                                        float contrast_threshold) {
    const float e = static_cast<float>(std::numbers::e);
    return glm::vec3(std::pow(1.0f - extinction_rgb.x, e), std::pow(1.0f - extinction_rgb.y, e),
                     std::pow(1.0f - extinction_rgb.z, e)) *
           koschmieder(visibility, contrast_threshold) * std::pow(extinction_alpha, e);
}

GpuFog gpu_fog_from(const DistanceFog& fog) {
    GpuFog gpu;
    gpu.base_color                 = fog.color.linear().vec4();
    gpu.directional_light_color    = fog.directional_light_color.linear().vec4();
    gpu.directional_light_exponent = fog.directional_light_exponent;

    std::visit(
        [&](const auto& falloff) {
            using T = std::decay_t<decltype(falloff)>;
            if constexpr (std::is_same_v<T, FogFalloffLinear>) {
                gpu.mode = GPU_FOG_MODE_LINEAR;
                gpu.be   = glm::vec3(falloff.start, falloff.end, 0.0f);
            } else if constexpr (std::is_same_v<T, FogFalloffExponential>) {
                gpu.mode = GPU_FOG_MODE_EXPONENTIAL;
                gpu.be   = glm::vec3(falloff.density, 0.0f, 0.0f);
            } else if constexpr (std::is_same_v<T, FogFalloffExponentialSquared>) {
                gpu.mode = GPU_FOG_MODE_EXPONENTIAL_SQUARED;
                gpu.be   = glm::vec3(falloff.density, 0.0f, 0.0f);
            } else {
                gpu.mode = GPU_FOG_MODE_ATMOSPHERIC;
                gpu.be   = falloff.extinction;
                gpu.bi   = falloff.inscattering;
            }
        },
        fog.falloff.value);
    return gpu;
}

void prepare_fog(ecs::Commands commands,
                 ecs::Res<wgpu::Device> render_device,
                 ecs::Res<wgpu::Queue> render_queue,
                 ecs::ResMut<FogMeta> fog_meta,
                 ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const DistanceFog&>>,
                            ecs::With<::epix::render::view::ExtractedView>> views) {
    // Bevy `prepare_fog` (`render/fog.rs:49-120`): one `GpuFog` per view, in view order,
    // each view stamped with its own offset. That stamp is what `SetMeshViewBindGroup<I>`
    // reads at binding 12, so a view it is missing from cannot be drawn at all.
    auto& gpu_fogs = fog_meta->gpu_fogs;
    gpu_fogs.clear();
    for (auto&& [entity, fog] : views.iter()) {
        const GpuFog gpu_fog      = fog ? gpu_fog_from(fog->get()) : GpuFog{.mode = GPU_FOG_MODE_OFF};
        const std::size_t offset  = gpu_fogs.push(gpu_fog);
        commands.entity(entity).insert(ViewFogUniformOffset{.offset = static_cast<std::uint32_t>(offset)});
    }
    gpu_fogs.write_buffer(render_device.get(), render_queue.get());
}

void FogPlugin::attach(epix::app::App& app) {
    // Bevy `FogPlugin` (`render/fog.rs:128-141`), in its order: the fog component is
    // extracted to the view, the meta resource holds the per-view uniforms, and the prepare
    // system runs where the other render resources are built. `FogMeta` belongs to the
    // render world, because that is the world `prepare_fog` runs in.
    app.add_plugins(render::ExtractComponentPlugin<DistanceFog>{});
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<FogMeta>();
        render_app->get().add_systems(render::Render,
                                      ecs::into(prepare_fog)
                                          .in_set(render::RenderSystems::PrepareResources)
                                          .set_name("prepare fog"));
    }
}

}  // namespace epix::pbr
