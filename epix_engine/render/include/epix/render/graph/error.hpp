#pragma once

#include "fwd.hpp"
#include "slot.hpp"

namespace epix::render::graph {
struct NodeNotPresent {
    NodeLabel label;
};
struct EdgeNodesNotPresent {
    NodeLabel output_node;
    NodeLabel input_node;
    bool missing_output;
    bool missing_input;
};
struct SlotNotPresent {
    NodeLabel output_node;
    SlotLabel output_slot;
    NodeLabel input_node;
    SlotLabel input_slot;
    bool missing_in_output;
    bool missing_in_input;
};
struct InputSlotOccupied {
    NodeLabel input_node;
    uint32_t input_index;
    NodeLabel current_output_node;
    uint32_t current_output_index;
    NodeLabel required_output_node;
    uint32_t required_output_index;
};
struct SlotTypeMismatch {
    NodeLabel output_node;
    uint32_t output_index;
    NodeLabel input_node;
    uint32_t input_index;
    SlotType output_type;
    SlotType input_type;
};
struct EdgeError : std::variant<EdgeNodesNotPresent, SlotNotPresent, InputSlotOccupied, SlotTypeMismatch> {
    using std::variant<EdgeNodesNotPresent, SlotNotPresent, InputSlotOccupied, SlotTypeMismatch>::variant;
};
struct SubGraphExists {
    GraphLabel id;
};

struct GraphError : std::variant<NodeNotPresent, EdgeError, SubGraphExists> {
    using std::variant<NodeNotPresent, EdgeError, SubGraphExists>::variant;
    std::string to_string() const;
};
}  // namespace epix::render::graph