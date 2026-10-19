// Left: CPU-baked reference. Right: the same four-joint blend through the real
// Mesh3d skin extraction, upload, specialization and vertex shader. The two
// silhouettes should match after a horizontal translation. Colours are vertex
// data; no custom shader or lighting workaround is involved.
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <expected>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace epix;
using namespace epix::app;
using namespace epix::assets;
using namespace epix::ecs;

struct SkinColourMaterial : pbr::Material<SkinColourMaterial> {};

template <>
struct epix::render::render_resource::AsBindGroup<SkinColourMaterial>
    : epix::render::render_resource::AsBindGroupBase {
    using Data = std::monostate;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "skinning colour material"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const SkinColourMaterial&) { return {}; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const SkinColourMaterial&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

namespace {

void spawn_scene(Commands cmd, ResMut<Assets<mesh::Mesh>> meshes,
                 ResMut<Assets<mesh::SkinnedMeshInverseBindposes>> bindposes,
                 ResMut<Assets<SkinColourMaterial>> materials) {
    const glm::vec4 weights{0.1f, 0.2f, 0.3f, 0.4f};
    std::vector<Entity> joint_entities;
    glm::mat4 blended{0.0f};
    for (int index = 0; index < 4; ++index) {
        const float i = static_cast<float>(index);
        const transform::Transform pose{
            .translation = {1.5f + 0.1f * i, -0.05f * i, 0},
            .rotation = glm::angleAxis(0.25f + 0.1f * i, glm::normalize(glm::vec3{1, 2, 1})),
            .scaler = {1.2f + 0.1f * i, 0.7f + 0.05f * i, 1.3f - 0.08f * i},
        };
        blended += weights[index] * pose.to_matrix();
        joint_entities.push_back(cmd.spawn(pose).id());
    }
    auto skinned = mesh::Cuboid(1.2f, 1.2f, 1.2f).mesh().build();
    auto reference = skinned;
    const auto count = skinned.count_vertices();
    std::vector<glm::vec3> positions;
    for (const auto& p : *reference.attribute(mesh::Mesh::ATTRIBUTE_POSITION)->get().as_float3()) {
        const auto world = blended * glm::vec4{p[0], p[1], p[2], 1};
        positions.push_back(glm::vec3{world} - glm::vec3{3.0f, 0, 0});
    }
    reference.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::move(positions));
    const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3{blended}));
    std::vector<glm::vec3> normals;
    for (const auto& n : *reference.attribute(mesh::Mesh::ATTRIBUTE_NORMAL)->get().as_float3())
        normals.push_back(glm::normalize(normal_matrix * glm::vec3{n[0], n[1], n[2]}));
    reference.insert_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, std::move(normals));
    reference.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, std::vector<glm::vec4>(count, {0.1f, 0.55f, 0.9f, 1}));
    skinned.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, std::vector<glm::vec4>(count, {0.95f, 0.35f, 0.1f, 1}));
    skinned.insert_attribute(mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT, std::vector<glm::vec4>(count, weights));
    skinned.insert_attribute(mesh::Mesh::ATTRIBUTE_JOINT_INDEX,
        mesh::VertexAttributeValues::Uint16x4{
            std::vector<std::array<std::uint16_t, 4>>(count, {0, 1, 2, 3})});
    const auto reference_handle = meshes->emplace(std::move(reference));
    const auto skinned_handle = meshes->emplace(std::move(skinned));
    const auto bindpose_handle = bindposes->emplace(
        mesh::SkinnedMeshInverseBindposes{std::vector<glm::mat4>(4, glm::mat4{1.0f})});
    const auto material = materials->emplace(SkinColourMaterial{});
    cmd.spawn(mesh::Mesh3d{reference_handle}, pbr::MeshMaterial3d<SkinColourMaterial>{material}, transform::Transform{});
    cmd.spawn(mesh::Mesh3d{skinned_handle}, pbr::MeshMaterial3d<SkinColourMaterial>{material},
              mesh::SkinnedMesh{.inverse_bindposes = bindpose_handle, .joints = std::move(joint_entities)},
              transform::Transform{});
    // Orthographic projection makes the reference/GPU silhouettes directly
    // comparable: a three-unit translation is 480 pixels at this window size.
    cmd.spawn(camera::Camera3d{},
              camera::Projection::orthographic(camera::OrthographicProjection{
                  .scaling_mode = camera::ScalingMode::FixedVertical{4.5f}}),
              transform::Transform::from_xyz(0, 0, 6));
}

void probe_uploaded_skin(Res<pbr::SkinUniforms> skins, Local<bool> logged) {
    if (*logged || skins->total_joints == 0) return;
    spdlog::info("[skinning] uploaded joints={} skins={} current_buffer_bytes={}", skins->total_joints,
                 skins->skin_uniform_info.size(), skins->current_buffer.getSize());
    if (skins->total_joints != 4 || skins->skin_uniform_info.size() != 1)
        spdlog::error("[skinning] Expected one four-joint skin");
    *logged = true;
}

}  // namespace

int main() {
    App app = App::create();
    window::Window window;
    window.title = "Skinning: CPU reference (blue) / GPU joints (orange)";
    window.size = {1280, 720};
    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = window,
                                          .exit_condition = window::ExitCondition::OnPrimaryClosed})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(sdl3::SDL3Plugin{})
        .add_plugins(sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(pbr::MaterialPlugin<SkinColourMaterial>{})
        .add_plugins(pbr::PbrPlugin{.supports_storage_buffers = true});
    app.add_systems(Startup, into(spawn_scene).set_name("spawn CPU and GPU skinning comparison"));
    app.get_sub_app_mut(render::Render)->get().add_systems(
        render::Render, into(probe_uploaded_skin).in_set(render::RenderSystems::PrepareBindGroups)
                            .set_name("probe uploaded skin"));
    app.run();
}
