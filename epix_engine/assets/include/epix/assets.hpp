#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#endif

#include <epix/assets/asset_changed.hpp>
#include <epix/assets/concepts.hpp>
#include <epix/assets/handle.hpp>
#include <epix/assets/id.hpp>
#include <epix/assets/index.hpp>
#include <epix/assets/io/embedded.hpp>
#include <epix/assets/io/file/asset.hpp>
#include <epix/assets/io/file/watcher.hpp>
#include <epix/assets/io/memory.hpp>
#include <epix/assets/io/memory/asset.hpp>
#include <epix/assets/io/processor_gated.hpp>
#include <epix/assets/io/reader.hpp>
#include <epix/assets/io/source.hpp>
#include <epix/assets/meta.hpp>
#include <epix/assets/path.hpp>
#include <epix/assets/processor.hpp>
#include <epix/assets/processor/log.hpp>
#include <epix/assets/processor/process.hpp>
#include <epix/assets/render_asset_usages.hpp>
#include <epix/assets/saver.hpp>
#include <epix/assets/server.hpp>
#include <epix/assets/server/info.hpp>
#include <epix/assets/server/loader.hpp>
#include <epix/assets/store.hpp>
#include <epix/assets/transformer.hpp>

namespace epix::assets {

/** @brief Built-in system set labels for asset event processing order. */
EPIX_EXPORT enum class AssetSystems {
    HandleEvents, /**< Systems that react to handle lifecycle events. */
    WriteEvents,  /**< Systems that emit asset lifecycle events. */
};
/** @brief Asset plugin configuration and source setup.
 *  Mirrors Bevy's AssetPlugin role: configure sources/mode and build core resources/systems. */
EPIX_EXPORT struct AssetPlugin {
   private:
    std::vector<std::pair<AssetSourceId, AssetSourceBuilder>> m_source_builders;

   public:
    /** @brief Filesystem path to the default asset source directory. */
    std::filesystem::path file_path = "assets";
    /** @brief Optional processed-asset directory path. */
    std::optional<std::filesystem::path> processed_file_path = "processed_assets";
    /** @brief Optional processed-asset directory for the embedded source.
     *  Defaults to nullopt so embedded assets stay on the in-memory source reader and do not
     *  participate in the processed-asset pipeline unless explicitly opted in.
     *  When set, a FileAssetReader/Writer rooted at {workspace}/{path} is used for the embedded
     *  source's processed IO. */
    std::optional<std::filesystem::path> embedded_processed_path = std::nullopt;
    /** @brief Asset server mode. */
    AssetServerMode mode = AssetServerMode::Processed;
    /** @brief Optional watch override (mirrors Bevy's watch_for_changes_override). */
    std::optional<bool> watch_for_changes_override = std::nullopt;
    /** @brief Optional processor override in Processed mode (mirrors Bevy's use_asset_processor_override). */
    std::optional<bool> use_asset_processor_override = std::nullopt;
    /** @brief Controls when and how asset metadata files are checked. */
    AssetMetaCheck meta_check = AssetMetaCheck{asset_meta_check::Always{}};
    /** @brief Controls how unapproved asset paths are handled. */
    UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid;

    /** @brief Register a named asset source builder. */
    AssetPlugin& register_asset_source(AssetSourceId id, AssetSourceBuilder source);
    /** @brief Build the plugin, inserting asset resources into the app. */
    void attach(epix::app::App& app);
    /** @brief Finalize the plugin after all other plugins have built. */
    void ready(epix::app::App& app);
};

/** @brief AssetApp-style helper: register an asset type directly on an App with an existing AssetServer. */
EPIX_EXPORT template <std::movable T>
epix::app::App& app_register_asset(epix::app::App& app) {
    if (app.world_mut().get_resource<Assets<T>>().has_value()) return app;
    app.world_mut().init_resource<Assets<T>>();
    app.resource_mut<AssetServer>().register_asset(app.resource<Assets<T>>());
    app.add_events<AssetEvent<T>>();
    app.add_events<AssetLoadFailedEvent<T>>();
    app.add_systems(epix::app::PostStartup,
                    ecs::into(ecs::into(Assets<T>::handle_events).in_set(AssetSystems::HandleEvents),
                              ecs::into(Assets<T>::asset_events).in_set(AssetSystems::WriteEvents))
                        .chain()
                        .set_names(std::array{std::format("handle {} asset events", meta::type_id<T>::name()),
                                              std::format("send {} asset events", meta::type_id<T>::name())}));
    app.add_systems(epix::app::PreStartup,
                    ecs::into(Assets<T>::asset_events)
                        .in_set(AssetSystems::WriteEvents)
                        .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
    app.add_systems(epix::app::First,
                    ecs::into(Assets<T>::asset_events)
                        .in_set(AssetSystems::WriteEvents)
                        .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
    app.add_systems(epix::app::Last,
                    ecs::into(Assets<T>::asset_events)
                        .in_set(AssetSystems::WriteEvents)
                        .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
    app.add_systems(epix::app::PostUpdate,
                    ecs::into(Assets<T>::handle_events)
                        .in_set(AssetSystems::HandleEvents)
                        .set_name(std::format("handle {} asset events", meta::type_id<T>::name())));
    return app;
}

/** @brief AssetApp-style helper: register a loader directly on an App with an existing AssetServer. */
EPIX_EXPORT template <AssetLoader T>
epix::app::App& app_register_loader(epix::app::App& app, const T& t = T()) {
    app.resource_mut<AssetServer>().register_loader(t);
    return app;
}

/** @brief AssetApp-style helper: preregister a loader extension mapping directly on an App. */
EPIX_EXPORT template <AssetLoader T>
epix::app::App& app_preregister_loader(epix::app::App& app, std::span<std::string_view> extensions) {
    app.resource_mut<AssetServer>().template preregister_loader<T>(extensions);
    return app;
}

/** @brief AssetApp-style helper: register an asset processor directly on an App. */
EPIX_EXPORT template <Process P>
epix::app::App& app_register_asset_processor(epix::app::App& app, P processor) {
    if (!app.world_mut().get_resource<AssetProcessor>().has_value()) {
        throw std::runtime_error("AssetProcessor resource not found. Build AssetPlugin in Processed mode first.");
    }
    app.resource_mut<AssetProcessor>().register_processor(std::move(processor));
    return app;
}

/** @brief AssetApp-style helper: set the default asset processor for an extension directly on an App. */
EPIX_EXPORT template <Process P>
epix::app::App& app_set_default_asset_processor(epix::app::App& app, const std::string& extension) {
    if (!app.world_mut().get_resource<AssetProcessor>().has_value()) {
        throw std::runtime_error("AssetProcessor resource not found. Build AssetPlugin in Processed mode first.");
    }
    app.resource_mut<AssetProcessor>().template set_default_processor<P>(extension);
    return app;
}

}  // namespace epix::assets
