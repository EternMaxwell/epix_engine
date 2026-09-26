#pragma once


#ifndef EPIX_CXX_MODULE
#include <bit>
#include <compare>
#include <cstdint>
#include <epix/camera.hpp>
#include <epix/core_graph/core3d.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/core_graph/tonemapping.hpp>
#include <epix/mesh/base_mesh_pipeline.hpp>
#include <epix/render.hpp>
#include <string>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/**
 * @brief The specialization key for every PBR mesh pipeline (Bevy
 * `MeshPipelineKey`, `bevy_pbr/src/render/mesh.rs:2146`).
 *
 * Bevy splits the 64-bit key in two: `bevy_mesh::BaseMeshPipelineKey` owns the
 * high bits (morph targets and the primitive topology), and this type owns the
 * low bits, which is why neither side has to shift the other's values. MSAA
 * occupies the highest three bits of the flag region and the remaining bitfields
 * (blend, tonemapping method, shadow filter method, view projection, screen-space
 * specular transmission) sit above `LAST_FLAG`.
 *
 * The flag values are `static const` members defined `inline constexpr` below the
 * class, matching `BaseMeshPipelineKey::MORPH_TARGETS`: a class cannot hold a
 * `constexpr` member of its own incomplete type, and an out-of-class
 * `inline constexpr` definition keeps every flag usable from other translation
 * units and from constant expressions, so the conversions and the key-mapping
 * functions below stay `constexpr` exactly as Bevy's `const fn` are.
 */
