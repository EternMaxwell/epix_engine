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
            app.add_system(First, Assets<T>::res_handle_events);
        });
        return *this;
    }
    template <typename T, typename Context = void>
    AssetPlugin& add_loader() {
        m_loader_inserts.push_back([](epix::App& app) {
            app.init_resource<AssetLoader<T>>()
                .add_system(
                    epix::PreStartup, AssetLoader<T>::get_handle_provider
                )
                .add_system(epix::First, AssetLoader<T>::loaded);
            if constexpr (!std::is_same_v<Context, void>) {
                void (*func)(epix::ResMut<AssetIO>, epix::ResMut<AssetLoader<T>>, epix::ResMut<Context>) =
                    AssetLoader<T>::load_cached_ctx;
                app.add_system(
                    epix::First, into(func).before(AssetLoader<T>::loaded)
                );
            } else {
                void (*func)(epix::ResMut<AssetIO>, epix::ResMut<AssetLoader<T>>) =
                    AssetLoader<T>::load_cached;
                app.add_system(
                    epix::First, into(func).before(AssetLoader<T>::loaded)
                );
            }
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