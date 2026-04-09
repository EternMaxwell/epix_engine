#include <gtest/gtest.h>

import std;
import epix.assets;
import epix.core;
import epix.shader;
import webgpu;

using namespace epix::assets;
using namespace epix::core;
using namespace epix::shader;

namespace {

static auto make_bytes(std::string_view text) {
    auto bytes = std::as_bytes(std::span(text));
    return std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());
}

void insert_text_asset(EmbeddedAssetRegistry& registry, std::string_view path, std::string_view text) {
    registry.insert_asset_static(path, std::as_bytes(std::span(text)));
}

wgpu::Device null_device() { return wgpu::Device{}; }

void flush_and_sync(App& app) {
    epix::utils::IOTaskPool::instance().wait();
    app.run_schedule(Last);
    app.run_schedule(Last);
}

EmbeddedAssetRegistry& embedded_registry(App& app) {
    auto registry = app.world_mut().get_resource_mut<EmbeddedAssetRegistry>();
    if (!registry.has_value()) {
        throw std::runtime_error("EmbeddedAssetRegistry resource is missing");
    }
    return registry->get();
}

struct EmbeddedPipelineEnv {
    App app;
    std::shared_ptr<int> load_count;
    std::shared_ptr<std::string> last_wgsl_source;
};

EmbeddedPipelineEnv make_embedded_pipeline_env() {
    App app = App::create();
    AssetPlugin{}.build(app);
    ShaderPlugin{}.build(app);

    auto load_count       = std::make_shared<int>(0);
    auto last_wgsl_source = std::make_shared<std::string>();
    app.world_mut().insert_resource(ShaderCache{
        null_device(),
        [load_count, last_wgsl_source](const wgpu::Device&, const ShaderCacheSource& source,
                                       ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
            ++(*load_count);
            if (const auto* wgsl = std::get_if<ShaderCacheSource::Wgsl>(&source.data)) {
                *last_wgsl_source = wgsl->source;
            }
            return wgpu::ShaderModule{};
        }});

    return {std::move(app), std::move(load_count), std::move(last_wgsl_source)};
}

void add_wgsl_success_fixture(EmbeddedAssetRegistry& registry) {
    insert_text_asset(registry, "mesh/main.wgsl",
                      "#import ui::custom\n"
                      "#import \"embedded://explicit/shared/full.wgsl\"\n"
                      "#import \"/shared/root.wgsl\"\n"
                      "#import \"common/relative.wgsl\"\n"
                      "@compute @workgroup_size(1)\n"
                      "fn main() {\n"
                      "    let _ = custom_value() + explicit_value() + root_value() + relative_value();\n"
                      "}\n");
    insert_text_asset(registry, "providers/custom.wgsl",
                      "#define_import_path ui::custom\n"
                      "fn custom_value() -> i32 { return 1; }\n");
    insert_text_asset(registry, "explicit/shared/full.wgsl", "fn explicit_value() -> i32 { return 2; }\n");
    insert_text_asset(registry, "shared/root.wgsl", "fn root_value() -> i32 { return 3; }\n");
    insert_text_asset(registry, "mesh/common/relative.wgsl", "fn relative_value() -> i32 { return 4; }\n");

    insert_text_asset(registry, "mesh/explicit/shared/full.wgsl", "fn wrong_explicit_value() -> i32 { return 20; }\n");
    insert_text_asset(registry, "mesh/shared/root.wgsl", "fn wrong_root_value() -> i32 { return 30; }\n");
    insert_text_asset(registry, "common/relative.wgsl", "fn wrong_relative_value() -> i32 { return 40; }\n");
}

