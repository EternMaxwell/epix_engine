#pragma once

#include <epix/transform/transform.h>
#include <epix/vulkan.h>


namespace epix::view {
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
}  // namespace epix::view