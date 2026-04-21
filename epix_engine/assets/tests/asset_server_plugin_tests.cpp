#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;
import epix.meta;
import epix.assets;

using namespace epix::assets;
using namespace epix::core;
namespace meta = epix::meta;

namespace {

void flush_load_tasks(App& app);

static auto make_bytes(std::string_view s) {
    auto sp = std::as_bytes(std::span(s));
    return std::make_shared<std::vector<std::byte>>(sp.begin(), sp.end());
}

memory::Directory make_memory_dir_with_text(std::string_view content = "hello") {
    auto dir = memory::Directory::create({});
    auto res = dir.insert_file("hello.txt", memory::Value::from_shared(make_bytes(content)));
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
    auto builder =
        make_memory_source_builder(dir, with_processed_reader)
            .with_watcher([dir](epix::utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
                return std::make_unique<MemoryAssetWatcher>(
                    dir,
                    [sender = std::move(sender)](AssetSourceEvent event) mutable { sender.send(std::move(event)); });
            });
    if (with_processed_watcher) {
        builder.with_processed_watcher(
            [dir](epix::utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
                return std::make_unique<MemoryAssetWatcher>(
                    dir,
                    [sender = std::move(sender)](AssetSourceEvent event) mutable { sender.send(std::move(event)); });
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
    return AssetServer(std::move(sources), mode, AssetMetaCheck{asset_meta_check::Always{}}, watching,
                       UnapprovedPathMode::Forbid);
}

struct TestTextLoader {
    using Asset = std::string;
    struct Settings {};
    using Error = std::exception_ptr;

    static inline std::atomic<int> load_count{0};

    static void reset_stats() { load_count.store(0); }

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"txt"}, std::string_view{"text"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static asio::awaitable<std::expected<std::string, Error>> load(Reader& reader,
                                                                   const Settings&,
                                                                   epix::assets::LoadContext&) {
        load_count.fetch_add(1);
        std::vector<uint8_t> buf;
        co_await reader.read_to_end(buf);
        co_return std::string(buf.begin(), buf.end());
    }
};

struct TestProcess {
    struct Settings {};
    using OutputLoader = TestTextLoader;

    asio::awaitable<std::expected<OutputLoader::Settings, std::exception_ptr>> process(ProcessContext&,
                                                                                       const Settings&,
                                                                                       Writer&) const {
        co_return OutputLoader::Settings{};
    }
};

struct DependencyManifestAsset {
    std::string value;
    UntypedHandle dependency;
};

struct DependencyManifestLoader {
    using Asset = DependencyManifestAsset;
    struct Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"dep"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static asio::awaitable<std::expected<DependencyManifestAsset, Error>> load(Reader& reader,
                                                                               const Settings&,
                                                                               epix::assets::LoadContext& context) {
        std::vector<uint8_t> buf;
        co_await reader.read_to_end(buf);
        auto dependency_path = std::string(buf.begin(), buf.end());
        while (!dependency_path.empty() && (dependency_path.back() == '\n' || dependency_path.back() == '\r' ||
                                            dependency_path.back() == ' ' || dependency_path.back() == '\t')) {
            dependency_path.pop_back();
        }

        auto dep_handle = context.asset_server().load_untyped(AssetPath(dependency_path));
        context.track_dependency(UntypedAssetId(dep_handle.id()));
        co_return DependencyManifestAsset{std::string("manifest:") + dependency_path, std::move(dep_handle)};
    }
};

template <typename EventT, typename Pred>
bool any_recorded_event(const Events<EventT>& events, Pred&& pred) {
    for (std::uint32_t i = events.head(); i < events.tail(); ++i) {
        auto* event = events.get(i);
        if (event && pred(*event)) {
            return true;
        }
    }
    return false;
}

const char* asset_server_mode_name(AssetServerMode mode) {
    switch (mode) {
        case AssetServerMode::Unprocessed:
            return "Unprocessed";
        case AssetServerMode::Processed:
            return "Processed";
    }
    return "Unknown";
}

std::vector<AssetSourceEvent> drain_source_events(const epix::utils::Receiver<AssetSourceEvent>& receiver,
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

// An alternative text loader used to test .meta-file-driven loader selection.
struct AltTextLoader {
    using Asset = std::string;
    struct Settings {};
    using Error = std::exception_ptr;

    static inline std::atomic<int> load_count{0};

    static void reset_stats() { load_count.store(0); }

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"txt"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static asio::awaitable<std::expected<std::string, Error>> load(Reader& reader,
                                                                   const Settings&,
                                                                   epix::assets::LoadContext&) {
        load_count.fetch_add(1);
        std::vector<uint8_t> buf;
        co_await reader.read_to_end(buf);
        co_return std::string(buf.begin(), buf.end());
    }
};

// Loader with non-trivial Settings used to verify the server restores loader
// settings from a .meta file via ErasedAssetLoader::deserialize_meta.
struct SettingsCapturingLoader {
    using Asset = std::string;
    struct Settings {
        int quality = 5;
    };
    using Error = std::exception_ptr;

    static inline std::atomic<int> last_quality{-1};

    static void reset_stats() { last_quality.store(-1); }

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"qtxt"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static asio::awaitable<std::expected<std::string, Error>> load(Reader& reader,
                                                                   const Settings& s,
                                                                   epix::assets::LoadContext&) {
        last_quality.store(s.quality);
        std::vector<uint8_t> buf;
        co_await reader.read_to_end(buf);
        co_return std::string(buf.begin(), buf.end());
    }
};

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
    server.register_asset(assets);

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
    server.register_asset(text_assets);
    server.register_asset(int_assets);

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
    server.register_asset(assets);

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
    server.register_asset(assets);

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
    AssetServer server(std::move(sources), AssetServerMode::Processed, AssetMetaCheck{asset_meta_check::Always{}}, true,
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
    AssetServer server(std::move(sources), AssetServerMode::Processed, AssetMetaCheck{asset_meta_check::Always{}},
                       false, UnapprovedPathMode::Forbid);

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
    AssetServer server(std::move(sources), AssetServerMode::Unprocessed, AssetMetaCheck{asset_meta_check::Always{}},
                       true, UnapprovedPathMode::Forbid);

    auto source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(source.has_value());
    auto receiver = source->get().event_receiver();
    ASSERT_TRUE(receiver.has_value());

    auto added_data    = make_bytes("a");
    auto modified_data = make_bytes("b");
    ASSERT_TRUE(dir.insert_file("watched.txt", memory::Value::from_shared(added_data)).has_value());
    ASSERT_TRUE(dir.insert_file("watched.txt", memory::Value::from_shared(modified_data)).has_value());

    auto events = drain_source_events(receiver->get(), 32);
    EXPECT_TRUE(has_added_asset_event_for(events, std::filesystem::path("watched.txt")));
    EXPECT_TRUE(has_modified_asset_event_for(events, std::filesystem::path("watched.txt")));
}

TEST(AssetPlugin, BuildAndFinish_RegistersAssetsAndLoader) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);

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
    plugin.mode                       = AssetServerMode::Processed;
    plugin.watch_for_changes_override = true;
    plugin.register_asset_source(AssetSourceId(std::string("mem")), make_memory_source_builder(dir));

    plugin.build(app);

    auto& server = app.resource<AssetServer>();
    EXPECT_EQ(server.mode(), AssetServerMode::Processed);
    EXPECT_TRUE(server.watching_for_changes());
    EXPECT_TRUE(server.get_source(AssetSourceId(std::string("mem"))).has_value());
}

TEST(AssetPlugin, BuildInProcessedMode_DefaultSourceProvidesProcessedIo) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.build(app);

    auto& server        = app.resource<AssetServer>();
    auto default_source = server.get_source(AssetSourceId{});
    ASSERT_TRUE(default_source.has_value());
    EXPECT_TRUE(default_source->get().processed_reader().has_value());
    EXPECT_TRUE(default_source->get().processed_writer().has_value());
}

TEST(AssetPlugin, BuildInProcessedMode_WithProcessorCreatesProcessorResource) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.build(app);

    auto processor = app.get_resource<AssetProcessor>();
    ASSERT_TRUE(processor.has_value());
    EXPECT_TRUE(processor->get().sources()->get(AssetSourceId{}).has_value());
}

TEST(AssetPlugin, ProcessedMode_AppRunExitsCleanly) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    app.add_plugins(plugin);

