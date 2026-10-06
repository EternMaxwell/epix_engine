// The `light_meta.hpp` surface: the render-world visible-entity components the shadow
// pass reads (PBR-842..PBR-846, `bevy_pbr/src/components.rs:8-38`) and the flags and
// packing helpers `prepare_lights` writes into `GpuLights`/`GpuClusterableObject`
// (`bevy_pbr/src/render/light.rs`).
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_markers.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace epix;
using namespace epix::pbr;

namespace {
// A transform whose local -Z (Bevy's `forward()`) points along `direction`.
glm::mat4 transform_looking_along(glm::vec3 direction) {
    glm::mat4 transform{1.0f};
    transform[2] = glm::vec4(glm::normalize(-direction), 0.0f);
    return transform;
}
}  // namespace

/** @brief `WithLight` is Bevy's three-way light filter (Bevy `WithLight`,
 *  `bevy_light/src/lib.rs:209`): a point, a spot or a directional light, and nothing
 *  else.
 *
 *  Pinned as a type rather than by spawning lights and counting matches, because the
 *  alias *is* its type expression: a filter widened with a fourth `With<..>` - or one
 *  that dropped a light kind - would still pass a runtime count of the three lights a
 *  test happened to spawn, and every call site that uses it as a filter would go on
 *  compiling. */
static_assert(std::is_same_v<WithLight,
                             ecs::Or<ecs::With<PointLight>, ecs::With<SpotLight>, ecs::With<DirectionalLight>>>);

// PBR-842/PBR-843 — `bevy_pbr/src/components.rs:8-13`.
TEST(pbr, light_meta_render_visible_mesh_entities_holds_render_and_main_pairs) {
    RenderVisibleMeshEntities list;
    EXPECT_TRUE(list.entities.empty());

    ecs::World world(ecs::WorldId(1));
    const ecs::Entity render_entity = world.spawn().id();
    const ecs::Entity main_entity   = world.spawn().id();
    list.entities.emplace_back(render_entity, ::epix::render::sync_world::MainEntity(main_entity));

    ASSERT_EQ(list.entities.size(), 1u);
    EXPECT_EQ(list.entities[0].first, render_entity);
    EXPECT_EQ(list.entities[0].second.id(), main_entity);
}

// PBR-844/PBR-845/PBR-846 — `bevy_pbr/src/components.rs:15-38`. `data` is crate-private
// in Bevy, so `get`/`get_mut`/`iter`/`iter_mut` are its whole surface.
TEST(pbr, light_meta_render_cubemap_visible_entities_exposes_bevys_four_accessors) {
    RenderCubemapVisibleEntities cubemap;

    std::size_t faces = 0;
    for (const RenderVisibleMeshEntities& face : cubemap.iter()) {
        EXPECT_TRUE(face.entities.empty());
        ++faces;
    }
    EXPECT_EQ(faces, 6u);  // `[RenderVisibleMeshEntities; 6]`, components.rs:19
    EXPECT_EQ(cubemap.iter().size(), 6u);

    const ecs::Entity render_entity = ecs::Entity();
    cubemap.get_mut(3).entities.emplace_back(
        render_entity, ::epix::render::sync_world::MainEntity(ecs::Entity()));
    EXPECT_EQ(cubemap.get(3).entities.size(), 1u);
    EXPECT_TRUE(cubemap.get(0).entities.empty());
    EXPECT_TRUE(cubemap.get(5).entities.empty());

    // `iter_mut` writes through to the same storage
    for (RenderVisibleMeshEntities& face : cubemap.iter_mut()) {
        face.entities.clear();
    }
    EXPECT_TRUE(cubemap.get(3).entities.empty());
}

