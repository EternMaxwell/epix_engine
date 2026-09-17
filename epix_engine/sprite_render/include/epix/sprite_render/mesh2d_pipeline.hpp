#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/core_graph.hpp>
#include <epix/render.hpp>
#include <array>
#include <bit>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::sprite_render {
/** @brief Flags carried per mesh2d instance (Bevy `MeshFlags`).
 *
 * Bevy keeps only the two sentinel values (`mesh.rs:226-233`); the type exists
 * so the shader's `flags` word has a named contract. */
EPIX_EXPORT struct MeshFlags {
    enum Bit : std::uint32_t {
        NONE          = 0,
        UNINITIALIZED = 0xFFFF,
    };
};

/** @brief Per-instance mesh2d transform (Bevy `Mesh2dTransforms` component).
 *
 * Epix stores the transform as the project-wide `glm::mat4`; Bevy's `Affine3`
 * is the 4x3 affine of the same matrix, so the uniform packing below extracts
 * exactly that. */
EPIX_EXPORT struct Mesh2dTransforms {
    glm::mat4 world_from_local{1.0f};
    std::uint32_t flags = MeshFlags::NONE;
};

/** @brief GPU representation of one mesh2d instance (Bevy `Mesh2dUniform`).
 *
 * The field order, padding and total size are the WGSL layout of Bevy's
 * `Mesh2d` struct (`mesh2d_types.wgsl`): a `mat3x4` (three `vec4` rows), a
 * `mat2x4` plus an `f32` carrying the inverse-transpose of the 3x3, then the
 * `flags` and `tag` words, padded to the 16-byte array stride. */
EPIX_EXPORT struct Mesh2dUniform {
    /** @brief Affine 4x3 transposed to 3x4: row `i` is axis `i` plus the
     *  translation component `i` (Bevy `Affine3::to_transpose`). */
    std::array<glm::vec4, 3> world_from_local{};
    /** @brief First eight entries of the packed inverse-transpose 3x3. */
    std::array<glm::vec4, 2> local_from_world_transpose_a{};
    /** @brief Ninth entry of the packed inverse-transpose 3x3. */
    float local_from_world_transpose_b = 0.0f;
    std::uint32_t flags                = MeshFlags::NONE;
    std::uint32_t tag                  = 0;
    /** @brief Tail padding to the 16-byte array stride (92 -> 96 bytes). */
    std::uint32_t padding = 0;

    /** @brief Bevy `Mesh2dUniform::from_components`. */
    static Mesh2dUniform from_components(const Mesh2dTransforms& transforms, std::uint32_t tag) noexcept {
        Mesh2dUniform out;
        const glm::mat4& matrix = transforms.world_from_local;
        // Bevy `Affine3::to_transpose` stores the transposed 4x3, i.e. row `i`
        // is (axis_0[i], axis_1[i], axis_2[i], translation[i]) - the `i`-th
        // component of every column. The shader's `affine3_to_square`
        // transposes that back, so packing columns here would silently render
        // the transpose of the intended transform.
        for (std::size_t axis = 0; axis < 3; ++axis) {
            out.world_from_local[axis] =
                glm::vec4(matrix[0][axis], matrix[1][axis], matrix[2][axis], matrix[3][axis]);
        }
        // Bevy's `Affine3::inverse_transpose_3x3`, packed as the shader's
        // `mat2x4_f32_to_mat3x3_unpack` expects: columns (a0.xyz),
        // (a0.w, a1.x, a1.y) and (a1.z, a1.w, b).
        const glm::mat3 inverse_transpose = glm::transpose(glm::inverse(glm::mat3(matrix)));
        out.local_from_world_transpose_a[0] = glm::vec4(inverse_transpose[0][0], inverse_transpose[0][1],
                                                        inverse_transpose[0][2], inverse_transpose[1][0]);
        out.local_from_world_transpose_a[1] = glm::vec4(inverse_transpose[1][1], inverse_transpose[1][2],
                                                        inverse_transpose[2][0], inverse_transpose[2][1]);
        out.local_from_world_transpose_b = inverse_transpose[2][2];
        out.flags                        = transforms.flags;
        out.tag                          = tag;
        return out;
    }

    /** @brief Rebuild the packed 3x3 exactly as the shader's
     *  `mat2x4_f32_to_mat3x3_unpack` does. */
    glm::mat3 unpack_local_from_world_transpose() const noexcept {
        return glm::mat3(glm::vec3(local_from_world_transpose_a[0]),
                         glm::vec3(local_from_world_transpose_a[0].w, local_from_world_transpose_a[1].x,
                                   local_from_world_transpose_a[1].y),
                         glm::vec3(local_from_world_transpose_a[1].z, local_from_world_transpose_a[1].w,
                                   local_from_world_transpose_b));
    }
};
static_assert(sizeof(Mesh2dUniform) == 96, "Mesh2dUniform must match the WGSL Mesh2d array stride");
static_assert(offsetof(Mesh2dUniform, local_from_world_transpose_a) == 48);
static_assert(offsetof(Mesh2dUniform, local_from_world_transpose_b) == 80);
static_assert(offsetof(Mesh2dUniform, flags) == 84);
static_assert(offsetof(Mesh2dUniform, tag) == 88);


