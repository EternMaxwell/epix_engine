#include <epix/prelude.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>
#include <box2d/box2d.h>
#include <epix/imgui.h>
#include <epix/physics2d.h>
#include <epix/world/sand_physics.h>
#include <stb_image.h>

#include <earcut.hpp>
#include <random>
#include <stack>
#include <tracy/Tracy.hpp>

#include "fragment_shader.h"
#include "vertex_shader.h"

using namespace epix::prelude;
using namespace epix::window;

namespace vk_trial {
using namespace epix::utils::grid;

template <typename T>
T& value_at(std::vector<std::vector<T>>& vec, size_t index) {
    if (!vec.size()) throw std::out_of_range("vector is empty");
    auto cur = vec.begin();
    while (cur != vec.end()) {
        if (index < (*cur).size()) return (*cur)[index];
        index -= cur->size();
        cur++;
    }
    throw std::out_of_range("index out of range");
}

struct pixelbin {
    int width, height;
    std::vector<uint32_t> pixels;
    int column;

    pixelbin(int width, int height) : width(width), height(height) {
        column = width / 32 + (width % 32 ? 1 : 0);
        pixels.resize(column * height);
    }
    void set(int x, int y, bool value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        if (value)
            pixels[index] |= 1 << bit;
        else
            pixels[index] &= ~(1 << bit);
    }
    bool operator[](glm::ivec2 pos) const {
        if (pos.x < 0 || pos.x >= width || pos.y < 0 || pos.y >= height)
            return false;
        int index = pos.x / 32 + pos.y * column;
        int bit   = pos.x % 32;
        return pixels[index] & (1 << bit);
    }
    bool contains(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        return pixels[index] & (1 << bit);
    }
    glm::ivec2 size() const { return {width, height}; }
};

void create_b2d_world(Command command) {
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity    = {0.0f, -29.8f};
    auto world     = b2CreateWorld(&def);
    command.spawn(world);
}

void create_ground(Command command, Query<Get<b2WorldId>> world_query) {
    if (!world_query) return;
    auto [world]         = world_query.single();
    b2BodyDef body_def   = b2DefaultBodyDef();
    body_def.type        = b2BodyType::b2_staticBody;
    body_def.position    = {0.0f, -100.0f};
    auto ground          = b2CreateBody(world, &body_def);
    b2Polygon ground_box = b2MakeBox(1000.0f, 10.0f);
    b2ShapeDef shape_def = b2DefaultShapeDef();
    b2CreatePolygonShape(ground, &shape_def, &ground_box);
    command.spawn(ground);
}

void create_dynamic(Command command, Query<Get<b2WorldId>> world_query) {
    if (!world_query) return;
    auto [world]          = world_query.single();
    b2BodyDef body_def    = b2DefaultBodyDef();
    body_def.type         = b2BodyType::b2_dynamicBody;
    body_def.position     = {0.0f, 100.0f};
    auto body             = b2CreateBody(world, &body_def);
    b2Polygon box         = b2MakeBox(4.0f, 4.0f);
    b2ShapeDef shape_def  = b2DefaultShapeDef();
    shape_def.density     = 1.0f;
    shape_def.friction    = 0.1f;
    shape_def.restitution = 0.9f;
    b2CreatePolygonShape(body, &shape_def, &box);
    command.spawn(body);
}

using namespace epix::input;

void create_dynamic_from_click(
    Command command,
    Query<Get<b2WorldId>> world_query,
    Query<
        Get<const Window,
            const ButtonInput<MouseButton>,
            const ButtonInput<KeyCode>>> window_query,
    ResMut<epix::imgui::ImGuiContext> imgui_context
) {
    if (!world_query) return;
    if (!window_query) return;
    auto [window, mouse_input, key_input] = window_query.single();
    if (imgui_context.has_value()) {
        ImGui::SetCurrentContext(imgui_context->context);
        if (ImGui::GetIO().WantCaptureMouse) return;
    }
    if (!mouse_input.pressed(epix::input::MouseButton1) &&
        !key_input.just_pressed(epix::input::KeySpace))
        return;
    auto [world]       = world_query.single();
    b2BodyDef body_def = b2DefaultBodyDef();
    body_def.type      = b2BodyType::b2_dynamicBody;
    body_def.position  = {
        (float)window.get_cursor().x - (float)window.get_size().width / 2.0f,
        -(float)window.get_cursor().y + (float)window.get_size().height / 2.0f
    };
    spdlog::info(
        "Creating body at {}, {}", body_def.position.x, body_def.position.y
    );
    auto body             = b2CreateBody(world, &body_def);
    b2Polygon box         = b2MakeBox(4.0f, 4.0f);
    b2ShapeDef shape_def  = b2DefaultShapeDef();
    shape_def.density     = 1.0f;
    shape_def.friction    = 0.6f;
    shape_def.restitution = 0.1f;
    b2CreatePolygonShape(body, &shape_def, &box);
    command.spawn(body);
}

void toggle_full_screen(Query<Get<Window, ButtonInput<KeyCode>>> query) {
    if (!query) return;
    auto [window, key_input] = query.single();
    if (key_input.just_pressed(epix::input::KeyF11)) {
        window.toggle_fullscreen();
    }
}

using namespace epix::render::vulkan2::backend;
using namespace epix::render::vulkan2;

struct WholePassBase : public render::vulkan2::PassBase {
   protected:
    WholePassBase(Device& device) : PassBase(device) {
        set_attachments(
            vk::AttachmentDescription()
                .setFormat(vk::Format::eR8G8B8A8Srgb)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setLoadOp(vk::AttachmentLoadOp::eLoad)
                .setStoreOp(vk::AttachmentStoreOp::eStore)
                .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
        );
        subpass_info(0)
            .set_colors(vk::AttachmentReference().setAttachment(0).setLayout(
                vk::ImageLayout::eColorAttachmentOptimal
            ))
            .set_bind_point(vk::PipelineBindPoint::eGraphics);
        subpass_info(1)
            .set_colors(vk::AttachmentReference().setAttachment(0).setLayout(
                vk::ImageLayout::eColorAttachmentOptimal
            ))
            .set_bind_point(vk::PipelineBindPoint::eGraphics);
        vk::SubpassDependency dependency;
        dependency.setSrcSubpass(0);
        dependency.setDstSubpass(1);
        dependency.setSrcStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );
        dependency.setDstStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );
        dependency.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        dependency.setDependencyFlags(vk::DependencyFlagBits::eByRegion);
        set_dependencies(dependency);
        create();
        add_pipeline(
            0, "test::sand::falling_sand",
            render::pixel::vulkan2::PixelPipeline::create()
        );
        add_pipeline(
            1, "test::sand::box2d_body",
            render::debug::vulkan2::DebugPipelines::line_pipeline()
        );
    }

   public:
    static WholePassBase* create_new(Device& device) {
        return new WholePassBase(device);
    }
};

