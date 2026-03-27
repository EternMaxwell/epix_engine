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
    auto sources = std::make_shared<AssetSources>(builders.build_sources(watch_for_changes, watch_for_changes));
    app.world_mut().emplace_resource<AssetServer>(std::move(sources), mode, meta_check, watch_for_changes,
                                                  unapproved_path_mode);
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