/** @brief Bit-packed Mesh2d specialization key (Bevy `Mesh2dPipelineKey`).
 *
 * Bevy packs the whole Mesh2d specialization state into one `u32`
 * (`bevy_sprite_render/src/mesh2d/mesh.rs:419-492`): the low bits carry the
 * shading switches, the top three bits the MSAA log2(sample count), the three
 * below them the primitive topology and the three below those the tonemapping
 * method. The mesh2d pipeline's specialization reads only this key plus the
 * material's bind group data, which is what lets one pipeline serve every mesh
 * instead of the previous per-variant shader set. */
EPIX_EXPORT struct Mesh2dPipelineKey {
    enum Bit : std::uint32_t {
        NONE                                      = 0,
        HDR                                       = 1u << 0u,
        TONEMAP_IN_SHADER                         = 1u << 1u,
        DEBAND_DITHER                             = 1u << 2u,
        BLEND_ALPHA                               = 1u << 3u,
        MAY_DISCARD                               = 1u << 4u,
        TONEMAP_METHOD_NONE                       = 0u << 23u,
        TONEMAP_METHOD_REINHARD                   = 1u << 23u,
        TONEMAP_METHOD_REINHARD_LUMINANCE         = 2u << 23u,
        TONEMAP_METHOD_ACES_FITTED                = 3u << 23u,
        TONEMAP_METHOD_AGX                        = 4u << 23u,
        TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM = 5u << 23u,
        TONEMAP_METHOD_TONY_MC_MAPFACE            = 6u << 23u,
        TONEMAP_METHOD_BLENDER_FILMIC             = 7u << 23u,
        TONEMAP_METHOD_RESERVED_BITS              = 0b111u << 23u,
        PRIMITIVE_TOPOLOGY_RESERVED_BITS          = 0b111u << 26u,
        MSAA_RESERVED_BITS                        = 0b111u << 29u,
    };

    /** @brief MSAA log2(sample count) occupies the highest three bits. */
    static constexpr std::uint32_t MSAA_MASK_BITS = 0b111u;
    static constexpr std::uint32_t MSAA_SHIFT_BITS = 32u - 3u;
    static constexpr std::uint32_t PRIMITIVE_TOPOLOGY_MASK_BITS = 0b111u;
    static constexpr std::uint32_t PRIMITIVE_TOPOLOGY_SHIFT_BITS = MSAA_SHIFT_BITS - 3u;
    static constexpr std::uint32_t TONEMAP_METHOD_MASK_BITS = 0b111u;
    static constexpr std::uint32_t TONEMAP_METHOD_SHIFT_BITS = PRIMITIVE_TOPOLOGY_SHIFT_BITS - 3u;

    std::uint32_t value = NONE;

    constexpr Mesh2dPipelineKey() noexcept = default;
    constexpr Mesh2dPipelineKey(Bit bit) noexcept : value(static_cast<std::uint32_t>(bit)) {}
    explicit constexpr Mesh2dPipelineKey(std::uint32_t bits) noexcept : value(bits) {}

    /** @brief Bevy `Mesh2dPipelineKey::from_msaa_samples`. */
    static constexpr Mesh2dPipelineKey from_msaa_samples(std::uint32_t msaa_samples) noexcept {
        return Mesh2dPipelineKey{(std::countr_zero(msaa_samples) & MSAA_MASK_BITS) << MSAA_SHIFT_BITS};
    }
    /** @brief Bevy `Mesh2dPipelineKey::from_hdr`. */
    static constexpr Mesh2dPipelineKey from_hdr(bool hdr) noexcept {
        return hdr ? Mesh2dPipelineKey{HDR} : Mesh2dPipelineKey{};
    }
    /** @brief Bevy `Mesh2dPipelineKey::from_primitive_topology`. */
    static constexpr Mesh2dPipelineKey from_primitive_topology(wgpu::PrimitiveTopology topology) noexcept {
        return Mesh2dPipelineKey{(static_cast<std::uint32_t>(topology) & PRIMITIVE_TOPOLOGY_MASK_BITS)
                                 << PRIMITIVE_TOPOLOGY_SHIFT_BITS};
    }

    constexpr std::uint32_t bits() const noexcept { return value; }
    /** @brief Bevy `Mesh2dPipelineKey::msaa_samples`. */
    constexpr std::uint32_t msaa_samples() const noexcept {
        return 1u << ((value >> MSAA_SHIFT_BITS) & MSAA_MASK_BITS);
    }
    /** @brief Bevy `Mesh2dPipelineKey::primitive_topology`, whose unknown
     *  three-bit patterns fall back to the WebGPU default. */
    constexpr wgpu::PrimitiveTopology primitive_topology() const noexcept {
        switch ((value >> PRIMITIVE_TOPOLOGY_SHIFT_BITS) & PRIMITIVE_TOPOLOGY_MASK_BITS) {
            case static_cast<std::uint32_t>(wgpu::PrimitiveTopology::ePointList): return wgpu::PrimitiveTopology::ePointList;
            case static_cast<std::uint32_t>(wgpu::PrimitiveTopology::eLineList): return wgpu::PrimitiveTopology::eLineList;
            case static_cast<std::uint32_t>(wgpu::PrimitiveTopology::eLineStrip): return wgpu::PrimitiveTopology::eLineStrip;
            case static_cast<std::uint32_t>(wgpu::PrimitiveTopology::eTriangleList):
                return wgpu::PrimitiveTopology::eTriangleList;
            case static_cast<std::uint32_t>(wgpu::PrimitiveTopology::eTriangleStrip):
                return wgpu::PrimitiveTopology::eTriangleStrip;
            default: return wgpu::PrimitiveTopology::eTriangleList;
        }
    }

    /** @brief Whether every bit of `bit` is set (Bevy `bitflags::contains`). */
    constexpr bool contains(Bit bit) const noexcept {
        const auto mask = static_cast<std::uint32_t>(bit);
        return (value & mask) == mask;
    }
    /** @brief This key masked to `mask` (Bevy `bitflags::intersection`). */
    constexpr Mesh2dPipelineKey intersection(Bit mask) const noexcept {
        return Mesh2dPipelineKey{value & static_cast<std::uint32_t>(mask)};
    }
    constexpr Mesh2dPipelineKey& operator|=(Mesh2dPipelineKey other) noexcept {
        value |= other.value;
        return *this;
    }
    constexpr Mesh2dPipelineKey& operator|=(Bit bit) noexcept {
        value |= static_cast<std::uint32_t>(bit);
        return *this;
    }
    auto operator<=>(const Mesh2dPipelineKey&) const = default;
};

