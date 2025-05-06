#include "epix/render/graph.h"

using namespace epix::render::graph;

enum class TestLabel {
    A,
    B,
    C,
    D,
};

struct TestNode : public Node {
    std::vector<SlotInfo> m_inputs;
    std::vector<SlotInfo> m_outputs;

    TestNode(size_t input_count, size_t output_count)
        : m_inputs(
              std::views::enumerate(
                  std::vector<SlotType>(input_count, SlotType::Entity)
              ) |
              std::views::transform([](auto&& pair) {
                  auto&& [index, type] = pair;
                  return SlotInfo{std::format("input_{}", index), type};
              }) |
              std::ranges::to<std::vector>()
          ),
          m_outputs(
              std::views::enumerate(
                  std::vector<SlotType>(output_count, SlotType::Entity)
              ) |
              std::views::transform([](auto&& pair) {
                  auto&& [index, type] = pair;
                  return SlotInfo{std::format("output_{}", index), type};
              }) |
              std::ranges::to<std::vector>()
          ) {}

    std::vector<SlotInfo> inputs() override { return m_inputs; }
    std::vector<SlotInfo> outputs() override { return m_outputs; }

    void run(
        RenderGraphContext& graph, RenderContext& ctx, epix::app::World& world
    ) override {}
};

#include <unordered_set>

void test_graph_edges() {
    RenderGraph graph;
    graph.add_node(TestLabel::A, TestNode(0, 1));
    graph.add_node(TestLabel::B, TestNode(0, 1));
    graph.add_node(TestLabel::C, TestNode(1, 1));
    graph.add_node(TestLabel::D, TestNode(1, 0));

    graph.add_slot_edge(TestLabel::A, "output_0", TestLabel::C, "input_0");
    graph.add_node_edge(TestLabel::B, TestLabel::C);
    graph.add_slot_edge(TestLabel::C, 0u, TestLabel::D, 0u);

    assert(
        (graph.get_node_state(TestLabel::A)->edges.input_edges().size() == 0) &&
        "Node A should have no input edges."
    );
    assert(
        ((graph.get_node_state(TestLabel::A)->edges.output_edges() |
          std::views::transform([](const Edge& e) { return e.input_node; }) |
          std::ranges::to<std::unordered_set>()) ==
         std::unordered_set<NodeLabel>{TestLabel::C}) &&
        "Node A should outputs to C."
    );
    assert(
        (graph.get_node_state(TestLabel::B)->edges.input_edges().size() == 0) &&
        "Node B should have no input edges."
    );
    assert(
        ((graph.get_node_state(TestLabel::B)->edges.output_edges() |
          std::views::transform([](const Edge& e) { return e.input_node; }) |
          std::ranges::to<std::unordered_set>()) ==
         std::unordered_set<NodeLabel>{TestLabel::C}) &&
        "Node B should outputs to C."
    );
    assert(
        ((graph.get_node_state(TestLabel::C)->edges.input_edges() |
          std::views::transform([](const Edge& e) { return e.output_node; }) |
          std::ranges::to<std::unordered_set>()) ==
         std::unordered_set<NodeLabel>{TestLabel::A, TestLabel::B}) &&
        "Node C should have inputs from A and B."
    );
    assert(
        ((graph.get_node_state(TestLabel::C)->edges.output_edges() |
          std::views::transform([](const Edge& e) { return e.input_node; }) |
          std::ranges::to<std::unordered_set>()) ==
         std::unordered_set<NodeLabel>{TestLabel::D}) &&
        "Node C should outputs to D."
    );
    assert(
        ((graph.get_node_state(TestLabel::D)->edges.input_edges() |
          std::views::transform([](const Edge& e) { return e.output_node; }) |
          std::ranges::to<std::unordered_set>()) ==
         std::unordered_set<NodeLabel>{TestLabel::C}) &&
        "Node D should have inputs from C."
    );
    assert(
        (graph.get_node_state(TestLabel::D)->edges.output_edges().size() == 0
        ) &&
        "Node D should have no output edges."
    );
}

int main() { test_graph_edges(); }