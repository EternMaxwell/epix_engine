module;

#include <gtest/gtest.h>

export module epix.assets;

export import :index;
export import :id;
export import :path;
export import :handle;
export import :meta;
export import :store;
export import :server.info;
export import :server.loader;
export import :server;
export import :saver;
export import :transformer;
export import :processor;
export import :io.memory;
export import :io.memory.asset;
export import :io.reader;
export import :io.source;
export import :io.embedded;

using namespace core;

namespace assets {
/** @brief Built-in system set labels for asset event processing order. */
export enum class AssetSystems {
    HandleEvents, /**< Systems that react to handle lifecycle events. */
    WriteEvents,  /**< Systems that emit asset lifecycle events. */
};
/** @brief Plugin that registers asset types and loaders with the application.
 *  Call register_asset<T>() and register_loader<L>() before building. */
export struct AssetPlugin {
   private:
    std::vector<std::function<void(App&)>> m_assets_inserts;
    std::vector<std::pair<AssetSourceId, AssetSourceBuilder>> m_source_builders;

   public:
    /** @brief Filesystem path to the default asset source directory. */
    std::filesystem::path file_path = "assets";
    /** @brief Optional processed-asset directory path (for Processed mode). */
    std::optional<std::filesystem::path> processed_file_path = std::nullopt;
    /** @brief Asset server mode. */
    AssetServerMode mode = AssetServerMode::Unprocessed;
    /** @brief Whether to watch files for changes and hot-reload. */
    bool watch_for_changes = false;
    /** @brief Controls when and how asset metadata files are checked. */
    AssetMetaCheck meta_check = AssetMetaCheck::Always;
    /** @brief Controls how unapproved asset paths are handled. */
    UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid;

    /** @brief Register an asset type T for management. */
    template <std::movable T>
    AssetPlugin& register_asset();
    /** @brief Register an asset loader for its associated asset type. */
    template <AssetLoader T>
    AssetPlugin& register_loader(const T& t = T());
    /** @brief Pre-register a loader type so its extensions are known before the loader is available. */
    template <AssetLoader T>
    AssetPlugin& preregister_loader(std::span<std::string_view> extensions);
    /** @brief Register an asset processor.
     *  Matches bevy_asset's AssetApp::register_asset_processor. */
    template <Process P>
    AssetPlugin& register_asset_processor(P processor);
    /** @brief Set the default processor for a file extension.
     *  Matches bevy_asset's AssetApp::set_default_asset_processor. */
    template <Process P>
    AssetPlugin& set_default_asset_processor(const std::string& extension);
    /** @brief Register a named asset source.
     *  Matches bevy_asset's AssetApp::register_asset_source. */
    AssetPlugin& register_asset_source(AssetSourceId id, AssetSourceBuilder source);
    /** @brief Build the plugin, inserting asset resources into the app. */
    void build(App& app);
    /** @brief Finalize the plugin after all other plugins have built. */
    void finish(App& app);
};

template <std::movable T>
AssetPlugin& AssetPlugin::register_asset() {
    m_assets_inserts.push_back([](App& app) {
        if (app.world_mut().get_resource<Assets<T>>().has_value()) return;
        app.world_mut().init_resource<Assets<T>>();
        app.resource_mut<AssetServer>().register_assets(app.resource<Assets<T>>());
        app.add_events<AssetEvent<T>>();
        app.add_systems(PostStartup,
                        into(into(Assets<T>::handle_events).in_set(AssetSystems::HandleEvents),
                             into(Assets<T>::asset_events).in_set(AssetSystems::WriteEvents))
                            .chain()
                            .set_names(std::array{std::format("handle {} asset events", meta::type_id<T>::name()),
                                                  std::format("send {} asset events", meta::type_id<T>::name())}));
        app.add_systems(PreStartup,
                        into(Assets<T>::asset_events)
                            .in_set(AssetSystems::WriteEvents)
                            .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
        app.add_systems(First,
                        into(Assets<T>::asset_events)
                            .in_set(AssetSystems::WriteEvents)
                            .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
        app.add_systems(Last,
                        into(Assets<T>::asset_events)
                            .in_set(AssetSystems::WriteEvents)
                            .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
        app.add_systems(PostUpdate, into(Assets<T>::handle_events)
                                        .in_set(AssetSystems::HandleEvents)
                                        .set_name(std::format("handle {} asset events", meta::type_id<T>::name())));
    });
    return *this;
}
template <AssetLoader T>
AssetPlugin& AssetPlugin::register_loader(const T& t) {
    m_assets_inserts.push_back([t](App& app) { app.resource_mut<AssetServer>().register_loader(t); });
    return *this;
}
template <AssetLoader T>
AssetPlugin& AssetPlugin::preregister_loader(std::span<std::string_view> extensions) {
    m_assets_inserts.push_back(
        [extensions = std::vector<std::string_view>(extensions.begin(), extensions.end())](App& app) {
            app.resource_mut<AssetServer>().template preregister_loader<T>(extensions);
        });
    return *this;
}
template <Process P>
AssetPlugin& AssetPlugin::register_asset_processor(P processor) {
    m_assets_inserts.push_back([processor = std::move(processor)](App& app) mutable {
        if (!app.world_mut().get_resource<AssetProcessor>().has_value()) {
            app.world_mut().init_resource<AssetProcessor>();
        }
        app.resource_mut<AssetProcessor>().register_processor(std::move(processor));
    });
    return *this;
}
template <Process P>
AssetPlugin& AssetPlugin::set_default_asset_processor(const std::string& extension) {
    m_assets_inserts.push_back([extension](App& app) {
        if (!app.world_mut().get_resource<AssetProcessor>().has_value()) {
            app.world_mut().init_resource<AssetProcessor>();
        }
        app.resource_mut<AssetProcessor>().template set_default_processor<P>(extension);
    });
    return *this;
}
}  // namespace assets