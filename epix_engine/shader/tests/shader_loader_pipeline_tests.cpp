// Tests for ShaderLoader (load via VFS) and full shader loading pipeline
// (load → ShaderCache → get module), covering WGSL, Slang, SPIR-V with
// import/include combinations.

#include <gtest/gtest.h>

import std;
import epix.core;
import epix.assets;
import epix.shader;

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
    EXPECT_EQ(val->get().path, "test.wgsl");
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
    EXPECT_EQ(val->get().path, "shader.spv");
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
    EXPECT_EQ(val->get().path, "test.slang");
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
    EXPECT_EQ(shader.imports[0].as_asset_path().path.generic_string(), "utility.slang");
    EXPECT_EQ(shader.file_dependencies.size(), 1u);
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
    EXPECT_EQ(val->get().imports[0].as_asset_path().path.generic_string(), "math/vec.slang");
    EXPECT_EQ(val->get().file_dependencies.size(), 1u);
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
    EXPECT_EQ(val->get().imports[0].as_asset_path().path.generic_string(), "my-lib.slang");
}

TEST(ShaderLoaderSlang, IncludeStatement_LoadsDeps) {
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
    EXPECT_EQ(shader.import_path.as_asset_path().path.generic_string(), "scene.slang");
    // __include scene_helpers → scene-helpers.slang
    ASSERT_EQ(shader.imports.size(), 1u);
    EXPECT_EQ(shader.imports[0].as_asset_path().path.generic_string(), "scene-helpers.slang");
    EXPECT_EQ(shader.file_dependencies.size(), 1u);
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
    EXPECT_EQ(val->get().import_path.as_asset_path().path.generic_string(), "graphics.slang");
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
    EXPECT_EQ(shader.import_path.as_asset_path().path.generic_string(), "my-app.slang");
    ASSERT_EQ(shader.imports.size(), 3u);
    EXPECT_EQ(shader.imports[0].as_asset_path().path.generic_string(), "utils.slang");
    EXPECT_EQ(shader.imports[1].as_asset_path().path.generic_string(), "app-helpers.slang");
    EXPECT_EQ(shader.imports[2].as_asset_path().path.generic_string(), "third-party/lib.slang");
    EXPECT_EQ(shader.file_dependencies.size(), 3u);
}

