// Compiles every shader the engine has loaded, on a device.
//
// `ShaderCache` compiles an entry point per definition set, so a shader branch
// is only ever compiled when something asks for it. Nothing in the suite asked,
// which is how `pbr/mesh.slang` kept two defects that made the whole module
// unusable — the HLSL `SV_VertexID` (Slang then declares `OpCapability
// DrawParameters`, which naga rejects) and column-major `float3x4`/`float2x4`
// members (naga: "unsupported matrix stride 8 for a 2x4 matrix") — while every
// asset-level test passed. This test asks, for every shader that has entry
// points, so a rejected module fails here instead of only when something renders
// with it.

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::shader;

namespace render = epix::render;

namespace {

// Loading an embedded shader spawns an async load task, which terminates the
// process when the IO task pool has not been initialized.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_io_task_pool_init;

/** @brief The shader's text, or nullptr for SPIR-V bytecode and serialized Slang
 *  IR, which carry no entry points to compile. */
const std::string* shader_text(const Shader& shader) {
    if (const auto* slang = std::get_if<Source::Slang>(&shader.source.data)) return &slang->code;
    if (const auto* wgsl = std::get_if<Source::Wgsl>(&shader.source.data)) return &wgsl->code;
    return nullptr;
}

/** @brief Whether the module declares an entry point. A library has none, and
 *  compiling one would ask Slang for a stage that does not exist. */
bool has_entry_points(const std::string& text) {
    return text.find("[shader(\"") != std::string::npos || text.find("@vertex") != std::string::npos ||
           text.find("@fragment") != std::string::npos || text.find("@compute") != std::string::npos;
}

/** @brief Whether any entry point sits behind a preprocessor branch.
 *
 *  A module whose code is definition-gated cannot be compiled bare — Slang
 *  reports the missing definitions rather than a stage (`pbr/mesh.slang` needs
 *  `MESH_PIPELINE`, `VERTEX_POSITIONS`, … before either entry point is valid).
 *  Those modules are compiled by the family test that owns their definition set
 *  (`tests/pbr/header/mesh_shader_tests.cpp` compiles nine mesh variants); this
 *  sweep covers the modules that must compile as written. */
bool is_definition_gated(const std::string& text) {
    return text.find("#ifdef") != std::string::npos || text.find("#ifndef") != std::string::npos ||
           text.find("#if ") != std::string::npos;
}

}  // namespace

TEST(ShaderCompile, EveryLoadedShaderCompilesOnADevice) {
    App app = App::create();
    // Unprocessed mode, as the other shader-loading fixtures use: the embedded
    // source has to be reachable by path when the modules are loaded.
    AssetPlugin asset_plugin;
    asset_plugin.mode = AssetServerMode::Unprocessed;
    app.add_plugins(asset_plugin);
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::mesh::MeshPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    // After the render plugin, so the shader loader and the embedded registry are
    // in place before the modules are published and loaded. The plugin families
    // here are the ones whose shaders the suite otherwise only ever loads: the
    // core pipeline's blit / tonemapping / mip generation, the mesh modules, the
    // material modules, and the 2D sprite renderer's.
    app.add_plugins(epix::core_graph::CoreGraphPlugin{});
    app.add_plugins(epix::pbr::MeshRenderPlugin{});
    app.add_plugins(epix::sprite_render::SpriteRenderPlugin{});
    epix::pbr::register_mesh_shaders(app.world_mut());
    // The shader assets load asynchronously, and the cache is filled from them.
    for (int attempt = 0; attempt < 10000; ++attempt) {
        app.run_schedule(Last);
        std::this_thread::yield();
    }

    // The backend step of the pipeline server's own loader
    // (`epix_engine/render/render/src/pipeline_server.cpp:60-82`): Slang compiles
    // to SPIR-V, which is what naga validates, so this is the step that rejects a
    // module for an unsupported capability or matrix stride.
    ShaderCache cache(app.resource<wgpu::Device>().clone(),
                      [](const wgpu::Device& device, const ShaderCacheSource& source,
                         ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          wgpu::ShaderModuleDescriptor descriptor;
                          if (const auto* wgsl = std::get_if<ShaderCacheSource::Wgsl>(&source.data)) {
                              descriptor.setNextInChain(wgpu::ShaderSourceWGSL().setCode(wgpu::StringView(wgsl->source)));
                          } else {
                              const auto& bytes = std::get<ShaderCacheSource::SpirV>(source.data).bytes;
                              descriptor.setNextInChain(
                                  wgpu::ShaderSourceSPIRV()
                                      .setCode(reinterpret_cast<const std::uint32_t*>(bytes.data()))
                                      .setCodeSize(static_cast<std::uint32_t>(bytes.size() / sizeof(std::uint32_t))));
                          }
                          auto module = device.createShaderModule(descriptor);
                          if (!module) {
                              return std::unexpected(
                                  ShaderCacheError::create_module_failed("WebGPU createShaderModule failed"));
                          }
                          return module;
                      });

    auto& shaders = app.resource<Assets<Shader>>();
    const auto ids = shaders.ids();
    ASSERT_GT(ids.size(), 5u) << "the embedded shader modules should be loaded by now";
    for (const auto id : ids) {
        const auto shader = shaders.get(id);
        ASSERT_TRUE(shader.has_value());
        cache.set_shader(id, shader->get());
    }

    std::size_t compiled = 0;
    std::size_t gated   = 0;
    for (const auto id : ids) {
        const auto shader = shaders.get(id);
        ASSERT_TRUE(shader.has_value());
        const auto* text = shader_text(shader->get());
        if (text == nullptr || !has_entry_points(*text)) continue;
        if (is_definition_gated(*text)) {
            ++gated;
            continue;
        }
        const auto module = cache.get(render::CachedPipelineId{}, id, {});
        EXPECT_TRUE(module.has_value()) << "compiling '" << shader->get().import_path.module_name() << "' failed";
        ++compiled;
    }
    EXPECT_GT(compiled, 0u) << "no definition-independent shader with entry points was compiled, so this test "
                               "proved nothing";
    std::cout << "[shader-compile] " << compiled << " module(s) compiled bare, " << gated
              << " definition-gated module(s) left to their family tests" << std::endl;
}
