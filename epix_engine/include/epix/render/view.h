#pragma once

#include <epix/render/window.h>
#include <epix/transform/transform.h>
#include <epix/vulkan.h>

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
    entt::dense_map<glm::uvec2, nvrhi::TextureHandle, UVec2Hash> cache;
};

struct ViewPlugin {
    EPIX_API void build(App& app);
};

EPIX_API void prepare_view_target(Query<Item<Entity, camera::ExtractedCamera, ExtractedView>> views,
                                  Commands& cmd,
                                  Res<window::ExtractedWindows> extracted_windows);
EPIX_API void create_view_depth(Query<Item<Entity, ExtractedView>> views,
                                Res<nvrhi::DeviceHandle> device,
                                ResMut<ViewDepthCache> depth_cache,
                                Commands& cmd);
}  // namespace epix::render::view