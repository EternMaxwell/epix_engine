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

#include "fragment_shader.h"
#include "vertex_shader.h"

using namespace epix::prelude;
using namespace epix::window;
using namespace epix::render_vk::components;
using namespace epix::sprite::components;
using namespace epix::sprite::resources;

namespace vk_trial {
using namespace epix::utils::grid2d;

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

using PixelBlock = epix::render::pixel::components::PixelBlock;
struct PixelBlockWrapper {
    PixelBlock block;
    bool contains(int x, int y) const {
        if (x < 0 || x >= block.size.x || y < 0 || y >= block.size.y)
            return false;
        return block[{x, y}].a > 0.1f;
    }
    glm::ivec2 size() const { return block.size; }
    PixelBlockWrapper(int width, int height)
        : block(PixelBlock::create({width, height})) {}
};

void create_b2d_world(Command command) {
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity    = {0.0f, -29.8f};
    auto world     = b2CreateWorld(&def);
    command.spawn(world);
}

void create_pixel_block_with_collision(
    Command command, Query<Get<b2WorldId>> world_query
) {
    if (!world_query) return;
    auto [world] = world_query.single();
    auto m_block = PixelBlockWrapper(64, 64);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < m_block.size().x; i++) {
        for (int j = 0; j < m_block.size().y; j++) {
            m_block.block[{i, j}] = {
                (float)(dis(gen) / 255.0f), (float)(dis(gen) / 255.0f),
                (float)(dis(gen) / 255.0f), dis(gen) > 127 ? 0.2f : 0.0f
            };
        }
    }
    using binary_grid = Grid2D<bool>;
    auto blocks_bin   = split(binary_grid(m_block));
    std::vector<std::pair<PixelBlockWrapper, glm::ivec2>> blocks;
    for (auto& block : blocks_bin) {
        glm::ivec2 offset;
        auto shrinked = shrink(block, &offset);
        auto size     = shrinked.size();
        auto width = size.x, height = size.y;
        blocks.emplace_back(PixelBlockWrapper(width, height), offset);
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                if (shrinked.contains(i, j)) {
                    blocks.back().first.block[{i, j}] =
                        m_block.block[offset + glm::ivec2(i, j)];
                }
            }
        }
    }
    for (auto&& [block, offset] : blocks) {
        auto outline = find_outline(block);
        outline.push_back(outline[0]);
        auto simplified = douglas_peucker(outline, 0.5f);
        simplified.pop_back();
        auto holes          = find_holes(block);
        auto earcut_polygon = std::vector<std::vector<glm::ivec2>>();
        earcut_polygon.emplace_back(std::move(simplified));
        for (auto& hole : holes) {
            hole.push_back(hole[0]);
            auto hole_simplified = douglas_peucker(hole, 0.8f);
            hole_simplified.pop_back();
            earcut_polygon.emplace_back(std::move(hole_simplified));
        }
        auto triangle_list = mapbox::earcut(earcut_polygon);
        // std::cout << "triangle amount: " << triangle_list.size() / 3
        //           << std::endl;
        // for (size_t i = 0; i < triangle_list.size(); i += 3) {
        //     for (int j = 2; j >= 0; j--) {
        //         std::cout << std::format(
        //             "({},{}) ",
        //             value_at(earcut_polygon, triangle_list[i + j]).x,
        //             value_at(earcut_polygon, triangle_list[i + j]).y
        //         );
        //     }
        //     std::cout << std::endl;
        // }
        b2BodyDef body_def = b2DefaultBodyDef();
        body_def.type      = b2BodyType::b2_dynamicBody;
        body_def.position  = b2Vec2(offset.x, offset.y) * 5 +
                            b2Vec2(-m_block.size().x / 2, 40) * 5;
        auto body = b2CreateBody(world, &body_def);
        for (size_t i = 0; i < triangle_list.size(); i += 3) {
            b2Vec2 points[3];
            for (int j = 2; j >= 0; j--) {
                points[j] = {
                    (float)value_at(earcut_polygon, triangle_list[i + j]).x * 5,
                    (float)value_at(earcut_polygon, triangle_list[i + j]).y * 5
                };
            }
            b2Hull hull           = b2ComputeHull(points, 3);
            b2Polygon polygon     = b2MakePolygon(&hull, 0.0f);
            b2ShapeDef shape_def  = b2DefaultShapeDef();
            shape_def.density     = 1.0f;
            shape_def.friction    = 0.6f;
            shape_def.restitution = 0.1f;
            b2CreatePolygonShape(body, &shape_def, &polygon);
        }
        command.spawn(body, block);
    }
}

