#include <gtest/gtest.h>

#include <cstring>

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
    mesh::Mesh clone(source);

    EXPECT_EQ(clone.asset_usage, source.asset_usage);
    EXPECT_EQ(clone.count_vertices(), source.count_vertices());
    ASSERT_TRUE(clone.remove_attribute(mesh::Mesh::ATTRIBUTE_POSITION).has_value());
    EXPECT_FALSE(clone.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_TRUE(source.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
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