EPIX_EXPORT class MeshPipelineKey {
   public:
    /** @brief No flags set. */
    static const MeshPipelineKey NONE;
    /** @brief The mesh has morph targets (inherited from the base key). */
    static const MeshPipelineKey MORPH_TARGETS;

    /** @brief The view renders to an HDR target. */
    static const MeshPipelineKey HDR;
    /** @brief Tonemapping happens in this pipeline's shader. */
    static const MeshPipelineKey TONEMAP_IN_SHADER;
    /** @brief Deband dithering is applied in the shader. */
    static const MeshPipelineKey DEBAND_DITHER;
    /** @brief The pipeline writes the depth prepass texture. */
    static const MeshPipelineKey DEPTH_PREPASS;
    /** @brief The pipeline writes the normals prepass texture. */
    static const MeshPipelineKey NORMAL_PREPASS;
    /** @brief The pipeline writes the deferred G-buffer. */
    static const MeshPipelineKey DEFERRED_PREPASS;
    /** @brief The pipeline writes the motion-vector prepass texture. */
    static const MeshPipelineKey MOTION_VECTOR_PREPASS;
    /** @brief Guards shader branches that may discard, which keeps the early
     * depth test available in most cases
     * (<https://www.khronos.org/opengl/wiki/Early_Fragment_Test>). */
    static const MeshPipelineKey MAY_DISCARD;
    /** @brief The view has an environment map. */
    static const MeshPipelineKey ENVIRONMENT_MAP;
    /** @brief The view has screen-space ambient occlusion. */
    static const MeshPipelineKey SCREEN_SPACE_AMBIENT_OCCLUSION;
    /** @brief Disables depth clipping for directional-light shadow views, which
     * is emulated by a fragment-shader depth write where the hardware lacks it
     * (<https://www.w3.org/TR/webgpu/#depth-clipping>). */
    static const MeshPipelineKey UNCLIPPED_DEPTH_ORTHO;
    /** @brief The view applies a temporal jitter. */
    static const MeshPipelineKey TEMPORAL_JITTER;
    /** @brief The pipeline reads the view transmission texture. */
    static const MeshPipelineKey READS_VIEW_TRANSMISSION_TEXTURE;
    /** @brief The mesh samples a lightmap. */
    static const MeshPipelineKey LIGHTMAPPED;
    /** @brief The lightmap is sampled bicubically. */
    static const MeshPipelineKey LIGHTMAP_BICUBIC_SAMPLING;
    /** @brief The view has irradiance volumes. */
    static const MeshPipelineKey IRRADIANCE_VOLUME;
    /** @brief Visibility-range dithering is applied in the shader. */
    static const MeshPipelineKey VISIBILITY_RANGE_DITHER;
    /** @brief The view has screen-space reflections. */
    static const MeshPipelineKey SCREEN_SPACE_REFLECTIONS;
    /** @brief The mesh had a skin last frame. */
    static const MeshPipelineKey HAS_PREVIOUS_SKIN;
    /** @brief The mesh had morph targets last frame. */
    static const MeshPipelineKey HAS_PREVIOUS_MORPH;
    /** @brief Order-independent transparency is enabled for the view. */
    static const MeshPipelineKey OIT_ENABLED;
    /** @brief The view has distance fog. */
    static const MeshPipelineKey DISTANCE_FOG;
    /** @brief The view has an atmosphere. */
    static const MeshPipelineKey ATMOSPHERE;
    /** @brief The view inverts its culling mode. */
    static const MeshPipelineKey INVERT_CULLING;
    /** @brief The highest single flag bit; the MSAA field starts above it. */
    static const MeshPipelineKey LAST_FLAG;

    /** @brief Reserved MSAA bits. */
    static const MeshPipelineKey MSAA_RESERVED_BITS;
    /** @brief Reserved alpha-blend bits. */
    static const MeshPipelineKey BLEND_RESERVED_BITS;
    /** @brief Opaque: no blending, depth written. */
    static const MeshPipelineKey BLEND_OPAQUE;
    /** @brief Premultiplied alpha blending. */
    static const MeshPipelineKey BLEND_PREMULTIPLIED_ALPHA;
    /** @brief Multiplicative blending. */
    static const MeshPipelineKey BLEND_MULTIPLY;
    /** @brief Standard alpha blending. */
    static const MeshPipelineKey BLEND_ALPHA;
    /** @brief Alpha-to-coverage, which needs MSAA. */
    static const MeshPipelineKey BLEND_ALPHA_TO_COVERAGE;
    /** @brief Reserved tonemapping-method bits. */
    static const MeshPipelineKey TONEMAP_METHOD_RESERVED_BITS;
    /** @brief The tonemapping-method field for `Tonemapping::None`. */
    static const MeshPipelineKey TONEMAP_METHOD_NONE;
    /** @brief The tonemapping-method field for `Tonemapping::Reinhard`. */
    static const MeshPipelineKey TONEMAP_METHOD_REINHARD;
    /** @brief The tonemapping-method field for `Tonemapping::ReinhardLuminance`. */
    static const MeshPipelineKey TONEMAP_METHOD_REINHARD_LUMINANCE;
    /** @brief The tonemapping-method field for `Tonemapping::AcesFitted`. */
    static const MeshPipelineKey TONEMAP_METHOD_ACES_FITTED;
    /** @brief The tonemapping-method field for `Tonemapping::AgX`. */
    static const MeshPipelineKey TONEMAP_METHOD_AGX;
    /** @brief The tonemapping-method field for
     * `Tonemapping::SomewhatBoringDisplayTransform`. */
    static const MeshPipelineKey TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM;
    /** @brief The tonemapping-method field for `Tonemapping::TonyMcMapface`. */
    static const MeshPipelineKey TONEMAP_METHOD_TONY_MC_MAPFACE;
    /** @brief The tonemapping-method field for `Tonemapping::BlenderFilmic`. */
    static const MeshPipelineKey TONEMAP_METHOD_BLENDER_FILMIC;
    /** @brief Reserved shadow-filter-method bits. */
    static const MeshPipelineKey SHADOW_FILTER_METHOD_RESERVED_BITS;
    /** @brief Hardware 2x2 PCF shadow filtering. */
    static const MeshPipelineKey SHADOW_FILTER_METHOD_HARDWARE_2X2;
    /** @brief Gaussian shadow filtering. */
    static const MeshPipelineKey SHADOW_FILTER_METHOD_GAUSSIAN;
    /** @brief Temporal shadow filtering. */
    static const MeshPipelineKey SHADOW_FILTER_METHOD_TEMPORAL;
    /** @brief Reserved view-projection bits. */
    static const MeshPipelineKey VIEW_PROJECTION_RESERVED_BITS;
    /** @brief A custom projection. */
    static const MeshPipelineKey VIEW_PROJECTION_NONSTANDARD;
    /** @brief A perspective projection. */
    static const MeshPipelineKey VIEW_PROJECTION_PERSPECTIVE;
    /** @brief An orthographic projection. */
    static const MeshPipelineKey VIEW_PROJECTION_ORTHOGRAPHIC;
    /** @brief Unused view-projection encoding. */
    static const MeshPipelineKey VIEW_PROJECTION_RESERVED;
    /** @brief Reserved screen-space specular transmission quality bits. */
    static const MeshPipelineKey SCREEN_SPACE_SPECULAR_TRANSMISSION_RESERVED_BITS;
    /** @brief Low transmission blur quality. */
    static const MeshPipelineKey SCREEN_SPACE_SPECULAR_TRANSMISSION_LOW;
    /** @brief Medium transmission blur quality. */
    static const MeshPipelineKey SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM;
    /** @brief High transmission blur quality. */
    static const MeshPipelineKey SCREEN_SPACE_SPECULAR_TRANSMISSION_HIGH;
    /** @brief Ultra transmission blur quality. */
    static const MeshPipelineKey SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA;
    /** @brief The union of every reserved bitfield. */
    static const MeshPipelineKey ALL_RESERVED_BITS;

    /** @brief Bits per MSAA field. */
    static constexpr std::uint64_t MSAA_MASK_BITS = 0b111;
    /** @brief First bit of the MSAA field, directly above `LAST_FLAG`.
     *
     * Bevy derives this as `LAST_FLAG.bits().trailing_zeros() + 1`
     * (`render/mesh.rs:2231`). The class body cannot compute it that way, because
     * `LAST_FLAG` is only defined out of class, so the value is written out and
     * the `static_assert`s below prove it is exactly that derivation. */
    static constexpr std::uint64_t MSAA_SHIFT_BITS = 24;
    /** @brief Bits per blend field. */
    static constexpr std::uint64_t BLEND_MASK_BITS = 0b111;
    /** @brief First bit of the blend field. */
    static constexpr std::uint64_t BLEND_SHIFT_BITS = 27;
    /** @brief Bits per tonemapping-method field. */
    static constexpr std::uint64_t TONEMAP_METHOD_MASK_BITS = 0b111;
    /** @brief First bit of the tonemapping-method field. */
    static constexpr std::uint64_t TONEMAP_METHOD_SHIFT_BITS = 30;
    /** @brief Bits per shadow-filter-method field. */
    static constexpr std::uint64_t SHADOW_FILTER_METHOD_MASK_BITS = 0b11;
    /** @brief First bit of the shadow-filter-method field. */
    static constexpr std::uint64_t SHADOW_FILTER_METHOD_SHIFT_BITS = 33;
    /** @brief Bits per view-projection field. */
    static constexpr std::uint64_t VIEW_PROJECTION_MASK_BITS = 0b11;
    /** @brief First bit of the view-projection field. */
    static constexpr std::uint64_t VIEW_PROJECTION_SHIFT_BITS = 35;
    /** @brief Bits per screen-space specular transmission quality field. */
    static constexpr std::uint64_t SCREEN_SPACE_SPECULAR_TRANSMISSION_MASK_BITS = 0b11;
    /** @brief First bit of the transmission-quality field. */
    static constexpr std::uint64_t SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS = 37;

    constexpr MeshPipelineKey() noexcept = default;

    /** @brief Build a key from raw bits, keeping unknown bits (Bevy
     * `from_bits_retain`). */
    static constexpr MeshPipelineKey from_bits_retain(std::uint64_t bits) noexcept {
        return MeshPipelineKey{bits};
    }

    /** @brief The MSAA field for a sample count (Bevy `from_msaa_samples`). */
    static constexpr MeshPipelineKey from_msaa_samples(std::uint32_t msaa_samples) noexcept {
        // Bevy's `trailing_zeros()` of 0 is 32; the mask then drops every bit, so
        // the field is empty rather than trapping.
        const auto trailing =
            msaa_samples == 0 ? std::uint64_t{0} : static_cast<std::uint64_t>(std::countr_zero(msaa_samples));
        const auto msaa_bits = (trailing & MSAA_MASK_BITS) << MSAA_SHIFT_BITS;
        return from_bits_retain(msaa_bits);
    }

    /** @brief The HDR flag (Bevy `from_hdr`). */
    static constexpr MeshPipelineKey from_hdr(bool hdr) noexcept { return hdr ? HDR : NONE; }

    /** @brief The primitive topology bits (Bevy `from_primitive_topology`). */
    static constexpr MeshPipelineKey from_primitive_topology(wgpu::PrimitiveTopology primitive_topology) noexcept {
        return from_bits_retain(::epix::mesh::BaseMeshPipelineKey::from_primitive_topology(primitive_topology).bits());
    }

    /** @brief The sample count encoded in the MSAA field (Bevy `msaa_samples`). */
    constexpr std::uint32_t msaa_samples() const noexcept {
        return 1u << ((bits_ >> MSAA_SHIFT_BITS) & MSAA_MASK_BITS);
    }

    /** @brief The primitive topology encoded in the base bits (Bevy
     * `primitive_topology`). */
    constexpr wgpu::PrimitiveTopology primitive_topology() const noexcept {
        return ::epix::mesh::BaseMeshPipelineKey::from_bits_retain(bits_).primitive_topology();
    }

    /** @brief The raw bits. */
    constexpr std::uint64_t bits() const noexcept { return bits_; }

    /** @brief Whether every bit of `other` is set. */
    constexpr bool contains(MeshPipelineKey other) const noexcept { return (bits_ & other.bits_) == other.bits_; }

    /** @brief Whether any bit of `other` is set. */
    constexpr bool intersects(MeshPipelineKey other) const noexcept { return (bits_ & other.bits_) != 0; }

    /** @brief The bits present in both keys. */
    constexpr MeshPipelineKey intersection(MeshPipelineKey other) const noexcept {
        return from_bits_retain(bits_ & other.bits_);
    }

    constexpr MeshPipelineKey operator|(MeshPipelineKey other) const noexcept {
        return from_bits_retain(bits_ | other.bits_);
    }
    constexpr MeshPipelineKey& operator|=(MeshPipelineKey other) noexcept {
        bits_ |= other.bits_;
        return *this;
    }
    constexpr MeshPipelineKey operator&(MeshPipelineKey other) const noexcept {
        return from_bits_retain(bits_ & other.bits_);
    }
    constexpr MeshPipelineKey operator~() const noexcept { return from_bits_retain(~bits_); }

    constexpr bool operator==(const MeshPipelineKey&) const noexcept = default;
    constexpr std::strong_ordering operator<=>(const MeshPipelineKey& other) const noexcept {
        return bits_ <=> other.bits_;
    }

   private:
    std::uint64_t bits_ = 0;
    constexpr explicit MeshPipelineKey(std::uint64_t bits) noexcept : bits_(bits) {}
    friend struct MeshPipelineKeyValue;
};

