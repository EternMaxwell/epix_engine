#include <epix/pbr/shadow_phase.hpp>

namespace epix::pbr {

std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT> cube_face_view_rotations() {
    std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT> rotations{};
    for (std::size_t face = 0; face < camera::CubeMapFaces.size(); ++face) {
        transform::Transform oriented;
        oriented.look_at(camera::CubeMapFaces[face].target, camera::CubeMapFaces[face].up);
        rotations[face] = glm::mat4_cast(oriented.rotation);
    }
    return rotations;
}

glm::mat4 point_light_face_world_from_view(
    const std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT>& rotations,
    glm::vec3 light_translation,
    std::size_t face_index) {
    glm::mat4 world_from_view = rotations[face_index % POINT_LIGHT_CUBE_FACE_COUNT];
    world_from_view[3]        = glm::vec4(light_translation, 1.0f);
    return world_from_view;
}

glm::mat4 point_light_face_clip_from_world(
    const std::array<glm::mat4, POINT_LIGHT_CUBE_FACE_COUNT>& rotations,
    glm::vec3 light_translation,
    std::size_t face_index,
    float shadow_map_near_z) {
    return cube_face_projection(shadow_map_near_z) *
           glm::inverse(point_light_face_world_from_view(rotations, light_translation, face_index));
}

std::uint32_t point_light_shadow_array_layer(std::size_t light_index, std::size_t face_index) {
    return static_cast<std::uint32_t>(light_index * POINT_LIGHT_CUBE_FACE_COUNT + face_index);
}

std::string point_light_shadow_pass_name(std::size_t light_index, std::size_t face_index) {
    return "shadow_point_light_" + std::to_string(light_index) + "_" +
           std::string(camera::face_index_to_name(face_index));
}

std::vector<ShadowViewRequest> point_light_shadow_view_requests(
    const std::vector<std::pair<ecs::Entity, glm::vec3>>& shadow_casting_lights,
    float shadow_map_near_z,
    std::uint32_t shadow_map_size) {
    std::vector<ShadowViewRequest> requests;
    requests.reserve(shadow_casting_lights.size() * POINT_LIGHT_CUBE_FACE_COUNT);

    const auto rotations = cube_face_view_rotations();
    for (std::size_t light_index = 0; light_index < shadow_casting_lights.size(); ++light_index) {
        const auto& [entity, translation] = shadow_casting_lights[light_index];
        for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
            requests.push_back(ShadowViewRequest{
                .light_entity    = entity,
                .owner           = LightEntity{.kind          = LightEntity::Kind::Point,
                                               .light_entity  = entity,
                                               .cascade_index = 0,
                                               .face_index    = face},
                .array_layer     = point_light_shadow_array_layer(light_index, face),
                .pass_name       = point_light_shadow_pass_name(light_index, face),
                .clip_from_world = point_light_face_clip_from_world(rotations, translation, face, shadow_map_near_z),
                .viewport_size   = shadow_map_size});
        }
    }
    return requests;
}

std::string directional_shadow_pass_name(std::size_t light_index, std::size_t cascade_index) {
    return "shadow_directional_light_" + std::to_string(light_index) + "_cascade_" + std::to_string(cascade_index);
}

void fill_gpu_directional_cascades(GpuDirectionalLight& light,
                                                      const std::vector<Cascade>& cascades,
                                                      const std::vector<float>& bounds) {
    const std::size_t count = std::min({cascades.size(), bounds.size(), MAX_CASCADES_PER_LIGHT});
    for (std::size_t cascade_index = 0; cascade_index < count; ++cascade_index) {
        light.cascades[cascade_index].clip_from_world = cascades[cascade_index].clip_from_world;
        light.cascades[cascade_index].texel_size      = cascades[cascade_index].texel_size;
        light.cascades[cascade_index].far_bound       = bounds[cascade_index];
    }
    for (std::size_t cascade_index = count; cascade_index < MAX_CASCADES_PER_LIGHT; ++cascade_index) {
        light.cascades[cascade_index] = GpuDirectionalCascade{};
    }
}

