# Render Graph

The render graph is a **directed acyclic graph (DAG)** of `Node` objects.
Nodes are connected by execution-order edges (ordering) and slot edges (data
flow).  Each frame, `RenderGraph::update()` is called to let nodes refresh
internal state, then `RenderGraphRunner` traverses the graph and invokes each
node's `run()` method.

---

## Labels

```cpp
namespace epix::render::graph {
struct NodeLabel  : public epix::ecs::Label { /* ... */ };
struct GraphLabel : public epix::ecs::Label { /* ... */ };
}
```

Both label types derive from `epix::ecs::Label` and can be constructed from
any value that constructs a `Label` (e.g. a `constexpr` struct, string, enum).

```cpp
// Declare custom labels
constexpr struct TriangleNode {} triangle_node;
constexpr struct MainGraph   {} main_graph;

graph.add_node(triangle_node, MyTriangleNode{});
```

---

## Slots

```cpp
enum class SlotType { Buffer, Texture, Sampler, Entity };

struct SlotInfo {
    std::string name;
    SlotType    type;
};

struct SlotValue { /* type-erased; holds Entity / Buffer / TextureView / Sampler */ };
```

`SlotValue` copies GPU handles via `.clone()` on copy.  Accessor helpers:

| Method | Returns |
|--------|---------|
| `type()` | `SlotType` |
| `entity()` | `optional<Entity>` |
| `buffer()` | `optional<wgpu::Buffer>` |
| `texture()` | `optional<wgpu::TextureView>` |
| `sampler()` | `optional<wgpu::Sampler>` |

`SlotLabel` addresses a slot either by zero-based `std::uint32_t` index or by
string name. Prefer names in extension code because they remain stable when a
node's slot list is reordered.

---

## Node

```cpp
namespace epix::render::graph {
struct Node {
    virtual std::vector<SlotInfo> inputs()  { return {}; }
    virtual std::vector<SlotInfo> outputs() { return {}; }
    virtual void update(const World&)       {}
    virtual void run(GraphContext&, RenderContext&, const World&) {}
};
}
```

Override `inputs()` / `outputs()` to declare typed data slots.  Override
`update()` for per-frame state (called before the render pass begins) and
`run()` to issue GPU commands.

`EmptyNode` is a no-op node useful as an ordering anchor. `NodeState` stores a
node's label, input/output metadata, edges, and owning polymorphic node. Use
`NodeState::node<T>()` for a checked downcast when node-specific setup is needed
after insertion.

### GraphInputNode

The special `GraphInput` sentinel and `GraphInputNode` type are used to
pass slot data into the graph from an external caller.

```cpp
graph.set_input({{ "view_entity", SlotType::Entity }});
// Now use GraphInput as the output node when adding slot edges:
graph.add_slot_edge(GraphInput, "view_entity", my_node, "view");
```

---

## RenderGraph

```cpp
struct RenderGraph {
    // Node management
    template <std::derived_from<Node> T, typename... Args>
    void add_node(const NodeLabel& id, Args&&... args);
    std::expected<void, GraphError> remove_node(const NodeLabel& id);

    // Execution-order edges
    template <typename... Args>
    void add_node_edges(Args&&... labels);   // chains A→B→C
    void add_node_edge(const NodeLabel& out, const NodeLabel& in);
    std::expected<void, GraphError> try_add_node_edge(...);

    // Data-flow (slot) edges
    void add_slot_edge(const NodeLabel& out_node, const SlotLabel& out_slot,
                       const NodeLabel& in_node,  const SlotLabel& in_slot);
    std::expected<void, GraphError> try_add_slot_edge(...);
    std::expected<void, GraphError> remove_slot_edge(...);

    // Sub-graphs
    std::expected<void, GraphError> add_sub_graph(const GraphLabel& id, RenderGraph&& g);
    RenderGraph& sub_graph(const GraphLabel& id);

    // Inspection
    bool set_input(std::span<const SlotInfo> inputs);
    std::optional<std::reference_wrapper<const NodeState>> get_input_node() const;
    const NodeState& input_node() const;
    std::optional<std::reference_wrapper<NodeState>> get_node_state(const NodeLabel&);
    NodeState& node_state(const NodeLabel&); // throws when absent
    std::optional<std::reference_wrapper<RenderGraph>> get_sub_graph(const GraphLabel&);
    bool has_edge(const Edge& edge) const;
    auto iter_nodes() const;  // range over NodeState
    void update(World& world);
};
```

