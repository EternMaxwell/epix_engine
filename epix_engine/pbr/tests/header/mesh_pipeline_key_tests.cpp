#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <epix/pbr.hpp>
#include <type_traits>

// Bevy `MeshPipelineKey` (`bevy_pbr/src/render/mesh.rs:2146-2291`) is a `u64`
// bitflags value whose low bits are PBR flags and whose high bits belong to
// `bevy_mesh::BaseMeshPipelineKey`. The ordinal values are part of the shader
// contract (they are passed as shader defs and consumed by `mesh_types.wgsl`), so
// they are asserted directly rather than derived.
TEST(MeshPipelineKey, FlagBitsMatchBevy) {
    using K = epix::pbr::MeshPipelineKey;
    EXPECT_EQ(K::NONE.bits(), 0u);
    EXPECT_EQ(K::MORPH_TARGETS.bits(), std::uint64_t{1} << 63);
    EXPECT_EQ(K::HDR.bits(), std::uint64_t{1} << 0);
    EXPECT_EQ(K::TONEMAP_IN_SHADER.bits(), std::uint64_t{1} << 1);
    EXPECT_EQ(K::DEBAND_DITHER.bits(), std::uint64_t{1} << 2);
    EXPECT_EQ(K::DEPTH_PREPASS.bits(), std::uint64_t{1} << 3);
    EXPECT_EQ(K::NORMAL_PREPASS.bits(), std::uint64_t{1} << 4);
    EXPECT_EQ(K::DEFERRED_PREPASS.bits(), std::uint64_t{1} << 5);
    EXPECT_EQ(K::MOTION_VECTOR_PREPASS.bits(), std::uint64_t{1} << 6);
    EXPECT_EQ(K::MAY_DISCARD.bits(), std::uint64_t{1} << 7);
    EXPECT_EQ(K::ENVIRONMENT_MAP.bits(), std::uint64_t{1} << 8);
    EXPECT_EQ(K::SCREEN_SPACE_AMBIENT_OCCLUSION.bits(), std::uint64_t{1} << 9);
    EXPECT_EQ(K::UNCLIPPED_DEPTH_ORTHO.bits(), std::uint64_t{1} << 10);
    EXPECT_EQ(K::TEMPORAL_JITTER.bits(), std::uint64_t{1} << 11);
    EXPECT_EQ(K::READS_VIEW_TRANSMISSION_TEXTURE.bits(), std::uint64_t{1} << 12);
    EXPECT_EQ(K::LIGHTMAPPED.bits(), std::uint64_t{1} << 13);
    EXPECT_EQ(K::LIGHTMAP_BICUBIC_SAMPLING.bits(), std::uint64_t{1} << 14);
    EXPECT_EQ(K::IRRADIANCE_VOLUME.bits(), std::uint64_t{1} << 15);
    EXPECT_EQ(K::VISIBILITY_RANGE_DITHER.bits(), std::uint64_t{1} << 16);
    EXPECT_EQ(K::SCREEN_SPACE_REFLECTIONS.bits(), std::uint64_t{1} << 17);
    EXPECT_EQ(K::HAS_PREVIOUS_SKIN.bits(), std::uint64_t{1} << 18);
    EXPECT_EQ(K::HAS_PREVIOUS_MORPH.bits(), std::uint64_t{1} << 19);
    EXPECT_EQ(K::OIT_ENABLED.bits(), std::uint64_t{1} << 20);
    EXPECT_EQ(K::DISTANCE_FOG.bits(), std::uint64_t{1} << 21);
    EXPECT_EQ(K::ATMOSPHERE.bits(), std::uint64_t{1} << 22);
    EXPECT_EQ(K::INVERT_CULLING.bits(), std::uint64_t{1} << 23);
    // `LAST_FLAG` is the highest single flag; the MSAA field starts directly
    // above it, which is why the shift is derived from this bit.
    EXPECT_EQ(K::LAST_FLAG.bits(), K::INVERT_CULLING.bits());
    EXPECT_EQ(K::MSAA_SHIFT_BITS, 24u);
    // The PBR flags must not collide with the base key's own bits.
    EXPECT_EQ(K::MORPH_TARGETS.bits() & (K::LAST_FLAG.bits() << 1), 0u);
}