void render_pixel_block(
    Query<Get<PixelBlockWrapper, b2BodyId>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query,
    Query<Get<epix::render::pixel::components::PixelRenderer>>
        pixel_renderer_query,
    Query<Get<b2WorldId>> world_query
) {
    if (!context_query) return;
    if (!pixel_renderer_query) return;
    if (!world_query) return;
    auto [b2_world]                  = world_query.single();
    auto [device, swap_chain, queue] = context_query.single();
    auto [pixel_renderer]            = pixel_renderer_query.single();
    epix::render::pixel::components::PixelUniformBuffer uniform_buffer;
    uniform_buffer.proj = glm::ortho(
        -(float)swap_chain.extent.width / 2.0f,
        (float)swap_chain.extent.width / 2.0f,
        (float)swap_chain.extent.height / 2.0f,
        -(float)swap_chain.extent.height / 2.0f, -1000.0f, 1000.0f
    );
    uniform_buffer.view = glm::mat4(1.0f);
    pixel_renderer.begin(device, swap_chain, queue, uniform_buffer);
    for (auto [block, body] : query.iter()) {
        if (!b2Body_IsValid(body)) continue;
        auto position = b2Body_GetPosition(body);
        auto rotation = b2Body_GetRotation(body);
        auto angle    = std::atan2(rotation.s, rotation.c);
        glm::mat4 model =
            glm::translate(glm::mat4(1.0f), {position.x, position.y, 0.0f});
        model = glm::rotate(model, angle, {0.0f, 0.0f, 1.0f});
        model = glm::scale(model, {5.0f, 5.0f, 1.0f});
        pixel_renderer.set_model(model);
        for (int i = 0; i < block.size().x; i++) {
            for (int j = 0; j < block.size().y; j++) {
                auto& color = block.block[{i, j}];
                if (color.a == 0.0f) continue;
                pixel_renderer.draw(color, {i, j});
            }
        }
    }
    pixel_renderer.end();
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

void render_bodies(
    Query<Get<b2BodyId>> query,
    Query<Get<epix::render::debug::vulkan::components::LineDrawer>>
        line_drawer_query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query
) {
    if (!line_drawer_query) return;
    if (!context_query) return;
    auto [device, swap_chain, queue] = context_query.single();
    auto [line_drawer]               = line_drawer_query.single();
    line_drawer.begin(device, queue, swap_chain);
    for (auto [body] : query.iter()) {
        if (!b2Body_IsValid(body)) continue;
        b2Vec2 position  = b2Body_GetPosition(body);
        b2Vec2 velocity  = b2Body_GetLinearVelocity(body);
        auto mass_center = b2Body_GetWorldCenterOfMass(body);
        auto rotation    = b2Body_GetRotation(body);  // a cosine / sine pair
        auto angle       = std::atan2(rotation.s, rotation.c);
        glm::mat4 model  = glm::translate(
            glm::mat4(1.0f), {mass_center.x, mass_center.y, 0.0f}
        );
        line_drawer.setModel(model);
        line_drawer.drawLine(
            {0.0f, 0.0f, 0.0f}, {velocity.x / 2, velocity.y / 2, 0.0f},
            {0.0f, 0.0f, 1.0f, 1.0f}
        );
        model = glm::translate(glm::mat4(1.0f), {position.x, position.y, 0.0f});
        model = glm::rotate(model, angle, {0.0f, 0.0f, 1.0f});
        line_drawer.setModel(model);
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
                    line_drawer.drawLine(
                        {vertex1.x, vertex1.y, 0.0f},
                        {vertex2.x, vertex2.y, 0.0f},
                        {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                    );
                }
            } else if (b2Shape_GetType(shape) ==
                       b2ShapeType::b2_smoothSegmentShape) {
                auto segment  = b2Shape_GetSmoothSegment(shape);
                auto& vertex1 = segment.segment.point1;
                auto& vertex2 = segment.segment.point2;
                bool awake    = b2Body_IsAwake(body);
                line_drawer.drawLine(
                    {vertex1.x, vertex1.y, 0.0f}, {vertex2.x, vertex2.y, 0.0f},
                    {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                );
            } else if (b2Shape_GetType(shape) == b2ShapeType::b2_segmentShape) {
                auto segment  = b2Shape_GetSegment(shape);
                auto& vertex1 = segment.point1;
                auto& vertex2 = segment.point2;
                bool awake    = b2Body_IsAwake(body);
                line_drawer.drawLine(
                    {vertex1.x, vertex1.y, 0.0f}, {vertex2.x, vertex2.y, 0.0f},
                    {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                );
            }
        }
    }
    line_drawer.end();
}

void update_b2d_world(
    Query<Get<b2WorldId>> world_query, Local<std::optional<double>> last_time
) {
    if (!world_query) return;
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
    for (auto [world] : query.iter()) {
        b2DestroyWorld(world);
    }
}

void create_text(
    Command command,
    ResMut<epix::font::resources::vulkan::FT2Library> ft2_library
) {
    if (!ft2_library.has_value()) return;
    epix::font::components::Text text;
    auto font_face =
        ft2_library->load_font("../assets/fonts/FiraSans-Bold.ttf");
    text.font        = epix::font::components::Font{.font_face = font_face};
    text.font.pixels = 32;
    text.text =
        L"Hello, "
        L"Worldajthgreawiohguhiuwearjhoiughjoaewrughiowahioulgerjioweahjgiu"
        L"awhi"
        L"ohiouaewhoiughjwaoigoiehafgioerhiiUWEGHNVIOAHJEDSKGBHJIUAERWHJIUG"
        L"OHoa"
        L"ghweiuo ioweafgioewajiojoiatg huihkljh";
    command.spawn(text, epix::font::components::TextPos{100, 500});
    epix::font::components::Text text2;
    font_face = ft2_library->load_font(
        "../assets/fonts/HachicroUndertaleBattleFontRegular-L3zlg.ttf"
    );
    text2.font        = epix::font::components::Font{.font_face = font_face};
    text2.font.pixels = 32;
    text2.text =
        L"Hello, "
        L"Worldoawgheruiahjgijanglkjwaehjgo;"
        L"ierwhjgiohnweaioulgfhjewjfweg3ioioiwefiowejhoiewfgjoiweaghioweahi"
        L"oawe"
        L"gHJWEAIOUHAWEFGIOULHJEAWio;hWE$gowaejgio";
    command.spawn(text2, epix::font::components::TextPos{100, 200});
}

void shuffle_text(Query<Get<epix::font::components::Text>> text_query) {
    for (auto [text] : text_query.iter()) {
        std::random_device rd;
        std::mt19937 g(rd());
        for (size_t i = 0; i < text.text.size(); i++) {
            std::uniform_int_distribution<> dis(0, text.text.size() - 1);
            text.text[i] = wchar_t(dis(g));
        }
    }
}

void create_pixel_block(Command command) {
    auto block =
        epix::render::pixel::components::PixelBlock::create({512, 512});
    for (size_t i = 0; i < block.size.x; i++) {
        for (size_t j = 0; j < block.size.y; j++) {
            block[{i, j}] = {
                std::rand() / (float)RAND_MAX, std::rand() / (float)RAND_MAX,
                std::rand() / (float)RAND_MAX, 1.0f
            };
        }
    }
    command.spawn(
        std::move(block),
        epix::render::pixel::components::BlockPos2d{
            .pos = {-1000, -1000, 0}, .scale = {4.0f, 4.0f}
        }
    );
}

void imgui_demo_window(ResMut<epix::imgui::ImGuiContext> imgui_context) {
    if (imgui_context.has_value()) {
        ImGui::SetCurrentContext(imgui_context->context);
        ImGui::ShowDemoWindow();
    }
}

void render_pixel_renderer_test(
    Query<Get<epix::render::pixel::components::PixelRenderer>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query
) {
    using namespace epix::render::pixel::components;
    if (!query) return;
    if (!context_query) return;
    auto [device, swap_chain, queue] = context_query.single();
    auto [renderer]                  = query.single();
    PixelUniformBuffer pixel_uniform;
    pixel_uniform.view = glm::mat4(1.0f);
    pixel_uniform.proj = glm::ortho(
        -static_cast<float>(swap_chain.extent.width) / 2.0f,
        static_cast<float>(swap_chain.extent.width) / 2.0f,
        static_cast<float>(swap_chain.extent.height) / 2.0f,
        -static_cast<float>(swap_chain.extent.height) / 2.0f, 1000.0f, -1000.0f
    );
    renderer.begin(device, swap_chain, queue, pixel_uniform);
    renderer.set_model(
        glm::vec2(0.0f, 0.0f), glm::vec2(4.0f), glm::radians(45.0f)
    );
    for (int x = -10; x < 10; x++) {
        for (int y = -10; y < 10; y++) {
            renderer.draw({1.0f, 0.0f, 0.0f, 1.0f}, {x, y});
        }
    }
    renderer.end();
}

enum class SimulateState { Running, Paused };

void toggle_simulation(
    ResMut<NextState<SimulateState>> next_state,
    Query<Get<ButtonInput<KeyCode>>, With<PrimaryWindow>> query
) {
    if (!query) return;
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

constexpr int CHUNK_SIZE                = 32;
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
    const int simulation_size = 512 / CHUNK_SIZE;
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
    if (!timer->has_value()) {
        *timer = RepeatTimer(1.0 / 60.0);
    }
    auto [simulation, sim_collisions] = query.single();
    auto count                        = timer->value().tick();
    for (int i = 0; i <= count; i++) {
        epix::time_scope a("Update simulation");
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
    Query<Get<const Simulation>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query,
    Query<Get<epix::render::pixel::components::PixelRenderer>> renderer_query
) {
    using namespace epix::render::pixel::components;
    if (!query) return;
    if (!context_query) return;
    if (!renderer_query) return;
    auto [device, swap_chain, queue] = context_query.single();
    auto [renderer]                  = renderer_query.single();
    auto [simulation]                = query.single();
    PixelUniformBuffer uniform_buffer;
    uniform_buffer.proj = glm::ortho(
        -static_cast<float>(swap_chain.extent.width) / 2.0f,
        static_cast<float>(swap_chain.extent.width) / 2.0f,
        static_cast<float>(swap_chain.extent.height) / 2.0f,
        -static_cast<float>(swap_chain.extent.height) / 2.0f, 1000.0f, -1000.0f
    );
    uniform_buffer.view = glm::scale(glm::mat4(1.0f), {scale, scale, 1.0f});
    renderer.begin(device, swap_chain, queue, uniform_buffer);
    for (auto&& [pos, chunk] : simulation.chunk_map()) {
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f), {pos.x * simulation.chunk_size(),
                              pos.y * simulation.chunk_size(), 0.0f}
        );
        renderer.set_model(model);
        for (int i = 0; i < chunk.size().x; i++) {
            for (int j = 0; j < chunk.size().y; j++) {
                auto& elem = chunk.get(i, j);
                if (elem)
                    renderer.draw(
                        elem.freefall ? elem.color : elem.color * 0.5f, {i, j}
                    );
            }
        }
    }
    renderer.end();
}

