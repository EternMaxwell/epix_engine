#include "epix/assets.h"

using namespace epix::assets;

EPIX_API void AssetPlugin::build(epix::App& app) {
    app.add_resource(std::move(m_asset_server));
    app.add_systems(Last, into(AssetServer::handle_events));
    for (auto&& insert : m_assets_inserts) {
        insert(app);
    }
}