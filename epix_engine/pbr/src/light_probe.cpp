#include <epix/pbr/light_probe.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <ranges>
#include <utility>
#endif

namespace epix::pbr {

void gather_environment_map_uniform(
    app::Extract<ecs::Query<ecs::Item<::epix::render::sync_world::RenderEntity, ecs::Opt<const EnvironmentMapLight&>>,
                            ecs::With<::epix::camera::Camera3d>>> views,
    ecs::Commands commands) {
    // Bevy `gather_environment_map_uniform` (light_probe/mod.rs:324-343): every
    // Camera3d view gets a uniform, and a view without the component gets the
    // default instance rather than nothing.
    for (auto&& [render_entity, environment_map_light] : views.iter()) {
        commands.entity(render_entity.id())
            .insert(environment_map_light.has_value()
                        ? environment_map_uniform_from(environment_map_light->get().rotation)
                        : environment_map_uniform_default());
    }
}

void prepare_environment_uniform_buffer(ecs::Commands commands,
                                        ecs::Res<wgpu::Device> render_device,
                                        ecs::Res<wgpu::Queue> render_queue,
                                        ecs::ResMut<EnvironmentMapUniformBuffer> environment_uniform_buffer,
                                        ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const EnvironmentMapUniform&>>,
                                                   ecs::With<::epix::render::view::ExtractedView>> views) {
    const auto view_count = static_cast<std::size_t>(std::ranges::distance(views.iter()));
    auto writer = environment_uniform_buffer->uniforms.get_writer(view_count, render_device.get(), render_queue.get());
    if (!writer.has_value()) {
        return;
    }

    for (auto&& [entity, maybe_uniform] : views.iter()) {
        // A view with no environment map takes offset 0 rather than a written entry:
        // the absence is the shader's business, so there is no reason to spend a slot.
        const std::uint32_t offset = maybe_uniform.has_value() ? writer->write(maybe_uniform->get()) : 0u;
        commands.entity(entity).insert(ViewEnvironmentMapUniformOffset{.offset = offset});
    }
}

bool light_probe_frustum_cull(const glm::mat4& world_from_light,
                                                 const ::epix::camera::Frustum& view_frustum) {
    const ::epix::camera::Aabb unit_cube{.center = glm::vec3(0.0f), .half_extents = glm::vec3(0.5f)};
    return view_frustum.intersects_obb(unit_cube, world_from_light, true, false);
}

float light_probe_camera_distance_sort_key(const glm::mat4& world_from_light,
                                                              glm::vec3 view_translation) {
    const glm::vec3 offset = glm::vec3(world_from_light[3]) - view_translation;
    return glm::dot(offset, offset);
}

void light_probes_gather_list(std::vector<RenderLightProbe>& out,
                                                 std::unordered_map<std::uint64_t, std::uint32_t>& indices,
                                                 const std::vector<LightProbeRecord>& probes) {
    const std::size_t count = std::min(probes.size(), MAX_VIEW_LIGHT_PROBES);
    for (std::size_t i = 0; i < count; ++i) {
        const LightProbeRecord& probe = probes[i];

        // The texture's slot, inserted on first sight; identical textures share it.
        auto [entry, inserted] = indices.try_emplace(probe.texture_id, static_cast<std::uint32_t>(indices.size()));
        (void)inserted;

        out.push_back(RenderLightProbe{
            .light_from_world_transposed      = probe.light_from_world_transposed,
            .texture_index                    = static_cast<std::int32_t>(entry->second),
            .intensity                        = probe.intensity,
            .affects_lightmapped_mesh_diffuse = probe.affects_lightmapped_mesh_diffuse ? 1u : 0u,
        });
    }
}

}  // namespace epix::pbr