/**
 * @brief The key that selects one of the mesh pipeline's view bind group layouts
 * (Bevy `MeshPipelineViewLayoutKey`, `mesh_view_bindings.rs:79`).
 *
 * Bevy generates every combination of these flags up front, because the number of
 * possible layouts grows exponentially, which is why the flag set must stay
 * small. `COUNT` is `all().bits() + 1`.
 */
EPIX_EXPORT class MeshPipelineViewLayoutKey {
   public:
    static const MeshPipelineViewLayoutKey MULTISAMPLED;
    static const MeshPipelineViewLayoutKey DEPTH_PREPASS;
    static const MeshPipelineViewLayoutKey NORMAL_PREPASS;
    static const MeshPipelineViewLayoutKey MOTION_VECTOR_PREPASS;
    static const MeshPipelineViewLayoutKey DEFERRED_PREPASS;
    static const MeshPipelineViewLayoutKey OIT_ENABLED;
    static const MeshPipelineViewLayoutKey ATMOSPHERE;

    /** @brief The number of possible layouts (Bevy
     * `MeshPipelineViewLayoutKey::COUNT`). */
    static constexpr std::uint32_t COUNT = 128;

    constexpr MeshPipelineViewLayoutKey() noexcept = default;

    /** @brief Build a key from raw bits (Bevy `from_bits_truncate`). */
    static constexpr MeshPipelineViewLayoutKey from_bits_retain(std::uint32_t bits) noexcept {
        return MeshPipelineViewLayoutKey{bits};
    }

    /** @brief A layout key holding the flags a pipeline key implies (Bevy
     * `From<MeshPipelineKey>`). */
    static constexpr MeshPipelineViewLayoutKey from_pipeline_key(MeshPipelineKey key) noexcept;

    /** @brief A layout key holding only `MULTISAMPLED` for a non-off MSAA (Bevy
     * `From<Msaa>`). */
    static constexpr MeshPipelineViewLayoutKey from_msaa(render::view::Msaa msaa) noexcept;

    /** @brief A layout key describing which prepass textures a view has (Bevy
     * `From<Option<&ViewPrepassTextures>>`). */
    static constexpr MeshPipelineViewLayoutKey from_prepass_textures(
        const core_graph::core_3d::ViewPrepassTextures* prepass_textures) noexcept;

    /** @brief The unique label Bevy builds from the flags (Bevy
     * `MeshPipelineViewLayoutKey::label`). */
    EPIX_API std::string label() const;

    /** @brief The raw bits. */
    constexpr std::uint32_t bits() const noexcept { return bits_; }

    /** @brief Whether every bit of `other` is set. */
    constexpr bool contains(MeshPipelineViewLayoutKey other) const noexcept {
        return (bits_ & other.bits_) == other.bits_;
    }

    constexpr MeshPipelineViewLayoutKey operator|(MeshPipelineViewLayoutKey other) const noexcept {
        return from_bits_retain(bits_ | other.bits_);
    }
    constexpr MeshPipelineViewLayoutKey& operator|=(MeshPipelineViewLayoutKey other) noexcept {
        bits_ |= other.bits_;
        return *this;
    }
    constexpr bool operator==(const MeshPipelineViewLayoutKey&) const noexcept = default;
    constexpr std::strong_ordering operator<=>(const MeshPipelineViewLayoutKey& other) const noexcept {
        return bits_ <=> other.bits_;
    }

   private:
    std::uint32_t bits_ = 0;
    constexpr explicit MeshPipelineViewLayoutKey(std::uint32_t bits) noexcept : bits_(bits) {}
    friend struct MeshPipelineViewLayoutKeyValue;
};

