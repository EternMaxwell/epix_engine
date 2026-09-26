#include <epix/pbr/fog.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <ranges>
#include <utility>
#endif

namespace epix::pbr {

void prepare_fog(ecs::Commands commands,
                 ecs::Res<wgpu::Device> render_device,
                 ecs::Res<wgpu::Queue> render_queue,
                 ecs::ResMut<FogMeta> fog_meta,
                 ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const DistanceFog&>>,
                            ecs::With<::epix::render::view::ExtractedView>> views) {
    const auto view_count = static_cast<std::size_t>(std::ranges::distance(views.iter()));
    auto writer           = fog_meta->gpu_fogs.get_writer(view_count, render_device.get(), render_queue.get());
    if (!writer.has_value()) {
        return;
    }

    for (auto&& [entity, maybe_fog] : views.iter()) {
        // No fog on this view is mode off with everything zeroed, not a missing entry.
        const GpuFog gpu_fog = maybe_fog.has_value() ? gpu_fog_from(maybe_fog->get()) : GpuFog{};
        commands.entity(entity).insert(ViewFogUniformOffset{.offset = writer->write(gpu_fog)});
    }
}

}  // namespace epix::pbr
