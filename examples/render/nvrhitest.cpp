#include <epix/app.h>
#include <epix/image.h>
#include <epix/render.h>
#include <epix/render/pipeline.h>
#include <epix/transform/plugin.h>
#include <epix/window.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
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

struct TestPhaseItem {
    Entity id;
    render::RenderPipelineId pipeline_id;
    render::render_phase::DrawFunctionId draw_function_id;

    Entity entity() const { return id; }
    render::RenderPipelineId pipeline() const { return pipeline_id; }
    render::render_phase::DrawFunctionId draw_function() const { return draw_function_id; }
    float sort_key() const { return 0.0f; }
};

static_assert(render::render_phase::PhaseItem<TestPhaseItem>);
static_assert(render::render_phase::CachedRenderPipelinePhaseItem<TestPhaseItem>);

nvrhi::BindingLayoutHandle binding_layout;

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
        binding_layout =
            device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
                                            .addItem(nvrhi::BindingLayoutItem::Sampler(0))
                                            .addItem(nvrhi::BindingLayoutItem::PushConstants(0, 2 * sizeof(glm::mat4)))
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

        auto pos_data =
            std::array{pos{-5.f, -5.f}, pos{5.f, -5.f}, pos{5.f, 5.f}, pos{-5.f, -5.f}, pos{-5.f, 5.f}, pos{5.f, 5.f}};
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

enum class TestGraphNodes {
    DrawTestItems,
};
struct TestItemsNode : render::graph::Node {
    std::optional<Query<Item<render::camera::ExtractedCamera,
                             Mut<render::render_phase::RenderPhase<TestPhaseItem>>,
                             render::view::ViewTarget>>>
        views;
    void update(World& world) override {
        // prepare draw functions
        auto& draw_functions = world.resource<render::render_phase::DrawFunctions<TestPhaseItem>>();
        draw_functions.prepare(world);
        views.emplace(world);
    }
    void run(render::graph::GraphContext& graph_context,
             render::graph::RenderContext& render_context,
             World& world) override {
        auto&& view_entity = graph_context.view_entity();

        auto opt = views.value().try_get(view_entity);
        if (!opt.has_value()) {
            return;
        }
        auto&& [camera, phase, view_target]  = *opt;
        auto&& buffers                       = world.resource<Buffers>();
        auto&& pipelines                     = world.resource<render::PipelineServer>();
        auto&& pipeline                      = world.resource<TestPipeline>();
        nvrhi::FramebufferHandle framebuffer = render_context.device()->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(view_target.texture));
        render_context.flush_encoder();
        auto state = nvrhi::GraphicsState()
                         .setFramebuffer(framebuffer)
                         .setViewport(camera.viewport
                                          .transform([](const render::camera::Viewport& vp) {
                                              nvrhi::Viewport viewport;
                                              viewport.minX = static_cast<float>(vp.pos.x);
                                              viewport.minY = static_cast<float>(vp.pos.y);
                                              viewport.maxX = static_cast<float>(vp.pos.x + vp.size.x);
                                              viewport.maxY = static_cast<float>(vp.pos.y + vp.size.y);
                                              viewport.minZ = vp.depth_range.first;
                                              viewport.maxZ = vp.depth_range.second;
                                              return nvrhi::ViewportState().addViewportAndScissorRect(viewport);
                                          })
                                          .value_or(nvrhi::ViewportState().addViewportAndScissorRect(
                                              nvrhi::Viewport(camera.viewport_size.x, camera.viewport_size.y))))
                         .addVertexBuffer(
                             nvrhi::VertexBufferBinding().setBuffer(buffers.vertex_buffer[0]).setSlot(0).setOffset(0))
                         .addVertexBuffer(
                             nvrhi::VertexBufferBinding().setBuffer(buffers.vertex_buffer[1]).setSlot(1).setOffset(0));