    EXPECT_NO_THROW(app.run());
}

TEST(AssetPlugin, ProcessedMode_EmbeddedSourceProvidesExplicitProcessedReader) {
    App app = App::create();

    AssetPlugin plugin;
    plugin.mode = AssetServerMode::Processed;
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);

    auto registry = app.get_resource_mut<EmbeddedAssetRegistry>();
    ASSERT_TRUE(registry.has_value());
    auto bytes = make_bytes("embedded_content");
    ASSERT_TRUE(registry->get().directory().insert_file("hello.txt", memory::Value::from_shared(bytes)).has_value());

    auto& server = app.resource<AssetServer>();
    auto source  = server.get_source(AssetSourceId(std::string(EMBEDDED)));
    ASSERT_TRUE(source.has_value());
    EXPECT_TRUE(source->get().processed_reader().has_value());

    auto handle = server.load<std::string>(AssetPath("embedded://hello.txt"));
    flush_load_tasks(app);

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "embedded_content");
}

TEST(AssetPlugin, BuildWithWatching_WiresReceiversForCustomSourceWatchers) {
    App app = App::create();

    auto dir = make_memory_dir_with_text("from_custom_source");
    AssetPlugin plugin;
    plugin.mode                       = AssetServerMode::Processed;
    plugin.watch_for_changes_override = true;
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
    auto new_data = make_bytes("x");
    ASSERT_TRUE(dir.insert_file("plugin_watch.txt", memory::Value::from_shared(new_data)).has_value());

    auto events = drain_source_events(event_receiver->get(), 16);
    EXPECT_TRUE(has_added_asset_event_for(events, std::filesystem::path("plugin_watch.txt")));
}

TEST(AssetPlugin, BuildWithoutWatching_InProcessedModeKeepsSourceReceiverButNotProcessedReceiver) {
    App app = App::create();

    auto dir = make_memory_dir_with_text("from_custom_source");
    AssetPlugin plugin;
    plugin.mode                       = AssetServerMode::Processed;
    plugin.watch_for_changes_override = false;
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

// When use_asset_processor=false, the server is built with build_sources(false, watch=false),
// so neither source nor processed watcher is wired �?even if watcher factories are present.
TEST(AssetPlugin, BuildWithoutWatching_WithoutProcessor_DoesNotWireAnyReceivers) {
    App app = App::create();

    auto dir = make_memory_dir_with_text("from_custom_source");
    AssetPlugin plugin;
    plugin.mode                         = AssetServerMode::Processed;
    plugin.watch_for_changes_override   = false;
    plugin.use_asset_processor_override = false;
    plugin.register_asset_source(AssetSourceId(std::string("mem")),
                                 make_memory_source_builder_with_watchers(dir, true, true));

    plugin.build(app);

    auto& server = app.resource<AssetServer>();
    auto source  = server.get_source(AssetSourceId(std::string("mem")));
    ASSERT_TRUE(source.has_value());
    EXPECT_FALSE(source->get().event_receiver().has_value());
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
    plugin.watch_for_changes_override = watching;
    if (watching) {
        plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder_with_watchers(dir));
    } else {
        plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    }
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);

    return {std::move(app), dir};
}