void print_hover_data(
    Query<Get<const Simulation>> query,
    Query<Get<const Window>, With<PrimaryWindow>> window_query
) {
    if (!query) return;
    if (!window_query) return;
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

void render_simulation_cell_vel(
    Query<Get<const Simulation>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query,
    Query<Get<epix::render::debug::vulkan::components::LineDrawer>>
        line_drawer_query
) {
    using namespace epix::render::debug::vulkan::components;
    if (!query) return;
    if (!context_query) return;
    if (!line_drawer_query) return;
    auto [device, swap_chain, queue] = context_query.single();
    auto [line_drawer]               = line_drawer_query.single();
    auto [simulation]                = query.single();
    line_drawer.begin(device, queue, swap_chain);
    float alpha = 0.3f;
    for (auto&& [pos, chunk] : simulation.chunk_map()) {
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f), {pos.x * simulation.chunk_size() * scale,
                              pos.y * simulation.chunk_size() * scale, 0.0f}
        );
        model = glm::scale(model, {scale, scale, 1.0f});
        line_drawer.setModel(model);
        for (int i = 0; i < chunk.size().x; i++) {
            for (int j = 0; j < chunk.size().y; j++) {
                auto& elem = chunk.get(i, j);
                if (elem) {
                    auto& type = simulation.registry().get_elem(elem.elem_id);
                    if (type.name == "grind")
                        line_drawer.drawLine(
                            {.5f + i + elem.inpos.x, .5f + j + elem.inpos.y,
                             0.0f},
                            {.5f + i + elem.inpos.x + elem.velocity.x,
                             .5f + j + elem.inpos.y + elem.velocity.y, 0.0f},
                            {0.0f, 0.0f, 1.0f, alpha}
                        );
                }
            }
        }
    }
    line_drawer.end();
}

