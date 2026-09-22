#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include <vector>

import epix.mesh;
import epix.render;
import webgpu;
import glm;
namespace mesh = epix::mesh;

TEST(VertexAttributeValues, CoversEveryBevyAlternativeAndMetadataContract) {
    std::size_t expected_index = 0;
    const auto check           = [&]<typename Alternative>(Alternative alternative, std::string_view name,
                                                           wgpu::VertexFormat format) {
        const mesh::VertexAttributeValues values{std::move(alternative)};
        EXPECT_EQ(values.enum_variant_index(), expected_index++);
        EXPECT_EQ(values.enum_variant_name(), name);
        EXPECT_EQ(values.format(), format);
        EXPECT_EQ(static_cast<wgpu::VertexFormat>(values), format);
        EXPECT_EQ(values.len(), 1u);
        EXPECT_FALSE(values.is_empty());
        EXPECT_EQ(values.get_bytes().size(), mesh::vertex_format_size(format));
        EXPECT_EQ(values, values);
    };

    // Every alternative below contains one element, so its byte count must
    // equal the matching WebGPU vertex-format size.
    using Values = mesh::VertexAttributeValues;
    check(Values::Float32{{1.0f}}, "Float32", wgpu::VertexFormat::eFloat32);
    check(Values::Sint32{{1}}, "Sint32", wgpu::VertexFormat::eSint32);
    check(Values::Uint32{{1}}, "Uint32", wgpu::VertexFormat::eUint32);
    check(Values::Float32x2{{{1.0f, 2.0f}}}, "Float32x2", wgpu::VertexFormat::eFloat32x2);
    check(Values::Sint32x2{{{1, 2}}}, "Sint32x2", wgpu::VertexFormat::eSint32x2);
    check(Values::Uint32x2{{{1, 2}}}, "Uint32x2", wgpu::VertexFormat::eUint32x2);
    check(Values::Float32x3{{{1.0f, 2.0f, 3.0f}}}, "Float32x3", wgpu::VertexFormat::eFloat32x3);
    check(Values::Sint32x3{{{1, 2, 3}}}, "Sint32x3", wgpu::VertexFormat::eSint32x3);
    check(Values::Uint32x3{{{1, 2, 3}}}, "Uint32x3", wgpu::VertexFormat::eUint32x3);
    check(Values::Float32x4{{{1.0f, 2.0f, 3.0f, 4.0f}}}, "Float32x4", wgpu::VertexFormat::eFloat32x4);
    check(Values::Sint32x4{{{1, 2, 3, 4}}}, "Sint32x4", wgpu::VertexFormat::eSint32x4);
    check(Values::Uint32x4{{{1, 2, 3, 4}}}, "Uint32x4", wgpu::VertexFormat::eUint32x4);
    check(Values::Sint16x2{{{1, 2}}}, "Sint16x2", wgpu::VertexFormat::eSint16x2);
    check(Values::Snorm16x2{{{1, 2}}}, "Snorm16x2", wgpu::VertexFormat::eSnorm16x2);
    check(Values::Uint16x2{{{1, 2}}}, "Uint16x2", wgpu::VertexFormat::eUint16x2);
    check(Values::Unorm16x2{{{1, 2}}}, "Unorm16x2", wgpu::VertexFormat::eUnorm16x2);
    check(Values::Sint16x4{{{1, 2, 3, 4}}}, "Sint16x4", wgpu::VertexFormat::eSint16x4);
    check(Values::Snorm16x4{{{1, 2, 3, 4}}}, "Snorm16x4", wgpu::VertexFormat::eSnorm16x4);
    check(Values::Uint16x4{{{1, 2, 3, 4}}}, "Uint16x4", wgpu::VertexFormat::eUint16x4);
    check(Values::Unorm16x4{{{1, 2, 3, 4}}}, "Unorm16x4", wgpu::VertexFormat::eUnorm16x4);
    check(Values::Sint8x2{{{1, 2}}}, "Sint8x2", wgpu::VertexFormat::eSint8x2);
    check(Values::Snorm8x2{{{1, 2}}}, "Snorm8x2", wgpu::VertexFormat::eSnorm8x2);
    check(Values::Uint8x2{{{1, 2}}}, "Uint8x2", wgpu::VertexFormat::eUint8x2);
    check(Values::Unorm8x2{{{1, 2}}}, "Unorm8x2", wgpu::VertexFormat::eUnorm8x2);
    check(Values::Sint8x4{{{1, 2, 3, 4}}}, "Sint8x4", wgpu::VertexFormat::eSint8x4);
    check(Values::Snorm8x4{{{1, 2, 3, 4}}}, "Snorm8x4", wgpu::VertexFormat::eSnorm8x4);
    check(Values::Uint8x4{{{1, 2, 3, 4}}}, "Uint8x4", wgpu::VertexFormat::eUint8x4);
    check(Values::Unorm8x4{{{1, 2, 3, 4}}}, "Unorm8x4", wgpu::VertexFormat::eUnorm8x4);
    EXPECT_EQ(expected_index, 28u);

    const Values empty{std::vector<float>{}};
    EXPECT_TRUE(empty.is_empty());
    EXPECT_TRUE(empty.get_bytes().empty());

    const Values float3{std::vector<Values::Float32x3Value>{{1.0f, 2.0f, 3.0f}}};
    ASSERT_NE(float3.as_float3(), nullptr);
    EXPECT_EQ(float3.as_float3()->front(), (Values::Float32x3Value{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(empty.as_float3(), nullptr);
}

TEST(VertexAttributeValues, MatchesBevyInputAndFallibleOutputConversions) {
    using Values = mesh::VertexAttributeValues;

    const Values from_float{std::vector<float>{1.0f, 2.0f}};
    EXPECT_EQ(from_float.enum_variant_name(), "Float32");
    const Values from_vec2{std::vector{glm::vec2(1.0f, 2.0f)}};
    const Values from_vec3{std::vector{glm::vec3(1.0f, 2.0f, 3.0f)}};
    const Values from_vec4{std::vector{glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)}};
    const Values from_ivec2{std::vector{glm::ivec2(1, 2)}};
    const Values from_ivec3{std::vector{glm::ivec3(1, 2, 3)}};
    const Values from_ivec4{std::vector{glm::ivec4(1, 2, 3, 4)}};
    const Values from_uvec2{std::vector{glm::uvec2(1, 2)}};
    const Values from_uvec3{std::vector{glm::uvec3(1, 2, 3)}};
    const Values from_uvec4{std::vector{glm::uvec4(1, 2, 3, 4)}};
    EXPECT_EQ(from_vec2.enum_variant_name(), "Float32x2");
    EXPECT_EQ(from_vec3.enum_variant_name(), "Float32x3");
    EXPECT_EQ(from_vec4.enum_variant_name(), "Float32x4");
    EXPECT_EQ(from_ivec2.enum_variant_name(), "Sint32x2");
    EXPECT_EQ(from_ivec3.enum_variant_name(), "Sint32x3");
    EXPECT_EQ(from_ivec4.enum_variant_name(), "Sint32x4");
    EXPECT_EQ(from_uvec2.enum_variant_name(), "Uint32x2");
    EXPECT_EQ(from_uvec3.enum_variant_name(), "Uint32x3");
    EXPECT_EQ(from_uvec4.enum_variant_name(), "Uint32x4");

    auto float_result = Values{std::vector<float>{1.0f, 2.0f}}.try_into<float>();
    ASSERT_TRUE(float_result.has_value());
    EXPECT_EQ(*float_result, (std::vector<float>{1.0f, 2.0f}));
    auto vec3_result = Values{std::vector{glm::vec3(1.0f, 2.0f, 3.0f)}}.try_into<glm::vec3>();
    ASSERT_TRUE(vec3_result.has_value());
    EXPECT_EQ(*vec3_result, (std::vector{glm::vec3(1.0f, 2.0f, 3.0f)}));

    EXPECT_TRUE(
        (Values{std::vector<Values::Float32x2Value>{{1.0f, 2.0f}}}.try_into<Values::Float32x2Value>().has_value()));
    EXPECT_TRUE((Values{std::vector<Values::Float32x3Value>{{1.0f, 2.0f, 3.0f}}}
                     .try_into<Values::Float32x3Value>()
                     .has_value()));
    EXPECT_TRUE((Values{std::vector<Values::Float32x4Value>{{1.0f, 2.0f, 3.0f, 4.0f}}}
                     .try_into<Values::Float32x4Value>()
                     .has_value()));
    EXPECT_TRUE(Values{std::vector<std::int32_t>{1}}.try_into<std::int32_t>().has_value());
    EXPECT_TRUE((Values{std::vector<Values::Sint32x2Value>{{1, 2}}}.try_into<Values::Sint32x2Value>().has_value()));
    EXPECT_TRUE((Values{std::vector<Values::Sint32x3Value>{{1, 2, 3}}}.try_into<Values::Sint32x3Value>().has_value()));
    EXPECT_TRUE(
        (Values{std::vector<Values::Sint32x4Value>{{1, 2, 3, 4}}}.try_into<Values::Sint32x4Value>().has_value()));
    EXPECT_TRUE(Values{std::vector<std::uint32_t>{1}}.try_into<std::uint32_t>().has_value());
    EXPECT_TRUE((Values{std::vector<Values::Uint32x2Value>{{1, 2}}}.try_into<Values::Uint32x2Value>().has_value()));
    EXPECT_TRUE((Values{std::vector<Values::Uint32x3Value>{{1, 2, 3}}}.try_into<Values::Uint32x3Value>().has_value()));
    EXPECT_TRUE(
        (Values{std::vector<Values::Uint32x4Value>{{1, 2, 3, 4}}}.try_into<Values::Uint32x4Value>().has_value()));

    EXPECT_TRUE(Values{from_vec2}.try_into<glm::vec2>().has_value());
    EXPECT_TRUE(Values{from_vec4}.try_into<glm::vec4>().has_value());
    EXPECT_TRUE(Values{from_ivec2}.try_into<glm::ivec2>().has_value());
    EXPECT_TRUE(Values{from_ivec3}.try_into<glm::ivec3>().has_value());
    EXPECT_TRUE(Values{from_ivec4}.try_into<glm::ivec4>().has_value());
    EXPECT_TRUE(Values{from_uvec2}.try_into<glm::uvec2>().has_value());
    EXPECT_TRUE(Values{from_uvec3}.try_into<glm::uvec3>().has_value());
    EXPECT_TRUE(Values{from_uvec4}.try_into<glm::uvec4>().has_value());

    EXPECT_TRUE((Values{Values::Sint16x2{{{1, 2}}}}.try_into<Values::Sint16x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Snorm16x2{{{1, 2}}}}.try_into<Values::Sint16x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Sint16x4{{{1, 2, 3, 4}}}}.try_into<Values::Sint16x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Snorm16x4{{{1, 2, 3, 4}}}}.try_into<Values::Sint16x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Uint16x2{{{1, 2}}}}.try_into<Values::Uint16x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Unorm16x2{{{1, 2}}}}.try_into<Values::Uint16x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Uint16x4{{{1, 2, 3, 4}}}}.try_into<Values::Uint16x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Unorm16x4{{{1, 2, 3, 4}}}}.try_into<Values::Uint16x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Sint8x2{{{1, 2}}}}.try_into<Values::Sint8x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Snorm8x2{{{1, 2}}}}.try_into<Values::Sint8x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Sint8x4{{{1, 2, 3, 4}}}}.try_into<Values::Sint8x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Snorm8x4{{{1, 2, 3, 4}}}}.try_into<Values::Sint8x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Uint8x2{{{1, 2}}}}.try_into<Values::Uint8x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Unorm8x2{{{1, 2}}}}.try_into<Values::Uint8x2Value>().has_value()));
    EXPECT_TRUE((Values{Values::Uint8x4{{{1, 2, 3, 4}}}}.try_into<Values::Uint8x4Value>().has_value()));
    EXPECT_TRUE((Values{Values::Unorm8x4{{{1, 2, 3, 4}}}}.try_into<Values::Uint8x4Value>().has_value()));

    auto snorm16 = Values{Values::Snorm16x2{{{1, -2}}}}.try_into<Values::Sint16x2Value>();
    auto unorm16 = Values{Values::Unorm16x4{{{1, 2, 3, 4}}}}.try_into<Values::Uint16x4Value>();
    auto snorm8  = Values{Values::Snorm8x4{{{1, -2, 3, -4}}}}.try_into<Values::Sint8x4Value>();
    auto unorm8  = Values{Values::Unorm8x2{{{1, 2}}}}.try_into<Values::Uint8x2Value>();
    ASSERT_TRUE(snorm16.has_value());
    ASSERT_TRUE(unorm16.has_value());
    ASSERT_TRUE(snorm8.has_value());
    ASSERT_TRUE(unorm8.has_value());
    EXPECT_EQ(snorm16->front(), (Values::Sint16x2Value{1, -2}));
    EXPECT_EQ(unorm16->front(), (Values::Uint16x4Value{1, 2, 3, 4}));
    EXPECT_EQ(snorm8->front(), (Values::Sint8x4Value{1, -2, 3, -4}));
    EXPECT_EQ(unorm8->front(), (Values::Uint8x2Value{1, 2}));

    const Values rejected{std::vector<float>{3.0f}};
    auto error = Values{rejected}.try_into<std::uint32_t>();
    ASSERT_FALSE(error.has_value());
    ASSERT_TRUE(error.error().from);
    EXPECT_EQ(*error.error().from, rejected);
    EXPECT_EQ(error.error().variant, "Float32");
    EXPECT_NE(error.error().to_string().find("cannot convert VertexAttributeValues::Float32 to"), std::string::npos);
}

TEST(MeshErrors, WindingAndTriangleErrorsPreserveBevyVariantsAndMessages) {
    const mesh::MeshWindingInvertError wrong_winding{mesh::mesh_winding_invert_error::WrongTopology{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_winding_invert_error::WrongTopology>(wrong_winding));
    EXPECT_EQ(wrong_winding.to_string(), "Mesh winding inversion does not work for primitive topology `PointList`");

    const mesh::MeshWindingInvertError abrupt{mesh::mesh_winding_invert_error::AbruptIndicesEnd{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_winding_invert_error::AbruptIndicesEnd>(abrupt));
    EXPECT_EQ(abrupt.to_string(), "Indices weren't in chunks according to topology");

    const mesh::MeshWindingInvertError winding_access{mesh::MeshAccessError::ExtractedToRenderWorld};
    EXPECT_TRUE(std::holds_alternative<mesh::MeshAccessError>(winding_access));
    EXPECT_EQ(winding_access.to_string(),
              "Mesh access error: The mesh vertex/index data has been extracted to the RenderWorld (via "
              "`Mesh::asset_usage`)");

    const mesh::MeshTrianglesError positions{mesh::mesh_triangles_error::PositionsFormat{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_triangles_error::PositionsFormat>(positions));
    EXPECT_EQ(positions.to_string(), "Source mesh position data is not Float32x3");

    const mesh::MeshTrianglesError bad_indices{mesh::mesh_triangles_error::BadIndices{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_triangles_error::BadIndices>(bad_indices));
    EXPECT_EQ(bad_indices.to_string(), "Face index data references vertices that do not exist");

    const mesh::MeshTrianglesError triangle_access{mesh::MeshAccessError::NotFound};
    EXPECT_TRUE(std::holds_alternative<mesh::MeshAccessError>(triangle_access));
    EXPECT_EQ(triangle_access.to_string(), "mesh access error: The requested mesh data wasn't found in this mesh");
}

TEST(MeshErrors, MergeErrorPreservesBevyVariantsAndMessages) {
    const mesh::MeshMergeError topology{mesh::mesh_merge_error::IncompatiblePrimitiveTopology{
        .self_primitive_topology  = wgpu::PrimitiveTopology::eTriangleList,
        .other_primitive_topology = wgpu::PrimitiveTopology::eLineList,
    }};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_merge_error::IncompatiblePrimitiveTopology>(topology));
    EXPECT_EQ(topology.to_string(), "Incompatible primitive topologies: TriangleList and LineList");

    const mesh::MeshMergeError attributes{mesh::mesh_merge_error::IncompatibleVertexAttributes{
        .self_attribute  = mesh::Mesh::ATTRIBUTE_POSITION,
        .other_attribute = mesh::Mesh::ATTRIBUTE_NORMAL,
    }};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_merge_error::IncompatibleVertexAttributes>(attributes));
    EXPECT_EQ(attributes.to_string(), "Incompatible vertex attribute types: Vertex_Position and Vertex_Normal");

    // Bevy's `None` arm of the other-attribute name lookup.
    const mesh::MeshMergeError unnamed{mesh::mesh_merge_error::IncompatibleVertexAttributes{
        .self_attribute  = mesh::Mesh::ATTRIBUTE_POSITION,
        .other_attribute = std::nullopt,
    }};
    EXPECT_EQ(unnamed.to_string(), "Incompatible vertex attribute types: Vertex_Position and None");

    const mesh::MeshMergeError access{mesh::MeshAccessError::ExtractedToRenderWorld};
    EXPECT_TRUE(std::holds_alternative<mesh::MeshAccessError>(access));
    EXPECT_EQ(access.to_string(),
              "Mesh access error: The mesh vertex/index data has been extracted to the RenderWorld (via "
              "`Mesh::asset_usage`)");
}

TEST(MeshAlgorithms, DuplicateVerticesMatchesBevyIndexedExpansion) {
    const std::vector<glm::vec3> positions{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    const std::vector<glm::vec4> colors{
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    };
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors);
    value.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 2, 1, 2, 3, 1}});

    value.duplicate_vertices();

    EXPECT_FALSE(value.indices().has_value());
    const auto* duplicated_positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* duplicated_colors =
        value.attribute(mesh::Mesh::ATTRIBUTE_COLOR)->get().get_if<mesh::VertexAttributeValues::Float32x4>();
    ASSERT_NE(duplicated_positions, nullptr);
    ASSERT_NE(duplicated_colors, nullptr);
    EXPECT_EQ(*duplicated_positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 0.0f},
                                                                                               {1.0f, 1.0f, 0.0f},
                                                                                               {1.0f, 0.0f, 0.0f},
                                                                                               {1.0f, 1.0f, 0.0f},
                                                                                               {0.0f, 1.0f, 0.0f},
                                                                                               {1.0f, 0.0f, 0.0f}}));
    EXPECT_EQ(*duplicated_colors, (std::vector<mesh::VertexAttributeValues::Float32x4Value>{{1.0f, 0.0f, 0.0f, 1.0f},
                                                                                            {0.0f, 0.0f, 1.0f, 1.0f},
                                                                                            {0.0f, 1.0f, 0.0f, 1.0f},
                                                                                            {0.0f, 0.0f, 1.0f, 1.0f},
                                                                                            {1.0f, 1.0f, 1.0f, 1.0f},
                                                                                            {0.0f, 1.0f, 0.0f, 1.0f}}));

    mesh::Mesh without_indices(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    auto unchanged = std::move(without_indices).try_with_duplicated_vertices();
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_FALSE(unchanged->indices().has_value());

    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto inaccessible = extracted.try_duplicate_vertices();
    ASSERT_FALSE(inaccessible.has_value());
    EXPECT_EQ(inaccessible.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
}

// Bevy's `Mesh::merge` appends the other mesh's attribute values, offsets its
// indices past this mesh's last vertex, and ignores attributes this mesh does not
// have (`mesh.rs:1643-1714`).
TEST(MeshAlgorithms, MergeAppendsBevyAttributesAndOffsetsIndices) {
    const std::vector<glm::vec3> first_positions{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    const std::vector<glm::vec3> second_positions{{2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}};
    const std::vector<glm::vec3> normals{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};

    mesh::Mesh first(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    first.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, first_positions);
    first.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, normals);
    first.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 0}});
    EXPECT_EQ(first.count_vertices(), 2u);

    mesh::Mesh second(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    second.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, second_positions);
    second.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, normals);
    second.insert_attribute(mesh::Mesh::ATTRIBUTE_UV_0, std::vector<glm::vec2>{{0.0f, 0.0f}, {1.0f, 1.0f}});
    second.insert_indices(mesh::Indices{std::vector<std::uint16_t>{1, 0, 1}});

    ASSERT_TRUE(first.merge(second).has_value());

    EXPECT_EQ(first.count_vertices(), 4u);
    const auto* positions =
        first.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}}));
    const auto* merged_normals =
        first.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(merged_normals, nullptr);
    EXPECT_EQ(*merged_normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                                   {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}));
    // The other mesh's UVs are ignored: this mesh has no UV attribute.
    EXPECT_FALSE(first.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    // Indices are appended with the offset, keeping the 16-bit alternative.
    ASSERT_NE(first.indices()->get().as_u16(), nullptr);
    EXPECT_EQ(*first.indices()->get().as_u16(), (std::vector<std::uint16_t>{0, 1, 0, 3, 2, 3}));

    // A mesh without indices contributes vertices only.
    mesh::Mesh unindexed(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    unindexed.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, first_positions);
    unindexed.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, normals);
    ASSERT_TRUE(first.merge(unindexed).has_value());
    EXPECT_EQ(first.count_vertices(), 6u);
    EXPECT_EQ(*first.indices()->get().as_u16(), (std::vector<std::uint16_t>{0, 1, 0, 3, 2, 3}));

    // Only shared attributes are appended, exactly as in Bevy: a mesh that lacks this mesh's
    // normals leaves the position list longer, and `count_vertices` then reports the minimum.
    mesh::Mesh position_only(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    position_only.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, first_positions);
    ASSERT_TRUE(first.merge(position_only).has_value());
    EXPECT_EQ(
        first.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>()->size(),
        8u);
    EXPECT_EQ(
        first.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>()->size(),
        6u);
    EXPECT_EQ(first.count_vertices(), 6u);
}

TEST(MeshAlgorithms, MergeRejectsBevyIncompatibleTopologiesAndAttributeTypes) {
    const std::vector<glm::vec3> positions{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};

    mesh::Mesh triangles(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    triangles.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);

    mesh::Mesh lines(wgpu::PrimitiveTopology::eLineList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    lines.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);

    const auto topology = triangles.merge(lines);
    ASSERT_FALSE(topology.has_value());
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_merge_error::IncompatiblePrimitiveTopology>(topology.error()));
    EXPECT_EQ(topology.error().to_string(), "Incompatible primitive topologies: TriangleList and LineList");
    // The rejection happens before anything is appended.
    EXPECT_EQ(triangles.count_vertices(), 2u);

    // The same attribute ID with a different type: both descriptors are reported.
    const mesh::MeshVertexAttribute custom_position{"Custom_Position", mesh::MeshVertexAttributeId{0},
                                                    wgpu::VertexFormat::eUint32x3};
    mesh::Mesh other(triangles);
    other.insert_attribute(custom_position, std::vector<glm::uvec3>{{0u, 0u, 0u}, {1u, 0u, 0u}});

    const auto attribute_types = triangles.merge(other);
    ASSERT_FALSE(attribute_types.has_value());
    ASSERT_TRUE(std::holds_alternative<mesh::mesh_merge_error::IncompatibleVertexAttributes>(attribute_types.error()));
    const auto& mismatch = std::get<mesh::mesh_merge_error::IncompatibleVertexAttributes>(attribute_types.error());
    EXPECT_EQ(mismatch.self_attribute.name, "Vertex_Position");
    ASSERT_TRUE(mismatch.other_attribute.has_value());
    EXPECT_EQ(mismatch.other_attribute->name, "Custom_Position");
    EXPECT_EQ(attribute_types.error().to_string(),
              "Incompatible vertex attribute types: Vertex_Position and Custom_Position");

    // A mesh whose data has been extracted cannot be merged from.
    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    ASSERT_TRUE(extracted.take_gpu_data().has_value());

    const auto inaccessible = triangles.merge(extracted);
    ASSERT_FALSE(inaccessible.has_value());
    ASSERT_TRUE(std::holds_alternative<mesh::MeshAccessError>(inaccessible.error()));
    EXPECT_EQ(std::get<mesh::MeshAccessError>(inaccessible.error()), mesh::MeshAccessError::ExtractedToRenderWorld);
}

// Bevy's `Mesh::translated_by` moves vertex positions only (`mesh.rs:1803-1853`).
TEST(MeshAlgorithms, TranslateMatchesBevyPositionOnlyTransform) {
    const std::vector<glm::vec3> positions{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    const std::vector<glm::vec3> normals{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    const std::vector<glm::vec2> uvs{{0.0f, 0.0f}, {1.0f, 1.0f}};

    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, normals);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_UV_0, uvs);

    const mesh::Mesh moved = std::move(value).translated_by(glm::vec3{1.0f, 2.0f, 3.0f});
    const auto* moved_positions =
        moved.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(moved_positions, nullptr);
    EXPECT_EQ(*moved_positions,
              (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{1.0f, 2.0f, 3.0f}, {2.0f, 2.0f, 3.0f}}));
    const auto* moved_normals =
        moved.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(moved_normals, nullptr);
    EXPECT_EQ(*moved_normals,
              (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}));
    const auto* moved_uvs =
        moved.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(moved_uvs, nullptr);
    EXPECT_EQ(*moved_uvs, (std::vector<mesh::VertexAttributeValues::Float32x2Value>{{0.0f, 0.0f}, {1.0f, 1.0f}}));

    // The zero translation is a no-op, and the in-place fallible form matches.
    mesh::Mesh in_place(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    in_place.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    ASSERT_TRUE(in_place.try_translate_by(glm::vec3{0.0f}).has_value());
    ASSERT_TRUE(in_place.try_translate_by(glm::vec3{-1.0f, 0.0f, 0.0f}).has_value());
    EXPECT_EQ(
        *in_place.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>(),
        (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}));

    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto inaccessible = extracted.try_translate_by(glm::vec3{1.0f});
    ASSERT_FALSE(inaccessible.has_value());
    EXPECT_EQ(inaccessible.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_THROW(extracted.translate_by(glm::vec3{1.0f}), std::logic_error);
}