struct WholePass : public render::vulkan2::Pass {
   protected:
    WholePass(const WholePassBase* base, CommandPool& command_pool)
        : Pass(base, command_pool, [](Pass& pass, const PassBase& base) {
              pass.add_subpass(0);
              pass.subpass_add_pipeline(
                  0, "test::sand::falling_sand",
                  [](Device& device, const DescriptorPool& pool,
                     const std::vector<DescriptorSetLayout>& layouts) {
                      return device.allocateDescriptorSets(
                          vk::DescriptorSetAllocateInfo()
                              .setDescriptorPool(pool)
                              .setSetLayouts(layouts[0])
                      );
                  },
                  [](Device& device, const DescriptorPool& pool,
                     std::vector<DescriptorSet>& sets) {
                      device.freeDescriptorSets(pool, sets[0]);
                  }
              );
              pass.add_subpass(1);
              pass.subpass_add_pipeline(
                  1, "test::sand::box2d_body",
                  [](Device& device, const DescriptorPool& pool,
                     const std::vector<DescriptorSetLayout>& layouts) {
                      return device.allocateDescriptorSets(
                          vk::DescriptorSetAllocateInfo()
                              .setDescriptorPool(pool)
                              .setSetLayouts(layouts[0])
                      );
                  },
                  [](Device& device, const DescriptorPool& pool,
                     std::vector<DescriptorSet>& sets) {
                      device.freeDescriptorSets(pool, sets[0]);
                  }
              );
          }) {}

   public:
    static WholePass* create_new(
        const WholePassBase* base, CommandPool& command_pool
    ) {
        return new WholePass(base, command_pool);
    }
};

void create_whole_pass_base(Command command, Res<RenderContext> context) {
    if (!context) return;
    auto& device = context->device;
    spdlog::info("Creating whole pass base");
    command.add_resource(WholePassBase::create_new(device));
}

void create_whole_pass(
    Command command, ResMut<RenderContext> context, Res<WholePassBase> base
) {
    if (!context) return;
    auto& device       = context->device;
    auto& command_pool = context->command_pool;
    spdlog::info("Creating whole pass");
    command.add_resource(WholePass::create_new(base.get(), command_pool));
}

void destroy_whole_pass(
    ResMut<WholePass> pass, ResMut<WholePassBase> pass_base
) {
    if (!pass || !pass_base) return;
    spdlog::info("Destroying whole pass");
    pass->destroy();
    pass_base->destroy();
}

void extract_whole_pass(ResMut<WholePass> pass, Command command) {
    if (!pass) return;
    ZoneScoped;
    command.share_resource(pass);
}

struct SandMesh : render::pixel::vulkan2::PixelMesh {};
struct SandMeshStaging : render::pixel::vulkan2::PixelStagingMesh {};
struct SandMeshGPU : render::pixel::vulkan2::PixelGPUMesh {};

struct Box2dMesh : render::debug::vulkan2::DebugMesh {};
struct Box2dMeshStaging : render::debug::vulkan2::DebugStagingMesh {};
struct Box2dMeshGPU : render::debug::vulkan2::DebugGPUMesh {};

void create_sand_meshes(Command command, Res<RenderContext> context) {
    if (!context) return;
    auto& device = context->device;
    spdlog::info("Creating sand meshes");
    SandMeshStaging mesh(device);
    command.insert_resource(mesh);
    SandMeshGPU mesh2(device);
    command.insert_resource(mesh2);
    SandMesh mesh3;
    command.insert_resource(mesh3);
}

void destroy_sand_meshes(
    ResMut<SandMeshStaging> mesh, ResMut<SandMeshGPU> mesh2
) {
    if (!mesh || !mesh2) return;
    spdlog::info("Destroying sand meshes");
    mesh->destroy();
    mesh2->destroy();
}

void extract_sand_mesh(
    ResMut<SandMeshStaging> mesh,
    ResMut<SandMeshGPU> mesh2,
    ResMut<SandMesh> mesh3,
    Command command
) {
    if (!mesh || !mesh2 || !mesh3) return;
    ZoneScoped;
    mesh->update(*mesh3);
    mesh3->clear();
    command.share_resource(mesh);
    command.share_resource(mesh2);
}

void create_box2d_meshes(Command command, Res<RenderContext> context) {
    if (!context) return;
    auto& device = context->device;
    spdlog::info("Creating box2d meshes");
    Box2dMeshStaging mesh(device);
    command.insert_resource(mesh);
    Box2dMeshGPU mesh2(device);
    command.insert_resource(mesh2);
    Box2dMesh mesh3;
    command.insert_resource(mesh3);
}

void destroy_box2d_meshes(
    ResMut<Box2dMeshStaging> mesh, ResMut<Box2dMeshGPU> mesh2
) {
    if (!mesh || !mesh2) return;
    spdlog::info("Destroying box2d meshes");
    mesh->destroy();
    mesh2->destroy();
}

void extract_box2d_mesh(
    ResMut<Box2dMeshStaging> mesh,
    ResMut<Box2dMeshGPU> mesh2,
    ResMut<Box2dMesh> mesh3,
    Command command
) {
    if (!mesh || !mesh2 || !mesh3) return;
    ZoneScoped;
    mesh->update(*mesh3);
    mesh3->clear();
    command.share_resource(mesh);
    command.share_resource(mesh2);
}