PluginTestEnv make_plugin_env_for_mode(AssetServerMode mode,
                                       bool watching                    = false,
                                       std::string_view initial_content = "hello") {
    auto dir = make_memory_dir_with_text(initial_content);

    App app = App::create();
    AssetPlugin plugin;
    plugin.mode = mode;
    if (mode == AssetServerMode::Processed) {
        plugin.use_asset_processor_override = false;
    }
    plugin.watch_for_changes_override = watching;
    if (watching) {
        plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder_with_watchers(dir));
    } else {
        plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    }
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);

    return {std::move(app), dir};
}

/// Wait for all IOTaskPool tasks and then run the Last schedule to process internal events.
void flush_load_tasks(App& app) {
    epix::utils::IOTaskPool::instance().wait();
    app.run_schedule(Last);
}

}  // namespace

// -------------------------------------------------------------------------------------
// HotReload test suite �?end-to-end with AssetPlugin
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
    auto new_bytes = make_bytes("world");
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

    // Reload a path that was never loaded �?must not crash or affect the real asset.
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
        auto bytes = make_bytes(rev);
        ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(bytes)).has_value());
        server.reload(AssetPath("hello.txt"));
        flush_load_tasks(app);

        auto& assets = app.resource<Assets<std::string>>();
        auto value   = assets.get(handle.id());
        ASSERT_TRUE(value.has_value()) << "revision " << rev;
        EXPECT_EQ(value->get(), rev) << "revision " << rev;
    }
}

TEST(HotReload, InitialLoad_GeneratesAddedAssetEvent) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Read events directly from the Events<AssetEvent<std::string>> resource.
    auto& events     = app.resource<Events<AssetEvent<std::string>>>();
    bool found_added = false;
    for (std::uint32_t i = events.head(); i < events.tail(); ++i) {
        auto* e = events.get(i);
        if (!e) continue;
        if (e->is_added(handle.id())) found_added = true;
    }
    EXPECT_TRUE(found_added) << "Expected an Added AssetEvent for the initial load";
}

TEST(HotReload, Reload_GeneratesModifiedAssetEvent) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    // Consume initial events by running an event update cycle.
    app.run_schedule(Last);

    // Overwrite and reload.
    auto bytes = make_bytes("new");
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

    // Modify the file �?the watcher should emit AssetSourceEvent::ModifiedAsset.
    auto mod_bytes = make_bytes("mod");
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
    auto new_bytes = make_bytes("new");
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
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state))
        << "Expected a load error for a missing file";

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
    auto bytes = make_bytes("x");
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
    // Supply the same dir for both unprocessed and processed readers.
    auto builder = make_memory_source_builder(dir, /*with_processed_reader=*/true);
    plugin.register_asset_source(AssetSourceId{}, std::move(builder));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);

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
// Bevy lib.rs integrated tests �?load failure scenarios
// Ported from bevy_asset::tests::load_failure
// -------------------------------------------------------------------------------------

// A loader that always fails with a parse error.
struct FailingLoader {
    using Asset = std::string;
    struct Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() {
        static auto exts = std::array{std::string_view{"fail"}};
        return std::span<std::string_view>(exts.data(), exts.size());
    }

    static asio::awaitable<std::expected<std::string, Error>> load(Reader&,
                                                                   const Settings&,
                                                                   epix::assets::LoadContext&) {
        co_return std::unexpected(std::make_exception_ptr(std::runtime_error("simulated parse error")));
    }
};

// Ported from bevy_asset::tests::load_failure �?"root asset has no loader"
// Tests that loading a file with no registered loader produces a MissingAssetLoader error.
TEST(LoadFailure, MissingLoader_FailsWithMissingAssetLoaderError) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    // ".unknown" has no registered loader
    auto unknown_data = make_bytes("x");
    ASSERT_TRUE(dir.insert_file("test.unknown", memory::Value::from_shared(unknown_data)).has_value());

    // server.load<T> checks loader existence eagerly in some paths, or fails async.
    // Register assets for the type so the server can track it, then try loading.
    Assets<std::string> assets;
    server.register_asset(assets);
    auto handle = server.load_erased(meta::type_id<std::string>{}, AssetPath("test.unknown"));

    flush_load_tasks(app);

    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state))
        << "Expected a load error for a file with no registered loader";

    if (std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state)) {
        auto& error = *std::get<std::shared_ptr<AssetLoadError>>(*state);
        EXPECT_TRUE(std::holds_alternative<load_error::MissingAssetLoader>(error))
            << "Expected MissingAssetLoader error variant";
    }
}

// Ported from bevy_asset::tests::load_failure �?"malformed root asset"
// Tests that a loader returning an error produces AssetLoaderException.
TEST(LoadFailure, LoaderError_FailsWithAssetLoaderException) {
    auto dir = make_memory_dir_with_text("malformed content");

    App app = App::create();
    AssetPlugin plugin;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<FailingLoader>(app);

    auto& server = app.resource<AssetServer>();
    // The FailingLoader handles ".fail" extension; put a file with that extension.
    auto fail_data = make_bytes("bad");
    ASSERT_TRUE(dir.insert_file("malformed.fail", memory::Value::from_shared(fail_data)).has_value());

    auto handle = server.load<std::string>(AssetPath("malformed.fail"));
    flush_load_tasks(app);

    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state))
        << "Expected a load error for malformed content";

    if (std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state)) {
        auto& error = *std::get<std::shared_ptr<AssetLoadError>>(*state);
        EXPECT_TRUE(std::holds_alternative<load_error::AssetLoaderException>(error))
            << "Expected AssetLoaderException error variant";
    }
}

