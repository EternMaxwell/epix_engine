#include <gtest/gtest.h>

import std;
import epix.core;
import epix.assets;

using namespace assets;
using namespace core;

namespace {
memory::Directory make_memory_dir_with_text(std::string_view content = "hello") {
    auto dir = memory::Directory::create({});
    auto buf = std::make_shared<std::vector<std::uint8_t>>(content.begin(), content.end());
    auto res = dir.insert_file("hello.txt", memory::Value::from_shared(buf));
    EXPECT_TRUE(res.has_value());
    return dir;
}

AssetSourceBuilder make_memory_source_builder(const memory::Directory& dir, bool with_processed_reader = true) {
    auto builder = AssetSourceBuilder::create(
        [dir]() -> std::unique_ptr<AssetReader> { return std::make_unique<MemoryAssetReader>(dir); });
    if (with_processed_reader) {
        builder.with_processed_reader(
            [dir]() -> std::unique_ptr<AssetReader> { return std::make_unique<MemoryAssetReader>(dir); });
    }
    return builder;
}

AssetSourceBuilder make_memory_source_builder_with_watchers(const memory::Directory& dir,
                                                            bool with_processed_reader  = true,
                                                            bool with_processed_watcher = true) {
    auto builder = make_memory_source_builder(dir, with_processed_reader)
                       .with_watcher([dir](utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
                           return std::make_unique<MemoryAssetWatcher>(
                               dir, [sender = std::move(sender)](AssetSourceEvent event) mutable {
                                   sender.send(std::move(event));
                               });
                       });
    if (with_processed_watcher) {
        builder.with_processed_watcher([dir](utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
            return std::make_unique<MemoryAssetWatcher>(
                dir, [sender = std::move(sender)](AssetSourceEvent event) mutable { sender.send(std::move(event)); });
        });
    }
    return builder;
}

AssetServer make_memory_server(bool with_processed_reader = true,
                               AssetServerMode mode       = AssetServerMode::Unprocessed,
                               bool watching              = false) {
    auto dir      = make_memory_dir_with_text();
    auto builders = AssetSourceBuilders();
    builders.insert(AssetSourceId{}, make_memory_source_builder(dir, with_processed_reader));
    auto sources = std::make_shared<AssetSources>(builders.build_sources(watching, watching));
    return AssetServer(std::move(sources), mode, AssetMetaCheck::Always, watching, UnapprovedPathMode::Forbid);
}

struct TestTextLoader {
    using Asset = std::string;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    static inline std::atomic<int> load_count{0};

    static void reset_stats() { load_count.store(0); }

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"txt"}, std::string_view{"text"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static std::expected<std::string, Error> load(std::istream& reader, const Settings&, assets::LoadContext&) {
        load_count.fetch_add(1);
        std::stringstream ss;
        ss << reader.rdbuf();
        return ss.str();
    }
};

struct TestProcess {
    struct Settings : assets::Settings {};
    using OutputLoader = TestTextLoader;

    std::expected<OutputLoader::Settings, std::exception_ptr> process(ProcessContext&,
                                                                      const Settings&,
                                                                      std::ostream&) const {
        return OutputLoader::Settings{};
    }
};

std::vector<AssetSourceEvent> drain_source_events(const utils::Receiver<AssetSourceEvent>& receiver,
                                                  std::size_t max_count = 16) {
    std::vector<AssetSourceEvent> events;
    events.reserve(max_count);
    for (std::size_t i = 0; i < max_count; ++i) {
        auto maybe = receiver.try_receive();
        if (!maybe) break;
        events.push_back(std::move(*maybe));
    }
    return events;
}

bool has_added_asset_event_for(const std::vector<AssetSourceEvent>& events, const std::filesystem::path& path) {
    return std::ranges::any_of(events, [&](const AssetSourceEvent& event) {
        if (auto added = std::get_if<source_events::AddedAsset>(&event)) {
            return added->path == path;
        }
        return false;
    });
}

bool has_modified_asset_event_for(const std::vector<AssetSourceEvent>& events, const std::filesystem::path& path) {
    return std::ranges::any_of(events, [&](const AssetSourceEvent& event) {
        if (auto modified = std::get_if<source_events::ModifiedAsset>(&event)) {
            return modified->path == path;
        }
        return false;
    });
}
}  // namespace

TEST(AssetServer, RegisterLoader_CanQueryByExtensionTypeAndAssetType) {
    auto server = make_memory_server();

    server.register_loader(TestTextLoader{});

    auto by_ext = server.get_asset_loader_with_extension("txt");
    ASSERT_TRUE(by_ext != nullptr);

    auto by_type = server.get_asset_loader_with_type_name(meta::type_id<TestTextLoader>{}.name());
    ASSERT_TRUE(by_type != nullptr);

    auto by_asset_type = server.get_asset_loader_with_asset_type<std::string>();
    ASSERT_TRUE(by_asset_type != nullptr);

    auto by_type_id = server.get_asset_loader_with_asset_type_id(meta::type_id<std::string>{});
    ASSERT_TRUE(by_type_id != nullptr);

    auto by_path = server.get_path_asset_loader(AssetPath("hello.txt"));
    ASSERT_TRUE(by_path != nullptr);
}

