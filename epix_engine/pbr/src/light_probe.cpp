#include <epix/pbr/light_probe.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <ranges>
#include <utility>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#endif

namespace epix::pbr {

void prepare_environment_uniform_buffer(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                                        ecs::Res<wgpu::Queue> render_queue,
                                        ecs::ResMut<EnvironmentMapUniformBuffer> environment_uniform_buffer,
                                        ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const EnvironmentMapUniform&>>,
                                                   ecs::With<::epix::render::view::ExtractedView>>
                                            views) {
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

}  // namespace epix::pbr
