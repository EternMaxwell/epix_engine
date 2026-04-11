#include <gtest/gtest.h>

import std;
import epix.assets;
import epix.core;
import epix.shader;

using namespace epix::assets;
using namespace epix::core;
using namespace epix::shader;

namespace {

static auto make_bytes(std::string_view text) {
    auto bytes = std::as_bytes(std::span(text));
    return std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());
}

AssetSourceBuilder make_processed_memory_source_builder(const memory::Directory& source_dir,
                                                        const memory::Directory& processed_dir) {
    return AssetSourceBuilder::create([source_dir]() -> std::unique_ptr<AssetReader> {
               return std::make_unique<MemoryAssetReader>(source_dir);
           })
        .with_processed_reader([processed_dir]() -> std::unique_ptr<AssetReader> {
            return std::make_unique<MemoryAssetReader>(processed_dir);
        })
        .with_processed_writer([processed_dir]() -> std::unique_ptr<AssetWriter> {
            return std::make_unique<MemoryAssetWriter>(processed_dir);
        });
}

struct ProcessedShaderEnv {
    App app;
    memory::Directory processed_dir;
};

ProcessedShaderEnv make_processed_shader_env(const memory::Directory& source_dir) {
    auto processed_dir = memory::Directory::create({});

    App app = App::create();
    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.register_asset_source(AssetSourceId{}, make_processed_memory_source_builder(source_dir, processed_dir));
    plugin.build(app);

    ShaderPlugin shader_plugin;
    shader_plugin.build(app);
    return {std::move(app), processed_dir};
}

bool preprocess_shader_assets(ProcessedShaderEnv& env, std::initializer_list<std::string_view> paths) {
    env.app.run_schedule(Startup);

    auto processor = env.app.get_resource<AssetProcessor>();
    if (!processor.has_value()) return false;

    processor->get().get_data()->wait_until_initialized();
    for (auto path : paths) {
        if (processor->get().get_data()->wait_until_processed(AssetPath(std::string(path))) !=
            ProcessStatus::Processed) {
            return false;
        }
    }
    processor->get().get_data()->wait_until_finished();
    return true;
}

bool wait_for_loaded(App& app,
                     const AssetServer& server,
                     const UntypedAssetId& id,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        auto state = server.get_load_state(id);
        if (state.has_value() && std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state)) {
            return false;
        }
        if (server.is_loaded_with_dependencies(id)) return true;
        app.run_schedule(Last);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::vector<std::uint8_t> read_bytes(const memory::Directory& dir, const std::filesystem::path& path) {
    MemoryAssetReader reader(dir);
    auto stream = reader.read(path);
    if (!stream.has_value()) return {};

    std::stringstream buffer;
    buffer << (*stream)->rdbuf();
    auto raw = buffer.str();
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

bool starts_with_processed_magic(const std::vector<std::uint8_t>& bytes) {
    static constexpr std::array<std::uint8_t, 8> k_magic = {'E', 'P', 'S', 'H', 'P', 'R', '0', '1'};
    return bytes.size() >= k_magic.size() && std::equal(k_magic.begin(), k_magic.end(), bytes.begin());
}

}  // namespace

TEST(ShaderProcessingWgsl, WritesProcessedShaderAssetAndPreservesImports) {
    auto source      = memory::Directory::create({});
    auto main_source = std::string(
        "#import \"dep.wgsl\"\n"
        "#ifdef USE_FAST\n"
        "fn branch() -> i32 { return 1; }\n"
        "#else\n"
        "fn branch() -> i32 { return 2; }\n"
        "#endif\n");

    (void)source.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn dep_fn() {}")));
    (void)source.insert_file("main.wgsl", memory::Value::from_shared(make_bytes(main_source)));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"dep.wgsl", "main.wgsl"}));

    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();
    auto handle  = server.load<Shader>(AssetPath("main.wgsl"));

    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_wgsl());
    EXPECT_EQ(shader->get().source.as_str(), main_source);
    ASSERT_EQ(shader->get().imports.size(), 1u);
    EXPECT_EQ(shader->get().imports[0].as_asset_path().path.generic_string(), "dep.wgsl");
    EXPECT_EQ(shader->get().file_dependencies.size(), 1u);

    auto processed = read_bytes(env.processed_dir, "main.wgsl");
    ASSERT_FALSE(processed.empty());
    EXPECT_TRUE(starts_with_processed_magic(processed));
}

TEST(ShaderProcessingSlang, WritesProcessedShaderAssetAndPreservesCustomImports) {
    auto source = memory::Directory::create({});
    (void)source.insert_file(
        "main.slang",
        memory::Value::from_shared(make_bytes(
            "module main.core;\nimport utility::core;\n[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid main() {}")));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"main.slang"}));

    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();
    auto handle  = server.load<Shader>(AssetPath("main.slang"));

    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_slang());
    ASSERT_EQ(shader->get().imports.size(), 1u);
    EXPECT_TRUE(shader->get().imports[0].is_custom());
    EXPECT_EQ(shader->get().imports[0].as_custom(), "utility/core");
    EXPECT_TRUE(shader->get().file_dependencies.empty());

    auto processed = read_bytes(env.processed_dir, "main.slang");
    ASSERT_FALSE(processed.empty());
    EXPECT_TRUE(starts_with_processed_magic(processed));
}