#include <epix/core_graph.hpp>

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace epix::app;
using namespace epix::ecs;

// Bevy 0.18 extraction for the camera components (extract_component_filter
// With<Camera>): copy the main-world camera's Tonemapping / DebandDither onto
// its synced render entity. Defined before the plugin that registers the
// ExtractComponentPlugins so the specializations are visible at instantiation.
EPIX_EXPORT template <>
struct epix::render::ExtractComponent<epix::core_graph::Tonemapping> {
    using QueryData   = const epix::core_graph::Tonemapping&;
    using QueryFilter = ::epix::ecs::With<::epix::camera::Camera>;
    using Out         = epix::core_graph::Tonemapping;

    static std::optional<Out> extract_component(QueryData value) { return value; }
};

EPIX_EXPORT template <>
struct epix::render::ExtractComponent<epix::core_graph::DebandDither> {
    using QueryData   = const epix::core_graph::DebandDither&;
    using QueryFilter = ::epix::ecs::With<::epix::camera::Camera>;
    using Out         = epix::core_graph::DebandDither;

    static std::optional<Out> extract_component(QueryData value) { return value; }
};

namespace epix::core_graph {
namespace {
constexpr std::string_view kTonemappingFragmentPath  = "core_pipeline/tonemapping.slang";
// Faithful Slang port of Bevy 0.18 tonemapping.wgsl + tonemapping_shared.wgsl +
// lut_bindings.wgsl (with bevy_render maths::{powsafe, PI_2} and
// color_operations::{hsv_to_rgb, rgb_to_hsv} inlined). The method and
// color-grading flags are selected per pipeline via shader_defs preprocessor
// macros, exactly like Bevy; LUT-requiring methods sample the bound 3D LUT
// (magenta placeholder in the default build) and the rest return magenta, as
// Bevy's `sample_current_lut` does without the `tonemapping_luts` feature.
constexpr std::string_view kTonemappingFragmentSlang = R"slang(
import epix.view;

[[vk::binding(0, 0)]] ConstantBuffer<epix::View> view_uniform;
[[vk::binding(1, 0)]] Texture2D<float4> hdr_texture;
[[vk::binding(2, 0)]] SamplerState hdr_sampler;
[[vk::binding(3, 0)]] Texture3D<float4> dt_lut_texture;
[[vk::binding(4, 0)]] SamplerState dt_lut_sampler;
struct VIn {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

static const float PI = 3.141592653589793;
static const float PI_2 = 6.283185307179586;
static const float FRAC_PI_3 = 1.0471975511965976;

// maths.wgsl powsafe: pow() but safe for NaNs/negatives.
float3 powsafe(float3 color, float power) {
    return pow(abs(color), float3(power)) * sign(color);
}
// color_operations.wgsl hsv_to_rgb / rgb_to_hsv.
float3 hsv_to_rgb(float3 hsv) {
    float3 n = float3(5.0, 3.0, 1.0);
    float3 k = (n + hsv.x / FRAC_PI_3) % 6.0;
    return hsv.z - hsv.z * hsv.y * max(float3(0.0), min(k, min(4.0 - k, float3(1.0))));
}
float3 rgb_to_hsv(float3 rgb) {
    float x_max = max(rgb.r, max(rgb.g, rgb.b));
    float x_min = min(rgb.r, min(rgb.g, rgb.b));
    float c = x_max - x_min;
    float3 swizzle = float3(0.0);
    if (x_max == rgb.r) { swizzle = float3(rgb.g, rgb.b, 0.0); }
    else if (x_max == rgb.g) { swizzle = float3(rgb.b, rgb.r, 2.0); }
    else { swizzle = float3(rgb.r, rgb.g, 4.0); }
    float h = FRAC_PI_3 * frac(((swizzle.x - swizzle.y) / c + swizzle.z) / 6.0) * 6.0;
    float s = 0.0;
    if (x_max > 0.0) { s = c / x_max; }
    return float3(h, s, x_max);
}

static const float LEVEL_MARGIN = 0.1;
static const float LEVEL_MARGIN_DIV = 0.5 / LEVEL_MARGIN;

// lut_bindings.wgsl sample_current_lut: sample the LUT for methods that need
// it, otherwise return magenta (placeholder).
float3 sample_current_lut(float3 p) {
#if defined(TONEMAP_METHOD_AGX) || defined(TONEMAP_METHOD_TONY_MC_MAPFACE) || defined(TONEMAP_METHOD_BLENDER_FILMIC)
    return dt_lut_texture.SampleLevel(dt_lut_sampler, p, 0.0).rgb;
#else
    return float3(1.0, 0.0, 1.0);
#endif
}

// --- SomewhatBoringDisplayTransform ---
float3 rgb_to_ycbcr(float3 col) {
    float3x3 m = float3x3(
        0.2126, 0.7152, 0.0722,
        -0.1146, -0.3854, 0.5,
        0.5, -0.4542, -0.0458);
    return mul(col, m);
}
float tonemap_curve(float v) { return 1.0 - exp(-v); }
float3 tonemap_curve3(float3 v) { return float3(tonemap_curve(v.r), tonemap_curve(v.g), tonemap_curve(v.b)); }
float tonemapping_luminance(float3 v) { return dot(v, float3(0.2126, 0.7152, 0.0722)); }
float3 somewhat_boring_display_transform(float3 col) {
    float3 boring_color = col;
    float3 ycbcr = rgb_to_ycbcr(boring_color);
    float bt = tonemap_curve(length(ycbcr.yz) * 2.4);
    float desat = max((bt - 0.7) * 0.8, 0.0);
    desat *= desat;
    float3 desat_col = lerp(boring_color.rgb, float3(ycbcr.x), desat);
    float tm_luma = tonemap_curve(ycbcr.x);
    float3 tm0 = boring_color.rgb * max(0.0, tm_luma / max(1e-5, tonemapping_luminance(boring_color.rgb)));
    float final_mult = 0.97;
    float3 tm1 = tonemap_curve3(desat_col);
    boring_color = lerp(tm0, tm1, bt * bt);
    return boring_color * final_mult;
}

// --- Tony McMapface ---
float3 sample_tony_mc_mapface_lut(float3 stimulus) {
    float3 uv = (stimulus / (stimulus + 1.0)) * (47.0 / 48.0) + 0.5 / 48.0;
    return sample_current_lut(saturate(uv));
}

// --- ACES Fitted ---
float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}
float3 ACESFitted(float3 color) {
    float3 fitted_color = color;
    float3x3 rgb_to_rrt = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777);
    float3x3 odt_to_rgb = float3x3(
        1.60475, -0.53108, -0.07367,
        -0.10208, 1.10813, -0.00605,
        -0.00327, -0.07276, 1.07602);
    fitted_color = mul(rgb_to_rrt, fitted_color);
    fitted_color = RRTAndODTFit(fitted_color);
    fitted_color = mul(odt_to_rgb, fitted_color);
    return saturate(fitted_color);
}

// --- AgX ---
float3 saturation_(float3 color, float saturationAmount) {
    float luma = tonemapping_luminance(color);
    return lerp(float3(luma), color, float3(saturationAmount));
}
float3 convertOpenDomainToNormalizedLog2_(float3 color, float minimum_ev, float maximum_ev) {
    float in_midgray = 0.18;
    float3 normalized_color = max(float3(0.0), color);
    normalized_color = normalized_color < float3(0.00003051757) ? float3(0.00001525878) + normalized_color : normalized_color;
    normalized_color = clamp(log2(normalized_color / in_midgray), float3(minimum_ev), float3(maximum_ev));
    float total_exposure = maximum_ev - minimum_ev;
    return (normalized_color - minimum_ev) / total_exposure;
}
float3 applyAgXLog(float3 image) {
    float3 prepared_image = max(float3(0.0), image);
    float r = dot(prepared_image, float3(0.84247906, 0.0784336, 0.07922375));
    float g = dot(prepared_image, float3(0.04232824, 0.87846864, 0.07916613));
    float b = dot(prepared_image, float3(0.04237565, 0.0784336, 0.87914297));
    prepared_image = float3(r, g, b);
    prepared_image = convertOpenDomainToNormalizedLog2_(prepared_image, -10.0, 6.5);
    return clamp(prepared_image, float3(0.0), float3(1.0));
}
float3 applyLUT3D(float3 image, float block_size) {
    return sample_current_lut(image * ((block_size - 1.0) / block_size) + 0.5 / block_size);
}

// --- BlenderFilmic ---
float3 sample_blender_filmic_lut(float3 stimulus) {
    float block_size = 64.0;
    float3 normalized = saturate(convertOpenDomainToNormalizedLog2_(stimulus, -11.0, 12.0));
    return applyLUT3D(normalized, block_size);
}

// --- generic tonemapping methods ---
float3 tonemapping_reinhard(float3 color) { return color / (1.0 + color); }
float3 tonemapping_change_luminance(float3 c_in, float l_out) {
    float l_in = tonemapping_luminance(c_in);
    return c_in * (l_out / l_in);
}
float3 tonemapping_reinhard_luminance(float3 color) {
    float l_old = tonemapping_luminance(color);
    float l_new = l_old / (1.0 + l_old);
    return tonemapping_change_luminance(color, l_new);
}
float3 screen_space_dither(float2 frag_coord) {
    float3 dither = float3(dot(float2(171.0, 231.0), frag_coord));
    dither = frac(dither.rgb / float3(103.0, 71.0, 97.0));
    return (dither - 0.5) / 255.0;
}
float3 sectional_color_grading(float3 in_color, inout epix::ColorGrading color_grading) {
    float3 color = in_color;
    float level = (color.r + color.g + color.b) / 3.0;
    float3 levels = float3(0.0);
    float2 midtone_range = color_grading.midtone_range;
    if (level < midtone_range.x - LEVEL_MARGIN) {
        levels.x = 1.0;
    } else if (level < midtone_range.x + LEVEL_MARGIN) {
        levels.y = ((level - midtone_range.x) * LEVEL_MARGIN_DIV) + 0.5;
        levels.z = 1.0 - levels.y;
    } else if (level < midtone_range.y - LEVEL_MARGIN) {
        levels.y = 1.0;
    } else if (level < midtone_range.y + LEVEL_MARGIN) {
        levels.z = ((level - midtone_range.y) * LEVEL_MARGIN_DIV) + 0.5;
        levels.y = 1.0 - levels.z;
    } else {
        levels.z = 1.0;
    }
    float contrast   = dot(levels, color_grading.contrast);
    float saturation = dot(levels, color_grading.saturation);
    float gamma      = dot(levels, color_grading.gamma);
    float gain       = dot(levels, color_grading.gain);
    float lift       = dot(levels, color_grading.lift);
    float luma = tonemapping_luminance(color);
    color = luma + saturation * (color - luma);
    color = 0.5 + (color - 0.5) * contrast;
    color = powsafe(color * gain + lift, 1.0 / gamma);
    color = color * powsafe(float3(2.0), color_grading.exposure);
    return max(color, float3(0.0));
}
float4 tone_mapping(float4 in_color, inout epix::ColorGrading color_grading) {
    float3 color = max(in_color.rgb, float3(0.0));
#if defined(HUE_ROTATE)
    float3 hsv = rgb_to_hsv(color);
    hsv.r = (hsv.r + color_grading.hue) % PI_2;
    color = hsv_to_rgb(hsv);
#endif
#if defined(WHITE_BALANCE)
    color = max(color_grading.balance * color, float3(0.0));
#endif
#if defined(SECTIONAL_COLOR_GRADING)
    color = sectional_color_grading(color, color_grading);
#else
    color = color * powsafe(float3(2.0), color_grading.exposure);
#endif
#if defined(TONEMAP_METHOD_NONE)
    color = color;
#elif defined(TONEMAP_METHOD_REINHARD)
    color = tonemapping_reinhard(color.rgb);
#elif defined(TONEMAP_METHOD_REINHARD_LUMINANCE)
    color = tonemapping_reinhard_luminance(color.rgb);
#elif defined(TONEMAP_METHOD_ACES_FITTED)
    color = ACESFitted(color.rgb);
#elif defined(TONEMAP_METHOD_AGX)
    color = applyLUT3D(applyAgXLog(color), 32.0);
#elif defined(TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM)
    color = somewhat_boring_display_transform(color.rgb);
#elif defined(TONEMAP_METHOD_TONY_MC_MAPFACE)
    color = sample_tony_mc_mapface_lut(color);
#elif defined(TONEMAP_METHOD_BLENDER_FILMIC)
    color = sample_blender_filmic_lut(color.rgb);
#endif
    color = saturation_(color, color_grading.post_saturation);
    return float4(color, in_color.a);
}

[shader("fragment")]
float4 fs_main(VIn input) : SV_Target {
    float4 hdr_color = hdr_texture.Sample(hdr_sampler, input.uv);
    // Copy the color-grading struct out of the constant buffer so it can be
    // written in-place by tone_mapping (the ConstantBuffer field is a
    // read-only value, not an l-value).
    epix::ColorGrading color_grading = view_uniform.color_grading;
    float3 output_rgb = tone_mapping(hdr_color, color_grading).rgb;
#ifdef DEBAND_DITHER
    output_rgb = powsafe(output_rgb, 1.0 / 2.2);
    output_rgb = output_rgb + screen_space_dither(input.position.xy);
    output_rgb = powsafe(output_rgb, 2.2);
#endif
    return float4(output_rgb, hdr_color.a);
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

bool tonemapping_key_equal(const TonemappingPipelineKey& lhs, const TonemappingPipelineKey& rhs) noexcept {
    return lhs.target_format == rhs.target_format && lhs.deband_dither == rhs.deband_dither &&
           lhs.tonemapping == rhs.tonemapping && lhs.flags == rhs.flags;
}

/** @brief Bevy `get_lut_bindings`: pick the LUT for the method (all point at
 * the placeholder in the default build), or the 3D fallback image. */
const render::texture::GpuImage* lut_bindings(const render::RenderAssets<image::Image>& images,
                                              const TonemappingLuts& tonemapping_luts, Tonemapping tonemapping,
                                              const render::texture::FallbackImage& fallback_image) {
    const assets::Handle<image::Image>* handle = &tonemapping_luts.agx;
    switch (tonemapping) {
        case Tonemapping::TonyMcMapface: handle = &tonemapping_luts.tony_mc_mapface; break;
        case Tonemapping::BlenderFilmic: handle = &tonemapping_luts.blender_filmic; break;
        default: break;
    }
    const auto* image = images.get(*handle);
    return image ? image : &fallback_image.d3;
}
}  // namespace

bool TonemappingPipelineKey::operator==(const TonemappingPipelineKey& other) const noexcept {
    return tonemapping_key_equal(*this, other);
}

wgpu::BindGroup TonemappingPipeline::create_bind_group(const wgpu::Device& device, const wgpu::Buffer& view_uniforms,
                                                       const wgpu::TextureView& source, const wgpu::TextureView& lut_view,
                                                       const wgpu::Sampler& lut_sampler) const {
    using render::render_resource::BindGroupEntries;
    return device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("tonemapping_bind_group")
            .setLayout(layout)
            .setEntries(BindGroupEntries<>::with_indices(
                std::pair{0u, BindGroupEntries<>::buffer_binding(view_uniforms, 0, sizeof(render::view::ViewUniform))},
                std::pair{1u, BindGroupEntries<>::texture_binding(source)},
                std::pair{2u, BindGroupEntries<>::sampler_binding(sampler)},
                std::pair{3u, BindGroupEntries<>::texture_binding(lut_view)},
                std::pair{4u, BindGroupEntries<>::sampler_binding(lut_sampler)})
                            .entries()));
}

