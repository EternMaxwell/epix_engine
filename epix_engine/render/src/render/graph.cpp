#include "epix/render/graph.hpp"

using namespace epix::render::graph;

void RenderGraph::update(epix::World& world) {
    for (auto&& [id, node] : nodes) {
        node.pnode->update(world);
    }
    for (auto&& [id, sub_graph] : sub_graphs) {
        sub_graph.update(world);
    }
}

bool RenderGraph::set_input(std::span<const SlotInfo> inputs) {
    if (nodes.contains(GraphInput)) {
        spdlog::warn("Graph input node already exists. Ignoring set_input.");
        return false;
    }
    nodes.emplace(GraphInput, NodeState(GraphInput, new GraphInputNode(inputs)));
    return true;
}

const NodeState* RenderGraph::get_input_node() const {
    auto iter = nodes.find(GraphInput);
    if (iter != nodes.end()) {
        return &iter->second;
    }
    return nullptr;
}
const NodeState& RenderGraph::input_node() const {
    auto iter = nodes.find(GraphInput);
    if (iter != nodes.end()) {
        return iter->second;
    }
    throw std::runtime_error("Input node not found.");
}

std::expected<void, GraphError> RenderGraph::remove_node(const NodeLabel& id) {
    if (auto state = get_node_state(id)) {
        for (auto&& edge : state->edges.input_edges()) {
            auto output_node = get_node_state(edge.output_node);
            if (output_node) {
                output_node->edges.remove_output_edge(edge);
            }
        }
        for (auto&& edge : state->edges.output_edges()) {
            auto input_node = get_node_state(edge.input_node);
            if (input_node) {
                input_node->edges.remove_input_edge(edge);
            }
        }
        nodes.erase(id);
        return {};
    }
    return std::unexpected(NodeNotPresent{id});
}

std::expected<void, EdgeError> RenderGraph::validate_edge(const Edge& edge, bool should_exist) {
    // this is a slot edge, check if the slot matches.
    auto output_node = get_node_state(edge.output_node);
    auto input_node  = get_node_state(edge.input_node);
    if (!output_node || !input_node) {
        return std::unexpected(EdgeNodesNotPresent{
            .output_node    = edge.output_node,
            .input_node     = edge.input_node,
            .missing_output = !output_node,
            .missing_input  = !input_node,
        });
    }
    if (!edge.is_slot_edge()) return {};
    auto from_slot = output_node->outputs.get_slot(edge.output_index);
    auto to_slot   = input_node->inputs.get_slot(edge.input_index);
    if (!from_slot || !to_slot) {
        return std::unexpected(SlotNotPresent{
            .output_node       = edge.output_node,
            .output_slot       = edge.output_index,
            .input_node        = edge.input_node,
            .input_slot        = edge.input_index,
            .missing_in_output = !from_slot,
            .missing_in_input  = !to_slot,
        });
    }

    // check if the input's input slot has not been connected to any other
    // node if should_exist is false
    if (auto to_input_edge_it =
            std::find_if(input_node->edges.input_edges().begin(), input_node->edges.input_edges().end(),
                         [&edge](const Edge& e) -> bool {
                             if (!e.is_slot_edge()) return false;
                             return e.input_index == edge.input_index;
                         });
        to_input_edge_it != input_node->edges.input_edges().end()) {
        if (!should_exist) {
            return std::unexpected(InputSlotOccupied{
                .input_node            = edge.input_node,
                .input_index           = edge.input_index,
                .current_output_node   = to_input_edge_it->output_node,
                .current_output_index  = to_input_edge_it->output_index,
                .required_output_node  = edge.output_node,
                .required_output_index = edge.output_index,
            });
        }
    }

    // check if the slot types match
    if (from_slot->type != to_slot->type) {
        return std::unexpected(SlotTypeMismatch{
            .output_node  = edge.output_node,
            .output_index = edge.output_index,
            .input_node   = edge.input_node,
            .input_index  = edge.input_index,
            .output_type  = from_slot->type,
            .input_type   = to_slot->type,
        });
    }

    return {};
}

