#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/image.hpp>
#include <epix/sprite_render/material2d.hpp>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>
#endif

namespace epix::sprite_render {
/** @brief Bit flags a `ColorMaterial` carries to its shader (Bevy
 *  `ColorMaterialFlags`, `color_material.rs:90-110`).  These values must match
 *  the shader's own bit flags. */
EPIX_EXPORT struct ColorMaterialFlags {
    enum Bit : std::uint32_t {
        TEXTURE                    = 1u << 0u,
        /** Bits reserved for the `AlphaMode2d` discriminator. */
        ALPHA_MODE_RESERVED_BITS   = 0b11u << (32u - 2u),
        ALPHA_MODE_OPAQUE          = 0u << (32u - 2u),
        ALPHA_MODE_MASK            = 1u << (32u - 2u),
        ALPHA_MODE_BLEND           = 2u << (32u - 2u),
        NONE                       = 0,
        UNINITIALIZED              = 0xFFFF,
    };
};

/** @brief The GPU representation of a `ColorMaterial`'s uniform (Bevy
 *  `ColorMaterialUniform`, `color_material.rs:112-119`).  Epix has no
 *  `bevy_color`, so the colour is a `glm::vec4`, exactly as the plan records. */
EPIX_EXPORT struct ColorMaterialUniform {
    /** @brief The tint, in linear RGBA. */
    glm::vec4 color{1.0f};
    /** @brief The UV transform's rows, each padded to a `vec4` so the matrix
     *  occupies the 48 bytes a `mat3x3` takes in a uniform block (a `glm::mat3`
     *  would be 36 and misplace every later field). */
    std::array<glm::vec4, 3> uv_transform{glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec4{0.0f, 1.0f, 0.0f, 0.0f},
                                          glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}};
    /** @brief `ColorMaterialFlags` bits. */
    std::uint32_t flags = 0;
    /** @brief The alpha cutoff used by `AlphaMode2d::Mask`. */
    float alpha_cutoff = 0.5f;
    /** @brief Tail padding to the 16-byte uniform stride (72 -> 80 bytes). */
    float padding[2] = {0.0f, 0.0f};

    /** @brief Compare by value, as Bevy's derive does for `AsBindGroup::Data`,
     *  which is `PartialEq + Eq + Hash + Clone` (`as_bind_group.rs`). Required
     *  because `Material2dKey<M>` compares its `bind_group_data` member, so
     *  without this the key's `operator==` is deleted. */
    bool operator==(const ColorMaterialUniform&) const noexcept = default;
};

}  // namespace epix::sprite_render

namespace epix::render::render_resource {
/** @brief `ColorMaterialUniform` is a fixed 80-byte GPU struct, so the raw
 *  `sizeof`-derived shader size is exactly this uniform's block size. */
template <>
struct ShaderTypeInfo<epix::sprite_render::ColorMaterialUniform>
    : RawShaderType<epix::sprite_render::ColorMaterialUniform> {};
}  // namespace epix::render::render_resource

namespace epix::sprite_render {
/** @brief Renders 2D meshes with a texture tinted by a uniform colour (Bevy
 *  `ColorMaterial`, `color_material.rs:35-88`).
 *
 *  Bevy's `alpha_mode()` trait method and its `alpha_mode` field cannot share a
 *  name in C++, so the field is `mode` here and `alpha_mode()` remains the
 *  accessor the pipeline calls. */
EPIX_EXPORT struct ColorMaterial : Material2d<ColorMaterial> {
    /** @brief The tint, in linear RGBA. */
    glm::vec4 color{1.0f};
    /** @brief How the material's output is composited. */
    AlphaMode2d mode = AlphaMode2d{AlphaMode2dBlend{}};
    /** @brief The UV transform applied before sampling. */
    glm::mat3 uv_transform{1.0f};
    /** @brief The image sampled by this material, if any. */
    std::optional<assets::Handle<image::Image>> texture;

    /** @brief The uniform type this material specializes on (Bevy's
     *  `AsBindGroup::Data`). */
    using Data = ColorMaterialUniform;

    /** @brief This material's fragment shader (Bevy returns the embedded
     *  `color_material.wgsl` path). */
    static std::optional<std::string_view> fragment_shader() {
        return std::string_view{"embedded://sprite/color_material.slang"};
    }
    /** @brief This material's alpha mode (Bevy `Material2d::alpha_mode`). */
    AlphaMode2d alpha_mode() const { return mode; }

    /** @brief A material tinted by @p color (Bevy `ColorMaterial::from_color`). */
    static ColorMaterial from_color(const glm::vec4& color) {
        ColorMaterial material;
        material.color = color;
        material.mode  = color.a < 1.0f ? AlphaMode2d{AlphaMode2dBlend{}} : AlphaMode2d{AlphaMode2dOpaque{}};
        return material;
    }
};
}  // namespace epix::sprite_render