void render_simulation_state(
    Query<Get<const Simulation>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query,
    Query<Get<epix::render::debug::vulkan::components::LineDrawer>>
        line_drawer_query
) {
    using namespace epix::render::debug::vulkan::components;
    if (!query) return;
    if (!context_query) return;
    if (!line_drawer_query) return;
    auto [device, swap_chain, queue] = context_query.single();
    auto [line_drawer]               = line_drawer_query.single();
    auto [simulation]                = query.single();
    line_drawer.begin(device, queue, swap_chain);
    float alpha = 0.5f;
    if (simulation.updating_state().is_updating) {
        auto&& post = simulation.updating_state().current_chunk();
        if (post) {
            auto&& pos      = post.value();
            auto& chunk     = simulation.chunk_map().get_chunk(pos.x, pos.y);
            glm::mat4 model = glm::translate(
                glm::mat4(1.0f), {pos.x * simulation.chunk_size() * scale,
                                  pos.y * simulation.chunk_size() * scale, 0.0f}
            );
            model           = glm::scale(model, {scale, scale, 1.0f});
            glm::vec4 color = {0.0f, 0.0f, 1.0f, alpha / 2};
            line_drawer.setModel(model);
            line_drawer.drawLine(
                {0.0f, 0.0f, 0.0f},
                {0.0f, static_cast<float>(simulation.chunk_size()), 0.0f}, color
            );
            line_drawer.drawLine(
                {0.0f, 0.0f, 0.0f},
                {static_cast<float>(simulation.chunk_size()), 0.0f, 0.0f}, color
            );
            line_drawer.drawLine(
                {static_cast<float>(simulation.chunk_size()), 0.0f, 0.0f},
                {static_cast<float>(simulation.chunk_size()),
                 static_cast<float>(simulation.chunk_size()), 0.0f},
                color
            );
            line_drawer.drawLine(
                {0.0f, static_cast<float>(simulation.chunk_size()), 0.0f},
                {static_cast<float>(simulation.chunk_size()),
                 static_cast<float>(simulation.chunk_size()), 0.0f},
                color
            );
            if (chunk.should_update()) {
                color    = {1.0f, 0.0f, 0.0f, alpha};
                float x1 = chunk.updating_area[0];
                float x2 = chunk.updating_area[1] + 1;
                float y1 = chunk.updating_area[2];
                float y2 = chunk.updating_area[3] + 1;
                line_drawer.drawLine({x1, y1, 0.0f}, {x2, y1, 0.0f}, color);
                line_drawer.drawLine({x2, y1, 0.0f}, {x2, y2, 0.0f}, color);
                line_drawer.drawLine({x2, y2, 0.0f}, {x1, y2, 0.0f}, color);
                line_drawer.drawLine({x1, y2, 0.0f}, {x1, y1, 0.0f}, color);
            }
        }
    }
    line_drawer.end();
}

