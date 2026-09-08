#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::mesh {

/** @brief Pipeline-key bits shared by mesh render pipelines.
 *
 * Matches Bevy 0.18 `bevy_mesh::BaseMeshPipelineKey`: base mesh bits are
 * allocated from the high end of a 64-bit key so downstream pipelines can use
 * the low bits without shifting.
 */
EPIX_EXPORT struct BaseMeshPipelineKey {
    static const BaseMeshPipelineKey MORPH_TARGETS;
    static constexpr std::uint64_t PRIMITIVE_TOPOLOGY_MASK_BITS = 0b111;
    static constexpr std::uint64_t PRIMITIVE_TOPOLOGY_SHIFT_BITS = 60;

    constexpr BaseMeshPipelineKey() noexcept = default;

    static constexpr BaseMeshPipelineKey from_bits_retain(std::uint64_t bits) noexcept {
        return BaseMeshPipelineKey{bits};
    }

    static constexpr BaseMeshPipelineKey from_primitive_topology(
        wgpu::PrimitiveTopology primitive_topology) noexcept {
        const auto topology_bits =
            (static_cast<std::uint64_t>(primitive_topology) & PRIMITIVE_TOPOLOGY_MASK_BITS)
            << PRIMITIVE_TOPOLOGY_SHIFT_BITS;
        return from_bits_retain(topology_bits);
    }

    constexpr wgpu::PrimitiveTopology primitive_topology() const noexcept {
        const auto topology_bits =
            (bits_ >> PRIMITIVE_TOPOLOGY_SHIFT_BITS) & PRIMITIVE_TOPOLOGY_MASK_BITS;
        switch (topology_bits) {
            case static_cast<std::uint64_t>(wgpu::PrimitiveTopology::ePointList):
                return wgpu::PrimitiveTopology::ePointList;
            case static_cast<std::uint64_t>(wgpu::PrimitiveTopology::eLineList):
                return wgpu::PrimitiveTopology::eLineList;
            case static_cast<std::uint64_t>(wgpu::PrimitiveTopology::eLineStrip):
                return wgpu::PrimitiveTopology::eLineStrip;
            case static_cast<std::uint64_t>(wgpu::PrimitiveTopology::eTriangleList):
                return wgpu::PrimitiveTopology::eTriangleList;
            case static_cast<std::uint64_t>(wgpu::PrimitiveTopology::eTriangleStrip):
                return wgpu::PrimitiveTopology::eTriangleStrip;
            default:
                return wgpu::PrimitiveTopology::eTriangleList;
        }
    }

    constexpr std::uint64_t bits() const noexcept { return bits_; }
    constexpr bool contains(BaseMeshPipelineKey other) const noexcept {
        return (bits_ & other.bits_) == other.bits_;
    }

    constexpr BaseMeshPipelineKey operator|(BaseMeshPipelineKey other) const noexcept {
        return from_bits_retain(bits_ | other.bits_);
    }

    constexpr BaseMeshPipelineKey& operator|=(BaseMeshPipelineKey other) noexcept {
        bits_ |= other.bits_;
        return *this;
    }

    constexpr bool operator==(const BaseMeshPipelineKey&) const noexcept = default;

   private:
    std::uint64_t bits_ = 0;

    constexpr explicit BaseMeshPipelineKey(std::uint64_t bits) noexcept : bits_(bits) {}
};

inline constexpr BaseMeshPipelineKey BaseMeshPipelineKey::MORPH_TARGETS =
    BaseMeshPipelineKey::from_bits_retain(std::uint64_t{1} << 63);

}  // namespace epix::mesh
