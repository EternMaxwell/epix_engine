#include "epix/render/graph.hpp"
#include "epix/render/graph/node.hpp"

namespace epix::render::graph {

bool Edges::has_input_edge(const Edge& edge) const {
    return std::find(m_input_edges.begin(), m_input_edges.end(), edge) != m_input_edges.end();
}

bool Edges::has_output_edge(const Edge& edge) const {
    return std::find(m_output_edges.begin(), m_output_edges.end(), edge) != m_output_edges.end();
}

void Edges::remove_input_edge(const Edge& edge) {
    m_input_edges.erase(std::remove(m_input_edges.begin(), m_input_edges.end(), edge), m_input_edges.end());
}

void Edges::remove_output_edge(const Edge& edge) {
    auto index =
        std::find_if(m_output_edges.begin(), m_output_edges.end(), [&edge](const Edge& e) { return e == edge; });
    if (index != m_output_edges.end()) {
        std::iter_swap(index, m_output_edges.end() - 1);
        m_output_edges.pop_back();
    }
}

void Edges::add_input_edge(const Edge& edge) {
    if (!has_input_edge(edge)) {
        m_input_edges.push_back(edge);
    }
}

void Edges::add_output_edge(const Edge& edge) {
    if (!has_output_edge(edge)) {
        m_output_edges.push_back(edge);
    }
}

const Edge* Edges::get_input_slot_edge(size_t index) const {
    auto iter = std::find_if(m_input_edges.begin(), m_input_edges.end(),
                             [index](const Edge& e) { return e.is_slot_edge() && e.input_index == index; });
    return iter != m_input_edges.end() ? &(*iter) : nullptr;
}

const Edge* Edges::get_output_slot_edge(size_t index) const {
    auto iter = std::find_if(m_output_edges.begin(), m_output_edges.end(),
                             [index](const Edge& e) { return e.is_slot_edge() && e.output_index == index; });
    return iter != m_output_edges.end() ? &(*iter) : nullptr;
}

void GraphInputNode::run(GraphContext& graph, RenderContext&, World&) {
    for (auto&& [index, value] : std::views::enumerate(graph.inputs())) {
        graph.set_output((uint32_t)index, value);
    }
}

}  // namespace epix::render::graph