void render_simulation_chunk_outline(
    Query<
        Get<const Simulation,
            const epix::world::sand_physics::SimulationCollisionGeneral>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query,
    Query<Get<epix::render::debug::vulkan::components::LineDrawer>>
        line_drawer_query
) {
    using namespace epix::render::debug::vulkan::components;
    if (!query) return;
    if (!context_query) return;
    if (!line_drawer_query) return;
    auto [device, swap_chain, queue]  = context_query.single();
    auto [line_drawer]                = line_drawer_query.single();
    auto [simulation, sim_collisions] = query.single();
    line_drawer.begin(device, queue, swap_chain);
    float alpha = 0.3f;
    for (auto&& [pos, chunk] : simulation.chunk_map()) {
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f), {pos.x * simulation.chunk_size() * scale,
                              pos.y * simulation.chunk_size() * scale, 0.0f}
        );
        model           = glm::scale(model, {scale, scale, 1.0f});
        glm::vec4 color = {1.0f, 1.0f, 1.0f, alpha / 4};
        line_drawer.setModel(model);
        line_drawer.drawLine(
            {0.0f, 0.0f, 0.0f},
            {0.0f, static_cast<float>(simulation.chunk_size()), 0.0f}, color
        );
        line_drawer.drawLine(
            {0.0f, 0.0f, 0.0f},
            {static_cast<float>(simulation.chunk_size()), 0.0f, 0.0f}, color
        );
        line_drawer.drawLine(
            {static_cast<float>(simulation.chunk_size()), 0.0f, 0.0f},
            {static_cast<float>(simulation.chunk_size()),
             static_cast<float>(simulation.chunk_size()), 0.0f},
            color
        );
        line_drawer.drawLine(
            {0.0f, static_cast<float>(simulation.chunk_size()), 0.0f},
            {static_cast<float>(simulation.chunk_size()),
             static_cast<float>(simulation.chunk_size()), 0.0f},
            color
        );
        if constexpr (render_collision_outline) {
            if (sim_collisions.collisions.contains(pos.x, pos.y)) {
                auto& collision_outline =
                    sim_collisions.collisions.get(pos.x, pos.y).collisions;
                for (auto&& outlines : collision_outline) {
                    for (auto&& outline : outlines) {
                        for (size_t i = 0; i < outline.size(); i++) {
                            auto start = outline[i];
                            auto end   = outline[(i + 1) % outline.size()];
                            line_drawer.drawLine(
                                {start.x, start.y, 0.0f}, {end.x, end.y, 0.0f},
                                {0.0f, 1.0f, 0.0f, alpha}
                            );
                        }
                    }
                }
            }
        }
        if (chunk.should_update()) {
            // orange
            color    = {1.0f, 0.5f, 0.0f, alpha};
            float x1 = chunk.updating_area[0];
            float x2 = chunk.updating_area[1] + 1;
            float y1 = chunk.updating_area[2];
            float y2 = chunk.updating_area[3] + 1;
            line_drawer.drawLine({x1, y1, 0.0f}, {x2, y1, 0.0f}, color);
            line_drawer.drawLine({x2, y1, 0.0f}, {x2, y2, 0.0f}, color);
            line_drawer.drawLine({x2, y2, 0.0f}, {x1, y2, 0.0f}, color);
            line_drawer.drawLine({x1, y2, 0.0f}, {x1, y1, 0.0f}, color);
        }
    }
    line_drawer.end();
}