void render_bodies(Extract<Get<b2BodyId>> query, ResMut<Box2dMesh> mesh) {
    if (!query) return;
    if (!mesh) return;
    ZoneScoped;
    for (auto [body] : query.iter()) {
        if (!b2Body_IsValid(body)) continue;
        b2Vec2 position  = b2Body_GetPosition(body);
        b2Vec2 velocity  = b2Body_GetLinearVelocity(body);
        auto mass_center = b2Body_GetWorldCenterOfMass(body);
        auto rotation    = b2Body_GetRotation(body);  // a cosine / sine pair
        auto angle       = std::atan2(rotation.s, rotation.c);
        glm::vec3 p1     = {mass_center.x, mass_center.y, 0.0f};
        glm::vec3 p2     = {
            mass_center.x + velocity.x / 2, mass_center.y + velocity.y / 2, 0.0f
        };
        mesh->draw_line(p1, p2, {0.0f, 0.0f, 1.0f, 1.0f});
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f), {mass_center.x, mass_center.y, 0.0f}
        );
        model = glm::translate(glm::mat4(1.0f), {position.x, position.y, 0.0f});
        model = glm::rotate(model, angle, {0.0f, 0.0f, 1.0f});
        auto shape_count = b2Body_GetShapeCount(body);
        std::vector<b2ShapeId> shapes(shape_count);
        b2Body_GetShapes(body, shapes.data(), shape_count);
        for (auto&& shape : shapes) {
            if (b2Shape_GetType(shape) == b2ShapeType::b2_polygonShape) {
                auto polygon = b2Shape_GetPolygon(shape);
                for (size_t i = 0; i < polygon.count; i++) {
                    auto& vertex1 = polygon.vertices[i];
                    auto& vertex2 = polygon.vertices[(i + 1) % polygon.count];
                    bool awake    = b2Body_IsAwake(body);
                    glm::vec3 p1  = {vertex1.x, vertex1.y, 0.0f};
                    glm::vec3 p2  = {vertex2.x, vertex2.y, 0.0f};
                    p1            = glm::vec3(model * glm::vec4(p1, 1.0f));
                    p2            = glm::vec3(model * glm::vec4(p2, 1.0f));
                    mesh->draw_line(
                        p1, p2,
                        {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                    );
                }
            } else if (b2Shape_GetType(shape) ==
                       b2ShapeType::b2_smoothSegmentShape) {
                auto segment  = b2Shape_GetSmoothSegment(shape);
                auto& vertex1 = segment.segment.point1;
                auto& vertex2 = segment.segment.point2;
                bool awake    = b2Body_IsAwake(body);
                glm::vec3 p1  = {vertex1.x, vertex1.y, 0.0f};
                glm::vec3 p2  = {vertex2.x, vertex2.y, 0.0f};
                p1            = glm::vec3(model * glm::vec4(p1, 1.0f));
                p2            = glm::vec3(model * glm::vec4(p2, 1.0f));
                mesh->draw_line(
                    p1, p2,
                    {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                );
            } else if (b2Shape_GetType(shape) == b2ShapeType::b2_segmentShape) {
                auto segment  = b2Shape_GetSegment(shape);
                auto& vertex1 = segment.point1;
                auto& vertex2 = segment.point2;
                bool awake    = b2Body_IsAwake(body);
                glm::vec3 p1  = {vertex1.x, vertex1.y, 0.0f};
                glm::vec3 p2  = {vertex2.x, vertex2.y, 0.0f};
                p1            = glm::vec3(model * glm::vec4(p1, 1.0f));
                p2            = glm::vec3(model * glm::vec4(p2, 1.0f));
                mesh->draw_line(
                    p1, p2,
                    {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                );
            }
        }
    }
}

void update_b2d_world(
    Query<Get<b2WorldId>> world_query, Local<std::optional<double>> last_time
) {
    if (!world_query) return;
    ZoneScoped;
    auto [world] = world_query.single();
    if (!last_time->has_value()) {
        *last_time = std::chrono::duration<double>(
                         std::chrono::system_clock::now().time_since_epoch()
        )
                         .count();
        return;
    } else {
        auto current_time =
            std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()
            )
                .count();
        auto dt    = current_time - last_time->value();
        dt         = std::min(dt, 0.1);
        *last_time = current_time;
        b2World_Step(world, dt, 6);
    }
}

void destroy_too_far_bodies(
    Query<Get<Entity, b2BodyId>> query,
    Query<Get<b2WorldId>> world_query,
    Command command
) {
    if (!world_query) return;
    ZoneScoped;
    auto [world] = world_query.single();
    for (auto [entity, body] : query.iter()) {
        auto position = b2Body_GetPosition(body);
        if (position.y < -1000.0f || position.y > 1000.0f ||
            position.x < -2000.0f || position.x > 2000.0f) {
            spdlog::info("Destroying body at {}, {}", position.x, position.y);
            b2DestroyBody(body);
            command.entity(entity).despawn();
        }
    }
}

void destroy_b2d_world(Query<Get<b2WorldId>> query) {
    spdlog::info("Destroying b2d world");
    for (auto [world] : query.iter()) {
        b2DestroyWorld(world);
    }
}

enum class SimulateState { Running, Paused };

void toggle_simulation(
    ResMut<NextState<SimulateState>> next_state,
    Query<Get<ButtonInput<KeyCode>>, With<PrimaryWindow>> query
) {
    if (!query) return;
    ZoneScoped;
    auto [key_input] = query.single();
    if (key_input.just_pressed(epix::input::KeyEscape)) {
        if (next_state.has_value()) {
            next_state->set_state(
                next_state->is_state(SimulateState::Running)
                    ? SimulateState::Paused
                    : SimulateState::Running
            );
        }
    }
}

struct MouseJoint {
    b2BodyId body   = b2_nullBodyId;
    b2JointId joint = b2_nullJointId;
};

