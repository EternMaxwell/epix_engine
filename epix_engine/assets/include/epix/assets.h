#pragma once

#include <epix/app.h>
#include <epix/common.h>

#include "assets/asset_io.h"
#include "assets/asset_loader.h"
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
    template <typename T>
    AssetPlugin& add_loader() {
        m_loader_inserts.push_back([](epix::App& app) {
            app.init_resource<AssetLoader<T>>()
                .add_systems(
                    epix::PreStartup, into(AssetLoader<T>::get_handle_provider)
                )
                .add_systems(epix::First, into(AssetLoader<T>::loaded));
            void (*func)(epix::ResMut<AssetIO>, epix::ResMut<AssetLoader<T>>) =
                AssetLoader<T>::load_cached;
            app.add_systems(
                epix::First, into(func).before(AssetLoader<T>::loaded)
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