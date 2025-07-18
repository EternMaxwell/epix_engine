#include <epix/render.h>
#include <epix/render/graph.h>

using namespace epix;
using namespace epix::render;
using namespace epix::render::graph;

struct DriverNode : Node {
    void run(GraphContext& graph_ctx,
             RenderContext& render_ctx,
             app::World& world) override {
        spdlog::info("Running driver node");
        graph_ctx.run_sub_graph(0, {});
    }
};

struct SubGraphNode : Node {
    void run(GraphContext& graph_ctx,
             RenderContext& render_ctx,
             app::World& world) override {
        spdlog::info("Running sub graph test node");
    }
};

struct OutputNode : Node {
    void run(GraphContext& graph_ctx,
             RenderContext& render_ctx,
             app::World& world) override {
        spdlog::info("Running output node");
        auto entity = world.spawn(0);
        graph_ctx.set_output("output_slot", entity);
        spdlog::info("Output node set output to entity with id: {}",
                     entity.index());
    }
    std::vector<SlotInfo> outputs() override {
        return {{"output_slot", SlotType::Entity}};
    }
};

struct InputNode : Node {
    void run(GraphContext& graph_ctx,
             RenderContext& render_ctx,
             app::World& world) override {
        spdlog::info("Running input node");
        auto pvalue = graph_ctx.get_input("input_slot");
        if (pvalue) {
            spdlog::info("Input node received entity with id: {}",
                         pvalue->entity().value().index());
        } else {
            spdlog::warn("Input node did not receive an entity.");
        }
    }
    std::vector<SlotInfo> inputs() override {
        return {{"input_slot", SlotType::Entity}};
    }
};

int main() {
    RenderGraph graph;
    app::World world(0);
    graph.add_node(0, DriverNode{});

    try {
        RenderGraph sub_graph_0;
        sub_graph_0.add_node(1, SubGraphNode{});
        sub_graph_0.add_node(2, OutputNode{});
        sub_graph_0.add_node(3, InputNode{});
        sub_graph_0.try_add_slot_edge(2, "output_slot", 3, "input_slot");
        sub_graph_0.add_node_edges(2, 1, 3);

        graph.add_sub_graph(0, std::move(sub_graph_0));
    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        return -1;
    }

    RenderGraphRunner::run(graph, nvrhi::DeviceHandle(), world, {});
}