// Bevy's `Mesh::scaled_by` scales positions, then renormalizes normals through
// `scale_normal` and tangents, keeping the tangent handedness (`mesh.rs:1928-2007`,
// `scale_normal` at `mesh.rs:2343-2357`).
TEST(MeshAlgorithms, ScaleMatchesBevyNormalAndTangentTransform) {
    const auto positions_of = [](const mesh::Mesh& mesh) {
        return *mesh.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    };
    const auto normals_of = [](const mesh::Mesh& mesh) {
        return *mesh.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    };
    const auto tangents_of = [](const mesh::Mesh& mesh) {
        return *mesh.attribute(mesh::Mesh::ATTRIBUTE_TANGENT)->get().get_if<mesh::VertexAttributeValues::Float32x4>();
    };

    // A uniform scale moves positions only: normals and tangents are already correct.
    mesh::Mesh uniform(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    uniform.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 2.0f, 3.0f}});
    uniform.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::vector<glm::vec3>{{0.0f, 0.0f, 1.0f}});
    uniform.insert_attribute(mesh::Mesh::ATTRIBUTE_TANGENT, std::vector<glm::vec4>{{1.0f, 0.0f, 0.0f, 1.0f}});

    const mesh::Mesh scaled = std::move(uniform).scaled_by(glm::vec3{2.0f});
    EXPECT_EQ(positions_of(scaled), (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{2.0f, 4.0f, 6.0f}}));
    EXPECT_EQ(normals_of(scaled), (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 1.0f}}));
    EXPECT_EQ(tangents_of(scaled),
              (std::vector<mesh::VertexAttributeValues::Float32x4Value>{{1.0f, 0.0f, 0.0f, 1.0f}}));

    // The extrusion's back-cap flip: negating Z negates both the position and the normal.
    mesh::Mesh flipped(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    flipped.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 2.0f, 3.0f}});
    flipped.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::vector<glm::vec3>{{0.0f, 0.0f, 1.0f}});
    ASSERT_TRUE(flipped.try_scale_by(glm::vec3{1.0f, 1.0f, -1.0f}).has_value());
    EXPECT_EQ(positions_of(flipped), (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{1.0f, 2.0f, -3.0f}}));
    EXPECT_EQ(normals_of(flipped), (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, -1.0f}}));

    // A non-uniform scale renormalizes a non-axis-aligned normal: (1, 0, 1) / sqrt(2) with a
    // reciprocal scale of (0.5, 1, 1) becomes (1, 0, 2) / sqrt(5).
    mesh::Mesh oblique(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    oblique.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    oblique.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::vector<glm::vec3>{{0.70710678f, 0.0f, 0.70710678f}});
    ASSERT_TRUE(oblique.try_scale_by(glm::vec3{2.0f, 1.0f, 1.0f}).has_value());
    const auto* oblique_normals =
        oblique.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(oblique_normals, nullptr);
    ASSERT_EQ(oblique_normals->size(), 1u);
    EXPECT_NEAR((*oblique_normals)[0][0], 0.44721360f, 1e-6f);
    EXPECT_NEAR((*oblique_normals)[0][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*oblique_normals)[0][2], 0.89442719f, 1e-6f);

    // A zero scale component collapses the position and points the normal along that axis, which is
    // Bevy's infinity branch of `scale_normal`.
    mesh::Mesh collapsed(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    collapsed.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 1.0f, 1.0f}});
    collapsed.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::vector<glm::vec3>{{0.0f, 0.0f, 1.0f}});
    ASSERT_TRUE(collapsed.try_scale_by(glm::vec3{1.0f, 1.0f, 0.0f}).has_value());
    EXPECT_EQ(positions_of(collapsed), (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{1.0f, 1.0f, 0.0f}}));
    EXPECT_EQ(normals_of(collapsed), (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 1.0f}}));

    // Tangents are renormalized and keep their handedness.
    mesh::Mesh tangents(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    tangents.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    tangents.insert_attribute(mesh::Mesh::ATTRIBUTE_TANGENT, std::vector<glm::vec4>{{1.0f, 0.0f, 0.0f, -1.0f}});
    ASSERT_TRUE(tangents.try_scale_by(glm::vec3{1.0f, 2.0f, 1.0f}).has_value());
    EXPECT_EQ(tangents_of(tangents),
              (std::vector<mesh::VertexAttributeValues::Float32x4Value>{{1.0f, 0.0f, 0.0f, -1.0f}}));

    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto inaccessible = std::move(extracted).try_scaled_by(glm::vec3{2.0f});
    ASSERT_FALSE(inaccessible.has_value());
    EXPECT_EQ(inaccessible.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_THROW(std::move(extracted).scaled_by(glm::vec3{2.0f}), std::logic_error);
}

// Bevy's `Mesh::rotate_by` rotates positions, then renormalizes and rotates normals and tangents,
// keeping the tangent handedness; a near-identity rotation skips the normal/tangent passes
// (`mesh.rs:1855-1926`, with `Quat::is_near_identity` being glam 0.30's threshold check).
TEST(MeshAlgorithms, RotateMatchesBevyNormalAndTangentTransform) {
    const auto positions_of = [](const mesh::Mesh& mesh) {
        return *mesh.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    };
    const auto normals_of = [](const mesh::Mesh& mesh) {
        return *mesh.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    };
    const auto tangents_of = [](const mesh::Mesh& mesh) {
        return *mesh.attribute(mesh::Mesh::ATTRIBUTE_TANGENT)->get().get_if<mesh::VertexAttributeValues::Float32x4>();
    };

    // A half turn about Z, which is unambiguous across handedness conventions: x and y negate and z
    // is unchanged.
    mesh::Mesh half_turn(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    half_turn.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 2.0f, 3.0f}});
    half_turn.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::vector<glm::vec3>{{1.0f, 0.0f, 0.0f}});
    half_turn.insert_attribute(mesh::Mesh::ATTRIBUTE_TANGENT, std::vector<glm::vec4>{{0.0f, 3.0f, 0.0f, -1.0f}});

    const glm::quat rotation = glm::angleAxis(glm::pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
    const mesh::Mesh rotated = std::move(half_turn).rotated_by(rotation);
    // The half turn is not exactly representable as a quaternion, so the results carry the usual
    // float residue around 1e-7 and are compared component-wise.
    const auto expect_near3 = [](const mesh::VertexAttributeValues::Float32x3Value& actual, const glm::vec3& expected) {
        for (glm::length_t index = 0; index < 3; ++index) EXPECT_NEAR(actual[index], expected[index], 1e-6f);
    };
    const auto expect_near4 = [](const mesh::VertexAttributeValues::Float32x4Value& actual, const glm::vec4& expected) {
        for (glm::length_t index = 0; index < 4; ++index) EXPECT_NEAR(actual[index], expected[index], 1e-6f);
    };

    const auto rotated_positions = positions_of(rotated);
    ASSERT_EQ(rotated_positions.size(), 1u);
    expect_near3(rotated_positions[0], glm::vec3{-1.0f, -2.0f, 3.0f});
    const auto rotated_normals = normals_of(rotated);
    ASSERT_EQ(rotated_normals.size(), 1u);
    expect_near3(rotated_normals[0], glm::vec3{-1.0f, 0.0f, 0.0f});
    // The tangent is normalized before it is rotated, and keeps its handedness.
    const auto rotated_tangents = tangents_of(rotated);
    ASSERT_EQ(rotated_tangents.size(), 1u);
    expect_near4(rotated_tangents[0], glm::vec4{0.0f, -1.0f, 0.0f, -1.0f});

    // A near-identity rotation leaves normals and tangents untouched, even when they are not unit
    // length, because only the position pass runs.
    mesh::Mesh near_identity(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    near_identity.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 0.0f, 0.0f}});
    near_identity.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::vector<glm::vec3>{{0.0f, 0.0f, 5.0f}});
    near_identity.insert_attribute(mesh::Mesh::ATTRIBUTE_TANGENT, std::vector<glm::vec4>{{0.0f, 0.0f, 4.0f, 1.0f}});
    ASSERT_TRUE(near_identity.try_rotate_by(glm::angleAxis(1e-7f, glm::vec3{0.0f, 0.0f, 1.0f})).has_value());
    EXPECT_EQ(normals_of(near_identity),
              (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 5.0f}}));
    EXPECT_EQ(tangents_of(near_identity),
              (std::vector<mesh::VertexAttributeValues::Float32x4Value>{{0.0f, 0.0f, 4.0f, 1.0f}}));

    // The extraction guard is the same as for the other in-place families.
    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto inaccessible = extracted.try_rotate_by(rotation);
    ASSERT_FALSE(inaccessible.has_value());
    EXPECT_EQ(inaccessible.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_THROW(extracted.rotate_by(rotation), std::logic_error);
}

// Bevy's own `transform_mesh` test (`mesh.rs:2542-2591`): positions are scaled, rotated and
// translated; normals go through `scale_normal`; UVs are untouched.
TEST(MeshAlgorithms, TransformMatchesBevyWholeTransformCase) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                           std::vector<glm::vec3>{{-1.0f, -1.0f, 2.0f}, {1.0f, -1.0f, 2.0f}, {0.0f, 1.0f, 2.0f}});
    value.insert_attribute(
        mesh::Mesh::ATTRIBUTE_NORMAL,
        std::vector<glm::vec3>{glm::normalize(glm::vec3{-1.0f, -1.0f, 1.0f}),
                               glm::normalize(glm::vec3{1.0f, -1.0f, 1.0f}), glm::vec3{0.0f, 0.0f, 1.0f}});
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_UV_0,
                           std::vector<glm::vec2>{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}});

    epix::transform::Transform transform;
    transform.translation = glm::vec3{-2.0f};
    transform.scaler      = glm::vec3{2.0f, 0.0f, -1.0f};

    const mesh::Mesh transformed = std::move(value).transformed_by(transform);
    const auto* positions =
        transformed.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {-4.0f, -2.0f, -4.0f}, {0.0f, -2.0f, -4.0f}, {-2.0f, -2.0f, -4.0f}}));
    const auto* normals =
        transformed.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(normals, nullptr);
    EXPECT_EQ(*normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                            {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}));
    const auto* uvs =
        transformed.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ(*uvs,
              (std::vector<mesh::VertexAttributeValues::Float32x2Value>{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}}));

    // The in-place and fallible forms agree, and the extraction guard still applies.
    mesh::Mesh in_place(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    in_place.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 2.0f, 3.0f}});
    ASSERT_TRUE(in_place.try_transform_by(transform).has_value());
    EXPECT_EQ(
        *in_place.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>(),
        (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, -2.0f, -5.0f}}));

    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto inaccessible = std::move(extracted).try_transformed_by(transform);
    ASSERT_FALSE(inaccessible.has_value());
    EXPECT_EQ(inaccessible.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_THROW(std::move(extracted).transformed_by(transform), std::logic_error);
}

TEST(MeshAlgorithms, InvertWindingMatchesEveryBevyTopologyAndError) {
    mesh::Mesh triangles(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    triangles.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 2, 3, 0}});
    ASSERT_TRUE(triangles.invert_winding().has_value());
    EXPECT_EQ(*triangles.indices()->get().as_u16(), (std::vector<std::uint16_t>{0, 2, 1, 2, 0, 3}));

    mesh::Mesh lines(wgpu::PrimitiveTopology::eLineList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    lines.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 3}});
    ASSERT_TRUE(lines.invert_winding().has_value());
    EXPECT_EQ(*lines.indices()->get().as_u32(), (std::vector<std::uint32_t>{3, 2, 1, 0}));

    mesh::Mesh strip(wgpu::PrimitiveTopology::eTriangleStrip, epix::assets::RenderAssetUsages::MAIN_WORLD);
    strip.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 3}});
    auto inverted_strip = std::move(strip).with_inverted_winding();
    ASSERT_TRUE(inverted_strip.has_value());
    EXPECT_EQ(*inverted_strip->indices()->get().as_u32(), (std::vector<std::uint32_t>{3, 2, 1, 0}));

    mesh::Mesh abrupt(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    abrupt.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 3}});
    const auto abrupt_result = abrupt.invert_winding();
    ASSERT_FALSE(abrupt_result.has_value());
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_winding_invert_error::AbruptIndicesEnd>(abrupt_result.error()));
    EXPECT_EQ(*abrupt.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 3}));

    mesh::Mesh points(wgpu::PrimitiveTopology::ePointList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    points.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0}});
    const auto point_result = points.invert_winding();
    ASSERT_FALSE(point_result.has_value());
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_winding_invert_error::WrongTopology>(point_result.error()));

    mesh::Mesh unindexed_points(wgpu::PrimitiveTopology::ePointList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    EXPECT_TRUE(unindexed_points.invert_winding().has_value());

    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto extracted_result = extracted.invert_winding();
    ASSERT_FALSE(extracted_result.has_value());
    ASSERT_TRUE(std::holds_alternative<mesh::MeshAccessError>(extracted_result.error()));
    EXPECT_EQ(std::get<mesh::MeshAccessError>(extracted_result.error()), mesh::MeshAccessError::ExtractedToRenderWorld);
}

TEST(MeshAlgorithms, TrianglesMatchesBevyLazyListStripAndErrorBehavior) {
    static_assert(std::ranges::view<mesh::MeshTriangles>);
    static_assert(std::ranges::input_range<mesh::MeshTriangles>);

    const std::vector<std::array<float, 3>> positions{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},  {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
    };

    mesh::Mesh list(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    list.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    list.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 2, 3, 0, 5}});
    auto list_result = list.triangles();
    ASSERT_TRUE(list_result.has_value());
    const auto list_triangles = std::ranges::to<std::vector<mesh::Triangle3d>>(*list_result);
    ASSERT_EQ(list_triangles.size(), 2u);
    EXPECT_EQ(list_triangles[0].vertices,
              (std::array{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)}));
    EXPECT_EQ(list_triangles[1].vertices,
              (std::array{glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}));

    mesh::Mesh strip(wgpu::PrimitiveTopology::eTriangleStrip, epix::assets::RenderAssetUsages::MAIN_WORLD);
    strip.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    strip.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}});
    auto strip_result = strip.triangles();
    ASSERT_TRUE(strip_result.has_value());
    const auto strip_triangles = std::ranges::to<std::vector<mesh::Triangle3d>>(*strip_result);
    ASSERT_EQ(strip_triangles.size(), 4u);
    EXPECT_EQ(strip_triangles[0].vertices,
              (std::array{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)}));
    EXPECT_EQ(strip_triangles[1].vertices,
              (std::array{glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}));
    EXPECT_EQ(strip_triangles[2].vertices,
              (std::array{glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f)}));
    EXPECT_EQ(strip_triangles[3].vertices,
              (std::array{glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)}));

    mesh::Mesh invalid(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    invalid.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    invalid.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 0, 99, 2, 2, 3, 0}});
    auto invalid_result = invalid.triangles();
    ASSERT_TRUE(invalid_result.has_value());
    EXPECT_EQ(std::ranges::distance(*invalid_result), 2);

    mesh::Mesh missing_position(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    missing_position.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    const auto missing_position_result = missing_position.triangles();
    ASSERT_FALSE(missing_position_result.has_value());
    ASSERT_TRUE(std::holds_alternative<mesh::MeshAccessError>(missing_position_result.error()));
    EXPECT_EQ(std::get<mesh::MeshAccessError>(missing_position_result.error()), mesh::MeshAccessError::NotFound);

    mesh::Mesh missing_indices(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    missing_indices.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    const auto missing_indices_result = missing_indices.triangles();
    ASSERT_FALSE(missing_indices_result.has_value());
    ASSERT_TRUE(std::holds_alternative<mesh::MeshAccessError>(missing_indices_result.error()));
    EXPECT_EQ(std::get<mesh::MeshAccessError>(missing_indices_result.error()), mesh::MeshAccessError::NotFound);

    mesh::Mesh wrong_format(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    wrong_format.insert_attribute(
        mesh::MeshVertexAttribute{"Wrong_Position", mesh::Mesh::ATTRIBUTE_POSITION.id, wgpu::VertexFormat::eFloat32x2},
        std::vector{glm::vec2(0.0f), glm::vec2(1.0f), glm::vec2(2.0f)});
    wrong_format.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    const auto wrong_format_result = wrong_format.triangles();
    ASSERT_FALSE(wrong_format_result.has_value());
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_triangles_error::PositionsFormat>(wrong_format_result.error()));

    mesh::Mesh wrong_topology(wgpu::PrimitiveTopology::eLineList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    wrong_topology.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    wrong_topology.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    const auto wrong_topology_result = wrong_topology.triangles();
    ASSERT_FALSE(wrong_topology_result.has_value());
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_triangles_error::WrongTopology>(wrong_topology_result.error()));

    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    extracted.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());
    const auto extracted_result = extracted.triangles();
    ASSERT_FALSE(extracted_result.has_value());
    ASSERT_TRUE(std::holds_alternative<mesh::MeshAccessError>(extracted_result.error()));
    EXPECT_EQ(std::get<mesh::MeshAccessError>(extracted_result.error()), mesh::MeshAccessError::ExtractedToRenderWorld);
}

TEST(Indices, PushAndExtendPromoteU16StorageWithoutLosingValues) {
    mesh::Indices indices{std::vector<std::uint16_t>{}};
    static_assert(std::ranges::view<decltype(std::declval<const mesh::Indices&>().iter())>);

    indices.push(10);
    EXPECT_EQ(static_cast<wgpu::IndexFormat>(indices), wgpu::IndexFormat::eUint16);
    EXPECT_EQ(std::ranges::to<std::vector<std::size_t>>(indices.iter()), (std::vector<std::size_t>{10}));

    indices.extend(std::array<std::uint32_t, 3>{11, 0x10012, 0x10013});
    EXPECT_EQ(static_cast<wgpu::IndexFormat>(indices), wgpu::IndexFormat::eUint32);
    EXPECT_EQ(std::ranges::to<std::vector<std::size_t>>(indices.iter()),
              (std::vector<std::size_t>{10, 11, 0x10012, 0x10013}));

    indices.push(20);
    EXPECT_EQ(indices.len(), 5u);
    EXPECT_FALSE(indices.is_empty());
    ASSERT_NE(indices.as_u32(), nullptr);
    EXPECT_EQ(*indices.as_u32(), (std::vector<std::uint32_t>{10, 11, 0x10012, 0x10013, 20}));
}

TEST(Indices, MeshExposesExactIndexBytes) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    value.insert_indices(mesh::Indices{std::vector<std::uint16_t>{1, 0x203, 4}});
    const auto bytes = value.get_index_buffer_bytes();
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(bytes->size(), 3u * sizeof(std::uint16_t));
    std::uint16_t middle = 0;
    std::memcpy(&middle, bytes->data() + sizeof(std::uint16_t), sizeof(middle));
    EXPECT_EQ(middle, 0x203u);
}

TEST(MeshComponents, Mesh2dAndMesh3dMatchBevyNewtypeContracts) {
    const auto id = epix::assets::AssetId<mesh::Mesh>::invalid();
    const epix::assets::Handle<mesh::Mesh> handle{id};
    mesh::Mesh2d mesh_2d{handle};
    mesh::Mesh3d mesh_3d{handle};

    EXPECT_EQ(mesh_2d.handle, handle);
    EXPECT_EQ(mesh_3d.handle, handle);
    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_2d), id);
    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_3d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh2d>::as_asset_id(mesh_2d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh3d>::as_asset_id(mesh_3d), id);
    EXPECT_EQ((*mesh_2d).id(), id);
    EXPECT_EQ((*mesh_3d).id(), id);
    EXPECT_EQ(mesh::Mesh2d{}.handle.id(), epix::assets::AssetId<mesh::Mesh>::invalid());
    EXPECT_EQ(mesh::Mesh3d{}.handle.id(), epix::assets::AssetId<mesh::Mesh>::invalid());

    auto app             = epix::app::App::create();
    const auto entity_2d = app.world_mut().spawn(mesh_2d).id();
    const auto entity_3d = app.world_mut().spawn(mesh_3d).id();
    EXPECT_TRUE(app.world().entity(entity_2d).contains<epix::transform::Transform>());
    EXPECT_TRUE(app.world().entity(entity_3d).contains<epix::transform::Transform>());

    EXPECT_EQ(mesh::MeshTag{}.value, 0u);
    EXPECT_EQ(mesh::MeshTag{.value = 17}, mesh::MeshTag{.value = 17});
}

// Bevy v0.18 `skinning.rs`: inverse bind poses are an asset-backed matrix slice, while
// `SkinnedMesh` is a component containing that asset handle and an ordered joint list.
TEST(MeshSkinning, ComponentAssetAndPluginRegistrationMatchBevy) {
    static_assert(epix::assets::AsAssetIdImpl<mesh::SkinnedMesh>);
    static_assert(!std::default_initializable<mesh::SkinnedMeshInverseBindposes>);

    const mesh::SkinnedMesh empty;
    EXPECT_EQ(empty.inverse_bindposes.id(), epix::assets::AssetId<mesh::SkinnedMeshInverseBindposes>::invalid());
    EXPECT_TRUE(empty.joints.empty());

    auto app          = epix::app::App::create();
    auto asset_plugin = epix::assets::AssetPlugin{};
    asset_plugin.mode = epix::assets::AssetServerMode::Unprocessed;
    app.add_plugins(std::move(asset_plugin));
    app.add_plugins(mesh::MeshPlugin{});

    const std::vector matrices{glm::mat4{1.0f}, glm::mat4{2.0f}};
    const auto handle = app.world_mut().resource_mut<epix::assets::Assets<mesh::SkinnedMeshInverseBindposes>>().emplace(
        mesh::SkinnedMeshInverseBindposes{matrices});
    const mesh::SkinnedMesh skinned{handle, {epix::ecs::Entity::from_index(3), epix::ecs::Entity::from_parts(7, 2)}};
    EXPECT_EQ(epix::assets::AsAssetId<mesh::SkinnedMesh>::as_asset_id(skinned), handle.id());
    EXPECT_EQ(skinned.joints, (std::vector{epix::ecs::Entity::from_index(3), epix::ecs::Entity::from_parts(7, 2)}));

    const auto stored =
        app.world().resource<epix::assets::Assets<mesh::SkinnedMeshInverseBindposes>>().get(handle.id());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->get().size(), 2u);
    EXPECT_EQ(stored->get().matrices().size(), 2u);
    EXPECT_FALSE(stored->get().empty());
    EXPECT_EQ(std::ranges::distance(stored->get()), 2);
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            EXPECT_FLOAT_EQ(stored->get()[1][column][row], matrices[1][column][row]);
        }
    }
}

// Bevy v0.18 `mesh.rs`: SerializedMesh is a lossy process-transfer representation
// whose default deserializer accepts the eight built-ins and explicitly registered custom attributes.
TEST(MeshSerialization, RoundTripPreservesBevyTransferFieldsAndResetsLossyState) {
    static_assert(std::copy_constructible<mesh::SerializedMesh>);

    constexpr auto default_usage = static_cast<epix::assets::RenderAssetUsages>(
        epix::assets::RenderAssetUsages::MAIN_WORLD | epix::assets::RenderAssetUsages::RENDER_WORLD);
    mesh::Mesh source{wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD};
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                            std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                                {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    source.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}});
    source.final_aabb = mesh::Aabb3d::from_min_max(glm::vec3{-1.0f}, glm::vec3{2.0f});

    auto encoded = mesh::serialize_serialized_mesh(mesh::SerializedMesh::from_mesh(source));
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(encoded->empty());

    auto decoded = mesh::deserialize_serialized_mesh(*encoded);
    ASSERT_TRUE(decoded.has_value());
    mesh::Mesh restored = std::move(*decoded).into_mesh();

    mesh::Mesh expected{wgpu::PrimitiveTopology::eTriangleList, default_usage};
    expected.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                              std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                                  {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    expected.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}});

    EXPECT_EQ(restored, expected);
    EXPECT_EQ(restored.asset_usage, default_usage);
    EXPECT_FALSE(restored.final_aabb.has_value());
    EXPECT_NE(restored, source);

    encoded->pop_back();
    EXPECT_FALSE(mesh::deserialize_serialized_mesh(*encoded).has_value());
}

TEST(MeshSerialization, CustomAttributeWhitelistMatchesBevyNameAndIdRules) {
    constexpr auto usage = static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::MAIN_WORLD |
                                                                        epix::assets::RenderAssetUsages::RENDER_WORLD);
    const mesh::MeshVertexAttribute custom{"Transfer_Custom", mesh::MeshVertexAttributeId{42},
                                           wgpu::VertexFormat::eSnorm8x4};

    mesh::Mesh source{wgpu::PrimitiveTopology::ePointList, usage};
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                            std::vector<mesh::VertexAttributeValues::Float32x3Value>{{1.0f, 2.0f, 3.0f}});
    source.insert_attribute(
        custom, mesh::VertexAttributeValues::Snorm8x4{{mesh::VertexAttributeValues::Sint8x4Value{1, -2, 3, -4}}});

    const auto transfer = [&source] {
        auto encoded = mesh::serialize_serialized_mesh(mesh::SerializedMesh::from_mesh(source));
        EXPECT_TRUE(encoded.has_value());
        auto decoded = mesh::deserialize_serialized_mesh(encoded.value());
        EXPECT_TRUE(decoded.has_value());
        return std::move(decoded).value();
    };

    mesh::Mesh default_restored = transfer().into_mesh();
    EXPECT_TRUE(default_restored.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_FALSE(default_restored.contains_attribute(custom));
    EXPECT_FALSE(default_restored.indices().has_value());

    mesh::MeshDeserializer deserializer;
    deserializer.add_custom_vertex_attribute(custom.name, custom);
    mesh::Mesh restored = deserializer.deserialize(transfer());
    ASSERT_TRUE(restored.contains_attribute(custom));
    EXPECT_EQ(restored.attribute(custom)->get(), (mesh::VertexAttributeValues{mesh::VertexAttributeValues::Snorm8x4{
                                                     {mesh::VertexAttributeValues::Sint8x4Value{1, -2, 3, -4}}}}));

    mesh::MeshDeserializer wrong_id;
    wrong_id.add_custom_vertex_attribute(
        custom.name, mesh::MeshVertexAttribute{custom.name, mesh::MeshVertexAttributeId{43}, custom.format});
    EXPECT_FALSE(wrong_id.deserialize(transfer()).contains_attribute(custom));

    // Bevy validates the serialized name and ID, then installs the registered descriptor;
    // it intentionally does not compare the serialized format.
    const mesh::MeshVertexAttribute remapped_format{custom.name, custom.id, wgpu::VertexFormat::eSint8x4};
    mesh::MeshDeserializer format_remapper;
    format_remapper.add_custom_vertex_attribute(custom.name, remapped_format);
    mesh::Mesh format_restored = format_remapper.deserialize(transfer());
    const auto attributes      = format_restored.attributes();
    const auto found = std::ranges::find_if(attributes, [&](const auto& entry) { return entry.first.id == custom.id; });
    ASSERT_NE(found, std::ranges::end(attributes));
    EXPECT_EQ((*found).first, remapped_format);
    EXPECT_EQ((*found).second.format(), wgpu::VertexFormat::eSnorm8x4);
}

TEST(MeshSerialization, FromMeshRejectsRenderWorldExtractedPayload) {
    constexpr auto usage = static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::MAIN_WORLD |
                                                                        epix::assets::RenderAssetUsages::RENDER_WORLD);
    mesh::Mesh source{wgpu::PrimitiveTopology::eTriangleList, usage};
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                            std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 0.0f}});
    ASSERT_TRUE(source.take_gpu_data().has_value());

    EXPECT_THROW(mesh::SerializedMesh::from_mesh(std::move(source)), std::logic_error);
}

TEST(MorphTargetImage, PacksBevyR32FloatTextureLayers) {
    constexpr auto usage = static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::MAIN_WORLD |
                                                                        epix::assets::RenderAssetUsages::RENDER_WORLD);
    const std::array targets{
        std::array{mesh::MorphAttributes{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f, 9.0f}},
                   mesh::MorphAttributes{{10.0f, 11.0f, 12.0f}, {13.0f, 14.0f, 15.0f}, {16.0f, 17.0f, 18.0f}}},
        std::array{
            mesh::MorphAttributes{{-1.0f, -2.0f, -3.0f}, {-4.0f, -5.0f, -6.0f}, {-7.0f, -8.0f, -9.0f}},
            mesh::MorphAttributes{{-10.0f, -11.0f, -12.0f}, {-13.0f, -14.0f, -15.0f}, {-16.0f, -17.0f, -18.0f}}}};

    auto built = mesh::MorphTargetImage::create(targets, 2, usage);
    ASSERT_TRUE(built.has_value());
    const auto& image = built->image;
    EXPECT_EQ(image.texture_descriptor.dimension, wgpu::TextureDimension::e3D);
    EXPECT_EQ(image.texture_descriptor.format, wgpu::TextureFormat::eR32Float);
    EXPECT_EQ(image.texture_descriptor.size.width, 1u);
    EXPECT_EQ(image.texture_descriptor.size.height, 18u);
    EXPECT_EQ(image.texture_descriptor.size.depthOrArrayLayers, 2u);
    EXPECT_EQ(image.usage(), usage);

    std::vector<float> packed(image.raw_view().size() / sizeof(float));
    std::memcpy(packed.data(), image.raw_view().data(), image.raw_view().size());
    const std::vector<float> expected{1,  2,  3,  4,  5,  6,  7,  8,  9,  10,  11,  12,  13,  14,  15,  16,  17,  18,
                                      -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15, -16, -17, -18};
    EXPECT_EQ(packed, expected);
}

TEST(MorphTargetImage, PreservesBevyLimitsAndRectangleSearch) {
    std::vector<std::vector<mesh::MorphAttributes>> too_many_targets(mesh::MAX_MORPH_WEIGHTS + 1);
    auto target_error = mesh::MorphTargetImage::create(too_many_targets, 0, epix::assets::RenderAssetUsages{});
    ASSERT_FALSE(target_error.has_value());
    ASSERT_TRUE((std::holds_alternative<mesh::morph_build_error::TooManyTargets>(target_error.error())));
    EXPECT_EQ(std::get<mesh::morph_build_error::TooManyTargets>(target_error.error()).target_count, 257u);
    EXPECT_EQ(target_error.error().to_string(),
              "Bevy only supports up to 256 morph targets (individual poses), tried to create a model with 257 morph "
              "targets");

    std::array<std::array<mesh::MorphAttributes, 0>, 1> one_empty_target;
    auto attribute_error = mesh::MorphTargetImage::create(one_empty_target, 466034, epix::assets::RenderAssetUsages{});
    ASSERT_FALSE(attribute_error.has_value());
    ASSERT_TRUE((std::holds_alternative<mesh::morph_build_error::TooManyAttributes>(attribute_error.error())));
    const auto& too_many = std::get<mesh::morph_build_error::TooManyAttributes>(attribute_error.error());
    EXPECT_EQ(too_many.vertex_count, 466034u);
    EXPECT_EQ(too_many.component_count, 4194306u);
    EXPECT_EQ(attribute_error.error().to_string(),
              "Too many vertex×components in morph target, max is 4194304, got 466034×4194306 = 1954689202404");

    const auto zero = mesh::detail::lowest_morph_texture_2d(0, 2048);
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(*zero, (mesh::detail::MorphTextureRectangle{.width = 1, .height = 0, .padding = 0}));
    const auto prime = mesh::detail::lowest_morph_texture_2d(18477, 2048);
    ASSERT_TRUE(prime.has_value());
    EXPECT_GE(static_cast<std::uint64_t>(prime->width) * prime->height, 18477u);
    EXPECT_LE(prime->width, 2048u);
    EXPECT_LE(prime->height, 2048u);
    EXPECT_FALSE(mesh::detail::lowest_morph_texture_2d(2048u * 2048u + 1u, 2048).has_value());
}

TEST(MorphWeights, EnforcesBevyTargetLimitAndMutableSliceBehavior) {
    auto canonical = mesh::MorphWeights::create({0.25f, 0.75f});
    ASSERT_TRUE(canonical.has_value());
    ASSERT_EQ(canonical->weights().size(), 2u);
    canonical->weights_mut()[0] = 0.5f;
    EXPECT_EQ(canonical->weights()[0], 0.5f);
    EXPECT_FALSE(canonical->first_mesh().has_value());

    epix::assets::Handle<mesh::Mesh> mesh_handle{epix::assets::AssetId<mesh::Mesh>::invalid()};
    auto with_mesh = mesh::MorphWeights::create({1.0f}, mesh_handle);
    ASSERT_TRUE(with_mesh.has_value());
    const auto first_mesh = with_mesh->first_mesh();
    ASSERT_TRUE(first_mesh.has_value());
    EXPECT_EQ(first_mesh->get(), mesh_handle);

    auto primitive = mesh::MeshMorphWeights::create({0.1f, 0.2f});
    ASSERT_TRUE(primitive.has_value());
    primitive->clear_weights();
    primitive->extend_weights(std::array{0.3f, 0.4f, 0.5f});
    EXPECT_EQ(std::vector(primitive->weights().begin(), primitive->weights().end()), (std::vector{0.3f, 0.4f, 0.5f}));

    EXPECT_FALSE(mesh::MorphWeights::create(std::vector<float>(257), std::nullopt).has_value());
    EXPECT_FALSE(mesh::MeshMorphWeights::create(std::vector<float>(257)).has_value());
    EXPECT_TRUE(mesh::MorphWeights::create(std::vector<float>(256), std::nullopt).has_value());
    EXPECT_TRUE(mesh::MeshMorphWeights::create(std::vector<float>(256)).has_value());
}

