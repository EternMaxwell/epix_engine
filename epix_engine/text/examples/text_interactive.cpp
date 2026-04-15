import std;
import webgpu;
import epix.assets;
import epix.core;
import epix.core_graph;
import epix.input;
import epix.mesh;
import epix.render;
import epix.sprite;
import epix.text;
import epix.transform;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import glm;

using namespace epix;

// Marker components
struct MainText {};
struct BoundsOutline {};
struct TextOutline {};
struct InfoText {};

// Drag state resource
enum class DragMode {
    None,
    DragText,
    DragBoundsLeft,
    DragBoundsRight,
    DragBoundsTop,
    DragBoundsBottom,
};

struct DragState {
    DragMode mode = DragMode::None;
    glm::vec2 drag_offset{0.0f};
    // Store entity IDs
    core::Entity text_entity;
    core::Entity bounds_outline_entity;
    core::Entity text_outline_entity;
    core::Entity info_entity;
    // Mesh handles
    std::optional<assets::Handle<mesh::Mesh>> bounds_mesh;
    std::optional<assets::Handle<mesh::Mesh>> text_outline_mesh;
};

struct CamControllPlugin {
    void build(core::App& app) {
        app.add_systems(
            core::Update,
            core::into([](core::Query<core::Item<const render::camera::Camera&, render::camera::Projection&,
                                                 transform::Transform&>> camera,
                          core::EventReader<input::MouseScroll> scroll_input,
                          core::Res<input::ButtonInput<input::KeyCode>> key_states) {
                if (auto opt = camera.single(); opt.has_value()) {
                    auto&& [cam, proj, trans] = *opt;
                    if (key_states->pressed(input::KeyCode::KeySpace)) {
                        trans.translation = glm::vec3(0, 0, 0);
                        proj.as_orthographic().transform([&](render::camera::OrthographicProjection* ortho) {
                            *ortho = render::camera::OrthographicProjection{};
                            return true;
                        });
                        return;
                    }
                    glm::vec3 delta(0.0f);
                    if (key_states->pressed(input::KeyCode::KeyW)) {
                        delta += glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyS)) {
                        delta -= glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyA)) {
                        delta -= glm::vec3(0.1f, 0, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyD)) {
                        delta += glm::vec3(0.1f, 0, 0);
                    }
                    if (glm::length(delta) > 0.0f) {
                        delta = glm::normalize(delta) * 0.1f;
                        trans.translation += delta;
                    }
                    proj.as_orthographic().transform([&](render::camera::OrthographicProjection* ortho) {
                        for (const auto& e : scroll_input.read()) {
                            float scale = std::exp(-static_cast<float>(e.yoffset) * 0.1f);
                            ortho->scale *= scale;
                        }
                        return true;
                    });
                }
            }).set_name("camera control"));
    }
};

// Convert screen coordinates to world coordinates
glm::vec2 screen_to_world(glm::vec2 screen_pos,
                          glm::vec2 window_size,
                          const render::camera::Camera& camera,
                          const render::camera::Projection& projection,
                          const transform::Transform& cam_transform) {
    // Normalize to NDC [-1, 1]
    float ndc_x = (screen_pos.x / window_size.x) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (screen_pos.y / window_size.y) * 2.0f;  // Flip Y

    glm::mat4 proj_matrix = camera.computed.projection;
    glm::mat4 view_matrix = glm::inverse(cam_transform.to_matrix());
    glm::mat4 vp_inv      = glm::inverse(proj_matrix * view_matrix);

    glm::vec4 world = vp_inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return glm::vec2(world.x / world.w, world.y / world.w);
}

// Create a line-strip mesh forming a rectangle outline
mesh::Mesh make_rect_outline(float left, float right, float bottom, float top, glm::vec4 color) {
    std::vector<glm::vec3> positions = {
        {left, bottom, 0.0f}, {right, bottom, 0.0f}, {right, top, 0.0f},
        {left, top, 0.0f},    {left, bottom, 0.0f},  // Close the loop
    };
    std::vector<glm::vec4> colors(5, color);
    auto m = mesh::Mesh(wgpu::PrimitiveTopology::eLineStrip);
    m.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions).value();
    m.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors).value();
    return m;
}

