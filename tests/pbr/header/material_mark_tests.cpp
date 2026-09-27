#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <gtest/gtest.h>

#include <vector>

using namespace epix::app;
using namespace epix::assets;

namespace app = epix::app;
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;

namespace {
/** @brief A stand-in material type: the registry is untyped and the system is
 *  templated only to name the component, so no `Material` implementation is
 *  needed to drive it. */
struct StandInMaterial {};

/** @brief What the probe saw: the entities whose `Mesh3d` the mark system
 *  touched this frame. */
struct SeenMeshes {
    std::vector<ecs::Entity> entities;
};

void probe_changed_meshes(ecs::Query<ecs::Item<ecs::Entity>, ecs::Filter<ecs::Modified<epix::mesh::Mesh3d>>> changed,
                          ecs::ResMut<SeenMeshes> seen) {
    for (auto&& [entity] : changed.iter()) {
        seen->entities.push_back(entity);
    }
}

void clear_seen(ecs::ResMut<SeenMeshes> seen) { seen->entities.clear(); }
}  // namespace

/** @brief A material change reaches the mesh's change tick (Bevy
 *  `mark_meshes_as_changed_if_their_materials_changed<M>`,
 *  `bevy_pbr/src/material.rs:679-691`).
 *
 *  This is the link the whole material path depends on: the extraction queries
 *  watch `Mesh3d`, so a material swap that only touched `MeshMaterial3d` would
 *  never be extracted. The second frame is the important half - the system must
 *  *mark*, not re-mark every frame, or every mesh would be re-extracted forever. */
TEST(MaterialMark, AMaterialChangeReachesTheMeshTickOnce) {
    App application = App::create();
    application.world_mut().init_resource<SeenMeshes>();
    application.add_systems(app::ScheduleInfo(PostUpdate),
                            ecs::into(pbr::mark_meshes_as_changed_if_their_materials_changed<StandInMaterial>)
                                .set_name("mark meshes as changed"));
    application.add_systems(app::ScheduleInfo(Last), ecs::into(probe_changed_meshes).set_name("probe changed meshes"));
    application.add_systems(app::ScheduleInfo(First), ecs::into(clear_seen).set_name("clear seen"));

    const auto handle = Handle<StandInMaterial>{AssetId<StandInMaterial>::invalid()};
    const ecs::Entity meshed = application.world_mut().spawn(epix::mesh::Mesh3d{}, pbr::MeshMaterial3d<StandInMaterial>{handle}).id();
    const ecs::Entity plain = application.world_mut().spawn(epix::mesh::Mesh3d{}).id();

    // First frame: the material component is new, so the mesh is marked.
    application.update();
    const auto first = application.resource<SeenMeshes>().entities;
    EXPECT_NE(std::find(first.begin(), first.end(), meshed), first.end())
        << "a mesh with a material change is marked";
    // The plain mesh is deliberately not asserted here: whether the port's
    // `Modified<Mesh3d>` reports a component added this frame is a change-detection
    // semantic this test has not pinned, and asserting it would be asserting a guess.
    (void)plain;

    // Second frame: nothing changed, so the system marks nothing - it must not
    // re-mark on every frame.
    application.update();
    const auto second = application.resource<SeenMeshes>().entities;
    EXPECT_EQ(std::find(second.begin(), second.end(), meshed), second.end())
        << "and it does not re-mark an unchanged mesh every frame";
}