TEST(MeshMorphTargets, AccessorsBuildersCopyAndExtractionMatchBevy) {
    constexpr auto usage = static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::MAIN_WORLD |
                                                                        epix::assets::RenderAssetUsages::RENDER_WORLD);
    epix::assets::Handle<epix::image::Image> target{epix::assets::AssetId<epix::image::Image>::invalid()};
    mesh::Mesh source{wgpu::PrimitiveTopology::eTriangleList, usage};
    EXPECT_FALSE(source.has_morph_targets());
    EXPECT_EQ(source.try_morph_targets().error(), mesh::MeshAccessError::NotFound);
    EXPECT_FALSE(source.morph_target_names().has_value());

    source = std::move(source).with_morph_targets(target).with_morph_target_names({"smile", "blink"});
    EXPECT_TRUE(source.has_morph_targets());
    ASSERT_TRUE(source.morph_targets().has_value());
    EXPECT_EQ(source.morph_targets()->get(), target);
    ASSERT_TRUE(source.morph_target_names().has_value());
    EXPECT_EQ(std::vector(source.morph_target_names()->begin(), source.morph_target_names()->end()),
              (std::vector<std::string>{"smile", "blink"}));

    const mesh::Mesh copied = source;
    EXPECT_EQ(copied, source);
    auto extracted = source.take_gpu_data();
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->morph_targets()->get(), target);
    EXPECT_EQ(extracted->morph_target_names()->front(), "smile");
    EXPECT_EQ(source.try_has_morph_targets().error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_EQ(source.try_morph_targets().error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_EQ(source.try_morph_target_names().error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_EQ(source.try_set_morph_targets(target).error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_EQ(source.try_set_morph_target_names({"other"}).error(), mesh::MeshAccessError::ExtractedToRenderWorld);

    const mesh::Mesh restored = mesh::SerializedMesh::from_mesh(copied).into_mesh();
    EXPECT_FALSE(restored.has_morph_targets());
    EXPECT_FALSE(restored.morph_target_names().has_value());
}

TEST(MeshPlugin, ModifiedMeshAssetMarksOnlyReferencingMesh3dChanged) {
    auto app          = epix::app::App::create();
    auto asset_plugin = epix::assets::AssetPlugin{};
    asset_plugin.mode = epix::assets::AssetServerMode::Unprocessed;
    app.add_plugins(std::move(asset_plugin));
    app.add_plugins(mesh::MeshPlugin{});

    auto first = app.world_mut().resource_mut<epix::assets::Assets<mesh::Mesh>>().emplace(mesh::make_box2d(2.0f, 2.0f));
    auto second =
        app.world_mut().resource_mut<epix::assets::Assets<mesh::Mesh>>().emplace(mesh::make_box2d(4.0f, 4.0f));
    app.world_mut().spawn(mesh::Mesh3d{first});
    app.world_mut().spawn(mesh::Mesh3d{second});

    std::size_t modified_count = 0;
    app.add_systems(epix::app::PostUpdate,
                    epix::ecs::into([&modified_count](
                                        epix::ecs::Query<epix::ecs::Item<const mesh::Mesh3d&>,
                                                         epix::ecs::Filter<epix::ecs::Modified<mesh::Mesh3d>>> meshes) {
                        modified_count += std::ranges::distance(meshes.iter());
                    }).after(mesh::mark_3d_meshes_as_changed_if_their_assets_changed));

    app.update();
    EXPECT_EQ(modified_count, 2u);
    app.update();
    EXPECT_EQ(modified_count, 2u);

    auto stored = app.world_mut().resource_mut<epix::assets::Assets<mesh::Mesh>>().get_mut(first.id());
    ASSERT_TRUE(stored.has_value());
    stored->get().insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, std::array{glm::vec4(1.0f)});
    app.update();
    EXPECT_EQ(modified_count, 3u);
}

TEST(MeshModule, BuiltInVertexAttributeIdsMatchBevy) {
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_POSITION.id.value, 0u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_NORMAL.id.value, 1u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_UV_0.id.value, 2u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_UV_1.id.value, 3u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_TANGENT.id.value, 4u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_COLOR.id.value, 5u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT.id.value, 6u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_JOINT_INDEX.id.value, 7u);
    EXPECT_EQ(mesh::Mesh::FIRST_AVAILABLE_CUSTOM_ATTRIBUTE, 8u);
}

TEST(MeshModule, BaseMeshPipelineKeyEncodesPrimitiveTopologyInHighBits) {
    constexpr auto line = mesh::BaseMeshPipelineKey::from_primitive_topology(wgpu::PrimitiveTopology::eLineList);
    static_assert(line.primitive_topology() == wgpu::PrimitiveTopology::eLineList);
    EXPECT_EQ(line.bits() >> mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_SHIFT_BITS,
              static_cast<std::uint64_t>(wgpu::PrimitiveTopology::eLineList));
    EXPECT_TRUE((line | mesh::BaseMeshPipelineKey::MORPH_TARGETS).contains(line));
}

TEST(MeshModule, MeshUsageAndCloneMatchBevyContract) {
    constexpr auto both_worlds = static_cast<epix::assets::RenderAssetUsages>(
        epix::assets::RenderAssetUsages::MAIN_WORLD | epix::assets::RenderAssetUsages::RENDER_WORLD);
    auto source = mesh::make_box2d(20.0f, 10.0f);
    EXPECT_EQ(source.asset_usage, both_worlds);
    source.final_aabb = mesh::Aabb3d::from_min_max(glm::vec3{-2.0f}, glm::vec3{3.0f});

    mesh::Mesh clone(source);
    EXPECT_EQ(clone.asset_usage, source.asset_usage);
    EXPECT_EQ(clone.final_aabb, source.final_aabb);
    EXPECT_EQ(clone.count_vertices(), source.count_vertices());
    ASSERT_TRUE(clone.remove_attribute(mesh::Mesh::ATTRIBUTE_POSITION).has_value());
    EXPECT_FALSE(clone.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_TRUE(source.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
}

TEST(MeshModule, RejectsIncompatibleAttributeType) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    EXPECT_THROW(value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{glm::vec2(0.0f, 0.0f)}),
                 std::invalid_argument);

    // Equal byte width is not enough: Bevy checks semantic VertexFormat.
    try {
        value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, mesh::VertexAttributeValues::Sint32x3{{{1, 2, 3}}});
        FAIL() << "semantic format mismatch did not throw";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(error.what(),
                  std::string{"Failed to insert attribute. Invalid attribute format for Vertex_Position. Given "
                              "format is Sint32x3 but expected Float32x3"});
    }
    EXPECT_FALSE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
}

TEST(MeshModule, AttributesStoreAndReturnSemanticVertexValues) {
    constexpr mesh::MeshVertexAttribute custom{"Vertex_Custom_Snorm16x2", mesh::MeshVertexAttributeId{8},
                                               wgpu::VertexFormat::eSnorm16x2};
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(custom, mesh::VertexAttributeValues::Snorm16x2{{{1, -2}, {3, -4}}});

    auto stored = value.attribute(custom);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->get().format(), wgpu::VertexFormat::eSnorm16x2);
    ASSERT_NE(stored->get().get_if<mesh::VertexAttributeValues::Snorm16x2>(), nullptr);
    EXPECT_EQ(value.count_vertices(), 2u);

    auto mutable_stored = value.attribute_mut(custom);
    ASSERT_TRUE(mutable_stored.has_value());
    mutable_stored->get().get_if<mesh::VertexAttributeValues::Snorm16x2>()->front() = {5, -6};

    value.insert_indices(mesh::Indices{std::vector<std::uint16_t>{1, 0}});
    value.duplicate_vertices();

    auto removed = value.remove_attribute(custom);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->format(), wgpu::VertexFormat::eSnorm16x2);
    EXPECT_EQ(*removed->get_if<mesh::VertexAttributeValues::Snorm16x2>(),
              (std::vector<mesh::VertexAttributeValues::Sint16x2Value>{{3, -4}, {5, -6}}));
}

TEST(MeshModule, Box2dBuildsIndexedQuad) {
    auto value = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(value.count_vertices(), 4);
    ASSERT_TRUE(value.indices().has_value());
    EXPECT_EQ(value.indices()->get().len(), 6);
    EXPECT_TRUE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
}

TEST(MeshModule, CircleBuildsTriangleList) {
    auto value = mesh::make_circle(12.0f, std::nullopt, 16);
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(value.count_vertices(), 17);
    ASSERT_TRUE(value.indices().has_value());
    EXPECT_EQ(value.indices()->get().len(), 48);
}

TEST(MeshPrimitives, FoundationalTraitsDefaultsAndConversionsMatchBevy) {
    static_assert(mesh::MeshBuilder<mesh::CircleMeshBuilder>);
    static_assert(mesh::MeshBuilder<mesh::EllipseMeshBuilder>);
    static_assert(mesh::Meshable<mesh::Circle>);
    static_assert(mesh::Meshable<mesh::Ellipse>);
    static_assert(mesh::Meshable<mesh::RegularPolygon>);
    static_assert(mesh::Meshable<mesh::Rectangle>);

    EXPECT_FLOAT_EQ(mesh::Circle{}.radius, 0.5f);
    EXPECT_EQ(mesh::Ellipse{}.half_size, glm::vec2(1.0f, 0.5f));
    EXPECT_FLOAT_EQ(mesh::RegularPolygon{}.circumradius(), 0.5f);
    EXPECT_EQ(mesh::RegularPolygon{}.sides, 6u);
    EXPECT_EQ(mesh::Rectangle{}.half_size, glm::vec2(0.5f));
    EXPECT_THROW((mesh::RegularPolygon{-1.0f, 6}), std::invalid_argument);
    EXPECT_THROW((mesh::RegularPolygon{1.0f, 2}), std::invalid_argument);

    const mesh::Mesh from_builder = mesh::RectangleMeshBuilder{2.0f, 4.0f};
    const mesh::Mesh from_shape   = mesh::Circle{2.0f};
    EXPECT_EQ(from_builder.count_vertices(), 4u);
    EXPECT_EQ(from_shape.count_vertices(), 32u);
}

TEST(MeshPrimitives, EllipseAndCircleBuildersMatchBevyVertexContract) {
    const auto ellipse = mesh::EllipseMeshBuilder{2.0f, 1.0f, 4}.build();
    EXPECT_EQ(ellipse.count_vertices(), 4u);
    EXPECT_TRUE(ellipse.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    EXPECT_TRUE(ellipse.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));

    const auto* positions =
        ellipse.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        ellipse.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        ellipse.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    ASSERT_EQ(positions->size(), 4u);
    EXPECT_NEAR((*positions)[0][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[0][1], 1.0f, 1e-6f);
    EXPECT_NEAR((*positions)[1][0], -2.0f, 1e-6f);
    EXPECT_NEAR((*positions)[2][1], -1.0f, 1e-6f);
    EXPECT_EQ(*normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>(4, {0.0f, 0.0f, 1.0f})));
    EXPECT_NEAR((*uvs)[0][0], 0.5f, 1e-6f);
    EXPECT_NEAR((*uvs)[0][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*uvs)[1][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*uvs)[2][1], 1.0f, 1e-6f);

    ASSERT_TRUE(ellipse.indices().has_value());
    const auto* indices = ellipse.indices()->get().as_u32();
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(*indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));

    const auto circle = mesh::Circle{3.0f}.mesh().with_resolution(8).build();
    EXPECT_EQ(circle.count_vertices(), 8u);
    EXPECT_EQ(circle.indices()->get().len(), 18u);
}

TEST(MeshPrimitives, RegularPolygonAndRectangleBuildersMatchBevyTopologyAndUvs) {
    const auto polygon = mesh::RegularPolygon{2.0f, 6}.mesh().build();
    EXPECT_EQ(polygon.count_vertices(), 6u);
    ASSERT_TRUE(polygon.indices().has_value());
    EXPECT_EQ(polygon.indices()->get().len(), 12u);

    const auto rectangle = mesh::RectangleMeshBuilder{4.0f, 2.0f}.build();
    const auto* positions =
        rectangle.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        rectangle.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {2.0f, 1.0f, 0.0f},
                              {-2.0f, 1.0f, 0.0f},
                              {-2.0f, -1.0f, 0.0f},
                              {2.0f, -1.0f, 0.0f},
                          }));
    EXPECT_EQ(*uvs, (std::vector<mesh::VertexAttributeValues::Float32x2Value>{
                        {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}));
    ASSERT_TRUE(rectangle.indices().has_value());
    ASSERT_NE(rectangle.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*rectangle.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));

    EXPECT_EQ(mesh::Rectangle::from_size({8.0f, 6.0f}).half_size, glm::vec2(4.0f, 3.0f));
    EXPECT_EQ(mesh::Rectangle::from_corners({-2.0f, -4.0f}, {6.0f, 2.0f}).half_size, glm::vec2(4.0f, 3.0f));
}

TEST(MeshPrimitives, CircularSectorMatchesBevyArcFanAndMaskUvs) {
    static_assert(mesh::Meshable<mesh::CircularSector>);
    static_assert(mesh::MeshBuilder<mesh::CircularSectorMeshBuilder>);

    EXPECT_FLOAT_EQ(mesh::Arc2d{}.radius, 0.5f);
    EXPECT_FLOAT_EQ(mesh::Arc2d{}.half_angle, 2.0f * std::numbers::pi_v<float> / 3.0f);
    EXPECT_EQ(mesh::CircularMeshUvMode{}, mesh::CircularMeshUvMode{mesh::CircularMeshUvMode::Mask{.angle = 0.0f}});

    const auto sector =
        mesh::CircularSector::from_radians(2.0f, std::numbers::pi_v<float>).mesh().with_resolution(3).build();
    const auto* positions =
        sector.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        sector.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    ASSERT_EQ(positions->size(), 4u);
    EXPECT_EQ((*positions)[0], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 0.0f, 0.0f}));
    EXPECT_NEAR((*positions)[1][0], 2.0f, 1e-6f);
    EXPECT_NEAR((*positions)[1][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[2][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[2][1], 2.0f, 1e-6f);
    EXPECT_NEAR((*positions)[3][0], -2.0f, 1e-6f);
    EXPECT_NEAR((*positions)[3][1], 0.0f, 1e-6f);
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.5f, 0.5f}));
    EXPECT_NEAR((*uvs)[1][0], 1.0f, 1e-6f);
    EXPECT_NEAR((*uvs)[1][1], 0.5f, 1e-6f);
    EXPECT_NEAR((*uvs)[2][0], 0.5f, 1e-6f);
    EXPECT_NEAR((*uvs)[2][1], 0.0f, 1e-6f);
    ASSERT_TRUE(sector.indices().has_value());
    ASSERT_NE(sector.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*sector.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
}

TEST(MeshPrimitives, CircularSegmentMatchesBevyChordFanAndRotatedMaskUvs) {
    static_assert(mesh::Meshable<mesh::CircularSegment>);
    static_assert(mesh::MeshBuilder<mesh::CircularSegmentMeshBuilder>);

    const auto shape = mesh::CircularSegment::from_radians(2.0f, std::numbers::pi_v<float> * 0.5f);
    EXPECT_NEAR(shape.apothem(), std::numbers::sqrt2_v<float>, 1e-6f);
    EXPECT_EQ(shape.chord_midpoint(), glm::vec2(0.0f, shape.apothem()));

    const auto segment = shape.mesh()
                             .with_resolution(3)
                             .with_uv_mode(mesh::CircularMeshUvMode::Mask{
                                 .angle = std::numbers::pi_v<float> * 0.5f,
                             })
                             .build();
    const auto* positions =
        segment.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        segment.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    ASSERT_EQ(positions->size(), 4u);
    EXPECT_NEAR((*positions)[0][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[0][1], std::numbers::sqrt2_v<float>, 1e-6f);
    EXPECT_NEAR((*positions)[1][0], std::numbers::sqrt2_v<float>, 1e-6f);
    EXPECT_NEAR((*positions)[1][1], std::numbers::sqrt2_v<float>, 1e-6f);
    EXPECT_NEAR((*uvs)[0][0], 0.5f - std::numbers::sqrt2_v<float> * 0.25f, 1e-6f);
    EXPECT_NEAR((*uvs)[0][1], 0.5f, 1e-6f);
    EXPECT_NEAR((*uvs)[1][0], 0.5f - std::numbers::sqrt2_v<float> * 0.25f, 1e-6f);
    EXPECT_NEAR((*uvs)[1][1], 0.5f - std::numbers::sqrt2_v<float> * 0.25f, 1e-6f);
    ASSERT_TRUE(segment.indices().has_value());
    ASSERT_NE(segment.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*segment.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
}

TEST(MeshPrimitives, SegmentAndPolylineMatchBevyLineListData) {
    const auto segment = mesh::Segment2d::from_direction_and_length({3.0f, 4.0f}, 10.0f);
    EXPECT_EQ(segment.vertices, (std::array{glm::vec2(-3.0f, -4.0f), glm::vec2(3.0f, 4.0f)}));
    EXPECT_FLOAT_EQ(segment.length(), 10.0f);

    const mesh::Mesh segment_mesh = segment;
    EXPECT_EQ(segment_mesh.get_primitive_type(), wgpu::PrimitiveTopology::eLineList);
    const auto* segment_positions =
        segment_mesh.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(segment_positions, nullptr);
    EXPECT_EQ(*segment_positions,
              (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{-3.0f, -4.0f, 0.0f}, {3.0f, 4.0f, 0.0f}}));
    ASSERT_NE(segment_mesh.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*segment_mesh.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1}));

    const auto polyline = mesh::Polyline2d::with_subdivisions({0.0f, 0.0f}, {3.0f, 0.0f}, 2);
    EXPECT_EQ(polyline.vertices, (std::vector{glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(2.0f, 0.0f),
                                              glm::vec2(3.0f, 0.0f)}));
    const auto polyline_mesh = polyline.mesh().build();
    EXPECT_EQ(polyline_mesh.get_primitive_type(), wgpu::PrimitiveTopology::eLineList);
    EXPECT_FALSE(polyline_mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    ASSERT_NE(polyline_mesh.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*polyline_mesh.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 1, 2, 2, 3}));
}

TEST(MeshPrimitives, ConvexPolygonMatchesBevyValidationAndFanData) {
    auto polygon = mesh::ConvexPolygon::create({
        {-2.0f, -1.0f},
        {2.0f, -1.0f},
        {2.0f, 1.0f},
        {-2.0f, 1.0f},
    });
    ASSERT_TRUE(polygon.has_value());
    EXPECT_EQ(polygon->vertices().size(), 4u);

    const auto concave = mesh::ConvexPolygon::create({
        {-2.0f, -1.0f},
        {0.0f, 0.0f},
        {2.0f, -1.0f},
        {0.0f, 2.0f},
    });
    ASSERT_FALSE(concave.has_value());
    EXPECT_EQ(concave.error(), mesh::ConvexPolygonError::Concave);
    EXPECT_EQ(mesh::to_string(concave.error()), "The created polygon is not convex");

    const auto collinear = mesh::ConvexPolygon::create({
        {-1.0f, 0.0f},
        {0.0f, 0.0f},
        {1.0f, 0.0f},
    });
    ASSERT_TRUE(collinear.has_value());

    const auto value = polygon->mesh().build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_FALSE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    EXPECT_FALSE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    EXPECT_FALSE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {-2.0f, -1.0f, 0.0f}, {2.0f, -1.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {-2.0f, 1.0f, 0.0f}}));
    ASSERT_TRUE(value.indices().has_value());
    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*value.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));

    const auto pentagon      = mesh::ConvexPolygon::new_unchecked({
        {0.0f, 110.0f},
        {-105.0f, 35.0f},
        {-65.0f, -90.0f},
        {65.0f, -90.0f},
        {105.0f, 35.0f},
    });
    const auto pentagon_mesh = pentagon.mesh().build();
    ASSERT_NE(pentagon_mesh.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*pentagon_mesh.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 0, 3, 4}));
    EXPECT_EQ(pentagon_mesh.count_vertices(), 5);
}

TEST(MeshPrimitives, ConvexPolygonRejectsDegenerateVertexCountsWithoutPanicking) {
    EXPECT_FALSE(mesh::ConvexPolygon::create({}).has_value());
    EXPECT_FALSE(mesh::ConvexPolygon::create({{0.0f, 0.0f}}).has_value());
    EXPECT_FALSE(mesh::ConvexPolygon::create({{0.0f, 0.0f}, {1.0f, 1.0f}}).has_value());
    EXPECT_EQ(mesh::ConvexPolygon::create({{0.0f, 0.0f}, {1.0f, 1.0f}}).error(), mesh::ConvexPolygonError::Concave);

    const auto single = mesh::ConvexPolygon::new_unchecked({{1.0f, 2.0f}}).mesh().build();
    EXPECT_EQ(single.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_FALSE(single.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    EXPECT_FALSE(single.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    const auto* positions =
        single.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{1.0f, 2.0f, 0.0f}}));
    ASSERT_TRUE(single.indices().has_value());
    EXPECT_TRUE(single.indices()->get().as_u32()->empty());
}

TEST(MeshPrimitives, LineListPrimitivesEmitOnlyBevyPositionAttribute) {
    static_assert(mesh::Meshable<mesh::Segment2d>);
    static_assert(mesh::Meshable<mesh::Polyline2d>);

    const mesh::Mesh segment = mesh::Segment2d{};
    EXPECT_TRUE(segment.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_FALSE(segment.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    EXPECT_FALSE(segment.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    EXPECT_EQ(segment.get_primitive_type(), wgpu::PrimitiveTopology::eLineList);

    const auto polyline_mesh = mesh::Polyline2d{}.mesh().build();
    EXPECT_TRUE(polyline_mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_FALSE(polyline_mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    EXPECT_FALSE(polyline_mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    EXPECT_EQ(polyline_mesh.get_primitive_type(), wgpu::PrimitiveTopology::eLineList);
    ASSERT_NE(polyline_mesh.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*polyline_mesh.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1}));

    const auto subdivided = mesh::Polyline2d::with_subdivisions({0.0f, 0.0f}, {4.0f, 0.0f}, 0);
    EXPECT_EQ(subdivided.vertices, (std::vector{glm::vec2(0.0f, 0.0f), glm::vec2(4.0f, 0.0f)}));
    EXPECT_EQ(*subdivided.mesh().build().indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1}));
}

TEST(MeshPrimitives, Triangle2dMatchesBevyShapeMeasurementAndWinding) {
    static_assert(mesh::Meshable<mesh::Triangle2d>);
    static_assert(mesh::MeshBuilder<mesh::Triangle2dMeshBuilder>);

    const mesh::Triangle2d default_triangle{};
    EXPECT_EQ(default_triangle.vertices,
              (std::array{glm::vec2(0.0f, 0.5f), glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f)}));
    EXPECT_EQ(default_triangle.winding_order(), mesh::WindingOrder::CounterClockwise);

    const mesh::Triangle2d clockwise{{-1.0f, 0.0f}, {0.0f, 2.0f}, {1.0f, 0.0f}};
    EXPECT_EQ(clockwise.winding_order(), mesh::WindingOrder::Clockwise);
    EXPECT_FLOAT_EQ(mesh::area(clockwise), 2.0f);
    EXPECT_NEAR(mesh::perimeter(clockwise), 2.0f + 2.0f * std::sqrt(5.0f), 1e-5f);
    EXPECT_TRUE(clockwise.is_acute());
    EXPECT_FALSE(clockwise.is_obtuse());
    // The angle at the origin is obtuse (ab . ac = -3), so squared sides 9, 17, 2 satisfy 17 > 9 + 2.
    const mesh::Triangle2d obtuse{{0.0f, 0.0f}, {3.0f, 0.0f}, {-1.0f, 1.0f}};
    EXPECT_TRUE(obtuse.is_obtuse());
    EXPECT_FALSE(obtuse.is_acute());
    EXPECT_FALSE(clockwise.is_degenerate());

    // Collinear vertices fall inside the `��f32::EPSILON` dead band and classify as Invalid.
    const mesh::Triangle2d collinear{{0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.0f}};
    EXPECT_EQ(collinear.winding_order(), mesh::WindingOrder::Invalid);
    EXPECT_TRUE(collinear.is_degenerate());
    EXPECT_FLOAT_EQ(mesh::area(collinear), 0.0f);

    EXPECT_EQ(clockwise.reversed().winding_order(), mesh::WindingOrder::CounterClockwise);
    mesh::Triangle2d mutable_triangle = clockwise;
    mutable_triangle.reverse();
    EXPECT_EQ(mutable_triangle.vertices,
              (std::array{glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 2.0f), glm::vec2(-1.0f, 0.0f)}));
}

TEST(MeshPrimitives, Triangle2dMeshBuilderMatchesBevyUvAndIndexWinding) {
    // Bevy keeps the authored vertex order and only flips the index winding.
    const auto clockwise = mesh::Triangle2d{{-1.0f, 0.0f}, {0.0f, 2.0f}, {1.0f, 0.0f}}.mesh().build();
    EXPECT_EQ(clockwise.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        clockwise.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        clockwise.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {-1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}));
    EXPECT_EQ(*normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>(3, {0.0f, 0.0f, 1.0f})));
    ASSERT_NE(clockwise.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*clockwise.indices()->get().as_u32(), (std::vector<std::uint32_t>{2, 1, 0}));

    const auto counter_clockwise = mesh::Triangle2d{{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 2.0f}}.mesh().build();
    ASSERT_NE(counter_clockwise.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*counter_clockwise.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2}));

    // Unskewed UV projection: the acute case keeps `a` at 0, `b` at 1, and shifts `c` by the
    // projection of `ac` onto `ab`.
    const auto acute = mesh::Triangle2d{{0.0f, 0.0f}, {4.0f, 0.0f}, {1.0f, 3.0f}}.mesh().build();
    const auto* uvs =
        acute.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ(*uvs,
              (std::vector<mesh::VertexAttributeValues::Float32x2Value>{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.25f, 1.0f}}));

    // Degenerate triangles take Bevy's fixed fallback UVs instead of dividing by zero.
    const auto degenerate = mesh::Triangle2d{{1.0f, 1.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}}.mesh().build();
    const auto* degenerate_uvs =
        degenerate.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(degenerate_uvs, nullptr);
    EXPECT_EQ(*degenerate_uvs,
              (std::vector<mesh::VertexAttributeValues::Float32x2Value>{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}}));
}

TEST(MeshPrimitives, RhombusMatchesBevyMeasurementClosestPointAndMesh) {
    static_assert(mesh::Meshable<mesh::Rhombus>);
    static_assert(mesh::MeshBuilder<mesh::RhombusMeshBuilder>);

    const mesh::Rhombus default_rhombus{};
    EXPECT_EQ(default_rhombus.half_diagonals, glm::vec2(0.5f));

    const mesh::Rhombus rhombus{4.0f, 6.0f};
    EXPECT_EQ(rhombus.half_diagonals, glm::vec2(2.0f, 3.0f));
    EXPECT_FLOAT_EQ(rhombus.side(), std::sqrt(13.0f));
    EXPECT_FLOAT_EQ(rhombus.circumradius(), 3.0f);
    EXPECT_NEAR(rhombus.inradius(), 6.0f / std::sqrt(13.0f), 1e-5f);
    EXPECT_EQ(rhombus.closest_point({0.0f, 0.0f}), glm::vec2(0.0f));
    EXPECT_EQ(rhombus.closest_point({1.0f, 1.0f}), glm::vec2(1.0f, 1.0f));
    // Outside the top-right edge: folded back onto the edge line.
    EXPECT_NEAR(rhombus.closest_point({2.0f, 3.0f}).x, 8.0f / 13.0f, 1e-5f);
    EXPECT_NEAR(rhombus.closest_point({2.0f, 3.0f}).y, 27.0f / 13.0f, 1e-5f);

    EXPECT_EQ(mesh::Rhombus::from_side(2.0f).half_diagonals, glm::vec2(2.0f * 0.5f * std::numbers::sqrt2_v<float>));
    EXPECT_NEAR(mesh::Rhombus::from_inradius(4.0f).half_diagonals.x, 4.0f * std::numbers::sqrt2_v<float>, 1e-6f);
    EXPECT_EQ(mesh::Rhombus::from_inradius(4.0f).half_diagonals.x, mesh::Rhombus::from_inradius(4.0f).half_diagonals.y);

    const auto value = rhombus.mesh().build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, {-2.0f, 0.0f, 0.0f}, {0.0f, -3.0f, 0.0f}}));
    EXPECT_EQ(*uvs, (std::vector<mesh::VertexAttributeValues::Float32x2Value>{
                        {1.0f, 0.5f}, {0.5f, 0.0f}, {0.0f, 0.5f}, {0.5f, 1.0f}}));
    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*value.indices()->get().as_u32(), (std::vector<std::uint32_t>{2, 0, 1, 2, 3, 0}));
}