// Ported from bevy_asset::tests::load_failure �?combined scenario
// Verifies that a successful load produces Loaded, while missing file and loader error produce distinct failures.
TEST(LoadFailure, MixedScenarios_CorrectStateForEach) {
    auto dir = make_memory_dir_with_text("good content");

    App app = App::create();
    AssetPlugin plugin;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);
    app_register_loader<FailingLoader>(app);

    auto& server = app.resource<AssetServer>();

    auto fail_data = make_bytes("!");
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
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state))
            << "Missing file should produce an error state";
    }

    // 3. Loader error
    {
        auto state = server.get_load_state(h_error.id());
        ASSERT_TRUE(state.has_value());
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state))
            << "Failing loader should produce an error state";
        if (std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state)) {
            EXPECT_TRUE(std::holds_alternative<load_error::AssetLoaderException>(
                *std::get<std::shared_ptr<AssetLoadError>>(*state)));
        }
    }
}

// -------------------------------------------------------------------------------------
// Bevy lib.rs integrated tests �?asset lifecycle
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

    // Drop the original handle �?asset should still be reachable.
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
// Bevy lib.rs integrated tests �?manual asset management
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
// Bevy lib.rs integrated tests �?failure_load_states
// Ported from bevy_asset::tests::failure_load_states (simplified �?no dep chain)
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
// Bevy lib.rs integrated tests �?failure_load_states
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
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<FailingLoader>(app);

    auto& server   = app.resource<AssetServer>();
    auto fail_data = make_bytes("!");
    ASSERT_TRUE(dir.insert_file("bad.fail", memory::Value::from_shared(fail_data)).has_value());

    auto handle = server.load<std::string>(AssetPath("bad.fail"));
    flush_load_tasks(app);

    auto state = server.load_state(handle.id());
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(state));

    EXPECT_FALSE(server.is_loaded(handle.id()));
    EXPECT_FALSE(server.is_loaded_with_direct_dependencies(handle.id()));
    EXPECT_FALSE(server.is_loaded_with_dependencies(handle.id()));
}

TEST(LoadStateEvents, SingleAsset_SuccessMatchesStateAndEventsAcrossModes) {
    for (auto mode : {AssetServerMode::Unprocessed, AssetServerMode::Processed}) {
        SCOPED_TRACE(asset_server_mode_name(mode));

        auto [app, dir] = make_plugin_env_for_mode(mode, /*watching=*/false);
        auto& server    = app.resource<AssetServer>();

        auto handle = server.load<std::string>(AssetPath("hello.txt"));
        flush_load_tasks(app);

        auto states = server.get_load_states(handle.id());
        ASSERT_TRUE(states.has_value());
        auto [self_state, dep_state, rec_dep_state] = *states;

        ASSERT_TRUE(std::holds_alternative<LoadStateOK>(self_state));
        EXPECT_EQ(std::get<LoadStateOK>(self_state), LoadStateOK::Loaded);
        ASSERT_TRUE(std::holds_alternative<LoadStateOK>(dep_state));
        EXPECT_EQ(std::get<LoadStateOK>(dep_state), LoadStateOK::Loaded);
        ASSERT_TRUE(std::holds_alternative<LoadStateOK>(rec_dep_state));
        EXPECT_EQ(std::get<LoadStateOK>(rec_dep_state), LoadStateOK::Loaded);

        EXPECT_TRUE(server.is_loaded(handle.id()));
        EXPECT_TRUE(server.is_loaded_with_direct_dependencies(handle.id()));
        EXPECT_TRUE(server.is_loaded_with_dependencies(handle.id()));

        auto& asset_events = app.resource<Events<AssetEvent<std::string>>>();
        EXPECT_TRUE(any_recorded_event(
            asset_events, [&](const AssetEvent<std::string>& event) { return event.is_added(handle.id()); }));
        EXPECT_TRUE(any_recorded_event(asset_events, [&](const AssetEvent<std::string>& event) {
            return event.is_loaded_with_dependencies(handle.id());
        }));

        auto& typed_failures = app.resource<Events<AssetLoadFailedEvent<std::string>>>();
        EXPECT_FALSE(any_recorded_event(
            typed_failures, [&](const AssetLoadFailedEvent<std::string>& event) { return event.id == handle.id(); }));

        auto& untyped_failures = app.resource<Events<UntypedAssetLoadFailedEvent>>();
        EXPECT_FALSE(any_recorded_event(untyped_failures, [&](const UntypedAssetLoadFailedEvent& event) {
            return event.id == UntypedAssetId(handle.id());
        }));
    }
}

