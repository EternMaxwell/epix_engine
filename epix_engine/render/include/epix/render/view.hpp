#pragma once

#include <epix/transform.hpp>

#include "nvrhi/nvrhi.h"
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
}  // namespace epix::render::view