#include "epix/render/graph.h"

using namespace epix::render::graph;

//==================== SlotLabel ==================//

EPIX_API SlotLabel::SlotLabel(uint32_t l) : label(l) {}
EPIX_API SlotLabel::SlotLabel(const std::string& l) : label(l) {}
EPIX_API SlotLabel::SlotLabel(const char* l) : label(std::string(l)) {}

//==================== SlotValue ==================//
EPIX_API SlotValue::SlotValue(const epix::app::Entity& entity)
    : value(entity) {}
EPIX_API SlotValue::SlotValue(const wgpu::Buffer& buffer) : value(buffer) {}
EPIX_API SlotValue::SlotValue(const wgpu::TextureView& texture_view)
    : value(texture_view) {}
EPIX_API SlotValue::SlotValue(const wgpu::Sampler& sampler) : value(sampler) {}
EPIX_API SlotType SlotValue::type() const {
    return std::visit(
        epix::util::visitor{
            [](const epix::app::Entity&) -> SlotType {
                return SlotType::Entity;
            },
            [](const wgpu::Buffer&) -> SlotType { return SlotType::Buffer; },
            [](const wgpu::TextureView&) -> SlotType {
                return SlotType::TextureView;
            },
            [](const wgpu::Sampler&) -> SlotType { return SlotType::Sampler; },
        },
        value
    );
}
EPIX_API bool SlotValue::is_entity() const {
    return std::holds_alternative<epix::app::Entity>(value);
}
EPIX_API bool SlotValue::is_buffer() const {
    return std::holds_alternative<wgpu::Buffer>(value);
}
EPIX_API bool SlotValue::is_texture() const {
    return std::holds_alternative<wgpu::TextureView>(value);
}
EPIX_API bool SlotValue::is_sampler() const {
    return std::holds_alternative<wgpu::Sampler>(value);
}
EPIX_API epix::app::Entity* SlotValue::entity() {
    if (is_entity()) {
        return &std::get<epix::app::Entity>(value);
    }
    return nullptr;
}
EPIX_API wgpu::Buffer* SlotValue::buffer() {
    if (is_buffer()) {
        return &std::get<wgpu::Buffer>(value);
    }
    return nullptr;
}
EPIX_API wgpu::TextureView* SlotValue::texture() {
    if (is_texture()) {
        return &std::get<wgpu::TextureView>(value);
    }
    return nullptr;
}
EPIX_API wgpu::Sampler* SlotValue::sampler() {
    if (is_sampler()) {
        return &std::get<wgpu::Sampler>(value);
    }
    return nullptr;
}
EPIX_API const epix::app::Entity* SlotValue::entity() const {
    if (is_entity()) {
        return &std::get<epix::app::Entity>(value);
    }
    return nullptr;
}
EPIX_API const wgpu::Buffer* SlotValue::buffer() const {
    if (is_buffer()) {
        return &std::get<wgpu::Buffer>(value);
    }
    return nullptr;
}
EPIX_API const wgpu::TextureView* SlotValue::texture() const {
    if (is_texture()) {
        return &std::get<wgpu::TextureView>(value);
    }
    return nullptr;
}
EPIX_API const wgpu::Sampler* SlotValue::sampler() const {
    if (is_sampler()) {
        return &std::get<wgpu::Sampler>(value);
    }
    return nullptr;
}

//==================== SlotInfos ==================//

EPIX_API SlotInfos::SlotInfos(epix::util::ArrayProxy<SlotInfo> slots)
    : slots(slots.begin(), slots.end()) {}

EPIX_API size_t SlotInfos::size() const { return slots.size(); }
EPIX_API bool SlotInfos::empty() const { return slots.empty(); }
EPIX_API SlotInfo* SlotInfos::get_slot(const SlotLabel& label) {
    auto index = get_slot_index(label);
    if (index) {
        return &slots[*index];
    }
    return nullptr;
}
EPIX_API const SlotInfo* SlotInfos::get_slot(const SlotLabel& label) const {
    auto index = get_slot_index(label);
    if (index) {
        return &slots[*index];
    }
    return nullptr;
}
EPIX_API std::optional<uint32_t> SlotInfos::get_slot_index(
    const SlotLabel& label
) const {
    return std::visit(
        epix::util::visitor{
            [](uint32_t l) -> std::optional<uint32_t> { return l; },
            [this](const std::string& l) -> std::optional<uint32_t> {
                if (auto iter = std::find_if(
                        slots.begin(), slots.end(),
                        [&l](const SlotInfo& info) { return info.name == l; }
                    );
                    iter != slots.end()) {
                    return static_cast<uint32_t>(iter - slots.begin());
                }
                return std::nullopt;
            },
        },
        label.label
    );
}
EPIX_API SlotInfos::iterable SlotInfos::iter() {
    return std::ranges::views::all(slots);
}
EPIX_API SlotInfos::const_iterable SlotInfos::iter() const {
    return std::ranges::views::all(slots);
}

//==================== RenderGraphContext ==================//

EPIX_API RenderGraphContext::RenderGraphContext(
    const RenderGraph& graph,
    const NodeState& node_state,
    const std::vector<SlotValue>& inputs,
    std::vector<std::optional<SlotValue>>& outputs
)
    : m_graph(graph),
      m_node_state(node_state),
      m_inputs(inputs),
      m_outputs(outputs) {}
