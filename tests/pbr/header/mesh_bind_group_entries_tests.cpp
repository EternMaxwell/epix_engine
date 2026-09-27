#include <epix/pbr/mesh_bindings.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace pbr = epix::pbr;

/** @brief The mesh bind group entries carry the binding they fill and the size
 *  their layout declares (Bevy's `mod bind_group_entry`,
 *  `bevy_pbr/src/render/mesh_bindings.rs:86-160`).
 *
 *  Two sizes matter and both are asserted here: the morph weights are a fixed
 *  slice of the morph buffer, and the joint matrices are a fixed slice only when
 *  skins are uniforms - as storage they bind the whole buffer, which is what the
 *  unsized sentinel means. */
TEST(MeshBindGroupEntries, BufferEntriesCarryTheirBindingAndSize) {
    const wgpu::Buffer buffer{};

    const auto model = pbr::bind_group_entry::model(3u, buffer);
    EXPECT_EQ(model.binding, 3u);
    EXPECT_EQ(model.offset, 0u);
    EXPECT_EQ(model.size, std::numeric_limits<std::uint64_t>::max())
        << "an unsized entry covers the whole buffer";

    const auto weights = pbr::bind_group_entry::weights(2u, buffer);
    EXPECT_EQ(weights.binding, 2u);
    EXPECT_EQ(weights.size, pbr::MORPH_BUFFER_SIZE);

    const auto uniform_skins = pbr::bind_group_entry::skinning(true, 1u, buffer);
    EXPECT_EQ(uniform_skins.binding, 1u);
    EXPECT_EQ(uniform_skins.size, pbr::JOINT_BUFFER_SIZE) << "a uniform slice must match the layout";

    const auto storage_skins = pbr::bind_group_entry::skinning(false, 1u, buffer);
    EXPECT_EQ(storage_skins.binding, 1u);
    EXPECT_EQ(storage_skins.size, std::numeric_limits<std::uint64_t>::max());
}

/** @brief The texture and sampler entries name their binding: the morph target
 *  texture, a lightmap's texture view and its sampler. */
TEST(MeshBindGroupEntries, TextureAndSamplerEntriesCarryTheirBinding) {
    const wgpu::TextureView view{};
    const wgpu::Sampler sampler{};

    EXPECT_EQ(pbr::bind_group_entry::targets(3u, view).binding, 3u);
    EXPECT_EQ(pbr::bind_group_entry::lightmaps_texture_view(4u, view).binding, 4u);
    EXPECT_EQ(pbr::bind_group_entry::lightmaps_sampler(5u, sampler).binding, 5u);
}