TEST(AssetServer, PreregisterLoader_ThenRegister_ResolvesByExtension) {
    auto server = make_memory_server();

    static auto exts = std::array{std::string_view{"txt"}};
    server.preregister_loader<TestTextLoader>(std::span<std::string_view>(exts.data(), exts.size()));
    server.register_loader(TestTextLoader{});

    auto by_ext = server.get_asset_loader_with_extension("txt");
    ASSERT_TRUE(by_ext != nullptr);
}

TEST(AssetServer, ConstructorOptions_AreVisibleFromQueries) {
    auto server = make_memory_server(true, AssetServerMode::Processed, true);

    EXPECT_EQ(server.mode(), AssetServerMode::Processed);
    EXPECT_TRUE(server.watching_for_changes());
    EXPECT_TRUE(server.get_source(AssetSourceId{}).has_value());
    EXPECT_FALSE(server.get_source(AssetSourceId(std::string("missing"))).has_value());
}

TEST(AssetServer, LoadErased_CreatesTrackableHandleAndPathMappings) {
    auto server = make_memory_server();
    Assets<std::string> assets;
    server.register_assets(assets);

    auto path   = AssetPath("hello.txt");
    auto handle = server.load_erased(meta::type_id<std::string>{}, path);

    EXPECT_TRUE(server.is_managed(handle.id()));

    auto typed = server.get_handle<std::string>(path);
    ASSERT_TRUE(typed.has_value());
    EXPECT_EQ(typed->id(), handle.id().typed<std::string>());

    auto untyped = server.get_handle_untyped(path);
    ASSERT_TRUE(untyped.has_value());
    EXPECT_EQ(untyped->id(), handle.id());

    auto by_path_and_type = server.get_path_and_type_id_handle(path, meta::type_id<std::string>{});
    ASSERT_TRUE(by_path_and_type.has_value());
    EXPECT_EQ(by_path_and_type->id(), handle.id());

    auto path_id = server.get_path_id(path);
    ASSERT_TRUE(path_id.has_value());
    EXPECT_EQ(*path_id, handle.id());

    auto ids = server.get_path_ids(path);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids.front(), handle.id());

    auto returned_path = server.get_path(handle.id());
    ASSERT_TRUE(returned_path.has_value());
    EXPECT_EQ(*returned_path, path);

    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<LoadStateOK>(*state));
    EXPECT_EQ(std::get<LoadStateOK>(*state), LoadStateOK::Loading);
}

TEST(AssetServer, UnknownIdQueries_ReturnNotLoadedOrNullopt) {
    auto server  = make_memory_server();
    auto fake_id = UntypedAssetId(AssetId<std::string>::invalid());

    EXPECT_FALSE(server.get_load_state(fake_id).has_value());
    EXPECT_FALSE(server.get_dependency_load_state(fake_id).has_value());
    EXPECT_FALSE(server.get_recursive_dependency_load_state(fake_id).has_value());
    EXPECT_FALSE(server.get_load_states(fake_id).has_value());
    EXPECT_FALSE(server.get_id_handle_untyped(fake_id).has_value());
    EXPECT_FALSE(server.get_path(fake_id).has_value());
    EXPECT_FALSE(server.is_managed(fake_id));

    auto fallback = server.load_state(fake_id);
    ASSERT_TRUE(std::holds_alternative<LoadStateOK>(fallback));
    EXPECT_EQ(std::get<LoadStateOK>(fallback), LoadStateOK::NotLoaded);
    EXPECT_FALSE(server.is_loaded(fake_id));
    EXPECT_FALSE(server.is_loaded_with_direct_dependencies(fake_id));
    EXPECT_FALSE(server.is_loaded_with_dependencies(fake_id));
}

TEST(AssetServer, LoadUntypedWithoutLoader_ThrowsForUnknownType) {
    auto server = make_memory_server();
    EXPECT_THROW({ auto _ = server.load_untyped(AssetPath("no_loader.unknown")); }, std::runtime_error);
}

TEST(AssetServer, SamePathDifferentTypes_GetHandlesUntypedReturnsAll) {
    auto server = make_memory_server();
    Assets<std::string> text_assets;
    Assets<int> int_assets;
    server.register_assets(text_assets);
    server.register_assets(int_assets);

    auto path = AssetPath("multi.asset");
    auto h1   = server.load_erased(meta::type_id<std::string>{}, path);
    auto h2   = server.load_erased(meta::type_id<int>{}, path);

    auto all = server.get_handles_untyped(path);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_TRUE(std::ranges::any_of(all, [&](const UntypedHandle& h) { return h.id() == h1.id(); }));
    EXPECT_TRUE(std::ranges::any_of(all, [&](const UntypedHandle& h) { return h.id() == h2.id(); }));
}

TEST(AssetServer, Reload_UnknownPath_IsNoOp) {
    auto server = make_memory_server();
    Assets<std::string> assets;
    server.register_assets(assets);

    auto path   = AssetPath("not_tracked.txt");
    auto handle = server.load_erased(meta::type_id<std::string>{}, AssetPath("hello.txt"));

    // Reload on an unknown path should not affect existing tracked assets.
    server.reload(path);
    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    ASSERT_TRUE(std::holds_alternative<LoadStateOK>(*state));
    EXPECT_EQ(std::get<LoadStateOK>(*state), LoadStateOK::Loading);
}