// Both types are components, and `extract_lights` clears the previous frame's lists by
// inserting fresh defaults over them (`bevy_pbr/src/render/light.rs:353-367`).
TEST(pbr, light_meta_render_cubemap_visible_entities_is_a_replaceable_component) {
    ecs::World world(ecs::WorldId(1));
    const ecs::Entity light      = world.spawn().id();
    const ecs::Entity first_mesh = world.spawn().id();
    world.entity_mut(light).insert(RenderCubemapVisibleEntities{});
    world.entity_mut(light).insert(RenderVisibleMeshEntities{});

    {
        auto mut = world.get_entity_mut(light).value();
        mut.get_mut<RenderCubemapVisibleEntities>()
            ->get_mut()
            .get_mut(0)
            .entities.emplace_back(first_mesh, ::epix::render::sync_world::MainEntity(first_mesh));
        mut.get_mut<RenderVisibleMeshEntities>()->get_mut().entities.emplace_back(
            first_mesh, ::epix::render::sync_world::MainEntity(first_mesh));
    }
    ASSERT_EQ(world.get_entity(light).value().get<RenderCubemapVisibleEntities>()->get().get(0).entities.size(), 1u);
    ASSERT_EQ(world.get_entity(light).value().get<RenderVisibleMeshEntities>()->get().entities.size(), 1u);

    // `insert` replaces the whole list, as Bevy's cleanup command does
    world.entity_mut(light).insert(RenderCubemapVisibleEntities{});
    world.entity_mut(light).insert(RenderVisibleMeshEntities{});
    EXPECT_TRUE(world.get_entity(light).value().get<RenderCubemapVisibleEntities>()->get().get(0).entities.empty());
    EXPECT_TRUE(world.get_entity(light).value().get<RenderVisibleMeshEntities>()->get().entities.empty());
}

// `bevy_pbr/src/render/light.rs:1030`, `:1076-1082` and `:1253`.
TEST(pbr, light_meta_shadow_texture_sizes_follow_bevys_descriptors) {
    EXPECT_EQ(POINT_LIGHT_CUBE_FACE_COUNT, 6u);

    // point_light_shadow_maps_count.max(1) * 6 - one cubemap even when nothing casts
    EXPECT_EQ(point_light_shadow_texture_layers(0), 6u);
    EXPECT_EQ(point_light_shadow_texture_layers(1), 6u);
    EXPECT_EQ(point_light_shadow_texture_layers(4), 24u);

    // (num_directional_cascades_enabled + spot_light_shadow_maps_count).max(1)
    EXPECT_EQ(directional_light_shadow_texture_layers(0, 0), 1u);
    EXPECT_EQ(directional_light_shadow_texture_layers(2, 3), 5u);

    // size.min(max_texture_dimension_2d)
    EXPECT_EQ(directional_light_shadow_texture_size(4096, 8192), 4096u);
    EXPECT_EQ(directional_light_shadow_texture_size(16384, 8192), 8192u);

    // point_light_count.min(max_texture_cubes)
    EXPECT_EQ(point_light_shadow_view_count(7, 3), 3u);
    EXPECT_EQ(point_light_shadow_view_count(2, 3), 2u);
}

// `bevy_pbr/src/render/light.rs:806-853`: every count is the filtered light count capped
// by the device limit, and the spot counts share the array with the directional cascade
// maps.
TEST(pbr, light_meta_count_lights_matches_bevys_caps) {
    std::vector<ExtractedPointLight> lights(3);
    lights[0].shadows_enabled = true;
    lights[1].shadows_enabled = true;
    lights[1].volumetric      = true;

    ExtractedPointLight spot;
    spot.spot_light_angles = std::pair{0.1f, 0.4f};
    spot.shadows_enabled   = true;
    ExtractedPointLight unlit_spot;
    unlit_spot.spot_light_angles = std::pair{0.1f, 0.4f};
    lights.push_back(spot);
    lights.push_back(unlit_spot);

    std::vector<ExtractedDirectionalLight> directional(1);
    directional[0].shadows_enabled = true;

    const auto counts = count_lights(lights, directional, /*max_texture_array_layers=*/16, /*max_texture_cubes=*/2);

    EXPECT_EQ(counts.point_lights, 3u);
    EXPECT_EQ(counts.point_light_shadow_maps, 2u);         // min(2, max_texture_cubes)
    EXPECT_EQ(counts.point_light_volumetric_enabled, 1u);  // min(1, max_texture_cubes)
    EXPECT_EQ(counts.spot_lights, 2u);
    // spot maps start where the directional cascades end: 16 - 1 * MAX_CASCADES_PER_LIGHT
    EXPECT_EQ(counts.spot_light_shadow_maps, 1u);
    EXPECT_EQ(counts.spot_light_volumetric_enabled, 0u);
    EXPECT_EQ(counts.directional_shadow_enabled, 1u);  // min(1, 16 / MAX_CASCADES_PER_LIGHT)
    EXPECT_EQ(counts.directional_volumetric_enabled, 0u);
}