/** @brief Defines the `static const` flag members of a bitflags-like key. */
#define EPIX_PBR_KEY_VALUE(type, name, bits) \
    inline constexpr type type::name = type::from_bits_retain(bits)

EPIX_PBR_KEY_VALUE(MeshPipelineKey, NONE, 0);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, MORPH_TARGETS, std::uint64_t{1} << 63);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, HDR, std::uint64_t{1} << 0);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_IN_SHADER, std::uint64_t{1} << 1);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, DEBAND_DITHER, std::uint64_t{1} << 2);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, DEPTH_PREPASS, std::uint64_t{1} << 3);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, NORMAL_PREPASS, std::uint64_t{1} << 4);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, DEFERRED_PREPASS, std::uint64_t{1} << 5);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, MOTION_VECTOR_PREPASS, std::uint64_t{1} << 6);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, MAY_DISCARD, std::uint64_t{1} << 7);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, ENVIRONMENT_MAP, std::uint64_t{1} << 8);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SCREEN_SPACE_AMBIENT_OCCLUSION, std::uint64_t{1} << 9);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, UNCLIPPED_DEPTH_ORTHO, std::uint64_t{1} << 10);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TEMPORAL_JITTER, std::uint64_t{1} << 11);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, READS_VIEW_TRANSMISSION_TEXTURE, std::uint64_t{1} << 12);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, LIGHTMAPPED, std::uint64_t{1} << 13);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, LIGHTMAP_BICUBIC_SAMPLING, std::uint64_t{1} << 14);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, IRRADIANCE_VOLUME, std::uint64_t{1} << 15);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, VISIBILITY_RANGE_DITHER, std::uint64_t{1} << 16);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SCREEN_SPACE_REFLECTIONS, std::uint64_t{1} << 17);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, HAS_PREVIOUS_SKIN, std::uint64_t{1} << 18);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, HAS_PREVIOUS_MORPH, std::uint64_t{1} << 19);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, OIT_ENABLED, std::uint64_t{1} << 20);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, DISTANCE_FOG, std::uint64_t{1} << 21);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, ATMOSPHERE, std::uint64_t{1} << 22);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, INVERT_CULLING, std::uint64_t{1} << 23);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, LAST_FLAG, std::uint64_t{1} << 23);