TEST(AssetServer, ConcurrentLoadRequestsForSamePath_ReturnSameId) {
    auto server = make_memory_server();
    Assets<std::string> assets;
    server.register_assets(assets);

    auto path = AssetPath("hello.txt");

    std::vector<AssetId<std::string>> ids;
    std::mutex ids_mutex;
    std::vector<std::jthread> workers;
    workers.reserve(12);

    for (int i = 0; i < 12; ++i) {
        workers.emplace_back([&]() {
            auto h = server.load_erased(meta::type_id<std::string>{}, path);
            std::lock_guard<std::mutex> lk(ids_mutex);
            ids.push_back(h.id().typed<std::string>());
        });
    }

    workers.clear();  // join all workers before assertions

    ASSERT_FALSE(ids.empty());
    auto first = ids.front();
    for (auto& id : ids) {
        EXPECT_EQ(id, first);
    }
}

TEST(AssetServer, ConcurrentLoaderRegistrationAndQueries_RemainUsable) {
    auto server = make_memory_server();

    std::vector<std::jthread> workers;
    workers.reserve(16);

    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&]() { server.register_loader(TestTextLoader{}); });
        workers.emplace_back([&]() {
            auto by_ext = server.get_asset_loader_with_extension("txt");
            if (by_ext) {
                EXPECT_EQ(by_ext->asset_type(), meta::type_id<std::string>{});
            }
        });
    }

    workers.clear();  // join all workers before final verification

    auto final_loader = server.get_asset_loader_with_extension("txt");
    ASSERT_TRUE(final_loader != nullptr);
    EXPECT_EQ(final_loader->asset_type(), meta::type_id<std::string>{});
}

TEST(AssetServer, WatchingEnabled_WiresSourceEventReceivers) {
    auto dir      = make_memory_dir_with_text();
    auto builders = AssetSourceBuilders();
    builders.insert(AssetSourceId{}, make_memory_source_builder_with_watchers(dir, true, true));
    auto sources = std::make_shared<AssetSources>(builders.build_sources(true, true));
    AssetServer server(std::move(sources), AssetServerMode::Processed, AssetMetaCheck::Always, true,
                       UnapprovedPathMode::Forbid);

    auto source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(source.has_value());
    EXPECT_TRUE(source->get().event_receiver().has_value());
    EXPECT_TRUE(source->get().processed_event_receiver().has_value());
}

TEST(AssetServer, WatchingDisabled_DoesNotWireSourceEventReceivers) {
    auto dir      = make_memory_dir_with_text();
    auto builders = AssetSourceBuilders();
    builders.insert(AssetSourceId{}, make_memory_source_builder_with_watchers(dir, true, true));
    auto sources = std::make_shared<AssetSources>(builders.build_sources(false, false));
    AssetServer server(std::move(sources), AssetServerMode::Processed, AssetMetaCheck::Always, false,
                       UnapprovedPathMode::Forbid);

    auto source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(source.has_value());
    EXPECT_FALSE(source->get().event_receiver().has_value());
    EXPECT_FALSE(source->get().processed_event_receiver().has_value());
}

TEST(AssetServer, SourceWatcher_ReceivesAddedAndModifiedEvents) {
    auto dir      = make_memory_dir_with_text();
    auto builders = AssetSourceBuilders();
    builders.insert(AssetSourceId{}, make_memory_source_builder_with_watchers(dir, true, false));
    auto sources = std::make_shared<AssetSources>(builders.build_sources(true, false));
    AssetServer server(std::move(sources), AssetServerMode::Unprocessed, AssetMetaCheck::Always, true,
                       UnapprovedPathMode::Forbid);

    auto source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(source.has_value());
    auto receiver = source->get().event_receiver();
    ASSERT_TRUE(receiver.has_value());

    auto added_data    = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'a'});
    auto modified_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'b'});
    ASSERT_TRUE(dir.insert_file("watched.txt", memory::Value::from_shared(added_data)).has_value());
    ASSERT_TRUE(dir.insert_file("watched.txt", memory::Value::from_shared(modified_data)).has_value());

    auto events = drain_source_events(receiver->get(), 32);
    EXPECT_TRUE(has_added_asset_event_for(events, std::filesystem::path("watched.txt")));
    EXPECT_TRUE(has_modified_asset_event_for(events, std::filesystem::path("watched.txt")));
}

TEST(AssetPlugin, BuildAndFinish_RegistersAssetsAndLoader) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.register_asset<std::string>().register_loader(TestTextLoader{});
    plugin.build(app);
    plugin.finish(app);

    ASSERT_TRUE(app.get_resource<AssetServer>().has_value());
    ASSERT_TRUE(app.get_resource<Assets<std::string>>().has_value());

    auto& server = app.resource<AssetServer>();
    auto loader  = server.get_asset_loader_with_extension("txt");
    ASSERT_TRUE(loader != nullptr);
}

TEST(AssetPlugin, Build_PropagatesModeWatchingAndCustomSource) {
    App app = App::create();

    auto dir = make_memory_dir_with_text("from_custom_source");
    AssetPlugin plugin;
    plugin.mode              = AssetServerMode::Processed;
    plugin.watch_for_changes = true;
    plugin.register_asset_source(AssetSourceId(std::string("mem")), make_memory_source_builder(dir));

    plugin.build(app);

    auto& server = app.resource<AssetServer>();
    EXPECT_EQ(server.mode(), AssetServerMode::Processed);
    EXPECT_TRUE(server.watching_for_changes());
    EXPECT_TRUE(server.get_source(AssetSourceId(std::string("mem"))).has_value());
}

