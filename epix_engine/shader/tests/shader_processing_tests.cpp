#include <gtest/gtest.h>

import std;
import epix.core;
import epix.assets;
import epix.shader;

using namespace epix::shader;
using namespace epix::assets;
using namespace epix::core;

namespace {

static auto make_bytes(std::string_view s) {
    auto sp = std::as_bytes(std::span(s));
    return std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
}

static auto make_bytes_from_vec(const std::vector<std::uint8_t>& v) {
    auto sp = std::as_bytes(std::span(v));
    return std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
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
    memory::Directory source_dir;
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
    return {std::move(app), source_dir, processed_dir};
}

bool wait_for_loaded(App& app,
                     const AssetServer& server,
                     const UntypedAssetId& id,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        auto state = server.get_load_state(id);
        if (state.has_value() && std::holds_alternative<AssetLoadError>(*state)) {
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

    std::stringstream ss;
    ss << (*stream)->rdbuf();
    auto raw = ss.str();
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

bool preprocess_shader_assets(ProcessedShaderEnv& env,
                              std::initializer_list<std::string_view> paths,
                              std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    try {
        env.app.run_schedule(Startup);

        auto processor = env.app.get_resource<AssetProcessor>();
        if (!processor.has_value()) return false;

        processor->get().get_data()->wait_until_initialized();
        (void)timeout;

        for (auto path : paths) {
            if (processor->get().get_data()->wait_until_processed(AssetPath(std::string(path))) !=
                ProcessStatus::Processed) {
                return false;
            }
        }

        processor->get().get_data()->wait_until_finished();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "preprocess_shader_assets exception: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "preprocess_shader_assets unknown exception" << std::endl;
        return false;
    }
}

bool starts_with_processed_magic(const std::vector<std::uint8_t>& bytes) {
    static constexpr std::array<std::uint8_t, 8> k_magic = {'E', 'P', 'S', 'H', 'P', 'R', '0', '1'};
    return bytes.size() >= k_magic.size() && std::equal(k_magic.begin(), k_magic.end(), bytes.begin());
}

TEST(ShaderProcessingWgsl, ProcessedMode_WritesBinaryAndLoadsDeps) {
    auto source = memory::Directory::create({});
    (void)source.insert_file("dep.wgsl", memory::Value::from_shared(make_bytes("fn dep_fn() {}")));
    (void)source.insert_file("main.wgsl",
                             memory::Value::from_shared(make_bytes("#import \"dep.wgsl\"\nfn main_fn() {}")));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"dep.wgsl", "main.wgsl"}));
    auto& server  = env.app.resource<AssetServer>();
    auto& assets  = env.app.resource<Assets<Shader>>();
    auto main_hnd = server.load<Shader>(AssetPath("main.wgsl"));

    ASSERT_TRUE(wait_for_loaded(env.app, server, main_hnd.id()));
    auto shader = assets.get(main_hnd.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_wgsl());
    EXPECT_EQ(shader->get().path, AssetPath("main.wgsl"));
    ASSERT_EQ(shader->get().imports.size(), 1u);
    EXPECT_TRUE(shader->get().imports[0].is_asset_path());
    EXPECT_EQ(shader->get().imports[0].as_asset_path().path.generic_string(), "dep.wgsl");
    EXPECT_EQ(shader->get().file_dependencies.size(), 1u);

    auto processed = read_bytes(env.processed_dir, "main.wgsl");
    ASSERT_FALSE(processed.empty());
    EXPECT_TRUE(starts_with_processed_magic(processed));
}

TEST(ShaderProcessingWgsl, ProcessedMode_CyclicImportsStillSerialize) {
    auto source = memory::Directory::create({});
    (void)source.insert_file("a.wgsl", memory::Value::from_shared(make_bytes("#import \"b.wgsl\"\nfn a() {}")));
    (void)source.insert_file("b.wgsl", memory::Value::from_shared(make_bytes("#import \"a.wgsl\"\nfn b() {}")));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"a.wgsl", "b.wgsl"}));

    auto processed_a = read_bytes(env.processed_dir, "a.wgsl");
    auto processed_b = read_bytes(env.processed_dir, "b.wgsl");

    ASSERT_FALSE(processed_a.empty());
    ASSERT_FALSE(processed_b.empty());
    EXPECT_TRUE(starts_with_processed_magic(processed_a));
    EXPECT_TRUE(starts_with_processed_magic(processed_b));
}

