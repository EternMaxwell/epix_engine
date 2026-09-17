#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <bit>
#include <cstdint>
#include <webgpu/webgpu.hpp>
#endif

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
struct Mesh2dPipelineKey {
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

}  // namespace epix::sprite_render
