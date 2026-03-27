module epix.assets;

using namespace assets;
using namespace core;

void AssetPlugin::build(App& app) {
    AssetSourceBuilders builders;
    builders.init_default(file_path, processed_file_path);
    for (auto& [id, builder] : m_source_builders) {
        builders.insert(std::move(id), std::move(builder));
    }
    m_source_builders.clear();

    switch (mode) {
        case AssetServerMode::Unprocessed: {
            auto sources = std::make_shared<AssetSources>(builders.build_sources(watch_for_changes, false));
            app.world_mut().emplace_resource<AssetServer>(std::move(sources), mode, meta_check, watch_for_changes,
                                                          unapproved_path_mode);
            break;
        }
        case AssetServerMode::Processed: {
            auto processor_data             = std::make_shared<AssetProcessorData>();
            processor_data->source_builders = std::make_shared<AssetSourceBuilders>(std::move(builders));
            auto processor                  = AssetProcessor(std::move(processor_data), watch_for_changes);
            // Main server shares loaders and sources with the processor's internal server
            app.world_mut().emplace_resource<AssetServer>(processor.sources(), processor.get_server().data->loaders,
                                                          AssetServerMode::Processed, AssetMetaCheck::Always,
                                                          watch_for_changes, unapproved_path_mode);
            app.world_mut().emplace_resource<AssetProcessor>(std::move(processor));
            app.add_systems(Startup, into(AssetProcessor::start));
            break;
        }
    }
    app.add_systems(Last, into(AssetServer::handle_internal_events));
    app.configure_sets(sets(AssetSystems::HandleEvents, AssetSystems::WriteEvents).chain());
}

void AssetPlugin::finish(App& app) {
    for (auto&& insert : m_assets_inserts) {
        insert(app);
    }
}

AssetPlugin& AssetPlugin::register_asset_source(AssetSourceId id, AssetSourceBuilder source) {
    m_source_builders.emplace_back(std::move(id), std::move(source));
    return *this;
}