void render_simulation_collision(
    Query<
        Get<const Simulation,
            const epix::world::sand_physics::SimulationCollisionGeneral>> query,
    Query<Get<Device, Swapchain, Queue>, With<RenderContext>> context_query,
    Query<Get<epix::render::debug::vulkan::components::LineDrawer>>
        line_drawer_query
) {
    using namespace epix::render::debug::vulkan::components;
    if (!query) return;
    if (!context_query) return;
    if (!line_drawer_query) return;
    auto [device, swap_chain, queue]  = context_query.single();
    auto [line_drawer]                = line_drawer_query.single();
    auto [simulation, sim_collisions] = query.single();
    line_drawer.begin(device, queue, swap_chain);
    float alpha = 0.3f;
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
                        line_drawer.drawLine(
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
                    line_drawer.drawLine(
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
                    line_drawer.drawLine(
                        {vertex1.x, vertex1.y, 0.0f},
                        {vertex2.x, vertex2.y, 0.0f},
                        {awake ? 0.0f : 1.0f, awake ? 1.0f : 0.0f, 0.0f, 0.3f}
                    );
                }
            }
        }
    }
    line_drawer.end();
}

enum class InputState { Simulation, Body };

void toggle_input_state(
    ResMut<NextState<InputState>> next_state,
    Query<Get<ButtonInput<KeyCode>>, With<PrimaryWindow>> query
) {
    if (!query) return;
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
        epix::time_scope a("Sync simulation with b2d");
        collisions.sync(sim);
        collisions.sync(
            world, collisions.pos_converter(CHUNK_SIZE, scale, {0, 0})
        );
    }
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
        app.add_system(Render, render_pixel_block);
        app.add_system(PostRender, render_bodies, render_simulation_collision)
            .before(epix::render_vk::systems::present_frame);
        app.add_system(Exit, destroy_b2d_world);
    }
};