bool overlap_callback(b2ShapeId shapeId, void* context) {
    auto&& [world, mouse_joint, cursor] =
        *static_cast<std::tuple<b2WorldId, MouseJoint, b2Vec2>*>(context);
    auto body = b2Shape_GetBody(shapeId);
    if (b2Body_GetType(body) != b2BodyType::b2_dynamicBody) return true;
    spdlog::info("Overlap with dynamic");
    b2MouseJointDef def = b2DefaultMouseJointDef();
    def.bodyIdA         = body;
    def.bodyIdB         = body;
    def.target          = cursor;
    def.maxForce        = 100000.0f * b2Body_GetMass(body);
    def.dampingRatio    = 0.7f;
    def.hertz           = 5.0f;
    mouse_joint.joint   = b2CreateMouseJoint(world, &def);
    mouse_joint.body    = body;
    return false;
}
void update_mouse_joint(
    Command command,
    Query<Get<b2WorldId>> world_query,
    Query<Get<Entity, MouseJoint>> mouse_joint_query,
    Query<Get<const Window, const ButtonInput<MouseButton>>> input_query,
    ResMut<epix::imgui::ImGuiContext> imgui_context
) {
    if (!world_query) return;
    if (!input_query) return;
    ZoneScoped;
    auto [world]               = world_query.single();
    auto [window, mouse_input] = input_query.single();
    if (mouse_joint_query) {
        auto [entity, joint] = mouse_joint_query.single();
        b2Vec2 cursor        = b2Vec2(
            window.get_cursor().x - window.get_size().width / 2,
            window.get_size().height / 2 - window.get_cursor().y
        );
        b2MouseJoint_SetTarget(joint.joint, cursor);
        if (!mouse_input.pressed(epix::input::MouseButton2)) {
            spdlog::info("Destroy Mouse Joint");
            b2DestroyJoint(joint.joint);
            command.entity(entity).despawn();
        }
    } else if (mouse_input.just_pressed(epix::input::MouseButton2)) {
        if (imgui_context.has_value()) {
            ImGui::SetCurrentContext(imgui_context->context);
            if (ImGui::GetIO().WantCaptureMouse) return;
        }
        if (mouse_joint_query) return;
        b2AABB aabb   = b2AABB();
        b2Vec2 cursor = b2Vec2(
            window.get_cursor().x - window.get_size().width / 2,
            window.get_size().height / 2 - window.get_cursor().y
        );
        std::tuple<b2WorldId, MouseJoint, b2Vec2> context = {world, {}, cursor};
        spdlog::info("Cursor at {}, {}", cursor.x, cursor.y);
        aabb.lowerBound      = cursor - b2Vec2(0.1f, 0.1f);
        aabb.upperBound      = cursor + b2Vec2(0.1f, 0.1f);
        b2QueryFilter filter = b2DefaultQueryFilter();
        b2World_OverlapAABB(world, aabb, filter, overlap_callback, &context);
        if (b2Joint_IsValid(std::get<1>(context).joint) &&
            b2Body_IsValid(std::get<1>(context).body)) {
            spdlog::info("Create Mouse Joint");
            command.spawn(std::get<1>(context));
            b2MouseJoint_SetTarget(std::get<1>(context).joint, cursor);
        }
    }
}

using namespace epix::world::sand;
using namespace epix::world::sand::components;

constexpr int CHUNK_SIZE                = 16;
constexpr float scale                   = 1.0f;
constexpr bool enable_collision         = true;
constexpr bool render_collision_outline = false;

void create_simulation(Command command) {
    spdlog::info("Creating falling sand simulation");

    ElemRegistry registry;
    registry.register_elem(
        Element::solid("wall")
            .set_color([]() {
                static std::random_device rd;
                static std::mt19937 gen(rd());
                static std::uniform_real_distribution<float> dis(0.24f, 0.32f);
                float r = dis(gen);
                return glm::vec4(r, r, r, 1.0f);
            })
            .set_density(5.0f)
            .set_friction(0.6f)
    );
    registry.register_elem(
        Element::powder("sand")
            .set_color([]() {
                static std::random_device rd;
                static std::mt19937 gen(rd());
                static std::uniform_real_distribution<float> dis(0.6f, 0.8f);
                float r = dis(gen);
                return glm::vec4(r, r, 0.0f, 1.0f);
            })
            .set_density(3.0f)
            .set_friction(0.3f)
            .set_awake_rate(1.0f)
    );
    registry.register_elem(
        Element::powder("grind")
            .set_color([]() {
                static std::random_device rd;
                static std::mt19937 gen(rd());
                static std::uniform_real_distribution<float> dis(0.3f, 0.4f);
                float r = dis(gen);
                return glm::vec4(r, r, r, 1.0f);
            })
            .set_density(0.7f)
            .set_friction(0.3f)
            .set_awake_rate(0.6f)
    );
    registry.register_elem(Element::liquid("water")
                               .set_color([]() {
                                   return glm::vec4(0.0f, 0.0f, 1.0f, 0.4f);
                               })
                               .set_density(1.0f)
                               .set_friction(0.0003f));
    registry.register_elem(Element::liquid("oil")
                               .set_color([]() {
                                   return glm::vec4(0.2f, 0.2f, 0.2f, 0.6f);
                               })
                               .set_density(0.8f)
                               .set_friction(0.0003f));
    registry.register_elem(
        Element::gas("smoke")
            .set_color([]() {
                static std::random_device rd;
                static std::mt19937 gen(rd());
                static std::uniform_real_distribution<float> dis(0.6f, 0.7f);
                float r = dis(gen);
                return glm::vec4(r, r, r, 0.3f);
            })
            .set_density(0.001f)
            .set_friction(0.3f)
    );
    registry.register_elem(Element::gas("steam")
                               .set_color([]() {
                                   return glm::vec4(1.0f, 1.0f, 1.0f, 0.3f);
                               })
                               .set_density(0.0007f)
                               .set_friction(0.3f));
    Simulation simulation(std::move(registry), CHUNK_SIZE);
    const int simulation_size = 512 / CHUNK_SIZE / scale;
    for (int i = -simulation_size; i < simulation_size; i++) {
        for (int j = -simulation_size; j < simulation_size; j++) {
            simulation.load_chunk(i, j);
        }
    }
    command.spawn(
        std::move(simulation),
        epix::world::sand_physics::SimulationCollisionGeneral{}
    );
}

constexpr int input_size = 16;

