#include "epix/render.hpp"
#include "epix/render/graph.hpp"

void epix::render::render_system(epix::World& world) {
    auto&& graph  = world.resource_mut<graph::RenderGraph>();
    auto&& device = world.resource<nvrhi::DeviceHandle>();
    graph.update(world);
    graph::RenderGraphRunner::run(graph, device, world, {});
}