struct VK_TrialPlugin : Plugin {
    void build(App& app) override {
        auto window_plugin                   = app.get_plugin<WindowPlugin>();
        window_plugin->primary_desc().width  = 1080;
        window_plugin->primary_desc().height = 1080;
        // window_plugin->primary_window_vsync = false;

        using namespace epix;

        app.enable_loop();
        // app.add_system(Startup, create_text);
        // app.add_system(Startup, create_pixel_block);
        // app.add_system(Render, draw_lines);
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
               Render, render_simulation, render_simulation_chunk_outline /* ,
                render_simulation_cell_vel, render_simulation_state */
        )
            .chain()
            .before(render_bodies, render_pixel_block);
    }
};

void run() {
    App app = App::create();
    app.enable_loop();
    app.set_log_level(spdlog::level::info);
    app.add_plugin(epix::window::WindowPlugin{});
    app.add_plugin(epix::input::InputPlugin{});
    app.add_plugin(epix::render_vk::RenderVKPlugin{});
    app.add_plugin(epix::render::debug::vulkan::DebugRenderPlugin{});
    app.add_plugin(epix::font::FontPlugin{});
    app.add_plugin(vk_trial::VK_TrialPlugin{});
    app.add_plugin(epix::imgui::ImGuiPluginVK{});
    app.add_plugin(epix::render::pixel::PixelRenderPlugin{});
    // app.add_plugin(pixel_engine::sprite::SpritePluginVK{});
    app.run();
}
}  // namespace vk_trial