void create_element_from_click(
    Query<Get<Simulation>> query,
    Query<
        Get<const Window,
            const ButtonInput<MouseButton>,
            const ButtonInput<KeyCode>>> window_query,
    ResMut<epix::imgui::ImGuiContext> imgui_context,
    Local<std::optional<int>> elem_id,
    Local<std::optional<glm::vec2>> last_cursor
) {
    if (!query) return;
    if (!window_query) return;
    ZoneScoped;
    auto [simulation]                     = query.single();
    auto [window, mouse_input, key_input] = window_query.single();
    if (!elem_id->has_value()) {
        *elem_id = 0;
    }
    if (key_input.just_pressed(epix::input::KeyEqual)) {
        elem_id->value() =
            (elem_id->value() + 1) % simulation.registry().count();
        spdlog::info(
            "Current element: {}",
            simulation.registry().elem_name(elem_id->value())
        );
    } else if (key_input.just_pressed(epix::input::KeyMinus)) {
        elem_id->value() =
            (elem_id->value() - 1 + simulation.registry().count()) %
            simulation.registry().count();
        spdlog::info(
            "Current element: {}",
            simulation.registry().elem_name(elem_id->value())
        );
    }
    if (imgui_context.has_value()) {
        ImGui::SetCurrentContext(imgui_context->context);
        imgui_context->sync_context();
        if (ImGui::GetIO().WantCaptureMouse) return;
    }
    if (mouse_input.pressed(epix::input::MouseButton1) ||
        mouse_input.pressed(epix::input::MouseButton2)) {
        auto cursor          = window.get_cursor();
        glm::vec4 cursor_pos = glm::vec4(
            cursor.x - window.get_size().width / 2,
            window.get_size().height / 2 - cursor.y, 0.0f, 1.0f
        );
        glm::mat4 viewport_to_world = glm::inverse(glm::scale(
            glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 0.0f}),
            {scale, scale, 1.0f}
        ));
        glm::vec4 world_pos1        = viewport_to_world * cursor_pos;
        glm::vec2 world_pos2        = world_pos1;
        if (last_cursor->has_value()) {
            world_pos2 = last_cursor->value();
        }
        float distance = std::sqrt(
            (world_pos1.x - world_pos2.x) * (world_pos1.x - world_pos2.x) +
            (world_pos1.y - world_pos2.y) * (world_pos1.y - world_pos2.y)
        );
        int count = std::max(distance * 2 / input_size, 1.0f);
        for (int i = 0; i < count; i++) {
            int cell_x = static_cast<int>(
                world_pos1.x * i / count + world_pos2.x * (count - i) / count
            );
            int cell_y = static_cast<int>(
                world_pos1.y * i / count + world_pos2.y * (count - i) / count
            );
            for (int tx = cell_x - input_size; tx < cell_x + input_size; tx++) {
                for (int ty = cell_y - input_size; ty < cell_y + input_size;
                     ty++) {
                    if (simulation.valid(tx, ty)) {
                        if (mouse_input.pressed(epix::input::MouseButton1)) {
                            simulation.create(
                                tx, ty, CellDef(elem_id->value())
                            );
                        } else if (mouse_input.pressed(epix::input::MouseButton2
                                   )) {
                            simulation.remove(tx, ty);
                        }
                    }
                }
            }
        }
        *last_cursor = world_pos1;
    } else {
        last_cursor->reset();
    }
}

struct RepeatTimer {
    double interval;
    double last_time;

    RepeatTimer(double interval) : interval(interval), last_time() {
        last_time = std::chrono::duration<double>(
                        std::chrono::system_clock::now().time_since_epoch()
        )
                        .count();
    }
    int tick() {
        auto current_time =
            std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()
            )
                .count();
        if (current_time - last_time > interval) {
            int count = (current_time - last_time) / interval;
            last_time += count * interval;
            return count;
        }
        return 0;
    }
};

void update_simulation(
    Query<
        Get<Simulation, epix::world::sand_physics::SimulationCollisionGeneral>>
        query,
    Local<std::optional<RepeatTimer>> timer
) {
    if (!query) return;
    ZoneScoped;
    if (!timer->has_value()) {
        *timer = RepeatTimer(1.0 / 60.0);
    }
    auto [simulation, sim_collisions] = query.single();
    auto count                        = timer->value().tick();
    for (int i = 0; i <= count; i++) {
        ZoneScopedN("Update simulation");
        simulation.update_multithread((float)timer->value().interval);
        if constexpr (enable_collision) {
            sim_collisions.cache(simulation);
        }
        return;
    }
}

void step_simulation(
    Query<Get<Simulation>> query, Query<Get<const ButtonInput<KeyCode>>> query2
) {
    if (!query) return;
    if (!query2) return;
    ZoneScoped;
    auto [simulation] = query.single();
    auto [key_input]  = query2.single();
    if (key_input.just_pressed(epix::input::KeySpace)) {
        spdlog::info("Step simulation");
        simulation.update((float)1.0 / 60.0);
    } else if (key_input.just_pressed(epix::input::KeyC) ||
               (key_input.pressed(epix::input::KeyC) &&
                key_input.pressed(epix::input::KeyLeftAlt))) {
        spdlog::info("Step simulation chunk");
        simulation.init_update_state();
        simulation.update_chunk((float)1.0 / 60.0);
        if (simulation.next_chunk()) {
            simulation.update_chunk((float)1.0 / 60.0);
        } else {
            simulation.deinit_update_state();
        }
    } else if (key_input.just_pressed(epix::input::KeyV) ||
               (key_input.pressed(epix::input::KeyV) &&
                key_input.pressed(epix::input::KeyLeftAlt))) {
        spdlog::info("Reset simulation");
        simulation.init_update_state();
        simulation.update_cell((float)1.0 / 60.0);
        if (simulation.next_cell()) {
            simulation.update_cell((float)1.0 / 60.0);
        } else if (!simulation.next_chunk()) {
            simulation.deinit_update_state();
        }
    }
}