TEST(AssetPlugin, BuildWithWatching_WiresReceiversForCustomSourceWatchers) {
    App app = App::create();

    auto dir = make_memory_dir_with_text("from_custom_source");
    AssetPlugin plugin;
    plugin.mode              = AssetServerMode::Processed;
    plugin.watch_for_changes = true;
    plugin.register_asset_source(AssetSourceId(std::string("mem")),
                                 make_memory_source_builder_with_watchers(dir, true, true));

    plugin.build(app);

    auto& server = app.resource<AssetServer>();
    auto source  = server.get_source(AssetSourceId(std::string("mem")));
    ASSERT_TRUE(source.has_value());
    EXPECT_TRUE(source->get().event_receiver().has_value());
    EXPECT_TRUE(source->get().processed_event_receiver().has_value());

    auto event_receiver = source->get().event_receiver();
    ASSERT_TRUE(event_receiver.has_value());
    auto new_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'x'});
    ASSERT_TRUE(dir.insert_file("plugin_watch.txt", memory::Value::from_shared(new_data)).has_value());

    auto events = drain_source_events(event_receiver->get(), 16);
    EXPECT_TRUE(has_added_asset_event_for(events, std::filesystem::path("plugin_watch.txt")));
}

TEST(AssetPlugin, BuildWithoutWatching_InProcessedModeKeepsSourceReceiverButNotProcessedReceiver) {
    App app = App::create();

    auto dir = make_memory_dir_with_text("from_custom_source");
    AssetPlugin plugin;
    plugin.mode              = AssetServerMode::Processed;
    plugin.watch_for_changes = false;
    plugin.register_asset_source(AssetSourceId(std::string("mem")),
                                 make_memory_source_builder_with_watchers(dir, true, true));

    plugin.build(app);

    auto& server = app.resource<AssetServer>();
    auto source  = server.get_source(AssetSourceId(std::string("mem")));
    ASSERT_TRUE(source.has_value());
    // In Processed mode, AssetProcessor always watches source assets so it can react to source changes.
    EXPECT_TRUE(source->get().event_receiver().has_value());
    EXPECT_FALSE(source->get().processed_event_receiver().has_value());
}

TEST(AssetPlugin, FinishWithoutRegistrations_DoesNotFail) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);
    plugin.finish(app);

    EXPECT_TRUE(app.get_resource<AssetServer>().has_value());
}

TEST(AssetAppFunctions, AppRegisterApis_DispatchToExistingServer) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.build(app);

    app_register_asset<std::string>(app);
    app_preregister_loader<TestTextLoader>(app, TestTextLoader::extensions());
    app_register_loader<TestTextLoader>(app);
    app_register_asset_processor<TestProcess>(app, TestProcess{});
    app_set_default_asset_processor<TestProcess>(app, "txt");

    ASSERT_TRUE(app.get_resource<Assets<std::string>>().has_value());
    ASSERT_TRUE(app.get_resource<AssetProcessor>().has_value());

    auto& server = app.resource<AssetServer>();
    auto loader  = server.get_asset_loader_with_extension("txt");
    ASSERT_TRUE(loader != nullptr);

    auto processor = app.resource<AssetProcessor>().get_default_processor("txt");
    ASSERT_TRUE(processor != nullptr);
}

TEST(AssetAppFunctions, RegisterAsset_IsIdempotent) {
    App app = App::create();
    AssetPlugin plugin;
    plugin.build(app);

    app_register_asset<std::string>(app);
    app_register_asset<std::string>(app);

    EXPECT_TRUE(app.get_resource<Assets<std::string>>().has_value());
}

TEST(AssetAppFunctions, ProcessorHelpers_ThrowWithoutProcessor) {
    App app = App::create();

    // Without an AssetProcessor resource, the helpers should throw.
    EXPECT_THROW(app_register_asset_processor<TestProcess>(app, TestProcess{}), std::runtime_error);
    EXPECT_THROW(app_set_default_asset_processor<TestProcess>(app, "txt"), std::runtime_error);
}

// -------------------------------------------------------------------------------------
// Helpers for end-to-end hot-reload / asset-lifecycle tests
// -------------------------------------------------------------------------------------
namespace {

/// Build an App with AssetPlugin wired to a memory directory.
/// Returns {app, dir} so tests can mutate the directory.
struct PluginTestEnv {
    App app;
    memory::Directory dir;
};

PluginTestEnv make_plugin_env(bool watching, std::string_view initial_content = "hello") {
    auto dir = make_memory_dir_with_text(initial_content);

    App app = App::create();
    AssetPlugin plugin;
    plugin.watch_for_changes = watching;
    plugin.register_asset<std::string>().register_loader(TestTextLoader{});
    if (watching) {
        plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder_with_watchers(dir));
    } else {
        plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    }
    plugin.build(app);
    plugin.finish(app);

    return {std::move(app), dir};
}

/// Wait for all IOTaskPool tasks and then run the Last schedule to process internal events.
void flush_load_tasks(App& app) {
    utils::IOTaskPool::instance().wait();
    app.run_schedule(Last);
}

}  // namespace

