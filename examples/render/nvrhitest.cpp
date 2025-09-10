#include <epix/app.h>
#include <epix/image.h>
#include <epix/render.h>
#include <epix/render/pipeline.h>
#include <epix/transform/plugin.h>
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
struct uv {
    float x;
    float y;
};
struct PrimaryWindowId {
    Entity id;
};

const std::string shader_path(__FILE__ "\\..\\shaders");
struct TestPipelineShaders {
    assets::Handle<render::Shader> vertex_shader;
    assets::Handle<render::Shader> fragment_shader;
};

struct TestPipeline {
   private:
    render::RenderPipelineId id;

   public:
    TestPipeline(render::RenderPipelineId id) : id(id) {}
    render::RenderPipelineId get_id() const { return id; }

    static TestPipeline create(nvrhi::DeviceHandle device,
                               TestPipelineShaders shaders,
                               render::PipelineServer& pipeline_server) {
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
                .setFormat(nvrhi::Format::RG32_FLOAT)
                .setOffset(0)
                .setElementStride(sizeof(uv)),
        };
        auto input_layout = device->createInputLayout(attributes.data(), attributes.size(), nullptr);
        auto binding_layout =
            device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                            .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                            .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0})
                                            .setVisibility(nvrhi::ShaderType::All));
        render::RenderPipelineDesc pipeline_desc;
        pipeline_desc
            .setRenderState(
                nvrhi::RenderState()
                    .setDepthStencilState(
                        nvrhi::DepthStencilState().setDepthTestEnable(false).setDepthWriteEnable(false))
                    .setRasterState(
                        nvrhi::RasterState().setCullMode(nvrhi::RasterCullMode::None).setFrontCounterClockwise(true)))
            .addBindingLayout(binding_layout)
            .setInputLayout(input_layout);

        pipeline_desc.vertexShader.shader      = shaders.vertex_shader;
        pipeline_desc.vertexShader.debugName   = "vertex_shader";
        pipeline_desc.vertexShader.entryName   = "main";
        pipeline_desc.fragmentShader.shader    = shaders.fragment_shader;
        pipeline_desc.fragmentShader.debugName = "fragment_shader";
        pipeline_desc.fragmentShader.entryName = "main";

        return TestPipeline(pipeline_server.queue_render_pipeline(pipeline_desc));
    }
};
struct Buffers {
    nvrhi::BufferHandle vertex_buffer[2];
    uint32_t vertex_count = 0;

    Buffers(World& world) {
        spdlog::info("Creating Buffers Resource");

        auto& device = world.resource<nvrhi::DeviceHandle>();

        vertex_buffer[0] = device->createBuffer(nvrhi::BufferDesc()
                                                    .setDebugName("vertex_buffer_0")
                                                    .setByteSize(sizeof(pos) * 6)
                                                    .setIsVertexBuffer(true)
                                                    .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                    .setKeepInitialState(true));
        vertex_buffer[1] = device->createBuffer(nvrhi::BufferDesc()
                                                    .setDebugName("vertex_buffer_1")
                                                    .setByteSize(sizeof(uv) * 6)
                                                    .setIsVertexBuffer(true)
                                                    .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                    .setKeepInitialState(true));

        auto pos_data = std::array{pos{-0.5f, -0.5f}, pos{0.5f, -0.5f}, pos{0.5f, 0.5f},
                                   pos{-0.5f, -0.5f}, pos{-0.5f, 0.5f}, pos{0.5f, 0.5f}};
        auto uv_data =
            std::array{uv{0.0f, 0.0f}, uv{1.0f, 0.0f}, uv{1.0f, 1.0f}, uv{0.0f, 0.0f}, uv{0.0f, 1.0f}, uv{1.0f, 1.0f}};

        auto commandlist = device->createCommandList();
        commandlist->open();
        commandlist->writeBuffer(vertex_buffer[0], pos_data.data(), sizeof(pos) * pos_data.size(), 0);
        commandlist->writeBuffer(vertex_buffer[1], uv_data.data(), sizeof(uv) * uv_data.size(), 0);
        commandlist->close();
        device->executeCommandList(commandlist);

        vertex_count = static_cast<uint32_t>(pos_data.size());
    }
};
struct CurrentFramebuffer {
    nvrhi::FramebufferHandle framebuffer;
};