// Bevy's reserved bitfields sit above `LAST_FLAG` and must not overlap each
// other or the primitive-topology bits, which its two `const_assert_eq!`s prove
// (`mesh.rs:2293-2306`) and this reproduces as static assertions.
TEST(MeshPipelineKey, ReservedBitfieldsMatchBevy) {
    using K = epix::pbr::MeshPipelineKey;
    EXPECT_EQ(K::MSAA_RESERVED_BITS.bits(), std::uint64_t{0b111} << 24);
    EXPECT_EQ(K::BLEND_SHIFT_BITS, 27u);
    EXPECT_EQ(K::BLEND_RESERVED_BITS.bits(), std::uint64_t{0b111} << 27);
    EXPECT_EQ(K::BLEND_OPAQUE.bits(), std::uint64_t{0} << 27);
    EXPECT_EQ(K::BLEND_PREMULTIPLIED_ALPHA.bits(), std::uint64_t{1} << 27);
    EXPECT_EQ(K::BLEND_MULTIPLY.bits(), std::uint64_t{2} << 27);
    EXPECT_EQ(K::BLEND_ALPHA.bits(), std::uint64_t{3} << 27);
    EXPECT_EQ(K::BLEND_ALPHA_TO_COVERAGE.bits(), std::uint64_t{4} << 27);
    EXPECT_EQ(K::TONEMAP_METHOD_SHIFT_BITS, 30u);
    EXPECT_EQ(K::TONEMAP_METHOD_NONE.bits(), std::uint64_t{0} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_REINHARD.bits(), std::uint64_t{1} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_REINHARD_LUMINANCE.bits(), std::uint64_t{2} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_ACES_FITTED.bits(), std::uint64_t{3} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_AGX.bits(), std::uint64_t{4} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM.bits(), std::uint64_t{5} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_TONY_MC_MAPFACE.bits(), std::uint64_t{6} << 30);
    EXPECT_EQ(K::TONEMAP_METHOD_BLENDER_FILMIC.bits(), std::uint64_t{7} << 30);
    EXPECT_EQ(K::SHADOW_FILTER_METHOD_SHIFT_BITS, 33u);
    EXPECT_EQ(K::SHADOW_FILTER_METHOD_HARDWARE_2X2.bits(), std::uint64_t{0} << 33);
    EXPECT_EQ(K::SHADOW_FILTER_METHOD_GAUSSIAN.bits(), std::uint64_t{1} << 33);
    EXPECT_EQ(K::SHADOW_FILTER_METHOD_TEMPORAL.bits(), std::uint64_t{2} << 33);
    EXPECT_EQ(K::VIEW_PROJECTION_SHIFT_BITS, 35u);
    EXPECT_EQ(K::VIEW_PROJECTION_NONSTANDARD.bits(), std::uint64_t{0} << 35);
    EXPECT_EQ(K::VIEW_PROJECTION_PERSPECTIVE.bits(), std::uint64_t{1} << 35);
    EXPECT_EQ(K::VIEW_PROJECTION_ORTHOGRAPHIC.bits(), std::uint64_t{2} << 35);
    EXPECT_EQ(K::VIEW_PROJECTION_RESERVED.bits(), std::uint64_t{3} << 35);
    EXPECT_EQ(K::SCREEN_SPACE_SPECULAR_TRANSMISSION_SHIFT_BITS, 37u);
    EXPECT_EQ(K::SCREEN_SPACE_SPECULAR_TRANSMISSION_LOW.bits(), std::uint64_t{0} << 37);
    EXPECT_EQ(K::SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM.bits(), std::uint64_t{1} << 37);
    EXPECT_EQ(K::SCREEN_SPACE_SPECULAR_TRANSMISSION_HIGH.bits(), std::uint64_t{2} << 37);
    EXPECT_EQ(K::SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA.bits(), std::uint64_t{3} << 37);

    const auto expected_reserved = (std::uint64_t{0b111} << 24) | (std::uint64_t{0b111} << 27) |
                                   (std::uint64_t{0b111} << 30) | (std::uint64_t{0b11} << 33) |
                                   (std::uint64_t{0b11} << 35) | (std::uint64_t{0b11} << 37);
    EXPECT_EQ(K::ALL_RESERVED_BITS.bits(), expected_reserved);
    // Bevy's first `const_assert_eq!`: the flag region and every reserved
    // bitfield together must not reach `MORPH_TARGETS`.
    EXPECT_EQ((((K::LAST_FLAG.bits() << 1) - 1) | K::ALL_RESERVED_BITS.bits()) & K::MORPH_TARGETS.bits(), 0u);
    // Bevy's second: the reserved bits must not overlap the topology bits.
    const auto topology_bits = (::epix::mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_MASK_BITS
                                << ::epix::mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_SHIFT_BITS);
    EXPECT_EQ(topology_bits & K::ALL_RESERVED_BITS.bits(), 0u);
}

