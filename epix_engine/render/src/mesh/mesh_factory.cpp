#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <ranges>

#include "epix/mesh/mesh.hpp"

using namespace epix;
using namespace epix::mesh;

Mesh mesh::make_circle(float radius, std::optional<glm::vec4> color, std::optional<uint32_t> segment_count) {
    auto segment_count_calculated = segment_count.value_or(
        static_cast<uint32_t>(std::clamp(static_cast<int>(radius * glm::two_pi<float>() / 0.1f), 12, 1024)));
    std::vector<glm::vec3> positions;
    positions.reserve(segment_count_calculated + 2);
    positions.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
    for (auto&& angle : std::views::iota(0u, segment_count_calculated) | std::views::transform([=](uint32_t i) {
                            return static_cast<float>(i) / static_cast<float>(segment_count_calculated) *
                                   glm::two_pi<float>();
                        })) {
        positions.push_back(glm::vec3(radius * std::cos(angle), radius * std::sin(angle), 0.0f));
    }
    positions.push_back(positions[1]);  // close the circle
    auto mesh = Mesh()
                    .with_primitive_type(nvrhi::PrimitiveType::TriangleFan)
                    .with_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
    color.and_then([&](const glm::vec4& c) -> std::optional<glm::vec4> {
        auto res = mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR,
                                         std::views::repeat(c) | std::views::take(segment_count_calculated + 2));
        return std::nullopt;
    });
    return std::move(mesh);
}
Mesh mesh::make_box2d(float width, float height, std::optional<glm::vec4> color) {
    float half_width                 = width * 0.5f;
    float half_height                = height * 0.5f;
    std::vector<glm::vec3> positions = {
        glm::vec3(-half_width, -half_height, 0.0f),
        glm::vec3(half_width, -half_height, 0.0f),
        glm::vec3(half_width, half_height, 0.0f),
        glm::vec3(-half_width, half_height, 0.0f),
    };
    std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};
    auto mesh                     = Mesh()
                    .with_primitive_type(nvrhi::PrimitiveType::TriangleList)
                    .with_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
                    .with_indices<std::uint16_t>(std::move(indices));
    color.and_then([&](const glm::vec4& c) -> std::optional<glm::vec4> {
        auto res = mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, std::views::repeat(c) | std::views::take(4));
        return std::nullopt;
    });
    return std::move(mesh);
}