EPIX_PBR_KEY_VALUE(MeshPipelineKey, MSAA_RESERVED_BITS,
                   MeshPipelineKey::MSAA_MASK_BITS << MeshPipelineKey::MSAA_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, BLEND_RESERVED_BITS,
                   MeshPipelineKey::BLEND_MASK_BITS << MeshPipelineKey::BLEND_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, BLEND_OPAQUE, std::uint64_t{0} << MeshPipelineKey::BLEND_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, BLEND_PREMULTIPLIED_ALPHA,
                   std::uint64_t{1} << MeshPipelineKey::BLEND_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, BLEND_MULTIPLY, std::uint64_t{2} << MeshPipelineKey::BLEND_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, BLEND_ALPHA, std::uint64_t{3} << MeshPipelineKey::BLEND_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, BLEND_ALPHA_TO_COVERAGE,
                   std::uint64_t{4} << MeshPipelineKey::BLEND_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_RESERVED_BITS,
                   MeshPipelineKey::TONEMAP_METHOD_MASK_BITS << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_NONE,
                   std::uint64_t{0} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_REINHARD,
                   std::uint64_t{1} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_REINHARD_LUMINANCE,
                   std::uint64_t{2} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_ACES_FITTED,
                   std::uint64_t{3} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_AGX,
                   std::uint64_t{4} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM,
                   std::uint64_t{5} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_TONY_MC_MAPFACE,
                   std::uint64_t{6} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, TONEMAP_METHOD_BLENDER_FILMIC,
                   std::uint64_t{7} << MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SHADOW_FILTER_METHOD_RESERVED_BITS,
                   MeshPipelineKey::SHADOW_FILTER_METHOD_MASK_BITS << MeshPipelineKey::SHADOW_FILTER_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SHADOW_FILTER_METHOD_HARDWARE_2X2,
                   std::uint64_t{0} << MeshPipelineKey::SHADOW_FILTER_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SHADOW_FILTER_METHOD_GAUSSIAN,
                   std::uint64_t{1} << MeshPipelineKey::SHADOW_FILTER_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SHADOW_FILTER_METHOD_TEMPORAL,
                   std::uint64_t{2} << MeshPipelineKey::SHADOW_FILTER_METHOD_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, VIEW_PROJECTION_RESERVED_BITS,
                   MeshPipelineKey::VIEW_PROJECTION_MASK_BITS << MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, VIEW_PROJECTION_NONSTANDARD,
                   std::uint64_t{0} << MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, VIEW_PROJECTION_PERSPECTIVE,
                   std::uint64_t{1} << MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, VIEW_PROJECTION_ORTHOGRAPHIC,
                   std::uint64_t{2} << MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, VIEW_PROJECTION_RESERVED,
                   std::uint64_t{3} << MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(
    MeshPipelineKey, SCREEN_SPACE_SPECULAR_TRANSMISSION_RESERVED_BITS,
    MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_MASK_BITS
        << MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SCREEN_SPACE_SPECULAR_TRANSMISSION_LOW,
                   std::uint64_t{0} << MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM,
                   std::uint64_t{1} << MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SCREEN_SPACE_SPECULAR_TRANSMISSION_HIGH,
                   std::uint64_t{2} << MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA,
                   std::uint64_t{3} << MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS);
EPIX_PBR_KEY_VALUE(MeshPipelineKey, ALL_RESERVED_BITS,
                   MeshPipelineKey::BLEND_RESERVED_BITS.bits() | MeshPipelineKey::MSAA_RESERVED_BITS.bits() |
                       MeshPipelineKey::TONEMAP_METHOD_RESERVED_BITS.bits() |
                       MeshPipelineKey::SHADOW_FILTER_METHOD_RESERVED_BITS.bits() |
                       MeshPipelineKey::VIEW_PROJECTION_RESERVED_BITS.bits() |
                       MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_RESERVED_BITS.bits());

// Every reserved field starts immediately above the previous one, and Bevy
// derives each shift from the one below it (`render/mesh.rs:2229-2251`). The
// class body cannot spell that for `MSAA_SHIFT_BITS`, because `LAST_FLAG` is
// defined out of class, so the chain is proved here instead: a field narrowed or
// widened without moving the next shift would otherwise overlap its neighbour
// silently.
static_assert(MeshPipelineKey::MSAA_SHIFT_BITS ==
                  static_cast<std::uint64_t>(std::countr_zero(MeshPipelineKey::LAST_FLAG.bits())) + 1,
              "the MSAA field must start directly above LAST_FLAG (bevy_pbr/src/render/mesh.rs:2231)");
static_assert(MeshPipelineKey::BLEND_SHIFT_BITS ==
                  static_cast<std::uint64_t>(std::popcount(MeshPipelineKey::MSAA_MASK_BITS)) +
                      MeshPipelineKey::MSAA_SHIFT_BITS,
              "the blend field must start above the seven MSAA bits (bevy_pbr/src/render/mesh.rs:2234)");
static_assert(MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS ==
                  static_cast<std::uint64_t>(std::popcount(MeshPipelineKey::BLEND_MASK_BITS)) +
                      MeshPipelineKey::BLEND_SHIFT_BITS,
              "the tonemapping field must start above the seven blend bits (bevy_pbr/src/render/mesh.rs:2237)");
static_assert(MeshPipelineKey::SHADOW_FILTER_METHOD_SHIFT_BITS ==
                  static_cast<std::uint64_t>(std::popcount(MeshPipelineKey::TONEMAP_METHOD_MASK_BITS)) +
                      MeshPipelineKey::TONEMAP_METHOD_SHIFT_BITS,
              "the shadow-filter field must start above the seven tonemapping bits (bevy_pbr/src/render/mesh.rs:2241)");
static_assert(MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS ==
                  static_cast<std::uint64_t>(std::popcount(MeshPipelineKey::SHADOW_FILTER_METHOD_MASK_BITS)) +
                      MeshPipelineKey::SHADOW_FILTER_METHOD_SHIFT_BITS,
              "the view-projection field must start above the two shadow-filter bits (mesh.rs:2245)");
static_assert(
    MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS ==
        static_cast<std::uint64_t>(std::popcount(MeshPipelineKey::VIEW_PROJECTION_MASK_BITS)) +
            MeshPipelineKey::VIEW_PROJECTION_SHIFT_BITS,
    "the transmission-quality field must start above the two view-projection bits (mesh.rs:2250)");

// Bevy's first `const_assert_eq!` (`render/mesh.rs:2293-2298`): every flag bit and
// every reserved bitfield together must not reach the base key's bits. Bevy writes
// `BaseMeshPipelineKey::all()`, which for that bitflags type is exactly
// `MORPH_TARGETS`, the only flag it declares.
static_assert(((((MeshPipelineKey::LAST_FLAG.bits() << 1) - 1) | MeshPipelineKey::ALL_RESERVED_BITS.bits()) &
               ::epix::mesh::BaseMeshPipelineKey::MORPH_TARGETS.bits()) == 0,
              "MeshPipelineKey overflowed the bits below the base key's MORPH_TARGETS");

// Bevy's second `const_assert_eq!` (`render/mesh.rs:2300-2306`): the reserved bits
// must not overlap the primitive-topology bits.
static_assert(((::epix::mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_MASK_BITS
                << ::epix::mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_SHIFT_BITS) &
               MeshPipelineKey::ALL_RESERVED_BITS.bits()) == 0,
              "the reserved bits must not overlap the primitive-topology bits");

EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, MULTISAMPLED, std::uint32_t{1} << 0);
EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, DEPTH_PREPASS, std::uint32_t{1} << 1);
EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, NORMAL_PREPASS, std::uint32_t{1} << 2);
EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, MOTION_VECTOR_PREPASS, std::uint32_t{1} << 3);
EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, DEFERRED_PREPASS, std::uint32_t{1} << 4);
EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, OIT_ENABLED, std::uint32_t{1} << 5);
EPIX_PBR_KEY_VALUE(MeshPipelineViewLayoutKey, ATMOSPHERE, std::uint32_t{1} << 6);