// -------------------------------------------------------------------------------------
// HotReload test suite – end-to-end with AssetPlugin
// -------------------------------------------------------------------------------------

TEST(HotReload, InitialLoad_ReachesAssetsStorage) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));

    flush_load_tasks(app);

    // Asset content must be in storage.
    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");

    // Server must report Loaded.
    EXPECT_TRUE(server.is_loaded(handle.id()));
}

TEST(HotReload, Reload_UpdatesStoredAssetContent) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Verify initial content.
    {
        auto& assets = app.resource<Assets<std::string>>();
        auto value   = assets.get(handle.id());
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->get(), "hello");
    }

    // Overwrite the file with new content.
    auto new_bytes =
        std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'w', 'o', 'r', 'l', 'd'});
    ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(new_bytes)).has_value());

    // Trigger reload and process.
    server.reload(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Storage must contain the new content.
    {
        auto& assets = app.resource<Assets<std::string>>();
        auto value   = assets.get(handle.id());
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->get(), "world");
    }
    EXPECT_TRUE(server.is_loaded(handle.id()));
}

TEST(HotReload, Reload_SetsStateToLoadingThenBackToLoaded) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // After reload() call, state must transition to Loading immediately.
    server.reload(AssetPath("hello.txt"));
    {
        auto state = server.get_load_state(handle.id());
        ASSERT_TRUE(state.has_value());
        ASSERT_TRUE(std::holds_alternative<LoadStateOK>(*state));
        EXPECT_EQ(std::get<LoadStateOK>(*state), LoadStateOK::Loading);
    }

    flush_load_tasks(app);

    // After processing, state must be Loaded again.
    EXPECT_TRUE(server.is_loaded(handle.id()));
}

TEST(HotReload, Reload_NonExistentPath_DoesNotAffectExistingAssets) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // Reload a path that was never loaded – must not crash or affect the real asset.
    server.reload(AssetPath("nonexistent.txt"));
    flush_load_tasks(app);

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
    EXPECT_TRUE(server.is_loaded(handle.id()));
}

TEST(HotReload, MultipleReloads_StorageReflectsLatestContent) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    const std::array<std::string, 3> revisions = {"rev1", "rev2", "rev3"};
    for (auto& rev : revisions) {
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(rev.begin(), rev.end());
        ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(bytes)).has_value());
        server.reload(AssetPath("hello.txt"));
        flush_load_tasks(app);

        auto& assets = app.resource<Assets<std::string>>();
        auto value   = assets.get(handle.id());
        ASSERT_TRUE(value.has_value()) << "revision " << rev;
        EXPECT_EQ(value->get(), rev) << "revision " << rev;
    }
}

TEST(HotReload, InitialLoad_GeneratesAddedAndModifiedAssetEvents) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Read events directly from the Events<AssetEvent<std::string>> resource.
    auto& events        = app.resource<Events<AssetEvent<std::string>>>();
    bool found_added    = false;
    bool found_modified = false;
    for (std::uint32_t i = events.head(); i < events.tail(); ++i) {
        auto* e = events.get(i);
        if (!e) continue;
        if (e->is_added(handle.id())) found_added = true;
        if (e->is_modified(handle.id())) found_modified = true;
    }
    EXPECT_TRUE(found_added) << "Expected an Added AssetEvent for the initial load";
    EXPECT_TRUE(found_modified) << "Expected a Modified AssetEvent for the initial load";
}

TEST(HotReload, Reload_GeneratesModifiedAssetEvent) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Consume initial events by running an event update cycle.
    app.run_schedule(Last);

    // Overwrite and reload.
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'n', 'e', 'w'});
    ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(bytes)).has_value());
    server.reload(AssetPath("hello.txt"));
    flush_load_tasks(app);

    auto& events        = app.resource<Events<AssetEvent<std::string>>>();
    bool found_modified = false;
    for (std::uint32_t i = events.head(); i < events.tail(); ++i) {
        auto* e = events.get(i);
        if (e && e->is_modified(handle.id())) found_modified = true;
    }
    EXPECT_TRUE(found_modified) << "Expected a Modified AssetEvent after reload";
}

TEST(HotReload, WatcherProducesSourceEvent_ManualReloadUpdatesStorage) {
    auto [app, dir] = make_plugin_env(/*watching=*/true);
    auto& server    = app.resource<AssetServer>();

    // Initial load.
    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // Modify the file – the watcher should emit AssetSourceEvent::ModifiedAsset.
    auto mod_bytes = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'m', 'o', 'd'});
    ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(mod_bytes)).has_value());

    // Verify source event was generated.
    auto source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(source.has_value());
    auto receiver = source->get().event_receiver();
    ASSERT_TRUE(receiver.has_value());
    auto source_events_list = drain_source_events(receiver->get(), 32);
    EXPECT_TRUE(has_modified_asset_event_for(source_events_list, std::filesystem::path("hello.txt")));

    // Manually call reload (simulating what a watch-and-reload system would do).
    server.reload(AssetPath("hello.txt"));
    flush_load_tasks(app);

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "mod");
}

