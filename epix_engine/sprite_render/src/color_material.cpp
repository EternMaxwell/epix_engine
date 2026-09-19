#include <epix/sprite_render/color_material.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace {
/** @brief Embedded path of the color material shader (Bevy
 *  `color_material.wgsl`). */
constexpr std::string_view kColorMaterialShaderPath = "sprite/color_material.slang";

/** @brief Bevy `color_material.wgsl`: only the fragment stage is overridden,
 *  the vertex stage stays the mesh2d pipeline's. The material bind group index
 *  arrives as the `MATERIAL_BIND_GROUP` definition, as it does in Bevy. */
constexpr std::string_view kColorMaterialShader = R"slang(
module epix.color_material;

import epix.view;
import epix.mesh2d_view_bindings;
import epix.mesh2d_vertex_output;
#ifdef TONEMAP_IN_SHADER
import epix.tonemapping;
#endif

struct ColorMaterial {
    float4 color;
    float3x3 uv_transform;
    // 'flags' is a bit field; the type is 32 bits, so up to 32 options.
    uint flags;
    float alpha_cutoff;
};

static const uint COLOR_MATERIAL_FLAGS_TEXTURE_BIT              = 1u;
static const uint COLOR_MATERIAL_FLAGS_ALPHA_MODE_RESERVED_BITS = 3221225472u; // (0b11u << 30)
static const uint COLOR_MATERIAL_FLAGS_ALPHA_MODE_OPAQUE        = 0u;          // (0u << 30)
static const uint COLOR_MATERIAL_FLAGS_ALPHA_MODE_MASK          = 1073741824u; // (1u << 30)
static const uint COLOR_MATERIAL_FLAGS_ALPHA_MODE_BLEND         = 2147483648u; // (2u << 30)

[[vk::binding(0, MATERIAL_BIND_GROUP)]] ConstantBuffer<ColorMaterial> material;
[[vk::binding(1, MATERIAL_BIND_GROUP)]] Texture2D<float4> texture;
[[vk::binding(2, MATERIAL_BIND_GROUP)]] SamplerState texture_sampler;

float4 alpha_discard(ColorMaterial material, float4 output_color) {
    float4 color = output_color;
    uint alpha_mode = material.flags & COLOR_MATERIAL_FLAGS_ALPHA_MODE_RESERVED_BITS;
    if (alpha_mode == COLOR_MATERIAL_FLAGS_ALPHA_MODE_OPAQUE) {
        // Rendering as opaque ignores alpha, so set it to 1.
        color.a = 1.0;
    }
#ifdef MAY_DISCARD
    else if (alpha_mode == COLOR_MATERIAL_FLAGS_ALPHA_MODE_MASK) {
        if (color.a >= material.alpha_cutoff) {
            // At or above the cutoff the fragment renders fully opaque.
            color.a = 1.0;
        } else {
            discard;
        }
    }
#endif
    return color;
}

[shader("fragment")]
float4 fragment(epix::mesh2d_vertex_output::VertexOutput mesh) : SV_Target {
    float4 output_color = material.color;

#ifdef VERTEX_COLORS
    output_color = output_color * mesh.color;
#endif

    float2 uv = (mul(material.uv_transform, float3(mesh.uv, 1.0))).xy;

    if ((material.flags & COLOR_MATERIAL_FLAGS_TEXTURE_BIT) != 0u) {
        output_color = output_color * texture.Sample(texture_sampler, uv);
    }

    output_color = alpha_discard(material, output_color);

#ifdef TONEMAP_IN_SHADER
    epix::view::ColorGrading grading = view.color_grading;
    output_color = epix::tonemapping::tone_mapping(output_color, grading, dt_lut_texture, dt_lut_sampler);
#endif
    return output_color;
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}
}  // namespace

void epix::sprite_render::ColorMaterialPlugin::register_color_material_shader(epix::ecs::World& world) {
    // Bevy's `embedded_asset!(app, "color_material.wgsl")`.
    auto registry = world.get_resource_mut<epix::assets::EmbeddedAssetRegistry>();
    if (!registry) {
        spdlog::warn("[sprite.color_material] EmbeddedAssetRegistry not available. Shader not registered.");
        return;
    }
    registry->get().insert_asset_static(kColorMaterialShaderPath, shader_bytes(kColorMaterialShader));
}
