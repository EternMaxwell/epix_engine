module;
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <string>
#include <variant>
#endif

export module epix.render:graph.error;

import :graph.decl;
import :graph.slot;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export namespace epix::render::graph {
/** @brief Error indicating a node was not found in the graph. */
struct NodeNotPresent {
    /** @brief Label of the missing node. */
    NodeLabel label;
};
/** @brief Error indicating one or both nodes of an edge are missing. */
struct EdgeNodesNotPresent {
    /** @brief Label of the output node. */
    NodeLabel output_node;
    /** @brief Label of the input node. */
    NodeLabel input_node;
    /** @brief True if the output node is missing. */
    bool missing_output;
    /** @brief True if the input node is missing. */
    bool missing_input;
};
/** @brief Error indicating a referenced slot was not found on one or
 * both nodes. */
struct SlotNotPresent {
    /** @brief Output node label. */
    NodeLabel output_node;
    /** @brief Output slot label. */
    SlotLabel output_slot;
    /** @brief Input node label. */
    NodeLabel input_node;
    /** @brief Input slot label. */
    SlotLabel input_slot;
    /** @brief True if the slot is missing on the output node. */
    bool missing_in_output;
    /** @brief True if the slot is missing on the input node. */
    bool missing_in_input;
};
/** @brief Error indicating an input slot is already connected to another
 * output. */
struct InputSlotOccupied {
    /** @brief Input node label. */
    NodeLabel input_node;
    /** @brief Index of the occupied input slot. */
    std::uint32_t input_index;
    /** @brief Node currently connected to the input. */
    NodeLabel current_output_node;
    /** @brief Slot index on the currently connected node. */
    std::uint32_t current_output_index;
    /** @brief Node that was requested to connect. */
    NodeLabel required_output_node;
    /** @brief Slot index on the requested node. */
    std::uint32_t required_output_index;
};
/** @brief Error indicating a type mismatch between connected output and
 * input slots. */
struct SlotTypeMismatch {
    /** @brief Output node label. */
    NodeLabel output_node;
    /** @brief Output slot index. */
    std::uint32_t output_index;
    /** @brief Input node label. */
    NodeLabel input_node;
    /** @brief Input slot index. */
    std::uint32_t input_index;
    /** @brief Type of the output slot. */
    SlotType output_type;
    /** @brief Type of the input slot. */
    SlotType input_type;
};
/** @brief Variant of edge-related errors. */
struct EdgeError : std::variant<EdgeNodesNotPresent, SlotNotPresent, InputSlotOccupied, SlotTypeMismatch> {
    using std::variant<EdgeNodesNotPresent, SlotNotPresent, InputSlotOccupied, SlotTypeMismatch>::variant;
};
/** @brief Error indicating a sub-graph with the given label already
 * exists. */
struct SubGraphExists {
    GraphLabel id;
};

/** @brief Top-level render graph error, wrapping node, edge, or sub-graph errors. */
struct GraphError : std::variant<NodeNotPresent, EdgeError, SubGraphExists> {
    using std::variant<NodeNotPresent, EdgeError, SubGraphExists>::variant;
    std::string to_string() const;
};
}  // namespace render::graph