// `from_msaa_samples` and `msaa_samples` are inverse, and both use the highest
// three bits of the flag region (`mesh.rs:2247-2264`).
TEST(MeshPipelineKey, MsaaRoundTripsThroughTheReservedField) {
    using K = epix::pbr::MeshPipelineKey;
    for (const std::uint32_t samples : {1u, 2u, 4u, 8u}) {
        const auto key = K::from_msaa_samples(samples);
        EXPECT_EQ(key.msaa_samples(), samples) << "samples=" << samples;
        // The MSAA field lives entirely above the flag region, so the sample
        // count changes no flag bit. (`LAST_FLAG << 1` is the field's own first
        // bit, so the flag region is everything below it.)
        EXPECT_EQ((key.bits() & ((K::LAST_FLAG.bits() << 1) - 1)), 0u) << "samples=" << samples;
    }
    // Bevy's `trailing_zeros()` of 0 is 32, and `32 & 0b111` is 0, so the field
    // stays empty rather than trapping.
    EXPECT_EQ(K::from_msaa_samples(0).bits(), 0u);
    EXPECT_EQ(K::from_msaa_samples(0).msaa_samples(), 1u);
    EXPECT_EQ(K::from_hdr(true).bits(), K::HDR.bits());
    EXPECT_EQ(K::from_hdr(false).bits(), K::NONE.bits());
}

// The primitive topology lives in the inherited base-key bits
// (`bevy_mesh/src/lib.rs:40-70`), so a PBR key encodes it without shifting.
TEST(MeshPipelineKey, PrimitiveTopologyRoundTripsThroughTheBaseBits) {
    using K = epix::pbr::MeshPipelineKey;
    for (const auto topology : {wgpu::PrimitiveTopology::ePointList, wgpu::PrimitiveTopology::eLineList,
                                wgpu::PrimitiveTopology::eLineStrip, wgpu::PrimitiveTopology::eTriangleList,
                                wgpu::PrimitiveTopology::eTriangleStrip}) {
        const auto key = K::from_primitive_topology(topology);
        EXPECT_EQ(key.primitive_topology(), topology);
        EXPECT_EQ(key.bits(),
                  ::epix::mesh::BaseMeshPipelineKey::from_primitive_topology(topology).bits());
    }
    // An unused encoding falls back to the default topology, as in Bevy.
    const auto unused = K::from_bits_retain(std::uint64_t{0b111}
                                            << ::epix::mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_SHIFT_BITS);
    EXPECT_EQ(unused.primitive_topology(), wgpu::PrimitiveTopology::eTriangleList);
}

// Set-like operations, including the `intersection` the blend and reserved-field
// lookups rely on.
TEST(MeshPipelineKey, SetOperations) {
    using K = epix::pbr::MeshPipelineKey;
    const auto combined = K::HDR | K::NORMAL_PREPASS;
    EXPECT_TRUE(combined.contains(K::HDR));
    EXPECT_TRUE(combined.contains(K::NORMAL_PREPASS));
    EXPECT_FALSE(combined.contains(K::DEPTH_PREPASS));
    EXPECT_TRUE(combined.intersects(K::NORMAL_PREPASS));
    EXPECT_FALSE(combined.intersects(K::DEPTH_PREPASS));
    EXPECT_EQ(combined.intersection(K::NORMAL_PREPASS).bits(), K::NORMAL_PREPASS.bits());
    EXPECT_EQ(combined.intersection(K::DEPTH_PREPASS).bits(), 0u);
    EXPECT_EQ(((K::HDR | K::HDR)).bits(), K::HDR.bits());
}

