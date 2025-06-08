#pragma once

#include <epix/app.h>
#include <epix/common.h>

#include "assets/asset_server.h"
#include "assets/assets.h"
#include "assets/handle.h"
#include "assets/index.h"

namespace epix::assets {
struct AssetPlugin : public epix::Plugin {
    std::vector<std::function<void(epix::App&)>> m_assets_inserts;
    std::shared_ptr<AssetServer> m_asset_server =
        std::make_shared<AssetServer>();

    template <typename T>
    AssetPlugin& register_asset() {
        m_assets_inserts.push_back([](epix::App& app) {
            app.init_resource<Assets<T>>();
            app.world().resource<AssetServer>().register_assets(
                app.world().resource<Assets<T>>()
            );
            app.add_events<AssetEvent<T>>();
            app.add_systems(
                First,
                into(Assets<T>::handle_events, Assets<T>::asset_events).chain()
            );
        });
        return *this;
    }
    template <typename T>
    AssetPlugin& register_loader(const T& t = T()) {
        m_asset_server->register_loader(t);
        return *this;
    }

    EPIX_API void build(epix::App& app) override;
    EPIX_API void finish(epix::App& app) override;
};
}  // namespace epix::assets