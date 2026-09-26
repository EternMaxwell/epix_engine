#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <epix/sprite_render/mesh2d_pipeline.hpp>
#include <webgpu/webgpu.hpp>

namespace sprite_render = epix::sprite_render;

using K = sprite_render::Mesh2dPipelineKey;

// Bevy packs the whole Mesh2d specialization state into one u32
// (bevy_sprite_render/src/mesh2d/mesh.rs:419-492): shading switches in the low
// bits, MSAA log2(sample count) in the top three, primitive topology in the
// three below them and the tonemapping method below those.
TEST(Mesh2dPipelineKey, MatchesBevyBitLayout) {
    EXPECT_EQ(K{K::HDR}.bits(), 1u << 0u);
    EXPECT_EQ(K{K::TONEMAP_IN_SHADER}.bits(), 1u << 1u);
    EXPECT_EQ(K{K::DEBAND_DITHER}.bits(), 1u << 2u);
    EXPECT_EQ(K{K::BLEND_ALPHA}.bits(), 1u << 3u);
    EXPECT_EQ(K{K::MAY_DISCARD}.bits(), 1u << 4u);

    EXPECT_EQ(K::MSAA_MASK_BITS, 0b111u);
    EXPECT_EQ(K::MSAA_SHIFT_BITS, 29u);
    EXPECT_EQ(K::PRIMITIVE_TOPOLOGY_SHIFT_BITS, 26u);
    EXPECT_EQ(K::TONEMAP_METHOD_SHIFT_BITS, 23u);

    EXPECT_EQ(K{K::TONEMAP_METHOD_NONE}.bits(), 0u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_REINHARD}.bits(), 1u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_REINHARD_LUMINANCE}.bits(), 2u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_ACES_FITTED}.bits(), 3u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_AGX}.bits(), 4u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM}.bits(), 5u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_TONY_MC_MAPFACE}.bits(), 6u << 23u);
    EXPECT_EQ(K{K::TONEMAP_METHOD_BLENDER_FILMIC}.bits(), 7u << 23u);

    EXPECT_EQ(static_cast<std::uint32_t>(K::TONEMAP_METHOD_RESERVED_BITS), 0b111u << 23u);
    EXPECT_EQ(static_cast<std::uint32_t>(K::PRIMITIVE_TOPOLOGY_RESERVED_BITS), 0b111u << 26u);
    EXPECT_EQ(static_cast<std::uint32_t>(K::MSAA_RESERVED_BITS), 0b111u << 29u);
    EXPECT_EQ(K{}.bits(), 0u);
}

TEST(Mesh2dPipelineKey, MsaaAndHdrRoundTrip) {
    for (const std::uint32_t samples : std::array{1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u}) {
        const auto key = K::from_msaa_samples(samples);
        EXPECT_EQ(key.msaa_samples(), samples);
        EXPECT_EQ(key.bits(), std::countr_zero(samples) << 29u);
    }
    EXPECT_EQ(K::from_hdr(false).bits(), 0u);
    EXPECT_EQ(K::from_hdr(true).bits(), K{K::HDR}.bits());
}

TEST(Mesh2dPipelineKey, PrimitiveTopologyRoundTrips) {
    const std::array topologies{
        wgpu::PrimitiveTopology::ePointList,
        wgpu::PrimitiveTopology::eLineList,
        wgpu::PrimitiveTopology::eLineStrip,
        wgpu::PrimitiveTopology::eTriangleList,
        wgpu::PrimitiveTopology::eTriangleStrip,
    };
    for (const auto topology : topologies) {
        const auto key = K::from_primitive_topology(topology);
        EXPECT_EQ(static_cast<int>(key.primitive_topology()), static_cast<int>(topology));
        // The topology occupies only the reserved three bits.
        EXPECT_EQ(key.bits() & ~static_cast<std::uint32_t>(K::PRIMITIVE_TOPOLOGY_RESERVED_BITS), 0u);
    }
    // An unknown three-bit pattern decodes to the WebGPU default.
    const auto unknown = K{static_cast<std::uint32_t>(0b111u << 26u)};
    EXPECT_EQ(static_cast<int>(unknown.primitive_topology()),
              static_cast<int>(wgpu::PrimitiveTopology::eTriangleList));
}

TEST(Mesh2dPipelineKey, ContainsAndIntersectionFollowBitflags) {
    const auto key =
        K::from_msaa_samples(4) | K::TONEMAP_IN_SHADER | K::TONEMAP_METHOD_AGX | K::DEBAND_DITHER;
    EXPECT_TRUE(key.contains(K::TONEMAP_IN_SHADER));
    EXPECT_TRUE(key.contains(K::DEBAND_DITHER));
    EXPECT_FALSE(key.contains(K::HDR));
    EXPECT_FALSE(key.contains(K::MAY_DISCARD));
    EXPECT_EQ(key.intersection(K::TONEMAP_METHOD_RESERVED_BITS).bits(), K{K::TONEMAP_METHOD_AGX}.bits());
    EXPECT_EQ(key.msaa_samples(), 4u);

    // Independent keys combine with the same bits a direct construction would.
    const auto combined = K::from_hdr(true) | K::from_msaa_samples(8) | K::BLEND_ALPHA;
    EXPECT_EQ(combined.bits(), (1u << 0u) | (3u << 29u) | (1u << 3u));
    EXPECT_EQ(combined.msaa_samples(), 8u);
}