// Bevy's `alpha_mode_pipeline_key` (`material.rs:619-633`): premultiplied and
// additive share a key, mask discards, and alpha-to-coverage only blends with
// MSAA.
TEST(MeshPipelineKey, AlphaModeMappingMatchesBevy) {
    using K = epix::pbr::MeshPipelineKey;
    using M = epix::render::AlphaMode;
    using Msaa = epix::render::view::Msaa;
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::opaque(), Msaa::Sample4).bits(), K::NONE.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::blend(), Msaa::Sample4).bits(), K::BLEND_ALPHA.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::premultiplied(), Msaa::Sample4).bits(),
              K::BLEND_PREMULTIPLIED_ALPHA.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::add(), Msaa::Sample4).bits(),
              K::BLEND_PREMULTIPLIED_ALPHA.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::multiply(), Msaa::Sample4).bits(), K::BLEND_MULTIPLY.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::mask(0.3f), Msaa::Sample4).bits(), K::MAY_DISCARD.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::alpha_to_coverage(), Msaa::Sample4).bits(),
              K::BLEND_ALPHA_TO_COVERAGE.bits());
    EXPECT_EQ(epix::pbr::alpha_mode_pipeline_key(M::alpha_to_coverage(), Msaa::Off).bits(), K::MAY_DISCARD.bits());
}

// Bevy's `tonemapping_pipeline_key` (`material.rs:635-649`).
TEST(MeshPipelineKey, TonemappingMappingMatchesBevy) {
    using K = epix::pbr::MeshPipelineKey;
    using T = epix::core_graph::Tonemapping;
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::None).bits(), K::TONEMAP_METHOD_NONE.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::Reinhard).bits(), K::TONEMAP_METHOD_REINHARD.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::ReinhardLuminance).bits(),
              K::TONEMAP_METHOD_REINHARD_LUMINANCE.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::AcesFitted).bits(), K::TONEMAP_METHOD_ACES_FITTED.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::AgX).bits(), K::TONEMAP_METHOD_AGX.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::SomewhatBoringDisplayTransform).bits(),
              K::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::TonyMcMapface).bits(),
              K::TONEMAP_METHOD_TONY_MC_MAPFACE.bits());
    EXPECT_EQ(epix::pbr::tonemapping_pipeline_key(T::BlenderFilmic).bits(),
              K::TONEMAP_METHOD_BLENDER_FILMIC.bits());
}

// Bevy's `screen_space_specular_transmission_pipeline_key` (`material.rs:651-667`).
TEST(MeshPipelineKey, TransmissionQualityMappingMatchesBevy) {
    using K = epix::pbr::MeshPipelineKey;
    using Q = epix::camera::ScreenSpaceTransmissionQuality;
    EXPECT_EQ(epix::pbr::screen_space_specular_transmission_pipeline_key(Q::Low).bits(),
              K::SCREEN_SPACE_SPECULAR_TRANSMISSION_LOW.bits());
    EXPECT_EQ(epix::pbr::screen_space_specular_transmission_pipeline_key(Q::Medium).bits(),
              K::SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM.bits());
    EXPECT_EQ(epix::pbr::screen_space_specular_transmission_pipeline_key(Q::High).bits(),
              K::SCREEN_SPACE_SPECULAR_TRANSMISSION_HIGH.bits());
    EXPECT_EQ(epix::pbr::screen_space_specular_transmission_pipeline_key(Q::Ultra).bits(),
              K::SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA.bits());
}

// Bevy's `MeshPipelineViewLayoutKey` (`mesh_view_bindings.rs:79-202`) has seven
// flags, so there are 128 possible layouts.
TEST(MeshPipelineViewLayoutKey, FlagsAndCountMatchBevy) {
    using V = epix::pbr::MeshPipelineViewLayoutKey;
    EXPECT_EQ(V::MULTISAMPLED.bits(), 1u << 0);
    EXPECT_EQ(V::DEPTH_PREPASS.bits(), 1u << 1);
    EXPECT_EQ(V::NORMAL_PREPASS.bits(), 1u << 2);
    EXPECT_EQ(V::MOTION_VECTOR_PREPASS.bits(), 1u << 3);
    EXPECT_EQ(V::DEFERRED_PREPASS.bits(), 1u << 4);
    EXPECT_EQ(V::OIT_ENABLED.bits(), 1u << 5);
    EXPECT_EQ(V::ATMOSPHERE.bits(), 1u << 6);
    EXPECT_EQ(V::COUNT, 128u);
    static_assert(std::is_trivially_copyable_v<V>);
}