render::RenderPipelineDescriptor TonemappingPipeline::specialize(Key key) const {
    render::FragmentState fragment{.shader = fragment_shader, .entry_point = std::string("fs_main")};
    std::vector<shader::ShaderDefVal> defs;
    switch (key.tonemapping) {
        case Tonemapping::None: defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_NONE")); break;
        case Tonemapping::Reinhard: defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_REINHARD")); break;
        case Tonemapping::ReinhardLuminance:
            defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_REINHARD_LUMINANCE"));
            break;
        case Tonemapping::AcesFitted:
            defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_ACES_FITTED"));
            break;
        case Tonemapping::AgX: defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_AGX")); break;
        case Tonemapping::SomewhatBoringDisplayTransform:
            defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM"));
            break;
        case Tonemapping::TonyMcMapface:
            defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_TONY_MC_MAPFACE"));
            break;
        case Tonemapping::BlenderFilmic:
            defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_BLENDER_FILMIC"));
            break;
    }
    if (key.deband_dither == DebandDither::Enabled) {
        defs.push_back(shader::ShaderDefVal::from_bool("DEBAND_DITHER"));
    }
    if ((key.flags & static_cast<std::uint8_t>(TonemappingPipelineKeyFlags::HueRotate)) != 0) {
        defs.push_back(shader::ShaderDefVal::from_bool("HUE_ROTATE"));
    }
    if ((key.flags & static_cast<std::uint8_t>(TonemappingPipelineKeyFlags::WhiteBalance)) != 0) {
        defs.push_back(shader::ShaderDefVal::from_bool("WHITE_BALANCE"));
    }
    if ((key.flags & static_cast<std::uint8_t>(TonemappingPipelineKeyFlags::SectionalColorGrading)) != 0) {
        defs.push_back(shader::ShaderDefVal::from_bool("SECTIONAL_COLOR_GRADING"));
    }
    fragment.shader_defs = std::move(defs);

    wgpu::ColorTargetState target;
    target.setFormat(key.target_format).setWriteMask(wgpu::ColorWriteMask::eAll);
    fragment.add_target(std::move(target));

    return render::RenderPipelineDescriptor{
        .label       = "tonemapping pipeline",
        .layouts     = {layout},
        .vertex      = fullscreen_shader.to_vertex_state(),
        .primitive   = wgpu::PrimitiveState()
                           .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                           .setFrontFace(wgpu::FrontFace::eCCW)
                           .setCullMode(wgpu::CullMode::eNone)
                           .setUnclippedDepth(false),
        .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment    = std::move(fragment),
    };
}

