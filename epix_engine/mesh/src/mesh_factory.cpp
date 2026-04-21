module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <vector>
#endif
#include <glm/gtc/constants.hpp>

module epix.mesh;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;

using namespace epix::mesh;
namespace mesh = epix::mesh;

Mesh mesh::make_circle(float radius, std::optional<glm::vec4> color, std::optional<std::uint32_t> segment_count) {
    auto segments = segment_count.transform([](std::uint32_t count) { return std::clamp(count, 12u, 1024u); })
                        .value_or(static_cast<std::uint32_t>(
                            std::clamp(static_cast<int>(radius * glm::two_pi<float>() / 0.1f), 12, 1024)));

    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    positions.reserve(static_cast<std::size_t>(segments) + 1);
    indices.reserve(static_cast<std::size_t>(segments) * 3);

    positions.emplace_back(0.0f, 0.0f, 0.0f);
    for (std::uint32_t i = 0; i < segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        positions.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 0.0f);
    }
    for (std::uint32_t i = 0; i < segments; ++i) {
        std::uint32_t next = i + 1 == segments ? 1 : i + 2;
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(next);
    }

    auto mesh = Mesh()
                    .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
                    .with_attribute(Mesh::ATTRIBUTE_POSITION, positions)
                    .with_indices<std::uint32_t>(indices);
    if (color) {
        [[maybe_unused]] auto result =
            mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR,
                                  std::views::take(std::views::repeat(*color),
                                                   static_cast<std::size_t>(segments) + 1));
    }
    return mesh;
}

Mesh mesh::make_box2d(float width, float height, std::optional<glm::vec4> color) {
    float half_width  = width * 0.5f;
    float half_height = height * 0.5f;

    std::vector<glm::vec3> positions = {
        {-half_width, -half_height, 0.0f},
        {half_width, -half_height, 0.0f},
        {half_width, half_height, 0.0f},
        {-half_width, half_height, 0.0f},
    };
    std::vector<std::uint16_t> indices = {0, 1, 2, 2, 3, 0};

    auto mesh = Mesh()
                    .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
                    .with_attribute(Mesh::ATTRIBUTE_POSITION, positions)
                    .with_indices<std::uint16_t>(indices);
    if (color) {
        [[maybe_unused]] auto result =
            mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, std::views::take(std::views::repeat(*color), positions.size()));
    }
    return mesh;
}

Mesh mesh::make_box2d_uv(float width, float height, glm::vec4 uv_rect, std::optional<glm::vec4> vertex_color) {
    float half_width  = width * 0.5f;
    float half_height = height * 0.5f;

    std::vector<glm::vec3> positions = {
        {-half_width, -half_height, 0.0f},
        {half_width, -half_height, 0.0f},
        {half_width, half_height, 0.0f},
        {-half_width, half_height, 0.0f},
    };
    std::vector<glm::vec2> uvs = {
        {uv_rect.x, uv_rect.y},
        {uv_rect.z, uv_rect.y},
        {uv_rect.z, uv_rect.w},
        {uv_rect.x, uv_rect.w},
    };
    std::vector<std::uint16_t> indices = {0, 1, 2, 2, 3, 0};

    auto mesh = Mesh()
                    .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
                    .with_attribute(Mesh::ATTRIBUTE_POSITION, positions)
                    .with_attribute(Mesh::ATTRIBUTE_UV0, uvs)
                    .with_indices<std::uint16_t>(indices);
    if (vertex_color) {
        [[maybe_unused]] auto result =
            mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, std::views::take(std::views::repeat(*vertex_color), 4));
    }
    return mesh;
}