#undef EPIX_PBR_KEY_VALUE

inline constexpr MeshPipelineViewLayoutKey MeshPipelineViewLayoutKey::from_pipeline_key(
    MeshPipelineKey key) noexcept {
    std::uint32_t result = 0;
    if (key.msaa_samples() > 1) result |= MULTISAMPLED.bits_;
    if (key.contains(MeshPipelineKey::DEPTH_PREPASS)) result |= DEPTH_PREPASS.bits_;
    if (key.contains(MeshPipelineKey::NORMAL_PREPASS)) result |= NORMAL_PREPASS.bits_;
    if (key.contains(MeshPipelineKey::MOTION_VECTOR_PREPASS)) result |= MOTION_VECTOR_PREPASS.bits_;
    if (key.contains(MeshPipelineKey::DEFERRED_PREPASS)) result |= DEFERRED_PREPASS.bits_;
    if (key.contains(MeshPipelineKey::OIT_ENABLED)) result |= OIT_ENABLED.bits_;
    if (key.contains(MeshPipelineKey::ATMOSPHERE)) result |= ATMOSPHERE.bits_;
    return from_bits_retain(result);
}

inline constexpr MeshPipelineViewLayoutKey MeshPipelineViewLayoutKey::from_msaa(render::view::Msaa msaa) noexcept {
    return render::view::samples(msaa) > 1 ? MULTISAMPLED : MeshPipelineViewLayoutKey{};
}

