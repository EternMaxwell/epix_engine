module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#endif
#define make_atomic_id(name)                                                                   \
    struct name {                                                                              \
        inline static std::atomic<std::uint64_t> counter{1};                                   \
        static name create() { return name{counter.fetch_add(1, std::memory_order_relaxed)}; } \
        operator std::uint64_t() const { return id; }                                          \
        std::uint64_t id{0};                                                                   \
    };

export module epix.render:pipeline;

import epix.shader;
import epix.assets;
import webgpu;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::render {
make_atomic_id(RenderPipelineId);
make_atomic_id(ComputePipelineId);
/** @brief A created render pipeline with a unique auto-incremented ID. */
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
/** @brief A created compute pipeline with a unique auto-incremented ID. */
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

/** @brief Vertex stage configuration: shader, entry point, and vertex
 * buffer layouts. */
export struct VertexState {
    /** @brief Handle to the vertex shader. */
    assets::Handle<shader::Shader> shader;
    /** @brief Optional entry-point function name (defaults to "vs_main"). */
    std::optional<std::string> entry_point;
    /** @brief Vertex buffer layouts describing attribute bindings. */
    std::vector<wgpu::VertexBufferLayout> buffers;

    /** @brief Set the vertex shader handle. */
    auto&& set_shader(this auto&& self, assets::Handle<shader::Shader> shader) {
        self.shader = std::move(shader);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Set the vertex shader entry-point name. */
    auto&& set_entry_point(this auto&& self, std::string entry_point) {
        self.entry_point = std::move(entry_point);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Set a vertex buffer layout at a specific binding index. */
    auto&& set_buffer(this auto&& self, std::uint32_t index, wgpu::VertexBufferLayout layout) {
        self.buffers.resize(std::max(self.buffers.size(), static_cast<std::size_t>(index + 1)));
        self.buffers[index] = std::move(layout);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Set all vertex buffer layouts from a range. */
    auto&& set_buffers(this auto&& self, std::ranges::range auto&& buffers)
        requires std::convertible_to<std::ranges::range_value_t<decltype(buffers)>, wgpu::VertexBufferLayout>
    {
        self.buffers = std::ranges::to<std::vector<wgpu::VertexBufferLayout>>(std::forward<decltype(buffers)>(buffers));
        return std::forward<decltype(self)>(self);
    }
};
/** @brief Fragment stage configuration: shader, entry point, and color
 * targets. */
export struct FragmentState {
    assets::Handle<shader::Shader> shader;
    std::optional<std::string> entry_point;
    std::vector<wgpu::ColorTargetState> targets;
    /** @brief Set the fragment shader handle. */
    auto&& set_shader(this auto&& self, assets::Handle<shader::Shader> shader) {
        self.shader = std::move(shader);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Set the fragment shader entry-point name. */
    auto&& set_entry_point(this auto&& self, std::string entry_point) {
        self.entry_point = std::move(entry_point);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Add a color target state to the fragment stage. */
    auto&& add_target(this auto&& self, wgpu::ColorTargetState target) {
        self.targets.push_back(std::move(target));
        return std::forward<decltype(self)>(self);
    }
};
/** @brief Full descriptor for creating a render pipeline, including
 * layout, vertex/fragment stages, primitive state, depth-stencil, and
 * multisampling. Uses a builder pattern with `set_*` methods. */
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
        self.layouts = std::ranges::to<std::vector<wgpu::BindGroupLayout>>(std::forward<decltype(layouts)>(layouts));
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
/** @brief Full descriptor for creating a compute pipeline, including
 * layout, shader, and entry point. Uses a builder pattern. */
export struct ComputePipelineDescriptor {
    std::string label;
    std::vector<wgpu::BindGroupLayout> layouts;
    assets::Handle<shader::Shader> shader;
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
        self.layouts = std::ranges::to<std::vector<wgpu::BindGroupLayout>>(std::forward<decltype(layouts)>(layouts));
        return std::forward<decltype(self)>(self);
    }
    auto&& set_shader(this auto&& self, assets::Handle<shader::Shader> shader) {
        self.shader = std::move(shader);
        return std::forward<decltype(self)>(self);
    }
    auto&& set_entry_point(this auto&& self, std::string entry_point) {
        self.entry_point = std::move(entry_point);
        return std::forward<decltype(self)>(self);
    }
};
}  // namespace epix::render