/** @brief Bevy `prepare_view_tonemapping_pipelines` (tonemapping/mod.rs:331-376).
 *
 * Processes every `ViewTarget` view, specializing a pipeline per
 * format/deband/method/color-grading-flags and inserting `ViewTonemappingPipeline`.
 * The node itself gates on the None method and non-HDR views. */
void prepare_view_tonemapping_pipelines(
    Commands commands,
    ResMut<render::PipelineServer> pipeline_server,
    ResMut<render::SpecializedRenderPipelines<TonemappingPipeline>> pipelines,
    Res<TonemappingPipeline> tonemapping_pipeline,
    Query<Item<Entity,
               const render::view::ExtractedView&,
               const render::view::ViewTarget&,
               Opt<const Tonemapping&>,
               Opt<const DebandDither&>>> view_targets) {
    for (auto&& [entity, view, target, opt_tonemapping, opt_deband] : view_targets.iter()) {
        // Bevy defaults: None/Disabled when the camera lacks the components.
        const Tonemapping tonemapping  = opt_tonemapping ? opt_tonemapping->get() : Tonemapping::None;
        const DebandDither deband_dither = opt_deband ? opt_deband->get() : DebandDither::Disabled;

        // Color-grading step flags from the view (Bevy).
        std::uint8_t flags = 0;
        const auto& grading = view.color_grading;
        if (grading.global.hue != 0.0f) flags |= static_cast<std::uint8_t>(TonemappingPipelineKeyFlags::HueRotate);
        if (grading.global.temperature != 0.0f || grading.global.tint != 0.0f)
            flags |= static_cast<std::uint8_t>(TonemappingPipelineKeyFlags::WhiteBalance);
        const bool sectional_non_default = std::ranges::any_of(
            grading.all_sections(), [](const auto& section) {
                const auto& s = section.get();
                const render::view::ColorGradingSection default_section;
                return s.saturation != default_section.saturation || s.contrast != default_section.contrast ||
                       s.gamma != default_section.gamma || s.gain != default_section.gain ||
                       s.lift != default_section.lift;
            });
        if (sectional_non_default)
            flags |= static_cast<std::uint8_t>(TonemappingPipelineKeyFlags::SectionalColorGrading);

        const TonemappingPipelineKey key{
            .target_format = target.main_texture_format(), .deband_dither = deband_dither,
            .tonemapping = tonemapping, .flags = flags};
        const auto pipeline_id = pipelines->specialize(*pipeline_server, *tonemapping_pipeline, key);
        commands.entity(entity).insert(ViewTonemappingPipeline{pipeline_id});
    }
}