void add_slang_success_fixture(EmbeddedAssetRegistry& registry) {
    insert_text_asset(registry, "mesh/main.slang",
                      "import utility;\n"
                      "import \"embedded://explicit/shared/full\";\n"
                      "import \"/shared/root\";\n"
                      "import \"common/relative\";\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() {\n"
                      "    int value = customValue() + explicitValue() + rootValue() + relativeValue();\n"
                      "}\n");
    insert_text_asset(registry, "providers/custom.slang",
                      "module utility;\n"
                      "public int customValue() { return 1; }\n");
    insert_text_asset(registry, "explicit/shared/full.slang",
                      "module \"explicit/shared/full\";\n"
                      "public int explicitValue() { return 2; }\n");
    insert_text_asset(registry, "shared/root.slang",
                      "module \"shared/root\";\n"
                      "public int rootValue() { return 3; }\n");
    insert_text_asset(registry, "mesh/common/relative.slang",
                      "module \"common/relative\";\n"
                      "public int relativeValue() { return 4; }\n");

    insert_text_asset(registry, "mesh/explicit/shared/full.slang",
                      "module \"mesh/explicit/shared/full\";\n"
                      "public int wrongExplicitValue() { return 20; }\n");
    insert_text_asset(registry, "mesh/shared/root.slang",
                      "module \"mesh/shared/root\";\n"
                      "public int wrongRootValue() { return 30; }\n");
    insert_text_asset(registry, "common/relative.slang",
                      "module \"common/relative\";\n"
                      "public int wrongRelativeValue() { return 40; }\n");
}

}  // namespace

TEST(ShaderLoaderPipelineWgsl, ValidatesAndCompilesAllFourImportFormsWithExactFileSelection) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    add_wgsl_success_fixture(registry);

    auto& server       = env.app.resource<AssetServer>();
    auto custom_handle = server.load<Shader>(AssetPath("embedded://providers/custom.wgsl"));
    auto main_handle   = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(custom_handle.id()));
    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& assets = env.app.resource<Assets<Shader>>();
    auto shader  = assets.get(main_handle.id());
    ASSERT_TRUE(shader.has_value());
    ASSERT_EQ(shader->get().imports.size(), 4u);
    ASSERT_TRUE(shader->get().imports[0].is_custom());
    EXPECT_EQ(shader->get().imports[0].as_custom(), "ui::custom");
    ASSERT_TRUE(shader->get().imports[1].is_asset_path());
    EXPECT_EQ(shader->get().imports[1].as_asset_path(), AssetPath("embedded://explicit/shared/full.wgsl"));
    ASSERT_TRUE(shader->get().imports[2].is_asset_path());
    EXPECT_EQ(shader->get().imports[2].as_asset_path(), AssetPath("embedded://shared/root.wgsl"));
    ASSERT_TRUE(shader->get().imports[3].is_asset_path());
    EXPECT_EQ(shader->get().imports[3].as_asset_path(), AssetPath("embedded://mesh/common/relative.wgsl"));

    ASSERT_EQ(shader->get().file_dependencies.size(), 3u);
    ASSERT_TRUE(shader->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*shader->get().file_dependencies[0].path(), AssetPath("embedded://explicit/shared/full.wgsl"));
    ASSERT_TRUE(shader->get().file_dependencies[1].path().has_value());
    EXPECT_EQ(*shader->get().file_dependencies[1].path(), AssetPath("embedded://shared/root.wgsl"));
    ASSERT_TRUE(shader->get().file_dependencies[2].path().has_value());
    EXPECT_EQ(*shader->get().file_dependencies[2].path(), AssetPath("embedded://mesh/common/relative.wgsl"));

    EXPECT_TRUE(server.get_handle<Shader>(AssetPath("embedded://explicit/shared/full.wgsl")).has_value());
    EXPECT_TRUE(server.get_handle<Shader>(AssetPath("embedded://shared/root.wgsl")).has_value());
    EXPECT_TRUE(server.get_handle<Shader>(AssetPath("embedded://mesh/common/relative.wgsl")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/explicit/shared/full.wgsl")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/shared/root.wgsl")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://common/relative.wgsl")).has_value());

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.load_count, 1);

    EXPECT_NE(env.last_wgsl_source->find("fn custom_value()"), std::string::npos);
    EXPECT_NE(env.last_wgsl_source->find("fn explicit_value()"), std::string::npos);
    EXPECT_NE(env.last_wgsl_source->find("fn root_value()"), std::string::npos);
    EXPECT_NE(env.last_wgsl_source->find("fn relative_value()"), std::string::npos);
    EXPECT_EQ(env.last_wgsl_source->find("wrong_explicit_value"), std::string::npos);
    EXPECT_EQ(env.last_wgsl_source->find("wrong_root_value"), std::string::npos);
    EXPECT_EQ(env.last_wgsl_source->find("wrong_relative_value"), std::string::npos);
}

