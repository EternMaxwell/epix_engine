#include <gtest/gtest.h>
#include <slang-com-ptr.h>
#include <slang.h>

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

void insert_bytes_asset(EmbeddedAssetRegistry& registry, std::string_view path, std::span<const std::uint8_t> bytes) {
    registry.insert_asset_static(path, std::as_bytes(bytes));
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
                      "import utility::core;\n"
                      "import \"embedded://explicit/shared/full\";\n"
                      "import \"/shared/root\";\n"
                      "import \"common/relative\";\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() {\n"
                      "    int value = customValue() + explicitValue() + rootValue() + relativeValue();\n"
                      "}\n");
    insert_text_asset(registry, "providers/custom.slang",
                      "module utility.core;\n"
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

AssetId<Shader> make_shader_id(std::uint8_t tag) {
    auto uuid = uuids::uuid::from_string(std::format("00000000-0000-0000-0000-0000000000{:02x}", tag));
    return AssetId<Shader>(uuid.value());
}

// Compile a simple Slang module from source to a serialized IR blob.
// Returns an empty vector on any failure (e.g., toolchain unavailable).
static std::vector<std::uint8_t> compile_module_to_ir_bytes(const char* source,
                                                            const char* module_name,
                                                            const char* identity) {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) return {};

    slang::SessionDesc desc{};
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global_session->createSession(desc, session.writeRef()))) return {};

    Slang::ComPtr<slang::IBlob> diag;
    auto* mod = session->loadModuleFromSourceString(module_name, identity, source, diag.writeRef());
    if (!mod) return {};

    Slang::ComPtr<ISlangBlob> blob;
    if (SLANG_FAILED(mod->serialize(blob.writeRef())) || !blob) return {};

    const auto* ptr = static_cast<const std::uint8_t*>(blob->getBufferPointer());
    return {ptr, ptr + blob->getBufferSize()};
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
    EXPECT_EQ(shader->get().imports[0].as_custom(), "ui/custom");
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
    ASSERT_TRUE(result.has_value()) << result.error().message();
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
    auto* missing = std::get_if<ShaderCacheError::ShaderImportNotYetAvailable>(&result.error().data);
    ASSERT_NE(missing, nullptr);
    ASSERT_EQ(missing->missing_imports.size(), 1u);
    EXPECT_TRUE(missing->missing_imports[0].is_custom());
    EXPECT_EQ(missing->missing_imports[0].as_custom(), "ui/missing");
    EXPECT_EQ(*env.load_count, 0);
}

TEST(ShaderLoaderPipelineWgsl, MissingNestedCustomImportReportsLeafImport) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/main.wgsl",
                      "#import ui::parent\n"
                      "@compute @workgroup_size(1)\n"
                      "fn main() {}\n");
    insert_text_asset(registry, "providers/parent.wgsl",
                      "#define_import_path ui::parent\n"
                      "#import ui::leaf\n"
                      "fn parent_value() -> i32 { return 1; }\n");

    auto& server       = env.app.resource<AssetServer>();
    auto parent_handle = server.load<Shader>(AssetPath("embedded://providers/parent.wgsl"));
    auto main_handle   = server.load<Shader>(AssetPath("embedded://mesh/main.wgsl"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(parent_handle.id()));
    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_FALSE(result.has_value());

    auto* missing = std::get_if<ShaderCacheError::ShaderImportNotYetAvailable>(&result.error().data);
    ASSERT_NE(missing, nullptr);
    ASSERT_EQ(missing->missing_imports.size(), 1u);
    EXPECT_TRUE(missing->missing_imports[0].is_custom());
    EXPECT_EQ(missing->missing_imports[0].as_custom(), "ui/leaf");
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
    EXPECT_EQ(shader->get().imports[0].as_custom(), "utility/core");
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
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_EQ(*env.load_count, 1);
}

TEST(ShaderLoaderPipelineSlang, MissingCustomImportReturnsRecoverableCacheError) {
    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/main.slang",
                      "import utility.core;\n"
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
    auto* missing = std::get_if<ShaderCacheError::ShaderImportNotYetAvailable>(&result.error().data);
    ASSERT_NE(missing, nullptr);
    ASSERT_EQ(missing->missing_imports.size(), 1u);
    EXPECT_TRUE(missing->missing_imports[0].is_custom());
    EXPECT_EQ(missing->missing_imports[0].as_custom(), "utility/core");
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

TEST(ShaderLoaderPipelineSlang, UpdatingRootShaderInvalidatesCachedPreprocessedSource) {
    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    auto main_id = make_shader_id(0x41);
    cache.set_shader(main_id, Shader::from_slang(R"(
module main.core;
[shader("compute")]
[numthreads(1,1,1)]
void computeMain() {}
)",
                                                 "embedded://mesh/main.slang"));

    auto first = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_EQ(load_count, 1);

    cache.set_shader(main_id, Shader::from_slang(R"(
module main.core;
[shader("compute")]
[numthreads(1,1,1)]
void computeMain( {}
)",
                                                 "embedded://mesh/main.slang"));

    auto second = cache.get(CachedPipelineId{2}, main_id, {});
    ASSERT_FALSE(second.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::SlangCompileError>(second.error().data));
    EXPECT_EQ(load_count, 1);
}