TEST(HotReload, AddNewFile_WatcherEmitsAddedEvent) {
    auto [app, dir] = make_plugin_env(/*watching=*/true);
    auto& server    = app.resource<AssetServer>();

    auto source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(source.has_value());
    auto receiver = source->get().event_receiver();
    ASSERT_TRUE(receiver.has_value());

    // Insert a brand-new file.
    auto new_bytes = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'n', 'e', 'w'});
    ASSERT_TRUE(dir.insert_file("brand_new.txt", memory::Value::from_shared(new_bytes)).has_value());

    auto source_events_list = drain_source_events(receiver->get(), 32);
    EXPECT_TRUE(has_added_asset_event_for(source_events_list, std::filesystem::path("brand_new.txt")));
}

TEST(HotReload, LoadMissingFile_FailsGracefully) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    // Load a file that doesn't exist in the memory directory.
    auto handle = server.load<std::string>(AssetPath("missing.txt"));
    flush_load_tasks(app);

    // The asset must not be in the Loaded state.
    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<AssetLoadError>(*state)) << "Expected a load error for a missing file";

    // Assets<T> must not contain the value.
    auto& assets = app.resource<Assets<std::string>>();
    EXPECT_FALSE(assets.get(handle.id()).has_value());
}

TEST(HotReload, DuplicateLoad_ReturnsSameHandle) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto h1 = server.load<std::string>(AssetPath("hello.txt"));
    auto h2 = server.load<std::string>(AssetPath("hello.txt"));

    EXPECT_EQ(h1.id(), h2.id());
}

TEST(HotReload, LoadOverride_ForcesReloadEvenIfAlreadyLoaded) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto h1 = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(h1.id()));
    int after_first = TestTextLoader::load_count.load();

    // load_override should force another load even though already loaded.
    auto h_override = server.load_override<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    int after_override = TestTextLoader::load_count.load();
    EXPECT_GT(after_override, after_first) << "load_override should trigger an additional load";
    EXPECT_TRUE(server.is_loaded(h_override.id()));
}

TEST(HotReload, ConcurrentReloads_DoNotCorruptState) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // Fire many reloads concurrently.
    {
        std::vector<std::jthread> workers;
        workers.reserve(8);
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&]() { server.reload(AssetPath("hello.txt")); });
        }
    }  // join all workers

    flush_load_tasks(app);

    // State must converge to Loaded (no corruption).
    EXPECT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<std::string>>();
    EXPECT_TRUE(assets.get(handle.id()).has_value());
}

TEST(HotReload, Reload_LoaderInvokedWithUpdatedBytes) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    int first_count = TestTextLoader::load_count.load();

    // Modify and reload.
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'x'});
    ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(bytes)).has_value());
    server.reload(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Loader must have been invoked again.
    EXPECT_GT(TestTextLoader::load_count.load(), first_count);
}

TEST(HotReload, WatchingEnabled_TracksDependencyState) {
    auto [app, dir] = make_plugin_env(/*watching=*/true);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(handle.id()));

    // With watching enabled and no sub-dependencies, all three states must be Loaded.
    auto states = server.get_load_states(handle.id());
    ASSERT_TRUE(states.has_value());
    auto [self_state, dep_state, rec_dep_state] = *states;
    EXPECT_EQ(std::get<LoadStateOK>(self_state), LoadStateOK::Loaded);
    EXPECT_EQ(std::get<LoadStateOK>(dep_state), LoadStateOK::Loaded);
    EXPECT_EQ(std::get<LoadStateOK>(rec_dep_state), LoadStateOK::Loaded);
}

TEST(HotReload, ProcessedMode_LoadsFromProcessedReader) {
    auto dir = make_memory_dir_with_text("processed_content");

    App app = App::create();
    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.register_asset<std::string>().register_loader(TestTextLoader{});
    // Supply the same dir for both unprocessed and processed readers.
    auto builder = make_memory_source_builder(dir, /*with_processed_reader=*/true);
    plugin.register_asset_source(AssetSourceId{}, std::move(builder));
    plugin.build(app);
    plugin.finish(app);

    auto& server = app.resource<AssetServer>();
    EXPECT_EQ(server.mode(), AssetServerMode::Processed);

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "processed_content");
}

// -------------------------------------------------------------------------------------
// Bevy lib.rs integrated tests — load failure scenarios
// Ported from bevy_asset::tests::load_failure
// -------------------------------------------------------------------------------------

// A loader that always fails with a parse error.
struct FailingLoader {
    using Asset = std::string;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"fail"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static std::expected<std::string, Error> load(std::istream&, const Settings&, assets::LoadContext&) {
        return std::unexpected(std::make_exception_ptr(std::runtime_error("simulated parse error")));
    }
};

// Ported from bevy_asset::tests::load_failure — "root asset has no loader"
// Tests that loading a file with no registered loader produces a MissingAssetLoader error.
TEST(LoadFailure, MissingLoader_FailsWithMissingAssetLoaderError) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    // ".unknown" has no registered loader
    auto unknown_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'x'});
    ASSERT_TRUE(dir.insert_file("test.unknown", memory::Value::from_shared(unknown_data)).has_value());

    // server.load<T> checks loader existence eagerly in some paths, or fails async.
    // Register assets for the type so the server can track it, then try loading.
    Assets<std::string> assets;
    server.register_assets(assets);
    auto handle = server.load_erased(meta::type_id<std::string>{}, AssetPath("test.unknown"));

    flush_load_tasks(app);

    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<AssetLoadError>(*state))
        << "Expected a load error for a file with no registered loader";

    if (std::holds_alternative<AssetLoadError>(*state)) {
        auto& error = std::get<AssetLoadError>(*state);
        EXPECT_TRUE(std::holds_alternative<load_error::MissingAssetLoader>(error))
            << "Expected MissingAssetLoader error variant";
    }
}