void render_simulation(
    Extract<Get<const Simulation>> query, ResMut<SandMesh> mesh
) {
    if (!query) return;
    if (!mesh) return;
    auto [simulation] = query.single();
    ZoneScopedN("Render simulation");
    for (auto&& [pos, chunk] : simulation.chunk_map()) {
        int offset_x = pos.x * simulation.chunk_size();
        int offset_y = pos.y * simulation.chunk_size();
        for (auto&& [cell_pos, cell] : chunk.cells.view()) {
            mesh->draw_pixel(
                {cell_pos[0] + offset_x, cell_pos[1] + offset_y}, cell.color
            );
        }
    }
}

void print_hover_data(
    Query<Get<const Simulation>> query,
    Query<Get<const Window>, With<PrimaryWindow>> window_query
) {
    if (!query) return;
    if (!window_query) return;
    ZoneScoped;
    auto [simulation] = query.single();
    auto [window]     = window_query.single();
    auto cursor       = window.get_cursor();
    if (!window.focused()) return;
    glm::vec4 cursor_pos = glm::vec4(
        cursor.x - window.get_size().width / 2,
        window.get_size().height / 2 - cursor.y, 0.0f, 1.0f
    );
    glm::mat4 viewport_to_world = glm::inverse(glm::scale(
        glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 0.0f}),
        {scale, scale, 1.0f}
    ));
    glm::vec4 world_pos         = viewport_to_world * cursor_pos;
    int cell_x                  = static_cast<int>(world_pos.x + 0.5f);
    int cell_y                  = static_cast<int>(world_pos.y + 0.5f);
    if (simulation.valid(cell_x, cell_y) &&
        simulation.contain_cell(cell_x, cell_y)) {
        auto [cell, elem] = simulation.get(cell_x, cell_y);
        spdlog::info(
            "Hovering over cell ({}, {}) with element {}, freefall: {}, "
            "velocity: ({}, {}) ",
            cell_x, cell_y, elem.name, cell.freefall, cell.velocity.x,
            cell.velocity.y
        );
    }
}

void render_simulation_chunk_outline(
    Extract<
        Get<const Simulation,
            const epix::world::sand_physics::SimulationCollisionGeneral>> query,
    ResMut<Box2dMesh> mesh
) {
    if (!query) return;
    if (!mesh) return;
    auto [simulation, sim_collisions] = query.single();
    ZoneScopedN("Render simulation collision");
    float alpha = 0.3f;
    for (auto&& [pos, chunk] : simulation.chunk_map()) {
        glm::vec3 origin = glm::vec3(
            pos.x * simulation.chunk_size(), pos.y * simulation.chunk_size(),
            0.0f
        );
        glm::vec4 color = {1.0f, 1.0f, 1.0f, alpha / 4};
        glm::vec3 lb    = glm::vec3(origin.x, origin.y, 0.0f) * scale;
        glm::vec3 rt    = glm::vec3(
                           origin.x + simulation.chunk_size(),
                           origin.y + simulation.chunk_size(), 0.0f
                       ) *
                       scale;
        glm::vec3 lt =
            glm::vec3(origin.x, origin.y + simulation.chunk_size(), 0.0f) *
            scale;
        glm::vec3 rb =
            glm::vec3(origin.x + simulation.chunk_size(), origin.y, 0.0f) *
            scale;
        mesh->draw_line(lb, rb, color);
        mesh->draw_line(rb, rt, color);
        mesh->draw_line(rt, lt, color);
        mesh->draw_line(lt, lb, color);
        if constexpr (render_collision_outline) {
            if (sim_collisions.collisions.contains(pos.x, pos.y)) {
                auto& collision_outline =
                    sim_collisions.collisions.get(pos.x, pos.y).collisions;
                for (auto&& outlines : collision_outline) {
                    for (auto&& outline : outlines) {
                        for (size_t i = 0; i < outline.size(); i++) {
                            auto start = glm::vec3(outline[i], 0.0f) + origin;
                            auto end =
                                glm::vec3(
                                    outline[(i + 1) % outline.size()], 0.0f
                                ) +
                                origin;
                            start *= scale;
                            end *= scale;
                            mesh->draw_line(
                                start, end, {0.0f, 1.0f, 0.0f, alpha}
                            );
                        }
                    }
                }
            }
        }
        if (chunk.should_update()) {
            // orange
            color        = {1.0f, 0.5f, 0.0f, alpha};
            float x1     = chunk.updating_area[0];
            float x2     = chunk.updating_area[1] + 1;
            float y1     = chunk.updating_area[2];
            float y2     = chunk.updating_area[3] + 1;
            glm::vec3 p1 = glm::vec3{x1, y1, 0.0f} + origin;
            glm::vec3 p2 = glm::vec3{x2, y1, 0.0f} + origin;
            glm::vec3 p3 = glm::vec3{x2, y2, 0.0f} + origin;
            glm::vec3 p4 = glm::vec3{x1, y2, 0.0f} + origin;
            p1 *= scale;
            p2 *= scale;
            p3 *= scale;
            p4 *= scale;
            mesh->draw_line(p1, p2, color);
            mesh->draw_line(p2, p3, color);
            mesh->draw_line(p3, p4, color);
            mesh->draw_line(p4, p1, color);
        }
    }
}

