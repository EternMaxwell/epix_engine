#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/sprite_render/tilemap_chunk.hpp>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {
/** @brief Embedded path of the tilemap chunk fragment shader (Bevy
 *  `tilemap_chunk_material.wgsl`). */
constexpr std::string_view kTilemapChunkMaterialShaderPath = "sprite/tilemap_chunk_material.slang";

/** @brief Slang representation of Bevy's `tilemap_chunk_material.wgsl`: only the
 *  fragment stage is overridden, the vertex stage stays the mesh2d pipeline's.
 *
 *  Bevy's WGSL samples a `texture_2d_array<f32>` for the tileset and loads a
 *  `texture_2d<u32>` for the per-tile data, so the Slang types are
 *  `Texture2DArray<float4>` and `Texture2D<uint4>` (the sampled type of an
 *  `Rgba16Uint` texture is `uint`, whatever the channel count). The `#import`s of
 *  `mesh2d_functions`/`mesh2d_view_bindings` in Bevy's file are unused by its
 *  body and are therefore not carried over. */
constexpr std::string_view kTilemapChunkMaterialShader = R"slang(
import epix.mesh2d_vertex_output;

[[vk::binding(0, MATERIAL_BIND_GROUP)]] Texture2DArray<float4> tileset;
[[vk::binding(1, MATERIAL_BIND_GROUP)]] SamplerState tileset_sampler;
[[vk::binding(2, MATERIAL_BIND_GROUP)]] Texture2D<uint4> tile_data;

struct Tile {
    uint tileset_index;
    float4 color;
    bool visible;
};

Tile get_tile_data(uint2 coord) {
    uint4 data = tile_data.Load(int3(int2(coord), 0));

    uint tileset_index = data.r;

    float color_r = float(data.g & 0xFFu) / 255.0;
    float color_g = float((data.g >> 8u) & 0xFFu) / 255.0;
    float color_b = float(data.b & 0xFFu) / 255.0;
    float color_a = float((data.b >> 8u) & 0xFFu) / 255.0;

    float4 color = float4(color_r, color_g, color_b, color_a);

    bool visible = data.a != 0u;

    return Tile(tileset_index, color, visible);
}

