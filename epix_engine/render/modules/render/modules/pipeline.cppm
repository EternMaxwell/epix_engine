module;

#define make_atomic_id(name)                                                                   \
    struct name {                                                                              \
        inline static std::atomic<std::uint64_t> counter{1};                                   \
        static name create() { return name{counter.fetch_add(1, std::memory_order_relaxed)}; } \
        operator std::uint64_t() const { return id; }                                          \
        std::uint64_t id{0};                                                                   \
    };

export module epix.render:pipeline;

import :shader;

namespace render {
make_atomic_id(RenderPipelineId);
make_atomic_id(ComputePipelineId);
export struct RenderPipeline {
    friend struct PipelineServer;  // Only PipelineServer can create pipelines
   public:
    RenderPipelineId id() const { return _id; }
    const wgpu::RenderPipeline& pipeline() const { return _pipeline; }

   private:
    RenderPipeline(wgpu::RenderPipeline pipeline) : _id(RenderPipelineId::create()), _pipeline(std::move(pipeline)) {}

    RenderPipelineId _id;
    wgpu::RenderPipeline _pipeline;
};
export struct ComputePipeline {
    friend struct PipelineServer;  // Only PipelineServer can create pipelines
   public:
    ComputePipelineId id() const { return _id; }
    const wgpu::ComputePipeline& pipeline() const { return _pipeline; }

   private:
    ComputePipeline(wgpu::ComputePipeline pipeline)
        : _id(ComputePipelineId::create()), _pipeline(std::move(pipeline)) {}

    ComputePipelineId _id;
    wgpu::ComputePipeline _pipeline;
};

export struct VertexState {
    assets::Handle<Shader> shader;
    std::optional<std::string> entry_point;
    std::vector<wgpu::VertexBufferLayout> buffers;

    auto&& set_shader(this auto&& self, assets::Handle<Shader> shader) {
        self.shader = std::move(shader);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_entry_point(this auto&& self, std::string entry_point) {
        self.entry_point = std::move(entry_point);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_buffer(this auto&& self, std::uint32_t index, wgpu::VertexBufferLayout layout) {
        self.buffers.resize(std::max(self.buffers.size(), static_cast<std::size_t>(index + 1)));
        self.buffers[index] = std::move(layout);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_buffers(this auto&& self, std::ranges::range auto&& buffers)
        requires std::convertible_to<std::ranges::range_value_t<decltype(buffers)>, wgpu::VertexBufferLayout>
    {
        self.buffers =
            std::forward<decltype(buffers)>(buffers) | std::ranges::to<std::vector<wgpu::VertexBufferLayout>>();
        return std::forward<decltype(self)>(self);
    }
};
export struct FragmentState {
    assets::Handle<Shader> shader;
    std::optional<std::string> entry_point;
    std::vector<wgpu::ColorTargetState> targets;
    auto&& set_shader(this auto&& self, assets::Handle<Shader> shader) {
        self.shader = std::move(shader);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_entry_point(this auto&& self, std::string entry_point) {
        self.entry_point = std::move(entry_point);
        return std::forward<decltype(self)>(self);
    }
    auto&& add_target(this auto&& self, wgpu::ColorTargetState target) {
        self.targets.push_back(std::move(target));
        return std::forward<decltype(self)>(self);
    }
};
export struct RenderPipelineDescriptor {
    std::string label;
    std::vector<wgpu::BindGroupLayout> layouts;
    VertexState vertex;
    wgpu::PrimitiveState primitive;
    std::optional<wgpu::DepthStencilState> depth_stencil;
    wgpu::MultisampleState multisample;
    std::optional<FragmentState> fragment;

    auto&& set_label(this auto&& self, std::string label) {
        self.label = std::move(label);
        return std::forward<decltype(self)>(self);
    }
    auto&& add_layout(this auto&& self, wgpu::BindGroupLayout layout) {
        self.layouts.push_back(std::move(layout));
        return std::forward<decltype(self)>(self);
    }
    auto&& set_layouts(this auto&& self, std::ranges::range auto&& layouts)
        requires std::convertible_to<std::ranges::range_value_t<decltype(layouts)>, wgpu::BindGroupLayout>
    {
        self.layouts = std::forward<decltype(layouts)>(layouts) | std::ranges::to<std::vector<wgpu::BindGroupLayout>>();
        return std::forward<decltype(self)>(self);
    }
    auto&& set_vertex(this auto&& self, VertexState vertex) {
        self.vertex = std::move(vertex);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_primitive(this auto&& self, wgpu::PrimitiveState primitive) {
        self.primitive = std::move(primitive);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_depth_stencil(this auto&& self, wgpu::DepthStencilState depth_stencil) {
        self.depth_stencil = std::move(depth_stencil);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_multisample(this auto&& self, wgpu::MultisampleState multisample) {
        self.multisample = std::move(multisample);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_fragment(this auto&& self, FragmentState fragment) {
        self.fragment = std::move(fragment);
        return std::forward<decltype(self)>(self);
    }
};
export struct ComputePipelineDescriptor {
    std::string label;
    std::vector<wgpu::BindGroupLayout> layouts;
    assets::Handle<Shader> shader;
    std::optional<std::string> entry_point;

    auto&& set_label(this auto&& self, std::string label) {
        self.label = std::move(label);
        return std::forward<decltype(self)>(self);
    }
    auto&& add_layout(this auto&& self, wgpu::BindGroupLayout layout) {
        self.layouts.push_back(std::move(layout));
        return std::forward<decltype(self)>(self);
    }
    auto&& set_layouts(this auto&& self, std::ranges::range auto&& layouts)
        requires std::convertible_to<std::ranges::range_value_t<decltype(layouts)>, wgpu::BindGroupLayout>
    {
        self.layouts = std::forward<decltype(layouts)>(layouts) | std::ranges::to<std::vector<wgpu::BindGroupLayout>>();
        return std::forward<decltype(self)>(self);
    }
    auto&& set_shader(this auto&& self, assets::Handle<Shader> shader) {
        self.shader = std::move(shader);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_entry_point(this auto&& self, std::string entry_point) {
        self.entry_point = std::move(entry_point);
        return std::forward<decltype(self)>(self);
    }
};
}  // namespace render