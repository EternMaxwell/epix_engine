#include <epix/core_graph/mip_generation.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <bit>
#include <format>
#include <spdlog/spdlog.h>
#endif

using namespace epix::ecs;

namespace epix::core_graph::mip_generation {

glm::uvec2 depth_pyramid_size(glm::uvec2 size) noexcept {
    // Bevy `ViewDepthPyramid::new` (`experimental/mip_generation/mod.rs:590-594`):
    // the pyramid is half the depth buffer, rounded up, so that every source
    // texel is covered by one level-0 pyramid texel.
    return glm::uvec2((size.x + 1u) / 2u, (size.y + 1u) / 2u);
}

std::uint32_t max_mips_for_size(glm::uvec2 size) noexcept {
    // Bevy `Extent3d::max_mips(TextureDimension::D2)`: the base-2 logarithm of
    // the greatest dimension, rounded up, and never less than one.
    const std::uint32_t greatest = std::max(size.x, size.y);
    return std::max(1u, 32u - static_cast<std::uint32_t>(std::countl_zero(greatest)));
}

std::uint32_t depth_pyramid_mip_count(glm::uvec2 size) noexcept {
    // Bevy `ViewDepthPyramid::new` (`:597`): `depth_pyramid_size.max_mips(D2)`.
    return max_mips_for_size(depth_pyramid_size(size));
}

render::BindGroupLayoutDescriptor create_downsample_depth_bind_group_layout(bool multisampled) {
    // Bevy `create_downsample_depth_bind_group_layout` (`:420-455`). Bevy's
    // storage entries are write-only except binding 6, which the second phase
    // reads while writing mip 7; Slang and HLSL have no write-only
    // storage-texture declaration, so every storage entry is read-write here.
    std::vector<wgpu::BindGroupLayoutEntry> entries;
    entries.reserve(DOWNSAMPLE_DEPTH_BIND_GROUP_ENTRIES);

    entries.push_back(wgpu::BindGroupLayoutEntry()
                          .setBinding(0)
                          .setVisibility(wgpu::ShaderStage::eCompute)
                          .setTexture(wgpu::TextureBindingLayout()
                                          .setSampleType(wgpu::TextureSampleType::eDepth)
                                          .setViewDimension(wgpu::TextureViewDimension::e2D)
                                          .setMultisampled(multisampled)));
    for (std::size_t level = 0; level < DEPTH_PYRAMID_MIP_COUNT; ++level) {
        entries.push_back(wgpu::BindGroupLayoutEntry()
                              .setBinding(static_cast<std::uint32_t>(level + 1))
                              .setVisibility(wgpu::ShaderStage::eCompute)
                              .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                                     .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                                     .setFormat(wgpu::TextureFormat::eR32Float)
                                                     .setViewDimension(wgpu::TextureViewDimension::e2D)));
    }
    entries.push_back(wgpu::BindGroupLayoutEntry()
                          .setBinding(static_cast<std::uint32_t>(DEPTH_PYRAMID_MIP_COUNT + 1))
                          .setVisibility(wgpu::ShaderStage::eCompute)
                          .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eNonFiltering)));

    return render::BindGroupLayoutDescriptor()
        .set_label(multisampled ? "downsample multisample depth bind group layout"
                                : "downsample depth bind group layout")
        .set_entries(entries);
}

render::ComputePipelineDescriptor DownsampleDepthPipeline::specialize(Key key) const {
    // Bevy `SpecializedComputePipeline for DownsampleDepthPipeline` (`:473-513`).
    const bool multisampled = key.contains(DownsampleDepthPipelineKey::MULTISAMPLE);
    const bool second_phase = key.contains(DownsampleDepthPipelineKey::SECOND_PHASE);

    std::vector<shader::ShaderDefVal> shader_defs;
    if (multisampled) {
        shader_defs.emplace_back("MULTISAMPLE");
    }

    return render::ComputePipelineDescriptor{
        .label = std::format("downsample depth{}{} pipeline", multisampled ? " multisample" : "",
                             second_phase ? " second phase" : " first phase"),
        .layouts = std::vector<render::BindGroupLayoutDescriptor>{bind_group_layout},
        .push_constant_ranges =
            std::vector<render::PushConstantRange>{render::PushConstantRange{
                .stages = wgpu::ShaderStage::eCompute, .start = 0, .end = 4}},
        .shader      = shader,
        .shader_defs = std::move(shader_defs),
        .entry_point = std::string(second_phase ? "downsample_depth_second" : "downsample_depth_first"),
    };
}

wgpu::TextureView create_depth_pyramid_dummy_texture(const wgpu::Device& device,
                                                    const char* texture_label,
                                                    const char* texture_view_label) {
    // Bevy `create_depth_pyramid_dummy_texture` (`:533-560`): a one-texel
    // R32Float storage texture that fills the pyramid bindings no mip uses.
    const wgpu::Texture texture = device.createTexture(wgpu::TextureDescriptor()
                                                          .setLabel(texture_label)
                                                          .setSize({1, 1, 1})
                                                          .setMipLevelCount(1)
                                                          .setSampleCount(1)
                                                          .setDimension(wgpu::TextureDimension::e2D)
                                                          .setFormat(wgpu::TextureFormat::eR32Float)
                                                          .setUsage(wgpu::TextureUsage::eStorageBinding));
    return texture.createView(wgpu::TextureViewDescriptor()
                                  .setLabel(texture_view_label)
                                  .setFormat(wgpu::TextureFormat::eR32Float)
                                  .setDimension(wgpu::TextureViewDimension::e2D)
                                  .setBaseMipLevel(0)
                                  .setMipLevelCount(1)
                                  .setBaseArrayLayer(0)
                                  .setArrayLayerCount(1));
}

}  // namespace epix::core_graph::mip_generation