void render_simulation_collision(
    Extract<
        Get<const Simulation,
            const epix::world::sand_physics::SimulationCollisionGeneral>> query,
    ResMut<Box2dMesh> mesh
) {
    if (!query) return;
    if (!mesh) return;
    ZoneScoped;
    auto [simulation, sim_collisions] = query.single();
    float alpha                       = 0.3f;
    for (auto&& [pos, chunk] : simulation.chunk_map()) {
        if (sim_collisions.collisions.contains(pos.x, pos.y)) {
            auto body =
                sim_collisions.collisions.get(pos.x, pos.y).user_data.first;
            if (!b2Body_IsValid(body)) continue;
            auto shape_count = b2Body_GetShapeCount(body);
            auto shapes      = new b2ShapeId[shape_count];
            b2Body_GetShapes(body, shapes, shape_count);
            for (int i = 0; i < shape_count; i++) {
                auto shape = shapes[i];
                if (b2Shape_GetType(shape) == b2ShapeType::b2_polygonShape) {
                    auto polygon = b2Shape_GetPolygon(shape);
                    for (size_t i = 0; i < polygon.count; i++) {
                        auto& vertex1 = polygon.vertices[i];
                        auto& vertex2 =
                            polygon.vertices[(i + 1) % polygon.count];
                        bool awake = b2Body_IsAwake(body);
                        mesh->draw_line(
                            {vertex1.x, vertex1.y, 0.0f},
                            {vertex2.x, vertex2.y, 0.0f},
                            {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f,
                             0.3f}
                        );
                    }
                } else if (b2Shape_GetType(shape) ==
                           b2ShapeType::b2_smoothSegmentShape) {
                    auto segment  = b2Shape_GetSmoothSegment(shape);
                    auto& vertex1 = segment.segment.point1;
                    auto& vertex2 = segment.segment.point2;
                    bool awake    = b2Body_IsAwake(body);
                    mesh->draw_line(
                        {vertex1.x, vertex1.y, 0.0f},
                        {vertex2.x, vertex2.y, 0.0f},
                        {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                    );
                } else if (b2Shape_GetType(shape) ==
                           b2ShapeType::b2_segmentShape) {
                    auto segment  = b2Shape_GetSegment(shape);
                    auto& vertex1 = segment.point1;
                    auto& vertex2 = segment.point2;
                    bool awake    = b2Body_IsAwake(body);
                    mesh->draw_line(
                        {vertex1.x, vertex1.y, 0.0f},
                        {vertex2.x, vertex2.y, 0.0f},
                        {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                    );
                }
            }
        }
    }
}

enum class InputState { Simulation, Body };

void toggle_input_state(
    ResMut<NextState<InputState>> next_state,
    Query<Get<ButtonInput<KeyCode>>, With<PrimaryWindow>> query
) {
    if (!query) return;
    ZoneScoped;
    auto [key_input] = query.single();
    if (key_input.just_pressed(epix::input::KeyTab)) {
        if (next_state.has_value()) {
            auto next = next_state->is_state(InputState::Simulation)
                            ? InputState::Body
                            : InputState::Simulation;
            spdlog::info(
                "Input mode switching to {}",
                next == InputState::Simulation ? "Simulation" : "Body"
            );
            next_state->set_state(next);
        }
    }
}

void sync_simulatino_with_b2d(
    Query<
        Get<epix::world::sand_physics::SimulationCollisionGeneral, Simulation>>
        simulation_query,
    Query<Get<b2WorldId>> world_query,
    Local<std::optional<RepeatTimer>> timer
) {
    if (!simulation_query) return;
    if (!world_query) return;
    if (!timer->has_value()) {
        *timer = RepeatTimer(1.0 / 5);
    }
    auto [collisions, sim] = simulation_query.single();
    auto [world]           = world_query.single();
    if (timer->value().tick() > 0) {
        ZoneScopedN("Sync simulation with b2d");
        collisions.sync(sim);
        collisions.sync(
            world, collisions.pos_converter(CHUNK_SIZE, scale, {0, 0})
        );
    }
}

void draw_meshes(
    ResMut<RenderContext> context,
    ResMut<Box2dMeshGPU> b2d_gpu_mesh,
    ResMut<SandMeshGPU> sand_gpu_mesh,
    Res<Box2dMeshStaging> b2d_staging,
    Res<SandMeshStaging> sand_staging,
    ResMut<WholePass> pass,
    Res<VulkanResources> res_mgr
) {
    if (!b2d_staging) return;
    if (!sand_staging) return;
    if (!b2d_gpu_mesh) return;
    if (!sand_gpu_mesh) return;
    if (!pass) return;
    if (!res_mgr) return;
    if (!context) return;
    ZoneScopedN("Draw meshes");
    pass->begin(
        [&](auto& device, auto& renderpass) {
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.setRenderPass(renderpass);
            framebuffer_info.setAttachments(
                context->primary_swapchain.current_image_view()
            );
            framebuffer_info.setWidth(context->primary_swapchain.extent().width
            );
            framebuffer_info.setHeight(
                context->primary_swapchain.extent().height
            );
            framebuffer_info.setLayers(1);
            return device.createFramebuffer(framebuffer_info);
        },
        context->primary_swapchain.extent()
    );
    pass->update_mesh(*b2d_gpu_mesh, *b2d_staging);
    pass->update_mesh(*sand_gpu_mesh, *sand_staging);
    auto& subpass_sand = pass->next_subpass();
    subpass_sand.activate_pipeline(
        0,
        [&](auto& viewports, auto& scissors) {
            viewports.resize(1);
            viewports[0].setWidth(context->primary_swapchain.extent().width);
            viewports[0].setHeight(context->primary_swapchain.extent().height);
            viewports[0].setX(0);
            viewports[0].setY(0);
            viewports[0].setMinDepth(0.0f);
            viewports[0].setMaxDepth(1.0f);
            scissors.resize(1);
            scissors[0].setExtent(context->primary_swapchain.extent());
            scissors[0].setOffset({0, 0});
        },
        [&](auto& device, auto& sets) {
            sets.resize(1);
            vk::DescriptorBufferInfo buffer_info;
            buffer_info.setBuffer(res_mgr->get_buffer("uniform_buffer"));
            buffer_info.setOffset(0);
            buffer_info.setRange(sizeof(glm::mat4) * 2);
            vk::WriteDescriptorSet descriptor_write;
            descriptor_write.setDstSet(sets[0]);
            descriptor_write.setDstBinding(0);
            descriptor_write.setDstArrayElement(0);
            descriptor_write.setDescriptorType(
                vk::DescriptorType::eUniformBuffer
            );
            descriptor_write.setDescriptorCount(1);
            descriptor_write.setPBufferInfo(&buffer_info);
            device.updateDescriptorSets({descriptor_write}, {});
        }
    );
    subpass_sand.draw(
        *sand_gpu_mesh, glm::scale(glm::mat4(1.0f), {scale, scale, 1.0f})
    );
    auto& subpass_b2d = pass->next_subpass();
    subpass_b2d.activate_pipeline(
        0,
        [&](auto& viewports, auto& scissors) {
            viewports.resize(1);
            viewports[0].setWidth(context->primary_swapchain.extent().width);
            viewports[0].setHeight(context->primary_swapchain.extent().height);
            viewports[0].setX(0);
            viewports[0].setY(0);
            viewports[0].setMinDepth(0.0f);
            viewports[0].setMaxDepth(1.0f);
            scissors.resize(1);
            scissors[0].setExtent(context->primary_swapchain.extent());
            scissors[0].setOffset({0, 0});
        },
        [&](auto& device, auto& sets) {
            sets.resize(1);
            vk::DescriptorBufferInfo buffer_info;
            buffer_info.setBuffer(res_mgr->get_buffer("uniform_buffer"));
            buffer_info.setOffset(0);
            buffer_info.setRange(sizeof(glm::mat4) * 2);
            vk::WriteDescriptorSet descriptor_write;
            descriptor_write.setDstSet(sets[0]);
            descriptor_write.setDstBinding(0);
            descriptor_write.setDstArrayElement(0);
            descriptor_write.setDescriptorType(
                vk::DescriptorType::eUniformBuffer
            );
            descriptor_write.setDescriptorCount(1);
            descriptor_write.setPBufferInfo(&buffer_info);
            device.updateDescriptorSets({descriptor_write}, {});
        }
    );
    subpass_b2d.draw(*b2d_gpu_mesh, glm::mat4(1.0f));
    pass->end();
    pass->submit(context->queue);
}

void create_uniform_buffer(
    ResMut<VulkanResources> res_mgr, ResMut<RenderContext> context
) {
    if (!res_mgr) return;
    if (!context) return;
    ZoneScopedN("Create uniform buffer");
    auto& device = context->device;
    auto buffer  = device.createBuffer(
        vk::BufferCreateInfo()
            .setSize(sizeof(glm::mat4) * 2)
            .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
            .setSharingMode(vk::SharingMode::eExclusive),
        backend::AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
    );
    auto data = (glm::mat4*)buffer.map();
    data[0]   = glm::ortho(
        -(float)context->primary_swapchain.extent().width / 2.0f,
        (float)context->primary_swapchain.extent().width / 2.0f,
        (float)context->primary_swapchain.extent().height / 2.0f,
        -(float)context->primary_swapchain.extent().height / 2.0f, -1.0f, 1.0f
    );
    data[1] = glm::mat4(1.0f);
    buffer.unmap();
    res_mgr->add_buffer("uniform_buffer", buffer);
}

void update_uniform_buffer(
    Res<RenderContext> context, Res<VulkanResources> res_mgr
) {
    if (!res_mgr || !context) return;
    auto buffer = res_mgr->get_buffer("uniform_buffer");
    auto data   = (glm::mat4*)buffer.map();
    data[0]     = glm::ortho(
        -(float)context->primary_swapchain.extent().width / 2.0f,
        (float)context->primary_swapchain.extent().width / 2.0f,
        (float)context->primary_swapchain.extent().height / 2.0f,
        -(float)context->primary_swapchain.extent().height / 2.0f, -1.0f, 1.0f
    );
    buffer.unmap();
}

struct Box2dTestPlugin : Plugin {
    void build(App& app) override {
        using namespace epix;

        app.enable_loop();
        app.insert_state(SimulateState::Running);
        app.add_system(Startup, create_b2d_world).chain();
        app.add_system(PreUpdate, update_b2d_world)
            .in_state(SimulateState::Running);
        app.add_system(Update, toggle_input_state);
        app.add_system(Update, destroy_too_far_bodies, toggle_simulation);
        if constexpr (enable_collision)
            app.add_system(PostUpdate, sync_simulatino_with_b2d)
                .in_state(SimulateState::Running);
        app.add_system(Update, create_dynamic_from_click, update_mouse_joint)
            .in_state(InputState::Body);
        app.add_system(Extraction, render_bodies, render_simulation_collision);
        app.add_system(Exit, destroy_b2d_world);
    }
};

struct VK_TrialPlugin : Plugin {
    void build(App& app) override {
        auto window_plugin                   = app.get_plugin<WindowPlugin>();
        window_plugin->primary_desc().width  = 1080;
        window_plugin->primary_desc().height = 1080;
        window_plugin->primary_desc().set_vsync(false);

        using namespace epix;

        app.enable_loop();
        app.add_plugin(Box2dTestPlugin{});

        app.insert_state(SimulateState::Paused);
        app.insert_state(InputState::Simulation);
        app.add_system(Startup, create_simulation);
        app.add_system(
            Update, toggle_simulation /* ,
             print_hover_data */
        );
        app.add_system(Update, create_element_from_click)
            .in_state(InputState::Simulation);
        app.add_system(Update, update_simulation)
            .chain()
            .in_state(SimulateState::Running);
        app.add_system(Update, step_simulation).in_state(SimulateState::Paused);
        app.add_system(PreUpdate, toggle_full_screen);
        app.add_system(
            Extraction, render_simulation, render_simulation_chunk_outline
        );
        spdlog::info(
            "conflict: {}",
            epix::test_systems_conflict(
                render_simulation, render_simulation_chunk_outline
            )
        );
        app.add_system(Startup, create_whole_pass_base, create_whole_pass)
            .chain();
        app.add_system(
            Startup, create_box2d_meshes, create_sand_meshes,
            create_uniform_buffer
        );
        app.add_system(
            PostExtract, extract_whole_pass, extract_box2d_mesh,
            extract_sand_mesh, update_uniform_buffer
        );
        app.add_system(Render, draw_meshes);
        app.add_system(
               Exit, destroy_whole_pass, destroy_box2d_meshes,
               destroy_sand_meshes
        )
            .chain();
    }
};

void run() {
    App app = App::create2();
    app.enable_loop();
    app.set_log_level(spdlog::level::info);
    app.add_plugin(epix::window::WindowPlugin{});
    app.add_plugin(epix::input::InputPlugin{});
    app.add_plugin(epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true
    ));
    // app.add_plugin(epix::font::FontPlugin{});
    app.add_plugin(vk_trial::VK_TrialPlugin{});
    app.add_plugin(epix::imgui::ImGuiPluginVK{});
    // app.add_plugin(pixel_engine::sprite::SpritePluginVK{});
    app.run();
}
}  // namespace vk_trial