`RenderGraph` is move-only (no copy).  Sub-graphs are invoked by nodes at
runtime via `GraphContext::run_sub_graph()`.

### Usage

```cpp
auto& render_app = app.sub_app_mut(render::Render);
auto& graph = render_app.world_mut().resource_mut<render::RenderGraph>();

// Add nodes and order them
graph.add_node(my_node_a, NodeA{});
graph.add_node(my_node_b, NodeB{});
graph.add_node_edges(my_node_a, my_node_b);  // A runs before B

// Add a sub-graph that a camera can drive
graph.add_sub_graph(my_sub_graph, std::move(sub_graph));
```

`set_input()` creates the special `GraphInput` node and returns whether it was
inserted. Connect its named outputs to ordinary node inputs with slot edges.
`add_node_edges(a, b, c)` is a convenience chain; it currently discards edge
errors, so use `try_add_node_edge()` when configuration errors must be reported.

| Operation | Checked form | Throwing/convenience form |
| --- | --- | --- |
| add order edge | `try_add_node_edge` | `add_node_edge` |
| add slot edge | `try_add_slot_edge` | `add_slot_edge` |
| remove order edge | `remove_node_edge` | none |
| remove slot edge | `remove_slot_edge` | none |
| remove node | `remove_node` | none |
| get node/sub-graph | `get_*` optional | `node_state` / `sub_graph` |

---

## GraphContext

```cpp
struct GraphContext {
    // Input slot access
    const std::vector<SlotValue>& inputs() const noexcept;
    const SlotInfos& input_info() const noexcept;
    const SlotInfos& output_info() const noexcept;
    const SlotValue* get_input(const SlotLabel& label) const;
    std::optional<Entity>           get_input_entity (const SlotLabel&) const;
    std::optional<wgpu::Buffer>     get_input_buffer (const SlotLabel&) const;
    std::optional<wgpu::TextureView> get_input_texture(const SlotLabel&) const;
    std::optional<wgpu::Sampler>    get_input_sampler(const SlotLabel&) const;

    // Output slot assignment
    bool set_output(const SlotLabel& label, SlotValue value);

    // View entity
    Entity view_entity() const;        // throws if not set
    std::optional<Entity> get_view_entity() const noexcept;
    void set_view_entity(Entity entity) noexcept;

    // Sub-graph scheduling
    bool run_sub_graph(const GraphLabel& label,
                       std::span<const SlotValue> inputs,
                       std::optional<Entity> view_entity = {});
};
```

---

## RenderContext

```cpp
struct RenderContext {
    const wgpu::Device& device() const noexcept;
    wgpu::CommandEncoder& command_encoder();  // lazily created
    wgpu::RenderPassEncoder begin_render_pass(const wgpu::RenderPassDescriptor&);
    void add_command_buffer(wgpu::CommandBuffer buffer);
    void flush_encoder();
    std::vector<wgpu::CommandBuffer> finish();  // flushes + returns all buffers
};
```

`RenderContext` is passed to `Node::run()` and provides the command encoder.
Multiple nodes accumulate commands into the same context; the runner submits
them to the GPU queue at the end of the frame.

`run_sub_graph()` queues the sub-graph invocation until the current node
finishes. Its input count and types must match the sub-graph's input node. Pass
the current camera/view entity when downstream nodes use
`GraphContext::view_entity()`.

---

## Error Types

All graph-mutation methods return `std::expected<void, GraphError>` on failure.
The top-level error is:

```
GraphError = variant<NodeNotPresent, EdgeError, SubGraphExists>
EdgeError  = variant<EdgeNodesNotPresent, SlotNotPresent, InputSlotOccupied, SlotTypeMismatch>
```

| Error | Cause |
|-------|-------|
| `NodeNotPresent` | `remove_node` or edge pointing to a missing node |
| `EdgeNodesNotPresent` | One or both nodes of a new edge don't exist |
| `SlotNotPresent` | Referenced slot name not declared by the node |
| `InputSlotOccupied` | An input slot already has an edge from a different node |
| `SlotTypeMismatch` | Output slot type ≠ input slot type |
| `SubGraphExists` | `add_sub_graph` called with a label already in use |

The non-try variants (`add_node_edge`, `add_slot_edge`) throw on error.
`GraphError::to_string()` produces a diagnostic for logging.
