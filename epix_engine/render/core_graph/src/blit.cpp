#include <epix/core_graph.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace epix::app;
using namespace epix::ecs;

namespace epix::core_graph {
namespace {
constexpr std::string_view kBlitFragmentPath  = "core_pipeline/blit.slang";
constexpr std::string_view kBlitFragmentSlang = R"slang(
[[vk::binding(0, 0)]] Texture2D<float4> in_texture;
[[vk::binding(1, 0)]] SamplerState in_sampler;
struct VIn { [[vk::location(0)]] float2 uv; };
[shader("fragment")]
float4 fs_main(VIn input) : SV_Target {
    return in_texture.Sample(in_sampler, input.uv);
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

bool same_blend_component(const wgpu::BlendComponent& lhs, const wgpu::BlendComponent& rhs) noexcept {
    return lhs.operation == rhs.operation && lhs.srcFactor == rhs.srcFactor && lhs.dstFactor == rhs.dstFactor;
}

bool same_blend_state(const std::optional<wgpu::BlendState>& lhs, const std::optional<wgpu::BlendState>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) return false;
    return !lhs || (same_blend_component(lhs->color, rhs->color) && same_blend_component(lhs->alpha, rhs->alpha));
}

}  // namespace

bool BlitPipelineKey::operator==(const BlitPipelineKey& other) const noexcept {
    return texture_format == other.texture_format && samples == other.samples &&
           same_blend_state(blend_state, other.blend_state);
}

wgpu::BindGroup BlitPipeline::create_bind_group(const wgpu::Device& device, const wgpu::TextureView& src_texture) const {
    using render::render_resource::BindGroupEntries;
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("blit_bind_group")
            .setLayout(layout)
            .setEntries(BindGroupEntries<>::sequential(BindGroupEntries<>::texture_binding(src_texture),
                                                        BindGroupEntries<>::sampler_binding(sampler)).entries()));
}

render::RenderPipelineDescriptor BlitPipeline::specialize(Key key) const {
    render::FragmentState fragment{.shader = fragment_shader, .entry_point = std::string("fs_main")};
    wgpu::ColorTargetState target;
    target.setFormat(key.texture_format).setWriteMask(wgpu::ColorWriteMask::eAll);
    if (key.blend_state) target.setBlend(*key.blend_state);
    fragment.add_target(std::move(target));
    return render::RenderPipelineDescriptor{
        .label       = "blit pipeline",
        .layouts     = {layout},
        .vertex      = fullscreen_shader.to_vertex_state(),
        // Rust wgpu's Default fills these fields with WebGPU defaults, while
        // the generated C++ wrappers value-initialize them to zero.
        .primitive = wgpu::PrimitiveState()
                         .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                         .setFrontFace(wgpu::FrontFace::eCCW)
                         .setCullMode(wgpu::CullMode::eNone)
                         .setUnclippedDepth(false),
        .multisample = wgpu::MultisampleState()
                           .setCount(key.samples)
                           .setMask(~0u)
                           .setAlphaToCoverageEnabled(false),
        .fragment    = std::move(fragment),
    };
}

void BlitPlugin::attach(App& app) {
    auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = app.world_mut().get_resource<assets::AssetServer>();
    if (!registry || !server) return;

    registry->get().insert_asset_static(kBlitFragmentPath, shader_bytes(kBlitFragmentSlang));
    const auto fragment_shader = server->get().load<shader::Shader>("embedded://core_pipeline/blit.slang");
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<render::SpecializedRenderPipelines<BlitPipeline>>();
        render_app->get().add_systems(
            render::RenderStartup,
            into([fragment_shader](Commands commands, Res<wgpu::Device> device, Res<FullscreenShader> fullscreen_shader) {
                using render::render_resource::BindGroupLayoutEntries;
                using render::render_resource::binding_types::sampler;
                using render::render_resource::binding_types::texture_2d;
                const auto entries = BindGroupLayoutEntries<>::sequential(
                    wgpu::ShaderStage::eFragment, texture_2d(wgpu::TextureSampleType::eFloat),
                    sampler(wgpu::SamplerBindingType::eNonFiltering));
                commands.insert_resource(BlitPipeline{
                    .layout = device->createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                                                 .setLabel("blit_bind_group_layout")
                                                                 .setEntries(entries.entries())),
                    // wgpu-native's C++ descriptor does not initialize this
                    // required field like Rust wgpu's Default does.
                    .sampler = device->createSampler(
                        wgpu::SamplerDescriptor().setLabel("blit_sampler").setMaxAnisotropy(1)),
                    .fullscreen_shader = *fullscreen_shader,
                    .fragment_shader   = fragment_shader,
                });
            }).set_name("init blit pipeline"));
    }
}

}  // namespace epix::core_graph

std::size_t std::hash<epix::core_graph::BlitPipelineKey>::operator()(
    const epix::core_graph::BlitPipelineKey& key) const noexcept {
    std::size_t result = static_cast<std::size_t>(key.texture_format);
    result ^= static_cast<std::size_t>(key.samples) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
    const auto blend = key.blend_state;
    if (blend) {
        const auto combine = [&result](const wgpu::BlendComponent& component) {
            result ^= static_cast<std::size_t>(component.operation) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^= static_cast<std::size_t>(component.srcFactor) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^= static_cast<std::size_t>(component.dstFactor) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
        };
        combine(blend->color);
        combine(blend->alpha);
    }
    return result;
}