std::expected<void, render::graph::NodeRunError> TonemappingNode::run(render::graph::GraphContext&,
                                                                      render::graph::RenderContext& render_context,
                                                                      typename ecs::QueryData<ViewQuery>::Item view,
                                                                      const ecs::World& world) const {
    const auto [view_uniform_offset, target, view_tonemapping_pipeline, tonemapping] = view;
    if (!is_enabled(tonemapping)) return {};
    if (!target.is_hdr()) return {};

    const auto pipeline_server      = world.get_resource<render::PipelineServer>();
    const auto tonemapping_pipeline = world.get_resource<TonemappingPipeline>();
    const auto tonemapping_luts     = world.get_resource<TonemappingLuts>();
    const auto view_uniforms        = world.get_resource<render::view::ViewUniforms>();
    const auto gpu_images           = world.get_resource<render::RenderAssets<image::Image>>();
    const auto fallback_image       = world.get_resource<render::texture::FallbackImage>();
    if (!pipeline_server || !tonemapping_pipeline || !tonemapping_luts || !view_uniforms || !gpu_images ||
        !fallback_image)
        return {};

    const auto pipeline = pipeline_server->get().get_render_pipeline(view_tonemapping_pipeline.pipeline_id);
    if (!pipeline) return {};

    const auto* uniform_buffer = view_uniforms->get().uniforms.buffer();
    if (!uniform_buffer) return {};

    const auto post_process = target.post_process_write();
    const auto* lut_image   = lut_bindings(*gpu_images, tonemapping_luts->get(), tonemapping, fallback_image->get());
    const wgpu::BindGroup bind_group = tonemapping_pipeline->get().create_bind_group(
        render_context.device(), *uniform_buffer, post_process.source, lut_image->texture_view, lut_image->sampler);

    const std::uint32_t offset          = view_uniform_offset.offset;
    const auto render_pipeline          = pipeline->get().pipeline();
    const wgpu::TextureView destination = post_process.destination;

    render_context.add_command_buffer_generation_task(
        [render_pipeline, bind_group, destination, offset](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("tonemapping"));
            auto color   = wgpu::RenderPassColorAttachment()
                              .setView(destination)
                              .setDepthSlice(~0u)
                              .setLoadOp(wgpu::LoadOp::eClear)
                              .setStoreOp(wgpu::StoreOp::eStore)
                              .setClearValue(wgpu::Color(0.0, 0.0, 0.0, 0.0));
            auto pass = encoder.beginRenderPass(
                wgpu::RenderPassDescriptor().setLabel("tonemapping").setColorAttachments(std::array{color}));
            pass.setPipeline(render_pipeline);
            pass.setBindGroup(0, bind_group, offset);
            pass.draw(3, 1, 0, 0);
            pass.end();
            return encoder.finish();
        });
    return {};
}

