module;
#include <epix/extension/fallingsand.hpp>

export module epix.extension.fallingsand;

export namespace epix::ext::fallingsand {
using epix::ext::fallingsand::Action;
using epix::ext::fallingsand::AirCell;
using epix::ext::fallingsand::BodyDebugPlugin;
using epix::ext::fallingsand::ChunkAirGrid;
using epix::ext::fallingsand::ChunkElementGrid;
using epix::ext::fallingsand::ChunkThermalGrid;
using epix::ext::fallingsand::Condition;
using epix::ext::fallingsand::ContactWith;
using epix::ext::fallingsand::Despawn;
using epix::ext::fallingsand::Element;
using epix::ext::fallingsand::ElementAction;
using epix::ext::fallingsand::ElementBase;
using epix::ext::fallingsand::ElementBaseBuilder;
using epix::ext::fallingsand::ElementRegistry;
using epix::ext::fallingsand::ElementRegistryError;
using epix::ext::fallingsand::ElementType;
using epix::ext::fallingsand::Extinguish;
using epix::ext::fallingsand::FallingSandPlugin;
using epix::ext::fallingsand::HasTag;
using epix::ext::fallingsand::Ignite;
using epix::ext::fallingsand::IsBurning;
using epix::ext::fallingsand::kDim;
using epix::ext::fallingsand::MeshBuildByPlugin;
using epix::ext::fallingsand::RandomTick;
using epix::ext::fallingsand::relative_to_world;
using epix::ext::fallingsand::SandChunkBodyDebug;
using epix::ext::fallingsand::SandChunkDirtyRect;
using epix::ext::fallingsand::SandChunkMesh;
using epix::ext::fallingsand::SandChunkOutline;
using epix::ext::fallingsand::SandChunkPos;
using epix::ext::fallingsand::SandChunkRenderChildren;
using epix::ext::fallingsand::SandSimCreateError;
using epix::ext::fallingsand::SandSimulation;
using epix::ext::fallingsand::SandWorld;
using epix::ext::fallingsand::SandWorldDebug;
using epix::ext::fallingsand::SimulatedByPlugin;
using epix::ext::fallingsand::SpawnNearby;
using epix::ext::fallingsand::StagingHeat;
using epix::ext::fallingsand::TemperatureAbove;
using epix::ext::fallingsand::TemperatureBelow;
using epix::ext::fallingsand::ThermalCell;
using epix::ext::fallingsand::TransformTo;
}  // namespace epix::ext::fallingsand

export namespace epix::ext::fallingsand::ops {
using epix::ext::fallingsand::ops::Explode;
using epix::ext::fallingsand::ops::Heat;
using epix::ext::fallingsand::ops::Remove;
using epix::ext::fallingsand::ops::Spawn;
}  // namespace epix::ext::fallingsand::ops

export namespace epix::ext::fallingsand::sand_sim_error {
using epix::ext::fallingsand::sand_sim_error::DuplicateChunkPos;
using epix::ext::fallingsand::sand_sim_error::MissingRequiredLayer;
}  // namespace epix::ext::fallingsand::sand_sim_error
