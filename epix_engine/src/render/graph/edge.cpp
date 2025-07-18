#include "epix/render/graph.h"

using namespace epix::render::graph;

//==================== Edge ==================//

EPIX_API Edge Edge::node_edge(const NodeLabel& output_node,
                              const NodeLabel& input_node) {
    return Edge{input_node, output_node};
}
EPIX_API Edge Edge::slot_edge(const NodeLabel& output_node,
                              uint32_t output_index,
                              const NodeLabel& input_node,
                              uint32_t input_index) {
    return Edge{input_node, output_node, input_index, output_index};
}

EPIX_API bool Edge::operator==(const Edge& other) const {
    return input_node == other.input_node && output_node == other.output_node &&
           input_index == other.input_index &&
           output_index == other.output_index;
}
EPIX_API bool Edge::operator!=(const Edge& other) const {
    return !(*this == other);
}
EPIX_API bool Edge::is_slot_edge() const {
    return input_index != -1 && output_index != -1;
}

//==================== Edges ==================//

EPIX_API Edges::Edges(NodeLabel label) : m_label(label) {}

EPIX_API NodeLabel Edges::label() const { return m_label; }
EPIX_API const std::vector<Edge>& Edges::input_edges() const {
    return m_input_edges;
}
EPIX_API const std::vector<Edge>& Edges::output_edges() const {
    return m_output_edges;
}
EPIX_API bool Edges::has_input_edge(const Edge& edge) const {
    return std::find_if(m_input_edges.begin(), m_input_edges.end(),
                        [&edge](const Edge& e) { return e == edge; }) !=
           m_input_edges.end();
}
EPIX_API bool Edges::has_output_edge(const Edge& edge) const {
    return std::find_if(m_output_edges.begin(), m_output_edges.end(),
                        [&edge](const Edge& e) { return e == edge; }) !=
           m_output_edges.end();
}
EPIX_API void Edges::remove_input_edge(const Edge& edge) {
    auto index = std::find_if(m_input_edges.begin(), m_input_edges.end(),
                              [&edge](const Edge& e) { return e == edge; });
    if (index != m_input_edges.end()) {
        std::iter_swap(index, m_input_edges.end() - 1);
        m_input_edges.pop_back();
    }
}
EPIX_API void Edges::remove_output_edge(const Edge& edge) {
    auto index = std::find_if(m_output_edges.begin(), m_output_edges.end(),
                              [&edge](const Edge& e) { return e == edge; });
    if (index != m_output_edges.end()) {
        std::iter_swap(index, m_input_edges.end() - 1);
        m_input_edges.pop_back();
    }
}
EPIX_API void Edges::add_input_edge(const Edge& edge) {
    if (!has_input_edge(edge)) {
        m_input_edges.push_back(edge);
    }
}
EPIX_API void Edges::add_output_edge(const Edge& edge) {
    if (!has_output_edge(edge)) {
        m_output_edges.push_back(edge);
    }
}
EPIX_API const Edge* Edges::get_input_slot_edge(size_t index) const {
    auto iter = std::find_if(
        m_input_edges.begin(), m_input_edges.end(),
        [&index](const Edge& e) { return e.input_index == index; });
    if (iter != m_input_edges.end()) {
        return &(*iter);
    }
    return nullptr;
}
EPIX_API const Edge* Edges::get_output_slot_edge(size_t index) const {
    auto iter = std::find_if(
        m_output_edges.begin(), m_output_edges.end(),
        [&index](const Edge& e) { return e.output_index == index; });
    if (iter != m_output_edges.end()) {
        return &(*iter);
    }
    return nullptr;
}