std::expected<void, GraphError> RenderGraph::try_add_node_edge(const NodeLabel& output_node,
                                                               const NodeLabel& input_node) {
    auto edge  = Edge::node_edge(output_node, input_node);
    auto valid = validate_edge(edge, false);
    if (!valid) {
        return std::unexpected(valid.error());
    }

    auto input_node_state  = get_node_state(input_node);
    auto output_node_state = get_node_state(output_node);
    if (!input_node_state || !output_node_state) {
        return std::unexpected<EdgeError>(EdgeNodesNotPresent{
            .output_node    = output_node,
            .input_node     = input_node,
            .missing_output = !output_node_state,
            .missing_input  = !input_node_state,
        });
    }
    output_node_state->edges.add_output_edge(edge);
    input_node_state->edges.add_input_edge(edge);

    return {};
}
void RenderGraph::add_node_edge(const NodeLabel& output_node, const NodeLabel& input_node) {
    auto res = try_add_node_edge(output_node, input_node)
                   .or_else([&](const GraphError& error) -> std::expected<void, GraphError> {
                       throw std::runtime_error(std::format("Failed to add node edge {} -> {}: {}",
                                                            output_node.type_index().short_name(),
                                                            input_node.type_index().short_name(), error.to_string()));
                   });
}

std::expected<void, GraphError> RenderGraph::try_add_slot_edge(const NodeLabel& output_node,
                                                               const SlotLabel& output_slot,
                                                               const NodeLabel& input_node,
                                                               const SlotLabel& input_slot) {
    auto output_node_state = get_node_state(output_node);
    auto input_node_state  = get_node_state(input_node);
    if (!output_node_state || !input_node_state) {
        return std::unexpected<EdgeError>{EdgeNodesNotPresent{
            .output_node    = output_node,
            .input_node     = input_node,
            .missing_output = !output_node_state,
            .missing_input  = !input_node_state,
        }};
    }
    auto output_index = output_node_state->outputs.get_slot_index(output_slot);
    auto input_index  = input_node_state->inputs.get_slot_index(input_slot);
    if (!output_index || !input_index) {
        return std::unexpected<EdgeError>{SlotNotPresent{
            .output_node       = output_node,
            .output_slot       = output_slot,
            .input_node        = input_node,
            .input_slot        = input_slot,
            .missing_in_output = !output_index,
            .missing_in_input  = !input_index,
        }};
    }
    auto edge  = Edge::slot_edge(output_node, *output_index, input_node, *input_index);
    auto valid = validate_edge(edge, false);
    if (!valid) {
        return std::unexpected(std::move(valid.error()));
    }

    output_node_state->edges.add_output_edge(edge);
    input_node_state->edges.add_input_edge(edge);

    return {};
}
void RenderGraph::add_slot_edge(const NodeLabel& output_node,
                                const SlotLabel& output_slot,
                                const NodeLabel& input_node,
                                const SlotLabel& input_slot) {
    auto res = try_add_slot_edge(output_node, output_slot, input_node, input_slot)
                   .or_else([&](const GraphError& error) -> std::expected<void, GraphError> {
                       throw std::runtime_error(std::format("Failed to add slot edge {} -> {}: {}",
                                                            output_node.type_index().short_name(),
                                                            input_node.type_index().short_name(), error.to_string()));
                   });
}
std::expected<void, GraphError> RenderGraph::remove_slot_edge(const NodeLabel& output_node,
                                                              const SlotLabel& output_slot,
                                                              const NodeLabel& input_node,
                                                              const SlotLabel& input_slot) {
    auto output_node_state = get_node_state(output_node);
    auto input_node_state  = get_node_state(input_node);
    if (!output_node_state || !input_node_state) {
        return std::unexpected<EdgeError>(EdgeNodesNotPresent{
            .output_node    = output_node,
            .input_node     = input_node,
            .missing_output = !output_node_state,
            .missing_input  = !input_node_state,
        });
    }
    auto output_index = output_node_state->outputs.get_slot_index(output_slot);
    auto input_index  = input_node_state->inputs.get_slot_index(input_slot);
    if (!output_index || !input_index) {
        return std::unexpected<EdgeError>(SlotNotPresent{
            .output_node       = output_node,
            .output_slot       = output_slot,
            .input_node        = input_node,
            .input_slot        = input_slot,
            .missing_in_output = !output_index,
            .missing_in_input  = !input_index,
        });
    }
    auto edge = Edge::slot_edge(output_node, *output_index, input_node, *input_index);
    return validate_edge(edge, true).and_then([&]() -> std::expected<void, EdgeError> {
        output_node_state->edges.remove_output_edge(edge);
        input_node_state->edges.remove_input_edge(edge);
        return {};
    });
}
std::expected<void, GraphError> RenderGraph::remove_node_edge(const NodeLabel& output_node,
                                                              const NodeLabel& input_node) {
    auto edge = Edge::node_edge(output_node, input_node);
    return validate_edge(edge, true).and_then([&, edge]() -> std::expected<void, EdgeError> {
        auto output_node_state = get_node_state(output_node);
        auto input_node_state  = get_node_state(input_node);
        if (output_node_state && input_node_state) {
            output_node_state->edges.remove_output_edge(edge);
            input_node_state->edges.remove_input_edge(edge);
        }
        return {};
    });
}
bool RenderGraph::has_edge(const Edge& edge) const {
    auto input_state  = get_node_state(edge.input_node);
    auto output_state = get_node_state(edge.output_node);
    if (!input_state || !output_state) {
        return false;
    }
    return input_state->edges.has_input_edge(edge) || output_state->edges.has_output_edge(edge);
}
NodeState* RenderGraph::get_node_state(const NodeLabel& id) {
    auto iter = nodes.find(id);
    if (iter != nodes.end()) {
        return &iter->second;
    }
    return nullptr;
}
const NodeState* RenderGraph::get_node_state(const NodeLabel& id) const {
    auto iter = nodes.find(id);
    if (iter != nodes.end()) {
        return &iter->second;
    }
    return nullptr;
}
NodeState& RenderGraph::node_state(const NodeLabel& id) {
    auto iter = nodes.find(id);
    if (iter != nodes.end()) {
        return iter->second;
    }
    throw std::runtime_error(std::format("Node {} not found.", id.type_index().short_name()));
}
const NodeState& RenderGraph::node_state(const NodeLabel& id) const {
    auto iter = nodes.find(id);
    if (iter != nodes.end()) {
        return iter->second;
    }
    throw std::runtime_error(std::format("Node {} not found.", id.type_index().short_name()));
}