TEST(MeshPrimitives, AnnulusMatchesBevySeamDuplicationAndUvs) {
    static_assert(mesh::Meshable<mesh::Annulus>);
    static_assert(mesh::MeshBuilder<mesh::AnnulusMeshBuilder>);

    const mesh::Annulus default_annulus{};
    EXPECT_FLOAT_EQ(default_annulus.inner_circle.radius, 0.5f);
    EXPECT_FLOAT_EQ(default_annulus.outer_circle.radius, 1.0f);
    EXPECT_FLOAT_EQ(default_annulus.diameter(), 2.0f);
    EXPECT_FLOAT_EQ(default_annulus.thickness(), 0.5f);

    const mesh::Annulus annulus{1.0f, 2.0f};
    EXPECT_EQ(annulus.closest_point({1.5f, 0.0f}), glm::vec2(1.5f, 0.0f));
    EXPECT_EQ(annulus.closest_point({3.0f, 0.0f}), glm::vec2(2.0f, 0.0f));
    EXPECT_EQ(annulus.closest_point({0.25f, 0.0f}), glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(mesh::Annulus{}.closest_point({4.0f, 0.0f}), glm::vec2(1.0f, 0.0f));

    const auto value = annulus.mesh().with_resolution(4).build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    // 2 * (resolution + 1) vertices; the seam vertex at `FRAC_PI_2` is duplicated for UV wrapping.
    EXPECT_EQ(positions->size(), 10u);
    EXPECT_NEAR((*positions)[0][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[0][1], 1.0f, 1e-6f);
    EXPECT_NEAR((*positions)[1][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[1][1], 2.0f, 1e-6f);
    EXPECT_NEAR((*positions)[2][0], -1.0f, 1e-6f);
    EXPECT_NEAR((*positions)[2][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[3][0], -2.0f, 1e-6f);
    EXPECT_NEAR((*positions)[3][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[8][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[8][1], 1.0f, 1e-6f);
    EXPECT_NEAR((*positions)[9][1], 2.0f, 1e-6f);
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[1], (mesh::VertexAttributeValues::Float32x2Value{1.0f, 0.0f}));
    EXPECT_EQ((*uvs)[2], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.25f}));
    EXPECT_EQ((*uvs)[8], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 1.0f}));
    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*value.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{0, 1, 3, 3, 2, 0, 2, 3, 5, 5, 4, 2, 4, 5, 7, 7, 6, 4, 6, 7, 9, 9, 8, 6}));
}

TEST(MeshPrimitives, Capsule2dMatchesBevySemicircleFanAndRadiusFractionUvs) {
    static_assert(mesh::Meshable<mesh::Capsule2d>);
    static_assert(mesh::MeshBuilder<mesh::Capsule2dMeshBuilder>);

    const mesh::Capsule2d default_capsule{};
    EXPECT_FLOAT_EQ(default_capsule.radius, 0.5f);
    EXPECT_FLOAT_EQ(default_capsule.half_length, 0.5f);

    const mesh::Capsule2d capsule{2.0f, 2.0f};
    EXPECT_FLOAT_EQ(capsule.radius, 2.0f);
    EXPECT_FLOAT_EQ(capsule.half_length, 1.0f);
    EXPECT_FLOAT_EQ(capsule.to_inner_rectangle().half_size.x, 2.0f);
    EXPECT_FLOAT_EQ(capsule.to_inner_rectangle().half_size.y, 1.0f);

    const auto value = capsule.mesh().with_resolution(4).build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ(positions->size(), 8u);
    // Even vertex counts start half a step in, so no vertex sits on the seam.
    const float step = std::numbers::pi_v<float> * 2.0f / 8.0f;
    EXPECT_NEAR((*positions)[0][0], 2.0f * std::cos(step * 0.5f), 1e-5f);
    EXPECT_NEAR((*positions)[0][1], 1.0f + 2.0f * std::sin(step * 0.5f), 1e-5f);
    EXPECT_NEAR((*positions)[4][0], 2.0f * std::cos(step * 4.5f), 1e-5f);
    EXPECT_NEAR((*positions)[4][1], -1.0f + 2.0f * std::sin(step * 4.5f), 1e-5f);
    // `radius_frac = radius / (half_length + radius)` compresses the UVs so the hemicircles do
    // not stretch across the capsule.
    const float radius_fraction = 2.0f / 3.0f;
    EXPECT_NEAR((*uvs)[0][1], radius_fraction * (1.0f - 0.5f * (std::sin(step * 0.5f) + 1.0f)), 1e-5f);
    EXPECT_NEAR((*uvs)[4][1], 1.0f - radius_fraction * 0.5f * (std::sin(step * 4.5f) + 1.0f), 1e-5f);
    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*value.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 0, 3, 4, 4, 5, 6, 4, 6, 7, 4, 7, 0}));
}

TEST(MeshPrimitives, InsetMatchesBevyForEverySupportedShape) {
    static_assert(mesh::Inset<mesh::Circle>);
    static_assert(mesh::Inset<mesh::Capsule2d>);
    static_assert(mesh::Inset<mesh::CircularSegment>);
    static_assert(mesh::Inset<mesh::Rectangle>);
    static_assert(mesh::Inset<mesh::RegularPolygon>);
    static_assert(mesh::Inset<mesh::Rhombus>);
    static_assert(mesh::Inset<mesh::Triangle2d>);

    // Radius-bearing shapes simply lose the inset distance.
    EXPECT_FLOAT_EQ(mesh::inset(mesh::Circle{2.0f}, 0.5f).radius, 1.5f);
    EXPECT_FLOAT_EQ(mesh::inset(mesh::Capsule2d{1.0f, 2.0f}, 0.25f).radius, 0.75f);
    EXPECT_FLOAT_EQ(mesh::inset(mesh::Capsule2d{1.0f, 2.0f}, 0.25f).half_length, 1.0f);

    EXPECT_EQ(mesh::inset(mesh::Rectangle{4.0f, 2.0f}, 0.5f).half_size, glm::vec2(1.5f, 0.5f));

    // RegularPolygon insets the circumradius by distance / sin(internal_angle / 2).
    const mesh::RegularPolygon hexagon{2.0f, 6};
    EXPECT_FLOAT_EQ(hexagon.internal_angle_radians(), std::numbers::pi_v<float> * 2.0f / 3.0f);
    EXPECT_FLOAT_EQ(hexagon.external_angle_radians(), std::numbers::pi_v<float> / 3.0f);
    const float hexagon_half_angle = hexagon.internal_angle_radians() * 0.5f;
    EXPECT_NEAR(mesh::inset(hexagon, 0.5f).circumcircle.radius, 2.0f - 0.5f / std::sin(hexagon_half_angle), 1e-5f);

    // Rhombus offsets each half diagonal by distance / sin(angle) and distance / cos(angle).
    const mesh::Rhombus rhombus{4.0f, 6.0f};
    const float rhombus_angle = std::atan(rhombus.half_diagonals.y / rhombus.half_diagonals.x);
    const auto inset_rhombus  = mesh::inset(rhombus, 0.2f);
    EXPECT_NEAR(inset_rhombus.half_diagonals.x, 2.0f - 0.2f / std::sin(rhombus_angle), 1e-5f);
    EXPECT_NEAR(inset_rhombus.half_diagonals.y, 3.0f - 0.2f / std::cos(rhombus_angle), 1e-5f);

    // Triangle2d moves each vertex along its angle bisector by distance / sin(half_angle).
    // The geometric contract is that every inset edge is parallel to its original edge and
    // exactly `distance` inside it, so the test checks that property rather than raw coordinates.
    const mesh::Triangle2d triangle{{0.0f, 1.0f}, {-0.8660254f, -0.5f}, {0.8660254f, -0.5f}};
    constexpr float inset_distance = 0.1f;
    const auto inset_triangle      = mesh::inset(triangle, inset_distance);
    const auto edge_distance       = [](glm::vec2 point, glm::vec2 first, glm::vec2 second) {
        const auto direction = second - first;
        const auto length    = glm::length(direction);
        return std::abs((point.x - first.x) * direction.y - (point.y - first.y) * direction.x) / length;
    };
    // Every inset vertex sits exactly `inset_distance` inside its original edge.
    for (std::size_t edge = 0; edge < 3; ++edge) {
        const auto first  = triangle.vertices[edge];
        const auto second = triangle.vertices[(edge + 1) % 3];
        EXPECT_NEAR(edge_distance(inset_triangle.vertices[edge], first, second), inset_distance, 2e-5f);
    }
    // An inset of zero is the identity, and two half insets compose into one whole inset.
    EXPECT_EQ(mesh::inset(triangle, 0.0f).vertices, triangle.vertices);
    const auto twice = mesh::inset(mesh::inset(triangle, 0.05f), 0.05f);
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        const auto first  = triangle.vertices[vertex];
        const auto second = triangle.vertices[(vertex + 1) % 3];
        EXPECT_NEAR(edge_distance(twice.vertices[vertex], first, second), 0.1f, 3e-5f);
    }

    // CircularSegment raises the apothem by the inset distance and lowers the radius by it.
    // The inset amount must stay small enough for `apothem + distance` to remain within the new
    // radius, which is the domain `acos` needs; larger amounts are undefined in Bevy too.
    const mesh::CircularSegment segment = mesh::CircularSegment{2.0f, 0.5f};
    constexpr float segment_inset       = 0.1f;
    const auto inset_segment            = mesh::inset(segment, segment_inset);
    EXPECT_FLOAT_EQ(inset_segment.radius(), segment.radius() - segment_inset);
    const float expected_half_angle = std::acos((segment.apothem() + segment_inset) / inset_segment.radius());
    EXPECT_NEAR(inset_segment.half_angle(), expected_half_angle, 1e-5f);
    // The chord stays parallel to the original one and one inset distance closer in.
    EXPECT_NEAR(inset_segment.apothem(), segment.apothem() + segment_inset, 1e-4f);
}

TEST(MeshPrimitives, RingBuildsBevyInsetShapePairAndMesh) {
    static_assert(mesh::Meshable<mesh::Ring<mesh::Circle>>);
    static_assert(mesh::MeshBuilder<mesh::RingMeshBuilder<mesh::Circle>>);

    // `Ring::new` stores the outer shape first, exactly as Bevy declares the fields.
    const mesh::Ring<mesh::Circle> explicit_ring{mesh::Circle{2.0f}, mesh::Circle{1.0f}};
    EXPECT_FLOAT_EQ(explicit_ring.outer_shape.radius, 2.0f);
    EXPECT_FLOAT_EQ(explicit_ring.inner_shape.radius, 1.0f);

    // `from_primitive_and_thickness` insets the outer shape by the requested thickness.
    const auto ring = mesh::Ring<mesh::Circle>::from_primitive_and_thickness(mesh::Circle{2.0f}, 0.5f);
    EXPECT_FLOAT_EQ(ring.outer_shape.radius, 2.0f);
    EXPECT_FLOAT_EQ(ring.inner_shape.radius, 1.5f);

    const auto value = ring.mesh().build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    // Both circles default to resolution 32, so the ring carries 33 outer + 33 inner vertices.
    ASSERT_EQ(positions->size(), 64u);
    EXPECT_EQ(normals->size(), 64u);
    EXPECT_EQ(uvs->size(), 64u);
    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    const auto& indices = *value.indices()->get().as_u32();
    ASSERT_EQ(indices.size(), 32u * 6u);
    // The last triangle pair closes back onto vertex 0 and the first inner vertex.
    EXPECT_EQ(indices[indices.size() - 6], 31u);
    EXPECT_EQ(indices[indices.size() - 5], 0u);
    EXPECT_EQ(indices[indices.size() - 4], 63u);
    EXPECT_EQ(indices[indices.size() - 3], 63u);
    EXPECT_EQ(indices[indices.size() - 2], 0u);
    EXPECT_EQ(indices[indices.size() - 1], 32u);
    // Inner vertices are appended after the outer ones.
    EXPECT_NEAR((*positions)[0][1], 2.0f, 1e-5f);
    EXPECT_NEAR((*positions)[32][1], 1.5f, 1e-5f);
    // Outer UVs pass through and inner UVs are radially rescaled about the UV center.
    const auto outer_mesh = ring.outer_shape.mesh().build();
    const auto* outer_uvs =
        outer_mesh.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(outer_uvs, nullptr);
    EXPECT_EQ((*uvs)[0], (*outer_uvs)[0]);
    EXPECT_EQ((*uvs)[1], (*outer_uvs)[1]);
    const glm::vec2 outer_uv{(*outer_uvs)[0][0], (*outer_uvs)[0][1]};
    const glm::vec2 inner_uv{(*uvs)[32][0], (*uvs)[32][1]};
    EXPECT_LT(glm::length(inner_uv - glm::vec2(0.5f)), glm::length(outer_uv - glm::vec2(0.5f)));
}

TEST(MeshPrimitives, RingBuilderReportsMismatchedShapesInsteadOfPanicking) {
    const mesh::Ring<mesh::Circle> ring{mesh::Circle{2.0f}, mesh::Circle{1.5f}};
    auto builder                = ring.mesh();
    builder.inner_shape_builder = std::move(builder.inner_shape_builder).with_resolution(8);
    // Bevy asserts the two vertex counts match; Epix reports the mismatch instead.
    const auto result = builder.try_build();
    EXPECT_FALSE(result.has_value());
    EXPECT_THROW(builder.build(), std::runtime_error);
}

TEST(MeshPrimitives, CuboidMatchesBevyFaceVertexTableAndIndices) {
    static_assert(mesh::Meshable<mesh::Cuboid>);
    static_assert(mesh::MeshBuilder<mesh::CuboidMeshBuilder>);

    const mesh::Cuboid default_cuboid{};
    EXPECT_EQ(default_cuboid.half_size, glm::vec3(0.5f));
    EXPECT_EQ((mesh::Cuboid{2.0f, 4.0f, 6.0f}.half_size), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ((mesh::Cuboid::from_size({2.0f, 4.0f, 6.0f}).half_size), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(mesh::Cuboid::from_length(3.0f).half_size, glm::vec3(1.5f));
    EXPECT_EQ((mesh::Cuboid::from_corners({-1.0f, -2.0f, -3.0f}, {1.0f, 2.0f, 3.0f}).half_size),
              glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(default_cuboid.size(), glm::vec3(1.0f));

    const auto value = mesh::Cuboid{2.0f, 2.0f, 2.0f}.mesh().build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    // Six faces of four unshared vertices each.
    ASSERT_EQ(positions->size(), 24u);
    EXPECT_EQ(normals->size(), 24u);
    EXPECT_EQ(uvs->size(), 24u);

    // Front face: min/max corners with the +Z normal and Bevy's corner UV order.
    EXPECT_EQ((*positions)[0], (mesh::VertexAttributeValues::Float32x3Value{-1.0f, -1.0f, 1.0f}));
    EXPECT_EQ((*positions)[2], (mesh::VertexAttributeValues::Float32x3Value{1.0f, 1.0f, 1.0f}));
    EXPECT_EQ((*normals)[0], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 0.0f, 1.0f}));
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[2], (mesh::VertexAttributeValues::Float32x2Value{1.0f, 1.0f}));
    // Back face starts at index 4 with the -Z normal.
    EXPECT_EQ((*normals)[4], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 0.0f, -1.0f}));
    // Right, left, top, and bottom face normals land at their Bevy-ordered offsets.
    EXPECT_EQ((*normals)[8], (mesh::VertexAttributeValues::Float32x3Value{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ((*normals)[12], (mesh::VertexAttributeValues::Float32x3Value{-1.0f, 0.0f, 0.0f}));
    EXPECT_EQ((*normals)[16], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ((*normals)[20], (mesh::VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f}));

    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*value.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
                                          12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20}));
}

TEST(MeshPrimitives, PlaneMatchesBevySubdivisionGridAndRotation) {
    static_assert(mesh::Meshable<mesh::Plane3d>);
    static_assert(mesh::MeshBuilder<mesh::PlaneMeshBuilder>);

    const mesh::Plane3d default_plane{};
    EXPECT_EQ(default_plane.normal, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(default_plane.half_size, glm::vec2(0.5f));
    EXPECT_EQ((mesh::PlaneMeshBuilder::from_size({4.0f, 2.0f}).plane.half_size), glm::vec2(2.0f, 1.0f));
    EXPECT_EQ(mesh::PlaneMeshBuilder::from_length(3.0f).plane.half_size, glm::vec2(1.5f));
    EXPECT_EQ((mesh::PlaneMeshBuilder(glm::vec3(0.0f, 0.0f, 1.0f), {1.0f, 1.0f}).plane.half_size), glm::vec2(0.5f));

    // The default +Y plane spans the XZ axes with 2x2 vertices and one quad.
    const auto flat = mesh::Plane3d{}.mesh().build();
    const auto* flat_positions =
        flat.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* flat_normals =
        flat.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* flat_uvs =
        flat.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(flat_positions, nullptr);
    ASSERT_NE(flat_normals, nullptr);
    ASSERT_NE(flat_uvs, nullptr);
    ASSERT_EQ(flat_positions->size(), 4u);
    EXPECT_EQ((*flat_positions)[0], (mesh::VertexAttributeValues::Float32x3Value{-0.5f, 0.0f, -0.5f}));
    EXPECT_EQ((*flat_positions)[3], (mesh::VertexAttributeValues::Float32x3Value{0.5f, 0.0f, 0.5f}));
    EXPECT_EQ((*flat_normals)[0], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 1.0f, 0.0f}));
    // UVs are the normalized grid coordinates in (x, z) order.
    EXPECT_EQ((*flat_uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    EXPECT_EQ((*flat_uvs)[1], (mesh::VertexAttributeValues::Float32x2Value{1.0f, 0.0f}));
    EXPECT_EQ((*flat_uvs)[2], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 1.0f}));
    EXPECT_EQ((*flat_uvs)[3], (mesh::VertexAttributeValues::Float32x2Value{1.0f, 1.0f}));
    ASSERT_NE(flat.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*flat.indices()->get().as_u32(), (std::vector<std::uint32_t>{3, 1, 2, 0, 2, 1}));

    // `subdivisions = n` produces (n + 2)^2 vertices and (n + 1)^2 quads.
    const auto subdivided = std::move(mesh::Plane3d{}.mesh()).with_subdivisions(1).build();
    const auto* positions =
        subdivided.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(positions->size(), 9u);
    ASSERT_NE(subdivided.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(subdivided.indices()->get().as_u32()->size(), 4u * 6u);
    // The middle vertex sits at the plane center.
    EXPECT_EQ((*positions)[4], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 0.0f, 0.0f}));

    // A +Z normal rotates the XZ grid onto the XY plane.
    auto vertical_builder         = mesh::PlaneMeshBuilder::from_size({2.0f, 2.0f});
    vertical_builder.plane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    const auto vertical           = vertical_builder.build();
    const auto* vertical_positions =
        vertical.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* vertical_normals =
        vertical.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(vertical_positions, nullptr);
    ASSERT_NE(vertical_normals, nullptr);
    EXPECT_NEAR((*vertical_normals)[0][2], 1.0f, 1e-5f);
    // Every rotated vertex keeps a zero Z extent in the rotated frame's normal axis.
    for (const auto& position : *vertical_positions) {
        EXPECT_NEAR(position[2], 0.0f, 1e-5f);
    }
}

TEST(MeshPrimitives, SegmentAndPolyline3dMatchBevyLineListData) {
    static_assert(mesh::Meshable<mesh::Segment3d>);
    static_assert(mesh::Meshable<mesh::Polyline3d>);

    EXPECT_EQ(mesh::Segment3d{}.vertices, (std::array{glm::vec3(-0.5f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f)}));
    EXPECT_FLOAT_EQ((mesh::Segment3d{{0.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 4.0f}}.length()), 5.0f);
    const auto scaled = mesh::Segment3d::from_scaled_direction({2.0f, 0.0f, 0.0f});
    EXPECT_EQ(scaled.vertices, (std::array{glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}));
    EXPECT_EQ((mesh::Segment3d::from_direction_and_length({0.0f, 0.0f, 2.0f}, 4.0f).vertices),
              (std::array{glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.0f, 0.0f, 2.0f)}));

    const mesh::Segment3d segment_shape{{0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}};
    const auto segment_mesh = segment_shape.mesh().build();
    EXPECT_EQ(segment_mesh.get_primitive_type(), wgpu::PrimitiveTopology::eLineList);
    const auto* segment_positions =
        segment_mesh.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(segment_positions, nullptr);
    EXPECT_EQ(*segment_positions,
              (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}}));
    ASSERT_NE(segment_mesh.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*segment_mesh.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1}));
    EXPECT_FALSE(segment_mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));

    const auto polyline = mesh::Polyline3d::with_subdivisions({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 3.0f}, 2);
    EXPECT_EQ(polyline.vertices, (std::vector{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                                              glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, 3.0f)}));
    const auto polyline_mesh = polyline.mesh().build();
    EXPECT_EQ(polyline_mesh.get_primitive_type(), wgpu::PrimitiveTopology::eLineList);
    EXPECT_FALSE(polyline_mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_NORMAL));
    ASSERT_NE(polyline_mesh.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*polyline_mesh.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 1, 2, 2, 3}));
}

TEST(MeshPrimitives, Triangle3dMatchesBevyFaceNormalUvAndClassification) {
    static_assert(mesh::Meshable<mesh::Triangle3d>);
    static_assert(mesh::MeshBuilder<mesh::Triangle3dMeshBuilder>);

    EXPECT_EQ(mesh::Triangle3d{}.vertices,
              (std::array{glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec3(0.5f, -0.5f, 0.0f)}));

    // `ab x ac` gives the right-hand-rule face normal.
    const mesh::Triangle3d flat{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    ASSERT_TRUE(flat.normal().has_value());
    EXPECT_EQ(*flat.normal(), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(flat.is_degenerate());
    EXPECT_EQ(flat.reversed().vertices,
              (std::array{glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f)}));

    // Collinear vertices have no direction and are reported as degenerate.
    const mesh::Triangle3d collinear{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
    EXPECT_FALSE(collinear.normal().has_value());
    EXPECT_TRUE(collinear.is_degenerate());

    const auto value = flat.mesh().build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}));
    // Every vertex carries the face normal, and the unskewed UVs put `a` at the origin.
    EXPECT_EQ(*normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>(3, {0.0f, 0.0f, 1.0f})));
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(uvs, nullptr);
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[1], (mesh::VertexAttributeValues::Float32x2Value{1.0f, 0.0f}));
    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*value.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2}));

    // A degenerate triangle gets Bevy's zero normal rather than an undefined direction.
    const auto degenerate = collinear.mesh().build();
    const auto* degenerate_normals =
        degenerate.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(degenerate_normals, nullptr);
    EXPECT_EQ(*degenerate_normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>(3, {0.0f, 0.0f, 0.0f})));
}

TEST(MeshPrimitives, TetrahedronMatchesBevyFaceOrientationAndVertexData) {
    static_assert(mesh::Meshable<mesh::Tetrahedron>);
    static_assert(mesh::MeshBuilder<mesh::TetrahedronMeshBuilder>);

    const mesh::Tetrahedron default_tetrahedron{};
    // Bevy's default vertex order has a negative signed volume, so its faces point inward and
    // the mesh builder reverses them to face outward.
    EXPECT_NEAR(default_tetrahedron.signed_volume(), -1.0f / 3.0f, 1e-5f);
    EXPECT_EQ(default_tetrahedron.centroid(), glm::vec3(0.0f));

    // Faces are (b,c,d), (a,c,d) reversed, (a,b,d), and (a,b,c) reversed.
    const auto faces = default_tetrahedron.faces();
    EXPECT_EQ((faces[0].vertices), (std::array{default_tetrahedron.vertices[1], default_tetrahedron.vertices[2],
                                               default_tetrahedron.vertices[3]}));
    EXPECT_EQ((faces[1].vertices), (std::array{default_tetrahedron.vertices[3], default_tetrahedron.vertices[2],
                                               default_tetrahedron.vertices[0]}));
    EXPECT_EQ((faces[2].vertices), (std::array{default_tetrahedron.vertices[0], default_tetrahedron.vertices[1],
                                               default_tetrahedron.vertices[3]}));
    EXPECT_EQ((faces[3].vertices), (std::array{default_tetrahedron.vertices[2], default_tetrahedron.vertices[1],
                                               default_tetrahedron.vertices[0]}));

    // A negative orientation is detected and its faces are reversed so they still face outward.
    // Swapping the first two vertices flips the sign without moving any point, so this
    // orientation is already outward and must not be reversed.
    const mesh::Tetrahedron inverted{default_tetrahedron.vertices[1], default_tetrahedron.vertices[0],
                                     default_tetrahedron.vertices[2], default_tetrahedron.vertices[3]};
    EXPECT_GT(inverted.signed_volume(), 0.0f);
    const auto outward = inverted.mesh().build();
    const auto* positions =
        outward.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        outward.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    // Four faces of three unshared vertices each.
    ASSERT_EQ(positions->size(), 12u);
    EXPECT_EQ(normals->size(), 12u);
    ASSERT_NE(outward.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(*outward.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));

    // Every face normal points away from the tetrahedron centroid.
    for (std::size_t face = 0; face < 4; ++face) {
        const glm::vec3 face_center =
            (glm::vec3{(*positions)[face * 3][0], (*positions)[face * 3][1], (*positions)[face * 3][2]} +
             glm::vec3{(*positions)[face * 3 + 1][0], (*positions)[face * 3 + 1][1], (*positions)[face * 3 + 1][2]} +
             glm::vec3{(*positions)[face * 3 + 2][0], (*positions)[face * 3 + 2][1], (*positions)[face * 3 + 2][2]}) /
            3.0f;
        const glm::vec3 face_normal{(*normals)[face * 3][0], (*normals)[face * 3][1], (*normals)[face * 3][2]};
        EXPECT_GT(glm::dot(face_normal, face_center - inverted.centroid()), 0.0f);
    }
}