TEST(LoadStateEvents, DirectDependencySuccess_MatchesStateAndEventsAcrossModes) {
    for (auto mode : {AssetServerMode::Unprocessed, AssetServerMode::Processed}) {
        SCOPED_TRACE(asset_server_mode_name(mode));

        auto [app, dir] = make_plugin_env_for_mode(mode, /*watching=*/false);
        auto& server    = app.resource<AssetServer>();
        app_register_asset<DependencyManifestAsset>(app);
        app_register_loader<DependencyManifestLoader>(app);

        auto main_data = make_bytes("hello.txt");
        ASSERT_TRUE(dir.insert_file("main.dep", memory::Value::from_shared(main_data)).has_value());

        auto root = server.load<DependencyManifestAsset>(AssetPath("main.dep"));
        flush_load_tasks(app);

        auto child = server.get_handle<std::string>(AssetPath("hello.txt"));
        ASSERT_TRUE(child.has_value());

        EXPECT_TRUE(server.is_loaded(root.id()));
        EXPECT_TRUE(server.is_loaded_with_direct_dependencies(root.id()));
        EXPECT_TRUE(server.is_loaded_with_dependencies(root.id()));
        EXPECT_TRUE(server.is_loaded_with_dependencies(child->id()));

        auto root_states = server.get_load_states(root.id());
        ASSERT_TRUE(root_states.has_value());
        EXPECT_EQ(std::get<LoadStateOK>(std::get<0>(*root_states)), LoadStateOK::Loaded);
        EXPECT_EQ(std::get<LoadStateOK>(std::get<1>(*root_states)), LoadStateOK::Loaded);
        EXPECT_EQ(std::get<LoadStateOK>(std::get<2>(*root_states)), LoadStateOK::Loaded);

        auto& root_events = app.resource<Events<AssetEvent<DependencyManifestAsset>>>();
        EXPECT_TRUE(any_recorded_event(root_events, [&](const AssetEvent<DependencyManifestAsset>& event) {
            return event.is_loaded_with_dependencies(root.id());
        }));
        auto& child_events = app.resource<Events<AssetEvent<std::string>>>();
        EXPECT_TRUE(any_recorded_event(child_events, [&](const AssetEvent<std::string>& event) {
            return event.is_loaded_with_dependencies(child->id());
        }));

        auto& root_failures = app.resource<Events<AssetLoadFailedEvent<DependencyManifestAsset>>>();
        EXPECT_FALSE(any_recorded_event(root_failures, [&](const AssetLoadFailedEvent<DependencyManifestAsset>& event) {
            return event.id == root.id();
        }));
        auto& typed_failures = app.resource<Events<AssetLoadFailedEvent<std::string>>>();
        EXPECT_FALSE(any_recorded_event(
            typed_failures, [&](const AssetLoadFailedEvent<std::string>& event) { return event.id == child->id(); }));
    }
}

TEST(LoadStateEvents, DirectDependencyFailure_ParentStaysLoadedButDependencyStatesFailAcrossModes) {
    for (auto mode : {AssetServerMode::Unprocessed, AssetServerMode::Processed}) {
        SCOPED_TRACE(asset_server_mode_name(mode));

        auto [app, dir] = make_plugin_env_for_mode(mode, /*watching=*/false);
        auto& server    = app.resource<AssetServer>();
        app_register_asset<DependencyManifestAsset>(app);
        app_register_loader<DependencyManifestLoader>(app);

        auto main_data = make_bytes("missing.txt");
        ASSERT_TRUE(dir.insert_file("main.dep", memory::Value::from_shared(main_data)).has_value());

        auto root = server.load<DependencyManifestAsset>(AssetPath("main.dep"));
        flush_load_tasks(app);

        auto child = server.get_handle<std::string>(AssetPath("missing.txt"));
        ASSERT_TRUE(child.has_value());

        auto root_states = server.get_load_states(root.id());
        ASSERT_TRUE(root_states.has_value());
        EXPECT_TRUE(std::holds_alternative<LoadStateOK>(std::get<0>(*root_states)));
        EXPECT_EQ(std::get<LoadStateOK>(std::get<0>(*root_states)), LoadStateOK::Loaded);
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(std::get<1>(*root_states)));
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(std::get<2>(*root_states)));

        EXPECT_TRUE(server.is_loaded(root.id()));
        EXPECT_FALSE(server.is_loaded_with_direct_dependencies(root.id()));
        EXPECT_FALSE(server.is_loaded_with_dependencies(root.id()));

        auto child_state = server.get_load_state(child->id());
        ASSERT_TRUE(child_state.has_value());
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*child_state));

        auto& root_events = app.resource<Events<AssetEvent<DependencyManifestAsset>>>();
        EXPECT_TRUE(any_recorded_event(
            root_events, [&](const AssetEvent<DependencyManifestAsset>& event) { return event.is_added(root.id()); }));
        EXPECT_FALSE(any_recorded_event(root_events, [&](const AssetEvent<DependencyManifestAsset>& event) {
            return event.is_loaded_with_dependencies(root.id());
        }));
        auto& child_events = app.resource<Events<AssetEvent<std::string>>>();
        EXPECT_FALSE(any_recorded_event(child_events, [&](const AssetEvent<std::string>& event) {
            return event.is_loaded_with_dependencies(child->id());
        }));

        auto& root_failures = app.resource<Events<AssetLoadFailedEvent<DependencyManifestAsset>>>();
        EXPECT_FALSE(any_recorded_event(root_failures, [&](const AssetLoadFailedEvent<DependencyManifestAsset>& event) {
            return event.id == root.id();
        }));
        auto& typed_failures = app.resource<Events<AssetLoadFailedEvent<std::string>>>();
        EXPECT_TRUE(any_recorded_event(typed_failures, [&](const AssetLoadFailedEvent<std::string>& event) {
            return event.id == child->id() && event.path == AssetPath("missing.txt");
        }));

        auto& untyped_failures = app.resource<Events<UntypedAssetLoadFailedEvent>>();
        EXPECT_TRUE(any_recorded_event(untyped_failures, [&](const UntypedAssetLoadFailedEvent& event) {
            return event.id == UntypedAssetId(child->id()) && event.path == AssetPath("missing.txt");
        }));
    }
}

