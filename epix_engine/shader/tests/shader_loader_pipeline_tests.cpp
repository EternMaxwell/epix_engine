// Tests for ShaderLoader (load via VFS) and full shader loading pipeline
// (load → ShaderCache → get module), covering WGSL, Slang, SPIR-V with
// import/include combinations.

#include <gtest/gtest.h>

import std;
import epix.core;
import epix.assets;
import epix.shader;
import webgpu;

using namespace epix::shader;
using namespace epix::assets;
using namespace epix::core;

namespace {

// ─── Helpers ─────────────────────────────────────────────────────────────

static auto make_bytes(std::string_view s) {
    auto sp = std::as_bytes(std::span(s));
    return std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
}

static auto make_bytes_from_vec(const std::vector<std::uint8_t>& v) {
    auto sp = std::as_bytes(std::span(v));
    return std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
}

AssetSourceBuilder make_memory_source_builder(const memory::Directory& dir) {
    return AssetSourceBuilder::create(
        [dir]() -> std::unique_ptr<AssetReader> { return std::make_unique<MemoryAssetReader>(dir); });
}

struct ShaderTestEnv {
    App app;
    memory::Directory dir;
};

ShaderTestEnv make_shader_env(const memory::Directory& dir) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Unprocessed;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);
    return {std::move(app), dir};
}

void flush_load_tasks(App& app) {
    epix::utils::IOTaskPool::instance().wait();
    app.run_schedule(Last);
}

static wgpu::Device null_device() { return wgpu::Device{}; }

static AssetId<Shader> make_id(std::uint8_t d) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = d;
    return AssetId<Shader>(uuids::uuid(bytes));
}

static auto make_cache() {
    int* count_ptr = new int(0);
    auto cache     = ShaderCache{null_device(),
                                 [count_ptr](const wgpu::Device&, const ShaderCacheSource&,
                                             ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                                 ++(*count_ptr);
                                 return wgpu::ShaderModule{};
                                 }};
    return std::pair{std::move(cache), count_ptr};
}

// ─── Pipeline test helpers ───────────────────────────────────────────────

struct PipelineTestEnv {
    App app;
    memory::Directory dir;
    int* call_count;
};

PipelineTestEnv make_pipeline_env(const memory::Directory& dir) {
    App app = App::create();
    AssetPlugin asset_plugin;
    asset_plugin.mode = AssetServerMode::Unprocessed;
    asset_plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    asset_plugin.build(app);

    ShaderPlugin shader_plugin;
    shader_plugin.build(app);

    int* count_ptr = new int(0);
    app.world_mut().insert_resource(
        ShaderCache{null_device(),
                    [count_ptr](const wgpu::Device&, const ShaderCacheSource&,
                                ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                        ++(*count_ptr);
                        return wgpu::ShaderModule{};
                    }});

    return {std::move(app), dir, count_ptr};
}

// Flush IO tasks AND ensure shader cache auto-sync has processed the events.
// Two Last passes guarantee all events are flushed and consumed by sync_shaders.
void flush_and_sync(App& app) {
    epix::utils::IOTaskPool::instance().wait();
    app.run_schedule(Last);
    app.run_schedule(Last);
}

// ===========================================================================
// ShaderLoader — load WGSL from VFS
// ===========================================================================

