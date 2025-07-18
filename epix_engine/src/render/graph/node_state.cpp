#include "epix/render/graph.h"

using namespace epix::render::graph;

//==================== NodeState ==================//

EPIX_API NodeState::NodeState(NodeLabel id, Node* node)
    : label(id),
      pnode(node),
      edges(id),
      inputs(node->inputs()),
      outputs(node->outputs()) {}
EPIX_API bool NodeState::validate_input_slots() {
    for (size_t i = 0; i < inputs.size(); i++) {
        auto edge = edges.get_input_slot_edge(i);
        if (!edge) {
            return false;
        }
    }
    return true;
}
EPIX_API bool NodeState::validate_output_slots() {
    for (size_t i = 0; i < outputs.size(); i++) {
        auto edge = edges.get_output_slot_edge(i);
        if (!edge) {
            return false;
        }
    }
    return true;
}