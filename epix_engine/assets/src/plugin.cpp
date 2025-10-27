#include "epix/assets.hpp"

using namespace epix::assets;

void AssetPlugin::build(epix::App& app) {
    app.world_mut().emplace_resource<AssetServer>();
    app.add_systems(Last, into(AssetServer::handle_events));
}
void AssetPlugin::finish(epix::App& app) {
    for (auto&& insert : m_assets_inserts) {
        insert(app);
    }
}