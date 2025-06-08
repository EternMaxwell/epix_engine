#include "epix/assets.h"

using namespace epix::assets;

EPIX_API void AssetPlugin::build(epix::App& app) {
    app.add_resource(epix::UntypedRes::create(m_asset_server));
    app.add_systems(Last, into(AssetServer::handle_events));
}
EPIX_API void AssetPlugin::finish(epix::App& app) {
    for (auto&& insert : m_assets_inserts) {
        insert(app);
    }
}