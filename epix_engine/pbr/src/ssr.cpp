#include <epix/pbr/ssr.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <ranges>
#endif

namespace epix::pbr {

void prepare_ssr_settings(ecs::Commands commands,
                          ecs::Res<wgpu::Device> render_device,
                          ecs::Res<wgpu::Queue> render_queue,
                          ecs::ResMut<ScreenSpaceReflectionsBuffer> ssr_settings_buffer,
                          ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const ScreenSpaceReflectionsUniform&>>,
                                     ecs::With<::epix::render::view::ExtractedView>> views) {
    // Bevy `prepare_ssr_settings` (`bevy_pbr/src/ssr/mod.rs:480-507`): one entry per view that
    // has SSR, and offset zero for the views that do not - the buffer's first entry, which is
    // what a view with no screen-space reflections reads.
    const auto view_count = static_cast<std::size_t>(std::ranges::distance(views.iter()));
    if (view_count == 0) {
        return;
    }
    auto writer = ssr_settings_buffer->uniforms.get_writer(view_count, render_device.get(), render_queue.get());
    if (!writer.has_value()) {
        return;
    }

    for (auto&& [view, ssr_uniform] : views.iter()) {
        const std::uint32_t offset = ssr_uniform.has_value() ? writer->write(ssr_uniform->get()) : 0u;
        commands.entity(view).insert(ViewScreenSpaceReflectionsUniformOffset{.offset = offset});
    }
}

}  // namespace epix::pbr