TEST(MeshPrimitives, CylinderMatchesBevyRingsBarrelSkinAndCaps) {
    static_assert(mesh::Meshable<mesh::Cylinder>);
    static_assert(mesh::MeshBuilder<mesh::CylinderMeshBuilder>);

    const mesh::Cylinder default_cylinder{};
    EXPECT_FLOAT_EQ(default_cylinder.radius, 0.5f);
    EXPECT_FLOAT_EQ(default_cylinder.half_height, 0.5f);
    EXPECT_FLOAT_EQ((mesh::Cylinder{2.0f, 3.0f}.half_height), 1.5f);
    EXPECT_FLOAT_EQ(default_cylinder.base().radius, 0.5f);
    EXPECT_FLOAT_EQ((mesh::Cylinder{2.0f, 4.0f}.lateral_area()), 4.0f * std::numbers::pi_v<float> * 2.0f * 2.0f);
    EXPECT_FLOAT_EQ((mesh::Cylinder{2.0f, 1.0f}.base_area()), std::numbers::pi_v<float> * 4.0f);

    // Default builder: resolution 32, one segment, caps enabled, midpoint anchor.
    const auto value = mesh::Cylinder{1.0f, 2.0f}.mesh().with_resolution(4).build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    // resolution * 2 cap vertices + (segments + 1) rings of (resolution + 1) seam-duplicated vertices.
    ASSERT_EQ(positions->size(), 4u * 2u + 2u * 5u);
    EXPECT_EQ(normals->size(), 18u);
    EXPECT_EQ(uvs->size(), 18u);

    // The first ring sits at the bottom cap height with a radial normal and wrapping UVs.
    EXPECT_NEAR((*positions)[0][1], -1.0f, 1e-5f);
    EXPECT_NEAR((*positions)[0][0], 1.0f, 1e-5f);
    EXPECT_NEAR((*positions)[4][0], 1.0f, 1e-5f);
    EXPECT_EQ((*normals)[0], (mesh::VertexAttributeValues::Float32x3Value{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[1], (mesh::VertexAttributeValues::Float32x2Value{0.25f, 0.0f}));
    // The second ring is the top cap height.
    EXPECT_NEAR((*positions)[5][1], 1.0f, 1e-5f);
    // Cap vertices carry the +-Y normals and polar UVs.
    EXPECT_EQ((*normals)[10], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ((*normals)[14], (mesh::VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ((*uvs)[10], (mesh::VertexAttributeValues::Float32x2Value{1.0f, 0.5f}));

    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    // One segment: 4 barrel quads of 2 triangles each, plus 2 caps of (resolution - 2) triangles.
    EXPECT_EQ(value.indices()->get().as_u32()->size(), 4u * 2u * 3u + 2u * 2u * 3u);
    // The first barrel triangle uses the ring and next-ring seam layout.
    const auto& indices = *value.indices()->get().as_u32();
    EXPECT_EQ((std::vector<std::uint32_t>{indices[0], indices[1], indices[2]}), (std::vector<std::uint32_t>{0, 5, 1}));
}

TEST(MeshPrimitives, CylinderAnchorAndWithoutCapsMatchBevy) {
    // `without_caps` drops both cap fans, leaving only the barrel.
    const auto tube = std::move(mesh::Cylinder{1.0f, 2.0f}.mesh()).with_resolution(4).without_caps().build();
    const auto* tube_positions =
        tube.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(tube_positions, nullptr);
    EXPECT_EQ(tube_positions->size(), 10u);
    ASSERT_NE(tube.indices()->get().as_u32(), nullptr);
    EXPECT_EQ(tube.indices()->get().as_u32()->size(), 4u * 2u * 3u);

    // `Top` moves the top cap to Y = 0, `Bottom` moves the bottom cap to Y = 0.
    const auto top =
        std::move(mesh::Cylinder{1.0f, 2.0f}.mesh()).with_resolution(4).with_anchor(mesh::CylinderAnchor::Top).build();
    const auto* top_positions =
        top.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(top_positions, nullptr);
    // Cap vertices are appended after the rings, so indices 10..13 are the top cap and 14..17 the bottom cap.
    for (std::size_t i = 10; i < 14; ++i) EXPECT_NEAR((*top_positions)[i][1], 0.0f, 1e-5f);
    for (std::size_t i = 14; i < 18; ++i) EXPECT_NEAR((*top_positions)[i][1], -2.0f, 1e-5f);

    const auto bottom = std::move(mesh::Cylinder{1.0f, 2.0f}.mesh())
                            .with_resolution(4)
                            .with_anchor(mesh::CylinderAnchor::Bottom)
                            .build();
    const auto* bottom_positions =
        bottom.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(bottom_positions, nullptr);
    for (std::size_t i = 10; i < 14; ++i) EXPECT_NEAR((*bottom_positions)[i][1], 2.0f, 1e-5f);
    for (std::size_t i = 14; i < 18; ++i) EXPECT_NEAR((*bottom_positions)[i][1], 0.0f, 1e-5f);

    // Two segments add one interior ring, so the ring vertex count grows by resolution + 1.
    const auto segmented = std::move(mesh::Cylinder{1.0f, 2.0f}.mesh()).with_resolution(4).with_segments(2).build();
    const auto* segmented_positions =
        segmented.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(segmented_positions, nullptr);
    EXPECT_EQ(segmented_positions->size(), 4u * 2u + 3u * 5u);
}

TEST(MeshPrimitives, TorusMatchesBevyTubeGridAndAngleRange) {
    static_assert(mesh::Meshable<mesh::Torus>);
    static_assert(mesh::MeshBuilder<mesh::TorusMeshBuilder>);

    const mesh::Torus default_torus{};
    EXPECT_FLOAT_EQ(default_torus.minor_radius, 0.25f);
    EXPECT_FLOAT_EQ(default_torus.major_radius, 0.75f);
    // `Torus::new` takes the hole radius and the overall outer radius.
    const auto derived = mesh::Torus::from_inner_and_outer(0.5f, 1.5f);
    EXPECT_FLOAT_EQ(derived.minor_radius, 0.5f);
    EXPECT_FLOAT_EQ(derived.major_radius, 1.0f);

    const auto value =
        std::move(mesh::Torus{0.25f, 0.75f}.mesh()).with_minor_resolution(4).with_major_resolution(6).build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    // (major + 1) * (minor + 1) vertices, closed in both directions.
    ASSERT_EQ(positions->size(), 7u * 5u);
    EXPECT_EQ(normals->size(), 35u);
    EXPECT_EQ(uvs->size(), 35u);

    // The first vertex sits at angle zero on the outer tube with a +X radial normal.
    EXPECT_NEAR((*positions)[0][0], 1.0f, 1e-5f);
    EXPECT_NEAR((*positions)[0][1], 0.0f, 1e-5f);
    EXPECT_NEAR((*positions)[0][2], 0.0f, 1e-5f);
    EXPECT_EQ((*normals)[0], (mesh::VertexAttributeValues::Float32x3Value{1.0f, 0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[1], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.25f}));
    // The second ring row starts one major step around the ring.
    EXPECT_NEAR((*uvs)[5][0], 1.0f / 6.0f, 1e-5f);
    // The second tube vertex is a quarter turn round the tube, i.e. the top of the torus.
    EXPECT_NEAR((*positions)[1][1], 0.25f, 1e-5f);

    ASSERT_NE(value.indices()->get().as_u32(), nullptr);
    // major * minor quads, two triangles each.
    EXPECT_EQ(value.indices()->get().as_u32()->size(), 6u * 4u * 6u);
    const auto& indices = *value.indices()->get().as_u32();
    EXPECT_EQ((std::vector<std::uint32_t>{indices[0], indices[1], indices[2]}), (std::vector<std::uint32_t>{0, 1, 5}));
    EXPECT_EQ((std::vector<std::uint32_t>{indices[3], indices[4], indices[5]}), (std::vector<std::uint32_t>{1, 6, 5}));

    // A partial angle range still emits a closed grid of major + 1 rows.
    const auto partial = std::move(mesh::Torus{0.25f, 0.75f}.mesh())
                             .with_minor_resolution(3)
                             .with_major_resolution(4)
                             .with_angle_range(0.0f, std::numbers::pi_v<float>)
                             .build();
    const auto* partial_positions =
        partial.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(partial_positions, nullptr);
    EXPECT_EQ(partial_positions->size(), 5u * 4u);
    // The last row sits a half turn round, i.e. on the -X side.
    EXPECT_NEAR((*partial_positions)[4u * 4u][0], -1.0f, 1e-5f);
}

// Bevy `ConeMeshBuilder::build`
// (`bevy_mesh/src/primitives/dim3/cone.rs:72-171`) at resolution 4, which is the
// resolution Bevy's own `cone_mesh` test uses.
TEST(MeshPrimitives, ConeMatchesBevyTipLateralSurfaceAndBase) {
    static_assert(mesh::Meshable<mesh::Cone>);
    static_assert(mesh::MeshBuilder<mesh::ConeMeshBuilder>);

    const mesh::Cone default_cone{};
    EXPECT_FLOAT_EQ(default_cone.radius, 0.5f);
    EXPECT_FLOAT_EQ(default_cone.height, 1.0f);
    EXPECT_FLOAT_EQ(default_cone.slant_height(), std::sqrt(0.5f * 0.5f + 1.0f));
    EXPECT_FLOAT_EQ(default_cone.base_area(), std::numbers::pi_v<float> * 0.25f);
    EXPECT_FLOAT_EQ(default_cone.lateral_area(), std::numbers::pi_v<float> * 0.5f * std::sqrt(0.5f * 0.5f + 1.0f));

    const auto value = std::move(mesh::Cone{0.5f, 1.0f}.mesh()).with_resolution(4).build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);

    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    // resolution * 2 + 1 vertices: the tip, the lateral ring, and the base ring.
    ASSERT_EQ(positions->size(), 9u);
    EXPECT_EQ(normals->size(), 9u);
    EXPECT_EQ(uvs->size(), 9u);

    // Tip vertex: at +half_height with Bevy's deliberately invalid zero normal
    // and the polar UV centre.
    EXPECT_EQ((*positions)[0], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 0.5f, 0.0f}));
    EXPECT_EQ((*normals)[0], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.5f, 0.5f}));

    // Lateral ring at -half_height, angle zero first: +X with the slope normal.
    EXPECT_NEAR((*positions)[1][0], 0.5f, 1e-5f);
    EXPECT_NEAR((*positions)[1][1], -0.5f, 1e-5f);
    EXPECT_NEAR((*positions)[1][2], 0.0f, 1e-5f);
    const float factor = 1.0f / std::sqrt(1.0f + 0.25f);
    EXPECT_NEAR((*normals)[1][0], factor, 1e-5f);
    EXPECT_NEAR((*normals)[1][1], 0.5f * factor, 1e-5f);
    EXPECT_NEAR((*normals)[1][2], 0.0f, 1e-5f);
    // Lateral UVs are polar: centred at the tip's UV.
    EXPECT_NEAR((*uvs)[1][0], 1.0f, 1e-5f);
    EXPECT_NEAR((*uvs)[1][1], 0.5f, 1e-5f);
    // The second lateral vertex is a quarter turn round, i.e. +Z.
    EXPECT_NEAR((*positions)[2][0], 0.0f, 1e-5f);
    EXPECT_NEAR((*positions)[2][2], 0.5f, 1e-5f);

    // Base ring: five vertices later, all facing -Y.
    EXPECT_NEAR((*positions)[5][1], -0.5f, 1e-5f);
    EXPECT_EQ((*normals)[5], (mesh::VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f}));
    EXPECT_EQ((*normals)[8], (mesh::VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f}));

    const auto* indices = value.indices()->get().as_u32();
    ASSERT_NE(indices, nullptr);
    // resolution * 6 - 6 indices: the lateral fan, its closing triangle, and the
    // base fan.
    EXPECT_EQ(indices->size(), 18u);
    // The lateral fan starts at the tip.
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[0], (*indices)[1], (*indices)[2]}),
              (std::vector<std::uint32_t>{0, 2, 1}));
    // Bevy closes the lateral surface with `[0, 1, resolution]`.
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[9], (*indices)[10], (*indices)[11]}),
              (std::vector<std::uint32_t>{0, 1, 4}));
    // The base fan is centred on the first base vertex (index 5).
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[12], (*indices)[13], (*indices)[14]}),
              (std::vector<std::uint32_t>{5, 6, 7}));

    // Bevy's anchors shift the whole mesh along Y.
    const auto tip =
        std::move(mesh::Cone{0.5f, 1.0f}.mesh()).with_resolution(4).with_anchor(mesh::ConeAnchor::Tip).build();
    const auto* tip_positions =
        tip.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(tip_positions, nullptr);
    EXPECT_NEAR((*tip_positions)[0][1], 0.0f, 1e-5f);
    EXPECT_NEAR((*tip_positions)[1][1], -1.0f, 1e-5f);

    const auto base =
        std::move(mesh::Cone{0.5f, 1.0f}.mesh()).with_resolution(4).with_anchor(mesh::ConeAnchor::Base).build();
    const auto* base_positions =
        base.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(base_positions, nullptr);
    EXPECT_NEAR((*base_positions)[0][1], 1.0f, 1e-5f);
    EXPECT_NEAR((*base_positions)[5][1], 0.0f, 1e-5f);
}

// Bevy `ConicalFrustumMeshBuilder::build`
// (`bevy_mesh/src/primitives/dim3/conical_frustum.rs:63-166`).
TEST(MeshPrimitives, ConicalFrustumMatchesBevyRingsLateralSurfaceAndCaps) {
    static_assert(mesh::Meshable<mesh::ConicalFrustum>);
    static_assert(mesh::MeshBuilder<mesh::ConicalFrustumMeshBuilder>);

    const mesh::ConicalFrustum default_frustum{};
    EXPECT_FLOAT_EQ(default_frustum.radius_top, 0.25f);
    EXPECT_FLOAT_EQ(default_frustum.radius_bottom, 0.5f);
    EXPECT_FLOAT_EQ(default_frustum.height, 0.5f);
    EXPECT_FLOAT_EQ(default_frustum.bottom_base_area(), std::numbers::pi_v<float> * 0.25f);
    EXPECT_FLOAT_EQ(default_frustum.top_base_area(), std::numbers::pi_v<float> * 0.0625f);
    EXPECT_FLOAT_EQ(default_frustum.slant_height(), std::sqrt(0.25f * 0.25f + 0.5f * 0.5f));

    const auto value =
        std::move(mesh::ConicalFrustum{0.25f, 0.5f, 0.5f}.mesh()).with_resolution(4).with_segments(1).build();
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);

    const auto* positions =
        value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);
    // Two seam-duplicated rings of resolution + 1, plus two caps of resolution.
    ASSERT_EQ(positions->size(), 2u * 5u + 2u * 4u);
    EXPECT_EQ(normals->size(), 18u);
    EXPECT_EQ(uvs->size(), 18u);

    // Bottom ring first, at -half_height with the bottom radius.
    EXPECT_NEAR((*positions)[0][0], 0.5f, 1e-5f);
    EXPECT_NEAR((*positions)[0][1], -0.25f, 1e-5f);
    EXPECT_NEAR((*positions)[0][2], 0.0f, 1e-5f);
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 0.0f}));
    // The lateral normal's Y is (radius_bottom - radius_top) / height.
    const glm::vec3 expected_normal = glm::normalize(glm::vec3{1.0f, (0.5f - 0.25f) / 0.5f, 0.0f});
    EXPECT_NEAR((*normals)[0][0], expected_normal.x, 1e-5f);
    EXPECT_NEAR((*normals)[0][1], expected_normal.y, 1e-5f);

    // The ring row is resolution + 1 wide and wraps back to the seam, so the
    // second ring starts at the top radius.
    EXPECT_NEAR((*positions)[5][0], 0.25f, 1e-5f);
    EXPECT_NEAR((*positions)[5][1], 0.25f, 1e-5f);
    EXPECT_EQ((*uvs)[5], (mesh::VertexAttributeValues::Float32x2Value{0.0f, 1.0f}));
    // The seam vertex duplicates the first one's position at u = 1.
    EXPECT_NEAR((*positions)[4][0], 0.5f, 1e-5f);
    EXPECT_NEAR((*uvs)[4][0], 1.0f, 1e-5f);

    // Top cap faces +Y, bottom cap faces -Y.
    EXPECT_EQ((*normals)[10], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ((*normals)[14], (mesh::VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f}));

    const auto* indices = value.indices()->get().as_u32();
    ASSERT_NE(indices, nullptr);
    // resolution * (num_rings - 2) faces plus two cap fans of resolution - 2
    // triangles each, two triangles per face.
    EXPECT_EQ(indices->size(), 4u * 2u * 3u + 2u * 2u * 3u);
    // Lateral quad 0: (ring + j, next_ring + j, ring + j + 1) then the second
    // triangle.
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[0], (*indices)[1], (*indices)[2]}),
              (std::vector<std::uint32_t>{0, 5, 1}));
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[3], (*indices)[4], (*indices)[5]}),
              (std::vector<std::uint32_t>{5, 6, 1}));
    // The top cap starts at the first cap vertex (10) with winding (1, 0).
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[24], (*indices)[25], (*indices)[26]}),
              (std::vector<std::uint32_t>{10, 12, 11}));
    // The bottom cap starts at the second cap vertex block (14) with (0, 1).
    EXPECT_EQ((std::vector<std::uint32_t>{(*indices)[30], (*indices)[31], (*indices)[32]}),
              (std::vector<std::uint32_t>{14, 15, 16}));

    // More segments add intermediate rings without changing the caps.
    const auto multi =
        std::move(mesh::ConicalFrustum{0.25f, 0.5f, 0.5f}.mesh()).with_resolution(4).with_segments(3).build();
    const auto* multi_positions =
        multi.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(multi_positions, nullptr);
    EXPECT_EQ(multi_positions->size(), 4u * 5u + 2u * 4u);
    // Three segments put the middle ring at `-half_height + 2 * step_y` with the
    // linearly interpolated radius, i.e. the middle third of the frustum.
    const float step_y      = 0.5f / 3.0f;
    const float step_radius = (0.25f - 0.5f) / 3.0f;
    EXPECT_NEAR((*multi_positions)[2u * 5u][1], -0.25f + 2.0f * step_y, 1e-5f);
    EXPECT_NEAR((*multi_positions)[2u * 5u][0], 0.5f + 2.0f * step_radius, 1e-5f);
}

TEST(MeshModule, Box2dUvBuildsTexturedQuad) {
    auto value = mesh::make_box2d_uv(20.0f, 10.0f, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), glm::vec4(0.5f));
    EXPECT_EQ(value.count_vertices(), 4);
    EXPECT_TRUE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    auto attribute = value.attribute(mesh::Mesh::ATTRIBUTE_UV_0);
    ASSERT_TRUE(attribute.has_value());
    const auto* uvs = attribute->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(uvs, nullptr);
    ASSERT_EQ(uvs->size(), 4u);
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.25f, 0.5f}));
    EXPECT_EQ((*uvs)[2], (mesh::VertexAttributeValues::Float32x2Value{0.75f, 1.0f}));
}

TEST(MeshModule, GetMeshVertexBufferLayoutMatchesBevy) {
    auto source = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    mesh::MeshVertexBufferLayouts layouts;
    const auto first = source.get_mesh_vertex_buffer_layout(layouts);
    const auto again = source.get_mesh_vertex_buffer_layout(layouts);
    ASSERT_TRUE(first.value);
    EXPECT_EQ(first, again);
    EXPECT_EQ(first.value->layout().array_stride, 28u);

    const auto remapped = first.value->get_layout(std::array{
        mesh::VertexAttributeDescriptor{.shader_location = 0, .id = mesh::Mesh::ATTRIBUTE_COLOR.id, .name = "color"},
        mesh::VertexAttributeDescriptor{
            .shader_location = 1, .id = mesh::Mesh::ATTRIBUTE_POSITION.id, .name = "position"},
    });
    ASSERT_TRUE(remapped.has_value());
    ASSERT_EQ(remapped->attributes.size(), 2u);
    EXPECT_EQ(remapped->attributes[0].shader_location, 0u);
    EXPECT_EQ(remapped->attributes[1].shader_location, 1u);
}

TEST(MeshModule, EmptyMeshDataExtractsOnceAndPreservesAccessState) {
    mesh::Mesh source(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    const auto missing_indices = source.try_indices();
    ASSERT_FALSE(missing_indices.has_value());
    EXPECT_EQ(missing_indices.error(), mesh::MeshAccessError::NotFound);
    EXPECT_TRUE(source.take_gpu_data().has_value());
    EXPECT_EQ(source.try_attributes().error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_FALSE(source.take_gpu_data().has_value());
}

// Bevy `Mesh::get_vertex_size`, `Mesh::get_vertex_buffer_size`,
// `Mesh::create_packed_vertex_buffer_data` and
// `Mesh::write_packed_vertex_buffer_data` (`bevy_mesh/src/mesh.rs:818-968`): the
// vertex size is the sum of the attribute format sizes, the buffer size is that
// size times the vertex count, and the packed data interleaves each vertex's
// attributes in ascending attribute-ID order.
TEST(MeshModule, VertexPackingMatchesBevy) {
    mesh::Mesh source(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    EXPECT_EQ(source.get_vertex_size(), 0u);
    EXPECT_EQ(source.get_vertex_buffer_size(), 0u);
    EXPECT_TRUE(source.create_packed_vertex_buffer_data().empty());

    source.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                            std::array{glm::vec3{0.0f, 1.0f, 2.0f}, glm::vec3{3.0f, 4.0f, 5.0f}});
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR,
                            std::array{glm::vec4{1.0f, 0.5f, 0.25f, 0.0f}, glm::vec4{0.0f, 0.25f, 0.5f, 1.0f}});

    EXPECT_EQ(source.get_vertex_size(), 28u);
    EXPECT_EQ(source.get_vertex_buffer_size(), 56u);

    const auto packed = source.create_packed_vertex_buffer_data();
    ASSERT_EQ(packed.size(), 56u);
    // Vertex 0's position and color, then vertex 1's, in attribute-ID order.
    std::array<float, 7> first_vertex{};
    std::memcpy(first_vertex.data(), packed.data(), 28);
    EXPECT_EQ(first_vertex, (std::array<float, 7>{0.0f, 1.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.0f}));
    std::array<float, 7> second_vertex{};
    std::memcpy(second_vertex.data(), packed.data() + 28, 28);
    EXPECT_EQ(second_vertex, (std::array<float, 7>{3.0f, 4.0f, 5.0f, 0.0f, 0.25f, 0.5f, 1.0f}));

    // The write form fills a caller-provided buffer with the same bytes.
    std::vector<std::uint8_t> buffer(source.get_vertex_buffer_size(), 0);
    source.write_packed_vertex_buffer_data(buffer);
    EXPECT_EQ(buffer, packed);

    // Attributes of different lengths are truncated to the smallest, as in Bevy.
    source.insert_attribute(mesh::Mesh::ATTRIBUTE_UV_0, std::array{glm::vec2{0.0f, 0.0f}});
    EXPECT_EQ(source.count_vertices(), 1u);
    EXPECT_EQ(source.get_vertex_buffer_size(), 36u);
}

// Bevy `Capsule3d` / `Capsule3dMeshBuilder` defaults and constructors
// (`bevy_math/src/primitives/dim3.rs`, `bevy_mesh/src/primitives/dim3/capsule.rs:9-94`).
TEST(MeshPrimitives, Capsule3dDefaultsAndConstructorsMatchBevy) {
    static_assert(mesh::Meshable<mesh::Capsule3d>);
    static_assert(mesh::MeshBuilder<mesh::Capsule3dMeshBuilder>);

    const mesh::Capsule3d capsule{};
    EXPECT_FLOAT_EQ(capsule.radius, 0.5f);
    EXPECT_FLOAT_EQ(capsule.half_length, 0.5f);
    // `new` takes the distance between the hemisphere centres, so half of it.
    const mesh::Capsule3d from_new{2.0f, 3.0f};
    EXPECT_FLOAT_EQ(from_new.radius, 2.0f);
    EXPECT_FLOAT_EQ(from_new.half_length, 1.5f);
    // The connecting part is a cylinder of the same radius and the full length.
    const mesh::Cylinder cylinder = from_new.to_cylinder();
    EXPECT_FLOAT_EQ(cylinder.radius, 2.0f);
    EXPECT_FLOAT_EQ(cylinder.half_height, 1.5f);

    const mesh::Capsule3dMeshBuilder builder{};
    EXPECT_FLOAT_EQ(builder.capsule.radius, 0.5f);
    EXPECT_EQ(builder.rings, 0u);
    EXPECT_EQ(builder.longitudes, 32u);
    EXPECT_EQ(builder.latitudes, 16u);
    EXPECT_EQ(builder.uv_profile, mesh::CapsuleUvProfile::Aspect);

    const mesh::Capsule3dMeshBuilder from_args{1.0f, 2.0f, 8u, 4u};
    EXPECT_FLOAT_EQ(from_args.capsule.radius, 1.0f);
    EXPECT_FLOAT_EQ(from_args.capsule.half_length, 1.0f);
    EXPECT_EQ(from_args.longitudes, 8u);
    EXPECT_EQ(from_args.latitudes, 4u);
    EXPECT_EQ(from_args.rings, 0u);
    EXPECT_EQ(from_args.uv_profile, mesh::CapsuleUvProfile::Aspect);
    // `mesh()` is the `Meshable` entry point and preserves the shape.
    EXPECT_EQ((mesh::Capsule3d{1.0f, 2.0f}.mesh().capsule), (mesh::Capsule3d{1.0f, 2.0f}));

    const auto configured =
        mesh::Capsule3dMeshBuilder{}.with_rings(3).with_longitudes(5).with_latitudes(6).with_uv_profile(
            mesh::CapsuleUvProfile::Fixed);
    EXPECT_EQ(configured.rings, 3u);
    EXPECT_EQ(configured.longitudes, 5u);
    EXPECT_EQ(configured.latitudes, 6u);
    EXPECT_EQ(configured.uv_profile, mesh::CapsuleUvProfile::Fixed);
}

// Bevy's `Capsule3dMeshBuilder::build` region layout and its resulting vertex and
// index counts (`capsule.rs:111-133`, `:288-298`). The counts are the clearest
// check that each region — north polar ring, north hemisphere, north equator,
// optional cylinder, south equator, south hemisphere, south polar ring, south cap
// ring — is written into its own slice of the pool, because one region's stride
// changing shows up directly here.
TEST(MeshPrimitives, Capsule3dMeshRegionLayoutMatchesBevy) {
    const auto counts = [](std::uint32_t rings, std::uint32_t longitudes, std::uint32_t latitudes) {
        const std::uint32_t half_lats     = latitudes / 2u;
        const std::uint32_t half_latsn1   = half_lats - 1u;
        const std::uint32_t half_latsn2   = half_lats - 2u;
        const std::uint32_t ringsp1       = rings + 1u;
        const std::uint32_t lonsp1        = longitudes + 1u;
        const std::uint32_t north_hemi    = longitudes;
        const std::uint32_t north_equator = north_hemi + lonsp1 * half_latsn1;
        const std::uint32_t cylinder      = north_equator + lonsp1;
        const std::uint32_t south_equator = rings > 0 ? cylinder + lonsp1 * rings : cylinder;
        const std::uint32_t south_hemi    = south_equator + lonsp1;
        const std::uint32_t south_polar   = south_hemi + lonsp1 * half_latsn2;
        const std::uint32_t south_cap     = south_polar + lonsp1;
        const std::uint32_t vertices      = south_cap + longitudes;
        const std::uint32_t lons3         = longitudes * 3u;
        const std::uint32_t lons6         = longitudes * 6u;
        const std::uint32_t hemi_indices  = half_latsn1 * lons6;
        const std::uint32_t indices       = lons3 + hemi_indices + ringsp1 * lons6 + hemi_indices + lons3;
        return std::pair{static_cast<std::size_t>(vertices), static_cast<std::size_t>(indices)};
    };

    for (const std::uint32_t rings : {0u, 1u, 4u}) {
        const mesh::Capsule3dMeshBuilder builder = mesh::Capsule3dMeshBuilder{0.5f, 1.0f, 32u, 16u}.with_rings(rings);
        const auto value                         = builder.build();
        const auto* positions =
            value.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
        const auto* normals =
            value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
        const auto* uvs =
            value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
        ASSERT_NE(positions, nullptr);
        ASSERT_NE(normals, nullptr);
        ASSERT_NE(uvs, nullptr);
        const auto expected_vertices = std::get<0>(counts(rings, 32u, 16u));
        const auto expected_indices  = std::get<1>(counts(rings, 32u, 16u));
        EXPECT_EQ(positions->size(), expected_vertices) << "rings=" << rings;
        EXPECT_EQ(normals->size(), expected_vertices) << "rings=" << rings;
        EXPECT_EQ(uvs->size(), expected_vertices) << "rings=" << rings;
        const auto indices = value.indices();
        ASSERT_TRUE(indices.has_value());
        EXPECT_EQ(indices->get().len(), expected_indices) << "rings=" << rings;
        // Every index addresses a real vertex.
        const auto* index_list = indices->get().as_u32();
        ASSERT_NE(index_list, nullptr);
        for (const std::uint32_t index : *index_list) {
            EXPECT_LT(index, expected_vertices) << "rings=" << rings;
        }
    }
    // The default builder (rings = 0) is 592 vertices and 3072 indices.
    EXPECT_EQ(std::get<0>(counts(0u, 32u, 16u)), 592u);
    EXPECT_EQ(std::get<1>(counts(0u, 32u, 16u)), 3072u);
}

// The vertex values Bevy's algorithm fixes: the poles, the two equator rings and
// the three UV profiles (`capsule.rs:144-150`, `:156-197`).
TEST(MeshPrimitives, Capsule3dPolesEquatorsAndUvProfilesMatchBevy) {
    const auto build = [](mesh::CapsuleUvProfile profile, std::uint32_t longitudes) {
        return mesh::Capsule3dMeshBuilder{0.5f, 1.0f, longitudes, 16u}.with_uv_profile(profile).build();
    };
    const auto attribute = [](const mesh::Mesh& value, const auto& id) {
        return value.attribute(id)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    };
    const auto uv_attribute = [](const mesh::Mesh& value) {
        return value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    };

    // `longitudes = 4` puts the ring vertices at 0, 90, 180 and 270 degrees.
    const auto value      = build(mesh::CapsuleUvProfile::Aspect, 4u);
    const auto* positions = attribute(value, mesh::Mesh::ATTRIBUTE_POSITION);
    const auto* normals   = attribute(value, mesh::Mesh::ATTRIBUTE_NORMAL);
    const auto* uvs       = uv_attribute(value);
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);

    // North polar ring: at `half_length + radius` with a +Y normal and the top of
    // the UV range, `s` offset by half a step.
    for (std::size_t j = 0; j < 4; ++j) {
        EXPECT_NEAR((*positions)[j][0], 0.0f, 1e-6f);
        EXPECT_NEAR((*positions)[j][1], 1.0f, 1e-6f);
        EXPECT_NEAR((*positions)[j][2], 0.0f, 1e-6f);
        EXPECT_EQ((*normals)[j], (mesh::VertexAttributeValues::Float32x3Value{0.0f, 1.0f, 0.0f}));
        EXPECT_NEAR((*uvs)[j][1], 1.0f, 1e-6f);
    }
    // South cap ring: the same ring mirrored, with a -Y normal and v = 0. Its
    // offset is the last region: the north polar ring, the north hemisphere, the
    // north equator, the (empty) cylinder, the south equator, the south
    // hemisphere, the south polar ring, then the cap.
    const std::size_t south_cap = 4u + 5u * 7u + 5u + 5u + 5u * 6u + 5u;
    for (std::size_t j = 0; j < 4; ++j) {
        EXPECT_NEAR((*positions)[south_cap + j][1], -1.0f, 1e-6f);
        EXPECT_EQ((*normals)[south_cap + j], (mesh::VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f}));
        EXPECT_NEAR((*uvs)[south_cap + j][1], 0.0f, 1e-6f);
    }

    // Equators: with `rings = 0` the north and south equator rings are adjacent,
    // at +/-half_length, with the radial normal (cos, 0, -sin). The first ring
    // vertex is at angle zero, so it is +X.
    const std::size_t north_equator = 4u + 5u * 7u;
    EXPECT_NEAR((*positions)[north_equator][0], 0.5f, 1e-6f);
    EXPECT_NEAR((*positions)[north_equator][1], 0.5f, 1e-6f);
    EXPECT_NEAR((*positions)[north_equator][2], 0.0f, 1e-6f);
    EXPECT_NEAR((*normals)[north_equator][0], 1.0f, 1e-6f);
    EXPECT_NEAR((*normals)[north_equator][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*normals)[north_equator][2], 0.0f, 1e-6f);
    const std::size_t south_equator = north_equator + 5u;
    EXPECT_NEAR((*positions)[south_equator][1], -0.5f, 1e-6f);

    // The UV profile sets how much of the vertical range the cylinder takes.
    // Aspect: radius / (2 * half_length + 2 * radius) = 0.5 / 2 = 0.25.
    EXPECT_NEAR((*uvs)[north_equator][1], 0.75f, 1e-6f);
    EXPECT_NEAR((*uvs)[south_equator][1], 0.25f, 1e-6f);
    // Uniform: half_lats / (rings + 1 + latitudes) = 8 / 17.
    const auto uniform      = build(mesh::CapsuleUvProfile::Uniform, 4u);
    const auto* uniform_uvs = uv_attribute(uniform);
    ASSERT_NE(uniform_uvs, nullptr);
    EXPECT_NEAR((*uniform_uvs)[north_equator][1], 1.0f - 8.0f / 17.0f, 1e-6f);
    EXPECT_NEAR((*uniform_uvs)[south_equator][1], 8.0f / 17.0f, 1e-6f);
    // Fixed: one third of the range per region.
    const auto fixed      = build(mesh::CapsuleUvProfile::Fixed, 4u);
    const auto* fixed_uvs = uv_attribute(fixed);
    ASSERT_NE(fixed_uvs, nullptr);
    EXPECT_NEAR((*fixed_uvs)[north_equator][1], 2.0f / 3.0f, 1e-6f);
    EXPECT_NEAR((*fixed_uvs)[south_equator][1], 1.0f / 3.0f, 1e-6f);
}