TEST(ShaderLoaderPipelineWgsl, MissingCustomImportReturnsRecoverableCacheError) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/main.wgsl",
                      "#import ui::missing\n"
                      "@compute @workgroup_size(1)\n"
                      "fn main() {}\n");

    auto& server     = env.app.resource<AssetServer>();
    auto main_handle = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().is_recoverable());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderImportNotYetAvailable>(result.error().data));
    EXPECT_EQ(*env.load_count, 0);
}

TEST(ShaderLoaderPipelineWgsl, MissingFileImportFormsStayOutOfCache) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/missing-explicit.wgsl",
                      "#import \"embedded://explicit/shared/missing.wgsl\"\n"
                      "@compute @workgroup_size(1)\n"
                      "fn main() {}\n");
    insert_text_asset(registry, "mesh/missing-root.wgsl",
                      "#import \"/shared/missing.wgsl\"\n"
                      "@compute @workgroup_size(1)\n"
                      "fn main() {}\n");
    insert_text_asset(registry, "mesh/missing-relative.wgsl",
                      "#import \"common/missing.wgsl\"\n"
                      "@compute @workgroup_size(1)\n"
                      "fn main() {}\n");

    auto& server         = env.app.resource<AssetServer>();
    auto explicit_handle = server.load<Shader>(AssetPath("embedded://mesh/missing-explicit.wgsl"));
    auto root_handle     = server.load<Shader>(AssetPath("embedded://mesh/missing-root.wgsl"));
    auto relative_handle = server.load<Shader>(AssetPath("embedded://mesh/missing-relative.wgsl"));
    flush_and_sync(env.app);

    EXPECT_FALSE(server.is_loaded_with_dependencies(explicit_handle.id()));
    EXPECT_FALSE(server.is_loaded_with_dependencies(root_handle.id()));
    EXPECT_FALSE(server.is_loaded_with_dependencies(relative_handle.id()));

    auto& cache          = env.app.resource_mut<ShaderCache>();
    auto explicit_result = cache.get(CachedPipelineId{1}, explicit_handle.id(), {});
    auto root_result     = cache.get(CachedPipelineId{2}, root_handle.id(), {});
    auto relative_result = cache.get(CachedPipelineId{3}, relative_handle.id(), {});
    ASSERT_FALSE(explicit_result.has_value());
    ASSERT_FALSE(root_result.has_value());
    ASSERT_FALSE(relative_result.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(explicit_result.error().data));
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(root_result.error().data));
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(relative_result.error().data));
    EXPECT_EQ(*env.load_count, 0);
}