inline constexpr MeshPipelineViewLayoutKey MeshPipelineViewLayoutKey::from_prepass_textures(
    const core_graph::core_3d::ViewPrepassTextures* prepass_textures) noexcept {
    std::uint32_t result = 0;
    if (prepass_textures) {
        if (prepass_textures->depth) result |= DEPTH_PREPASS.bits_;
        if (prepass_textures->normal) result |= NORMAL_PREPASS.bits_;
        if (prepass_textures->motion_vectors) result |= MOTION_VECTOR_PREPASS.bits_;
        if (prepass_textures->deferred) result |= DEFERRED_PREPASS.bits_;
    }
    return from_bits_retain(result);
}

/** @brief The blend and discard bits an alpha mode asks for (Bevy
 * `alpha_mode_pipeline_key`, `material.rs:619`).
 *
 * Premultiplied and additive share one key and the shader separates them through
 * `premultiply_alpha()`; alpha-to-coverage falls back to `MAY_DISCARD` without
 * MSAA. Opacity is the fallthrough, so `Opaque` gets no bits. */
EPIX_EXPORT constexpr MeshPipelineKey alpha_mode_pipeline_key(render::AlphaMode alpha_mode,
                                                              render::view::Msaa msaa) noexcept {
    switch (alpha_mode.type) {
        case render::AlphaMode::Type::Premultiplied:
        case render::AlphaMode::Type::Add:
            return MeshPipelineKey::BLEND_PREMULTIPLIED_ALPHA;
        case render::AlphaMode::Type::Blend:
            return MeshPipelineKey::BLEND_ALPHA;
        case render::AlphaMode::Type::Multiply:
            return MeshPipelineKey::BLEND_MULTIPLY;
        case render::AlphaMode::Type::Mask:
            return MeshPipelineKey::MAY_DISCARD;
        case render::AlphaMode::Type::AlphaToCoverage:
            return msaa == render::view::Msaa::Off ? MeshPipelineKey::MAY_DISCARD
                                                   : MeshPipelineKey::BLEND_ALPHA_TO_COVERAGE;
        default:
            return MeshPipelineKey::NONE;
    }
}