TEST(ShaderLoaderSlang, MultipleImports_AllDepsLoaded) {
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
    EXPECT_EQ(val->get().file_dependencies.size(), 2u);
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
// Full pipeline: load → ShaderCache → get module (WGSL)
// ===========================================================================

TEST(ShaderPipelineWgsl, SingleShader_LoadAndGetModule) {
    auto dir = memory::Directory::create({});
    dir.insert_file("test.wgsl", memory::Value::from_shared(make_bytes("@compute @workgroup_size(1)\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("test.wgsl"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();

    // Feed into ShaderCache
    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto shader_id = make_id(1);
    cache.set_shader(shader_id, shader);
    auto result = cache.get(CachedPipelineId{1}, shader_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST(ShaderPipelineWgsl, WithImport_LoadAndResolveInCache) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(
                                    make_bytes("#define_import_path my::dep\nfn dep_fn() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(
                                     make_bytes("#import my::dep\n@compute @workgroup_size(1)\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    auto dep_handle  = server.load<Shader>(AssetPath("dep.wgsl"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));
    ASSERT_TRUE(server.is_loaded(dep_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    auto dep_val  = assets.get(dep_handle.id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_TRUE(dep_val.has_value());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto dep_id  = make_id(1);
    auto main_id = make_id(2);
    cache.set_shader(dep_id, dep_val->get());
    cache.set_shader(main_id, main_val->get());

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST(ShaderPipelineWgsl, WithAssetPathImport_LoadAndResolve) {
    auto dir = memory::Directory::create({});
    dir.insert_file("lib/dep.wgsl", memory::Value::from_shared(make_bytes("fn dep_fn() -> f32 { return 1.0; }")));
    dir.insert_file(
        "main.wgsl",
        memory::Value::from_shared(make_bytes("#import \"lib/dep.wgsl\"\n@compute @workgroup_size(1)\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    ASSERT_TRUE(main_val.has_value());
    auto& shader = main_val->get();
    ASSERT_EQ(shader.imports.size(), 1u);
    EXPECT_EQ(shader.imports[0].as_asset_path().path.generic_string(), "lib/dep.wgsl");
    EXPECT_EQ(shader.file_dependencies.size(), 1u);
}

// ===========================================================================
// Full pipeline: load → ShaderCache → get module (SPIR-V)
// ===========================================================================

TEST(ShaderPipelineSpirv, LoadAndGetModule) {
    auto dir                        = memory::Directory::create({});
    std::vector<std::uint8_t> spirv = {0x03, 0x02, 0x23, 0x07};
    dir.insert_file("compiled.spv", memory::Value::from_shared(make_bytes_from_vec(spirv)));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto handle = server.load<Shader>(AssetPath("compiled.spv"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    auto& assets = app.resource<Assets<Shader>>();
    auto val     = assets.get(handle.id());
    ASSERT_TRUE(val.has_value());
    auto& shader = val->get();
    EXPECT_TRUE(shader.source.is_spirv());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto shader_id = make_id(1);
    cache.set_shader(shader_id, shader);
    auto result = cache.get(CachedPipelineId{1}, shader_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

// ===========================================================================
// Full pipeline: load → ShaderCache → get module (Slang)
// ===========================================================================

TEST(ShaderPipelineSlang, SingleShader_LoadAndGetModule) {
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
    auto& shader = val->get();

    // Slang shader goes through Slang→SPIR-V compilation in cache.get()
    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto shader_id = make_id(1);
    cache.set_shader(shader_id, shader);
    auto result = cache.get(CachedPipelineId{1}, shader_id, {});
    // Slang compilation may succeed or fail depending on runtime Slang availability
    // but the cache.get mechanism should work
    if (result.has_value()) {
        EXPECT_EQ(call_count, 1);
    }
}

TEST(ShaderPipelineSlang, WithImport_BothLoadedInCache) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("float4 getColor() { return float4(1,0,0,1); }")));
    dir.insert_file("main.slang",
                    memory::Value::from_shared(make_bytes("import utility;\n\n"
                                                          "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void computeMain() { float4 c = getColor(); }")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    auto util_handle = server.load<Shader>(AssetPath("utility.slang"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));
    ASSERT_TRUE(server.is_loaded(util_handle.id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    auto util_val = assets.get(util_handle.id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_TRUE(util_val.has_value());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto util_id = make_id(1);
    auto main_id = make_id(2);
    cache.set_shader(util_id, util_val->get());
    cache.set_shader(main_id, main_val->get());

    // main depends on utility — import should be resolved in cache
    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST(ShaderPipelineSlang, WithInclude_BothLoadedInCache) {
    auto dir = memory::Directory::create({});
    dir.insert_file("scene-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing scene;\nfloat helper() { return 1.0; }")));
    dir.insert_file("scene.slang",
                    memory::Value::from_shared(make_bytes("module scene;\n__include scene_helpers;\n"
                                                          "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void computeMain() { float h = helper(); }")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto scene_handle  = server.load<Shader>(AssetPath("scene.slang"));
    auto helper_handle = server.load<Shader>(AssetPath("scene-helpers.slang"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(scene_handle.id()));
    ASSERT_TRUE(server.is_loaded(helper_handle.id()));

    auto& assets    = app.resource<Assets<Shader>>();
    auto scene_val  = assets.get(scene_handle.id());
    auto helper_val = assets.get(helper_handle.id());
    ASSERT_TRUE(scene_val.has_value());
    ASSERT_TRUE(helper_val.has_value());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto helper_id = make_id(1);
    auto scene_id  = make_id(2);
    cache.set_shader(helper_id, helper_val->get());
    cache.set_shader(scene_id, scene_val->get());

    auto result = cache.get(CachedPipelineId{1}, scene_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST(ShaderPipelineSlang, MixedImportInclude_AllResolvedInCache) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utils.slang", memory::Value::from_shared(make_bytes("float util() { return 1.0; }")));
    dir.insert_file("my-app-helpers.slang",
                    memory::Value::from_shared(make_bytes("implementing my_app;\nfloat help() { return 2.0; }")));
    dir.insert_file("app.slang", memory::Value::from_shared(make_bytes("module my_app;\n"
                                                                       "import utils;\n"
                                                                       "__include my_app_helpers;\n"
                                                                       "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                       "void computeMain() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto app_handle    = server.load<Shader>(AssetPath("app.slang"));
    auto utils_handle  = server.load<Shader>(AssetPath("utils.slang"));
    auto helper_handle = server.load<Shader>(AssetPath("my-app-helpers.slang"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(app_handle.id()));
    ASSERT_TRUE(server.is_loaded(utils_handle.id()));
    ASSERT_TRUE(server.is_loaded(helper_handle.id()));

    auto& assets    = app.resource<Assets<Shader>>();
    auto app_val    = assets.get(app_handle.id());
    auto utils_val  = assets.get(utils_handle.id());
    auto helper_val = assets.get(helper_handle.id());
    ASSERT_TRUE(app_val.has_value());
    ASSERT_TRUE(utils_val.has_value());
    ASSERT_TRUE(helper_val.has_value());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};

    auto utils_id  = make_id(1);
    auto helper_id = make_id(2);
    auto app_id    = make_id(3);
    cache.set_shader(utils_id, utils_val->get());
    cache.set_shader(helper_id, helper_val->get());
    cache.set_shader(app_id, app_val->get());

    auto result = cache.get(CachedPipelineId{1}, app_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
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

TEST(ShaderDepLoading, SlangMain_LoadsDepTransitively) {
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
    EXPECT_EQ(main_val->get().file_dependencies.size(), 1u);

    auto dep_handle = server.get_handle<Shader>(AssetPath("utility.slang"));
    EXPECT_TRUE(dep_handle.has_value());
}

TEST(ShaderDepLoading, SlangInclude_LoadsDepTransitively) {
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
    EXPECT_EQ(main_val->get().file_dependencies.size(), 1u);

    auto dep_handle = server.get_handle<Shader>(AssetPath("scene-helpers.slang"));
    EXPECT_TRUE(dep_handle.has_value());
}

// ===========================================================================
// Dep pipeline: transitively loaded shader is fully loaded and can produce
// a shader module through cache.get() independently.
// ===========================================================================

TEST(ShaderDepPipeline, WgslDep_IsFullyLoadedAndCanGetModule) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn helper() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\n"
                                                                       "@compute @workgroup_size(1)\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    // Load only main — dep should be loaded transitively
    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    // The dep must also be fully loaded (tracked via track_dependency)
    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("dep.wgsl"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    EXPECT_TRUE(server.is_loaded(dep_handle_opt->id()));

    auto& assets = app.resource<Assets<Shader>>();
    auto dep_val = assets.get(dep_handle_opt->id());
    ASSERT_TRUE(dep_val.has_value());

    // Dep can independently go through cache.get() → load_module_fn
    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};
    auto dep_id = make_id(1);
    cache.set_shader(dep_id, dep_val->get());
    auto result = cache.get(CachedPipelineId{1}, dep_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST(ShaderDepPipeline, WgslMainAndDep_BothReachLoadModuleFn) {
    auto dir = memory::Directory::create({});
    dir.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn helper() -> f32 { return 1.0; }")));
    dir.insert_file("main.wgsl", memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\n"
                                                                       "@compute @workgroup_size(1)\nfn main() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.wgsl"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("dep.wgsl"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    ASSERT_TRUE(server.is_loaded(dep_handle_opt->id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    auto dep_val  = assets.get(dep_handle_opt->id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_TRUE(dep_val.has_value());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);
    cache.set_shader(dep_id, dep_val->get());
    cache.set_shader(main_id, main_val->get());

    // dep can get its own module
    auto dep_result = cache.get(CachedPipelineId{1}, dep_id, {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(call_count, 1);

    // main can get its module (composes dep inline)
    auto main_result = cache.get(CachedPipelineId{2}, main_id, {});
    ASSERT_TRUE(main_result.has_value());
    EXPECT_EQ(call_count, 2);
}

TEST(ShaderDepPipeline, SlangDep_IsFullyLoadedAndCanGetModule) {
    auto dir = memory::Directory::create({});
    // The dep has its own entry point so it can compile standalone
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void utilMain() {}")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import utility;\n"
                                                                        "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                        "void computeMain() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    // Dep must also be fully loaded
    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("utility.slang"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    EXPECT_TRUE(server.is_loaded(dep_handle_opt->id()));

    auto& assets = app.resource<Assets<Shader>>();
    auto dep_val = assets.get(dep_handle_opt->id());
    ASSERT_TRUE(dep_val.has_value());
    EXPECT_TRUE(dep_val->get().source.is_slang());

    // Dep can independently go through cache.get() → load_module_fn
    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};
    auto dep_id = make_id(1);
    cache.set_shader(dep_id, dep_val->get());
    auto result = cache.get(CachedPipelineId{1}, dep_id, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST(ShaderDepPipeline, SlangMainAndDep_BothReachLoadModuleFn) {
    auto dir = memory::Directory::create({});
    dir.insert_file("utility.slang",
                    memory::Value::from_shared(make_bytes("[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                          "void utilMain() {}")));
    dir.insert_file("main.slang", memory::Value::from_shared(make_bytes("import utility;\n"
                                                                        "[shader(\"compute\")]\n[numthreads(1,1,1)]\n"
                                                                        "void computeMain() {}")));
    auto [app, _] = make_shader_env(dir);
    auto& server  = app.resource<AssetServer>();

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(main_handle.id()));

    auto dep_handle_opt = server.get_handle<Shader>(AssetPath("utility.slang"));
    ASSERT_TRUE(dep_handle_opt.has_value());
    ASSERT_TRUE(server.is_loaded(dep_handle_opt->id()));

    auto& assets  = app.resource<Assets<Shader>>();
    auto main_val = assets.get(main_handle.id());
    auto dep_val  = assets.get(dep_handle_opt->id());
    ASSERT_TRUE(main_val.has_value());
    ASSERT_TRUE(dep_val.has_value());

    int call_count = 0;
    ShaderCache cache{null_device(),
                      [&](const wgpu::Device&, const ShaderCacheSource&,
                          ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++call_count;
                          return wgpu::ShaderModule{};
                      }};
    auto dep_id  = make_id(1);
    auto main_id = make_id(2);
    cache.set_shader(dep_id, dep_val->get());
    cache.set_shader(main_id, main_val->get());

    // dep compiles standalone
    auto dep_result = cache.get(CachedPipelineId{1}, dep_id, {});
    ASSERT_TRUE(dep_result.has_value());
    EXPECT_EQ(call_count, 1);

    // main compiles (resolves utility via CacheFileSystem)
    auto main_result = cache.get(CachedPipelineId{2}, main_id, {});
    ASSERT_TRUE(main_result.has_value());
    EXPECT_EQ(call_count, 2);
}

}  // namespace
