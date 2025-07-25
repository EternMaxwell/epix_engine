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

    nvrhi::GraphicsPipelineHandle pipeline;

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
        input_layout = device->createInputLayout(attributes.data(),
                                                 attributes.size(), nullptr);
        pipeline_desc =
            nvrhi::GraphicsPipelineDesc()
                .setVertexShader(vertex_shader)
                .setPixelShader(fragment_shader)
                .setRenderState(nvrhi::RenderState().setDepthStencilState(
                    nvrhi::DepthStencilState()
                        .setDepthTestEnable(false)
                        .setDepthWriteEnable(false)))
                .setInputLayout(input_layout);
    }
};
struct Buffers {
    nvrhi::BufferHandle vertex_buffer[2];
    uint32_t vertex_count = 0;

    Buffers(World& world) {
        spdlog::info("Creating Buffers Resource");

        auto& device = world.resource<nvrhi::DeviceHandle>();

        vertex_buffer[0] = device->createBuffer(
            nvrhi::BufferDesc()
                .setDebugName("vertex_buffer_0")
                .setByteSize(sizeof(pos) * 3)
                .setIsVertexBuffer(true)
                .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                .setKeepInitialState(true));
        vertex_buffer[1] = device->createBuffer(
            nvrhi::BufferDesc()
                .setDebugName("vertex_buffer_1")
                .setByteSize(sizeof(color) * 3)
                .setIsVertexBuffer(true)
                .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                .setKeepInitialState(true));

        auto pos_data =
            std::array{pos{0.0f, 0.5f}, pos{-0.5f, -0.5f}, pos{0.5f, -0.5f}};
        auto color_data = std::array{color{1.0f, 0.0f, 0.0f, 1.0f},
                                     color{0.0f, 1.0f, 0.0f, 1.0f},
                                     color{0.0f, 0.0f, 1.0f, 1.0f}};

        auto commandlist = device->createCommandList();
        commandlist->open();
        commandlist->writeBuffer(vertex_buffer[0], pos_data.data(),
                                 sizeof(pos) * pos_data.size(), 0);
        commandlist->writeBuffer(vertex_buffer[1], color_data.data(),
                                 sizeof(color) * color_data.size(), 0);
        commandlist->close();
        device->executeCommandList(commandlist);

        vertex_count = static_cast<uint32_t>(pos_data.size());
    }
};
struct CurrentFramebuffer {
    nvrhi::FramebufferHandle framebuffer;
};

int main() {
    App app = App::create();
    app.add_plugins(window::WindowPlugin{.primary_window = window::Window{
                                             .title = "nvrhi Test",
                                         }});
    app.add_plugins(glfw::GLFWPlugin{});
    app.add_plugins(input::InputPlugin{});
    app.add_plugins(render::RenderPlugin{}.set_validation(1));

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
        render_app.add_systems(
            render::Render,
            into([](ResMut<CurrentFramebuffer> framebuffer,
                    Res<PrimaryWindowId> primary_window_id,
                    Res<nvrhi::DeviceHandle> nvrhi_device,
                    Res<render::window::ExtractedWindows> windows) {
                auto it = windows->windows.find(primary_window_id->id);
                if (it == windows->windows.end()) {
                    framebuffer->framebuffer = nullptr;
                    throw std::runtime_error(
                        "Primary window not found in extracted windows!");
                }
                auto& device             = nvrhi_device.get();
                framebuffer->framebuffer = device->createFramebuffer(
                    nvrhi::FramebufferDesc().addColorAttachment(
                        it->second.swapchain_texture));
            })
                .in_set(render::RenderSet::ManageViews)
                .set_name("create framebuffer"));
        render_app.add_systems(
            render::Render,
            into([](Res<CurrentFramebuffer> framebuffer,
                    ResMut<TestPipeline> pipeline,
                    Res<nvrhi::DeviceHandle> nvrhi_device) {
                auto& device = nvrhi_device.get();
                if (!framebuffer->framebuffer) {
                    throw std::runtime_error("Framebuffer not created!");
                }
                if (!pipeline->pipeline) {
                    pipeline->pipeline = device->createGraphicsPipeline(
                        pipeline->pipeline_desc, framebuffer->framebuffer);
                }
            })
                .in_set(render::RenderSet::Prepare)
                .set_name("create pipeline if not exists"));
        render_app.add_systems(
            render::Render,
            into([](Res<CurrentFramebuffer> framebuffer,
                    Res<TestPipeline> pipeline, Res<Buffers> buffers,
                    Res<nvrhi::DeviceHandle> nvrhi_device) {
                auto& device = nvrhi_device.get();
                if (!framebuffer->framebuffer) {
                    throw std::runtime_error("Framebuffer not created!");
                }
                if (!pipeline->pipeline) {
                    throw std::runtime_error("Pipeline not created!");
                }

                auto commandlist = device->createCommandList();
                commandlist->open();
                commandlist->clearTextureFloat(
                    framebuffer->framebuffer->getDesc()
                        .colorAttachments[0]
                        .texture,
                    nvrhi::TextureSubresourceSet(), 0.0f);
                commandlist->setGraphicsState(
                    nvrhi::GraphicsState()
                        .setFramebuffer(framebuffer->framebuffer)
                        .setPipeline(pipeline->pipeline)
                        .setViewport(
                            nvrhi::ViewportState().addViewportAndScissorRect(
                                framebuffer->framebuffer->getFramebufferInfo()
                                    .getViewport()))
                        .addVertexBuffer(
                            nvrhi::VertexBufferBinding()
                                .setBuffer(buffers->vertex_buffer[0])
                                .setSlot(0)
                                .setOffset(0))
                        .addVertexBuffer(
                            nvrhi::VertexBufferBinding()
                                .setBuffer(buffers->vertex_buffer[1])
                                .setSlot(1)
                                .setOffset(0)));
                commandlist->draw(nvrhi::DrawArguments().setVertexCount(
                    buffers->vertex_count));
                commandlist->close();
                device->executeCommandList(commandlist);
            })
                .in_set(render::RenderSet::Render)
                .set_name("render test pipeline"));

        render_app.init_resource<TestPipeline>();
        render_app.init_resource<Buffers>();
        render_app.init_resource<CurrentFramebuffer>();
    });

    app.run();
    return 0;
}