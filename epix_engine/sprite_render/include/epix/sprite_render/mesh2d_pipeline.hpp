#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/core_graph.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/render.hpp>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <glm/glm.hpp>
#include <optional>
#include <variant>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::sprite_render {
/** @brief C++ tagged-union counterpart to Bevy `AlphaMode2d::Opaque`. */
EPIX_EXPORT struct AlphaMode2dOpaque {};
/** @brief C++ tagged-union counterpart to Bevy `AlphaMode2d::Mask(f32)`. */
EPIX_EXPORT struct AlphaMode2dMask {
    float cutoff                                           = 0.5f;
    bool operator==(const AlphaMode2dMask&) const noexcept = default;
};
/** @brief C++ tagged-union counterpart to Bevy `AlphaMode2d::Blend`. */
EPIX_EXPORT struct AlphaMode2dBlend {};
/** @brief Alpha mode for 2D meshes (Bevy `AlphaMode2d`, `material.rs:242-259`). */
EPIX_EXPORT using AlphaMode2d = std::variant<AlphaMode2dOpaque, AlphaMode2dMask, AlphaMode2dBlend>;

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

}  // namespace epix::sprite_render

namespace epix::render::render_resource {
/** @brief `Mesh2dUniform` is a plain 96-byte GPU struct, so the raw
 *  `sizeof`-derived shader size is exactly the WGSL `Mesh2d` array stride. */
template <>
struct ShaderTypeInfo<epix::sprite_render::Mesh2dUniform>
    : RawShaderType<epix::sprite_render::Mesh2dUniform> {};
}  // namespace epix::render::render_resource

namespace epix::sprite_render {


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

}  // namespace epix::sprite_render

/** @brief Hash the packed key bits; the defaulted `operator<=>` supplies the
 *  equality a hash container needs. Bevy derives `Hash` over the same packed
 *  fields (`Mesh2dPipelineKey`). Defined at file scope because an explicit
 *  specialization of a `std` template may not be defined inside another
 *  namespace (a partial specialization may, which is why the sibling
 *  `std::hash<Material2dKey<M>>` can live in `material2d.hpp`). */
template <>
struct std::hash<epix::sprite_render::Mesh2dPipelineKey> {
    std::size_t operator()(const epix::sprite_render::Mesh2dPipelineKey& key) const noexcept {
        return std::hash<std::uint32_t>{}(key.bits());
    }
};

namespace epix::sprite_render {


/** @brief The pipeline key bits an alpha mode contributes (Bevy
 *  `alpha_mode_pipeline_key`, `material.rs:545-552`). */
inline constexpr Mesh2dPipelineKey alpha_mode_pipeline_key(const AlphaMode2d& alpha_mode) noexcept {
    return std::visit(
        [](const auto& mode) -> Mesh2dPipelineKey {
            using T = std::remove_cvref_t<decltype(mode)>;
            if constexpr (std::same_as<T, AlphaMode2dBlend>) {
                return Mesh2dPipelineKey{Mesh2dPipelineKey::BLEND_ALPHA};
            } else if constexpr (std::same_as<T, AlphaMode2dMask>) {
                return Mesh2dPipelineKey{Mesh2dPipelineKey::MAY_DISCARD};
            } else {
                return Mesh2dPipelineKey{Mesh2dPipelineKey::NONE};
            }
        },
        alpha_mode);
}

/** @brief The pipeline key bits a tonemapping method contributes (Bevy
 *  `tonemapping_pipeline_key`, `material.rs:552-...`). */
inline constexpr Mesh2dPipelineKey tonemapping_pipeline_key(const core_graph::Tonemapping& tonemapping) noexcept {
    using core_graph::Tonemapping;
    switch (tonemapping) {
        case Tonemapping::None: return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_NONE};
        case Tonemapping::Reinhard: return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_REINHARD};
        case Tonemapping::ReinhardLuminance:
            return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_REINHARD_LUMINANCE};
        case Tonemapping::AcesFitted: return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_ACES_FITTED};
        case Tonemapping::AgX: return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_AGX};
        case Tonemapping::SomewhatBoringDisplayTransform:
            return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM};
        case Tonemapping::TonyMcMapface:
            return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_TONY_MC_MAPFACE};
        case Tonemapping::BlenderFilmic:
            return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_BLENDER_FILMIC};
    }
    return Mesh2dPipelineKey{Mesh2dPipelineKey::TONEMAP_METHOD_NONE};
}

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
    /** @brief The specialization key of this pipeline (Bevy
     *  `SpecializedMeshPipeline::Key`). */
    using Key = Mesh2dPipelineKey;
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

    /** @brief Build this pipeline's descriptor for one key and mesh layout
     *  (Bevy `SpecializedMeshPipeline for Mesh2dPipeline`, `mesh.rs:494-650`).
     *  The single mesh2d shader is specialized through shader definitions, as
     *  Bevy does, rather than through separate shader variants. */
    std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> specialize(
        const Mesh2dPipelineKey& key, const mesh::MeshVertexBufferLayoutRef& layout) const;
};