struct ShaderPluginTest {
    assets::Handle<render::Shader> shader;
};

assets::Handle<image::Image> image_handle =
    assets::AssetId<image::Image>(uuids::uuid::from_string("2e4fbfdf-da16-4546-96b0-f1a5e2fc35b8").value());

enum class TestGraph {};
inline struct TestGraphLabelT {
} TestGraphLabel;

int main() {
    App app = App::create();
    app.add_plugins(window::WindowPlugin{.primary_window = window::Window{
                                             .title = "nvrhi Test",
                                         }});
    app.add_plugins(glfw::GLFWPlugin{});
    app.add_plugins(input::InputPlugin{});
    app.add_plugins(render::RenderPlugin{}.set_validation(1));
    app.add_plugins(assets::AssetPlugin{});
    app.add_plugins(render::ShaderPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(render::PipelineServerPlugin{});
    app.add_plugins(transform::TransformPlugin{});

    app.add_plugins([](App& app) {
        app.insert_resource(ShaderPluginTest{});
        app.spawn(render::camera::CameraBundle::with_render_graph(TestGraphLabel));
        app.add_systems(
               Startup, into([](Commands cmd, Res<assets::AssetServer> asset_server) {
                   cmd.insert_resource(TestPipelineShaders{
                       .vertex_shader   = asset_server->load<render::Shader>(shader_path + "\\shader.vert.spv"),
                       .fragment_shader = asset_server->load<render::Shader>(shader_path + "\\shader.frag.spv"),
                   });
               }))
            .add_systems(Startup, into([](ResMut<assets::Assets<image::Image>> assets) {
                             auto image = image::Image::srgba8unorm_render(2, 2);
                             image.set_data(0, 0, 2, 2,
                                            std::span<const uint8_t>(std::vector<uint8_t>{
                                                0xff, 0x00, 0xff, 0xff,  // purple
                                                0x00, 0x00, 0x00, 0xff,  // black
                                                0x00, 0x00, 0x00, 0xff,  // black
                                                0xff, 0x00, 0xff, 0xff   // purple
                                            }));
                             image.flip_vertical();
                             assets->insert(image_handle, std::move(image));
                         }));
        app.add_systems(Update, into([](EventReader<assets::AssetEvent<render::Shader>> events) {
                            for (const auto& event : events.read()) {
                                if (event.is_loaded()) {
                                    spdlog::info("Shader {} loaded", event.id.to_string());
                                } else if (event.is_removed()) {
                                    spdlog::info("Shader {} removed", event.id.to_string());
                                }
                            }
                        }));

        auto& render_app = app.sub_app(render::Render);
        render_app.insert_resource(PrimaryWindowId{});
        render_app.add_systems(
            render::ExtractSchedule,
            into([](Commands cmd, Extract<Res<TestPipelineShaders>> shaders, Res<nvrhi::DeviceHandle> nvrhi_device,
                    std::optional<Res<TestPipeline>> pipeline, ResMut<render::PipelineServer> pipeline_server) {
                if (pipeline) return;
                cmd.insert_resource(TestPipeline::create(nvrhi_device.get(), shaders.get(), pipeline_server.get()));
            }).set_name("create test pipeline"));
        render_app.add_systems(
            render::Render, into([loaded = false](Res<render::assets::RenderAssets<image::Image>> textures) mutable {
                if (!loaded) {
                    auto ptexture = textures->try_get(image_handle);
                    if (ptexture) {
                        auto& texture = *ptexture;
                        spdlog::info("Image loaded: {}\n\t with size: {}x{}", image_handle.id().to_string(),
                                     texture->getDesc().width, texture->getDesc().height);
                    }
                    loaded = true;
                }
            }));
        render_app.add_systems(
            render::ExtractSchedule,
            into([](ResMut<PrimaryWindowId> extracted_window_id,
                    Extract<Query<Get<Entity>, With<window::PrimaryWindow, window::Window>>> windows) {
                for (auto&& [entity] : windows.iter()) {
                    extracted_window_id->id = entity;
                    static bool found       = false;
                    if (!found) {
                        spdlog::info("Primary window ID: {}", entity.index());
                        found = true;
                    }
                    return;
                }
                throw std::runtime_error("No primary window found in the query!");
            }).set_name("extract primary window id"));
        render_app.add_systems(
            render::Render,
            into([](ResMut<CurrentFramebuffer> framebuffer, Res<PrimaryWindowId> primary_window_id,
                    Res<nvrhi::DeviceHandle> nvrhi_device, Res<render::window::ExtractedWindows> windows) {
                auto it = windows->windows.find(primary_window_id->id);
                if (it == windows->windows.end()) {
                    framebuffer->framebuffer = nullptr;
                    throw std::runtime_error("Primary window not found in extracted windows!");
                }
                auto& device             = nvrhi_device.get();
                framebuffer->framebuffer = device->createFramebuffer(
                    nvrhi::FramebufferDesc().addColorAttachment(it->second.swapchain_texture));
            })
                .in_set(render::RenderSet::ManageViews)
                .after(render::window::prepare_windows)
                .set_name("create framebuffer"));
        // render_app.add_systems(
        //     render::Render,
        //     into([](Res<CurrentFramebuffer> framebuffer,
        //             ResMut<TestPipeline> pipeline,
        //             Res<nvrhi::DeviceHandle> nvrhi_device) {
        //         auto& device = nvrhi_device.get();
        //         if (!framebuffer->framebuffer) {
        //             throw std::runtime_error("Framebuffer not created!");
        //         }
        //         if (!pipeline->pipeline) {
        //             pipeline->pipeline = device->createGraphicsPipeline(
        //                 pipeline->pipeline_desc, framebuffer->framebuffer);
        //         }
        //     })
        //         .in_set(render::RenderSet::Prepare)
        //         .set_name("create pipeline if not exists"));
        render_app.add_systems(
            render::Render,
            into([](Res<CurrentFramebuffer> framebuffer, Res<TestPipeline> pipeline, Res<Buffers> buffers,
                    Res<nvrhi::DeviceHandle> nvrhi_device, Res<render::assets::RenderAssets<image::Image>> textures,
                    Res<render::PipelineServer> pipeline_server, Local<nvrhi::SamplerHandle> sampler) {
                auto& device = nvrhi_device.get();
                if (!framebuffer->framebuffer) {
                    throw std::runtime_error("Framebuffer not created!");
                }

                auto pipe = pipeline_server->get_render_pipeline(pipeline->get_id(), framebuffer->framebuffer);

                if (!pipe) {
                    // spdlog::warn("Pipeline not created!");
                    return;
                }
                {
                    auto& pip = *pipe;
                    if (pip.specializedIndex == -1) {
                        spdlog::error("Failed to specialize pipeline");
                    }
                }
                {
                    if (!textures->try_get(image_handle)) {
                        // spdlog::warn("Texture not loaded!");
                        return;
                    }
                }
                {
                    if (!*sampler) {
                        *sampler = device->createSampler(nvrhi::SamplerDesc().setAllFilters(false));
                    }
                }

                auto bindingSet = device->createBindingSet(
                    nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, textures->get(image_handle)))
                        .addItem(nvrhi::BindingSetItem::Sampler(1, *sampler))
                        .setTrackLiveness(true),
                    pipe->handle->getDesc().bindingLayouts[0]);
                auto commandlist = device->createCommandList();
                commandlist->open();
                commandlist->clearTextureFloat(framebuffer->framebuffer->getDesc().colorAttachments[0].texture,
                                               nvrhi::TextureSubresourceSet(), 0.05f);
                commandlist->setGraphicsState(
                    nvrhi::GraphicsState()
                        .setFramebuffer(framebuffer->framebuffer)
                        .setPipeline(pipe->handle)
                        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                            framebuffer->framebuffer->getFramebufferInfo().getViewport()))
                        .addBindingSet(bindingSet)
                        .addVertexBuffer(
                            nvrhi::VertexBufferBinding().setBuffer(buffers->vertex_buffer[0]).setSlot(0).setOffset(0))
                        .addVertexBuffer(
                            nvrhi::VertexBufferBinding().setBuffer(buffers->vertex_buffer[1]).setSlot(1).setOffset(0)));
                commandlist->draw(nvrhi::DrawArguments().setVertexCount(buffers->vertex_count));
                commandlist->close();
                device->executeCommandList(commandlist);
            })
                .in_set(render::RenderSet::Render)
                .set_name("render test pipeline"));

        render_app.init_resource<Buffers>();
        render_app.init_resource<CurrentFramebuffer>();
    });

    app.run();
    return 0;
}