TEST(MeshPrimitives, Measured2dMatchesBevyTraitFormulas) {
    // Bevy measures 2D primitives through the `Measured2d` trait; Epix mirrors it with
    // `Measured2dImpl` plus the free functions `area` and `perimeter`.
    static_assert(mesh::Measured2d<mesh::Circle>);
    static_assert(mesh::Measured2d<mesh::CircularSector>);
    static_assert(mesh::Measured2d<mesh::CircularSegment>);
    static_assert(mesh::Measured2d<mesh::Ellipse>);
    static_assert(mesh::Measured2d<mesh::Annulus>);
    static_assert(mesh::Measured2d<mesh::Rhombus>);
    static_assert(mesh::Measured2d<mesh::Triangle2d>);
    static_assert(mesh::Measured2d<mesh::Rectangle>);
    static_assert(mesh::Measured2d<mesh::RegularPolygon>);
    static_assert(mesh::Measured2d<mesh::Capsule2d>);
    static_assert(mesh::Measured2d<mesh::Plane3d>);
    static_assert(mesh::Measured2d<mesh::Triangle3d>);
    static_assert(mesh::Measured2d<mesh::Ring<mesh::Circle>>);
    // `Cuboid` is only measured in 3D, exactly like in Bevy.
    static_assert(!mesh::Measured2d<mesh::Cuboid>);

    // Circle: `PI * r^2` and `2 * PI * r`.
    EXPECT_NEAR(mesh::area(mesh::Circle{0.5f}), 0.785398163f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(mesh::Circle{0.5f}), 3.141592654f, 1e-6f);

    // CircularSector: the area uses the *half* angle, and the perimeter switches to the full
    // circumference once the sector covers at least half a circle.
    const mesh::CircularSector default_sector{};
    EXPECT_NEAR(mesh::area(default_sector), 0.523598776f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(default_sector), 3.094395102f, 1e-6f);
    const auto minor_sector = mesh::CircularSector::from_radians(0.5f, std::numbers::pi_v<float> / 2.0f);
    EXPECT_NEAR(mesh::area(minor_sector), 0.196349541f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(minor_sector), 1.785398163f, 1e-6f);
    // `half_angle() >= PI` is an inclusive boundary: a full circle uses `2 * PI * r`.
    const auto full_sector = mesh::CircularSector::from_turns(0.5f, 1.0f);
    EXPECT_NEAR(mesh::area(full_sector), 0.785398163f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(full_sector), 3.141592654f, 1e-6f);

    // CircularSegment: `r^2 / 2 * (angle - sin(angle))` and chord plus arc length.
    const mesh::CircularSegment default_segment{};
    EXPECT_NEAR(default_segment.chord_length(), 0.866025404f, 1e-6f);
    EXPECT_NEAR(default_segment.arc_length(), 2.094395102f, 1e-6f);
    EXPECT_NEAR(mesh::area(default_segment), 0.631851951f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(default_segment), 2.960420506f, 1e-6f);

    // Ellipse: `PI * a * b`, plus all three of Bevy's perimeter branches.
    const mesh::Ellipse default_ellipse{};
    EXPECT_FLOAT_EQ(default_ellipse.semi_major(), 1.0f);
    EXPECT_FLOAT_EQ(default_ellipse.semi_minor(), 0.5f);
    EXPECT_NEAR(mesh::area(default_ellipse), 1.570796327f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(default_ellipse), 4.844224110f, 1e-4f);
    // `a == b` short circuits to the circle circumference.
    EXPECT_NEAR(mesh::perimeter(mesh::Ellipse{0.5f, 0.5f}), 3.141592654f, 1e-6f);
    // `a / b > 1e4` treats the ellipse as a line of length `4 * a`.
    EXPECT_FLOAT_EQ(mesh::perimeter(mesh::Ellipse{100.0f, 0.001f}), 400.0f);

    // Annulus: `PI * (outer^2 - inner^2)` and `2 * PI * (outer + inner)`.
    EXPECT_NEAR(mesh::area(mesh::Annulus{}), 2.356194490f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(mesh::Annulus{}), 9.424777961f, 1e-6f);

    // Rhombus: half the diagonal product and four sides.
    EXPECT_NEAR(mesh::area(mesh::Rhombus{0.62f, 0.98f}), 0.3038f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(mesh::Rhombus{0.62f, 0.98f}), 2.319310242f, 1e-6f);

    // Triangle2d: the shoelace area and the side sum.
    EXPECT_FLOAT_EQ(mesh::area(mesh::Triangle2d{}), 0.5f);
    EXPECT_NEAR(mesh::perimeter(mesh::Triangle2d{}), 3.236067977f, 1e-6f);

    // Rectangle and Plane3d share the `4 * half_size` product/sum form.
    EXPECT_FLOAT_EQ(mesh::area(mesh::Rectangle{}), 1.0f);
    EXPECT_FLOAT_EQ(mesh::perimeter(mesh::Rectangle{}), 4.0f);
    EXPECT_FLOAT_EQ(mesh::area(mesh::Plane3d{}), 1.0f);
    EXPECT_FLOAT_EQ(mesh::perimeter(mesh::Plane3d{}), 4.0f);

    // RegularPolygon: area from the circumradius, perimeter from the side length.
    const mesh::RegularPolygon default_polygon{};
    EXPECT_FLOAT_EQ(default_polygon.side_length(), 0.5f);
    EXPECT_NEAR(mesh::area(default_polygon), 0.649519053f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(default_polygon), 3.0f, 1e-6f);

    // Capsule2d: the inner rectangle plus the two half discs.
    EXPECT_NEAR(mesh::area(mesh::Capsule2d{0.5f, 1.0f}), 1.785398163f, 1e-6f);
    EXPECT_NEAR(mesh::perimeter(mesh::Capsule2d{0.5f, 1.0f}), 5.141592654f, 1e-6f);

    // Triangle3d is measured as a 2D shape, as in Bevy.
    EXPECT_FLOAT_EQ(mesh::area(mesh::Triangle3d{}), 0.5f);
    EXPECT_NEAR(mesh::perimeter(mesh::Triangle3d{}), 3.236067977f, 1e-6f);

    // Ring<P> subtracts the inner shape from the outer one and adds the perimeters.
    const auto ring = mesh::Ring<mesh::Circle>::from_primitive_and_thickness(mesh::Circle{0.42f}, 0.18f);
    EXPECT_NEAR(mesh::area(ring), 0.373221207f, 1e-5f);
    EXPECT_NEAR(mesh::perimeter(ring), 4.146902303f, 1e-5f);
}

TEST(MeshPrimitives, Measured3dMatchesBevyTraitFormulas) {
    static_assert(mesh::Measured3d<mesh::Cuboid>);
    static_assert(mesh::Measured3d<mesh::Cylinder>);
    static_assert(mesh::Measured3d<mesh::Capsule3d>);
    static_assert(mesh::Measured3d<mesh::Cone>);
    static_assert(mesh::Measured3d<mesh::ConicalFrustum>);
    static_assert(mesh::Measured3d<mesh::Torus>);
    static_assert(mesh::Measured3d<mesh::Tetrahedron>);

    // Cuboid: `8 * (xy + yz + xz)` and `8 * xyz`.
    EXPECT_FLOAT_EQ(mesh::area(mesh::Cuboid{}), 6.0f);
    EXPECT_FLOAT_EQ(mesh::volume(mesh::Cuboid{}), 1.0f);

    // Cylinder: `2 * PI * r * (r + 2 * half_height)` and `base_area * 2 * half_height`.
    EXPECT_NEAR(mesh::area(mesh::Cylinder{}), 4.712388980f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::Cylinder{}), 0.785398163f, 1e-6f);

    // Capsule3d: `4 * PI * r * (r + half_length)` and `PI * r * diameter * (diameter / 3 + half_length)`.
    EXPECT_NEAR(mesh::area(mesh::Capsule3d{}), 6.283185307f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::Capsule3d{}), 1.308996939f, 1e-6f);

    // Cone: base plus lateral area, and a third of `base_area * height`.
    EXPECT_NEAR(mesh::area(mesh::Cone{}), 2.541601846f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::Cone{}), 0.261799388f, 1e-6f);

    // ConicalFrustum: both bases plus the lateral area, and `PI / 3 * h * (rb^2 + rt^2 + rt * rb)`.
    EXPECT_NEAR(mesh::area(mesh::ConicalFrustum{}), 2.298900466f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::ConicalFrustum{}), 0.229074464f, 1e-6f);

    // Torus: `4 * PI^2 * R * r` and `2 * PI^2 * R * r^2`.
    EXPECT_NEAR(mesh::area(mesh::Torus{}), 7.402203301f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::Torus{}), 0.925275413f, 1e-6f);

    // Tetrahedron: the four face areas over two, and the absolute signed volume. The default
    // tetrahedron is regular with edge length `sqrt(2)`, so the surface is `2 * sqrt(3)` and the
    // volume is `1 / 3`.
    EXPECT_NEAR(mesh::area(mesh::Tetrahedron{}), 3.464101615f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::Tetrahedron{}), 0.333333333f, 1e-6f);
}

// The `Extrudable` trait (`extrusion.rs:76-86`) is a customization point over the twelve Bevy
// `perimeter` implementations in `dim2.rs`. Single closed loops come first: `Circle`/`Ellipse`
// shade smooth and close back onto index 0, while the four polygons shade flat and emit
// `2 * (n - 1)` vertices per layer.
TEST(MeshPrimitives, ExtrudablePerimeterMatchesBevyForClosedLoops) {
    const auto expect_vec2 = [](glm::vec2 actual, glm::vec2 expected) {
        EXPECT_FLOAT_EQ(actual.x, expected.x);
        EXPECT_FLOAT_EQ(actual.y, expected.y);
    };

    // Circle and Ellipse: `(0..resolution).chain([0])`, with `Vec2::Y` at both ends.
    mesh::CircleMeshBuilder circle;
    circle.resolution           = 6;
    const auto circle_perimeter = mesh::perimeter(circle);
    ASSERT_EQ(circle_perimeter.size(), 1u);
    const auto* circle_smooth = std::get_if<mesh::PerimeterSegment::Smooth>(&circle_perimeter[0].value);
    ASSERT_NE(circle_smooth, nullptr);
    EXPECT_EQ(circle_smooth->indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 0}));
    expect_vec2(circle_smooth->first_normal, glm::vec2{0.0f, 1.0f});
    expect_vec2(circle_smooth->last_normal, glm::vec2{0.0f, 1.0f});
    EXPECT_EQ(mesh::vertices_per_layer(circle_perimeter[0]), 7u);
    EXPECT_EQ(mesh::indices_per_segment(circle_perimeter[0]), 36u);

    mesh::EllipseMeshBuilder ellipse;
    ellipse.resolution           = 4;
    const auto ellipse_perimeter = mesh::perimeter(ellipse);
    ASSERT_EQ(ellipse_perimeter.size(), 1u);
    const auto* ellipse_smooth = std::get_if<mesh::PerimeterSegment::Smooth>(&ellipse_perimeter[0].value);
    ASSERT_NE(ellipse_smooth, nullptr);
    EXPECT_EQ(ellipse_smooth->indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 0}));

    // RegularPolygon and ConvexPolygon: `(0..count).chain([0])` as a flat loop.
    mesh::RegularPolygonMeshBuilder polygon;
    polygon.sides                = 5;
    const auto polygon_perimeter = mesh::perimeter(polygon);
    ASSERT_EQ(polygon_perimeter.size(), 1u);
    const auto* polygon_flat = std::get_if<mesh::PerimeterSegment::Flat>(&polygon_perimeter[0].value);
    ASSERT_NE(polygon_flat, nullptr);
    EXPECT_EQ(polygon_flat->indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 0}));
    EXPECT_EQ(mesh::vertices_per_layer(polygon_perimeter[0]), 10u);
    EXPECT_EQ(mesh::indices_per_segment(polygon_perimeter[0]), 30u);

    mesh::ConvexPolygonMeshBuilder convex;
    convex.vertices             = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    const auto convex_perimeter = mesh::perimeter(convex);
    ASSERT_EQ(convex_perimeter.size(), 1u);
    const auto* convex_flat = std::get_if<mesh::PerimeterSegment::Flat>(&convex_perimeter[0].value);
    ASSERT_NE(convex_flat, nullptr);
    EXPECT_EQ(convex_flat->indices, (std::vector<std::uint32_t>{0, 1, 2, 0}));

    // Rhombus and Rectangle: Bevy hard-codes the four-vertex loop.
    const auto rhombus_perimeter = mesh::perimeter(mesh::RhombusMeshBuilder{});
    ASSERT_EQ(rhombus_perimeter.size(), 1u);
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(rhombus_perimeter[0].value).indices,
              (std::vector<std::uint32_t>{0, 1, 2, 3, 0}));
    const auto rectangle_perimeter = mesh::perimeter(mesh::RectangleMeshBuilder{});
    ASSERT_EQ(rectangle_perimeter.size(), 1u);
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(rectangle_perimeter[0].value).indices,
              (std::vector<std::uint32_t>{0, 1, 2, 3, 0}));
}

// The winding-sensitive and multi-segment perimeters. `Triangle2d` follows its own winding order,
// the sector/segment pair emit a flat chord followed by a smooth arc whose end normals come from
// `sin_cos(half_angle)`, `Annulus` emits its inner hole first wound the other way, and `Capsule2d`
// emits four segments in order.
TEST(MeshPrimitives, ExtrudablePerimeterMatchesBevyForWindingAndMultiSegmentShapes) {
    const auto expect_vec2 = [](glm::vec2 actual, glm::vec2 expected) {
        EXPECT_FLOAT_EQ(actual.x, expected.x);
        EXPECT_FLOAT_EQ(actual.y, expected.y);
    };

    // Counter-clockwise triangle: 0, 1, 2, 0; clockwise: 2, 1, 0, 2.
    const auto ccw_perimeter = mesh::perimeter(mesh::Triangle2d{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}}.mesh());
    ASSERT_EQ(ccw_perimeter.size(), 1u);
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(ccw_perimeter[0].value).indices,
              (std::vector<std::uint32_t>{0, 1, 2, 0}));
    const auto cw_perimeter = mesh::perimeter(mesh::Triangle2d{{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}}.mesh());
    ASSERT_EQ(cw_perimeter.size(), 1u);
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(cw_perimeter[0].value).indices,
              (std::vector<std::uint32_t>{2, 1, 0, 2}));

    // CircularSector / CircularSegment: flat chord `[resolution, 0, 1]`, then the arc `1..=resolution`.
    mesh::CircularSectorMeshBuilder sector;
    sector.resolution           = 4;
    const auto sector_perimeter = mesh::perimeter(sector);
    ASSERT_EQ(sector_perimeter.size(), 2u);
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(sector_perimeter[0].value).indices,
              (std::vector<std::uint32_t>{4, 0, 1}));
    const auto* sector_arc = std::get_if<mesh::PerimeterSegment::Smooth>(&sector_perimeter[1].value);
    ASSERT_NE(sector_arc, nullptr);
    EXPECT_EQ(sector_arc->indices, (std::vector<std::uint32_t>{1, 2, 3, 4}));
    const float half_angle = mesh::CircularSector{}.arc.half_angle;
    expect_vec2(sector_arc->first_normal, glm::vec2{std::sin(half_angle), std::cos(half_angle)});
    expect_vec2(sector_arc->last_normal, glm::vec2{-std::sin(half_angle), std::cos(half_angle)});

    mesh::CircularSegmentMeshBuilder segment;
    segment.resolution           = 3;
    const auto segment_perimeter = mesh::perimeter(segment);
    ASSERT_EQ(segment_perimeter.size(), 2u);
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(segment_perimeter[0].value).indices,
              (std::vector<std::uint32_t>{3, 0, 1}));
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Smooth>(segment_perimeter[1].value).indices,
              (std::vector<std::uint32_t>{1, 2, 3}));

    // Annulus: the inner hole is emitted first, stepped by two and reversed, so its indices descend
    // from the closing 0; the outer perimeter steps by two from 1.
    mesh::AnnulusMeshBuilder annulus;
    annulus.resolution           = 4;
    const auto annulus_perimeter = mesh::perimeter(annulus);
    ASSERT_EQ(annulus_perimeter.size(), 2u);
    const auto* inner = std::get_if<mesh::PerimeterSegment::Smooth>(&annulus_perimeter[0].value);
    const auto* outer = std::get_if<mesh::PerimeterSegment::Smooth>(&annulus_perimeter[1].value);
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(inner->indices, (std::vector<std::uint32_t>{0, 6, 4, 2, 0}));
    EXPECT_EQ(outer->indices, (std::vector<std::uint32_t>{1, 3, 5, 7, 1}));
    expect_vec2(inner->first_normal, glm::vec2{0.0f, -1.0f});
    expect_vec2(outer->first_normal, glm::vec2{0.0f, 1.0f});

    // Capsule2d: top semi-circle, left edge, bottom semi-circle, right edge.
    mesh::Capsule2dMeshBuilder capsule;
    capsule.resolution           = 4;
    const auto capsule_perimeter = mesh::perimeter(capsule);
    ASSERT_EQ(capsule_perimeter.size(), 4u);
    const auto* top = std::get_if<mesh::PerimeterSegment::Smooth>(&capsule_perimeter[0].value);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->indices, (std::vector<std::uint32_t>{0, 1, 2, 3}));
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(capsule_perimeter[1].value).indices,
              (std::vector<std::uint32_t>{3, 4}));
    const auto* bottom = std::get_if<mesh::PerimeterSegment::Smooth>(&capsule_perimeter[2].value);
    ASSERT_NE(bottom, nullptr);
    EXPECT_EQ(bottom->indices, (std::vector<std::uint32_t>{4, 5, 6, 7}));
    EXPECT_EQ(std::get<mesh::PerimeterSegment::Flat>(capsule_perimeter[3].value).indices,
              (std::vector<std::uint32_t>{7, 0}));
    expect_vec2(top->first_normal, glm::vec2{1.0f, 0.0f});
    expect_vec2(top->last_normal, glm::vec2{-1.0f, 0.0f});
    expect_vec2(bottom->first_normal, glm::vec2{-1.0f, 0.0f});
    expect_vec2(bottom->last_normal, glm::vec2{1.0f, 0.0f});
}

// `RingMeshBuilder<P>`'s perimeter appends the *inner* perimeter reversed, offset past the outer
// vertices, with its smooth normals negated and swapped (`dim2.rs:1441-1505`).
TEST(MeshPrimitives, ExtrudableRingPerimeterTransformsTheInnerLoop) {
    const auto expect_vec2 = [](glm::vec2 actual, glm::vec2 expected) {
        EXPECT_FLOAT_EQ(actual.x, expected.x);
        EXPECT_FLOAT_EQ(actual.y, expected.y);
    };

    mesh::CircleMeshBuilder outer;
    outer.resolution = 4;
    mesh::CircleMeshBuilder inner;
    inner.resolution    = 4;
    const auto segments = mesh::perimeter(mesh::RingMeshBuilder<mesh::Circle>{outer, inner});

    ASSERT_EQ(segments.size(), 2u);
    const auto* outer_segment = std::get_if<mesh::PerimeterSegment::Smooth>(&segments[0].value);
    const auto* inner_segment = std::get_if<mesh::PerimeterSegment::Smooth>(&segments[1].value);
    ASSERT_NE(outer_segment, nullptr);
    ASSERT_NE(inner_segment, nullptr);
    // The outer loop is untouched: 0..3 then the closing 0.
    EXPECT_EQ(outer_segment->indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 0}));
    // The inner loop is reversed so the solid stays on the right, and offset by the outer vertex
    // count; a circle of resolution four meshes to four vertices.
    EXPECT_EQ(inner_segment->indices, (std::vector<std::uint32_t>{4, 7, 6, 5, 4}));
    expect_vec2(inner_segment->first_normal, glm::vec2{0.0f, -1.0f});
    expect_vec2(inner_segment->last_normal, glm::vec2{0.0f, -1.0f});

    // A ring whose inner and outer meshes have different vertex counts is Bevy's panic case; Epix
    // reports it as the module's typed exception instead.
    mesh::CircleMeshBuilder smaller_inner;
    smaller_inner.resolution = 3;
    EXPECT_THROW(mesh::perimeter(mesh::RingMeshBuilder<mesh::Circle>{outer, smaller_inner}), std::invalid_argument);
}

// Bevy's `ExtrusionBuilder<P>` holds the 2D builder, the half depth and the segment count, and its
// `resolution` method exists for exactly the four shapes whose 2D builder carries a `resolution`
// field (`extrusion.rs:104-173`). `Extrusion<T>::mesh` keeps `half_depth` as it stands, unlike
// `ExtrusionBuilder::new`, which halves a full depth.
TEST(MeshExtrusion, BuilderAndShapeMatchBevyContracts) {
    static_assert(mesh::Meshable<mesh::Extrusion<mesh::Circle>>);
    static_assert(mesh::MeshBuilder<mesh::ExtrusionBuilder<mesh::Circle>>);
    static_assert(mesh::Measured3d<mesh::Extrusion<mesh::Circle>>);
    // `Meshable for Extrusion<P>` needs `P::Output: Extrudable`, so neither of these meshes.
    static_assert(!mesh::Meshable<mesh::Extrusion<mesh::Segment2d>>);
    static_assert(!mesh::Meshable<mesh::Extrusion<mesh::Polyline2d>>);
    static_assert(!mesh::MeshBuilder<mesh::ExtrusionBuilder<mesh::Segment2d>>);

    // Bevy declares the inherent `resolution` method for exactly four shapes.
    static_assert(mesh::ExtrusionResolutionShape<mesh::Circle>);
    static_assert(mesh::ExtrusionResolutionShape<mesh::Ellipse>);
    static_assert(mesh::ExtrusionResolutionShape<mesh::Annulus>);
    static_assert(mesh::ExtrusionResolutionShape<mesh::Capsule2d>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::CircularSector>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::CircularSegment>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::Rectangle>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::RegularPolygon>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::Rhombus>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::Triangle2d>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::ConvexPolygon>);
    static_assert(!mesh::ExtrusionResolutionShape<mesh::Ring<mesh::Circle>>);

    const mesh::ExtrusionBuilder<mesh::Circle> from_new{mesh::Circle{0.5f}, 2.0f};
    EXPECT_FLOAT_EQ(from_new.half_depth, 1.0f);
    EXPECT_EQ(from_new.segments, 1u);
    EXPECT_EQ(from_new.base_builder.resolution, 32u);

    const mesh::Extrusion<mesh::Circle> shape{mesh::Circle{0.25f}, 4.0f};
    EXPECT_FLOAT_EQ(shape.base_shape.radius, 0.25f);
    EXPECT_FLOAT_EQ(shape.half_depth, 2.0f);
    EXPECT_EQ(shape.mesh().half_depth, 2.0f);
    EXPECT_EQ(shape.mesh().segments, 1u);

    // The consuming setters return a configured copy and leave the receiver alone.
    const auto segmented = from_new.with_segments(3);
    EXPECT_EQ(segmented.segments, 3u);
    EXPECT_EQ(from_new.segments, 1u);

    const auto inner_configured = from_new.with_inner([](mesh::CircleMeshBuilder builder) {
        builder.resolution = 8;
        return builder;
    });
    EXPECT_EQ(inner_configured.base_builder.resolution, 8u);
    EXPECT_EQ(from_new.base_builder.resolution, 32u);

    const auto with_resolution = from_new.resolution(6);
    EXPECT_EQ(with_resolution.base_builder.resolution, 6u);
    EXPECT_FLOAT_EQ(with_resolution.half_depth, 1.0f);
    EXPECT_EQ(with_resolution.segments, 1u);

    // Bevy derives `PartialEq` on `Extrusion<T>` and declares no `Default` for either type.
    EXPECT_EQ((mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 2.0f}),
              (mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 2.0f}));
    EXPECT_NE((mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 2.0f}),
              (mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 4.0f}));
    static_assert(!std::default_initializable<mesh::Extrusion<mesh::Circle>>);
    static_assert(!std::default_initializable<mesh::ExtrusionBuilder<mesh::Circle>>);

    // `Measured3d for Extrusion<T>`: `2 * (base.area + half_depth * base.perimeter)` and
    // `2 * base.area * half_depth`.
    EXPECT_NEAR(mesh::area(mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 2.0f}), 7.853981634f, 1e-5f);
    EXPECT_NEAR(mesh::volume(mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 2.0f}), 1.570796327f, 1e-6f);
    EXPECT_FLOAT_EQ(mesh::area(mesh::Extrusion<mesh::Rectangle>{mesh::Rectangle{}, 2.0f}), 10.0f);
    EXPECT_FLOAT_EQ(mesh::volume(mesh::Extrusion<mesh::Rectangle>{mesh::Rectangle{}, 2.0f}), 2.0f);
}

// The caps are the 2D mesh translated to `+half_depth` with its UVs halved, and a copy scaled by
// `(1, 1, -1)` with `u += 0.5` and every triangle's index pair swapped. A single smooth perimeter
// segment then walks one mantel column per perimeter index over two layers, taking its end normals
// from the segment and interpolating the interior ones (`extrusion.rs:343-427`, `478-575`).
TEST(MeshExtrusion, SmoothCircleExtrusionMatchesBevyCapsAndMantel) {
    const auto expect_vec3_near = [](const std::vector<mesh::VertexAttributeValues::Float32x3Value>& actual,
                                     const std::vector<mesh::VertexAttributeValues::Float32x3Value>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t index = 0; index < actual.size(); ++index) {
            EXPECT_NEAR(actual[index][0], expected[index][0], 1e-6f) << "vertex " << index;
            EXPECT_NEAR(actual[index][1], expected[index][1], 1e-6f) << "vertex " << index;
            EXPECT_NEAR(actual[index][2], expected[index][2], 1e-6f) << "vertex " << index;
        }
    };
    const auto expect_vec2_near = [](const std::vector<mesh::VertexAttributeValues::Float32x2Value>& actual,
                                     const std::vector<mesh::VertexAttributeValues::Float32x2Value>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t index = 0; index < actual.size(); ++index) {
            EXPECT_NEAR(actual[index][0], expected[index][0], 1e-6f) << "vertex " << index;
            EXPECT_NEAR(actual[index][1], expected[index][1], 1e-6f) << "vertex " << index;
        }
    };

    // A resolution-4 circle has four rim vertices and a single smooth loop `0, 1, 2, 3, 0`.
    const mesh::Mesh extrusion{mesh::Extrusion<mesh::Circle>{mesh::Circle{0.5f}, 2.0f}.mesh().resolution(4)};
    EXPECT_EQ(extrusion.count_vertices(), 18u);
    ASSERT_NE(extrusion.indices(), std::nullopt);
    EXPECT_TRUE(extrusion.indices()->get().as_u32() != nullptr);
    EXPECT_EQ(extrusion.indices()->get().len(), 36u);

    // Front cap, back cap (mirrored and rewound), then the five mantel columns of two layers each.
    EXPECT_EQ(*extrusion.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{0,  1,  2,  0,  2,  3,  5,  4,  6,  6,  4,  7,  8,  9,  10, 10, 9,  11,
                                          10, 11, 12, 12, 11, 13, 12, 13, 14, 14, 13, 15, 14, 15, 16, 16, 15, 17}));

    const auto* positions =
        extrusion.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        extrusion.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        extrusion.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);

    expect_vec3_near(*positions, {{0.0f, 0.5f, 1.0f},
                                  {-0.5f, 0.0f, 1.0f},
                                  {0.0f, -0.5f, 1.0f},
                                  {0.5f, 0.0f, 1.0f},
                                  {0.0f, 0.5f, -1.0f},
                                  {-0.5f, 0.0f, -1.0f},
                                  {0.0f, -0.5f, -1.0f},
                                  {0.5f, 0.0f, -1.0f},
                                  {0.0f, 0.5f, 1.0f},
                                  {0.0f, 0.5f, -1.0f},
                                  {-0.5f, 0.0f, 1.0f},
                                  {-0.5f, 0.0f, -1.0f},
                                  {0.0f, -0.5f, 1.0f},
                                  {0.0f, -0.5f, -1.0f},
                                  {0.5f, 0.0f, 1.0f},
                                  {0.5f, 0.0f, -1.0f},
                                  {0.0f, 0.5f, 1.0f},
                                  {0.0f, 0.5f, -1.0f}});

    // `Vec2::Y` at both ends of the loop, and the interpolated `(n.y, -n.x)` normals in between.
    expect_vec3_near(*normals, {{0.0f, 0.0f, 1.0f},
                                {0.0f, 0.0f, 1.0f},
                                {0.0f, 0.0f, 1.0f},
                                {0.0f, 0.0f, 1.0f},
                                {0.0f, 0.0f, -1.0f},
                                {0.0f, 0.0f, -1.0f},
                                {0.0f, 0.0f, -1.0f},
                                {0.0f, 0.0f, -1.0f},
                                {0.0f, 1.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f},
                                {-1.0f, 0.0f, 0.0f},
                                {-1.0f, 0.0f, 0.0f},
                                {0.0f, -1.0f, 0.0f},
                                {0.0f, -1.0f, 0.0f},
                                {1.0f, 0.0f, 0.0f},
                                {1.0f, 0.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f},
                                {0.0f, 1.0f, 0.0f}});

    // Cap UVs are halved (`u` shifted by `0.5` on the back), mantel UVs span `(0, 0.5)` to `(1, 1)`.
    expect_vec2_near(*uvs, {{0.25f, 0.0f},
                            {0.0f, 0.25f},
                            {0.25f, 0.5f},
                            {0.5f, 0.25f},
                            {0.75f, 0.0f},
                            {0.5f, 0.25f},
                            {0.75f, 0.5f},
                            {1.0f, 0.25f},
                            {0.0f, 0.5f},
                            {0.0f, 1.0f},
                            {0.25f, 0.5f},
                            {0.25f, 1.0f},
                            {0.5f, 0.5f},
                            {0.5f, 1.0f},
                            {0.75f, 0.5f},
                            {0.75f, 1.0f},
                            {1.0f, 0.5f},
                            {1.0f, 1.0f}});
}

