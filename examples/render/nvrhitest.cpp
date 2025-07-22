#include <epix/app.h>
#include <epix/render.h>
#include <epix/render/pipeline.h>
#include <epix/window.h>

using namespace epix;

struct PrimaryWindowId {
    Entity id;
};
struct TestPipeline {
    nvrhi::GraphicsPipelineDesc descriptor() const {
        return nvrhi::GraphicsPipelineDesc().setPrimType(
            nvrhi::PrimitiveType::TriangleList);
    }
    std::string_view key() const { return "TestPipeline"; }
};

int main() {
    App app = App::create();
    app.add_plugins(window::WindowPlugin{.primary_window = window::Window{
                                             .opacity = 0.6f,
                                             .title   = "nvrhi Test",
                                         }});
    app.add_plugins(glfw::GLFWPlugin{});
    app.add_plugins(input::InputPlugin{});
    app.add_plugins(render::RenderPlugin{}.enable_validation(false));

    app.add_plugins([](App& app) {
        auto& render_app = app.sub_app(render::Render);
        render_app.insert_resource(PrimaryWindowId{});
        render_app.add_systems(
            render::ExtractSchedule,
            into([](ResMut<PrimaryWindowId> extracted_window_id,
                    Extract<Query<Get<Entity>, With<window::PrimaryWindow,
                                                    window::Window>>> windows) {
                for (auto&& [entity] : windows.iter()) {
                    extracted_window_id->id = entity;
                    static bool found       = false;
                    if (!found) {
                        spdlog::info("Primary window ID: {}", entity.index());
                        found = true;
                    }
                    return;
                }
                throw std::runtime_error(
                    "No primary window found in the query!");
            }).set_name("extract primary window id"));
    });

    app.run();
    return 0;
}