// Ported from bevy_asset::tests::load_failure — "malformed root asset"
// Tests that a loader returning an error produces AssetLoaderException.
TEST(LoadFailure, LoaderError_FailsWithAssetLoaderException) {
    auto dir = make_memory_dir_with_text("malformed content");

    App app = App::create();
    AssetPlugin plugin;
    plugin.register_asset<std::string>().register_loader(FailingLoader{});
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    plugin.finish(app);

    auto& server = app.resource<AssetServer>();
    // The FailingLoader handles ".fail" extension; put a file with that extension.
    auto fail_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'b', 'a', 'd'});
    ASSERT_TRUE(dir.insert_file("malformed.fail", memory::Value::from_shared(fail_data)).has_value());

    auto handle = server.load<std::string>(AssetPath("malformed.fail"));
    flush_load_tasks(app);

    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<AssetLoadError>(*state)) << "Expected a load error for malformed content";

    if (std::holds_alternative<AssetLoadError>(*state)) {
        auto& error = std::get<AssetLoadError>(*state);
        EXPECT_TRUE(std::holds_alternative<load_error::AssetLoaderException>(error))
            << "Expected AssetLoaderException error variant";
    }
}

// Ported from bevy_asset::tests::load_failure — combined scenario
// Verifies that a successful load produces Loaded, while missing file and loader error produce distinct failures.
TEST(LoadFailure, MixedScenarios_CorrectStateForEach) {
    auto dir = make_memory_dir_with_text("good content");

    App app = App::create();
    AssetPlugin plugin;
    plugin.register_asset<std::string>().register_loader(TestTextLoader{}).register_loader(FailingLoader{});
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    plugin.finish(app);

    auto& server = app.resource<AssetServer>();

    auto fail_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'!'});
    ASSERT_TRUE(dir.insert_file("bad.fail", memory::Value::from_shared(fail_data)).has_value());

    // 1. Successful load
    auto h_ok = server.load<std::string>(AssetPath("hello.txt"));
    // 2. Missing file
    auto h_missing = server.load<std::string>(AssetPath("does_not_exist.txt"));
    // 3. Loader error (FailingLoader always fails)
    auto h_error = server.load<std::string>(AssetPath("bad.fail"));

    flush_load_tasks(app);

    // 1. Successful
    EXPECT_TRUE(server.is_loaded(h_ok.id())) << "Existing file with valid loader should be loaded";
    auto& assets = app.resource<Assets<std::string>>();
    auto val     = assets.get(h_ok.id());
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), "good content");

    // 2. Missing file
    {
        auto state = server.get_load_state(h_missing.id());
        ASSERT_TRUE(state.has_value());
        EXPECT_TRUE(std::holds_alternative<AssetLoadError>(*state)) << "Missing file should produce an error state";
    }

    // 3. Loader error
    {
        auto state = server.get_load_state(h_error.id());
        ASSERT_TRUE(state.has_value());
        EXPECT_TRUE(std::holds_alternative<AssetLoadError>(*state)) << "Failing loader should produce an error state";
        if (std::holds_alternative<AssetLoadError>(*state)) {
            EXPECT_TRUE(std::holds_alternative<load_error::AssetLoaderException>(std::get<AssetLoadError>(*state)));
        }
    }
}

// -------------------------------------------------------------------------------------
// Bevy lib.rs integrated tests — asset lifecycle
// Ported from bevy_asset::tests::keep_gotten_strong_handles
// -------------------------------------------------------------------------------------

// Tests that get_strong_handle keeps the asset alive after the original handle is dropped.
TEST(AssetLifecycle, GetStrongHandle_KeepsAssetAlive) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& assets    = app.resource_mut<Assets<std::string>>();

    // Add an asset manually.
    auto handle = assets.add(std::string("hello"));
    auto id     = handle.id();

    // Get a second strong handle from the id.
    auto strong = assets.get_strong_handle(id);
    ASSERT_TRUE(strong.has_value());

    // Drop the original handle — asset should still be reachable.
    handle = id;  // convert to weak handle, releasing the strong reference

    // Process handle destruction events.
    assets.handle_events_manual();

    auto val = assets.get(id);
    EXPECT_TRUE(val.has_value()) << "Asset should still exist because a strong handle remains";
    EXPECT_EQ(val->get(), "hello");
}

// Tests that dropping ALL strong handles causes the asset to be released.
TEST(AssetLifecycle, AllHandlesDropped_AssetRemoved) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& assets    = app.resource_mut<Assets<std::string>>();

    auto id = [&]() {
        auto handle = assets.add(std::string("temporary"));
        auto id     = handle.id();

        auto val = assets.get(id);
        EXPECT_TRUE(val.has_value());
        return id;
    }();
    // handle is dropped here

    // Process handle destruction events.
    assets.handle_events_manual();

    auto val = assets.get(id);
    EXPECT_FALSE(val.has_value()) << "Asset should be removed after all handles are dropped";
}

