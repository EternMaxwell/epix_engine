#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/time.hpp>
#include <string>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/render_resource.hpp>

namespace epix::render {
/**
 * @brief Global values useful when writing shaders; currently time-related
 * only (Bevy GlobalsUniform).
 */
EPIX_EXPORT struct GlobalsUniform {
    /** @brief Time since startup in seconds, wraps to 0 after 1 hour. */
    float time = 0.0f;
    /** @brief Delta time since the previous frame in seconds. */
    float delta_time = 0.0f;
    /** @brief Frame count since app start, wraps at u32 max. */
    std::uint32_t frame_count = 0;
};
static_assert(render_resource::ShaderType<GlobalsUniform>);

/** @brief Resource holding the GPU buffer containing GlobalsUniform (Bevy GlobalsBuffer). */
EPIX_EXPORT struct GlobalsBuffer {
    render_resource::UniformBuffer<GlobalsUniform> buffer;
};

/** @brief Resource holding the running frame count (used by GlobalsUniform,
 * Bevy's FrameCount from bevy_diagnostic). Extracted into the render world
 * each frame. */
EPIX_EXPORT struct FrameCount {
    /** @brief Number of frames elapsed since app start. */
    std::uint32_t count = 0;
};

namespace detail {
/** @brief System that advances the main-world FrameCount each Update (Bevy
 * bevy_diagnostic::increment_frame_count). */
inline void increment_frame_count(ecs::ResMut<FrameCount> frame_count) { frame_count->count += 1; }
/** @brief System that writes GlobalsUniform into GlobalsBuffer each frame
 * from the extracted render-world Time and FrameCount (Bevy
 * prepare_globals_buffer). */
inline void prepare_globals_buffer(ecs::ResMut<GlobalsBuffer> globals_buffer,
                                   ecs::Res<wgpu::Device> device,
                                   ecs::Res<wgpu::Queue> queue,
                                   ecs::Res<epix::time::Time<>> time,
                                   ecs::Res<FrameCount> frame_count) {
    auto& uniform       = globals_buffer->buffer.get_mut();
    uniform.time        = time.get().elapsed_secs_wrapped();
    uniform.delta_time  = time.get().delta_secs();
    uniform.frame_count = frame_count.get().count;
    globals_buffer->buffer.write_buffer(device.get(), queue.get());
}

/** @brief Copies the main-world FrameCount into the render world (Bevy
 * extract_frame_count). */
inline void extract_frame_count(ecs::Commands cmd, app::Extract<ecs::Res<FrameCount>> frame_count) {
    cmd.insert_resource(frame_count.get());
}

/** @brief Copies the main-world Time into the render world (Bevy
 * extract_time). */
inline void extract_time(ecs::Commands cmd, app::Extract<ecs::Res<epix::time::Time<>>> time) {
    cmd.insert_resource(time.get());
}
}  // namespace detail

/**
 * @brief Plugin that initializes GlobalsBuffer and updates it every frame
 * (Bevy GlobalsPlugin).
 */
EPIX_EXPORT struct GlobalsPlugin {
    void attach(app::App& app);
};

/**
 * @brief Plugin that provides a main-world FrameCount resource and advances
 * it every frame (Bevy bevy_diagnostic::FrameCountDiagnosticsPlugin).
 */
EPIX_EXPORT struct FrameCountPlugin {
    void attach(app::App& app);
};

}  // namespace epix::render