/** @brief Insert the `Mesh2dPipeline` resource (Bevy `init_mesh_2d_pipeline`,
 *  `mesh.rs:292-327`). */
EPIX_EXPORT void init_mesh_2d_pipeline(ecs::Commands commands,
                                       ecs::Res<wgpu::Device> device,
                                       ecs::Res<assets::AssetServer> asset_server);


/** @brief The material identity a prepared 2D material resolved to (Bevy
 *  `Material2dBindGroupId`, `material.rs:929`), and the comparand 2D batching
 *  keys on (Bevy's `GetBatchData::CompareData`).
 *
 * Bevy wraps `Option<BindGroupId>`; direct wgpu has no such id, so Epix keeps
 * the material's asset identity instead, exactly as the core 2D phase keys
 * already do (`core2d.hpp` `BinKey2D::material_bind_group_id`). `nullopt` means
 * "no material bind group yet", which is what an instance carries until its
 * material is prepared. */
EPIX_EXPORT struct Material2dBindGroupId {
    /** @brief The material's asset identity, if it has one. */
    std::optional<assets::UntypedAssetId> value = std::nullopt;

    bool operator==(const Material2dBindGroupId&) const noexcept = default;
};

/** @brief Render-world data for one main-world 2D mesh entity (Bevy
 * `RenderMesh2dInstance`).  Binned phase items intentionally carry only a
 * `MainEntity`; draw commands recover their mesh and material data here. */
EPIX_EXPORT struct RenderMesh2dInstance {
    Mesh2dTransforms transforms{};
    assets::AssetId<mesh::Mesh> mesh_asset_id = assets::AssetId<mesh::Mesh>::invalid();
    Material2dBindGroupId material_bind_group_id{};
    bool automatic_batching = true;
    std::uint32_t tag = 0;
};

/** @brief Marks an entity as a 2D mesh (Bevy `Mesh2dMarker`, `mesh.rs:246-247`). */
EPIX_EXPORT struct Mesh2dMarker {};

/** @brief Main-entity keyed render instances (Bevy
 * `RenderMesh2dInstances`).  This is deliberately a resource rather than
 * render-entity components: batchable binned items use `Entity::PLACEHOLDER`.
 */
EPIX_EXPORT struct RenderMesh2dInstances {
    render::sync_world::MainEntityHashMap<RenderMesh2dInstance> instances;

    void clear() noexcept { instances.clear(); }
    auto find(render::sync_world::MainEntity entity) { return instances.find(entity); }
    auto find(render::sync_world::MainEntity entity) const { return instances.find(entity); }
    auto end() { return instances.end(); }
    auto end() const { return instances.end(); }
};
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
    ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
    ecs::Res<render::texture::FallbackImage> fallback_image);

/** @brief The bind group over the shared mesh2d instance buffer (Bevy
 *  `Mesh2dBindGroup`, `mesh.rs:652-655`). */
EPIX_EXPORT struct Mesh2dBindGroup {
    /** @brief Bind group exposing the mesh2d instance array at group 1. */
    wgpu::BindGroup value;
};

/** @brief Build `Mesh2dBindGroup` from the mesh2d mesh layout and the shared
 *  `Mesh2dUniform` instance buffer (Bevy `prepare_mesh2d_bind_group`,
 *  `mesh.rs:657-673`).  Bevy inserts the resource only while the buffer
 *  exists. */
EPIX_EXPORT void prepare_mesh2d_bind_group(
    ecs::Commands commands,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<wgpu::Device> device,
    ecs::Res<Mesh2dPipeline> mesh2d_pipeline,
    ecs::Res<render::batching::BatchedInstanceBuffer<Mesh2dUniform>> mesh2d_uniforms);