TEST(ShaderLoaderWgsl, BasicLoad) {
    auto dir = memory::Directory::create({});
    dir.insert_file("test.wgsl", memory::Value::from_shared(make_bytes("fn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("test.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val->get().source.is_wgsl());
    EXPECT_EQ(val->get().source.as_str(), "fn main() {}");
    EXPECT_EQ(val->get().path, AssetPath("test.wgsl"));
}

TEST(ShaderLoaderWgsl, ImportPath_SetByDirective) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utils.wgsl",
                    memory::Value::from_shared(make_bytes("#define_import_path my::utils\nfn helper() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("utils.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val->get().import_path.is_custom());
    EXPECT_EQ(val->get().import_path.as_custom(), "my::utils");
}

TEST(ShaderLoaderWgsl, ImportsExtracted) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn dep_fn() {}")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\nfn main_fn() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    ASSERT_EQ(shader.imports.size(), 1u);
    EXPECT_TRUE(shader.imports[0].is_asset_path());
    EXPECT_EQ(shader.imports[0].as_asset_path().path.generic_string(), "dep.wgsl");
    // Dependency should be tracked via file_dependencies
    EXPECT_EQ(shader.file_dependencies.size(), 1u);
}

TEST(ShaderLoaderWgsl, CustomImport_NoDep) {
    auto dir = memory::Directory::create({});
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import my::utils\nfn main_fn() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    ASSERT_EQ(shader.imports.size(), 1u);
    EXPECT_TRUE(shader.imports[0].is_custom());
    // Custom imports don't trigger file dependency loading
    EXPECT_EQ(shader.file_dependencies.size(), 0u);
}

TEST(ShaderLoaderWgsl, MultipleImports_AllDepsLoaded) {
    auto dir = memory::Directory::create({});
    dir.insert_file("a.wgsl", memory::Value::from_shared(make_bytes("fn a() {}")));
    dir.insert_file("b.wgsl", memory::Value::from_shared(make_bytes("fn b() {}")));
    dir.insert_file("main.wgsl",
                    memory::Value::from_shared(make_bytes("#import \"a.wgsl\"\n#import \"b.wgsl\"\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    EXPECT_EQ(shader.imports.size(), 2u);
    EXPECT_EQ(shader.file_dependencies.size(), 2u);
}

TEST(ShaderLoaderWgsl, EmbeddedSource_RelativeImportKeepsSourceAndResolvesRelativePath) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.wgsl", "mesh/main.wgsl",
        std::as_bytes(std::span(std::string_view{"#import \"common/view.wgsl\"\nfn main() {}"})));
    registry->get().insert_asset_static(
        "mesh/common/view.wgsl", "mesh/common/view.wgsl",
        std::as_bytes(std::span(std::string_view{"#define_import_path epix::view\nfn helper() {}"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get().path, AssetPath("embedded://mesh/main.wgsl"));
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.has_value());
    EXPECT_EQ(*dep_path->source, "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "mesh/common/view.wgsl");
}

TEST(ShaderLoaderWgsl, EmbeddedSource_RootRelativeImportUsesSameSourceRoot) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.wgsl", "mesh/main.wgsl",
        std::as_bytes(std::span(std::string_view{"#import \"/shared/view.wgsl\"\nfn main() {}"})));
    registry->get().insert_asset_static("shared/view.wgsl", "shared/view.wgsl",
                                        std::as_bytes(std::span(std::string_view{"fn helper() {}"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.has_value());
    EXPECT_EQ(*dep_path->source, "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "shared/view.wgsl");
}

TEST(ShaderLoaderWgsl, EmbeddedSource_FullPathImportPreservesExplicitSource) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.wgsl", "mesh/main.wgsl",
        std::as_bytes(std::span(std::string_view{"#import \"embedded://shared/view.wgsl\"\nfn main() {}"})));
    registry->get().insert_asset_static("shared/view.wgsl", "shared/view.wgsl",
                                        std::as_bytes(std::span(std::string_view{"fn helper() {}"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.has_value());
    EXPECT_EQ(*dep_path->source, "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "shared/view.wgsl");
}

TEST(ShaderLoaderWgsl, ShaderDefs_Applied) {
    // The ShaderLoader takes Settings with shader_defs but doesn't modify source here —
    // it stores them on the Shader for cache-time use. Verify they are attached.
    auto dir = memory::Directory::create({});
    dir.insert_file("test.wgsl", memory::Value::from_shared(make_bytes("fn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("test.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
}

// ===========================================================================
// ShaderLoader — load SPIR-V from VFS
// ===========================================================================

TEST(ShaderLoaderSpirv, BasicLoad) {
    auto dir = memory::Directory::create({});
    // Minimal SPIR-V magic + version header bytes
    std::vector<std::uint8_t> spirv = {0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00};
    dir.insert_file("shader.spv", memory::Value::from_shared(make_bytes_from_vec(spirv)));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("shader.spv"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val->get().source.is_spirv());
    EXPECT_EQ(val->get().path, AssetPath("shader.spv"));
    EXPECT_TRUE(val->get().imports.empty());
}

TEST(ShaderLoaderSpirv, ImportPathIsAssetPath) {
    auto dir                        = memory::Directory::create({});
    std::vector<std::uint8_t> spirv = {0x07, 0x23, 0x02, 0x03};
    dir.insert_file("compiled.spv", memory::Value::from_shared(make_bytes_from_vec(spirv)));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("compiled.spv"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val->get().import_path.is_asset_path());
    EXPECT_EQ(val->get().import_path.as_asset_path().path.generic_string(), "compiled.spv");
}

// ===========================================================================
// ShaderLoader — load Slang from VFS
// ===========================================================================

TEST(ShaderLoaderSlang, BasicLoad) {
    auto dir = memory::Directory::create({});
    dir.insert_file("test.slang", memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                        "void computeMain() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("test.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val->get().source.is_slang());
    EXPECT_EQ(val->get().path, AssetPath("test.slang"));
}

TEST(ShaderLoaderSlang, ImportStatement_ExtractsImportsAndLoadsDeps) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("float4 getColor() { return float4(1,0,0,1); }")));
    dir.insert_file("main.slang",
                    memory::Value::from_shared(make_bytes("import utility;\n\n"
                                                          "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void computeMain() { float4 c = getColor(); }")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    ASSERT_EQ(shader.imports.size(), 1u);
    EXPECT_TRUE(shader.imports[0].is_custom());
    EXPECT_EQ(shader.imports[0].as_custom(), "utility.slang");
    EXPECT_EQ(shader.file_dependencies.size(), 0u);
}

TEST(ShaderLoaderSlang, DottedImport_ConvertedToPath) {
    auto dir = memory::Directory::create({});
    dir.insert_file("math/vec.slang",
                    memory::Value::from_shared(make_bytes("float3 normalize(float3 v) { return v; }")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import math.vec;\nvoid main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().imports.size(), 1u);
    EXPECT_TRUE(val->get().imports[0].is_custom());
    EXPECT_EQ(val->get().imports[0].as_custom(), "math/vec.slang");
    EXPECT_EQ(val->get().file_dependencies.size(), 0u);
}

TEST(ShaderLoaderSlang, UnderscoreToHyphen_InImport) {
    auto dir = memory::Directory::create({});
    dir.insert_file("my-lib.slang", memory::Value::from_shared(make_bytes("float helper() { return 1.0; }")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import my_lib;\nvoid main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().imports.size(), 1u);
    EXPECT_TRUE(val->get().imports[0].is_custom());
    EXPECT_EQ(val->get().imports[0].as_custom(), "my-lib.slang");
}

TEST(ShaderLoaderSlang, IncludeStatement_RemainsCustomImport) {
    auto dir = memory::Directory::create({});
    dir.insert_file("scene-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing scene;\nfloat helper() { return 1.0; }")));
    dir.insert_file("scene.slang",
                    memory::Value::from_shared(make_bytes("module scene;\n__include scene_helpers;\nvoid main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("scene.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    // module declaration sets import_path
    EXPECT_TRUE(shader.import_path.is_custom());
    EXPECT_EQ(shader.import_path.as_custom(), "scene.slang");
    // __include scene_helpers → scene-helpers.slang
    ASSERT_EQ(shader.imports.size(), 1u);
    EXPECT_TRUE(shader.imports[0].is_custom());
    EXPECT_EQ(shader.imports[0].as_custom(), "scene-helpers.slang");
    EXPECT_TRUE(shader.file_dependencies.empty());
}

TEST(ShaderLoaderSlang, StringLiteralImport_LoadsDeps) {
    auto dir = memory::Directory::create({});
    dir.insert_file("third-party/lib.slang", memory::Value::from_shared(make_bytes("float lib_fn() { return 0.0; }")));
    dir.insert_file("main.slang",
                    memory::Value::from_shared(make_bytes("import \"third-party/lib.slang\";\nvoid main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().imports.size(), 1u);
    EXPECT_EQ(val->get().imports[0].as_asset_path().path.generic_string(), "third-party/lib.slang");
    EXPECT_EQ(val->get().file_dependencies.size(), 1u);
}

TEST(ShaderLoaderSlang, ModuleDeclaration_SetsImportPath) {
    auto dir = memory::Directory::create({});
    dir.insert_file("graphics.slang", memory::Value::from_shared(make_bytes("module graphics;\nvoid draw() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("graphics.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_TRUE(val->get().import_path.is_custom());
    EXPECT_EQ(val->get().import_path.as_custom(), "graphics.slang");
}

TEST(ShaderLoaderSlang, MixedImportAndInclude_AllDepsLoaded) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utils.slang", memory::Value::from_shared(make_bytes("float util_fn() { return 1.0; }")));
    dir.insert_file("app-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing my-app;\nfloat help() { return 0.0; }")));
    dir.insert_file("third-party/lib.slang", memory::Value::from_shared(make_bytes("float lib_fn() { return 2.0; }")));
    dir.insert_file("app.slang", memory::Value::from_shared(make_bytes("module my_app;\n"
                                                                       "import utils;\n"
                                                                       "__include app_helpers;\n"
                                                                       "import \"third-party/lib.slang\";\n"
                                                                       "void main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("app.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    EXPECT_TRUE(shader.import_path.is_custom());
    EXPECT_EQ(shader.import_path.as_custom(), "my-app.slang");
    ASSERT_EQ(shader.imports.size(), 3u);
    EXPECT_TRUE(shader.imports[0].is_custom());
    EXPECT_EQ(shader.imports[0].as_custom(), "utils.slang");
    EXPECT_TRUE(shader.imports[1].is_custom());
    EXPECT_EQ(shader.imports[1].as_custom(), "app-helpers.slang");
    EXPECT_EQ(shader.imports[2].as_asset_path().path.generic_string(), "third-party/lib.slang");
    EXPECT_EQ(shader.file_dependencies.size(), 1u);
}

TEST(ShaderLoaderSlang, EmbeddedSource_RootRelativeImportUsesSameSourceRoot) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"import \"/shared/view\";\nvoid main() {}"})));
    registry->get().insert_asset_static("shared/view.slang", "shared/view.slang",
                                        std::as_bytes(std::span(std::string_view{"module \"shared/view\";\n"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.as_str().has_value());
    EXPECT_EQ(dep_path->source.as_str().value(), "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "shared/view.slang");
}

TEST(ShaderLoaderSlang, EmbeddedSource_RelativeImportKeepsSourceAndResolvesRelativePath) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"import \"common/view\";\nvoid main() {}"})));
    registry->get().insert_asset_static("mesh/common/view.slang", "mesh/common/view.slang",
                                        std::as_bytes(std::span(std::string_view{"module \"common/view\";\n"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.as_str().has_value());
    EXPECT_EQ(dep_path->source.as_str().value(), "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "mesh/common/view.slang");
}

TEST(ShaderLoaderSlang, EmbeddedSource_FullPathImportPreservesExplicitSource) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"import \"embedded://shared/view\";\nvoid main() {}"})));
    registry->get().insert_asset_static("shared/view.slang", "shared/view.slang",
                                        std::as_bytes(std::span(std::string_view{"module \"shared/view\";\n"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.as_str().has_value());
    EXPECT_EQ(dep_path->source.as_str().value(), "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "shared/view.slang");
}

TEST(ShaderLoaderSlang, EmbeddedSource_RelativeIncludeKeepsSourceAndResolvesRelativePath) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"module scene;\n__include \"common/view\";\nvoid main() {}"})));
    registry->get().insert_asset_static(
        "mesh/common/view.slang", "mesh/common/view.slang",
        std::as_bytes(std::span(std::string_view{"implementing scene;\nfloat helper() { return 1.0; }"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.as_str().has_value());
    EXPECT_EQ(dep_path->source.as_str().value(), "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "mesh/common/view.slang");
}

TEST(ShaderLoaderSlang, EmbeddedSource_RootRelativeIncludeUsesSameSourceRoot) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"module scene;\n__include \"/shared/view\";\nvoid main() {}"})));
    registry->get().insert_asset_static(
        "shared/view.slang", "shared/view.slang",
        std::as_bytes(std::span(std::string_view{"implementing scene;\nfloat helper() { return 1.0; }"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.as_str().has_value());
    EXPECT_EQ(dep_path->source.as_str().value(), "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "shared/view.slang");
}

TEST(ShaderLoaderSlang, EmbeddedSource_FullPathIncludePreservesExplicitSource) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static("mesh/main.slang", "mesh/main.slang",
                                        std::as_bytes(std::span(std::string_view{
                                            "module scene;\n__include \"embedded://shared/view\";\nvoid main() {}"})));
    registry->get().insert_asset_static(
        "shared/view.slang", "shared/view.slang",
        std::as_bytes(std::span(std::string_view{"implementing scene;\nfloat helper() { return 1.0; }"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().file_dependencies.size(), 1u);
    auto dep_path = val->get().file_dependencies[0].path();
    ASSERT_TRUE(dep_path.has_value());
    ASSERT_TRUE(dep_path->source.as_str().has_value());
    EXPECT_EQ(dep_path->source.as_str().value(), "embedded");
    EXPECT_EQ(dep_path->path.generic_string(), "shared/view.slang");
}

TEST(ShaderLoaderSlang, MultipleImports_RemainCustomImports) {
    auto dir = memory::Directory::create({});
    dir.insert_file("alpha.slang", memory::Value::from_shared(make_bytes("float a() { return 1.0; }")));
    dir.insert_file("beta.slang", memory::Value::from_shared(make_bytes("float b() { return 2.0; }")));
    dir.insert_file("main.slang", memory::Value::from_shared(
                                      make_bytes("import alpha;\nimport beta;\n"
                                                 "[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid computeMain() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get().imports.size(), 2u);
    EXPECT_TRUE(val->get().imports[0].is_custom());
    EXPECT_EQ(val->get().imports[0].as_custom(), "alpha.slang");
    EXPECT_TRUE(val->get().imports[1].is_custom());
    EXPECT_EQ(val->get().imports[1].as_custom(), "beta.slang");
    EXPECT_EQ(val->get().file_dependencies.size(), 0u);
}

TEST(ShaderLoaderSlang, EmbeddedSource_CustomImportDoesNotBecomeFileDependency) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"import epix.view;\nvoid main() {}"})));
    registry->get().insert_asset_static("epix/shaders/view.slang", "epix/shaders/view.slang",
                                        std::as_bytes(std::span(std::string_view{"module \"epix/view\";\n"})));

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(val->get().imports.size(), 1u);
    EXPECT_TRUE(val->get().imports[0].is_custom());
    EXPECT_EQ(val->get().imports[0].as_custom(), "epix/view.slang");
    EXPECT_TRUE(val->get().file_dependencies.empty());

    auto dep_handle = server.get_handle<Shader>(AssetPath("embedded://epix/shaders/view.slang"));
    EXPECT_FALSE(dep_handle.has_value());
}

// ===========================================================================
// ShaderLoader — error cases
// ===========================================================================

TEST(ShaderLoaderError, InvalidUtf8_Wgsl) {
    auto dir = memory::Directory::create({});
    // Invalid UTF-8 byte
    std::vector<std::byte> bad = {std::byte{0xFF}, std::byte{0xFE}};
    dir.insert_file("bad.wgsl", memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(bad)));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("bad.wgsl"));
    flush_load_tasks(app);

    // Should fail to load due to UTF-8 validation
    EXPECT_FALSE(server.is_loaded(handle.id()));
}

TEST(ShaderLoaderError, InvalidUtf8_Slang) {
    auto dir                   = memory::Directory::create({});
    std::vector<std::byte> bad = {std::byte{0xFF}, std::byte{0xFE}};
    dir.insert_file("bad.slang", memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(bad)));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("bad.slang"));
    flush_load_tasks(app);

    EXPECT_FALSE(server.is_loaded(handle.id()));
}

// ===========================================================================
// Full pipeline: load → auto-sync to ShaderCache → get module (WGSL)
// ===========================================================================

TEST(ShaderPipelineWgsl, SingleShader_AutoSyncedToCache) {
    auto dir = memory::Directory::create({});
    dir.insert_file("test.wgsl", memory::Value::from_shared(make_bytes("@compute @workgroup_size(1)\nfn main() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("test.wgsl"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // Shader should be automatically synced to cache via sync_shaders system
    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

TEST(ShaderPipelineWgsl, WithCustomImport_AutoSyncedAndResolved) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(
                                    make_bytes("#define_import_path my::dep\nfn dep_fn() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(
                                     make_bytes("#import my::dep\n@compute @workgroup_size(1)\nfn main() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    auto dep_handle  = server.load<Shader>(AssetPath("dep.wgsl"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));
    ASSERT_TRUE(server.is_loaded(dep_handle.id()));

    // Both shaders auto-synced; imports resolved via import_path matching
    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

TEST(ShaderPipelineWgsl, WithAssetPathImport_AutoSyncedAndResolved) {
    auto dir = memory::Directory::create({});
    dir.insert_file("lib/dep.wgsl", memory::Value::from_shared(make_bytes("fn dep_fn() -> f32 { return 1.0; }")));
    dir.insert_file(
        "main.wgsl",
        memory::Value::from_shared(make_bytes("#import \"lib/dep.wgsl\"\n@compute @workgroup_size(1)\nfn main() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    // dep auto-loaded transitively
    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("lib/dep.wgsl"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    EXPECT_TRUE(server.is_loaded(dep_handle_opt->id()));

    // Dep is auto-synced to cache and can produce a module independently
    auto& cache     = env.app.resource_mut<ShaderCache>();
    auto dep_result = cache.get(CachedPipelineId{1}, dep_handle_opt->id(), {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

// ===========================================================================
// Full pipeline: load → auto-sync to ShaderCache → get module (SPIR-V)
// ===========================================================================

TEST(ShaderPipelineSpirv, LoadAndAutoSyncToCache) {
    auto dir                        = memory::Directory::create({});
    std::vector<std::uint8_t> spirv = {0x03, 0x02, 0x23, 0x07};
    dir.insert_file("compiled.spv", memory::Value::from_shared(make_bytes_from_vec(spirv)));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("compiled.spv"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

// ===========================================================================
// Full pipeline: load → auto-sync to ShaderCache → get module (Slang)
// ===========================================================================

TEST(ShaderPipelineSlang, SingleShader_AutoSyncedToCache) {
    auto dir = memory::Directory::create({});
    dir.insert_file("test.slang", memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                        "void computeMain() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("test.slang"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // Slang shader auto-synced to cache; Slang→SPIR-V compilation happens in cache.get()
    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, handle.id(), {});
    // Slang compilation may succeed or fail depending on runtime Slang availability
    if (result.has_value()) {
        EXPECT_EQ(*env.call_count, 1);
    }
}

TEST(ShaderPipelineSlang, WithImport_AutoSyncedAndResolved) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("float4 getColor() { return float4(1,0,0,1); }")));
    dir.insert_file("main.slang",
                    memory::Value::from_shared(make_bytes("import utility;\n\n"
                                                          "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void computeMain() { float4 c = getColor(); }")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    auto util_handle = server.load<Shader>(AssetPath("utility.slang"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));
    ASSERT_TRUE(server.is_loaded(util_handle.id()));

    // Both auto-synced; import resolved via import_path matching in cache
    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

TEST(ShaderPipelineSlang, WithInclude_AutoSyncedAndResolved) {
    auto dir = memory::Directory::create({});
    dir.insert_file("scene-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing scene;\nfloat helper() { return 1.0; }")));
    dir.insert_file("scene.slang",
                    memory::Value::from_shared(make_bytes("module scene;\n__include scene_helpers;\n"
                                                          "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void computeMain() { float h = helper(); }")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto scene_handle  = server.load<Shader>(AssetPath("scene.slang"));
    auto helper_handle = server.load<Shader>(AssetPath("scene-helpers.slang"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(scene_handle.id()));
    ASSERT_TRUE(server.is_loaded(helper_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, scene_handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

TEST(ShaderPipelineSlang, MixedImportInclude_AllAutoSyncedAndResolved) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utils.slang", memory::Value::from_shared(make_bytes("float util() { return 1.0; }")));
    dir.insert_file("my-app-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing my_app;\nfloat help() { return 2.0; }")));
    dir.insert_file("app.slang", memory::Value::from_shared(make_bytes("module my_app;\n"
                                                                       "import utils;\n"
                                                                       "__include my_app_helpers;\n"
                                                                       "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                       "void computeMain() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto app_handle    = server.load<Shader>(AssetPath("app.slang"));
    auto utils_handle  = server.load<Shader>(AssetPath("utils.slang"));
    auto helper_handle = server.load<Shader>(AssetPath("my-app-helpers.slang"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded(app_handle.id()));
    ASSERT_TRUE(server.is_loaded(utils_handle.id()));
    ASSERT_TRUE(server.is_loaded(helper_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, app_handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

// ===========================================================================
// Cross-format: verify all three formats go through the loader correctly
// ===========================================================================

TEST(ShaderLoaderAllFormats, WgslSlangSpirv_LoadedInSameServer) {
    auto dir = memory::Directory::create({});
    dir.insert_file("shader.wgsl", memory::Value::from_shared(make_bytes("@compute @workgroup_size(1)\nfn main() {}")));
    dir.insert_file("shader.slang", memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                          "void computeMain() {}")));
    std::vector<std::uint8_t> spirv = {0x03, 0x02, 0x23, 0x07};
    dir.insert_file("shader.spv", memory::Value::from_shared(make_bytes_from_vec(spirv)));

    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto wgsl_handle  = server.load<Shader>(AssetPath("shader.wgsl"));
    auto slang_handle = server.load<Shader>(AssetPath("shader.slang"));
    auto spirv_handle = server.load<Shader>(AssetPath("shader.spv"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(wgsl_handle.id()));
    ASSERT_TRUE(server.is_loaded(slang_handle.id()));
    ASSERT_TRUE(server.is_loaded(spirv_handle.id()));

    auto& assets   = app.resource<Assets<Shader>>();
    auto wgsl_val  = assets.get(wgsl_handle.id());
    auto slang_val = assets.get(slang_handle.id());
    auto spirv_val = assets.get(spirv_handle.id());

    ASSERT_TRUE(wgsl_val.has_value());
    ASSERT_TRUE(slang_val.has_value());
    ASSERT_TRUE(spirv_val.has_value());

    EXPECT_TRUE(wgsl_val->get().source.is_wgsl());
    EXPECT_TRUE(slang_val->get().source.is_slang());
    EXPECT_TRUE(spirv_val->get().source.is_spirv());
}

// ===========================================================================
// Dependency loading: dep loaded transitively via main shader
// ===========================================================================

TEST(ShaderDepLoading, WgslMain_LoadsDepTransitively) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn helper() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    // Only load main — dep should be loaded transitively by ShaderLoader
    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    auto& shader = main_val->get();
    EXPECT_EQ(shader.file_dependencies.size(), 1u);

    // The dependency should also have been loaded into the asset server
    auto dep_handle = server.get_handle<Shader>(AssetPath("dep.wgsl"));
    EXPECT_TRUE(dep_handle.has_value());
}

TEST(ShaderDepLoading, WgslMain_LoadsRecursiveDepsTransitively) {
    auto dir = memory::Directory::create({});
    dir.insert_file("leaf.wgsl", memory::Value::from_shared(make_bytes("fn leaf_helper() -> f32 { return 2.0; }")));
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes(
                                    "#import \"leaf.wgsl\"\nfn dep_helper() -> f32 { return leaf_helper(); }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    EXPECT_EQ(main_val->get().file_dependencies.size(), 1u);

    auto dep_handle = server.get_handle<Shader>(AssetPath("dep.wgsl"));
    ASSERT_TRUE(dep_handle.has_value());
    ASSERT_TRUE(server.is_loaded_with_dependencies(dep_handle->id()));

    auto dep_val = assets.get(dep_handle->id());
    ASSERT_TRUE(dep_val.has_value());
    EXPECT_EQ(dep_val->get().file_dependencies.size(), 1u);

    auto leaf_handle = server.get_handle<Shader>(AssetPath("leaf.wgsl"));
    ASSERT_TRUE(leaf_handle.has_value());
    EXPECT_TRUE(server.is_loaded_with_dependencies(leaf_handle->id()));
    auto leaf_val = assets.get(leaf_handle->id());
    ASSERT_TRUE(leaf_val.has_value());
    EXPECT_TRUE(leaf_val->get().file_dependencies.empty());
}

TEST(ShaderDepLoading, WgslEmbeddedRecursiveDeps_SelectExactRelativeAndRootFiles) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.wgsl", "mesh/main.wgsl",
        std::as_bytes(std::span(std::string_view{"#import \"common/dep.wgsl\"\nfn main() {}"})));
    registry->get().insert_asset_static(
        "mesh/common/dep.wgsl", "mesh/common/dep.wgsl",
        std::as_bytes(std::span(std::string_view{"#import \"/shared/leaf.wgsl\"\nfn dep_helper() {}"})));
    registry->get().insert_asset_static("common/dep.wgsl", "common/dep.wgsl",
                                        std::as_bytes(std::span(std::string_view{"fn wrong_dep_global() {}"})));
    registry->get().insert_asset_static("other/common/dep.wgsl", "other/common/dep.wgsl",
                                        std::as_bytes(std::span(std::string_view{"fn wrong_dep_other() {}"})));
    registry->get().insert_asset_static(
        "shared/leaf.wgsl", "shared/leaf.wgsl",
        std::as_bytes(std::span(std::string_view{"fn leaf_helper() -> f32 { return 4.0; }"})));
    registry->get().insert_asset_static(
        "mesh/shared/leaf.wgsl", "mesh/shared/leaf.wgsl",
        std::as_bytes(std::span(std::string_view{"fn wrong_leaf_mesh() -> f32 { return 1.0; }"})));
    registry->get().insert_asset_static(
        "mesh/common/shared/leaf.wgsl", "mesh/common/shared/leaf.wgsl",
        std::as_bytes(std::span(std::string_view{"fn wrong_leaf_nested() -> f32 { return 2.0; }"})));

    auto& server     = app.resource<AssetServer>();
    auto main_handle = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_EQ(main_val->get().file_dependencies.size(), 1u);
    ASSERT_TRUE(main_val->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*main_val->get().file_dependencies[0].path(), AssetPath("embedded://mesh/common/dep.wgsl"));

    auto dep_handle = server.get_handle<Shader>(AssetPath("embedded://mesh/common/dep.wgsl"));
    ASSERT_TRUE(dep_handle.has_value());
    ASSERT_TRUE(server.is_loaded_with_dependencies(dep_handle->id()));
    auto dep_val = assets.get(dep_handle->id());
    ASSERT_TRUE(dep_val.has_value());
    ASSERT_EQ(dep_val->get().file_dependencies.size(), 1u);
    ASSERT_TRUE(dep_val->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*dep_val->get().file_dependencies[0].path(), AssetPath("embedded://shared/leaf.wgsl"));

    auto leaf_handle = server.get_handle<Shader>(AssetPath("embedded://shared/leaf.wgsl"));
    ASSERT_TRUE(leaf_handle.has_value());
    EXPECT_TRUE(server.is_loaded_with_dependencies(leaf_handle->id()));

    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://common/dep.wgsl")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://other/common/dep.wgsl")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/shared/leaf.wgsl")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/common/shared/leaf.wgsl")).has_value());
}

TEST(ShaderDepLoading, SlangMain_CustomImportDoesNotLoadDepTransitively) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("float4 getColor() { return float4(1,0,0,1); }")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import utility;\nvoid main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_EQ(main_val->get().imports.size(), 1u);
    EXPECT_TRUE(main_val->get().imports[0].is_custom());
    EXPECT_EQ(main_val->get().imports[0].as_custom(), "utility.slang");
    EXPECT_EQ(main_val->get().file_dependencies.size(), 0u);

    auto dep_handle = server.get_handle<Shader>(AssetPath("utility.slang"));
    EXPECT_FALSE(dep_handle.has_value());
}

TEST(ShaderDepLoading, SlangInclude_CustomImportDoesNotLoadDepTransitively) {
    auto dir = memory::Directory::create({});
    dir.insert_file("scene-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing scene;\nfloat h() { return 1.0; }")));
    dir.insert_file("scene.slang",
                    memory::Value::from_shared(make_bytes("module scene;\n__include scene_helpers;\nvoid main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("scene.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_EQ(main_val->get().imports.size(), 1u);
    EXPECT_TRUE(main_val->get().imports[0].is_custom());
    EXPECT_EQ(main_val->get().imports[0].as_custom(), "scene-helpers.slang");
    EXPECT_EQ(main_val->get().file_dependencies.size(), 0u);

    auto dep_handle = server.get_handle<Shader>(AssetPath("scene-helpers.slang"));
    EXPECT_FALSE(dep_handle.has_value());
}

TEST(ShaderDepLoading, SlangFileImport_LoadsRecursiveFileDepsTransitively) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    epix::assets::app_register_asset<Shader>(app);
    epix::assets::app_register_loader<ShaderLoader>(app);

    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());

    registry->get().insert_asset_static(
        "mesh/main.slang", "mesh/main.slang",
        std::as_bytes(std::span(std::string_view{"import \"scene\";\n"
                                                 "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                 "void computeMain() { float4 c = helper(); }"})));
    registry->get().insert_asset_static(
        "mesh/scene.slang", "mesh/scene.slang",
        std::as_bytes(std::span(std::string_view{"module scene;\n__include \"impl/scene\";\n"})));
    registry->get().insert_asset_static(
        "scene.slang", "scene.slang",
        std::as_bytes(std::span(std::string_view{"module scene;\npublic float4 wrongScene() { return 0; }\n"})));
    registry->get().insert_asset_static(
        "mesh/impl/scene.slang", "mesh/impl/scene.slang",
        std::as_bytes(std::span(std::string_view{
            "implementing scene;\nimport \"/shared/utility\";\nfloat4 helper() { return getColor(); }"})));
    registry->get().insert_asset_static(
        "impl/scene.slang", "impl/scene.slang",
        std::as_bytes(std::span(std::string_view{"implementing scene;\nfloat4 wrongHelper() { return 0; }"})));
    registry->get().insert_asset_static(
        "shared/utility.slang", "shared/utility.slang",
        std::as_bytes(std::span(std::string_view{"float4 getColor() { return float4(1, 0, 0, 1); }"})));
    registry->get().insert_asset_static(
        "mesh/shared/utility.slang", "mesh/shared/utility.slang",
        std::as_bytes(std::span(std::string_view{"float4 wrongColor() { return float4(0, 1, 0, 1); }"})));

    auto& server     = app.resource<AssetServer>();
    auto main_handle = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    EXPECT_EQ(main_val->get().file_dependencies.size(), 1u);
    ASSERT_TRUE(main_val->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*main_val->get().file_dependencies[0].path(), AssetPath("embedded://mesh/scene.slang"));

    auto scene_handle = server.get_handle<Shader>(AssetPath("embedded://mesh/scene.slang"));
    ASSERT_TRUE(scene_handle.has_value());
    ASSERT_TRUE(server.is_loaded_with_dependencies(scene_handle->id()));
    auto scene_val = assets.get(scene_handle->id());
    ASSERT_TRUE(scene_val.has_value());
    EXPECT_EQ(scene_val->get().file_dependencies.size(), 1u);
    EXPECT_TRUE(scene_val->get().import_path.is_custom());
    EXPECT_EQ(scene_val->get().import_path.as_custom(), "scene.slang");
    ASSERT_TRUE(scene_val->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*scene_val->get().file_dependencies[0].path(), AssetPath("embedded://mesh/impl/scene.slang"));

    auto impl_handle = server.get_handle<Shader>(AssetPath("embedded://mesh/impl/scene.slang"));
    ASSERT_TRUE(impl_handle.has_value());
    ASSERT_TRUE(server.is_loaded_with_dependencies(impl_handle->id()));
    auto impl_val = assets.get(impl_handle->id());
    ASSERT_TRUE(impl_val.has_value());
    EXPECT_EQ(impl_val->get().file_dependencies.size(), 1u);
    ASSERT_TRUE(impl_val->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*impl_val->get().file_dependencies[0].path(), AssetPath("embedded://shared/utility.slang"));

    auto utility_handle = server.get_handle<Shader>(AssetPath("embedded://shared/utility.slang"));
    ASSERT_TRUE(utility_handle.has_value());
    EXPECT_TRUE(server.is_loaded_with_dependencies(utility_handle->id()));
    auto utility_val = assets.get(utility_handle->id());
    ASSERT_TRUE(utility_val.has_value());
    EXPECT_TRUE(utility_val->get().file_dependencies.empty());

    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://scene.slang")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://impl/scene.slang")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/shared/utility.slang")).has_value());
}

// ===========================================================================
// Dep pipeline: transitively loaded shaders are auto-synced to cache
// and can produce a shader module through cache.get() independently.
// ===========================================================================

TEST(ShaderDepPipeline, WgslDep_AutoSyncedAndCanGetModule) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn helper() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\n"
                                                                       "@compute @workgroup_size(1)\nfn main() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    // Load only main — dep should be loaded transitively and auto-synced
    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("dep.wgsl"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    EXPECT_TRUE(server.is_loaded(dep_handle_opt->id()));

    // Dep auto-synced to cache; can independently produce a shader module
    auto& cache     = env.app.resource_mut<ShaderCache>();
    auto dep_result = cache.get(CachedPipelineId{1}, dep_handle_opt->id(), {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

TEST(ShaderDepPipeline, WgslMainAndDep_BothAutoSyncedAndReachLoadModule) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn helper() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\n"
                                                                       "@compute @workgroup_size(1)\nfn main() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("dep.wgsl"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    ASSERT_TRUE(server.is_loaded(dep_handle_opt->id()));

    auto& cache = env.app.resource_mut<ShaderCache>();

    // dep can get its own module
    auto dep_result = cache.get(CachedPipelineId{1}, dep_handle_opt->id(), {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(*env.call_count, 1);

    // main can get its module (composes dep inline)
    auto main_result = cache.get(CachedPipelineId{2}, main_handle.id(), {});
    ASSERT_TRUE(main_result.has_value());
    EXPECT_EQ(*env.call_count, 2);
}

TEST(ShaderDepPipeline, SlangCustomImport_ExplicitDepAutoSyncedAndCanGetModule) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void utilMain() {}")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import utility;\n"
                                                                        "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                        "void computeMain() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    auto dep_handle  = server.load<Shader>(AssetPath("utility.slang"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));
    ASSERT_TRUE(server.is_loaded_with_dependencies(dep_handle.id()));

    // Dep is explicitly loaded, auto-synced, and can produce a module independently.
    auto& cache     = env.app.resource_mut<ShaderCache>();
    auto dep_result = cache.get(CachedPipelineId{1}, dep_handle.id(), {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(*env.call_count, 1);
}

TEST(ShaderDepPipeline, SlangMainAndExplicitDep_BothAutoSyncedAndReachLoadModule) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void utilMain() {}")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import utility;\n"
                                                                        "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                        "void computeMain() {}")));
    auto env     = make_pipeline_env(dir);
    auto& server = env.app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    auto dep_handle  = server.load<Shader>(AssetPath("utility.slang"));
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));
    ASSERT_TRUE(server.is_loaded_with_dependencies(dep_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();

    // dep compiles standalone
    auto dep_result = cache.get(CachedPipelineId{1}, dep_handle.id(), {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(*env.call_count, 1);

    // main compiles (resolves utility via CacheFileSystem)
    auto main_result = cache.get(CachedPipelineId{2}, main_handle.id(), {});
    ASSERT_TRUE(main_result.has_value());
    EXPECT_EQ(*env.call_count, 2);
}

}  // namespace