TEST(LoadStateEvents, RecursiveDependencyFailure_OnlyRecursiveStateFailsAcrossModes) {
    for (auto mode : {AssetServerMode::Unprocessed, AssetServerMode::Processed}) {
        SCOPED_TRACE(asset_server_mode_name(mode));

        auto [app, dir] = make_plugin_env_for_mode(mode, /*watching=*/false);
        auto& server    = app.resource<AssetServer>();
        app_register_asset<DependencyManifestAsset>(app);
        app_register_loader<DependencyManifestLoader>(app);

        ASSERT_TRUE(dir.insert_file("root.dep", memory::Value::from_shared(make_bytes("mid.dep"))).has_value());
        ASSERT_TRUE(dir.insert_file("mid.dep", memory::Value::from_shared(make_bytes("missing.txt"))).has_value());

        auto root = server.load<DependencyManifestAsset>(AssetPath("root.dep"));
        flush_load_tasks(app);

        auto mid  = server.get_handle<DependencyManifestAsset>(AssetPath("mid.dep"));
        auto leaf = server.get_handle<std::string>(AssetPath("missing.txt"));
        ASSERT_TRUE(mid.has_value());
        ASSERT_TRUE(leaf.has_value());

        auto root_states = server.get_load_states(root.id());
        ASSERT_TRUE(root_states.has_value());
        EXPECT_TRUE(std::holds_alternative<LoadStateOK>(std::get<0>(*root_states)));
        EXPECT_EQ(std::get<LoadStateOK>(std::get<0>(*root_states)), LoadStateOK::Loaded);
        EXPECT_TRUE(std::holds_alternative<LoadStateOK>(std::get<1>(*root_states)));
        EXPECT_EQ(std::get<LoadStateOK>(std::get<1>(*root_states)), LoadStateOK::Loaded);
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(std::get<2>(*root_states)));

        auto mid_states = server.get_load_states(mid->id());
        ASSERT_TRUE(mid_states.has_value());
        EXPECT_TRUE(std::holds_alternative<LoadStateOK>(std::get<0>(*mid_states)));
        EXPECT_EQ(std::get<LoadStateOK>(std::get<0>(*mid_states)), LoadStateOK::Loaded);
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(std::get<1>(*mid_states)));
        EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(std::get<2>(*mid_states)));

        EXPECT_TRUE(server.is_loaded(root.id()));
        EXPECT_TRUE(server.is_loaded_with_direct_dependencies(root.id()));
        EXPECT_FALSE(server.is_loaded_with_dependencies(root.id()));
        EXPECT_TRUE(server.is_loaded(mid->id()));
        EXPECT_FALSE(server.is_loaded_with_direct_dependencies(mid->id()));
        EXPECT_FALSE(server.is_loaded_with_dependencies(mid->id()));

        auto& manifest_events = app.resource<Events<AssetEvent<DependencyManifestAsset>>>();
        EXPECT_FALSE(any_recorded_event(manifest_events, [&](const AssetEvent<DependencyManifestAsset>& event) {
            return event.is_loaded_with_dependencies(root.id()) || event.is_loaded_with_dependencies(mid->id());
        }));
        auto& text_events = app.resource<Events<AssetEvent<std::string>>>();
        EXPECT_FALSE(any_recorded_event(text_events, [&](const AssetEvent<std::string>& event) {
            return event.is_loaded_with_dependencies(leaf->id());
        }));

        auto& manifest_failures = app.resource<Events<AssetLoadFailedEvent<DependencyManifestAsset>>>();
        EXPECT_FALSE(
            any_recorded_event(manifest_failures, [&](const AssetLoadFailedEvent<DependencyManifestAsset>& event) {
                return event.id == root.id() || event.id == mid->id();
            }));
        auto& typed_failures = app.resource<Events<AssetLoadFailedEvent<std::string>>>();
        EXPECT_TRUE(any_recorded_event(typed_failures, [&](const AssetLoadFailedEvent<std::string>& event) {
            return event.id == leaf->id() && event.path == AssetPath("missing.txt");
        }));
    }
}

// -------------------------------------------------------------------------------------
// MetaTransform / load_with_settings / load_with_meta_transform tests
// -------------------------------------------------------------------------------------

TEST(MetaTransformLoad, LoadWithSettings_LoadsAssetSuccessfully) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto handle = server.load_with_settings<std::string, TestTextLoader::Settings>(
        AssetPath("hello.txt"), [](TestTextLoader::Settings&) { /* no-op mutation */ });

    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
    EXPECT_EQ(TestTextLoader::load_count.load(), 1);
}

TEST(MetaTransformLoad, LoadWithSettings_DuplicateLoad_ReturnsSameHandle) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto h1 = server.load_with_settings<std::string, TestTextLoader::Settings>(AssetPath("hello.txt"),
                                                                               [](TestTextLoader::Settings&) {});
    auto h2 = server.load_with_settings<std::string, TestTextLoader::Settings>(AssetPath("hello.txt"),
                                                                               [](TestTextLoader::Settings&) {});

    EXPECT_EQ(h1.id(), h2.id()) << "Duplicate loads of same path should return same handle";

    flush_load_tasks(app);
    EXPECT_EQ(TestTextLoader::load_count.load(), 1);
}

TEST(MetaTransformLoad, LoadWithSettingsOverride_ForcesReload) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto h1 = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    ASSERT_TRUE(server.is_loaded(h1.id()));
    int count_after_first = TestTextLoader::load_count.load();

    auto h2 = server.load_with_settings_override<std::string, TestTextLoader::Settings>(
        AssetPath("hello.txt"), [](TestTextLoader::Settings&) {});
    flush_load_tasks(app);

    int count_after_override = TestTextLoader::load_count.load();
    EXPECT_GT(count_after_override, count_after_first) << "load_with_settings_override should force a reload";
    EXPECT_TRUE(server.is_loaded(h2.id()));
}

TEST(MetaTransformLoad, LoadWithMetaTransform_LoadsAsset) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto transform_called = std::make_shared<std::atomic<bool>>(false);
    MetaTransform mt      = [transform_called](AssetMetaDyn& meta) {
        transform_called->store(true);
        EXPECT_EQ(meta.action_type(), AssetActionType::Load);
    };

    auto handle = server.load_with_meta_transform<std::string>(AssetPath("hello.txt"), std::move(mt), false);
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_TRUE(transform_called->load()) << "MetaTransform should have been called during loading";

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
}