// -------------------------------------------------------------------------------------
// Bevy lib.rs integrated tests — manual asset management
// Ported from bevy_asset::tests::manual_asset_management
// -------------------------------------------------------------------------------------

// Tests manually adding an asset (not via loader) produces Added event,
// and dropping the handle produces Unused/Removed events.
TEST(ManualAssetManagement, AddAndDrop_GeneratesCorrectEvents) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& assets    = app.resource_mut<Assets<std::string>>();

    auto handle = assets.add(std::string("manual"));
    auto id     = handle.id();

    // Flush events to the event writer.
    app.run_schedule(Last);

    {
        auto& events     = app.resource<Events<AssetEvent<std::string>>>();
        bool found_added = false;
        for (std::uint32_t i = events.head(); i < events.tail(); ++i) {
            auto* e = events.get(i);
            if (e && e->is_added(id)) found_added = true;
        }
        EXPECT_TRUE(found_added) << "Expected an Added AssetEvent after manual add";
    }

    // Drop the handle by converting to weak.
    handle = id;

    // Process handle destruction + flush events.
    assets.handle_events_manual();
    app.run_schedule(Last);

    {
        auto& events       = app.resource<Events<AssetEvent<std::string>>>();
        bool found_unused  = false;
        bool found_removed = false;
        for (std::uint32_t i = events.head(); i < events.tail(); ++i) {
            auto* e = events.get(i);
            if (!e) continue;
            if (e->is_unused(id)) found_unused = true;
            if (e->is_removed(id)) found_removed = true;
        }
        EXPECT_TRUE(found_unused) << "Expected an Unused AssetEvent after dropping the handle";
        EXPECT_TRUE(found_removed) << "Expected a Removed AssetEvent after dropping the handle";
    }
}

// -------------------------------------------------------------------------------------
// Bevy lib.rs integrated tests — failure_load_states
// Ported from bevy_asset::tests::failure_load_states (simplified — no dep chain)
// -------------------------------------------------------------------------------------

// Tests that consecutive loads of the same path return the same handle (Bevy guarantee).
// Enhanced version of existing DuplicateLoad_ReturnsSameHandle with additional state checks.
TEST(LoadFailure, ConsecutiveLoads_ReturnSameHandleAndSingleLoadTask) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();

    auto h1 = server.load<std::string>(AssetPath("hello.txt"));
    auto h2 = server.load<std::string>(AssetPath("hello.txt"));

    EXPECT_EQ(h1.id(), h2.id()) << "Consecutive loads of same path should return same handle id";

    flush_load_tasks(app);

    // Only one load should have occurred despite two load() calls.
    EXPECT_EQ(TestTextLoader::load_count.load(), 1)
        << "Only one loader invocation should occur for duplicate load requests";

    EXPECT_TRUE(server.is_loaded(h1.id()));
    EXPECT_TRUE(server.is_loaded(h2.id()));
}

// -------------------------------------------------------------------------------------
// Bevy lib.rs integrated tests — failure_load_states
// Ported from bevy_asset::tests::failure_load_states
// Tests load state transitions: loaded, dep_loaded, rec_dep_loaded for a single-asset load.
// -------------------------------------------------------------------------------------

TEST(LoadStates, SingleAsset_AllStatesReachLoaded) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    ASSERT_TRUE(server.is_loaded(handle.id()));

    auto states = server.get_load_states(handle.id());
    ASSERT_TRUE(states.has_value());

    auto [self_state, dep_state, rec_dep_state] = *states;
    EXPECT_TRUE(std::holds_alternative<LoadStateOK>(self_state));
    EXPECT_EQ(std::get<LoadStateOK>(self_state), LoadStateOK::Loaded);

    EXPECT_TRUE(std::holds_alternative<LoadStateOK>(dep_state));
    EXPECT_EQ(std::get<LoadStateOK>(dep_state), LoadStateOK::Loaded);

    EXPECT_TRUE(std::holds_alternative<LoadStateOK>(rec_dep_state));
    EXPECT_EQ(std::get<LoadStateOK>(rec_dep_state), LoadStateOK::Loaded);

    // Convenience helpers should agree.
    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_TRUE(server.is_loaded_with_direct_dependencies(handle.id()));
    EXPECT_TRUE(server.is_loaded_with_dependencies(handle.id()));
}

// Tests that a failed load sets all three states to Failed.
TEST(LoadStates, FailedAsset_AllStatesReflectFailure) {
    auto dir = make_memory_dir_with_text("ignored");

    App app = App::create();
    AssetPlugin plugin;
    plugin.register_asset<std::string>().register_loader(FailingLoader{});
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    plugin.finish(app);

    auto& server   = app.resource<AssetServer>();
    auto fail_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{'!'});
    ASSERT_TRUE(dir.insert_file("bad.fail", memory::Value::from_shared(fail_data)).has_value());

    auto handle = server.load<std::string>(AssetPath("bad.fail"));
    flush_load_tasks(app);

    auto state = server.load_state(handle.id());
    EXPECT_TRUE(std::holds_alternative<AssetLoadError>(state));

    EXPECT_FALSE(server.is_loaded(handle.id()));
    EXPECT_FALSE(server.is_loaded_with_direct_dependencies(handle.id()));
    EXPECT_FALSE(server.is_loaded_with_dependencies(handle.id()));
}