int main(int argc, char** argv) {
    int render_validation = 0;
    if (argc > 1) {
        std::string_view arg1 = argv[1];
        if (arg1 == "--render-validation=1") {
            render_validation = 1;
        } else if (arg1 == "--render-validation=2") {
            render_validation = 2;
        }
    }

    core::App app = core::App::create();
    app.add_plugins(window::WindowPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(render_validation))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(text::TextPlugin{})
        .add_plugins(text::TextRenderPlugin{});

    app.world_mut().spawn(core_graph::core_2d::Camera2DBundle{});

    // Setup: spawn text entity, outline meshes, info text
    app.add_systems(
        core::PreStartup,
        core::into([&](core::Commands cmd, core::Res<assets::AssetServer> asset_server,
                       core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
            // Load font
            auto font_handle = asset_server->load<text::font::Font>("embedded://fonts/default.ttf");

            // Spawn main text
            auto text_entity = cmd.spawn(text::TextBundle{.text{"Hello, Epix Engine!\nInteractive text test."},
                                                          .font{
                                                              .font            = font_handle,
                                                              .size            = 36.0f,
                                                              .line_height     = 36.0f,
                                                              .relative_height = false,
                                                          },
                                                          .layout{.justify = text::Justify::Left},
                                                          .bounds{.width = 400.0f}},
                                         text::Text2d{}, transform::Transform{}, text::TextColor{}, MainText{})
                                   .id();

            // Bounds outline mesh (will be updated each frame)
            auto bounds_mesh_handle = meshes->emplace(make_rect_outline(-200, 200, -50, 50, {0.0f, 1.0f, 0.0f, 1.0f}));
            auto bounds_outline_entity =
                cmd.spawn(mesh::Mesh2d{bounds_mesh_handle},
                          mesh::MeshMaterial2d{.color = glm::vec4(1.0f), .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                          transform::Transform{.translation = glm::vec3(0.0f, 0.0f, 0.1f)}, BoundsOutline{})
                    .id();

            // Text outline mesh (shaped text bounding box, updated each frame)
            auto text_outline_handle = meshes->emplace(make_rect_outline(-100, 100, -25, 25, {1.0f, 1.0f, 0.0f, 1.0f}));
            auto text_outline_entity =
                cmd.spawn(mesh::Mesh2d{text_outline_handle},
                          mesh::MeshMaterial2d{.color = glm::vec4(1.0f), .alpha_mode = mesh::MeshAlphaMode2d::Blend},
                          transform::Transform{.translation = glm::vec3(0.0f, 0.0f, 0.1f)}, TextOutline{})
                    .id();

            // Info text (shows coordinates)
            auto info_entity =
                cmd.spawn(text::TextBundle{.text{"Info: ..."},
                                           .font{
                                               .font            = font_handle,
                                               .size            = 18.0f,
                                               .line_height     = 18.0f,
                                               .relative_height = false,
                                           },
                                           .layout{.justify = text::Justify::Left}},
                          text::Text2d{}, transform::Transform{.translation = glm::vec3(-400.0f, 300.0f, 0.0f)},
                          text::TextColor{.r = 0.7f, .g = 0.9f, .b = 1.0f, .a = 1.0f}, InfoText{})
                    .id();

            // Store state
            DragState state;
            state.text_entity           = text_entity;
            state.bounds_outline_entity = bounds_outline_entity;
            state.text_outline_entity   = text_outline_entity;
            state.info_entity           = info_entity;
            state.bounds_mesh           = std::move(bounds_mesh_handle);
            state.text_outline_mesh     = std::move(text_outline_handle);
            cmd.insert_resource(std::move(state));
        })
            .before(text::font::FontSystems::AddFontAtlasSet)
            .before(assets::AssetSystems::WriteEvents));

    // Interaction system: handle mouse drag for text and bounds
    app.add_systems(
        core::Update,
        core::into(
            [](core::ResMut<DragState> drag_state, core::Res<input::ButtonInput<input::MouseButton>> mouse_input,
               core::Query<core::Item<const window::CachedWindow&>, core::With<window::PrimaryWindow>> window_query,
               core::ParamSet<core::Query<core::Item<const render::camera::Camera&, const render::camera::Projection&,
                                                     const transform::Transform&>>,
                              core::Query<core::Item<transform::Transform&, text::TextBounds&, const text::ShapedText&>,
                                          core::With<MainText>>> conflicting_queries,
               core::ResMut<assets::Assets<mesh::Mesh>> meshes,
               core::Query<core::Item<core::Mut<text::Text>>, core::With<InfoText>> info_query) {
                // Get queries from ParamSet (allows access conflict between camera and text transforms)
                auto&& [camera_query, text_query] = conflicting_queries.get();

                // Get camera
                auto cam_opt = camera_query.single();
                if (!cam_opt) return;
                auto&& [cam, proj, cam_transform] = *cam_opt;

                // Get window for cursor position
                auto win_opt = window_query.single();
                if (!win_opt) return;
                auto&& [window] = *win_opt;

                auto [cursor_x, cursor_y] = window.cursor_pos;
                auto [win_w, win_h]       = window.size;
                if (win_w <= 0 || win_h <= 0) return;

                glm::vec2 world_cursor = screen_to_world(
                    glm::vec2(static_cast<float>(cursor_x), static_cast<float>(cursor_y)),
                    glm::vec2(static_cast<float>(win_w), static_cast<float>(win_h)), cam, proj, cam_transform);

                // Get text transform and shaped text info
                auto text_opt = text_query.single();
                if (!text_opt) return;
                auto&& [text_transform, text_bounds, shaped_text] = *text_opt;

                glm::vec2 text_pos = glm::vec2(text_transform.translation);

                // Compute bounds rect in world space
                float bounds_w      = text_bounds.width.value_or(shaped_text.width());
                float bounds_h      = text_bounds.height.value_or(shaped_text.height());
                float bounds_left   = text_pos.x;
                float bounds_right  = text_pos.x + bounds_w;
                float bounds_top    = text_pos.y;
                float bounds_bottom = text_pos.y - bounds_h;

                // Shaped text rect in world space
                float st_left   = text_pos.x + shaped_text.left();
                float st_right  = text_pos.x + shaped_text.right();
                float st_top    = text_pos.y + shaped_text.top();
                float st_bottom = text_pos.y + shaped_text.bottom();

                constexpr float edge_threshold = 10.0f;

                // Handle mouse press - start drag
                if (mouse_input->just_pressed(input::MouseButton::MouseButtonLeft)) {
                    // Check edge hits for bounds resizing
                    if (std::abs(world_cursor.x - bounds_left) < edge_threshold && world_cursor.y >= bounds_bottom &&
                        world_cursor.y <= bounds_top) {
                        drag_state->mode        = DragMode::DragBoundsLeft;
                        drag_state->drag_offset = glm::vec2(world_cursor.x - bounds_left, 0.0f);
                    } else if (std::abs(world_cursor.x - bounds_right) < edge_threshold &&
                               world_cursor.y >= bounds_bottom && world_cursor.y <= bounds_top) {
                        drag_state->mode        = DragMode::DragBoundsRight;
                        drag_state->drag_offset = glm::vec2(world_cursor.x - bounds_right, 0.0f);
                    } else if (std::abs(world_cursor.y - bounds_top) < edge_threshold &&
                               world_cursor.x >= bounds_left && world_cursor.x <= bounds_right) {
                        drag_state->mode        = DragMode::DragBoundsTop;
                        drag_state->drag_offset = glm::vec2(0.0f, world_cursor.y - bounds_top);
                    } else if (std::abs(world_cursor.y - bounds_bottom) < edge_threshold &&
                               world_cursor.x >= bounds_left && world_cursor.x <= bounds_right) {
                        drag_state->mode        = DragMode::DragBoundsBottom;
                        drag_state->drag_offset = glm::vec2(0.0f, world_cursor.y - bounds_bottom);
                    }
                    // Otherwise check if inside text bounds area - drag text
                    else if (world_cursor.x >= bounds_left && world_cursor.x <= bounds_right &&
                             world_cursor.y >= bounds_bottom && world_cursor.y <= bounds_top) {
                        drag_state->mode        = DragMode::DragText;
                        drag_state->drag_offset = world_cursor - text_pos;
                    }
                }

                // Handle mouse release
                if (mouse_input->just_released(input::MouseButton::MouseButtonLeft)) {
                    drag_state->mode = DragMode::None;
                }

                // Apply drag
                if (mouse_input->pressed(input::MouseButton::MouseButtonLeft)) {
                    switch (drag_state->mode) {
                        case DragMode::DragText: {
                            glm::vec2 new_pos          = world_cursor - drag_state->drag_offset;
                            text_transform.translation = glm::vec3(new_pos, 0.0f);
                            break;
                        }
                        case DragMode::DragBoundsRight: {
                            float new_right = world_cursor.x - drag_state->drag_offset.x;
                            float new_width = new_right - text_pos.x;
                            if (new_width > 20.0f) {
                                text_bounds.width = new_width;
                            }
                            break;
                        }
                        case DragMode::DragBoundsLeft: {
                            // Move text position and adjust width to keep right edge fixed
                            float new_left  = world_cursor.x - drag_state->drag_offset.x;
                            float new_width = bounds_right - new_left;
                            if (new_width > 20.0f) {
                                text_transform.translation.x = new_left;
                                text_bounds.width            = new_width;
                            }
                            break;
                        }
                        case DragMode::DragBoundsTop: {
                            // Height from top to bottom, top is at text_pos.y
                            // Moving top changes text_pos.y and adjusts height
                            float new_top    = world_cursor.y - drag_state->drag_offset.y;
                            float new_height = new_top - bounds_bottom;
                            if (new_height > 20.0f) {
                                text_transform.translation.y = new_top;
                                text_bounds.height           = new_height;
                            }
                            break;
                        }
                        case DragMode::DragBoundsBottom: {
                            float new_bottom = world_cursor.y - drag_state->drag_offset.y;
                            float new_height = bounds_top - new_bottom;
                            if (new_height > 20.0f) {
                                text_bounds.height = new_height;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }

                // Recalculate positions after drag
                glm::vec2 new_text_pos = glm::vec2(text_transform.translation);
                float new_bounds_w     = text_bounds.width.value_or(shaped_text.width());
                float new_bounds_h     = text_bounds.height.value_or(shaped_text.height());

                // Update bounds outline mesh
                {
                    auto new_mesh =
                        make_rect_outline(0.0f, new_bounds_w, -new_bounds_h, 0.0f, {0.0f, 1.0f, 0.0f, 1.0f});
                    if (drag_state->bounds_mesh)
                        (void)meshes->insert(drag_state->bounds_mesh->id(), std::move(new_mesh));
                }

                // Update text outline mesh (shaped text bounds)
                {
                    auto new_mesh = make_rect_outline(shaped_text.left(), shaped_text.right(), shaped_text.bottom(),
                                                      shaped_text.top(), {1.0f, 1.0f, 0.0f, 1.0f});
                    if (drag_state->text_outline_mesh)
                        (void)meshes->insert(drag_state->text_outline_mesh->id(), std::move(new_mesh));
                }

                // Update outline transforms to follow text
                // bounds_outline and text_outline share the text entity's position
                // We set their transforms via the text entity's position (they don't have their own query here,
                // so we use cmd.entity - but since we can't use Commands in this system signature,
                // we just make the outlines use the same transform as the text).
                // Actually let's just update it differently - the outlines are relative to text_pos.

                // Update info text
                if (auto opt = info_query.single(); opt) {
                    auto&& [info_text] = *opt;
                    auto& content      = info_text.get_mut().content;
                    content            = std::format(
                        "Cursor: ({:.1f}, {:.1f})\n"
                        "Text pos: ({:.1f}, {:.1f})\n"
                        "Bounds: {:.1f} x {:.1f}\n"
                        "Shaped: {:.1f} x {:.1f}\n"
                        "Drag: {}",
                        world_cursor.x, world_cursor.y, new_text_pos.x, new_text_pos.y, new_bounds_w, new_bounds_h,
                        shaped_text.width(), shaped_text.height(), static_cast<int>(drag_state->mode));
                }
            })
            .set_name("text interaction"));

    // Sync outline transforms to text position
    app.add_systems(
        core::Update,
        core::into([](core::Res<DragState> drag_state,
                      core::ParamSet<core::Query<core::Item<const transform::Transform&>, core::With<MainText>>,
                                     core::Query<core::Item<transform::Transform&>, core::With<BoundsOutline>>,
                                     core::Query<core::Item<transform::Transform&>, core::With<TextOutline>>>
                          transform_queries) {
            auto&& [text_pos_query, bounds_outline_query, text_outline_query] = transform_queries.get();

            auto text_opt = text_pos_query.single();
            if (!text_opt) return;
            auto&& [text_transform] = *text_opt;

            if (auto opt = bounds_outline_query.single(); opt) {
                auto&& [t]    = *opt;
                t.translation = glm::vec3(glm::vec2(text_transform.translation), 0.1f);
            }
            if (auto opt = text_outline_query.single(); opt) {
                auto&& [t]    = *opt;
                t.translation = glm::vec3(glm::vec2(text_transform.translation), 0.1f);
            }
        }).set_name("sync outline transforms"));

    app.run();
}