TEST(ShaderLoaderPipelineSlang, UpdatingImportedShaderInvalidatesCachedPreprocessedSource) {
    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    auto lib_id  = make_shader_id(0x51);
    auto main_id = make_shader_id(0x52);

    cache.set_shader(lib_id, Shader::from_slang(R"(
module utility.core;
public int customValue() { return 1; }
)",
                                                "embedded://providers/custom.slang"));
    cache.set_shader(main_id, Shader::from_slang(R"(
import utility.core;
[shader("compute")]
[numthreads(1,1,1)]
void computeMain() {
    int value = customValue();
}
)",
                                                 "embedded://mesh/main.slang"));

    auto first = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_EQ(load_count, 1);

    cache.set_shader(lib_id, Shader::from_slang(R"(
module utility.core;
public int customValue( { return 2; }
)",
                                                "embedded://providers/custom.slang"));

    auto second = cache.get(CachedPipelineId{2}, main_id, {});
    ASSERT_FALSE(second.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::SlangCompileError>(second.error().data));
    EXPECT_EQ(load_count, 1);
}

TEST(ShaderLoaderPipelineSlang, RootCompileInputCacheIsKeyedByDefinitionSet) {
    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    auto main_id = make_shader_id(0x61);
    cache.set_shader(main_id, Shader::from_slang(R"(
#ifdef ENABLE_BAD
[shader("compute")]
[numthreads(1,1,1)]
void computeMain( {}
#else
[shader("compute")]
[numthreads(1,1,1)]
void computeMain() {}
#endif
)",
                                                 "embedded://mesh/main.slang"));

    auto first = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_EQ(load_count, 1);

    std::array<ShaderDefVal, 1> bad_defs{ShaderDefVal::from_bool("ENABLE_BAD")};
    auto second = cache.get(CachedPipelineId{2}, main_id, bad_defs);
    ASSERT_FALSE(second.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::SlangCompileError>(second.error().data));
    EXPECT_EQ(load_count, 1);
}

TEST(ShaderLoaderPipelineSlang, SerializedLibraryCacheIsKeyedByDefinitionSet) {
    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    auto lib_id  = make_shader_id(0x71);
    auto main_id = make_shader_id(0x72);

    cache.set_shader(lib_id, Shader::from_slang(R"(
module utility.core;
#ifdef ENABLE_BAD
public int customValue( { return 1; }
#else
public int customValue() { return 1; }
#endif
)",
                                                "embedded://providers/custom.slang"));
    cache.set_shader(main_id, Shader::from_slang(R"(
import utility.core;
[shader("compute")]
[numthreads(1,1,1)]
void computeMain() {
    int value = customValue();
}
)",
                                                 "embedded://mesh/main.slang"));

    auto first = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_EQ(load_count, 1);

    std::array<ShaderDefVal, 1> bad_defs{ShaderDefVal::from_bool("ENABLE_BAD")};
    auto second = cache.get(CachedPipelineId{2}, main_id, bad_defs);
    ASSERT_FALSE(second.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::SlangCompileError>(second.error().data));
    EXPECT_EQ(load_count, 1);
}

// ── ShaderLoaderPipelineSlangIr ─────────────────────────────────────────────

TEST(ShaderLoaderPipelineSlangIr, RootSlangIrShaderIsRejected) {
    // A SlangIr source used as a root shader (not as a dependency) must be
    // rejected with a SlangCompileError.
    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    auto main_id = make_shader_id(0x81);
    cache.set_shader(main_id, Shader::from_slang_ir({0x01, 0x02}, AssetPath("embedded://mod.slang-module")));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<ShaderCacheError::SlangCompileError>(result.error().data));
    EXPECT_EQ(load_count, 0);
}

TEST(ShaderLoaderPipelineSlangIr, ManuallyAddedDepViaAssetPathImportCompiles) {
    // A SlangIr shader registered directly via set_shader() (asset-path import)
    // can be used as a dependency by a root Slang shader.
    auto ir_bytes = compile_module_to_ir_bytes(
        "module \"embedded://providers/helper\";\n"
        "public int helperValue() { return 99; }\n",
        "embedded://providers/helper.slang", "embedded://providers/helper.slang");
    ASSERT_FALSE(ir_bytes.empty()) << "Module IR compilation failed — Slang toolchain unavailable";

    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    // import_path defaults to asset_path — the root imports it by string path.
    auto dep_id = make_shader_id(0x82);
    cache.set_shader(dep_id, Shader::from_slang_ir(ir_bytes, AssetPath("embedded://providers/helper.slang")));

    auto main_id = make_shader_id(0x83);
    cache.set_shader(main_id, Shader::from_slang("import \"embedded://providers/helper.slang\";\n"
                                                 "[shader(\"compute\")]\n"
                                                 "[numthreads(1,1,1)]\n"
                                                 "void computeMain() { int v = helperValue(); }\n",
                                                 AssetPath("embedded://mesh/main.slang")));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_EQ(load_count, 1);
}

