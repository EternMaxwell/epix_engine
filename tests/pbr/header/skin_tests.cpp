#include <epix/ecs.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/render/mesh/offset_allocator.hpp>
#include <epix/render/sync_world.hpp>
#include <gtest/gtest.h>

#include <cstdint>

namespace pbr    = epix::pbr;
namespace render = epix::render;

/** @brief The skin allocation accessors the batching adapter and the mesh bind
 *  groups read (Bevy `SkinUniforms::{skin_index, skin_byte_offset, all_skins}`,
 *  `bevy_pbr/src/render/skin.rs:156-175`): a skin's offset is reported in joints
 *  and in bytes, a skin that was never allocated reports nothing, and `all_skins`
 *  enumerates what has been allocated.
 *
 *  The buffer-holding type is built without a device here, with empty buffers,
 *  because the accessors read only the allocation bookkeeping; the systems that
 *  fill the buffers are `PBR-315`-like work of their own. */
TEST(Skin, UniformsExposeTheSkinIndexAndByteOffset) {
    const auto skinned_mesh = render::sync_world::MainEntity{epix::ecs::Entity{7}};
    const auto other_mesh   = render::sync_world::MainEntity{epix::ecs::Entity{9}};

    pbr::SkinUniforms uniforms{
        .allocator = render::mesh::offset_allocator::Allocator(pbr::MAX_TOTAL_JOINTS),
    };

    EXPECT_FALSE(uniforms.skin_index(skinned_mesh).has_value()) << "nothing is allocated yet";
    EXPECT_FALSE(uniforms.skin_byte_offset(skinned_mesh).has_value());
    EXPECT_TRUE(uniforms.all_skins().empty());

    // An allocation at unit 3 starts at joint 3 * JOINTS_PER_ALLOCATION_UNIT.
    pbr::SkinUniformInfo info{};
    info.allocation = render::mesh::offset_allocator::Allocation{.offset = 3};
    uniforms.skin_uniform_info.emplace(skinned_mesh, info);

    const std::uint32_t expected_joint = 3u * pbr::JOINTS_PER_ALLOCATION_UNIT;
    ASSERT_TRUE(uniforms.skin_index(skinned_mesh).has_value());
    EXPECT_EQ(*uniforms.skin_index(skinned_mesh), expected_joint);

    const auto byte_offset = uniforms.skin_byte_offset(skinned_mesh);
    ASSERT_TRUE(byte_offset.has_value());
    EXPECT_EQ(byte_offset->index(), expected_joint) << "the byte offset converts back to the same joint";
    EXPECT_EQ(byte_offset->byte_offset, expected_joint * static_cast<std::uint32_t>(sizeof(glm::mat4)));

    // A mesh with no allocation is not reported, and the enumeration holds what
    // has been allocated.
    EXPECT_FALSE(uniforms.skin_index(other_mesh).has_value());
    ASSERT_EQ(uniforms.all_skins().size(), 1u);
    EXPECT_EQ(uniforms.all_skins()[0], skinned_mesh);
}

/** @brief One allocation unit is 256 bytes, which is what makes four 64-byte
 *  joint matrices (Bevy `JOINTS_PER_ALLOCATION_UNIT`, `skin.rs:38-43`). */
TEST(Skin, AllocationUnitIsFourJointMatrices) {
    EXPECT_EQ(pbr::JOINTS_PER_ALLOCATION_UNIT, 4u);
    EXPECT_EQ(pbr::JOINTS_PER_ALLOCATION_UNIT * sizeof(glm::mat4), 256u);
    EXPECT_EQ(pbr::MAX_JOINTS, 256u);
}
