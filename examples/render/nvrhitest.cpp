#include <epix/app.h>
#include <epix/render.h>
#include <epix/render/pipeline.h>
#include <epix/window.h>

#include <spirv_cross/spirv_cross.hpp>

namespace shader_codes {
#include "shaders/shader.frag.h"
#include "shaders/shader.vert.h"
}  // namespace shader_codes

using namespace epix;

struct pos {
    float x;
    float y;
};
struct color {
    float r;
    float g;
    float b;
    float a;
};
struct PrimaryWindowId {
    Entity id;
};
struct TestPipeline {
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle fragment_shader;

    nvrhi::InputLayoutHandle input_layout;
    nvrhi::GraphicsPipelineDesc pipeline_desc;

    TestPipeline(World& world) {
        spdlog::info("Creating TestPipeline Resource");

        auto& device = world.resource<nvrhi::DeviceHandle>();

        vertex_shader = device->createShader(
            nvrhi::ShaderDesc()
                .setShaderType(nvrhi::ShaderType::Vertex)
                .setDebugName("vertex_shader")
                .setEntryName("main"),
            shader_codes::vert_spv, sizeof(shader_codes::vert_spv));
        fragment_shader = device->createShader(
            nvrhi::ShaderDesc()
                .setShaderType(nvrhi::ShaderType::Pixel)
                .setDebugName("fragment_shader")
                .setEntryName("main"),
            shader_codes::frag_spv, sizeof(shader_codes::frag_spv));
        auto attributes = std::array{
            nvrhi::VertexAttributeDesc()
                .setName("POSITION")
                .setBufferIndex(0)
                .setFormat(nvrhi::Format::RG32_FLOAT)
                .setOffset(0)
                .setElementStride(sizeof(pos)),
            nvrhi::VertexAttributeDesc()
                .setName("COLOR")
                .setBufferIndex(1)
                .setFormat(nvrhi::Format::RGBA32_FLOAT)
                .setOffset(0)
                .setElementStride(sizeof(color)),
        };
        input_layout  = device->createInputLayout(attributes.data(),
                                                  attributes.size(), nullptr);
        pipeline_desc = nvrhi::GraphicsPipelineDesc()
                            .setVertexShader(vertex_shader)
                            .setPixelShader(fragment_shader)
                            .setInputLayout(input_layout);
    }
};
struct Buffers {
    nvrhi::BufferHandle vertex_buffer[2];
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

        render_app.init_resource<TestPipeline>();
    });

    app.run();
    return 0;
}