// A flat perimeter segment emits one vertex pair per edge per layer with the edge's own normal, and
// its mantel UVs are distributed across the segment's horizontal band (`extrusion.rs:432-477`).
TEST(MeshExtrusion, FlatRectangleExtrusionMatchesBevyCapsAndMantel) {
    const mesh::Mesh extrusion{mesh::Extrusion<mesh::Rectangle>{mesh::Rectangle{}, 2.0f}};
    EXPECT_EQ(extrusion.count_vertices(), 24u);
    ASSERT_NE(extrusion.indices(), std::nullopt);
    EXPECT_EQ(extrusion.indices()->get().len(), 36u);

    EXPECT_EQ(*extrusion.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{0,  1,  2,  0,  2,  3,  5,  4,  6,  6,  4,  7,  8,  10, 9,  9,  10, 11,
                                          12, 14, 13, 13, 14, 15, 16, 18, 17, 17, 18, 19, 20, 22, 21, 21, 22, 23}));

    const auto* positions =
        extrusion.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        extrusion.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        extrusion.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);

    EXPECT_EQ(*positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                              {0.5f, 0.5f, 1.0f},   {-0.5f, 0.5f, 1.0f},  {-0.5f, -0.5f, 1.0f},  {0.5f, -0.5f, 1.0f},
                              {0.5f, 0.5f, -1.0f},  {-0.5f, 0.5f, -1.0f}, {-0.5f, -0.5f, -1.0f}, {0.5f, -0.5f, -1.0f},
                              {0.5f, 0.5f, 1.0f},   {-0.5f, 0.5f, 1.0f},  {0.5f, 0.5f, -1.0f},   {-0.5f, 0.5f, -1.0f},
                              {-0.5f, 0.5f, 1.0f},  {-0.5f, -0.5f, 1.0f}, {-0.5f, 0.5f, -1.0f},  {-0.5f, -0.5f, -1.0f},
                              {-0.5f, -0.5f, 1.0f}, {0.5f, -0.5f, 1.0f},  {-0.5f, -0.5f, -1.0f}, {0.5f, -0.5f, -1.0f},
                              {0.5f, -0.5f, 1.0f},  {0.5f, 0.5f, 1.0f},   {0.5f, -0.5f, -1.0f},  {0.5f, 0.5f, -1.0f}}));

    // Four flat edges: `+Y`, `-X`, `-Y`, `+X`, two layers each.
    EXPECT_EQ(*normals, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{
                            {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f},
                            {0.0f, 1.0f, 0.0f},  {0.0f, 1.0f, 0.0f},  {0.0f, 1.0f, 0.0f},  {0.0f, 1.0f, 0.0f},
                            {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                            {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                            {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f}}));

    EXPECT_EQ(*uvs, (std::vector<mesh::VertexAttributeValues::Float32x2Value>{
                        {0.5f, 0.0f},  {0.0f, 0.0f},  {0.0f, 0.5f},  {0.5f, 0.5f},  {1.0f, 0.0f},  {0.5f, 0.0f},
                        {0.5f, 0.5f},  {1.0f, 0.5f},  {0.0f, 0.5f},  {0.25f, 0.5f}, {0.0f, 1.0f},  {0.25f, 1.0f},
                        {0.25f, 0.5f}, {0.5f, 0.5f},  {0.25f, 1.0f}, {0.5f, 1.0f},  {0.5f, 0.5f},  {0.75f, 0.5f},
                        {0.5f, 1.0f},  {0.75f, 1.0f}, {0.75f, 0.5f}, {1.0f, 0.5f},  {0.75f, 1.0f}, {1.0f, 1.0f}}));
}

// `segments` grows the mantel to `segments + 1` layers between `+half_depth` and `-half_depth`, and a
// zero half depth merges the two caps without any mantel at all (`extrusion.rs:392-408`). The
// generic `Mesh` conversion is Bevy's `From<Extrusion<P>> for Mesh`.
TEST(MeshExtrusion, SegmentsWalkBevyLayersAndZeroDepthSkipsTheMantel) {
    const mesh::Mesh segmented{mesh::Extrusion<mesh::Rectangle>{mesh::Rectangle{}, 2.0f}.mesh().with_segments(3)};
    EXPECT_EQ(segmented.count_vertices(), 40u);
    ASSERT_NE(segmented.indices(), std::nullopt);
    EXPECT_EQ(segmented.indices()->get().len(), 84u);
    // The first edge's eight mantel vertices: two per layer over four layers, `2 / 3` apart.
    EXPECT_EQ(*segmented.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{0,  1,  2,  0,  2,  3,  5,  4,  6,  6,  4,  7,  8,  10, 9,  9,  10,
                                          11, 10, 12, 11, 11, 12, 13, 12, 14, 13, 13, 14, 15, 16, 18, 17, 17,
                                          18, 19, 18, 20, 19, 19, 20, 21, 20, 22, 21, 21, 22, 23, 24, 26, 25,
                                          25, 26, 27, 26, 28, 27, 27, 28, 29, 28, 30, 29, 29, 30, 31, 32, 34,
                                          33, 33, 34, 35, 34, 36, 35, 35, 36, 37, 36, 38, 37, 37, 38, 39}));

    const auto* positions =
        segmented.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs =
        segmented.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(uvs, nullptr);
    for (std::size_t layer = 0; layer < 4; ++layer) {
        EXPECT_NEAR((*positions)[8 + 2 * layer][2], 1.0f - 2.0f / 3.0f * static_cast<float>(layer), 1e-6f);
        EXPECT_NEAR((*positions)[9 + 2 * layer][2], 1.0f - 2.0f / 3.0f * static_cast<float>(layer), 1e-6f);
        EXPECT_NEAR((*uvs)[8 + 2 * layer][1], 0.5f + 0.5f * static_cast<float>(layer) / 3.0f, 1e-6f);
        EXPECT_NEAR((*uvs)[9 + 2 * layer][1], 0.5f + 0.5f * static_cast<float>(layer) / 3.0f, 1e-6f);
    }
    EXPECT_NEAR((*uvs)[8][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*uvs)[9][0], 0.25f, 1e-6f);

    // A depth of zero still builds both caps, and merges them without a mantel.
    const mesh::Mesh flattened{mesh::Extrusion<mesh::Rectangle>{mesh::Rectangle{}, 0.0f}};
    EXPECT_EQ(flattened.count_vertices(), 8u);
    ASSERT_NE(flattened.indices(), std::nullopt);
    EXPECT_EQ(flattened.indices()->get().len(), 12u);
    EXPECT_EQ(*flattened.indices()->get().as_u32(), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 5, 4, 6, 6, 4, 7}));
    const auto* flattened_positions =
        flattened.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(flattened_positions, nullptr);
    EXPECT_EQ(*flattened_positions, (std::vector<mesh::VertexAttributeValues::Float32x3Value>{{0.5f, 0.5f, 0.0f},
                                                                                              {-0.5f, 0.5f, 0.0f},
                                                                                              {-0.5f, -0.5f, 0.0f},
                                                                                              {0.5f, -0.5f, 0.0f},
                                                                                              {0.5f, 0.5f, 0.0f},
                                                                                              {-0.5f, 0.5f, 0.0f},
                                                                                              {-0.5f, -0.5f, 0.0f},
                                                                                              {0.5f, -0.5f, 0.0f}}));

    // A ring has two smooth perimeter segments: five columns each over two layers, so its mantel adds
    // `2 * 5 * 2` vertices and `2 * 6 * 4` indices to the two copies of the cap mesh. `Extrusion` has
    // no `resolution` method for a ring, so both builders are configured through `with_inner`.
    const auto with_resolution = [](mesh::CircleMeshBuilder circle) {
        circle.resolution = 4;
        return circle;
    };
    const mesh::Ring<mesh::Circle> ring{mesh::Circle{0.5f}, mesh::Circle{0.25f}};
    const mesh::Mesh cap = ring.mesh().with_inner(with_resolution).build();
    const mesh::Mesh ring_extrusion{mesh::Extrusion<mesh::Ring<mesh::Circle>>{ring, 2.0f}.mesh().with_inner(
        [&with_resolution](mesh::RingMeshBuilder<mesh::Circle> ring_builder) {
            return ring_builder.with_inner(with_resolution);
        })};
    ASSERT_NE(cap.indices(), std::nullopt);
    EXPECT_EQ(ring_extrusion.count_vertices(), 2 * cap.count_vertices() + 20u);
    EXPECT_EQ(ring_extrusion.indices()->get().len(), 2 * cap.indices()->get().len() + 48u);
}

// Bevy's `Sphere` is a radius with a `0.5` default, a `diameter`, a `closest_point` that returns the
// point itself when it is inside, and `Measured3d` (`bevy_math/src/primitives/dim3.rs:30-100`).
TEST(MeshSphere, ShapeAndBuilderMatchBevyContracts) {
    static_assert(mesh::Meshable<mesh::Sphere>);
    static_assert(mesh::MeshBuilder<mesh::SphereMeshBuilder>);
    static_assert(mesh::Measured3d<mesh::Sphere>);

    EXPECT_FLOAT_EQ(mesh::Sphere{}.radius, 0.5f);
    EXPECT_FLOAT_EQ(mesh::Sphere{2.0f}.diameter(), 4.0f);
    EXPECT_EQ(mesh::Sphere{1.0f}, mesh::Sphere{1.0f});
    EXPECT_NE(mesh::Sphere{1.0f}, mesh::Sphere{2.0f});

    // `4 * PI * r^2` and `4 / 3 * PI * r^3`.
    EXPECT_NEAR(mesh::area(mesh::Sphere{0.5f}), 3.141592654f, 1e-6f);
    EXPECT_NEAR(mesh::volume(mesh::Sphere{0.5f}), 0.523598776f, 1e-6f);
    EXPECT_NEAR(mesh::area(mesh::Sphere{1.0f}), 12.566370614f, 1e-5f);
    EXPECT_NEAR(mesh::volume(mesh::Sphere{1.0f}), 4.188790205f, 1e-5f);

    // Inside the sphere the point is returned unchanged; outside it is projected onto the surface.
    EXPECT_EQ(mesh::Sphere{1.0f}.closest_point(glm::vec3{0.25f, 0.0f, 0.0f}), (glm::vec3{0.25f, 0.0f, 0.0f}));
    const glm::vec3 projected = mesh::Sphere{1.0f}.closest_point(glm::vec3{3.0f, 4.0f, 0.0f});
    EXPECT_NEAR(projected.x, 0.6f, 1e-6f);
    EXPECT_NEAR(projected.y, 0.8f, 1e-6f);
    EXPECT_NEAR(projected.z, 0.0f, 1e-6f);
    EXPECT_NEAR(glm::length(projected), 1.0f, 1e-6f);

    // `SphereKind` defaults to `Ico { subdivisions: 5 }`, and `Sphere::mesh` keeps that default.
    const mesh::SphereKind default_kind{};
    EXPECT_TRUE(std::holds_alternative<mesh::SphereKind::Ico>(default_kind.value));
    EXPECT_EQ(std::get<mesh::SphereKind::Ico>(default_kind.value).subdivisions, 5u);
    EXPECT_EQ(mesh::SphereKind{}, (mesh::SphereKind{mesh::SphereKind::Ico{5}}));

    const auto builder = mesh::Sphere{0.75f}.mesh();
    EXPECT_FLOAT_EQ(builder.sphere.radius, 0.75f);
    EXPECT_EQ(builder.kind, default_kind);

    const auto from_new = mesh::SphereMeshBuilder{2.0f, mesh::SphereKind{mesh::SphereKind::Uv{8, 6}}};
    EXPECT_FLOAT_EQ(from_new.sphere.radius, 2.0f);
    EXPECT_EQ(std::get<mesh::SphereKind::Uv>(from_new.kind.value).sectors, 8u);
    EXPECT_EQ(std::get<mesh::SphereKind::Uv>(from_new.kind.value).stacks, 6u);

    const auto kinded =
        mesh::SphereMeshBuilder{1.0f, mesh::SphereKind{}}.with_kind(mesh::SphereKind{mesh::SphereKind::Uv{4, 3}});
    EXPECT_EQ(std::get<mesh::SphereKind::Uv>(kinded.kind.value).sectors, 4u);
}

// Bevy's `uv` sphere walks `stacks + 1` rings of `sectors + 1` vertices (the seam is duplicated),
// pushes positions/normals/UVs in that order, and emits the two triangles per quad except at the
// poles (`sphere.rs:168-231`).
TEST(MeshSphere, UvSphereMatchesBevyRingsAndIndexLayout) {
    const mesh::Mesh uv = mesh::Sphere{1.0f}.mesh().with_kind(mesh::SphereKind{mesh::SphereKind::Uv{4, 3}}).build();

    EXPECT_EQ(uv.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(uv.count_vertices(), 20u);
    ASSERT_NE(uv.indices(), std::nullopt);
    EXPECT_EQ(uv.indices()->get().len(), 48u);

    const auto* positions =
        uv.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals =
        uv.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* uvs = uv.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(positions, nullptr);
    ASSERT_NE(normals, nullptr);
    ASSERT_NE(uvs, nullptr);

    // Stack 0 sits at the north pole, stack 1 at `PI / 6`, so `z = 1` then `z = 0.5` with an XY radius
    // of `sqrt(3) / 2`. The last vertex of each ring repeats the first, with `u = 1`.
    EXPECT_NEAR((*positions)[0][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[0][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[0][2], 1.0f, 1e-6f);
    EXPECT_NEAR((*positions)[5][0], 0.86602540f, 1e-6f);
    EXPECT_NEAR((*positions)[5][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[5][2], 0.5f, 1e-6f);
    EXPECT_NEAR((*positions)[6][0], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[6][1], 0.86602540f, 1e-6f);
    EXPECT_NEAR((*positions)[9][0], 0.86602540f, 1e-6f);
    EXPECT_NEAR((*positions)[9][1], 0.0f, 1e-6f);
    EXPECT_NEAR((*positions)[10][2], -0.5f, 1e-6f);
    EXPECT_NEAR((*positions)[15][2], -1.0f, 1e-6f);
    EXPECT_NEAR((*positions)[19][2], -1.0f, 1e-6f);

    // A unit sphere: the normal is the position itself.
    for (std::size_t index = 0; index < positions->size(); ++index) {
        EXPECT_NEAR((*normals)[index][0], (*positions)[index][0], 1e-6f);
        EXPECT_NEAR((*normals)[index][1], (*positions)[index][1], 1e-6f);
        EXPECT_NEAR((*normals)[index][2], (*positions)[index][2], 1e-6f);
    }

    const std::vector<mesh::VertexAttributeValues::Float32x2Value> expected_uvs{
        {0.0f, 0.0f},        {0.25f, 0.0f},        {0.5f, 0.0f},        {0.75f, 0.0f},        {1.0f, 0.0f},
        {0.0f, 1.0f / 3.0f}, {0.25f, 1.0f / 3.0f}, {0.5f, 1.0f / 3.0f}, {0.75f, 1.0f / 3.0f}, {1.0f, 1.0f / 3.0f},
        {0.0f, 2.0f / 3.0f}, {0.25f, 2.0f / 3.0f}, {0.5f, 2.0f / 3.0f}, {0.75f, 2.0f / 3.0f}, {1.0f, 2.0f / 3.0f},
        {0.0f, 1.0f},        {0.25f, 1.0f},        {0.5f, 1.0f},        {0.75f, 1.0f},        {1.0f, 1.0f}};
    ASSERT_EQ(uvs->size(), expected_uvs.size());
    for (std::size_t index = 0; index < expected_uvs.size(); ++index) {
        EXPECT_NEAR((*uvs)[index][0], expected_uvs[index][0], 1e-6f) << "vertex " << index;
        EXPECT_NEAR((*uvs)[index][1], expected_uvs[index][1], 1e-6f) << "vertex " << index;
    }

    // The north pole ring contributes only the lower triangle, the south pole ring only the upper one.
    EXPECT_EQ(*uv.indices()->get().as_u32(),
              (std::vector<std::uint32_t>{1,  5,  6,  2,  6,  7,  3,  7,  8,  4,  8,  9,  5,  10, 6,  6,
                                          10, 11, 6,  11, 7,  7,  11, 12, 7,  12, 8,  8,  12, 13, 8,  13,
                                          9,  9,  13, 14, 10, 15, 11, 11, 16, 12, 12, 17, 13, 13, 18, 14}));
}

// Bevy rejects `subdivisions >= 80` before generating anything, with the vertex count its own
// derivation produces. Below that the `hexasphere` 16.0.0 generator is still unported (B48b).
TEST(MeshSphere, IcosphereGuardMatchesBevyErrorText) {
    const mesh::SphereMeshBuilder builder{1.0f, mesh::SphereKind{mesh::SphereKind::Ico{80}}};
    const auto rejected = builder.ico(80);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().subdivisions, 80u);
    EXPECT_EQ(rejected.error().number_of_resulting_points, 65612u);
    EXPECT_EQ(rejected.error().to_string(),
              "Cannot create an icosphere of 80 subdivisions due to there being too many vertices being generated: "
              "65612. (Limited to 65535 vertices or 79 subdivisions)");
    EXPECT_EQ(rejected.error(), (mesh::IcosphereError{.subdivisions = 80, .number_of_resulting_points = 65612}));
    EXPECT_EQ(builder.ico(100).error().number_of_resulting_points, 102012u);

    // `build` unwraps the fallible constructor, as Bevy's does, so the error surfaces as an exception.
    EXPECT_THROW(builder.build(), std::runtime_error);
    // Below the guard the generator itself runs, so the same builder meshes at 79 subdivisions.
    EXPECT_TRUE(builder.ico(79).has_value());
}

// Bevy's `ico` is `hexasphere::shapes::IcoSphere::new(subdivisions, interpolator)`, so the C++ port
// in `primitives.cpp` must reproduce that crate's 16.0.0 vertex, UV and index order. The expected
// data below is a recorded dump of the real crate (the `tmp/hexa-fixture` Rust project, which
// depends on `hexasphere = "=16.0.0"`), not a reimplementation of it.
TEST(MeshSphere, IcosphereMatchesHexasphere16PointUvAndIndexOrder) {
    const auto expect_points_near = [](const std::vector<mesh::VertexAttributeValues::Float32x3Value>& actual,
                                       const std::vector<mesh::VertexAttributeValues::Float32x3Value>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t index = 0; index < actual.size(); ++index) {
            EXPECT_NEAR(actual[index][0], expected[index][0], 1e-6f) << "vertex " << index;
            EXPECT_NEAR(actual[index][1], expected[index][1], 1e-6f) << "vertex " << index;
            EXPECT_NEAR(actual[index][2], expected[index][2], 1e-6f) << "vertex " << index;
        }
    };
    const auto expect_uvs_near = [](const std::vector<mesh::VertexAttributeValues::Float32x2Value>& actual,
                                    const std::vector<mesh::VertexAttributeValues::Float32x2Value>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t index = 0; index < actual.size(); ++index) {
            EXPECT_NEAR(actual[index][0], expected[index][0], 1e-6f) << "vertex " << index;
            EXPECT_NEAR(actual[index][1], expected[index][1], 1e-6f) << "vertex " << index;
        }
    };
    const auto uvs_of = [](const mesh::Mesh& value) {
        return *value.attribute(mesh::Mesh::ATTRIBUTE_UV_0)->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    };

    // Subdivision 0 is the bare icosahedron: 12 points and 20 triangles.
    const auto ico0 = mesh::Sphere{1.0f}.mesh().ico(0);
    ASSERT_TRUE(ico0.has_value());
    EXPECT_EQ(ico0->count_vertices(), 12u);
    ASSERT_NE(ico0->indices(), std::nullopt);
    EXPECT_EQ(ico0->indices()->get().len(), 60u);
    const auto* positions0 =
        ico0->attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    const auto* normals0 =
        ico0->attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions0, nullptr);
    ASSERT_NE(normals0, nullptr);
    const std::vector<mesh::VertexAttributeValues::Float32x3Value> expected_points0{
        {0.000000000f, 1.000000000f, 0.000000000f},    {0.894427180f, 0.447213590f, 0.000000000f},
        {0.276393205f, 0.447213590f, 0.850650787f},    {-0.723606825f, 0.447213590f, 0.525731087f},
        {-0.723606825f, 0.447213590f, -0.525731087f},  {0.276393205f, 0.447213590f, -0.850650787f},
        {0.723606825f, -0.447213590f, -0.525731087f},  {0.723606825f, -0.447213590f, 0.525731087f},
        {-0.276393205f, -0.447213590f, 0.850650787f},  {-0.894427180f, -0.447213590f, 0.000000000f},
        {-0.276393205f, -0.447213590f, -0.850650787f}, {0.000000000f, -1.000000000f, 0.000000000f},
    };
    expect_points_near(*positions0, expected_points0);
    // A unit sphere carries the raw points as its normals.
    expect_points_near(*normals0, expected_points0);
    const std::vector<mesh::VertexAttributeValues::Float32x2Value> expected_uvs0{
        {0.500000000f, 0.000000000f}, {0.500000000f, 0.352416396f}, {0.300000012f, 0.352416396f},
        {0.099999994f, 0.352416396f}, {0.899999976f, 0.352416396f}, {0.699999988f, 0.352416396f},
        {0.600000024f, 0.647583544f}, {0.400000006f, 0.647583544f}, {0.199999988f, 0.647583544f},
        {0.000000000f, 0.647583544f}, {0.800000012f, 0.647583544f}, {0.500000000f, 1.000000000f},
    };
    expect_uvs_near(uvs_of(*ico0), expected_uvs0);
    EXPECT_EQ(*ico0->indices()->get().as_u32(),
              (std::vector<std::uint32_t>{
                  0, 2, 1,  0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 1,  5, 5,  1, 6,  1, 2, 7,  2, 3, 8,  3, 4, 9,  4, 5,  10,
                  5, 6, 10, 1, 7, 6, 2, 8, 7, 3, 9, 8, 4, 10, 9, 10, 6, 11, 6, 7, 11, 7, 8, 11, 8, 9, 11, 9, 10, 11,
              }));

    // Subdivision 1 adds one point per edge and one interior point per face: 42 points, 80 faces.
    const auto ico1 = mesh::Sphere{1.0f}.mesh().ico(1);
    ASSERT_TRUE(ico1.has_value());
    EXPECT_EQ(ico1->count_vertices(), 42u);
    ASSERT_NE(ico1->indices(), std::nullopt);
    EXPECT_EQ(ico1->indices()->get().len(), 240u);
    const auto* positions1 =
        ico1->attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(positions1, nullptr);
    const std::vector<mesh::VertexAttributeValues::Float32x3Value> expected_points1{
        {0.000000000f, 1.000000000f, 0.000000000f},    {0.894427180f, 0.447213590f, 0.000000000f},
        {0.276393205f, 0.447213590f, 0.850650787f},    {-0.723606825f, 0.447213590f, 0.525731087f},
        {-0.723606825f, 0.447213590f, -0.525731087f},  {0.276393205f, 0.447213590f, -0.850650787f},
        {0.723606825f, -0.447213590f, -0.525731087f},  {0.723606825f, -0.447213590f, 0.525731087f},
        {-0.276393205f, -0.447213590f, 0.850650787f},  {-0.894427180f, -0.447213590f, 0.000000000f},
        {-0.276393205f, -0.447213590f, -0.850650787f}, {0.000000000f, -1.000000000f, 0.000000000f},
        {0.162459850f, 0.850650787f, 0.499999970f},    {-0.425325423f, 0.850650787f, 0.309016973f},
        {-0.425325423f, 0.850650787f, -0.309016973f},  {0.162459850f, 0.850650787f, -0.499999970f},
        {0.525731087f, 0.850650787f, 0.000000000f},    {0.688190937f, 0.525731087f, 0.499999970f},
        {-0.262865573f, 0.525731087f, 0.809016943f},   {-0.850650847f, 0.525731087f, 0.000000000f},
        {-0.262865573f, 0.525731087f, -0.809016943f},  {0.688190937f, 0.525731087f, -0.499999970f},
        {0.951056480f, 0.000000000f, -0.309016973f},   {0.587785244f, 0.000000000f, 0.809016943f},
        {-0.587785244f, 0.000000000f, 0.809016943f},   {-0.951056480f, 0.000000000f, -0.309016973f},
        {0.000000000f, 0.000000000f, -0.999999940f},   {0.587785244f, 0.000000000f, -0.809016943f},
        {0.951056480f, 0.000000000f, 0.309016973f},    {0.000000000f, 0.000000000f, 0.999999940f},
        {-0.951056480f, 0.000000000f, 0.309016973f},   {-0.587785244f, 0.000000000f, -0.809016943f},
        {0.262865573f, -0.525731087f, -0.809016943f},  {0.850650847f, -0.525731087f, 0.000000000f},
        {0.262865573f, -0.525731087f, 0.809016943f},   {-0.688190937f, -0.525731087f, 0.499999970f},
        {-0.688190937f, -0.525731087f, -0.499999970f}, {-0.162459850f, -0.850650787f, -0.499999970f},
        {0.425325423f, -0.850650787f, -0.309016973f},  {0.425325423f, -0.850650787f, 0.309016973f},
        {-0.162459850f, -0.850650787f, 0.499999970f},  {-0.525731087f, -0.850650787f, 0.000000000f},
    };
    expect_points_near(*positions1, expected_points1);
    const std::vector<mesh::VertexAttributeValues::Float32x2Value> expected_uvs1{
        {0.500000000f, 0.000000000f}, {0.500000000f, 0.352416396f}, {0.300000012f, 0.352416396f},
        {0.099999994f, 0.352416396f}, {0.899999976f, 0.352416396f}, {0.699999988f, 0.352416396f},
        {0.600000024f, 0.647583544f}, {0.400000006f, 0.647583544f}, {0.199999988f, 0.647583544f},
        {0.000000000f, 0.647583544f}, {0.800000012f, 0.647583544f}, {0.500000000f, 1.000000000f},
        {0.300000012f, 0.176208198f}, {0.099999994f, 0.176208198f}, {0.899999976f, 0.176208198f},
        {0.699999988f, 0.176208198f}, {0.500000000f, 0.176208198f}, {0.400000006f, 0.323791832f},
        {0.199999988f, 0.323791832f}, {0.000000000f, 0.323791832f}, {0.800000012f, 0.323791832f},
        {0.600000024f, 0.323791832f}, {0.550000012f, 0.500000000f}, {0.350000024f, 0.500000000f},
        {0.150000006f, 0.500000000f}, {0.949999988f, 0.500000000f}, {0.750000000f, 0.500000000f},
        {0.649999976f, 0.500000000f}, {0.449999988f, 0.500000000f}, {0.250000000f, 0.500000000f},
        {0.050000012f, 0.500000000f}, {0.850000024f, 0.500000000f}, {0.699999988f, 0.676208138f},
        {0.500000000f, 0.676208138f}, {0.300000012f, 0.676208138f}, {0.099999994f, 0.676208138f},
        {0.899999976f, 0.676208138f}, {0.800000012f, 0.823791742f}, {0.600000024f, 0.823791742f},
        {0.400000006f, 0.823791742f}, {0.199999988f, 0.823791742f}, {0.000000000f, 0.823791742f},
    };
    expect_uvs_near(uvs_of(*ico1), expected_uvs1);
    EXPECT_EQ(
        *ico1->indices()->get().as_u32(),
        (std::vector<std::uint32_t>{
            0,  12, 16, 2,  17, 12, 1,  16, 17, 12, 17, 16, 0,  13, 12, 3,  18, 13, 2,  12, 18, 13, 18, 12, 0,  14, 13,
            4,  19, 14, 3,  13, 19, 14, 19, 13, 0,  15, 14, 5,  20, 15, 4,  14, 20, 15, 20, 14, 0,  16, 15, 1,  21, 16,
            5,  15, 21, 16, 21, 15, 5,  21, 27, 1,  22, 21, 6,  27, 22, 21, 22, 27, 1,  17, 28, 2,  23, 17, 7,  28, 23,
            17, 23, 28, 2,  18, 29, 3,  24, 18, 8,  29, 24, 18, 24, 29, 3,  19, 30, 4,  25, 19, 9,  30, 25, 19, 25, 30,
            4,  20, 31, 5,  26, 20, 10, 31, 26, 20, 26, 31, 5,  27, 26, 6,  32, 27, 10, 26, 32, 27, 32, 26, 1,  28, 22,
            7,  33, 28, 6,  22, 33, 28, 33, 22, 2,  29, 23, 8,  34, 29, 7,  23, 34, 29, 34, 23, 3,  30, 24, 9,  35, 30,
            8,  24, 35, 30, 35, 24, 4,  31, 25, 10, 36, 31, 9,  25, 36, 31, 36, 25, 10, 32, 37, 6,  38, 32, 11, 37, 38,
            32, 38, 37, 6,  33, 38, 7,  39, 33, 11, 38, 39, 33, 39, 38, 7,  34, 39, 8,  40, 34, 11, 39, 40, 34, 40, 39,
            8,  35, 40, 9,  41, 35, 11, 40, 41, 35, 41, 40, 9,  36, 41, 10, 37, 36, 11, 41, 37, 36, 37, 41,
        }));

    // Deeper subdivisions keep the counts the crate produces: `12 + 30s + 20 * (s(s-1)/2)` points
    // and `60 * (s + 1)^2` indices, which is also Bevy's `10(s + 1)^2 + 2` vertex derivation.
    for (const auto& [subdivisions, vertices, indices] :
         std::vector<std::array<std::uint32_t, 3>>{{2, 92, 540}, {3, 162, 960}, {4, 252, 1500}, {5, 362, 2160}}) {
        const auto mesh_value = mesh::Sphere{1.0f}.mesh().ico(subdivisions);
        ASSERT_TRUE(mesh_value.has_value()) << subdivisions;
        EXPECT_EQ(mesh_value->count_vertices(), vertices) << subdivisions;
        EXPECT_EQ(mesh_value->indices()->get().len(), indices) << subdivisions;
    }
}