EPIX_API const std::vector<SlotValue>& RenderGraphContext::inputs() const {
    return m_inputs;
}
EPIX_API const SlotInfos& RenderGraphContext::input_info() const {
    return m_node_state.inputs;
}
EPIX_API const SlotInfos& RenderGraphContext::output_info() const {
    return m_node_state.outputs;
}
EPIX_API const SlotValue* RenderGraphContext::get_input(const SlotLabel& label
) const {
    auto index = m_node_state.inputs.get_slot_index(label);
    if (index) {
        return &m_inputs[*index];
    }
    return nullptr;
}
EPIX_API const epix::app::Entity* RenderGraphContext::get_input_entity(
    const SlotLabel& label
) const {
    auto value = get_input(label);
    if (value && value->is_entity()) {
        return value->entity();
    }
    return nullptr;
}
EPIX_API const wgpu::Buffer* RenderGraphContext::get_input_buffer(
    const SlotLabel& label
) const {
    auto value = get_input(label);
    if (value && value->is_buffer()) {
        return value->buffer();
    }
    return nullptr;
}
EPIX_API const wgpu::TextureView* RenderGraphContext::get_input_texture(
    const SlotLabel& label
) const {
    auto value = get_input(label);
    if (value && value->is_texture()) {
        return value->texture();
    }
    return nullptr;
}
EPIX_API const wgpu::Sampler* RenderGraphContext::get_input_sampler(
    const SlotLabel& label
) const {
    auto value = get_input(label);
    if (value && value->is_sampler()) {
        return value->sampler();
    }
    return nullptr;
}

EPIX_API bool RenderGraphContext::set_output(
    const SlotLabel& label, const SlotValue& value
) {
    auto index = m_node_state.outputs.get_slot_index(label);
    auto info  = m_node_state.outputs.get_slot(label);
    if (index && info) {
        if (info->type == value.type()) {
            m_outputs[*index].emplace(value);
            return true;
        } else {
            return false;
        }
    }
    return false;
}

EPIX_API epix::app::Entity RenderGraphContext::view_entity() const {
    return m_view_entity.value();
}
EPIX_API std::optional<epix::app::Entity> RenderGraphContext::get_view_entity(
) const {
    return m_view_entity;
}
EPIX_API void RenderGraphContext::set_view_entity(epix::app::Entity entity) {
    m_view_entity = entity;
}

EPIX_API bool RenderGraphContext::run_sub_graph(
    const GraphLabel& graph,
    epix::util::ArrayProxy<SlotValue> inputs,
    std::optional<epix::app::Entity> view_entity
) {
    auto sub_graph = m_graph.get_sub_graph(graph);
    if (sub_graph) {
        // check the inputs matches the sub graph inputs
        if (auto input_node = sub_graph->get_input_node()) {
            size_t required_inputs_size = input_node->inputs.size();
            size_t inputs_size          = inputs.size();
            if (required_inputs_size != inputs_size) {
                spdlog::warn(
                    "Sub graph {} has {} inputs, but {} inputs were provided.",
                    graph.name(), required_inputs_size, inputs_size
                );
                return false;
            }
            for (size_t i = 0; i < required_inputs_size; i++) {
                auto input = input_node->inputs.get_slot(i);
                if (input) {
                    auto value = *(inputs.begin() + i);
                    if (input->type != value.type()) {
                        spdlog::warn(
                            "Sub graph {} input {} type mismatch. Expected {}, "
                            "got {}.",
                            graph.name(), input->name, type_name(input->type),
                            type_name(value.type())
                        );
                        return false;
                    }
                }
            }
        } else {
            if (!inputs.empty()) {
                spdlog::warn(
                    "Sub graph {} has no input node, but {} inputs were "
                    "provided.",
                    graph.name(), inputs.size()
                );
                return false;
            }
        }
        m_sub_graphs.emplace_back(
            graph, std::vector<SlotValue>(inputs.begin(), inputs.end()),
            view_entity
        );
        return true;
    }
    return false;
}
EPIX_API std::vector<RunSubGraph> RenderGraphContext::finish() {
    return std::move(m_sub_graphs);
}

//==================== Edge ==================//

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
    return std::find_if(
               m_input_edges.begin(), m_input_edges.end(),
               [&edge](const Edge& e) { return e == edge; }
           ) != m_input_edges.end();
}
EPIX_API bool Edges::has_output_edge(const Edge& edge) const {
    return std::find_if(
               m_output_edges.begin(), m_output_edges.end(),
               [&edge](const Edge& e) { return e == edge; }
           ) != m_output_edges.end();
}
EPIX_API void Edges::remove_input_edge(const Edge& edge) {
    auto index = std::find_if(
        m_input_edges.begin(), m_input_edges.end(),
        [&edge](const Edge& e) { return e == edge; }
    );
    if (index != m_input_edges.end()) {
        std::iter_swap(index, m_input_edges.end() - 1);
        m_input_edges.pop_back();
    }
}
EPIX_API void Edges::remove_output_edge(const Edge& edge) {
    auto index = std::find_if(
        m_output_edges.begin(), m_output_edges.end(),
        [&edge](const Edge& e) { return e == edge; }
    );
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
        [&index](const Edge& e) { return e.input_index == index; }
    );
    if (iter != m_input_edges.end()) {
        return &(*iter);
    }
    return nullptr;
}
EPIX_API const Edge* Edges::get_output_slot_edge(size_t index) const {
    auto iter = std::find_if(
        m_output_edges.begin(), m_output_edges.end(),
        [&index](const Edge& e) { return e.output_index == index; }
    );
    if (iter != m_output_edges.end()) {
        return &(*iter);
    }
    return nullptr;
}

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