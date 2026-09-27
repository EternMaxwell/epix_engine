#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <gtest/gtest.h>

#include <algorithm>

using namespace epix::app;
using namespace epix::assets;

namespace app = epix::app;
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace render = epix::render;

namespace {
struct StandInMaterial {};


}  // namespace

/** @brief The specialization queue is per-frame and holds exactly the entities
 *  whose mesh or material changed (Bevy `check_entities_needing_specialization<M>`,
 *  `bevy_pbr/src/material.rs:969-990`).
 *
 *  Two things are worth pinning. It is **cleared** each run, because it is a queue
 *  for this frame rather than a set that accumulates - a version that appended
 *  without clearing would grow forever and re-specialize everything. And it only
 *  holds entities that have a material: Bevy's query carries `With<MeshMaterial3d<M>>`,
 *  so a mesh with no material cannot enter the queue however much it changed. */
TEST(MaterialSpecialization, TheQueueIsPerFrameAndNeedsAMaterial) {
    App application = App::create();
    application.world_mut().init_resource<pbr::EntitiesNeedingSpecialization<StandInMaterial>>();
    application.add_systems(app::ScheduleInfo(PostUpdate),
                            ecs::into(pbr::check_entities_needing_specialization<StandInMaterial>)
                                .set_name("check entities needing specialization"));

    const auto handle = Handle<StandInMaterial>{AssetId<StandInMaterial>::invalid()};
    const ecs::Entity meshed =
        application.world_mut().spawn(epix::mesh::Mesh3d{}, pbr::MeshMaterial3d<StandInMaterial>{handle}).id();
    const ecs::Entity plain = application.world_mut().spawn(epix::mesh::Mesh3d{}).id();

    application.update();
    const auto first = application.resource<pbr::EntitiesNeedingSpecialization<StandInMaterial>>().entities;
    EXPECT_NE(std::find(first.begin(), first.end(), meshed), first.end())
        << "a mesh whose material is new needs specialization";
    EXPECT_EQ(std::find(first.begin(), first.end(), plain), first.end())
        << "a mesh with no material cannot need material specialization";

    // Second frame: the queue was cleared and nothing changed, so it is empty.
    application.update();
    const auto second = application.resource<pbr::EntitiesNeedingSpecialization<StandInMaterial>>().entities;
    EXPECT_TRUE(second.empty())
        << "the queue is per-frame: cleared on each run, not accumulated";
}

/** @brief The specialization tick pair carries **two** ticks (Bevy
 *  `EntitySpecializationTickPair`, `bevy_pbr/src/material.rs:944-952`).
 *
 *  The pair is easy to mistake for a map of ticks - the 2D counterpart is exactly
 *  that - but in 3D it is a struct of two, and the map lives in
 *  `EntitySpecializationTicks`. This test exists because that mistake was made once
 *  while porting: the two ticks are separate because either can invalidate a
 *  specialization, and they are advanced by different systems. */
TEST(MaterialSpecialization, TheTickPairCarriesBothTicks) {
    pbr::EntitySpecializationTickPair pair{.system_tick = ecs::Tick{3}, .material_instances_tick = ecs::Tick{7}};
    EXPECT_EQ(pair.system_tick.get(), 3u);
    EXPECT_EQ(pair.material_instances_tick.get(), 7u) << "the registry tick is not the system tick";

    pbr::EntitySpecializationTicks ticks;
    ticks.entities.insert_or_assign(render::sync_world::MainEntity{ecs::Entity{}}, pair);
    EXPECT_EQ(ticks.entities.size(), 1u) << "and the map is the resource, not the pair";
}