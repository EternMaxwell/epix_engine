#pragma once

#include <epix/transform.hpp>

#include "nvrhi/nvrhi.h"
#include "render_phase.hpp"
#include "vulkan.hpp"
#include "window.hpp"

namespace epix::render::camera {
/// Forward declare
struct ExtractedCamera;
}  // namespace epix::render::camera

namespace epix::render::view {
struct ExtractedView {
    glm::mat4 projection;
    transform::GlobalTransform transform;
    glm::uvec2 viewport_size;
    glm::uvec2 viewport_origin;
};
struct VisibleEntities {
    std::vector<Entity> entities;
};
struct ViewTarget {
    nvrhi::TextureHandle texture;
};
struct ViewDepth {
    nvrhi::TextureHandle texture;
};
struct UVec2Hash {
    std::size_t operator()(const glm::uvec2& v) const noexcept {
        return std::hash<size_t>()(static_cast<size_t>(v.x) << 32 | static_cast<size_t>(v.y));
    }
};
struct ViewDepthCache {
    std::unordered_map<glm::uvec2, nvrhi::TextureHandle, UVec2Hash> cache;
};

struct ViewPlugin {
    void build(App& app);
};

void prepare_view_target(Query<Item<Entity, const camera::ExtractedCamera&, const ExtractedView&>> views,
                         Commands cmd,
                         Res<window::ExtractedWindows> extracted_windows);
void create_view_depth(Query<Item<Entity, const ExtractedView&>> views,
                       Res<nvrhi::DeviceHandle> device,
                       ResMut<ViewDepthCache> depth_cache,
                       Commands cmd);

struct ViewUniform {
    glm::mat4 projection;
    glm::mat4 view;
};
struct UniformBuffer {
    nvrhi::BufferHandle buffer;
};
struct ViewUniformBindingLayout {
    const nvrhi::BindingLayoutHandle layout;
    ViewUniformBindingLayout(World& world)
        : layout(world.resource<nvrhi::DeviceHandle>()->createBindingLayout(
              nvrhi::BindingLayoutDesc()
                  .setVisibility(nvrhi::ShaderType::All)
                  .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
                  .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}))) {}
};
template <size_t Slot>
struct BindViewUniform {
    template <render::render_phase::PhaseItem P>
    struct Command {
        std::unordered_map<nvrhi::BufferHandle, nvrhi::BindingSetHandle> uniform_set_cache;
        void prepare(World&) { uniform_set_cache.clear(); }
        bool render(const P&,
                    Item<const UniformBuffer&> view_uniform,
                    std::optional<Item<>> entity_item,
                    ParamSet<Res<nvrhi::DeviceHandle>, Res<ViewUniformBindingLayout>> params,
                    render::render_phase::DrawContext& ctx) {
            auto&& [device, uniform_layout] = params.get();
            auto&& [ub]                     = *view_uniform;
            nvrhi::BufferHandle buffer      = ub.buffer;
            nvrhi::BindingSetHandle binding_set;
            if (auto it = uniform_set_cache.find(buffer); it != uniform_set_cache.end()) {
                binding_set = it->second;
            } else {
                binding_set = device.get()->createBindingSet(
                    nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, buffer)),
                    uniform_layout->layout);
                uniform_set_cache.emplace(buffer, binding_set);
            }
            ctx.graphics_state.bindings.resize(Slot + 1);
            ctx.graphics_state.bindings[Slot] = binding_set;
            return true;
        }
    };
};
}  // namespace epix::render::view