// `bevy_pbr/src/render/light.rs:887-923`: the shadow and volumetric bits are decided by
// the light's position in its own block, and a spot light's cone direction decides
// `SPOT_LIGHT_Y_NEGATIVE`.
TEST(pbr, light_meta_point_light_gpu_flags_index_into_the_shadow_blocks) {
    LightCounts counts;
    counts.point_lights                   = 2;
    counts.point_light_shadow_maps        = 1;
    counts.point_light_volumetric_enabled = 1;
    counts.spot_light_shadow_maps         = 1;

    ExtractedPointLight point;
    point.shadows_enabled                 = true;
    point.affects_lightmapped_mesh_diffuse = false;

    // index 0 is a point light inside the shadow block, index 1 is past it
    EXPECT_EQ(point_light_gpu_flags(0, point, counts), PointLightFlags::SHADOWS_ENABLED);
    EXPECT_EQ(point_light_gpu_flags(1, point, counts), PointLightFlags::NONE);

    // the volume requires shadows as well as volumetrics
    ExtractedPointLight volumetric = point;
    volumetric.volumetric          = true;
    EXPECT_EQ(point_light_gpu_flags(0, volumetric, counts),
              PointLightFlags::SHADOWS_ENABLED | PointLightFlags::VOLUMETRIC);
    volumetric.shadows_enabled = false;
    EXPECT_EQ(point_light_gpu_flags(0, volumetric, counts), PointLightFlags::NONE);

    // spot lights sit after the point lights, so index 2 has in-block index 0
    ExtractedPointLight spot = point;
    spot.spot_light_angles   = std::pair{0.2f, 0.6f};
    spot.transform           = transform_looking_along(glm::vec3(0.0f, -1.0f, 0.0f));
    EXPECT_EQ(point_light_gpu_flags(2, spot, counts),
              PointLightFlags::SHADOWS_ENABLED | PointLightFlags::SPOT_LIGHT_Y_NEGATIVE);
    EXPECT_EQ(point_light_gpu_flags(3, spot, counts), PointLightFlags::SPOT_LIGHT_Y_NEGATIVE);

    // a cone that does not point below the horizon leaves the bit clear
    spot.transform = transform_looking_along(glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(point_light_gpu_flags(2, spot, counts), PointLightFlags::SHADOWS_ENABLED);

    // the lightmapped flag is independent of the rest
    spot.affects_lightmapped_mesh_diffuse = true;
    EXPECT_EQ(point_light_gpu_flags(3, spot, counts), PointLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE);
}

// `bevy_pbr/src/render/light.rs:112-122`.
TEST(pbr, light_meta_point_light_flags_match_the_shader_bits) {
    EXPECT_EQ(PointLightFlags::NONE, 0u);
    EXPECT_EQ(PointLightFlags::SHADOWS_ENABLED, 1u << 0);
    EXPECT_EQ(PointLightFlags::SPOT_LIGHT_Y_NEGATIVE, 1u << 1);
    EXPECT_EQ(PointLightFlags::VOLUMETRIC, 1u << 2);
    EXPECT_EQ(PointLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE, 1u << 3);
    EXPECT_EQ(PointLightFlags::UNINITIALIZED, 0xFFFFu);
}

// `bevy_pbr/src/render/light.rs:936-946` and `:952-957`.
TEST(pbr, light_meta_point_light_custom_data_and_colour_packing) {
    // the lower-right 2x2 of the cube-face projection: [2][2] [2][3] [3][2] [3][3]
    glm::mat4 projection{0.0f};
    projection[2][2] = 1.0f;
    projection[2][3] = 2.0f;
    projection[3][2] = 3.0f;
    projection[3][3] = 4.0f;
    const glm::vec4 custom = point_light_custom_data(projection);
    EXPECT_FLOAT_EQ(custom.x, 1.0f);
    EXPECT_FLOAT_EQ(custom.y, 2.0f);
    EXPECT_FLOAT_EQ(custom.z, 3.0f);
    EXPECT_FLOAT_EQ(custom.w, 4.0f);

    // `perspective_infinite_reverse_rh(FRAC_PI_2, 1.0, shadow_map_near_z)` (light.rs:900-904)
    const glm::mat4 face = cube_face_projection(0.1f);
    const glm::mat4 expected =
        camera::perspective_infinite_reverse_rh(std::numbers::pi_v<float> / 2.0f, 1.0f, 0.1f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_FLOAT_EQ(face[column][row], expected[column][row]);
        }
    }

    // the whole vec4 is premultiplied, then `1 / range^2` goes in w
    const glm::vec4 packed = pack_light_color_intensity_range(glm::vec4(0.5f, 0.25f, 0.125f, 0.75f), 4.0f, 2.0f);
    EXPECT_FLOAT_EQ(packed.x, 2.0f);
    EXPECT_FLOAT_EQ(packed.y, 1.0f);
    EXPECT_FLOAT_EQ(packed.z, 0.5f);
    EXPECT_FLOAT_EQ(packed.w, 0.25f);  // 1 / (2 * 2)

    // the alpha is premultiplied too, but the returned xyz is what the shader reads
    EXPECT_FLOAT_EQ(pack_light_color_intensity_range(glm::vec4(1.0f), 2.0f, 4.0f).w, 1.0f / 16.0f);
}

