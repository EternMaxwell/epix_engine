#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/render.hpp>
#include <functional>
#include <optional>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::core_graph {

/** @brief Specialization key for `BlitPipeline` (Bevy `BlitPipelineKey`). */
EPIX_EXPORT struct BlitPipelineKey {
    wgpu::TextureFormat texture_format = wgpu::TextureFormat::eUndefined;
    std::optional<wgpu::BlendState> blend_state;
    std::uint32_t samples = 1;

    bool operator==(const BlitPipelineKey& other) const noexcept;
};

/** @brief Reusable fullscreen texture-copy pipeline (Bevy `BlitPipeline`). */
EPIX_EXPORT struct BlitPipeline {
    using Key = BlitPipelineKey;

    wgpu::BindGroupLayout layout;
    wgpu::Sampler sampler;
    FullscreenShader fullscreen_shader;
    assets::Handle<shader::Shader> fragment_shader;

    /** @brief Creates the texture/sampler bind group for one source view. */
    wgpu::BindGroup create_bind_group(const wgpu::Device& device, const wgpu::TextureView& src_texture) const;

    /** @brief Builds a format/blend/sample-specialized render descriptor. */
    render::RenderPipelineDescriptor specialize(Key key) const;
};

/** @brief Installs the reusable blit pipeline (Bevy `BlitPlugin`). */
EPIX_EXPORT struct BlitPlugin {
    void attach(app::App& app);
};

}  // namespace epix::core_graph

template <>
struct std::hash<epix::core_graph::BlitPipelineKey> {
    std::size_t operator()(const epix::core_graph::BlitPipelineKey& key) const noexcept;
};
