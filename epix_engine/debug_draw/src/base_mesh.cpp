#include "epix/render/debug.h"

using namespace epix::render::debug;

EPIX_API vulkan2::PipelineBase::mesh::mesh(
    size_t max_vertex_count, size_t max_model_count
)
    : max_vertex_count(max_vertex_count), max_model_count(max_model_count) {}

EPIX_API void vulkan2::PipelineBase::mesh::clear() {
    vertices.clear();
    models.clear();
    draw_calls.clear();
}
EPIX_API void vulkan2::PipelineBase::mesh::set_model(const glm::mat4& model) {
    if (draw_calls.empty()) {
        draw_calls.push_back({0, 0, 0, 0});
    }
    if (draw_calls.back().model_count >= max_model_count) {
        next_draw_call();
    }
    auto& last_call = draw_calls.back();
    last_call.model_count++;
    models.push_back(model);
}

EPIX_API void vulkan2::PipelineBase::mesh::assure_new(size_t count) {
    if (vertices.size() + count > max_vertex_count) {
        next_draw_call();
    }
}
EPIX_API void vulkan2::PipelineBase::mesh::add_vertex(
    const glm::vec3& pos, const glm::vec4& color
) {
    if (draw_calls.empty()) {
        draw_calls.push_back({0, 0, 0, 0});
    }
    assure_new(1);
    auto& last_call = draw_calls.back();
    last_call.vertex_count++;
    uint32_t model_index =
        static_cast<uint32_t>(models.size() - last_call.model_offset);
    if (model_index == 0) {
        set_model(glm::mat4(1.0f));
    }
    vertices.emplace_back(pos, color, model_index - 1);
}
EPIX_API void vulkan2::PipelineBase::mesh::next_draw_call() {
    size_t vertex_offset = vertices.size();
    size_t model_offset  = models.size();
    draw_calls.emplace_back(vertex_offset, 0, model_offset, 0);
}

EPIX_API void vulkan2::PipelineBase::mesh::draw_point(
    const glm::vec3& pos, const glm::vec4& color
) {
    assure_new(1);
    add_vertex(pos, color);
}
EPIX_API void vulkan2::PipelineBase::mesh::draw_line(
    const glm::vec3& start, const glm::vec3& end, const glm::vec4& color
) {
    assure_new(2);
    add_vertex(start, color);
    add_vertex(end, color);
}
EPIX_API void vulkan2::PipelineBase::mesh::draw_triangle(
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& v2,
    const glm::vec4& color
) {
    assure_new(3);
    add_vertex(v0, color);
    add_vertex(v1, color);
    add_vertex(v2, color);
}