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

struct ViewPlugin {
    EPIX_API void build(App& app);
};

EPIX_API void prepare_view_target(Query<Item<Entity, camera::ExtractedCamera, ExtractedView>> views,
                                  Commands& cmd,
                                  Res<window::ExtractedWindows> extracted_windows);
}  // namespace epix::render::view