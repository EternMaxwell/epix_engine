#include "epix/render/graph.h"

using namespace epix::render::graph;

EPIX_API GraphInputNode::GraphInputNode(epix::util::ArrayProxy<SlotInfo> inputs)
    : m_inputs(inputs.begin(), inputs.end()) {}
EPIX_API std::vector<SlotInfo> GraphInputNode::inputs() { return m_inputs; }
EPIX_API std::vector<SlotInfo> GraphInputNode::outputs() { return m_inputs; }
EPIX_API void GraphInputNode::run(GraphContext& graph,
                                  RenderContext& ctx,
                                  epix::app::World& world) {
    for (auto&& [index, value] : std::views::enumerate(graph.inputs())) {
        graph.set_output((uint32_t)index, value);
    }
}