TEST(ShaderProcessingWgsl, ProcessedMode_LargeSourceRoundTrips) {
    auto source              = memory::Directory::create({});
    std::string large_source = "#import \"common.wgsl\"\n";
    for (int index = 0; index < 2048; ++index) {
        large_source += std::format("fn generated_{}() -> i32 {{ return {}; }}\n", index, index);
    }

    (void)source.insert_file("common.wgsl", memory::Value::from_shared(make_bytes("fn common() {}")));
    (void)source.insert_file("large.wgsl", memory::Value::from_shared(make_bytes(large_source)));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"common.wgsl", "large.wgsl"}));
    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();

    auto handle = server.load<Shader>(AssetPath("large.wgsl"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));

    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_wgsl());
    EXPECT_EQ(shader->get().source.as_str(), large_source);
    ASSERT_EQ(shader->get().imports.size(), 1u);
    EXPECT_EQ(shader->get().imports[0].as_asset_path().path.generic_string(), "common.wgsl");

    auto processed = read_bytes(env.processed_dir, "large.wgsl");
    ASSERT_FALSE(processed.empty());
    EXPECT_TRUE(starts_with_processed_magic(processed));
}

TEST(ShaderProcessingSlang, ProcessedMode_PreservesCustomImportsWithoutFileDeps) {
    auto source = memory::Directory::create({});
    (void)source.insert_file("utility.slang", memory::Value::from_shared(make_bytes("float util() { return 1.0; }")));
    (void)source.insert_file(
        "main.slang", memory::Value::from_shared(
                          make_bytes("import utility;\n[shader(\"compute\")]\n[numthreads(1,1,1)]\nvoid main() {}")));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"utility.slang", "main.slang"}));
    auto& server  = env.app.resource<AssetServer>();
    auto& assets  = env.app.resource<Assets<Shader>>();
    auto main_hnd = server.load<Shader>(AssetPath("main.slang"));

    ASSERT_TRUE(wait_for_loaded(env.app, server, main_hnd.id()));
    auto shader = assets.get(main_hnd.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_slang());
    EXPECT_EQ(shader->get().path, AssetPath("main.slang"));
    ASSERT_EQ(shader->get().imports.size(), 1u);
    EXPECT_TRUE(shader->get().imports[0].is_custom());
    EXPECT_EQ(shader->get().imports[0].as_custom(), "utility.slang");
    EXPECT_EQ(shader->get().file_dependencies.size(), 0u);

    auto processed = read_bytes(env.processed_dir, "main.slang");
    ASSERT_FALSE(processed.empty());
    EXPECT_TRUE(starts_with_processed_magic(processed));
}

TEST(ShaderProcessingSlang, ProcessedMode_DoesNotConvertToSpirv) {
    auto source = memory::Directory::create({});
    (void)source.insert_file("scene.slang", memory::Value::from_shared(make_bytes("module scene;\nvoid main() {}")));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"scene.slang"}));
    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();

    auto handle = server.load<Shader>(AssetPath("scene.slang"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_slang());
    EXPECT_FALSE(shader->get().source.is_spirv());
}

TEST(ShaderProcessing, InvalidUtf8PreprocessFailureBlocksLoad) {
    auto source                = memory::Directory::create({});
    std::vector<std::byte> bad = {std::byte{0xFF}, std::byte{0xFE}};
    auto bad_shared            = std::make_shared<std::vector<std::byte>>(bad);
    (void)source.insert_file("bad.wgsl", memory::Value::from_shared(bad_shared));

    auto env = make_processed_shader_env(source);
    EXPECT_FALSE(preprocess_shader_assets(env, {"bad.wgsl"}));
    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();

    auto processed = read_bytes(env.processed_dir, "bad.wgsl");
    EXPECT_TRUE(processed.empty());

    // In Processed mode, ProcessStatus::Failed causes the gated reader to return NotFound.
    // The asset server marks the load as failed — no fallback to source (matches Bevy behavior).
    auto handle = server.load<Shader>(AssetPath("bad.wgsl"));
    const auto start = std::chrono::steady_clock::now();
    bool load_failed = false;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500)) {
        auto state = server.get_load_state(handle.id());
        if (state.has_value() && std::holds_alternative<AssetLoadError>(*state)) {
            load_failed = true;
            break;
        }
        env.app.run_schedule(Last);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(load_failed);
    EXPECT_FALSE(assets.get(handle.id()).has_value());
}

TEST(ShaderProcessingSpirv, ProcessedMode_CopyThroughStillLoads) {
    auto source                     = memory::Directory::create({});
    std::vector<std::uint8_t> spirv = {0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00};
    (void)source.insert_file("compiled.spv", memory::Value::from_shared(make_bytes_from_vec(spirv)));

    auto env = make_processed_shader_env(source);
    ASSERT_TRUE(preprocess_shader_assets(env, {"compiled.spv"}));
    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();

    auto handle = server.load<Shader>(AssetPath("compiled.spv"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_spirv());

    auto processed = read_bytes(env.processed_dir, "compiled.spv");
    ASSERT_EQ(processed.size(), spirv.size());
    for (std::size_t i = 0; i < spirv.size(); ++i) {
        EXPECT_EQ(processed[i], spirv[i]);
    }
}

}  // namespace
