#include "epix/render/pixel.h"

using namespace epix::render::pixel::vulkan2;

EPIX_API PixelPipeline::mesh::mesh(
    size_t max_vertex_count, size_t max_model_count
)
    : max_vertex_count(max_vertex_count), max_model_count(max_model_count) {}

EPIX_API void PixelPipeline::mesh::clear() {
    vertices.clear();
    models.clear();
    draw_calls.clear();
}
EPIX_API void PixelPipeline::mesh::set_model(const glm::mat4& model) {
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

EPIX_API void PixelPipeline::mesh::assure_new(size_t count) {
    if (vertices.size() + count > max_vertex_count) {
        next_draw_call();
    }
}
EPIX_API void PixelPipeline::mesh::add_vertex(
    const glm::vec4& color, const glm::vec2& pos
) {
    if (draw_calls.empty()) {
        draw_calls.push_back({0, 0, 0, 0});
    }
    auto& last_call = draw_calls.back();
    last_call.vertex_count++;
    uint32_t model_index =
        static_cast<uint32_t>(models.size() - last_call.model_offset);
    if (model_index == 0) {
        set_model(glm::mat4(1.0f));
    }
    vertices.emplace_back(color, pos, model_index - 1);
}
EPIX_API void PixelPipeline::mesh::next_draw_call() {
    size_t vertex_offset = vertices.size();
    size_t model_offset  = models.size();
    draw_calls.emplace_back(vertex_offset, 0, model_offset, 0);
}
EPIX_API void PixelPipeline::mesh::draw_pixel(
    const glm::vec4& color, const glm::vec2& pos
) {
    assure_new(1);
    add_vertex(color, pos);
}