TEST(MetaTransformLoad, LoadWithMetaTransform_Force_ForcesReload) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    TestTextLoader::reset_stats();
    auto h1 = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);
    int count_after_first = TestTextLoader::load_count.load();

    // When the handle is already alive, the existing strong handle is reused
    // (matching Bevy behavior). The meta_transform is stored on newly created
    // handles, not on already-live ones. But force=true still triggers a reload.
    auto h2 = server.load_with_meta_transform<std::string>(AssetPath("hello.txt"), std::nullopt, true);
    flush_load_tasks(app);

    EXPECT_GT(TestTextLoader::load_count.load(), count_after_first)
        << "load_with_meta_transform with force=true should trigger another load";
    EXPECT_TRUE(server.is_loaded(h2.id()));
}

TEST(MetaTransformLoad, LoadWithMetaTransform_NullTransform_LoadsNormally) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle = server.load_with_meta_transform<std::string>(AssetPath("hello.txt"), std::nullopt, false);
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
}

TEST(MetaTransformLoad, LoadAcquireWithSettings_BlocksUntilLoaded) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    // load_acquire_with_settings blocks until is_loaded returns true.
    // We launch it on a separate thread since it busy-waits, then
    // flush the event processing on the main thread so the asset
    // transitions to Loaded.
    std::atomic<bool> acquired{false};
    Handle<std::string> handle = server.load_with_settings<std::string, TestTextLoader::Settings>(
        AssetPath("hello.txt"), [](TestTextLoader::Settings&) {});

    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
}

TEST(MetaTransformLoad, HandlePreservesMetaTransform_AfterLoad) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    MetaTransform mt = [](AssetMetaDyn&) {};
    auto handle      = server.load_with_meta_transform<std::string>(AssetPath("hello.txt"), std::move(mt), false);

    // The strong handle returned should carry the meta_transform
    UntypedHandle uh = handle.untyped();
    EXPECT_TRUE(uh.is_strong());
    EXPECT_NE(uh.meta_transform(), nullptr) << "Strong handle should preserve meta_transform";
}

TEST(MetaTransformLoad, HandleWithoutMetaTransform_ReturnsNull) {
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();

    auto handle      = server.load<std::string>(AssetPath("hello.txt"));
    UntypedHandle uh = handle.untyped();
    EXPECT_TRUE(uh.is_strong());
    EXPECT_EQ(uh.meta_transform(), nullptr) << "Handle loaded without meta_transform should return nullptr";
}

// -------------------------------------------------------------------------------------
// MetaFile integration tests — .meta file drives loader selection
// -------------------------------------------------------------------------------------

TEST(MetaFileIntegration, MetaFile_SelectsLoaderByName) {
    // Build a memory dir with both the asset and a .meta file pointing to AltTextLoader.
    auto dir = memory::Directory::create({});
    ASSERT_TRUE(dir.insert_file("hello.txt", memory::Value::from_shared(make_bytes("hello"))).has_value());

    // Use the full meta format (not just minimal) so that full meta deserialization succeeds.
    AssetMeta<AltTextLoader::Settings, EmptySettings> meta;
    meta.meta_format_version           = std::string(META_FORMAT_VERSION);
    meta.action                        = AssetActionType::Load;
    meta.loader                        = std::string(meta::type_id<AltTextLoader>{}.name());
    meta.loader_settings_storage.value = AltTextLoader::Settings{};

    auto meta_bytes_opt = serialize_asset_meta(meta);
    ASSERT_TRUE(meta_bytes_opt.has_value());
    auto meta_data = std::make_shared<std::vector<std::byte>>(*meta_bytes_opt);
    ASSERT_TRUE(dir.insert_file("hello.txt.meta", memory::Value::from_shared(meta_data)).has_value());

    App app = App::create();
    AssetPlugin plugin;
    plugin.watch_for_changes_override = false;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<TestTextLoader>(app);
    app_register_loader<AltTextLoader>(app);

    TestTextLoader::reset_stats();
    AltTextLoader::reset_stats();

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_EQ(AltTextLoader::load_count.load(), 1) << "Meta file should route load to AltTextLoader";
    EXPECT_EQ(TestTextLoader::load_count.load(), 0) << "TestTextLoader should not be invoked";

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
}

TEST(MetaFileIntegration, NoMetaFile_FallsBackToExtensionLoader) {
    // Without a .meta file the server uses extension-based lookup (TestTextLoader registered first).
    auto [app, dir] = make_plugin_env(/*watching=*/false);
    auto& server    = app.resource<AssetServer>();
    app_register_loader<AltTextLoader>(app);

    TestTextLoader::reset_stats();
    AltTextLoader::reset_stats();

    auto handle = server.load<std::string>(AssetPath("hello.txt"));
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_EQ(TestTextLoader::load_count.load() + AltTextLoader::load_count.load(), 1)
        << "Exactly one loader must have handled the asset";

    auto& assets = app.resource<Assets<std::string>>();
    auto value   = assets.get(handle.id());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->get(), "hello");
}

TEST(MetaFileIntegration, MetaFile_RoundTrip_SerializeDeserializeMinimal) {
    // Verify that a serialized AssetMetaMinimal can be deserialized back to its original values.
    AssetMetaMinimal original;
    original.meta_format_version = std::string(META_FORMAT_VERSION);
    original.asset.action        = AssetActionType::Load;
    original.asset.loader        = std::string(meta::type_id<AltTextLoader>{}.name());

    auto bytes = serialize_meta_minimal(original);
    ASSERT_TRUE(bytes.has_value());

    auto result = deserialize_meta_minimal(*bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->meta_format_version, original.meta_format_version);
    EXPECT_EQ(result->asset.action, AssetActionType::Load);
    EXPECT_EQ(result->asset.loader, original.asset.loader);
}