/** @brief Binds the mesh2d view bind group at slot `I` (Bevy
 *  `SetMesh2dViewBindGroup`, `mesh.rs:719-737`). Its `RenderCommand<C, P>`
 *  specialisation is generic over the phase, as Bevy's `impl<P: PhaseItem>
 *  RenderCommand<P> for SetMesh2dViewBindGroup<I>` is. */
EPIX_EXPORT template <std::size_t I>
struct SetMesh2dViewBindGroup {};

/** @brief Binds the mesh2d instance bind group at slot `I`, passing the item's
 *  dynamic offset when it has one (Bevy `SetMesh2dBindGroup`,
 *  `mesh.rs:739-766`). */
EPIX_EXPORT template <std::size_t I>
struct SetMesh2dBindGroup {};

/** @brief Draws one batched 2D mesh from the mesh allocator (Bevy
 *  `DrawMesh2d`, `mesh.rs:768-828`).  Missing instance data, a missing GPU mesh
 *  or a missing allocator slice all skip, as they do in Bevy. */
EPIX_EXPORT struct DrawMesh2d {};

}  // namespace epix::sprite_render

namespace epix::render::phase {
/** @brief The mesh2d draw commands' trait specialisations: each command is one
 *  tag type whose `RenderCommand<C, P>` specialisation is generic over the
 *  phase, matching Bevy's `impl<P: PhaseItem> RenderCommand<P> for
 *  SetMesh2dViewBindGroup<I>` and the two that follow it (`mesh.rs:719-828`).
 *  They live here rather than in `sprite_render` because the trait they
 *  specialise does. */
EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::sprite_render::SetMesh2dViewBindGroup<I>, P> {
    using Param     = std::tuple<>;
    using ViewQuery = epix::ecs::Item<const ::epix::render::view::ViewUniformOffset&,
                                      const ::epix::sprite_render::Mesh2dViewBindGroup&>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P&,
                                                   ViewQuery view,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        const auto& offset     = std::get<0>(view);
        const auto& bind_group = std::get<1>(view);
        encoder.setBindGroup(I, bind_group.value, offset.offset);
        return {};
    }
};

EPIX_EXPORT template <std::size_t I, PhaseItem P>
struct RenderCommand<::epix::sprite_render::SetMesh2dBindGroup<I>, P> {
    using Param     = epix::ecs::Res<::epix::sprite_render::Mesh2dBindGroup>;
    using ViewQuery = std::tuple<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   std::tuple<>,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param mesh2d_bind_group,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        // Bevy supplies a dynamic offset only when the item carries one.
        std::array<std::uint32_t, 1> dynamic_offsets{};
        std::size_t offset_count = 0;
        const auto& extra_index  = item.extra_index();
        if (extra_index.type == PhaseItemExtraIndex::Type::DynamicOffset) {
            dynamic_offsets[offset_count++] = extra_index.value;
        }
        encoder.setBindGroup(I, mesh2d_bind_group->value,
                             std::span<const std::uint32_t>{dynamic_offsets.data(), offset_count});
        return {};
    }
};

EPIX_EXPORT template <PhaseItem P>
struct RenderCommand<::epix::sprite_render::DrawMesh2d, P> {
    using Param     = std::tuple<epix::ecs::Res<::epix::render::RenderAssets<::epix::render::mesh::RenderMesh>>,
                                 epix::ecs::Res<::epix::sprite_render::RenderMesh2dInstances>,
                                 epix::ecs::Res<::epix::render::mesh::MeshAllocator>>;
    using ViewQuery = std::tuple<>;
    using ItemQuery = epix::ecs::Item<>;

    void prepare(const epix::ecs::World&) {}

