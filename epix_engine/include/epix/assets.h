#pragma once

#include <epix/app.h>
#include <epix/common.h>

#include "assets/asset_io.h"
#include "assets/asset_server.h"
#include "assets/assets.h"
#include "assets/handle.h"
#include "assets/index.h"

namespace epix::assets {
struct AssetPlugin : public epix::Plugin {
    std::vector<std::function<void(epix::App&)>> m_assets_inserts;
    std::vector<std::function<void(epix::App&)>> m_loader_inserts;

    template <typename T>
    AssetPlugin& register_asset() {
        m_assets_inserts.push_back([](epix::App& app) {
            app.init_resource<Assets<T>>();
            app.add_events<AssetEvent<T>>();
            app.add_systems(
                First,
                into(Assets<T>::res_handle_events, Assets<T>::asset_events)
                    .chain()
            );
        });
        return *this;
    }

    void build(epix::App& app) override {
        app.init_resource<AssetIO>();
        for (auto&& insert : m_assets_inserts) {
            insert(app);
        }
        for (auto&& insert : m_loader_inserts) {
            insert(app);
        }
    }
};
}  // namespace epix::assets