std::expected<void, GraphError> RenderGraph::add_sub_graph(const GraphLabel& id, RenderGraph&& graph) {
    if (sub_graphs.contains(id)) {
        return std::unexpected(SubGraphExists{id});
    }
    sub_graphs.emplace(id, std::move(graph));
    return {};
}
RenderGraph* RenderGraph::get_sub_graph(const GraphLabel& id) {
    auto iter = sub_graphs.find(id);
    if (iter != sub_graphs.end()) {
        return &iter->second;
    }
    return nullptr;
}
const RenderGraph* RenderGraph::get_sub_graph(const GraphLabel& id) const {
    auto iter = sub_graphs.find(id);
    if (iter != sub_graphs.end()) {
        return &iter->second;
    }
    return nullptr;
}
RenderGraph& RenderGraph::sub_graph(const GraphLabel& id) {
    auto iter = sub_graphs.find(id);
    if (iter != sub_graphs.end()) {
        return iter->second;
    }
    throw std::runtime_error(std::format("Sub graph {} not found.", id.type_index().short_name()));
}
const RenderGraph& RenderGraph::sub_graph(const GraphLabel& id) const {
    auto iter = sub_graphs.find(id);
    if (iter != sub_graphs.end()) {
        return iter->second;
    }
    throw std::runtime_error(std::format("Sub graph {} not found.", id.type_index().short_name()));
}