std::vector<ShadowViewRequest> directional_shadow_view_requests(
    const std::vector<ecs::Entity>& light_entities,
    const std::vector<std::vector<Cascade>>& cascades,
    const std::vector<DirectionalShadowSlice>& slices,
    std::uint32_t shadow_map_size) {
    std::vector<ShadowViewRequest> requests;

    for (const DirectionalShadowSlice& slice : slices) {
        if (slice.num_cascades == 0) continue;
        if (slice.light_index >= light_entities.size() || slice.light_index >= cascades.size()) continue;

        const ecs::Entity entity = light_entities[slice.light_index];
        for (std::uint32_t cascade_index = 0; cascade_index < slice.num_cascades; ++cascade_index) {
            if (cascade_index >= cascades[slice.light_index].size()) break;
            requests.push_back(
                ShadowViewRequest{.light_entity    = entity,
                                  .owner           = LightEntity{.kind          = LightEntity::Kind::Directional,
                                                                 .light_entity  = entity,
                                                                 .cascade_index = cascade_index,
                                                                 .face_index    = 0},
                                  .array_layer     = slice.base_index + cascade_index,
                                  .pass_name       = directional_shadow_pass_name(slice.light_index, cascade_index),
                                  .clip_from_world = cascades[slice.light_index][cascade_index].clip_from_world,
                                  .viewport_size   = shadow_map_size});
        }
    }
    return requests;
}

std::array<camera::Frustum, POINT_LIGHT_CUBE_FACE_COUNT> point_light_cubemap_frusta(
    glm::vec3 light_translation, glm::vec3 light_back, float range, float shadow_map_near_z) {
    const auto rotations = cube_face_view_rotations();

    std::array<camera::Frustum, POINT_LIGHT_CUBE_FACE_COUNT> frusta{};
    for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
        const glm::mat4 world_from_view = point_light_face_world_from_view(rotations, light_translation, face);
        const glm::mat4 clip_from_world = cube_face_projection(shadow_map_near_z) * glm::inverse(world_from_view);
        frusta[face] =
            camera::Frustum::from_clip_from_world_custom_far(clip_from_world, light_translation, light_back, range);
    }
    return frusta;
}

std::vector<camera::Frustum> directional_cascade_frusta(const std::vector<Cascade>& cascades) {
    std::vector<camera::Frustum> frusta;
    frusta.reserve(cascades.size());
    for (const Cascade& cascade : cascades) {
        frusta.push_back(camera::Frustum::from_clip_from_world(cascade.clip_from_world));
    }
    return frusta;
}

camera::Frustum directional_view_frustum(const camera::Frustum& frustum) {
    camera::Frustum pushed     = frustum;
    const glm::vec4 near_plane = pushed.planes[camera::Frustum::NEAR_PLANE_IDX];
    pushed.planes[camera::Frustum::NEAR_PLANE_IDX] =
        glm::vec4(glm::vec3(near_plane), std::numeric_limits<float>::infinity());
    return pushed;
}

camera::CascadesFrusta to_cascades_frusta(const Cascades& cascades) {
    camera::CascadesFrusta frusta;
    for (const auto& [view, view_cascades] : cascades.cascades) {
        frusta.frusta.emplace(view, directional_cascade_frusta(view_cascades));
    }
    return frusta;
}

bool should_update_point_light_frusta(bool light_changed,
                                                         bool global_lights_changed,
                                                         bool shadows_enabled,
                                                         bool in_global_lights) {
    if (!global_lights_changed && !light_changed) return false;
    if (!shadows_enabled) return false;
    return in_global_lights;
}

bool should_update_directional_cascade_frusta(bool shadows_enabled, bool light_visible) {
    return shadows_enabled && light_visible;
}

void shrink_entities(std::vector<ecs::Entity>& visible_entities) {
    const std::size_t capacity = visible_entities.capacity();
    std::size_t reserved       = 0;
    if (!visible_entities.empty()) {
        const std::size_t reserve = capacity / visible_entities.size();
        reserved                  = reserve > 2 ? capacity / (reserve / 2) : capacity;
    }
    if (reserved >= capacity) return;

    std::vector<ecs::Entity> shrunk;
    shrunk.reserve(reserved);
    shrunk.insert(shrunk.end(), visible_entities.begin(), visible_entities.end());
    visible_entities.swap(shrunk);
}

}  // namespace epix::pbr