[shader("fragment")]
float4 fragment(epix::mesh2d_vertex_output::VertexOutput mesh) : SV_Target {
    // Bevy's `textureDimensions(tile_data, 0)`: the tile-data texture is exactly
    // one texel per tile, so its dimensions are the chunk size in tiles.
    uint chunk_width;
    uint chunk_height;
    tile_data.GetDimensions(chunk_width, chunk_height);

    float2 tile_uv = mesh.uv * float2(chunk_width, chunk_height);
    float2 clamped = clamp(floor(tile_uv), float2(0.0, 0.0), float2(chunk_width, chunk_height) - 1.0);
    uint2 tile_coord = uint2(clamped);
    // The tile-data image's row 0 is the chunk's top row, while tile positions
    // count from the chunk's bottom-left.
    tile_coord.y = chunk_height - 1 - tile_coord.y;

    Tile tile = get_tile_data(tile_coord);

    if (tile.tileset_index == 0xffffu || !tile.visible) {
        discard;
    }

    float2 local_uv = frac(tile_uv);
    float4 tex_color = tileset.Sample(tileset_sampler, float3(local_uv, float(tile.tileset_index)));
    float4 final_color = tex_color * tile.color;

    if (final_color.a < 0.001) {
        discard;
    }

    return final_color;
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

/** @brief Bevy `ColorToPacked for Srgba::to_u8_array`'s per-channel rounding. */
std::uint8_t color_component_to_u8(float value) noexcept {
    return static_cast<std::uint8_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

/** @brief Bevy `Srgba::gamma_function_inverse`. Epix colours are linear
 *  `glm::vec4`, so packing the tint applies the same linear-to-sRGB transfer
 *  Bevy's `Color::to_srgba()` does before `to_u8_array()`. */
float linear_to_srgb(float value) noexcept {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}
}  // namespace

epix::sprite_render::PackedTileData epix::sprite_render::PackedTileData::empty() noexcept {
    PackedTileData data;
    data.tileset_index = std::numeric_limits<std::uint16_t>::max();
    return data;
}

epix::sprite_render::PackedTileData::PackedTileData(const TileData& tile) noexcept
    : tileset_index(tile.tileset_index),
      color{color_component_to_u8(linear_to_srgb(tile.color.r)), color_component_to_u8(linear_to_srgb(tile.color.g)),
            color_component_to_u8(linear_to_srgb(tile.color.b)), color_component_to_u8(tile.color.a)},
      flags(tile.visible ? 1u : 0u) {}

epix::sprite_render::PackedTileData::PackedTileData(const std::optional<TileData>& maybe_tile) noexcept {
    *this = maybe_tile ? PackedTileData(*maybe_tile) : PackedTileData::empty();
}

epix::image::Image epix::sprite_render::make_chunk_tile_data_image(glm::uvec2 size,
                                                                   std::span<const PackedTileData> data) {
    // Bevy casts the packed slice straight into the image's byte payload.
    std::vector<std::byte> bytes(data.size() * sizeof(PackedTileData));
    std::memcpy(bytes.data(), data.data(), bytes.size());

    epix::image::Image image{
        wgpu::Extent3D{size.x, size.y, 1}, wgpu::TextureDimension::e2D, std::move(bytes),
        wgpu::TextureFormat::eRGBA16Uint,
        static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::RENDER_WORLD |
                                                     epix::assets::RenderAssetUsages::MAIN_WORLD)};
    image.data_order = epix::image::TextureDataOrder::LayerMajor;
    image.set_sampler(epix::image::ImageSampler::nearest());
    image.texture_view_descriptor = std::nullopt;
    image.set_copy_on_resize(false);
    return image;
}

void epix::sprite_render::TilemapChunkMaterialPlugin::attach(epix::app::App& app) {
    // Bevy's `embedded_asset!(app, "tilemap_chunk_material.wgsl")`.
    if (auto registry = app.world_mut().get_resource_mut<epix::assets::EmbeddedAssetRegistry>()) {
        registry->get().insert_asset_static(kTilemapChunkMaterialShaderPath, shader_bytes(kTilemapChunkMaterialShader));
    } else {
        spdlog::warn("[tilemap] EmbeddedAssetRegistry not available. Shader not registered.");
    }
    app.add_plugins(epix::sprite_render::Material2dPlugin<epix::sprite_render::TilemapChunkMaterial>{});
}

std::expected<epix::render::render_resource::UnpreparedBindGroup, epix::render::render_resource::AsBindGroupError>
epix::render::render_resource::AsBindGroup<epix::sprite_render::TilemapChunkMaterial>::unprepared_bind_group(
    const wgpu::Device& device,
    const wgpu::BindGroupLayout& layout,
    const epix::sprite_render::TilemapChunkMaterial& material,
    const Param& param) {
    (void)device;
    (void)layout;
    const auto& [images] = param;

    // Bevy's derive retries while a declared texture is not prepared yet
    // (`as_bind_group.rs:568`); this material has no fallback image to fall back
    // to, because both textures are required.
    const auto* tileset = images->try_get(material.tileset);
    if (!tileset) return std::unexpected(AsBindGroupError{RetryBindGroupNextUpdate{}});
    const auto* tile_data = images->try_get(material.tile_data);
    if (!tile_data) return std::unexpected(AsBindGroupError{RetryBindGroupNextUpdate{}});

    return UnpreparedBindGroup{BindingResources{std::vector<BindingResources::value_type>{
        {0u, BindingResources::texture_view(wgpu::TextureViewDimension::e2DArray, tileset->texture_view)},
        {1u, BindingResources::sampler(wgpu::SamplerBindingType::eFiltering, tileset->sampler)},
        {2u, BindingResources::texture_view(wgpu::TextureViewDimension::e2D, tile_data->texture_view)},
    }}};
}