void TonemappingPlugin::attach(App& app) {
    const auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
    const auto server   = app.world_mut().get_resource<assets::AssetServer>();
    if (!registry || !server) return;

    registry->get().insert_asset_static(kTonemappingFragmentPath, shader_bytes(kTonemappingFragmentSlang));
    const auto fragment_shader = server->get().load<shader::Shader>("embedded://core_pipeline/tonemapping.slang");

    // Bevy TonemappingPlugin: LUT placeholders (tonemapping_luts-disabled
    // build), ExtractionPlugins, then the render-world pipeline.
    if (!app.world().get_resource<TonemappingLuts>()) {
        if (auto images = app.world_mut().get_resource_mut<assets::Assets<image::Image>>()) {
            std::vector<std::uint8_t> magenta{255, 0, 255, 255};
            auto placeholder = image::Image::create3d(1, 1, 1, image::Format::RGBA8, magenta);
            if (placeholder) {
                const auto handle = images->get().add(std::move(*placeholder));
                app.world_mut().insert_resource(TonemappingLuts{handle, handle, handle});
            }
        }
    }
    app.add_plugins(render::ExtractResourcePlugin<TonemappingLuts>{});
    app.add_plugins(render::ExtractComponentPlugin<Tonemapping>{}, render::ExtractComponentPlugin<DebandDither>{});

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<render::SpecializedRenderPipelines<TonemappingPipeline>>();
        render_app->get().add_systems(
            render::RenderStartup,
            into([fragment_shader](Commands commands, Res<wgpu::Device> device, Res<FullscreenShader> fullscreen_shader) {
                using render::render_resource::BindGroupLayoutEntries;
                using render::render_resource::binding_types::sampler;
                using render::render_resource::binding_types::texture_2d;
                using render::render_resource::binding_types::texture_3d;
                using render::render_resource::binding_types::uniform_buffer;
                const auto entries = BindGroupLayoutEntries<>::with_indices(
                    wgpu::ShaderStage::eFragment,
                    std::pair{0u, uniform_buffer(true, sizeof(render::view::ViewUniform))},
                    std::pair{1u, texture_2d(wgpu::TextureSampleType::eUnfilterableFloat)},
                    std::pair{2u, sampler(wgpu::SamplerBindingType::eNonFiltering)},
                    std::pair{3u, texture_3d(wgpu::TextureSampleType::eFloat)},
                    std::pair{4u, sampler(wgpu::SamplerBindingType::eFiltering)});

                commands.insert_resource(TonemappingPipeline{
                    .layout           = device->createBindGroupLayout(wgpu::BindGroupLayoutDescriptor()
                                                                          .setLabel("tonemapping_bind_group_layout")
                                                                          .setEntries(entries.entries())),
                    .sampler          = device->createSampler(
                        wgpu::SamplerDescriptor().setLabel("tonemapping_sampler").setMaxAnisotropy(1)),
                    .fullscreen_shader = *fullscreen_shader,
                    .fragment_shader   = fragment_shader,
                });
            }).set_name("init tonemapping pipeline"));
        render_app->get().add_systems(
            render::Render,
            into(prepare_view_tonemapping_pipelines).in_set(render::RenderSystems::Prepare).set_name(
                "prepare view tonemapping pipelines"));
    }
}

}  // namespace epix::core_graph

std::size_t std::hash<epix::core_graph::TonemappingPipelineKey>::operator()(
    const epix::core_graph::TonemappingPipelineKey& key) const noexcept {
    std::size_t result = static_cast<std::size_t>(key.target_format);
    result ^= static_cast<std::size_t>(key.deband_dither) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
    result ^= static_cast<std::size_t>(key.tonemapping) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
    result ^= static_cast<std::size_t>(key.flags) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
    return result;
}