// Each of Bevy's three `From` conversions for the layout key
// (`mesh_view_bindings.rs:139-202`).
TEST(MeshPipelineViewLayoutKey, ConversionsMatchBevy) {
    using K = epix::pbr::MeshPipelineKey;
    using V = epix::pbr::MeshPipelineViewLayoutKey;

    const auto key = K::from_msaa_samples(4) | K::DEPTH_PREPASS | K::NORMAL_PREPASS | K::DEFERRED_PREPASS |
                     K::OIT_ENABLED | K::ATMOSPHERE;
    const auto converted = V::from_pipeline_key(key);
    EXPECT_TRUE(converted.contains(V::MULTISAMPLED));
    EXPECT_TRUE(converted.contains(V::DEPTH_PREPASS));
    EXPECT_TRUE(converted.contains(V::NORMAL_PREPASS));
    EXPECT_TRUE(converted.contains(V::DEFERRED_PREPASS));
    EXPECT_TRUE(converted.contains(V::OIT_ENABLED));
    EXPECT_TRUE(converted.contains(V::ATMOSPHERE));
    // A flag with no layout counterpart contributes nothing.
    EXPECT_FALSE(converted.contains(V::MOTION_VECTOR_PREPASS));
    // `DEPTH_PREPASS`-in-key but `MOTION_VECTOR_PREPASS` not set: the motion flag
    // must come only from the key's own motion bit.
    const auto single = V::from_pipeline_key(K::MOTION_VECTOR_PREPASS);
    EXPECT_TRUE(single.contains(V::MOTION_VECTOR_PREPASS));
    EXPECT_FALSE(single.contains(V::DEPTH_PREPASS));
    // A single-sampled key has no `MULTISAMPLED` flag.
    EXPECT_FALSE(V::from_pipeline_key(K::from_msaa_samples(1)).contains(V::MULTISAMPLED));

    EXPECT_TRUE(V::from_msaa(epix::render::view::Msaa::Sample4).contains(V::MULTISAMPLED));
    EXPECT_FALSE(V::from_msaa(epix::render::view::Msaa::Off).contains(V::MULTISAMPLED));

    // `from_prepass_textures` reads the four optional attachments.
    epix::core_graph::core_3d::ViewPrepassTextures no_textures{};
    EXPECT_EQ(V::from_prepass_textures(nullptr).bits(), 0u);
    EXPECT_EQ(V::from_prepass_textures(&no_textures).bits(), 0u);
    epix::core_graph::core_3d::ViewPrepassTextures textures{};
    textures.depth          = epix::render::render_resource::ColorAttachment{};
    textures.normal         = epix::render::render_resource::ColorAttachment{};
    textures.motion_vectors = epix::render::render_resource::ColorAttachment{};
    textures.deferred       = epix::render::render_resource::ColorAttachment{};
    const auto from_textures = V::from_prepass_textures(&textures);
    EXPECT_TRUE(from_textures.contains(V::DEPTH_PREPASS));
    EXPECT_TRUE(from_textures.contains(V::NORMAL_PREPASS));
    EXPECT_TRUE(from_textures.contains(V::MOTION_VECTOR_PREPASS));
    EXPECT_TRUE(from_textures.contains(V::DEFERRED_PREPASS));
    // The lighting-pass id attachment does not affect the layout.
    EXPECT_EQ(from_textures.bits(),
              (V::DEPTH_PREPASS | V::NORMAL_PREPASS | V::MOTION_VECTOR_PREPASS | V::DEFERRED_PREPASS).bits());
}

// Bevy's `label()` (`mesh_view_bindings.rs:99-136`) is order-sensitive and is
// what names every generated bind group layout.
TEST(MeshPipelineViewLayoutKey, LabelsMatchBevy) {
    using V = epix::pbr::MeshPipelineViewLayoutKey;
    EXPECT_EQ(V{}.label(), "mesh_view_layout");
    EXPECT_EQ(V::MULTISAMPLED.label(), "mesh_view_layout_multisampled");
    EXPECT_EQ(V::DEPTH_PREPASS.label(), "mesh_view_layout_depth");
    EXPECT_EQ((V::MULTISAMPLED | V::DEPTH_PREPASS | V::NORMAL_PREPASS).label(),
              "mesh_view_layout_multisampled_depth_normal");
    EXPECT_EQ((V::MOTION_VECTOR_PREPASS | V::DEFERRED_PREPASS | V::OIT_ENABLED | V::ATMOSPHERE).label(),
              "mesh_view_layout_motion_deferred_oit_atmosphere");
}
