#include <epix/ecs.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace pbr    = epix::pbr;
namespace render = epix::render;

/** @brief Removing a mesh's input uniform frees its slot and reports which one
 *  (Bevy `remove_mesh_input_uniform`, `render/mesh.rs:1267-1278`).
 *
 *  No device is involved: the input buffer's `remove` only records the slot as
 *  free for the next `add` to reuse, so the whole operation is checkable as plain
 *  data - which is also why the row could be closed without a GPU test. */
TEST(MeshInstance, RemoveMeshInputUniformFreesTheSlot) {
    render::batching::InstanceInputUniformBuffer<pbr::MeshInputUniform> buffer;
    render::sync_world::MainEntityHashMap<pbr::RenderMeshInstanceGpu> instances;

    // Two instances sharing the buffer, so the freed slot can be seen to be
    // reusable rather than merely gone.
    const render::sync_world::MainEntity first{epix::ecs::Entity{71}};
    const render::sync_world::MainEntity second{epix::ecs::Entity{72}};
    const std::uint32_t first_index  = buffer.add(pbr::MeshInputUniform{});
    const std::uint32_t second_index = buffer.add(pbr::MeshInputUniform{});
    // The shared record has no default constructor - the mesh id is what it is
    // about - so every member is spelled out, as the batching tests do.
    const pbr::RenderMeshInstanceShared shared{
        .mesh_asset_id           = epix::assets::AssetId<epix::mesh::Mesh>::invalid(),
        .material_bindings_index = pbr::MaterialBindingId{},
        .flags                   = pbr::RenderMeshInstanceFlags{},
        .lightmap_slab_index     = std::nullopt,
        .tag                     = 0,
        .render_layers           = std::nullopt,
        .center                  = glm::vec3{0.0f},
    };
    instances.emplace(first, pbr::RenderMeshInstanceGpu{
                                 .shared = shared, .center = glm::vec3{0.0f}, .current_uniform_index = first_index});
    instances.emplace(second, pbr::RenderMeshInstanceGpu{
                                  .shared = shared, .center = glm::vec3{0.0f}, .current_uniform_index = second_index});

    const std::optional<std::uint32_t> removed = pbr::remove_mesh_input_uniform(first, instances, buffer);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, first_index) << "the slot index is handed back for the buffers keyed by it";
    EXPECT_EQ(instances.size(), 1u) << "the instance is forgotten";
    EXPECT_FALSE(instances.contains(first));
    EXPECT_FALSE(buffer.get(first_index).has_value()) << "a freed slot no longer yields data";
    EXPECT_TRUE(buffer.get(second_index).has_value()) << "the other instance is untouched";

    // And the freed slot is what the next addition reuses.
    const std::uint32_t reused = buffer.add(pbr::MeshInputUniform{});
    EXPECT_EQ(reused, first_index) << "the buffer reuses the freed slot";

    // An entity with no instance is a no-op.
    EXPECT_FALSE(pbr::remove_mesh_input_uniform(render::sync_world::MainEntity{epix::ecs::Entity{99}}, instances, buffer)
                     .has_value());
}
