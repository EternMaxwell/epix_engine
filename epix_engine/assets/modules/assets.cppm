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
export import :processor.process;
export import :processor.log;
export import :processor;
export import :io.processor_gated;
export import :io.memory;
export import :io.memory.asset;
export import :io.reader;
export import :io.source;
export import :io.embedded;

using namespace epix::core;

namespace epix::assets {
/** @brief Built-in system set labels for asset event processing order. */
export enum class AssetSystems {
    HandleEvents, /**< Systems that react to handle lifecycle events. */
    WriteEvents,  /**< Systems that emit asset lifecycle events. */
};
/** @brief Asset plugin configuration and source setup.
 *  Mirrors Bevy's AssetPlugin role: configure sources/mode and build core resources/systems. */
export struct AssetPlugin {
   private:
    std::vector<std::pair<AssetSourceId, AssetSourceBuilder>> m_source_builders;

   public:
    /** @brief Filesystem path to the default asset source directory. */
    std::filesystem::path file_path = "assets";
    /** @brief Optional processed-asset directory path. */
    std::optional<std::filesystem::path> processed_file_path = std::nullopt;
    /** @brief Asset server mode. */
    AssetServerMode mode = AssetServerMode::Unprocessed;
    /** @brief Optional watch override (mirrors Bevy's watch_for_changes_override). */
    std::optional<bool> watch_for_changes_override = std::nullopt;
    /** @brief Optional processor override in Processed mode (mirrors Bevy's use_asset_processor_override). */
    std::optional<bool> use_asset_processor_override = std::nullopt;
    /** @brief Controls when and how asset metadata files are checked. */
    AssetMetaCheck meta_check = AssetMetaCheck::Always;
    /** @brief Controls how unapproved asset paths are handled. */
    UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid;

    /** @brief Register a named asset source builder. */
    AssetPlugin& register_asset_source(AssetSourceId id, AssetSourceBuilder source);
    /** @brief Build the plugin, inserting asset resources into the app. */
    void build(App& app);
    /** @brief Finalize the plugin after all other plugins have built. */
    void finish(App& app);
};

/** @brief AssetApp-style helper: register an asset type directly on an App with an existing AssetServer. */
export template <std::movable T>
App& app_register_asset(App& app) {
    if (app.world_mut().get_resource<Assets<T>>().has_value()) return app;
    app.world_mut().init_resource<Assets<T>>();
    app.resource_mut<AssetServer>().register_assets(app.resource<Assets<T>>());
    app.add_events<AssetEvent<T>>();
    app.add_events<AssetLoadFailedEvent<T>>();
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
    app.add_systems(First, into(Assets<T>::asset_events)
                               .in_set(AssetSystems::WriteEvents)
                               .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
    app.add_systems(Last, into(Assets<T>::asset_events)
                              .in_set(AssetSystems::WriteEvents)
                              .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
    app.add_systems(PostUpdate, into(Assets<T>::handle_events)
                                    .in_set(AssetSystems::HandleEvents)
                                    .set_name(std::format("handle {} asset events", meta::type_id<T>::name())));
    return app;
}

/** @brief AssetApp-style helper: register a loader directly on an App with an existing AssetServer. */
export template <AssetLoader T>
App& app_register_loader(App& app, const T& t = T()) {
    app.resource_mut<AssetServer>().register_loader(t);
    return app;
}

/** @brief AssetApp-style helper: preregister a loader extension mapping directly on an App. */
export template <AssetLoader T>
App& app_preregister_loader(App& app, std::span<std::string_view> extensions) {
    app.resource_mut<AssetServer>().template preregister_loader<T>(extensions);
    return app;
}

/** @brief AssetApp-style helper: register an asset processor directly on an App. */
export template <Process P>
App& app_register_asset_processor(App& app, P processor) {
    if (!app.world_mut().get_resource<AssetProcessor>().has_value()) {
        throw std::runtime_error("AssetProcessor resource not found. Build AssetPlugin in Processed mode first.");
    }
    app.resource_mut<AssetProcessor>().register_processor(std::move(processor));
    return app;
}

/** @brief AssetApp-style helper: set the default asset processor for an extension directly on an App. */
export template <Process P>
App& app_set_default_asset_processor(App& app, const std::string& extension) {
    if (!app.world_mut().get_resource<AssetProcessor>().has_value()) {
        throw std::runtime_error("AssetProcessor resource not found. Build AssetPlugin in Processed mode first.");
    }
    app.resource_mut<AssetProcessor>().template set_default_processor<P>(extension);
    return app;
}

}  // namespace assets