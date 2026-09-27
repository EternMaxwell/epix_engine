#include <epix/assets/id.hpp>
#include <epix/pbr/material.hpp>
#include <gtest/gtest.h>

#include <optional>

namespace pbr    = epix::pbr;
namespace render = epix::render;

/** @brief The material registry answers for meshes with and without a material
 *  (Bevy `RenderMaterialInstances::mesh_material`, `material.rs:590-595`).
 *
 *  The registry is plain data - a map and a tick - so none of this needs a device.
 *  What matters is the fallback: a mesh drawn by a custom pipeline has no material
 *  record, and the dummy id is what keeps it from being mistaken for a real asset
 *  or dropped outright. */
TEST(MaterialRegistry, MeshMaterialFallsBackToTheDummy) {
    using pbr::RenderMaterialInstance;
    using pbr::RenderMaterialInstances;

    EXPECT_EQ(pbr::DUMMY_MESH_MATERIAL, epix::assets::UntypedAssetId::invalid())
        << "the sentinel stands for \"no material at all\"";

    RenderMaterialInstances instances;
    const render::sync_world::MainEntity meshed{epix::ecs::Entity{31}};
    const render::sync_world::MainEntity unmeshed{epix::ecs::Entity{32}};

    instances.instances.emplace(
        meshed, RenderMaterialInstance{.asset_id = epix::assets::UntypedAssetId::invalid(), .last_change_tick = {}});

    EXPECT_EQ(instances.mesh_material(meshed), epix::assets::UntypedAssetId::invalid());
    EXPECT_EQ(instances.mesh_material(unmeshed), pbr::DUMMY_MESH_MATERIAL)
        << "a mesh with no record gets the dummy id rather than nothing";
}

/** @brief The bindings registry answers only for materials that were prepared
 *  (Bevy `RenderMaterialBindings::get`, `material.rs:1602`).
 *
 *  A miss is how the GPU instance builder learns that a material has not been
 *  prepared yet, and it responds by asking for the mesh to be re-extracted next
 *  frame rather than drawing with a stale binding - so the miss has to be
 *  distinguishable from a binding at slot zero. */
TEST(MaterialRegistry, BindingsDistinguishAMissFromSlotZero) {
    pbr::RenderMaterialBindings bindings;
    const auto missing = epix::assets::UntypedAssetId::invalid();

    EXPECT_FALSE(bindings.get(missing).has_value()) << "an unprepared material is a miss";

    bindings.bindings.emplace(missing,
                              pbr::MaterialBindingId{.group = pbr::MaterialBindGroupIndex{.index = 0},
                                                     .slot  = pbr::MaterialBindGroupSlot{.slot = 0}});
    const std::optional<pbr::MaterialBindingId> found = bindings.get(missing);
    ASSERT_TRUE(found.has_value()) << "a prepared material is found even at slot zero";
    EXPECT_EQ(found->slot.slot, 0u);
}