// `bevy_pbr/src/render/light.rs:918-933` and `:920-923`.
TEST(pbr, light_meta_spot_light_custom_data_and_y_sign) {
    const float inner = 0.2f;
    const float outer = 0.6f;
    const glm::vec3 direction{-0.4f, -0.3f, 0.5f};
    const auto [custom, tan_angle] = spot_light_custom_data(inner, outer, direction);

    const float cos_outer   = std::cos(outer);
    const float spot_scale  = 1.0f / std::max(std::cos(inner) - cos_outer, 1e-4f);
    const float spot_offset = -cos_outer * spot_scale;
    EXPECT_FLOAT_EQ(custom.x, direction.x);
    EXPECT_FLOAT_EQ(custom.y, direction.z);
    EXPECT_FLOAT_EQ(custom.z, spot_scale);
    EXPECT_FLOAT_EQ(custom.w, spot_offset);
    EXPECT_FLOAT_EQ(tan_angle, std::tan(outer));

    // the `1e-4` guard keeps the scale finite when the angles coincide
    const auto [coincident, unused] = spot_light_custom_data(0.5f, 0.5f, direction);
    EXPECT_FLOAT_EQ(coincident.z, 1.0f / 1e-4f);
    (void)unused;

    // `is_sign_negative`, not `< 0.0`: -0.0 counts as negative
    EXPECT_TRUE(spot_light_y_negative(glm::vec3(0.0f, -0.0f, 1.0f)));
    EXPECT_TRUE(spot_light_y_negative(glm::vec3(0.0f, -1.0f, 0.0f)));
    EXPECT_FALSE(spot_light_y_negative(glm::vec3(0.0f, 0.0f, -1.0f)));
}