// Bevy's `compute_area_weighted_normals` tests (`mesh.rs:2683` and `mesh.rs:2718`): the sum of the
// double-area face normals already carries each triangle's area, so a larger triangle skews the
// normals of its vertices further.
TEST(MeshAlgorithms, ComputeAreaWeightedNormalsMatchesBevy) {
    const auto expect_near3 = [](const mesh::VertexAttributeValues::Float32x3Value& actual, glm::vec3 expected) {
        for (glm::length_t index = 0; index < 3; ++index) EXPECT_NEAR(actual[index], expected[index], 1e-6f);
    };
    const auto normals_of = [](const mesh::Mesh& value) {
        return value.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    };

    mesh::Mesh unit(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    unit.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    unit.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}});
    unit.compute_area_weighted_normals();

    const auto* normals = normals_of(unit);
    ASSERT_NE(normals, nullptr);
    ASSERT_EQ(normals->size(), 4);
    expect_near3((*normals)[0], glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
    expect_near3((*normals)[1], glm::vec3{0.0f, 0.0f, 1.0f});
    expect_near3((*normals)[2], glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
    expect_near3((*normals)[3], glm::vec3{1.0f, 0.0f, 0.0f});

    // The same two triangles with the first one twice as wide: vertex 1 keeps the first face's
    // normal, while vertices 0 and 2 lean towards it because that face is now twice the area.
    mesh::Mesh proportionate(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    proportionate.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    proportionate.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}});
    proportionate.compute_area_weighted_normals();

    const auto* proportionate_normals = normals_of(proportionate);
    ASSERT_NE(proportionate_normals, nullptr);
    ASSERT_EQ(proportionate_normals->size(), 4);
    expect_near3((*proportionate_normals)[0], glm::normalize(glm::vec3{1.0f, 0.0f, 2.0f}));
    expect_near3((*proportionate_normals)[1], glm::vec3{0.0f, 0.0f, 1.0f});
    expect_near3((*proportionate_normals)[2], glm::normalize(glm::vec3{1.0f, 0.0f, 2.0f}));
    expect_near3((*proportionate_normals)[3], glm::vec3{1.0f, 0.0f, 0.0f});
}

// Bevy's `compute_angle_weighted_normals` test (`mesh.rs:2753`) over an indexed cuboid whose
// corners are shared by three faces: weighting by corner angle, not area, is what makes every
// component of every normal `1 / sqrt(3)`.
TEST(MeshAlgorithms, ComputeSmoothNormalsMatchesBevy) {
    mesh::Mesh cuboid(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    cuboid.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{1.0f, 1.0f, 1.0f},
                                                                                   {-1.0f, 1.0f, 1.0f},
                                                                                   {-1.0f, -1.0f, 1.0f},
                                                                                   {1.0f, -1.0f, 1.0f},
                                                                                   {1.0f, 1.0f, -1.0f},
                                                                                   {-1.0f, 1.0f, -1.0f},
                                                                                   {-1.0f, -1.0f, -1.0f},
                                                                                   {1.0f, -1.0f, -1.0f}});
    cuboid.insert_indices(mesh::Indices{std::vector<std::uint16_t>{
        0, 1, 2, 2, 3, 0,  // front
        5, 4, 7, 7, 6, 5,  // back
        1, 5, 6, 6, 2, 1,  // left
        4, 0, 3, 3, 7, 4,  // right
        4, 5, 1, 1, 0, 4,  // top
        3, 2, 6, 6, 7, 3,  // bottom
    }});
    cuboid.compute_smooth_normals();

    const auto* normals =
        cuboid.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(normals, nullptr);
    ASSERT_EQ(normals->size(), 8);
    constexpr float kFrac1Sqrt3 = 0.57735026f;
    for (const auto& normal : *normals) {
        for (const float component : normal) EXPECT_NEAR(std::abs(component), kFrac1Sqrt3, 1e-6f);
    }

    // Angle weighting is not area weighting: with a wider first triangle the two disagree about
    // where the shared corners point, because the corner angles stay 90 degrees while the areas
    // no longer match.
    mesh::Mesh proportionate(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    proportionate.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    proportionate.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}});
    proportionate.compute_smooth_normals();

    const auto* smooth =
        proportionate.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(smooth, nullptr);
    ASSERT_EQ(smooth->size(), 4);
    const auto expect_near3 = [](const mesh::VertexAttributeValues::Float32x3Value& actual, glm::vec3 expected) {
        for (glm::length_t index = 0; index < 3; ++index) EXPECT_NEAR(actual[index], expected[index], 1e-6f);
    };
    expect_near3((*smooth)[0], glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
    expect_near3((*smooth)[1], glm::vec3{0.0f, 0.0f, 1.0f});
    // The shared corner 2 subtends 90 degrees in the second triangle and `acos(1 / sqrt(5))` in the
    // first, so angle weighting puts it between the two face normals - not at the area-weighted
    // `normalize(1, 0, 2)` direction the area-weighted test above expects.
    expect_near3((*smooth)[2],
                 glm::normalize(glm::vec3{glm::pi<float>() / 4.0f, 0.0f, std::acos(1.0f / std::sqrt(5.0f))}));
    expect_near3((*smooth)[3], glm::vec3{1.0f, 0.0f, 0.0f});
}

// Flat normals are one normal per face, repeated for that face's three vertices, and `compute_normals`
// picks them exactly when the mesh has no index buffer.
TEST(MeshAlgorithms, ComputeFlatNormalsAndDispatchMatchBevy) {
    const auto expect_near3 = [](const mesh::VertexAttributeValues::Float32x3Value& actual, glm::vec3 expected) {
        for (glm::length_t index = 0; index < 3; ++index) EXPECT_NEAR(actual[index], expected[index], 1e-6f);
    };
    const auto positions = std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                                  {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

    mesh::Mesh flat(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    flat.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions);
    flat.compute_normals();

    const auto* normals =
        flat.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(normals, nullptr);
    ASSERT_EQ(normals->size(), 6);
    for (const std::size_t index : {0, 1, 2}) expect_near3((*normals)[index], glm::vec3{0.0f, 0.0f, 1.0f});
    for (const std::size_t index : {3, 4, 5}) expect_near3((*normals)[index], glm::vec3{1.0f, 0.0f, 0.0f});

    // The consuming forms return the same mesh the in-place forms do.
    mesh::Mesh consumed = std::move(flat).with_computed_normals();
    EXPECT_EQ(consumed.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)
                  ->get()
                  .get_if<mesh::VertexAttributeValues::Float32x3>()
                  ->size(),
              6);

    // Indexed input routes `compute_normals` to smooth normals; the same vertices without indices
    // are the flat case above.
    mesh::Mesh indexed(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    indexed.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}});
    indexed.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}});
    indexed.compute_normals();

    const auto* indexed_normals =
        indexed.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    ASSERT_NE(indexed_normals, nullptr);
    ASSERT_EQ(indexed_normals->size(), 4);
    expect_near3((*indexed_normals)[0], glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
    expect_near3((*indexed_normals)[1], glm::vec3{0.0f, 0.0f, 1.0f});
    expect_near3((*indexed_normals)[3], glm::vec3{1.0f, 0.0f, 0.0f});
}

// Normal computed normals follow the same extraction rule as every other accessor (M9).
TEST(MeshAlgorithms, ComputeNormalsReportsBevyAccessErrors) {
    mesh::Mesh extracted(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    extracted.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                               std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
    ASSERT_TRUE(extracted.take_gpu_data().has_value());

    for (const auto& result :
         {extracted.try_compute_normals(), extracted.try_compute_flat_normals(), extracted.try_compute_smooth_normals(),
          extracted.try_compute_area_weighted_normals(), extracted.try_normalize_joint_weights()}) {
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    }
    EXPECT_THROW(extracted.compute_normals(), std::logic_error);
    EXPECT_THROW(extracted.normalize_joint_weights(), std::logic_error);
}

// Joint weights are clamped at zero, normalised to a sum of one, and an all-zero set hands the
// vertex to the first joint (Bevy `Mesh::normalize_joint_weights`).
TEST(MeshAlgorithms, NormalizeJointWeightsMatchesBevy) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    value.insert_attribute(
        mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT,
        std::vector<glm::vec4>{
            {0.5f, 0.5f, 0.0f, 0.0f}, {-1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 0.0f, 0.0f}});
    value.normalize_joint_weights();

    const auto* weights =
        value.attribute(mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT)->get().get_if<mesh::VertexAttributeValues::Float32x4>();
    ASSERT_NE(weights, nullptr);
    ASSERT_EQ(weights->size(), 4);
    EXPECT_EQ((*weights)[0], (mesh::VertexAttributeValues::Float32x4Value{0.5f, 0.5f, 0.0f, 0.0f}));
    EXPECT_EQ((*weights)[1],
              (mesh::VertexAttributeValues::Float32x4Value{0.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f}));
    EXPECT_EQ((*weights)[2], (mesh::VertexAttributeValues::Float32x4Value{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ((*weights)[3], (mesh::VertexAttributeValues::Float32x4Value{0.5f, 0.5f, 0.0f, 0.0f}));
}

// Bevy's `take_gpu_data_calculates_aabb` test (`mesh.rs:2883-2903`): the extents are stored while
// the position data is still readable, because they cannot be computed after extraction.
TEST(MeshAlgorithms, TakeGpuDataCalculatesFinalAabb) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        std::vector<glm::vec3>{{-0.5f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {-0.5f, -1.0f, 0.0f}});
    value.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 2, 3, 0}});
    EXPECT_FALSE(value.final_aabb.has_value());

    auto taken = value.take_gpu_data();
    ASSERT_TRUE(taken.has_value());
    // Bevy assigns the extents to `self` and returns `Self { ..self.clone() }`, so the mesh that
    // gave up its data keeps the bounds as well as the extracted copy.
    EXPECT_TRUE(value.final_aabb.has_value());
    ASSERT_TRUE(taken->final_aabb.has_value());
    EXPECT_EQ(*taken->final_aabb,
              (mesh::Aabb3d::from_min_max(glm::vec3{-1.0f, -1.0f, 0.0f}, glm::vec3{-0.5f, 0.0f, 0.0f})));
    EXPECT_EQ(taken->final_aabb->center(), (glm::vec3{-0.75f, -0.5f, 0.0f}));
    EXPECT_EQ(taken->final_aabb->half_size(), (glm::vec3{0.25f, 0.5f, 0.0f}));

    // With no readable position attribute, Bevy's struct update keeps previously stored metadata.
    mesh::Mesh no_positions(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    no_positions.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2}});
    no_positions.final_aabb = mesh::Aabb3d::from_min_max(glm::vec3{-4.0f}, glm::vec3{5.0f});
    auto without_positions  = no_positions.take_gpu_data();
    ASSERT_TRUE(without_positions.has_value());
    EXPECT_EQ(without_positions->final_aabb, no_positions.final_aabb);
}

// Bevy v0.18 `Segment2d` inherent helpers (dim2.rs:1277-1544). Epix uses raw GLM vectors
// for checked unit directions and translation-plus-angle for the otherwise identical isometry.
TEST(MeshPrimitives, Segment2dHelpersMatchBevy) {
    const auto expect_vec2_near = [](glm::vec2 actual, glm::vec2 expected) {
        EXPECT_NEAR(actual.x, expected.x, 1e-6f);
        EXPECT_NEAR(actual.y, expected.y, 1e-6f);
    };

    const auto centered = mesh::Segment2d::from_direction_and_length({3.0f, 4.0f}, 10.0f);
    expect_vec2_near(centered.point1(), {-3.0f, -4.0f});
    expect_vec2_near(centered.point2(), {3.0f, 4.0f});
    const auto ray = mesh::Segment2d::from_ray_and_length({1.0f, 2.0f}, {0.0f, 2.0f}, 3.0f);
    expect_vec2_near(ray.point1(), {1.0f, 2.0f});
    expect_vec2_near(ray.point2(), {1.0f, 5.0f});

    const mesh::Segment2d segment{{1.0f, 2.0f}, {4.0f, 6.0f}};
    EXPECT_FLOAT_EQ(segment.length(), 5.0f);
    EXPECT_FLOAT_EQ(segment.length_squared(), 25.0f);
    EXPECT_EQ(segment.scaled_direction(), (glm::vec2{3.0f, 4.0f}));
    expect_vec2_near(segment.direction(), {0.6f, 0.8f});
    expect_vec2_near(segment.left_normal(), {-0.8f, 0.6f});
    expect_vec2_near(segment.right_normal(), {0.8f, -0.6f});
    EXPECT_EQ(segment.scaled_left_normal(), (glm::vec2{-4.0f, 3.0f}));
    EXPECT_EQ(segment.scaled_right_normal(), (glm::vec2{4.0f, -3.0f}));

    const mesh::Segment2d degenerate{{2.0f, 3.0f}, {2.0f, 3.0f}};
    EXPECT_FALSE(degenerate.try_direction().has_value());
    EXPECT_FALSE(degenerate.try_left_normal().has_value());
    EXPECT_FALSE(degenerate.try_right_normal().has_value());
    EXPECT_THROW(degenerate.direction(), std::invalid_argument);
    EXPECT_THROW(mesh::Segment2d::from_direction_and_length({std::numeric_limits<float>::infinity(), 0.0f}, 1.0f),
                 std::invalid_argument);

    EXPECT_EQ(segment.translated({-1.0f, 3.0f}), (mesh::Segment2d{{0.0f, 5.0f}, {3.0f, 9.0f}}));
    const auto rotated = segment.rotated(std::numbers::pi_v<float> * 0.5f);
    expect_vec2_near(rotated.point1(), {-2.0f, 1.0f});
    expect_vec2_near(rotated.point2(), {-6.0f, 4.0f});
    const auto transformed = segment.transformed({10.0f, -2.0f}, std::numbers::pi_v<float> * 0.5f);
    expect_vec2_near(transformed.point1(), {8.0f, -1.0f});
    expect_vec2_near(transformed.point2(), {4.0f, 2.0f});
    const auto around_first = segment.rotated_around(std::numbers::pi_v<float>, segment.point1());
    expect_vec2_near(around_first.point1(), segment.point1());
    expect_vec2_near(around_first.point2(), {-2.0f, -2.0f});
    EXPECT_EQ(segment.centered(), (mesh::Segment2d{{-1.5f, -2.0f}, {1.5f, 2.0f}}));
    EXPECT_EQ(segment.resized(10.0f), (mesh::Segment2d{{-0.5f, 0.0f}, {5.5f, 8.0f}}));

    auto reversed = segment;
    reversed.reverse();
    EXPECT_EQ(reversed, (mesh::Segment2d{{4.0f, 6.0f}, {1.0f, 2.0f}}));
    EXPECT_EQ(segment.reversed(), reversed);
    EXPECT_EQ(segment.closest_point({-10.0f, -10.0f}), segment.point1());
    EXPECT_EQ(segment.closest_point({10.0f, 10.0f}), segment.point2());
    expect_vec2_near(segment.closest_point({2.5f, 4.0f}), segment.center());
}

// Bevy v0.18 `Segment3d` inherent helpers (dim3.rs:399-602), with the same GLM
// direction/isometry adaptation used by Segment2d.
TEST(MeshPrimitives, Segment3dHelpersMatchBevy) {
    const auto expect_vec3_near = [](glm::vec3 actual, glm::vec3 expected) {
        EXPECT_NEAR(actual.x, expected.x, 1e-6f);
        EXPECT_NEAR(actual.y, expected.y, 1e-6f);
        EXPECT_NEAR(actual.z, expected.z, 1e-6f);
    };

    const auto centered = mesh::Segment3d::from_direction_and_length({0.0f, 3.0f, 4.0f}, 10.0f);
    expect_vec3_near(centered.point1(), {0.0f, -3.0f, -4.0f});
    expect_vec3_near(centered.point2(), {0.0f, 3.0f, 4.0f});
    const auto ray = mesh::Segment3d::from_ray_and_length({1.0f, 2.0f, 3.0f}, {2.0f, 0.0f, 0.0f}, 4.0f);
    EXPECT_EQ(ray, (mesh::Segment3d{{1.0f, 2.0f, 3.0f}, {5.0f, 2.0f, 3.0f}}));

    const mesh::Segment3d segment{{1.0f, 2.0f, 3.0f}, {1.0f, 5.0f, 7.0f}};
    EXPECT_FLOAT_EQ(segment.length(), 5.0f);
    EXPECT_FLOAT_EQ(segment.length_squared(), 25.0f);
    EXPECT_EQ(segment.scaled_direction(), (glm::vec3{0.0f, 3.0f, 4.0f}));
    expect_vec3_near(segment.direction(), {0.0f, 0.6f, 0.8f});

    const mesh::Segment3d degenerate{glm::vec3{1.0f}, glm::vec3{1.0f}};
    EXPECT_FALSE(degenerate.try_direction().has_value());
    EXPECT_THROW(degenerate.direction(), std::invalid_argument);

    const glm::quat half_turn = glm::angleAxis(std::numbers::pi_v<float>, glm::vec3{0.0f, 0.0f, 1.0f});
    const auto transformed    = segment.transformed({2.0f, -1.0f, 3.0f}, half_turn);
    expect_vec3_near(transformed.point1(), {1.0f, -3.0f, 6.0f});
    expect_vec3_near(transformed.point2(), {1.0f, -6.0f, 10.0f});
    const auto around_first = segment.rotated_around(half_turn, segment.point1());
    expect_vec3_near(around_first.point1(), segment.point1());
    expect_vec3_near(around_first.point2(), {1.0f, -1.0f, 7.0f});
    EXPECT_EQ(segment.centered(), (mesh::Segment3d{{0.0f, -1.5f, -2.0f}, {0.0f, 1.5f, 2.0f}}));
    EXPECT_EQ(segment.resized(10.0f), (mesh::Segment3d{{1.0f, 0.5f, 1.0f}, {1.0f, 6.5f, 9.0f}}));

    auto reversed = segment;
    reversed.reverse();
    EXPECT_EQ(segment.reversed(), reversed);
    EXPECT_EQ(segment.closest_point({1.0f, -10.0f, -10.0f}), segment.point1());
    EXPECT_EQ(segment.closest_point({1.0f, 10.0f, 12.0f}), segment.point2());
    expect_vec3_near(segment.closest_point({8.0f, 3.5f, 5.0f}), segment.center());
}

// The remaining B47-D2 helpers are direct ports of Bevy v0.18's primitive impl blocks.
// Bevy v0.18 `Primitive2d`, `Plane2d`, and `Polygon` (`primitives/mod.rs:14`,
// `dim2.rs:1190-1225, 1891-2030`, and `polygon.rs:289-320`).
TEST(MeshPrimitives, PlanePolygonAndPrimitive2dMatchBevy) {
    static_assert(mesh::Primitive2d<mesh::Circle>);
    static_assert(mesh::Primitive2d<mesh::Polygon>);
    static_assert(mesh::Primitive2d<mesh::Plane2d>);
    static_assert(mesh::Primitive2d<mesh::Ring<mesh::Plane2d>>);
    static_assert(!mesh::Primitive2d<mesh::Sphere>);
    static_assert(std::constructible_from<mesh::Extrusion<mesh::Polygon>, mesh::Polygon, float>);
    static_assert(!mesh::Meshable<mesh::Extrusion<mesh::Polygon>>);

    EXPECT_EQ(mesh::Plane2d{}.normal, (glm::vec2{0.0f, 1.0f}));
    const mesh::Plane2d plane{{3.0f, 4.0f}};
    EXPECT_NEAR(plane.normal.x, 0.6f, 1e-6f);
    EXPECT_NEAR(plane.normal.y, 0.8f, 1e-6f);
    EXPECT_THROW(mesh::Plane2d(glm::vec2{0.0f}), std::invalid_argument);
    EXPECT_THROW(mesh::Plane2d(glm::vec2{std::numeric_limits<float>::infinity(), 0.0f}), std::invalid_argument);

    const mesh::Polygon square{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    EXPECT_TRUE(square.is_simple());
    EXPECT_TRUE(mesh::Polygon{}.is_simple());
    EXPECT_TRUE((mesh::Polygon{{0.0f, 0.0f}, {1.0f, 0.0f}}.is_simple()));
    EXPECT_FALSE((mesh::Polygon{{1.0f, 1.0f}, {3.0f, 2.0f}, {-1.0f, 0.0f}}.is_simple()));
    EXPECT_FALSE((mesh::Polygon{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {2.0f, 0.5f}}.is_simple()));
    EXPECT_FALSE((mesh::Polygon{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.5f}}.is_simple()));
    EXPECT_FALSE((mesh::Polygon{{1.0f, 1.0f}, {3.0f, 2.0f}, {5.0f, 3.0f}, {-1.0f, 0.0f}}.is_simple()));
    EXPECT_FALSE((mesh::Polygon{{1.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f}}.is_simple()));
    EXPECT_FALSE((
        mesh::Polygon{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}}.is_simple()));

    const std::array vertices{glm::vec2{0.0f, 0.0f}, glm::vec2{2.0f, 0.0f}, glm::vec2{0.0f, 2.0f}};
    const mesh::Polygon from_iterators{vertices.begin(), vertices.end()};
    EXPECT_EQ(from_iterators.vertices.size(), 3u);
    auto convex = mesh::ConvexPolygon::from_polygon(from_iterators);
    ASSERT_TRUE(convex.has_value());
    const mesh::Polygon round_trip{std::move(*convex)};
    EXPECT_EQ(round_trip, from_iterators);
    EXPECT_FALSE((mesh::ConvexPolygon::from_polygon(
                      mesh::Polygon{{0.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 0.5f}, {2.0f, 2.0f}, {0.0f, 2.0f}})
                      .has_value()));
}

TEST(MeshPrimitives, InherentShapeHelpersMatchBevy) {
    const auto expect_vec2_near = [](glm::vec2 actual, glm::vec2 expected) {
        EXPECT_NEAR(actual.x, expected.x, 1e-6f);
        EXPECT_NEAR(actual.y, expected.y, 1e-6f);
    };
    const auto expect_vec3_near = [](glm::vec3 actual, glm::vec3 expected) {
        EXPECT_NEAR(actual.x, expected.x, 1e-6f);
        EXPECT_NEAR(actual.y, expected.y, 1e-6f);
        EXPECT_NEAR(actual.z, expected.z, 1e-6f);
    };

    const mesh::Circle circle{2.0f};
    EXPECT_EQ(circle.closest_point({1.0f, 0.0f}), (glm::vec2{1.0f, 0.0f}));
    expect_vec2_near(circle.closest_point({3.0f, 4.0f}), {1.2f, 1.6f});

    const mesh::Arc2d arc{2.0f, std::numbers::pi_v<float> / 3.0f};
    expect_vec2_near(arc.right_endpoint(), {std::sqrt(3.0f), 1.0f});
    expect_vec2_near(arc.left_endpoint(), {-std::sqrt(3.0f), 1.0f});
    expect_vec2_near(arc.endpoints()[0], arc.left_endpoint());
    expect_vec2_near(arc.endpoints()[1], arc.right_endpoint());
    EXPECT_EQ(arc.midpoint(), (glm::vec2{0.0f, 2.0f}));
    EXPECT_NEAR(arc.half_chord_length(), std::sqrt(3.0f), 1e-6f);
    EXPECT_NEAR(arc.chord_length(), 2.0f * std::sqrt(3.0f), 1e-6f);
    EXPECT_NEAR(arc.apothem(), 1.0f, 1e-6f);
    EXPECT_NEAR(arc.sagitta(), 1.0f, 1e-6f);
    EXPECT_TRUE(arc.is_minor());
    EXPECT_FALSE(arc.is_major());
    const mesh::Arc2d semicircle{2.0f, std::numbers::pi_v<float> * 0.5f};
    EXPECT_TRUE(semicircle.is_minor());
    EXPECT_TRUE(semicircle.is_major());

    const mesh::CircularSector sector{arc.radius, arc.half_angle};
    const mesh::CircularSegment circular_segment{arc.radius, arc.half_angle};
    EXPECT_FLOAT_EQ(sector.half_chord_length(), arc.half_chord_length());
    EXPECT_FLOAT_EQ(sector.chord_length(), arc.chord_length());
    EXPECT_FLOAT_EQ(sector.sagitta(), arc.sagitta());
    EXPECT_FLOAT_EQ(circular_segment.half_chord_length(), arc.half_chord_length());
    EXPECT_FLOAT_EQ(circular_segment.sagitta(), arc.sagitta());

    const mesh::Ellipse ellipse{glm::vec2{3.0f, 2.0f}};
    EXPECT_NEAR(ellipse.eccentricity(), std::sqrt(5.0f) / 3.0f, 1e-6f);
    EXPECT_NEAR(ellipse.focal_length(), std::sqrt(5.0f), 1e-6f);

    const mesh::RegularPolygon square{2.0f, 4};
    EXPECT_NEAR(square.inradius(), std::sqrt(2.0f), 1e-6f);
    EXPECT_FLOAT_EQ(square.internal_angle_degrees(), 90.0f);
    EXPECT_FLOAT_EQ(square.external_angle_degrees(), 90.0f);
    const auto square_vertices = square.vertices(0.0f);
    ASSERT_EQ(square_vertices.size(), 4u);
    expect_vec2_near(square_vertices[0], {0.0f, 2.0f});
    expect_vec2_near(square_vertices[1], {-2.0f, 0.0f});

    const mesh::Rectangle rectangle{glm::vec2{2.0f, 3.0f}};
    EXPECT_EQ(rectangle.closest_point({4.0f, -1.0f}), (glm::vec2{2.0f, -1.0f}));
    const auto [circumcircle, circumcenter] =
        mesh::Triangle2d{glm::vec2{0.0f, 0.0f}, glm::vec2{2.0f, 0.0f}, glm::vec2{0.0f, 2.0f}}.circumcircle();
    EXPECT_NEAR(circumcircle.radius, std::sqrt(2.0f), 1e-6f);
    expect_vec2_near(circumcenter, {1.0f, 1.0f});

    const mesh::Cuboid cuboid{glm::vec3{2.0f, 3.0f, 4.0f}};
    EXPECT_EQ(cuboid.closest_point({5.0f, -2.0f, -8.0f}), (glm::vec3{2.0f, -2.0f, -4.0f}));
    const mesh::Torus ring_torus{0.5f, 1.0f};
    const mesh::Torus horn_torus{1.0f, 1.0f};
    const mesh::Torus spindle_torus{2.0f, 1.0f};
    const mesh::Torus invalid_torus{0.0f, 1.0f};
    EXPECT_EQ(ring_torus.inner_radius(), 0.5f);
    EXPECT_EQ(ring_torus.outer_radius(), 1.5f);
    EXPECT_EQ(ring_torus.kind(), mesh::TorusKind::Ring);
    EXPECT_EQ(horn_torus.kind(), mesh::TorusKind::Horn);
    EXPECT_EQ(spindle_torus.kind(), mesh::TorusKind::Spindle);
    EXPECT_EQ(invalid_torus.kind(), mesh::TorusKind::Invalid);

    const mesh::Triangle3d triangle{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 0.0f, 0.0f},
                                    glm::vec3{0.0f, 2.0f, 0.0f}};
    expect_vec3_near(triangle.centroid(), {2.0f / 3.0f, 2.0f / 3.0f, 0.0f});
    EXPECT_EQ(triangle.largest_side(), (std::pair<glm::vec3, glm::vec3>{{2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}));
    expect_vec3_near(triangle.circumcenter(), {1.0f, 1.0f, 0.0f});
    const mesh::Triangle3d degenerate_triangle{glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f},
                                               glm::vec3{4.0f, 0.0f, 0.0f}};
    expect_vec3_near(degenerate_triangle.circumcenter(), {2.0f, 0.0f, 0.0f});
}