        auto draw_context = render::render_phase::DrawContext{render_context.commands(), state};
        phase.render(draw_context, world, view_entity);
        render_context.flush_encoder();
    }
};
template <typename T = TestPhaseItem>
struct BindingSetCommand {
    nvrhi::BindingSetHandle binding_set;
    nvrhi::SamplerHandle sampler;
    void prepare(World& world) {
        if (binding_set) return;
        auto& device = world.resource<nvrhi::DeviceHandle>();

        // create sampler if not created
        if (!sampler) {
            sampler = device->createSampler(nvrhi::SamplerDesc().setAllFilters(false));
        }

        if (!binding_layout) {
            spdlog::error("Binding layout is not created");
            return;
        }
        auto& image = world.resource<render::assets::RenderAssets<image::Image>>().get(image_handle);
        binding_set =
            device->createBindingSet(nvrhi::BindingSetDesc()
                                         .addItem(nvrhi::BindingSetItem::Texture_SRV(1, image))
                                         .addItem(nvrhi::BindingSetItem::Sampler(0, sampler))
                                         .addItem(nvrhi::BindingSetItem::PushConstants(0, 2 * sizeof(glm::mat4)))
                                         .setTrackLiveness(true),
                                     binding_layout);
    }
    void render(const T& item, Item<>, std::optional<Item<>>, ParamSet<>, render::render_phase::DrawContext& ctx) {
        if (!binding_set) {
            throw std::runtime_error("Binding set is not created");
        }
        ctx.graphics_state.bindings.resize(0);
        ctx.commandlist->setGraphicsState(ctx.graphics_state.addBindingSet(binding_set));
    }
};
template <typename T = TestPhaseItem>
struct PushConstant {
    void render(const T& item,
                Item<render::view::ExtractedView>& view,
                std::optional<Item<>>,
                ParamSet<>,
                render::render_phase::DrawContext& ctx) {
        // set push constant
        auto&& [extracted_view] = view;
        struct PushConstants {
            glm::mat4 proj;
            glm::mat4 view;
        } pc;
        pc.proj = extracted_view.projection;
        pc.view = extracted_view.transform.matrix;
        ctx.commandlist->setPushConstants(&pc, sizeof(PushConstants));
    }
};
template <typename T = TestPhaseItem>
struct DrawCommand {
    void render(const T& item,
                Item<>,
                std::optional<Item<>>,
                ParamSet<Res<Buffers>> bs,
                render::render_phase::DrawContext& ctx) {
        auto&& [buffers] = bs.get();
        ctx.commandlist->draw(nvrhi::DrawArguments().setVertexCount(buffers->vertex_count));
    }
};
inline struct TestGraphLabelT {
} TestGraphLabel;
void queue_render_phase(Query<Item<Entity>, With<render::camera::ExtractedCamera, render::view::ViewTarget>> views,
                        Commands cmd,
                        Res<TestPipeline> pipeline,
                        ResMut<render::render_phase::DrawFunctions<TestPhaseItem>> draw_functions) {
    for (auto&& [entity] : views.iter()) {
        render::render_phase::RenderPhase<TestPhaseItem> phase;
        phase.add(TestPhaseItem{
            .id          = entity,
            .pipeline_id = pipeline->get_id(),
            .draw_function_id =
                render::render_phase::get_or_add_render_commands<TestPhaseItem, render::render_phase::SetItemPipeline,
                                                                 BindingSetCommand, PushConstant, DrawCommand>(
                    *draw_functions),
        });
        cmd.entity(entity).emplace(std::move(phase));
    }
}
struct TestGraph {
    void build(App& app) {
        if (auto render_app = app.get_sub_app(render::Render); render_app) {
            // add test phase item and its draw function
            render_app->insert_resource(render::render_phase::RenderPhase<TestPhaseItem>{});
            render_app->insert_resource(render::render_phase::DrawFunctions<TestPhaseItem>{});

            // add test graph
            auto& graph = render_app->resource<render::graph::RenderGraph>();
            graph.add_sub_graph(TestGraphLabel, render::graph::RenderGraph{});
            auto& test_graph = graph.sub_graph(TestGraphLabel);
            test_graph.add_node(TestGraphNodes::DrawTestItems, TestItemsNode{});
        }
    }
};

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

    app.add_plugins(TestGraph{});
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
        render_app.add_systems(render::Render,
                               into(queue_render_phase).in_set(render::RenderSet::Queue).set_name("queue test phase"));

        render_app.init_resource<Buffers>();
    });

    app.run();
    return 0;
}