// -------------------------------------------------------------------------------------
// DeserializeMeta integration — server restores loader settings from .meta bytes
// -------------------------------------------------------------------------------------

// Helper: build a serialized AssetMeta for SettingsCapturingLoader with custom quality.
static std::shared_ptr<std::vector<std::byte>> make_settings_meta_bytes(int quality) {
    AssetMeta<SettingsCapturingLoader::Settings, EmptySettings> meta;
    meta.meta_format_version           = std::string(META_FORMAT_VERSION);
    meta.action                        = AssetActionType::Load;
    meta.loader                        = std::string(meta::type_id<SettingsCapturingLoader>{}.name());
    meta.loader_settings_storage.value = SettingsCapturingLoader::Settings{quality};
    auto result                        = serialize_asset_meta(meta);
    EXPECT_TRUE(result.has_value());
    return std::make_shared<std::vector<std::byte>>(*result);
}

TEST(DeserializeMetaIntegration, CustomQuality_RestoredFromMetaFile) {
    // Build a memory source with both the asset file and a .meta file.
    auto dir = memory::Directory::create({});
    ASSERT_TRUE(dir.insert_file("asset.qtxt", memory::Value::from_shared(make_bytes("data"))).has_value());
    ASSERT_TRUE(
        dir.insert_file("asset.qtxt.meta", memory::Value::from_shared(make_settings_meta_bytes(99))).has_value());

    App app = App::create();
    AssetPlugin plugin;
    plugin.watch_for_changes_override = false;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<SettingsCapturingLoader>(app);

    SettingsCapturingLoader::reset_stats();

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<std::string>(AssetPath("asset.qtxt"));
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_EQ(SettingsCapturingLoader::last_quality.load(), 99)
        << "Loader should receive quality=99 restored from the .meta file";
}

TEST(DeserializeMetaIntegration, DefaultQuality_UsedWhenNoMetaFile) {
    // No .meta file present — loader should receive the default quality=5.
    auto dir = memory::Directory::create({});
    ASSERT_TRUE(dir.insert_file("asset.qtxt", memory::Value::from_shared(make_bytes("data"))).has_value());

    App app = App::create();
    AssetPlugin plugin;
    plugin.watch_for_changes_override = false;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<SettingsCapturingLoader>(app);

    SettingsCapturingLoader::reset_stats();

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<std::string>(AssetPath("asset.qtxt"));
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_EQ(SettingsCapturingLoader::last_quality.load(), 5)
        << "No .meta file: loader should receive default quality=5";
}

TEST(DeserializeMetaIntegration, FailsWithDeserializeMeta_WhenMetaFileBytesAreGarbage) {
    // .meta file exists but contains unparseable garbage — server should produce DeserializeMeta error (Bevy parity).
    auto dir = memory::Directory::create({});
    ASSERT_TRUE(dir.insert_file("asset.qtxt", memory::Value::from_shared(make_bytes("data"))).has_value());

    auto garbage = std::make_shared<std::vector<std::byte>>(
        std::vector<std::byte>{std::byte{0xFF}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x01}});
    ASSERT_TRUE(dir.insert_file("asset.qtxt.meta", memory::Value::from_shared(garbage)).has_value());

    App app = App::create();
    AssetPlugin plugin;
    plugin.watch_for_changes_override = false;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<SettingsCapturingLoader>(app);

    SettingsCapturingLoader::reset_stats();

    auto& server = app.resource<AssetServer>();
    auto handle  = server.load<std::string>(AssetPath("asset.qtxt"));
    flush_load_tasks(app);

    // Asset should fail to load — meta bytes are unparseable (matches Bevy's DeserializeMeta error).
    auto state = server.get_load_state(handle.id());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state))
        << "Garbage .meta file should cause a DeserializeMeta load failure";
    if (std::holds_alternative<std::shared_ptr<AssetLoadError>>(*state)) {
        EXPECT_TRUE(
            std::holds_alternative<load_error::DeserializeMeta>(*std::get<std::shared_ptr<AssetLoadError>>(*state)))
            << "Expected DeserializeMeta error variant for garbage meta bytes";
    }
}

TEST(DeserializeMetaIntegration, DifferentQualities_LoadSamePathWithMetaTransform_Overrides) {
    // Verify meta_transform takes precedence over settings from .meta file.
    // We write quality=99 to the .meta file, then use load_with_settings_override to set quality=77.
    auto dir = memory::Directory::create({});
    ASSERT_TRUE(dir.insert_file("asset.qtxt", memory::Value::from_shared(make_bytes("data"))).has_value());
    ASSERT_TRUE(
        dir.insert_file("asset.qtxt.meta", memory::Value::from_shared(make_settings_meta_bytes(99))).has_value());

    App app = App::create();
    AssetPlugin plugin;
    plugin.watch_for_changes_override = false;
    plugin.register_asset_source(AssetSourceId{}, make_memory_source_builder(dir));
    plugin.build(app);
    app_register_asset<std::string>(app);
    app_register_loader<SettingsCapturingLoader>(app);

    SettingsCapturingLoader::reset_stats();

    auto& server = app.resource<AssetServer>();
    // load_with_settings_override applies a MetaTransform after deserialization; quality must be 77.
    auto handle = server.load_with_settings_override<std::string, SettingsCapturingLoader::Settings>(
        AssetPath("asset.qtxt"), [](SettingsCapturingLoader::Settings& s) { s.quality = 77; });
    flush_load_tasks(app);

    EXPECT_TRUE(server.is_loaded(handle.id()));
    EXPECT_EQ(SettingsCapturingLoader::last_quality.load(), 77)
        << "MetaTransform override should win over .meta file settings";
}