TEST(ShaderLoaderPipelineSlang, ValidatesAndCompilesAllFourImportFormsWithExactFileSelection) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    add_slang_success_fixture(registry);

    auto& server       = env.app.resource<AssetServer>();
    auto custom_handle = server.load<Shader>(AssetPath("embedded://providers/custom.slang"));
    auto main_handle   = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(custom_handle.id()));
    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& assets = env.app.resource<Assets<Shader>>();
    auto shader  = assets.get(main_handle.id());
    ASSERT_TRUE(shader.has_value());
    ASSERT_EQ(shader->get().imports.size(), 4u);
    ASSERT_TRUE(shader->get().imports[0].is_custom());
    EXPECT_EQ(shader->get().imports[0].as_custom(), "utility.slang");
    ASSERT_TRUE(shader->get().imports[1].is_asset_path());
    EXPECT_EQ(shader->get().imports[1].as_asset_path(), AssetPath("embedded://explicit/shared/full.slang"));
    ASSERT_TRUE(shader->get().imports[2].is_asset_path());
    EXPECT_EQ(shader->get().imports[2].as_asset_path(), AssetPath("embedded://shared/root.slang"));
    ASSERT_TRUE(shader->get().imports[3].is_asset_path());
    EXPECT_EQ(shader->get().imports[3].as_asset_path(), AssetPath("embedded://mesh/common/relative.slang"));

    ASSERT_EQ(shader->get().file_dependencies.size(), 3u);
    ASSERT_TRUE(shader->get().file_dependencies[0].path().has_value());
    EXPECT_EQ(*shader->get().file_dependencies[0].path(), AssetPath("embedded://explicit/shared/full.slang"));
    ASSERT_TRUE(shader->get().file_dependencies[1].path().has_value());
    EXPECT_EQ(*shader->get().file_dependencies[1].path(), AssetPath("embedded://shared/root.slang"));
    ASSERT_TRUE(shader->get().file_dependencies[2].path().has_value());
    EXPECT_EQ(*shader->get().file_dependencies[2].path(), AssetPath("embedded://mesh/common/relative.slang"));

    EXPECT_TRUE(server.get_handle<Shader>(AssetPath("embedded://explicit/shared/full.slang")).has_value());
    EXPECT_TRUE(server.get_handle<Shader>(AssetPath("embedded://shared/root.slang")).has_value());
    EXPECT_TRUE(server.get_handle<Shader>(AssetPath("embedded://mesh/common/relative.slang")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/explicit/shared/full.slang")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://mesh/shared/root.slang")).has_value());
    EXPECT_FALSE(server.get_handle<Shader>(AssetPath("embedded://common/relative.slang")).has_value());

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*env.load_count, 1);
}

TEST(ShaderLoaderPipelineSlang, MissingCustomImportReturnsRecoverableCacheError) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/main.slang",
                      "import utility;\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() {}\n");

    auto& server     = env.app.resource<AssetServer>();
    auto main_handle = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().is_recoverable());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderImportNotYetAvailable>(result.error().data));
    EXPECT_EQ(*env.load_count, 0);
}

TEST(ShaderLoaderPipelineSlang, MissingFileImportFormsStayOutOfCache) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/missing-explicit.slang",
                      "import \"embedded://explicit/shared/missing\";\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() {}\n");
    insert_text_asset(registry, "mesh/missing-root.slang",
                      "import \"/shared/missing\";\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() {}\n");
    insert_text_asset(registry, "mesh/missing-relative.slang",
                      "import \"common/missing\";\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() {}\n");

    auto& server         = env.app.resource<AssetServer>();
    auto explicit_handle = server.load<Shader>(AssetPath("embedded://mesh/missing-explicit.slang"));
    auto root_handle     = server.load<Shader>(AssetPath("embedded://mesh/missing-root.slang"));
    auto relative_handle = server.load<Shader>(AssetPath("embedded://mesh/missing-relative.slang"));
    flush_and_sync(env.app);

    EXPECT_FALSE(server.is_loaded_with_dependencies(explicit_handle.id()));
    EXPECT_FALSE(server.is_loaded_with_dependencies(root_handle.id()));
    EXPECT_FALSE(server.is_loaded_with_dependencies(relative_handle.id()));

    auto& cache          = env.app.resource_mut<ShaderCache>();
    auto explicit_result = cache.get(CachedPipelineId{1}, explicit_handle.id(), {});
    auto root_result     = cache.get(CachedPipelineId{2}, root_handle.id(), {});
    auto relative_result = cache.get(CachedPipelineId{3}, relative_handle.id(), {});
    ASSERT_FALSE(explicit_result.has_value());
    ASSERT_FALSE(root_result.has_value());
    ASSERT_FALSE(relative_result.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(explicit_result.error().data));
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(root_result.error().data));
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::ShaderNotLoaded>(relative_result.error().data));
    EXPECT_EQ(*env.load_count, 0);
}