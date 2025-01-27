#pragma once

#include <epix/common.h>
#include <epix/rdvk.h>

#include "draw_dbg/components.h"
#include "draw_dbg/systems.h"

namespace epix::render::debug {
namespace vulkan {
using namespace epix::prelude;
struct DebugRenderPlugin : Plugin {
    size_t max_vertex_count = 2048 * 256;
    size_t max_model_count  = 2048 * 64;
    EPIX_API void build(App& app) override;
};
}  // namespace vulkan
namespace vulkan2 {
using namespace epix::render::vulkan2::backend;
struct DebugVertex {
    glm::vec3 pos;
    glm::vec4 color;
};
struct DebugMesh : public epix::render::vulkan2::Mesh<DebugVertex> {
    DebugMesh() : epix::render::vulkan2::Mesh<DebugVertex>(false) {}
    void draw_point(const glm::vec3& pos, const glm::vec4& color) {
        emplace_vertex(pos, color);
    }
    void draw_line(
        const glm::vec3& start, const glm::vec3& end, const glm::vec4& color
    ) {
        emplace_vertex(start, color);
        emplace_vertex(end, color);
    }
    void draw_triangle(
        const glm::vec3& v0,
        const glm::vec3& v1,
        const glm::vec3& v2,
        const glm::vec4& color
    ) {
        emplace_vertex(v0, color);
        emplace_vertex(v1, color);
        emplace_vertex(v2, color);
    }
};
using DebugStagingMesh = epix::render::vulkan2::StagingMesh<
    epix::render::vulkan2::Mesh<DebugVertex>>;

using DebugGPUMesh = epix::render::vulkan2::GPUMesh<DebugStagingMesh>;

struct DebugPipelines {
    EPIX_API static epix::render::vulkan2::PipelineBase* point_pipeline();
    EPIX_API static epix::render::vulkan2::PipelineBase* line_pipeline();
    EPIX_API static epix::render::vulkan2::PipelineBase* triangle_pipeline();
};
}  // namespace vulkan2
}  // namespace epix::render::debug