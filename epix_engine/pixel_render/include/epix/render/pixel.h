#pragma once

#include <epix/common.h>
#include <epix/rdvk.h>

#include "pixel/components.h"
#include "pixel/systems.h"

namespace epix::render::pixel {
using namespace epix::prelude;
struct PixelRenderPlugin : Plugin {
    EPIX_API void build(App& app) override;
};
namespace vulkan2 {
using namespace epix::render::vulkan2::backend;
struct PixelVertex {
    glm::vec4 color;
    glm::vec2 pos;
};
struct PixelMesh : public epix::render::vulkan2::Mesh<PixelVertex> {
    PixelMesh() : epix::render::vulkan2::Mesh<PixelVertex>(false) {}
    void draw_pixel(const glm::vec2& pos, const glm::vec4& color) {
        emplace_vertex(color, pos);
    }
};
using PixelStagingMesh = epix::render::vulkan2::StagingMesh<
    epix::render::vulkan2::Mesh<PixelVertex>>;

struct PixelPipeline {
    EPIX_API static epix::render::vulkan2::PipelineBase* create();
};
}  // namespace vulkan2
}  // namespace epix::render::pixel