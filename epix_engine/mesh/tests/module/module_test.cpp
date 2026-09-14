#include <gtest/gtest.h>

#include <cstring>
#include <numbers>

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

    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_2d), id);
    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_3d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh2d>::as_asset_id(mesh_2d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh3d>::as_asset_id(mesh_3d), id);
    EXPECT_EQ((*mesh_2d).id(), id);
    EXPECT_EQ((*mesh_3d).id(), id);

    auto app             = epix::app::App::create();
    const auto entity_2d = app.world_mut().spawn(mesh_2d).id();
    const auto entity_3d = app.world_mut().spawn(mesh_3d).id();
    EXPECT_TRUE(app.world().entity(entity_2d).contains<epix::transform::Transform>());
    EXPECT_TRUE(app.world().entity(entity_3d).contains<epix::transform::Transform>());

    EXPECT_EQ(mesh::MeshTag{}.value, 0u);
    EXPECT_EQ(mesh::MeshTag{.value = 17}, mesh::MeshTag{.value = 17});
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
    auto mesh = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(mesh.count_vertices(), 4);
    ASSERT_TRUE(mesh.indices().has_value());
    EXPECT_EQ(mesh.indices()->get().len(), 6);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
    EXPECT_EQ(mesh.asset_usage,
              static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::MAIN_WORLD |
                                                           epix::assets::RenderAssetUsages::RENDER_WORLD));
}

TEST(MeshModule, MeshCloneRetainsIndependentGpuDataAndUsage) {
    auto source = mesh::make_box2d(20.0f, 10.0f);
    source.final_aabb = mesh::Aabb3d::from_min_max(glm::vec3{-2.0f}, glm::vec3{3.0f});
    mesh::Mesh clone(source);

    EXPECT_EQ(clone.asset_usage, source.asset_usage);
    EXPECT_EQ(clone.final_aabb, source.final_aabb);
    EXPECT_EQ(clone.count_vertices(), source.count_vertices());
    ASSERT_TRUE(clone.remove_attribute(mesh::Mesh::ATTRIBUTE_POSITION).has_value());
    EXPECT_FALSE(clone.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_TRUE(source.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
}

TEST(MeshAlgorithms, TakeGpuDataCalculatesFinalAabb) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    value.insert_attribute(
        mesh::Mesh::ATTRIBUTE_POSITION,
        std::vector<glm::vec3>{{-0.5f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {-0.5f, -1.0f, 0.0f}});
    value.insert_indices(mesh::Indices{std::vector<std::uint32_t>{0, 1, 2, 2, 3, 0}});

    auto taken = value.take_gpu_data();
    ASSERT_TRUE(taken.has_value());
    ASSERT_TRUE(taken->final_aabb.has_value());
    EXPECT_EQ(value.final_aabb, taken->final_aabb);
    EXPECT_EQ(*taken->final_aabb,
              (mesh::Aabb3d::from_min_max(glm::vec3{-1.0f, -1.0f, 0.0f}, glm::vec3{-0.5f, 0.0f, 0.0f})));

    mesh::Mesh no_positions(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::MAIN_WORLD);
    no_positions.final_aabb = mesh::Aabb3d::from_min_max(glm::vec3{-4.0f}, glm::vec3{5.0f});
    auto without_positions = no_positions.take_gpu_data();
    ASSERT_TRUE(without_positions.has_value());
    EXPECT_EQ(without_positions->final_aabb, no_positions.final_aabb);
}

TEST(MeshModule, CircleBuildsTriangleList) {
    auto mesh = mesh::make_circle(12.0f, std::nullopt, 16);
    EXPECT_EQ(mesh.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(mesh.count_vertices(), 17);
    ASSERT_TRUE(mesh.indices().has_value());
    EXPECT_EQ(mesh.indices()->get().len(), 48);
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

TEST(MeshModule, Box2dUvBuildsTexturedQuad) {
    auto mesh = mesh::make_box2d_uv(20.0f, 10.0f, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), glm::vec4(0.5f));

    EXPECT_EQ(mesh.count_vertices(), 4);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));

    auto uv_attribute = mesh.attribute(mesh::Mesh::ATTRIBUTE_UV_0);
    ASSERT_TRUE(uv_attribute.has_value());

    const auto* uvs = uv_attribute->get().get_if<mesh::VertexAttributeValues::Float32x2>();
    ASSERT_NE(uvs, nullptr);
    ASSERT_EQ(uvs->size(), 4u);
    EXPECT_EQ((*uvs)[0], (mesh::VertexAttributeValues::Float32x2Value{0.25f, 0.5f}));
    EXPECT_EQ((*uvs)[2], (mesh::VertexAttributeValues::Float32x2Value{0.75f, 1.0f}));
}

// Bevy RenderAsset::byte_len for RenderMesh: sum of per-vertex attribute
// strides * vertex count + index bytes.
TEST(MeshModule, RenderAssetByteLenMatchesBevyContract) {
    mesh::Mesh mesh(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                          std::array{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 1.0f}});
    mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR,
                          std::array{glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, glm::vec4{0.5f, 0.5f, 0.5f, 0.5f}});
    mesh.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 0}});

    const epix::render::RenderAsset<mesh::Mesh> asset{};
    const auto len = asset.byte_len(mesh);
    ASSERT_TRUE(len.has_value());
    EXPECT_EQ(*len, 62u);
}

TEST(MeshModule, EmptyMeshDataExtractsOnceAndPreservesAccessState) {
    mesh::Mesh source(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    const auto missing_indices = source.try_indices();
    ASSERT_FALSE(missing_indices.has_value());
    EXPECT_EQ(missing_indices.error(), mesh::MeshAccessError::NotFound);

    auto extracted = source.take_gpu_data();
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->count_vertices(), 0u);

    const auto source_attributes = source.try_attributes();
    ASSERT_FALSE(source_attributes.has_value());
    EXPECT_EQ(source_attributes.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    auto repeated = source.take_gpu_data();
    ASSERT_FALSE(repeated.has_value());
    EXPECT_EQ(repeated.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
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