constexpr Mesh2dPipelineKey operator|(Mesh2dPipelineKey lhs, Mesh2dPipelineKey rhs) noexcept {
    lhs |= rhs;
    return lhs;
}
constexpr Mesh2dPipelineKey operator|(Mesh2dPipelineKey lhs, Mesh2dPipelineKey::Bit rhs) noexcept {
    lhs |= rhs;
    return lhs;
}

/** @brief The Mesh2d render pipeline's shared resources (Bevy `Mesh2dPipeline`).
 *
 * The two layouts are descriptors, as Bevy's are; the pipeline cache interns
 * them when a pipeline is specialized against them. */
EPIX_EXPORT struct Mesh2dPipeline {
    /** @brief View layout descriptor: dynamic view uniform, globals, LUT
     *  texture, LUT sampler (Bevy `Mesh2dPipeline::view_layout`). */
    render::BindGroupLayoutDescriptor view_layout;
    /** @brief Mesh layout descriptor: the per-instance mesh2d uniform array. */
    render::BindGroupLayoutDescriptor mesh_layout;
    /** @brief The embedded mesh2d shader. */
    assets::Handle<shader::Shader> shader;
    /** @brief Elements per binding when the platform has no storage buffers;
     *  empty when the storage-buffer path is used (Bevy
     *  `per_object_buffer_batch_size`). */
    std::optional<std::uint32_t> per_object_buffer_batch_size;
};

/** @brief Insert the `Mesh2dPipeline` resource (Bevy `init_mesh_2d_pipeline`,
 *  `mesh.rs:292-327`). */
EPIX_EXPORT void init_mesh_2d_pipeline(ecs::Commands commands,
                                       ecs::Res<wgpu::Device> device,
                                       ecs::Res<assets::AssetServer> asset_server);

/** @brief Per-view bind group for the mesh2d view bindings (Bevy
 *  `Mesh2dViewBindGroup`, `mesh.rs:675-678`). */
EPIX_EXPORT struct Mesh2dViewBindGroup {
    /** @brief Bind group exposing the view uniform, globals and LUT bindings. */
    wgpu::BindGroup value;
};

/** @brief Prepare one `Mesh2dViewBindGroup` per `Camera2d` view from the mesh2d
 *  view layout (Bevy `prepare_mesh2d_view_bind_groups`, `mesh.rs:680-717`). */
EPIX_EXPORT void prepare_mesh2d_view_bind_groups(
    ecs::Commands commands,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<wgpu::Device> device,
    ecs::Res<Mesh2dPipeline> mesh2d_pipeline,
    ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const core_graph::Tonemapping&>,
               ecs::With<render::view::ExtractedView, epix::camera::Camera2d>>
        views,
    ecs::Res<render::GlobalsBuffer> globals_buffer,
    ecs::Res<core_graph::TonemappingLuts> tonemapping_luts,
    ecs::Res<render::RenderAssets<image::Image>> images,
    ecs::Res<render::texture::FallbackImage> fallback_image);

}  // namespace epix::sprite_render

namespace epix::render::render_resource {
/** @brief `Mesh2dUniform` is a plain 96-byte GPU struct, so the raw
 *  `sizeof`-derived shader size is exactly the WGSL `Mesh2d` array stride. */
template <>
struct ShaderTypeInfo<epix::sprite_render::Mesh2dUniform>
    : RawShaderType<epix::sprite_render::Mesh2dUniform> {};
}  // namespace epix::render::render_resource