// `bevy_pbr/src/render/light.rs:1205-1244`.
TEST(pbr, light_meta_directional_and_view_packing) {
    // premultiply the whole vec4 by the illuminance (light.rs:1205-1207)
    const glm::vec4 colour = pack_directional_color(glm::vec4(0.5f, 0.5f, 0.5f, 0.5f), 4.0f);
    EXPECT_EQ(colour, glm::vec4(2.0f));

    // ambient colour scaled by brightness (light.rs:1230-1231)
    EXPECT_EQ(pack_ambient_color(glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), 2.0f), glm::vec4(0.5f, 1.0f, 1.5f, 2.0f));

    // cluster factors: grid / viewport, then the log pair (light.rs:1232-1237)
    const glm::vec4 factors = pack_cluster_factors(glm::uvec3(16u, 9u, 24u), glm::vec2(1600.0f, 900.0f),
                                                   glm::vec2(3.0f, 4.0f));
    EXPECT_FLOAT_EQ(factors.x, 16.0f / 1600.0f);
    EXPECT_FLOAT_EQ(factors.y, 9.0f / 900.0f);
    EXPECT_FLOAT_EQ(factors.z, 3.0f);
    EXPECT_FLOAT_EQ(factors.w, 4.0f);

    // cluster dimensions: the counts and their product (light.rs:1238)
    EXPECT_EQ(pack_cluster_dimensions(glm::uvec3(16u, 9u, 24u)), glm::uvec4(16u, 9u, 24u, 16u * 9u * 24u));

    // spot shadow maps live in the directional array (light.rs:1240-1244)
    EXPECT_EQ(spot_light_shadowmap_offset(4u, 3u), 1);
    EXPECT_EQ(spot_light_shadowmap_offset(1u, 5u), -4);

    // no clustered decal is `u32::MAX` in both light kinds (light.rs:1219-1223)
    EXPECT_EQ(NO_CLUSTERED_DECAL, 0xFFFFFFFFu);
}

// `bevy_pbr/src/render/light.rs:1171-1196`: the shadow flag and the granted cascades are
// one computation, because a light that does not fit the remaining layers loses its
// shadows while keeping its other flags.
TEST(pbr, light_meta_directional_light_gpu_flags_budget_the_cascade_layers) {
    ExtractedDirectionalLight light;
    light.shadows_enabled                 = true;
    light.volumetric                      = true;
    light.affects_lightmapped_mesh_diffuse = false;
    light.cascade_shadow_config.bounds.resize(4);

    const auto fitting = directional_light_gpu_flags(light, /*index=*/0, /*volumetric_count=*/1,
                                                     /*cascades_used_so_far=*/0, /*max_texture_array_layers=*/4);
    EXPECT_EQ(fitting.flags, DirectionalLightFlags::SHADOWS_ENABLED | DirectionalLightFlags::VOLUMETRIC);
    EXPECT_EQ(fitting.num_cascades, 4u);

    // one cascade already taken by an earlier light: the shadows no longer fit, but the
    // light keeps every other flag it had earned
    const auto no_shadows =
        directional_light_gpu_flags(light, 0u, 1u, /*cascades_used_so_far=*/1, /*max_texture_array_layers=*/4);
    EXPECT_EQ(no_shadows.flags, DirectionalLightFlags::VOLUMETRIC);
    EXPECT_EQ(no_shadows.num_cascades, 0u);

    // the volumetric flag needs shadows as well as volumetrics, and the index is what
    // the volumetric budget is compared against
    const auto past_volumetric_budget = directional_light_gpu_flags(light, 1u, 1u, 0u, 4u);
    EXPECT_EQ(past_volumetric_budget.flags, DirectionalLightFlags::SHADOWS_ENABLED);
    EXPECT_EQ(past_volumetric_budget.num_cascades, 4u);

    ExtractedDirectionalLight unlit;
    unlit.affects_lightmapped_mesh_diffuse = false;
    const auto none = directional_light_gpu_flags(unlit, 0u, 0u, 0u, 4u);
    EXPECT_EQ(none.flags, DirectionalLightFlags::NONE);
    EXPECT_EQ(none.num_cascades, 0u);
}