    std::expected<void, RenderCommandError> render(const P& item,
                                                   std::tuple<>,
                                                   std::optional<epix::ecs::Item<>> entity_item,
                                                   Param params,
                                                   const wgpu::RenderPassEncoder& encoder) {
        (void)entity_item;
        auto&& [render_meshes, instances, mesh_allocator] = params;

        const auto instance = instances->find(item.main_entity());
        if (instance == instances->end()) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh2d] Main entity {:#x} has no RenderMesh2dInstance during draw.",
                                       item.main_entity().id().index),
            });
        }
        const auto mesh_asset_id = instance->second.mesh_asset_id;

        auto* render_mesh = render_meshes->try_get(mesh_asset_id);
        if (!render_mesh) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh2d] GPU mesh {} is missing from RenderAssets<Mesh> at draw time.",
                                       mesh_asset_id.to_string_short()),
            });
        }
        const auto vertex_slice = mesh_allocator->mesh_vertex_slice(mesh_asset_id);
        if (!vertex_slice) {
            return std::unexpected(RenderCommandError{
                .type    = RenderCommandError::Type::Skip,
                .message = std::format("[mesh2d] Mesh {} has no MeshAllocator vertex slice at draw time.",
                                       mesh_asset_id.to_string_short()),
            });
        }

        const auto& layout      = render_mesh->layout.value->layout();
        const auto stride       = static_cast<std::uint64_t>(layout.array_stride);
        const auto vertex_first = static_cast<std::uint64_t>(vertex_slice->range.first);
        const auto vertex_count = static_cast<std::uint64_t>(vertex_slice->range.second - vertex_first);
        encoder.setVertexBuffer(0, *vertex_slice->buffer, vertex_first * stride, vertex_count * stride);

        const auto& batch_range = item.batch_range();
        const auto batch_size   = batch_range_len(batch_range);
        if (render_mesh->indexed()) {
            const auto index_slice = mesh_allocator->mesh_index_slice(mesh_asset_id);
            if (!index_slice) {
                return std::unexpected(RenderCommandError{
                    .type    = RenderCommandError::Type::Skip,
                    .message = std::format("[mesh2d] Indexed mesh {} has no MeshAllocator index slice.",
                                           mesh_asset_id.to_string_short()),
                });
            }
            const auto* info                 = render_mesh->buffer_info.indexed_info();
            const wgpu::IndexFormat format   = info ? info->index_format : wgpu::IndexFormat::eUint16;
            const std::uint64_t element_size = format == wgpu::IndexFormat::eUint16 ? 2 : 4;
            const auto index_first           = static_cast<std::uint64_t>(index_slice->range.first);
            const auto index_count           = static_cast<std::uint64_t>(index_slice->range.second - index_first);
            encoder.setIndexBuffer(*index_slice->buffer, format, index_first * element_size,
                                   index_count * element_size);
            encoder.drawIndexed(static_cast<std::uint32_t>(index_count), batch_size, 0, 0, batch_range.first);
        } else {
            encoder.draw(static_cast<std::uint32_t>(vertex_count), batch_size, 0, batch_range.first);
        }
        return {};
    }
};
}  // namespace epix::render::phase

namespace epix::sprite_render {

/** @brief The mesh2d pipeline key of each view, refreshed when it changes
 *  (Bevy `ViewKeyCache`, `mesh.rs:116-117`). */
EPIX_EXPORT struct ViewKeyCache {
    render::sync_world::MainEntityHashMap<Mesh2dPipelineKey> keys;
};

/** @brief When each view last needed specialization (Bevy
 *  `ViewSpecializationTicks`, `mesh.rs:119-120`). */
EPIX_EXPORT struct ViewSpecializationTicks {
    render::sync_world::MainEntityHashMap<ecs::Tick> ticks;
};

/** @brief Refresh each view's mesh2d key from its MSAA, HDR, tonemapping and
 *  deband state, stamping a specialization tick only when the key changes
 *  (Bevy `check_views_need_specialization`, `mesh.rs:122-156`). */
EPIX_EXPORT void check_views_need_specialization(
    ecs::ResMut<ViewKeyCache> view_key_cache,
    ecs::ResMut<ViewSpecializationTicks> view_specialization_ticks,
    ecs::Query<ecs::Item<const render::sync_world::MainEntity&, const render::view::ExtractedView&,
                         const render::view::Msaa&, ecs::Opt<const core_graph::Tonemapping&>,
                         ecs::Opt<const core_graph::DebandDither&>>>
        views,
    ecs::SystemChangeTick ticks);

/** @brief Insert the shared mesh2d instance buffer (Bevy
 *  `init_batched_instance_buffer`, `mesh.rs:158-161`). */
EPIX_EXPORT void init_batched_instance_buffer(ecs::Commands commands, ecs::Res<wgpu::Device> device);



}  // namespace epix::sprite_render