/** @brief The tonemapping-method field for a method (Bevy
 * `tonemapping_pipeline_key`, `material.rs:635`). */
EPIX_EXPORT constexpr MeshPipelineKey tonemapping_pipeline_key(core_graph::Tonemapping tonemapping) noexcept {
    switch (tonemapping) {
        case core_graph::Tonemapping::None:
            return MeshPipelineKey::TONEMAP_METHOD_NONE;
        case core_graph::Tonemapping::Reinhard:
            return MeshPipelineKey::TONEMAP_METHOD_REINHARD;
        case core_graph::Tonemapping::ReinhardLuminance:
            return MeshPipelineKey::TONEMAP_METHOD_REINHARD_LUMINANCE;
        case core_graph::Tonemapping::AcesFitted:
            return MeshPipelineKey::TONEMAP_METHOD_ACES_FITTED;
        case core_graph::Tonemapping::AgX:
            return MeshPipelineKey::TONEMAP_METHOD_AGX;
        case core_graph::Tonemapping::SomewhatBoringDisplayTransform:
            return MeshPipelineKey::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM;
        case core_graph::Tonemapping::TonyMcMapface:
            return MeshPipelineKey::TONEMAP_METHOD_TONY_MC_MAPFACE;
        case core_graph::Tonemapping::BlenderFilmic:
            return MeshPipelineKey::TONEMAP_METHOD_BLENDER_FILMIC;
    }
    return MeshPipelineKey::TONEMAP_METHOD_NONE;
}

/** @brief The transmission-quality field for a quality level (Bevy
 * `screen_space_specular_transmission_pipeline_key`, `material.rs:651`). */
EPIX_EXPORT constexpr MeshPipelineKey screen_space_specular_transmission_pipeline_key(
    ::epix::camera::ScreenSpaceTransmissionQuality screen_space_transmissive_blur_quality) noexcept {
    switch (screen_space_transmissive_blur_quality) {
        case ::epix::camera::ScreenSpaceTransmissionQuality::Low:
            return MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_LOW;
        case ::epix::camera::ScreenSpaceTransmissionQuality::Medium:
            return MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM;
        case ::epix::camera::ScreenSpaceTransmissionQuality::High:
            return MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_HIGH;
        case ::epix::camera::ScreenSpaceTransmissionQuality::Ultra:
            return MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA;
    }
    return MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM;
}

}  // namespace epix::pbr
