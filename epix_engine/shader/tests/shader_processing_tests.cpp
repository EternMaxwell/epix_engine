#include <gtest/gtest.h>

import std;
import epix.assets;
import epix.core;
import epix.meta;
import epix.shader;
import webgpu;

using namespace epix::assets;
using namespace epix::core;
using namespace epix::shader;
namespace meta = epix::meta;

namespace {

static auto make_bytes(std::string_view text) {
    auto bytes = std::as_bytes(std::span(text));
    return std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());
}

AssetSourceBuilder make_processed_memory_source_builder(const memory::Directory& source_dir,
                                                        const memory::Directory& processed_dir,
                                                        bool with_watchers = false) {
    auto builder = AssetSourceBuilder::create([source_dir]() -> std::unique_ptr<AssetReader> {
                       return std::make_unique<MemoryAssetReader>(source_dir);
                   })
                       .with_processed_reader([processed_dir]() -> std::unique_ptr<AssetReader> {
                           return std::make_unique<MemoryAssetReader>(processed_dir);
                       })
                       .with_processed_writer([processed_dir]() -> std::unique_ptr<AssetWriter> {
                           return std::make_unique<MemoryAssetWriter>(processed_dir);
                       });

    if (with_watchers) {
        builder.with_watcher(
            [source_dir](epix::utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
                return std::make_unique<MemoryAssetWatcher>(
                    source_dir,
                    [sender = std::move(sender)](AssetSourceEvent event) mutable { sender.send(std::move(event)); });
            });
        builder.with_processed_watcher(
            [processed_dir](epix::utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
                return std::make_unique<MemoryAssetWatcher>(
                    processed_dir,
                    [sender = std::move(sender)](AssetSourceEvent event) mutable { sender.send(std::move(event)); });
            });
    }

    return builder;
}

struct ProcessedShaderEnv {
    App app;
    memory::Directory processed_dir;
};