/** @brief How the mesh2d pipeline batches: one `Mesh2dUniform` per instance,
 *  compared by `(material bind group id, mesh)` (Bevy `GetBatchData for
 *  Mesh2dPipeline`, `mesh.rs:329-351`).  `automatic_batching == false` yields
 *  no compare data, which is what makes the item unbatchable. */
template <>
struct epix::render::batching::GetBatchData<epix::sprite_render::Mesh2dPipeline> {
    // A narrow `Res<...>` rather than Bevy's `&World` (mesh.rs:329-351). Bevy can
    // pass `&World` here only because it wraps the adapter param in
    // `StaticSystemParam` (no_gpu_preprocessing.rs:100), which exists for Rust
    // lifetime reasons and whose only effect is to opt out of access-conflict
    // checking — dangerous, and deliberately not offered by Epix. The faithful
    // option within Epix's model is for the adapter to name the one resource it
    // reads.
    using Param       = ecs::Res<epix::sprite_render::RenderMesh2dInstances>;
    using CompareData = std::pair<epix::sprite_render::Material2dBindGroupId, assets::AssetId<epix::mesh::Mesh>>;
    using BufferData  = epix::sprite_render::Mesh2dUniform;

    std::optional<std::pair<BufferData, std::optional<CompareData>>> get_batch_data(
        Param& instances, std::pair<ecs::Entity, epix::render::sync_world::MainEntity> ids) const {
        const auto it = instances->find(ids.second);
        if (it == instances->end()) return std::nullopt;

        const auto& instance = it->second;
        std::optional<CompareData> compare_data;
        if (instance.automatic_batching) {
            compare_data = CompareData{instance.material_bind_group_id, instance.mesh_asset_id};
        }
        return std::pair{
            epix::sprite_render::Mesh2dUniform::from_components(instance.transforms, instance.tag), compare_data};
    }
};

/** @brief The binned side of mesh2d batching (Bevy `GetFullBatchData for
 *  Mesh2dPipeline`, `mesh.rs:353-417`).  The GPU-uniform-building entry points
 *  are unimplemented for 2D meshes in Bevy too, so they log and decline. */
template <>
struct epix::render::batching::GetFullBatchData<epix::sprite_render::Mesh2dPipeline> {
    using BufferInputData = std::monostate;
    using CompareData =
        std::pair<epix::sprite_render::Material2dBindGroupId, assets::AssetId<epix::mesh::Mesh>>;

    std::optional<epix::sprite_render::Mesh2dUniform> get_binned_batch_data(
        GetBatchData<epix::sprite_render::Mesh2dPipeline>::Param& instances,
        epix::render::sync_world::MainEntity entity) const {
        const auto it = instances->find(entity);
        if (it == instances->end()) return std::nullopt;
        return epix::sprite_render::Mesh2dUniform::from_components(it->second.transforms, it->second.tag);
    }

    std::optional<std::pair<std::uint32_t, std::optional<CompareData>>> get_index_and_compare_data(
        GetBatchData<epix::sprite_render::Mesh2dPipeline>::Param&, epix::render::sync_world::MainEntity) const {
        spdlog::error(
            "[mesh2d] get_index_and_compare_data is only intended for GPU mesh uniform building, which is not "
            "implemented for 2d meshes.");
        return std::nullopt;
    }

    std::optional<std::uint32_t> get_binned_index(GetBatchData<epix::sprite_render::Mesh2dPipeline>::Param&, epix::render::sync_world::MainEntity) const {
        spdlog::error(
            "[mesh2d] get_binned_index is only intended for GPU mesh uniform building, which is not implemented "
            "for 2d meshes.");
        return std::nullopt;
    }

    void write_batch_indirect_parameters_metadata(
        bool indexed,
        std::uint32_t base_output_index,
        std::optional<std::uint32_t> batch_set_index,
        epix::render::batching::UntypedPhaseIndirectParametersBuffers& buffers,
        std::uint32_t offset) const {
        // Bevy writes only the CPU metadata here; the draw itself supplies the
        // vertex and instance counts (mesh.rs:389-416).
        buffers.set_cpu_metadata(indexed, offset,
                                 {.base_output_index = base_output_index,
                                  .batch_set_index   = batch_set_index.value_or(!0u)});
    }
};