TEST(ShaderLoaderPipelineSlangIr, ManuallyAddedDepViaCustomNameImportCompiles) {
    // A SlangIr shader registered with a custom import_path can be used as a
    // dependency by a root Slang shader that imports it by module name.
    //
    // compile_module_to_ir_bytes uses "utility_helper" (a plain identifier) as
    // the Slang compilation-time module name. The blob is then registered under
    // "shader_custom://utility/helper" by preload_cached_modules, because Slang
    // honours the name passed to loadModuleFromIRBlob, not what is embedded in
    // the blob.
    auto ir_bytes = compile_module_to_ir_bytes("public int helperValue() { return 77; }\n", "utility_helper",
                                               "embedded://providers/utility_helper.slang");
    ASSERT_FALSE(ir_bytes.empty()) << "Module IR compilation failed — Slang toolchain unavailable";

    int load_count = 0;
    ShaderCache cache(null_device(),
                      [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                    ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                          ++load_count;
                          return wgpu::ShaderModule{};
                      });

    auto dep_id     = make_shader_id(0x84);
    auto dep        = Shader::from_slang_ir(ir_bytes, AssetPath("embedded://providers/utility_helper.slang"));
    dep.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    cache.set_shader(dep_id, dep);

    auto main_id = make_shader_id(0x85);
    cache.set_shader(main_id, Shader::from_slang("import utility.helper;\n"
                                                 "[shader(\"compute\")]\n"
                                                 "[numthreads(1,1,1)]\n"
                                                 "void computeMain() { int v = helperValue(); }\n",
                                                 AssetPath("embedded://mesh/main.slang")));

    auto result = cache.get(CachedPipelineId{1}, main_id, {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_EQ(load_count, 1);
}

TEST(ShaderLoaderPipelineSlangIr, FullPipelineEmbeddedRegistryAssetPathImportCompilesAndLoads) {
    // A pre-compiled .slang-module blob registered in the embedded asset registry is
    // loaded automatically by the full asset-server pipeline (not by set_shader directly)
    // when a root Slang shader imports it via an asset-path string import.
    auto ir_bytes = compile_module_to_ir_bytes("public int helperValue() { return 99; }\n", "helper",
                                               "embedded://providers/helper.slang-module");
    ASSERT_FALSE(ir_bytes.empty()) << "Module IR compilation failed - Slang toolchain unavailable";

    auto env       = make_embedded_pipeline_env();
    auto& registry = embedded_registry(env.app);

    // Register the pre-compiled IR blob in the embedded registry.
    insert_bytes_asset(registry, "providers/helper.slang-module", ir_bytes);
    // Root imports the dep by its embedded asset path.
    insert_text_asset(registry, "mesh/main.slang",
                      "import \"embedded://providers/helper.slang-module\";\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() { int v = helperValue(); }\n");

    auto& server     = env.app.resource<AssetServer>();
    auto lib_handle  = server.load<Shader>(AssetPath("embedded://providers/helper.slang-module"));
    auto main_handle = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(lib_handle.id()));
    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_EQ(*env.load_count, 1);
}

TEST(ShaderLoaderPipelineSlangIr, FullPipelineProgrammaticallyAddedDepWithCustomImportCompilesAndLoads) {
    // A SlangIr dep added programmatically via AssetServer::add() (not from a file)
    // with a custom import_path is registered in ShaderCache via the LoadedWithDependencies
    // event emitted by the asset server.  A root Slang shader that imports it by the
    // custom module name compiles successfully.
    auto ir_bytes = compile_module_to_ir_bytes("public int helperValue() { return 77; }\n", "utility_helper",
                                               "embedded://providers/utility_helper.slang-module");
    ASSERT_FALSE(ir_bytes.empty()) << "Module IR compilation failed - Slang toolchain unavailable";

    auto env = make_embedded_pipeline_env();

    // Add dep programmatically (no file in registry) - the asset server emits
    // LoadedWithDependencies which drives sync() to register it in ShaderCache.
    auto dep        = Shader::from_slang_ir(ir_bytes, AssetPath("embedded://providers/utility_helper.slang-module"));
    dep.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto& server    = env.app.resource<AssetServer>();
    auto dep_handle = server.add(std::move(dep));

    // First flush: process dep's LoadedWithDependencies event -> sync registers dep.
    flush_and_sync(env.app);
    ASSERT_TRUE(server.is_loaded_with_dependencies(dep_handle.id()));

    // Load root after dep is registered - custom import resolves immediately.
    auto& registry = embedded_registry(env.app);
    insert_text_asset(registry, "mesh/main.slang",
                      "import utility.helper;\n"
                      "[shader(\"compute\")]\n"
                      "[numthreads(1,1,1)]\n"
                      "void computeMain() { int v = helperValue(); }\n");
    auto main_handle = server.load<Shader>(AssetPath("embedded://mesh/main.slang"));

    // Second flush: root loads and its LoadedWithDependencies is processed.
    flush_and_sync(env.app);

    ASSERT_TRUE(server.is_loaded_with_dependencies(main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_EQ(*env.load_count, 1);
}