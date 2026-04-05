module;

#include <spdlog/spdlog.h>

module epix.assets;

using namespace epix::assets;
using namespace epix::core;

void AssetPlugin::build(App& app) {
    spdlog::debug("[assets] Building AssetPlugin (mode={}).", static_cast<int>(mode));
    auto& world = app.world_mut();

    EmbeddedAssetRegistry embedded;

    // Bevy-style: fetch builders from World, emplace if absent.
    auto& builders = world.resource_or_emplace<AssetSourceBuilders>();
    auto processed = (mode != AssetServerMode::Unprocessed) ? processed_file_path : std::nullopt;
    builders.init_default(file_path, processed);
    embedded.register_source(builders);

    for (auto& [id, builder] : m_source_builders) {
        builders.insert(std::move(id), std::move(builder));
    }
    m_source_builders.clear();

    const bool watch = watch_for_changes_override.value_or(false);

    switch (mode) {
        case AssetServerMode::Unprocessed: {
            auto sources = std::make_shared<AssetSources>(builders.build_sources(watch, false));
            world.emplace_resource<AssetServer>(std::move(sources), AssetServerMode::Unprocessed, meta_check, watch,
                                                unapproved_path_mode);
            break;
        }
        case AssetServerMode::Processed: {
            const bool use_asset_processor = use_asset_processor_override.value_or(true);
            if (use_asset_processor && processed.has_value()) {
                auto processor_data             = std::make_shared<AssetProcessorData>();
                processor_data->source_builders = std::make_shared<AssetSourceBuilders>(std::move(builders));
                processor_data->set_log_factory(std::make_shared<FileTransactionLogFactory>(processed.value() / "log"));
                auto processor = AssetProcessor(std::move(processor_data), watch);

                world.emplace_resource<AssetServer>(processor.sources(), processor.get_server().data->loaders,
                                                    AssetServerMode::Processed, AssetMetaCheck{asset_meta_check::Always{}}, watch,
                                                    unapproved_path_mode);
                world.emplace_resource<AssetProcessor>(std::move(processor));
                app.add_systems(Startup, into(AssetProcessor::start));
            } else {
                if (!processed.has_value()) {
                    spdlog::warn(
                        "[assets] AssetPlugin is in Processed mode but no processed_file_path was set. Defaulting to "
                        "unprocessed behavior.");
                }
                auto sources = std::make_shared<AssetSources>(builders.build_sources(false, watch));
                world.emplace_resource<AssetServer>(std::move(sources), AssetServerMode::Processed,
                                                    AssetMetaCheck{asset_meta_check::Always{}}, watch, unapproved_path_mode);
            }
            break;
        }
    }

    world.emplace_resource<EmbeddedAssetRegistry>(std::move(embedded));

    // Mirror Bevy: init_asset::<LoadedFolder>() and init_asset::<LoadedUntypedAsset>()
    app_register_asset<LoadedFolder>(app);
    app_register_asset<LoadedUntypedAsset>(app);

    app.add_events<UntypedAssetLoadFailedEvent>();

    app.add_systems(Last, into(AssetServer::handle_internal_events));
    app.configure_sets(sets(AssetSystems::HandleEvents, AssetSystems::WriteEvents).chain());
}

void AssetPlugin::finish(App& app) { (void)app; }

AssetPlugin& AssetPlugin::register_asset_source(AssetSourceId id, AssetSourceBuilder source) {
    m_source_builders.emplace_back(std::move(id), std::move(source));
    return *this;
}