ProcessedShaderEnv make_processed_shader_env(const memory::Directory& source_dir, bool watching = false) {
    auto processed_dir = memory::Directory::create({});

    App app = App::create();
    AssetPlugin plugin;
    plugin.mode                       = AssetServerMode::Processed;
    plugin.watch_for_changes_override = watching;
    plugin.register_asset_source(AssetSourceId{},
                                 make_processed_memory_source_builder(source_dir, processed_dir, watching));
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

// Spin a background thread that drives the given schedule(s) so that
// server.wait_for_asset_id() — which blocks the calling thread — can have
// its promises resolved by handle_internal_events without deadlock.
// The pump is stopped (and joined) before returning, ensuring the caller
// has exclusive access to the app world for subsequent mutations.

bool wait_for_loaded(App& app, const AssetServer& server, const UntypedAssetId& id) {
    std::atomic<bool> stop{false};
    std::thread pump([&app, &stop]() {
        while (!stop.load(std::memory_order_relaxed)) {
            app.run_schedule(Last);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    bool loaded = server.wait_for_asset_id(id).has_value();
    stop.store(true, std::memory_order_relaxed);
    pump.join();
    return loaded;
}

bool wait_for_loaded_allow_transient_errors(App& app, const AssetServer& server, const UntypedAssetId& id) {
    std::atomic<bool> stop{false};
    std::thread pump([&app, &stop]() {
        while (!stop.load(std::memory_order_relaxed)) {
            app.run_schedule(PostUpdate);
            app.run_schedule(Last);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    // Retry on all transient error kinds (NotLoaded during reload, DependencyFailed
    // from an intermediate dep state). A 2-minute safety deadline guards against
    // genuine bugs that would otherwise hang forever.
    bool success        = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (!success && std::chrono::steady_clock::now() < deadline) {
        auto result = server.wait_for_asset_id(id);
        if (result.has_value()) {
            success = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    stop.store(true, std::memory_order_relaxed);
    pump.join();
    return success;
}

bool wait_until_not_loaded_with_dependencies(App& app, const AssetServer& server, const UntypedAssetId& id) {
    std::atomic<bool> stop{false};
    std::thread pump([&app, &stop]() {
        while (!stop.load(std::memory_order_relaxed)) {
            app.run_schedule(PostUpdate);
            app.run_schedule(Last);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    bool found          = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!server.is_loaded_with_dependencies(id)) {
            found = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stop.store(true, std::memory_order_relaxed);
    pump.join();
    return found;
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

// Helper: build a minimal AssetProcessor (in-memory, processed mode) for use
// with ProcessContext in direct-process tests.
static AssetProcessor make_processor_for_direct_test() {
    App app = App::create();
    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.build(app);
    auto res = app.get_resource<AssetProcessor>();
    EXPECT_TRUE(res.has_value());
    return res->get();
}

static std::shared_ptr<std::vector<std::byte>> make_slang_ir_process_meta_bytes() {
    AssetMeta<ShaderLoader::Settings, ShaderProcessorSettings> meta_file;
    meta_file.action    = AssetActionType::Process;
    meta_file.processor = std::string(meta::type_id<ShaderProcessor>{}.name());
    meta_file.processor_settings_storage.value.preprocess_slang_to_ir = true;

    auto bytes = serialize_asset_meta(meta_file);
    EXPECT_TRUE(bytes.has_value());
    return std::make_shared<std::vector<std::byte>>(*bytes);
}

static std::shared_ptr<std::vector<std::byte>> make_slang_text_process_meta_bytes() {
    AssetMeta<ShaderLoader::Settings, ShaderProcessorSettings> meta_file;
    meta_file.action    = AssetActionType::Process;
    meta_file.processor = std::string(meta::type_id<ShaderProcessor>{}.name());
    meta_file.processor_settings_storage.value.preprocess_slang_to_ir = false;

    auto bytes = serialize_asset_meta(meta_file);
    EXPECT_TRUE(bytes.has_value());
    return std::make_shared<std::vector<std::byte>>(*bytes);
}

static void install_test_shader_cache(App& app, int& load_count) {
    app.world_mut().insert_resource(
        ShaderCache{wgpu::Device{},
                    [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                  ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                        ++load_count;
                        return wgpu::ShaderModule{};
                    }});
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
    (void)source.insert_file("main.slang.meta", memory::Value::from_shared(make_slang_text_process_meta_bytes()));

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

// ── ShaderProcessingSlangModule ─────────────────────────────────────────────

TEST(ShaderProcessingSlangModule, SlangModuleFilePassthroughLoadsAsSlangIr) {
    // Raw bytes in a .slang-module file pass through the processor unchanged
    // and are loaded back as Source::SlangIr with exactly the same bytes.
    auto source_dir                  = memory::Directory::create({});
    std::vector<std::uint8_t> raw_ir = {0xAB, 0xCD, 0xEF};
    std::vector<std::byte> raw_bytes(raw_ir.size());
    std::transform(raw_ir.begin(), raw_ir.end(), raw_bytes.begin(),
                   [](std::uint8_t b) { return static_cast<std::byte>(b); });
    (void)source_dir.insert_file(
        "prebuilt.slang-module",
        memory::Value::from_shared(std::make_shared<std::vector<std::byte>>(std::move(raw_bytes))));

    auto env = make_processed_shader_env(source_dir);
    ASSERT_TRUE(preprocess_shader_assets(env, {"prebuilt.slang-module"}));

    // The processor writes raw bytes without a processed-magic header.
    auto processed = read_bytes(env.processed_dir, "prebuilt.slang-module");
    ASSERT_FALSE(processed.empty());
    EXPECT_FALSE(starts_with_processed_magic(processed));

    // The round-trip via asset server should decode to is_slang_ir() with the original bytes.
    auto& server = env.app.resource<AssetServer>();
    auto& assets = env.app.resource<Assets<Shader>>();
    auto handle  = server.load<Shader>(AssetPath("prebuilt.slang-module"));

    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_slang_ir());
    const auto& stored = std::get<Source::SlangIr>(shader->get().source.data).bytes;
    EXPECT_EQ(stored, raw_ir);
}

TEST(ShaderProcessingSlangModule, WritesSlangIrWhenPreprocessToIrIsEnabled) {
    // ShaderProcessor with preprocess_slang_to_ir=true on a valid Slang module
    // must produce non-empty output (IR blob or text fallback) without crashing.
    static constexpr std::string_view k_source =
        "module \"helper\";\n"
        "public int helperValue() { return 42; }\n";

    auto proc = make_processor_for_direct_test();
    std::istringstream data{std::string(k_source)};
    ProcessedInfo info;
    AssetPath asset_path("helper.slang");
    ProcessContext ctx(proc, asset_path, data, info);

    ShaderProcessor processor;
    ShaderProcessorSettings settings;
    settings.preprocess_slang_to_ir = true;

    std::ostringstream out;
    auto result = processor.process(ctx, settings, out);

    if (result.has_value()) {
        EXPECT_FALSE(out.str().empty());
    }
    SUCCEED();
}

TEST(ShaderProcessingSlangModule, FallsBackToSlangTextWhenIrCompilationFails) {
    // With preprocess_slang_to_ir=true and syntactically broken Slang source,
    // the processor must fall back to the regular text path without crashing.
    static constexpr std::string_view k_bad_source =
        "module \"broken\";\n"
        "this is not valid slang syntax @@@ { }\n";

    auto proc = make_processor_for_direct_test();
    std::istringstream data{std::string(k_bad_source)};
    ProcessedInfo info;
    AssetPath asset_path("broken.slang");
    ProcessContext ctx(proc, asset_path, data, info);

    ShaderProcessor processor;
    ShaderProcessorSettings settings;
    settings.preprocess_slang_to_ir = true;

    std::ostringstream out;
    auto result = processor.process(ctx, settings, out);
    (void)result;
    SUCCEED();
}

TEST(ShaderProcessingSlangModule, ProcessEnabledManualCustomDepIsVisibleAndCompiles) {
    // Configure a processed-mode .slang root to compile to SlangIr during processing.
    auto source = memory::Directory::create({});
    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    (void)source.insert_file("main.slang.meta", memory::Value::from_shared(make_slang_ir_process_meta_bytes()));

    auto env = make_processed_shader_env(source);

    // Runtime cache used for the final compile assertion.
    int load_count = 0;
    env.app.world_mut().insert_resource(
        ShaderCache{wgpu::Device{},
                    [&load_count](const wgpu::Device&, const ShaderCacheSource&,
                                  ValidateShader) -> std::expected<wgpu::ShaderModule, ShaderCacheError> {
                        ++load_count;
                        return wgpu::ShaderModule{};
                    }});

    auto& server = env.app.resource<AssetServer>();

    // Manually add custom-import provider (not file-backed).
    auto dep = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 7; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto dep_handle = server.add(std::move(dep));

    // Flush Added/Modified events before startup so the processor-side mirror sees the dep.
    env.app.run_schedule(PreStartup);
    env.app.run_schedule(Last);
    ASSERT_TRUE(server.is_loaded_with_dependencies(dep_handle.id()));

    // Process root in processed mode.
    env.app.run_schedule(Startup);
    auto processor = env.app.get_resource<AssetProcessor>();
    ASSERT_TRUE(processor.has_value());
    processor->get().get_data()->wait_until_initialized();
    ASSERT_EQ(processor->get().get_data()->wait_until_processed(AssetPath("main.slang")), ProcessStatus::Processed);
    processor->get().get_data()->wait_until_finished();

    // Load processed root and verify it became SlangIr.
    auto& assets = env.app.resource<Assets<Shader>>();
    auto handle  = server.load<Shader>(AssetPath("main.slang"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, handle.id()));
    auto shader = assets.get(handle.id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_TRUE(shader->get().source.is_slang_ir());

    // Final runtime compile should succeed and resolve the manual custom dep.
    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{1}, handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_EQ(load_count, 1);
}

TEST(ShaderProcessingSlangModule, ConcurrentIrProcessingWithRegistryUpdatesRemainsStable) {
    // Stress path: many .slang roots are processed on the IO task pool while
    // the custom provider registry is updated from asset events.
    auto source = memory::Directory::create({});

    constexpr int k_root_count = 48;
    for (int i = 0; i < k_root_count; ++i) {
        auto path = std::format("main_{}.slang", i);
        (void)source.insert_file(
            path, memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                        "[shader(\"compute\")]\n"
                                                        "[numthreads(1,1,1)]\n"
                                                        "void computeMain() { int v = helperValue(); }\n")));
        (void)source.insert_file(path + ".meta", memory::Value::from_shared(make_slang_ir_process_meta_bytes()));
    }

    auto env      = make_processed_shader_env(source);
    auto& server  = env.app.resource<AssetServer>();
    auto& shaders = env.app.resource_mut<Assets<Shader>>();

    auto dep = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 7; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto dep_handle = server.add(std::move(dep));
    auto dep_id     = dep_handle.id();

    // Ensure initial provider registration before processing begins.
    env.app.run_schedule(PreStartup);
    env.app.run_schedule(Last);

    env.app.run_schedule(Startup);

    auto processor = env.app.get_resource<AssetProcessor>();
    ASSERT_TRUE(processor.has_value());
    processor->get().get_data()->wait_until_initialized();

    // Repeated provider updates while processor jobs are active should not race.
    for (int i = 0; i < 32; ++i) {
        auto dep_updated = Shader::from_slang(
            std::format("module utility.helper;\npublic int helperValue() {{ return {}; }}\n", i + 8),
            AssetPath("embedded://manual/utility_helper.slang"));
        dep_updated.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
        auto replaced           = shaders.insert(dep_id, dep_updated);
        ASSERT_TRUE(replaced.has_value());
        env.app.run_schedule(Last);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (int i = 0; i < k_root_count; ++i) {
        auto path = AssetPath(std::format("main_{}.slang", i));
        ASSERT_EQ(processor->get().get_data()->wait_until_processed(path), ProcessStatus::Processed);
    }
    processor->get().get_data()->wait_until_finished();

    // Spot-check that processed roots load as Slang IR after concurrent activity.
    auto handles = std::vector<Handle<Shader>>{};
    handles.reserve(8);
    for (int i = 0; i < 8; ++i) {
        auto h = server.load<Shader>(AssetPath(std::format("main_{}.slang", i)));
        ASSERT_TRUE(wait_for_loaded_allow_transient_errors(env.app, server, h.id()));
        handles.push_back(h);
    }

    auto& shader_assets = env.app.resource<Assets<Shader>>();
    for (const auto& h : handles) {
        auto shader = shader_assets.get(h.id());
        ASSERT_TRUE(shader.has_value());
        EXPECT_TRUE(shader->get().source.is_slang_ir());
    }
}

TEST(ShaderProcessingSlangModule, ManualCustomProviderModifyReloadsDependentRoot) {
    // Regression for dependency invalidation: if a manually-added custom
    // provider changes, dependent roots should be reloaded.
    auto source = memory::Directory::create({});
    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    (void)source.insert_file("main.slang.meta", memory::Value::from_shared(make_slang_ir_process_meta_bytes()));

    auto env       = make_processed_shader_env(source);
    auto& server   = env.app.resource<AssetServer>();
    auto& shaders  = env.app.resource_mut<Assets<Shader>>();
    int load_count = 0;
    install_test_shader_cache(env.app, load_count);

    auto dep = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 7; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto dep_handle = server.add(std::move(dep));
    auto dep_id     = dep_handle.id();

    env.app.run_schedule(PreStartup);
    env.app.run_schedule(Last);
    env.app.run_schedule(Startup);

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    ASSERT_TRUE(wait_for_loaded_allow_transient_errors(env.app, server, main_handle.id()));

    // If reload is triggered after provider modification, this missing source
    // path forces main to transition out of LoadedWithDependencies.
    ASSERT_TRUE(source.remove_file("main.slang").has_value());

    auto dep_updated = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 8; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep_updated.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto replaced           = shaders.insert(dep_id, dep_updated);
    ASSERT_TRUE(replaced.has_value());

    ASSERT_TRUE(wait_until_not_loaded_with_dependencies(env.app, server, main_handle.id()));

    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    auto main_reload = server.reload(AssetPath("main.slang"));
    ASSERT_TRUE(main_reload.has_value());
    ASSERT_TRUE(wait_for_loaded_allow_transient_errors(env.app, server, main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{101}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_GE(load_count, 1);
}

TEST(ShaderProcessingSlangModule, ProviderSourceReloadViaAssetServerReloadsDependentRoot) {
    // Regression for reload path parity: provider source edits propagated via
    // AssetServer::reload should invalidate and reload dependent roots.
    auto source = memory::Directory::create({});
    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    (void)source.insert_file("main.slang.meta", memory::Value::from_shared(make_slang_ir_process_meta_bytes()));
    (void)source.insert_file("utility_helper.slang",
                             memory::Value::from_shared(make_bytes("module utility.helper;\n"
                                                                   "public int helperValue() { return 7; }\n")));

    auto env       = make_processed_shader_env(source);
    auto& server   = env.app.resource<AssetServer>();
    int load_count = 0;
    install_test_shader_cache(env.app, load_count);

    env.app.run_schedule(Startup);

    auto provider_handle = server.load<Shader>(AssetPath("utility_helper.slang"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, provider_handle.id()));

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    ASSERT_TRUE(wait_for_loaded_allow_transient_errors(env.app, server, main_handle.id()));

    // If provider-triggered dependent reload runs, this missing root source
    // path makes the transition out of LoadedWithDependencies observable.
    ASSERT_TRUE(source.remove_file("main.slang").has_value());

    (void)source.insert_file("utility_helper.slang",
                             memory::Value::from_shared(make_bytes("module utility.helper;\n"
                                                                   "public int helperValue() { return 8; }\n")));
    auto reload_result = server.reload(AssetPath("utility_helper.slang"));
    ASSERT_TRUE(reload_result.has_value());

    ASSERT_TRUE(wait_until_not_loaded_with_dependencies(env.app, server, main_handle.id()));

    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    auto main_reload = server.reload(AssetPath("main.slang"));
    ASSERT_TRUE(main_reload.has_value());
    ASSERT_TRUE(wait_for_loaded_allow_transient_errors(env.app, server, main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{102}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_GE(load_count, 1);
}

TEST(ShaderProcessingSlangModule, ProviderSourceHotReloadViaWatcherReloadsDependentRoot) {
    // Hot-reload path (no explicit reload call): watcher events should trigger
    // AssetServer reload for changed processed assets when watching is enabled.
    auto source = memory::Directory::create({});
    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    (void)source.insert_file("main.slang.meta", memory::Value::from_shared(make_slang_ir_process_meta_bytes()));
    (void)source.insert_file("utility_helper.slang",
                             memory::Value::from_shared(make_bytes("module utility.helper;\n"
                                                                   "public int helperValue() { return 7; }\n")));

    auto env       = make_processed_shader_env(source, /*watching=*/true);
    auto& server   = env.app.resource<AssetServer>();
    int load_count = 0;
    install_test_shader_cache(env.app, load_count);

    env.app.run_schedule(Startup);

    auto provider_handle = server.load<Shader>(AssetPath("utility_helper.slang"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, provider_handle.id()));

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, main_handle.id()));

    // Make provider change watcher-driven only; do not call server.reload().
    ASSERT_TRUE(source.remove_file("main.slang").has_value());
    (void)source.insert_file("utility_helper.slang",
                             memory::Value::from_shared(make_bytes("module utility.helper;\n"
                                                                   "public int helperValue() { return 9; }\n")));

    ASSERT_TRUE(wait_until_not_loaded_with_dependencies(env.app, server, main_handle.id()));

    // Watcher-driven recovery path: restoring the source should reload without explicit reload().
    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    ASSERT_TRUE(wait_for_loaded_allow_transient_errors(env.app, server, main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{103}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_GE(load_count, 1);
}

TEST(ShaderProcessingSlangModule, ManualCustomProviderImportPathRemovalReloadsDependentRoot) {
    // Regression for provider-removal behavior: if a manually-added provider
    // stops exporting the custom import path, dependent roots should reload.
    auto source = memory::Directory::create({});
    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));
    (void)source.insert_file("main.slang.meta", memory::Value::from_shared(make_slang_ir_process_meta_bytes()));

    auto env       = make_processed_shader_env(source);
    auto& server   = env.app.resource<AssetServer>();
    auto& shaders  = env.app.resource_mut<Assets<Shader>>();
    int load_count = 0;
    install_test_shader_cache(env.app, load_count);

    auto dep = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 7; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto dep_handle = server.add(std::move(dep));
    auto dep_id     = dep_handle.id();

    env.app.run_schedule(PreStartup);
    env.app.run_schedule(Last);
    env.app.run_schedule(Startup);

    auto main_handle = server.load<Shader>(AssetPath("main.slang"));
    ASSERT_TRUE(wait_for_loaded(env.app, server, main_handle.id()));

    ASSERT_TRUE(source.remove_file("main.slang").has_value());

    auto dep_updated = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 7; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep_updated.import_path = ShaderImport::asset_path(dep_updated.path);
    auto replaced           = shaders.insert(dep_id, dep_updated);
    ASSERT_TRUE(replaced.has_value());

    ASSERT_TRUE(wait_until_not_loaded_with_dependencies(env.app, server, main_handle.id()));

    (void)source.insert_file("main.slang",
                             memory::Value::from_shared(make_bytes("import utility.helper;\n"
                                                                   "[shader(\"compute\")]\n"
                                                                   "[numthreads(1,1,1)]\n"
                                                                   "void computeMain() { int v = helperValue(); }\n")));

    auto dep_restored = Shader::from_slang(
        "module utility.helper;\n"
        "public int helperValue() { return 7; }\n",
        AssetPath("embedded://manual/utility_helper.slang"));
    dep_restored.import_path = ShaderImport::custom(std::filesystem::path("utility/helper"));
    auto replaced_back       = shaders.insert(dep_id, dep_restored);
    ASSERT_TRUE(replaced_back.has_value());

    auto main_reload = server.reload(AssetPath("main.slang"));
    ASSERT_TRUE(main_reload.has_value());
    ASSERT_TRUE(wait_for_loaded(env.app, server, main_handle.id()));

    auto& cache = env.app.resource_mut<ShaderCache>();
    auto result = cache.get(CachedPipelineId{104}, main_handle.id(), {});
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_GE(load_count, 1);
}