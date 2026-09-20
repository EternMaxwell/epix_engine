#include <epix/core_graph/skybox.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/render/view.hpp>
#endif

using namespace epix::ecs;

namespace epix::core_graph {

bool SkyboxPipelineKey::operator==(const SkyboxPipelineKey&) const noexcept = default;

SkyboxPipeline SkyboxPipeline::create(assets::Handle<shader::Shader> shader) {
    // Bevy `SkyboxPipeline::new` (`skybox/mod.rs:156-175`): a cube texture, a
    // filtering sampler, the view uniform with a dynamic offset, and the skybox
    // uniforms - all on the fragment stage except the view uniform, which the
    // vertex stage reads too. The entries are built by hand rather than with the
    // `binding_types` helpers because the view uniform is the only entry whose
    // visibility differs from the rest.
    const auto uniform = [](std::uint64_t min_binding_size) {
        return wgpu::BufferBindingLayout()
            .setType(wgpu::BufferBindingType::eUniform)
            .setHasDynamicOffset(true)
            .setMinBindingSize(min_binding_size);
    };
    const std::vector<wgpu::BindGroupLayoutEntry> entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setTexture(wgpu::TextureBindingLayout()
                            .setSampleType(wgpu::TextureSampleType::eFloat)
                            .setViewDimension(wgpu::TextureViewDimension::eCube)
                            .setMultisampled(false)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(1)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eFiltering)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(2)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(uniform(0)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(3)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setBuffer(uniform(sizeof(SkyboxUniforms))),
    };

    return SkyboxPipeline{
        .bind_group_layout = render::BindGroupLayoutDescriptor()
                                 .set_label("skybox_bind_group_layout")
                                 .set_entries(entries),
        .shader            = std::move(shader),
    };
}

render::RenderPipelineDescriptor SkyboxPipeline::specialize(Key key) const {
    // Bevy `SpecializedRenderPipeline for SkyboxPipeline` (`:189-238`).
    render::FragmentState fragment{.shader = shader, .entry_point = std::string("fragment")};
    wgpu::ColorTargetState target;
    // Bevy leaves the blend state unset here: `None` is not `REPLACE`, and is
    // potentially faster.
    target.setFormat(key.hdr ? render::view::ViewTarget::TEXTURE_FORMAT_HDR
                             : image::bevy_default<wgpu::TextureFormat>())
        .setWriteMask(wgpu::ColorWriteMask::eAll);
    fragment.add_target(std::move(target));

    const auto stencil_ignore = wgpu::StencilFaceState()
                                    .setCompare(wgpu::CompareFunction::eAlways)
                                    .setFailOp(wgpu::StencilOperation::eKeep)
                                    .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                    .setPassOp(wgpu::StencilOperation::eKeep);
    auto depth_stencil = wgpu::DepthStencilState()
                             .setFormat(key.depth_format)
                             .setDepthWriteEnabled(wgpu::OptionalBool::eFalse)
                             .setDepthCompare(wgpu::CompareFunction::eGreaterEqual)
                             .setStencilFront(stencil_ignore)
                             .setStencilBack(stencil_ignore)
                             .setStencilReadMask(0)
                             .setStencilWriteMask(0)
                             .setDepthBias(0)
                             .setDepthBiasSlopeScale(0.0f)
                             .setDepthBiasClamp(0.0f);

    return render::RenderPipelineDescriptor{
        .label = "skybox_pipeline",
        .layouts = {bind_group_layout},
        .vertex = render::VertexState{.shader = shader, .entry_point = std::string("vertex")},
        .primitive = wgpu::PrimitiveState()
                         .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                         .setFrontFace(wgpu::FrontFace::eCCW)
                         .setCullMode(wgpu::CullMode::eNone)
                         .setUnclippedDepth(false),
        .depth_stencil = std::move(depth_stencil),
        .multisample   = wgpu::MultisampleState()
                             .setCount(key.samples)
                             .setMask(~0u)
                             .setAlphaToCoverageEnabled(false),
        .fragment      = std::move(fragment),
    };
}

}  // namespace epix::core_graph

namespace std {
std::size_t hash<epix::core_graph::SkyboxPipelineKey>::operator()(
    const epix::core_graph::SkyboxPipelineKey& key) const noexcept {
    std::size_t h = std::hash<bool>{}(key.hdr);
    h ^= std::hash<std::uint32_t>{}(key.samples) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(key.depth_format)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}
}  // namespace std