namespace epix::render::render_resource {
/** @brief Convert a `ColorMaterial` to its uniform (Bevy's
 *  `AsBindGroupShaderType<ColorMaterialUniform> for ColorMaterial`,
 *  `color_material.rs:121-145`).  The flags word carries the texture bit and
 *  the alpha-mode discriminator, and the cutoff defaults to 0.5 like 3D. */
template <>
struct AsBindGroupShaderType<epix::sprite_render::ColorMaterial, epix::sprite_render::ColorMaterialUniform> {
    static epix::sprite_render::ColorMaterialUniform as_bind_group_shader_type(
        const epix::sprite_render::ColorMaterial& material, const RenderAssets<texture::GpuImage>&) {
        using epix::sprite_render::ColorMaterialFlags;
        std::uint32_t flags = ColorMaterialFlags::NONE;
        if (material.texture) flags |= ColorMaterialFlags::TEXTURE;

        float alpha_cutoff = 0.5f;
        std::visit(
            [&](const auto& mode) {
                using T = std::remove_cvref_t<decltype(mode)>;
                if constexpr (std::same_as<T, epix::sprite_render::AlphaMode2dOpaque>) {
                    flags |= ColorMaterialFlags::ALPHA_MODE_OPAQUE;
                } else if constexpr (std::same_as<T, epix::sprite_render::AlphaMode2dMask>) {
                    alpha_cutoff = mode.cutoff;
                    flags |= ColorMaterialFlags::ALPHA_MODE_MASK;
                } else {
                    flags |= ColorMaterialFlags::ALPHA_MODE_BLEND;
                }
            },
            material.mode);

        return epix::sprite_render::ColorMaterialUniform{
            .color        = material.color,
            .uv_transform = {glm::vec4{glm::row(material.uv_transform, 0), 0.0f},
                             glm::vec4{glm::row(material.uv_transform, 1), 0.0f},
                             glm::vec4{glm::row(material.uv_transform, 2), 0.0f}},
            .flags        = flags,
            .alpha_cutoff = alpha_cutoff,
        };
    }
};

/** @brief `ColorMaterial`'s bind group: a uniform at 0, texture at 1 and
 *  sampler at 2, as Bevy's derive declares (`color_material.rs:38-45`). */
template <>
struct AsBindGroup<epix::sprite_render::ColorMaterial> {
    using Data  = epix::sprite_render::ColorMaterialUniform;
    /** @brief The images plus the fallback Bevy's derive passes to a textured
     *  material. */
    using Param =
        std::tuple<epix::ecs::Res<RenderAssets<texture::GpuImage>>, epix::ecs::Res<texture::FallbackImage>>;

    static constexpr std::string_view label() { return "color_material"; }

    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() {
        const auto entries = BindGroupLayoutEntries<>::sequential(
            wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
            binding_types::uniform_buffer(
                false, epix::render::render_resource::ShaderTypeInfo<
                           epix::sprite_render::ColorMaterialUniform>::shader_size),
            binding_types::texture_2d(wgpu::TextureSampleType::eFloat),
            binding_types::sampler(wgpu::SamplerBindingType::eFiltering));
        return {entries.begin(), entries.end()};
    }

    static Data bind_group_data(const epix::sprite_render::ColorMaterial& material) {
        return AsBindGroupShaderType<epix::sprite_render::ColorMaterial,
                                     epix::sprite_render::ColorMaterialUniform>::as_bind_group_shader_type(material,
                                                                                                           {});
    }

    /** @brief Own the material's uniform buffer, texture view and sampler
     *  (Bevy's derive, `as_bind_group.rs`): the uniform is created with its
     *  contents, and a material without a texture falls back to the fallback
     *  image, exactly as `#[texture]`/`#[sampler]` do. */
    static std::expected<UnpreparedBindGroup, AsBindGroupError> unprepared_bind_group(
        const wgpu::Device& device,
        const wgpu::BindGroupLayout& layout,
        const epix::sprite_render::ColorMaterial& material,
        const Param& param) {
        (void)layout;
        const auto& [images, fallback_image] = param;

        const auto data = bind_group_data(material);
        std::vector<std::uint8_t> encoded(ShaderTypeInfo<epix::sprite_render::ColorMaterialUniform>::shader_size);
        ShaderTypeInfo<epix::sprite_render::ColorMaterialUniform>::write_into(data, encoded);
        auto uniform = create_buffer_with_data(device, std::as_bytes(std::span{encoded}),
                                               wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst);

        const auto* image = material.texture ? images->try_get(*material.texture) : nullptr;
        if (material.texture && image == nullptr) {
            // Bevy's derive retries when a material declares a texture that is not
            // prepared yet (`as_bind_group.rs:568`: `images.get(handle).ok_or_else(||
            // AsBindGroupError::RetryNextUpdate)`); only a material with no texture
            // at all uses the fallback image.
            return std::unexpected(AsBindGroupError{RetryBindGroupNextUpdate{}});
        }
        const auto& texture_view     = image ? image->texture_view : fallback_image->d2.texture_view;
        const auto& sampler          = image ? image->sampler : fallback_image->d2.sampler;

        return UnpreparedBindGroup{BindingResources{std::vector<BindingResources::value_type>{
            {0u, BindingResources::buffer(std::move(uniform))},
            {1u, BindingResources::texture_view(wgpu::TextureViewDimension::e2D, texture_view)},
            {2u, BindingResources::sampler(wgpu::SamplerBindingType::eFiltering, sampler)},
        }}};
    }
};
}  // namespace epix::render::render_resource
