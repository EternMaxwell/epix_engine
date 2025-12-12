#pragma once

#include "assets/asset_id.hpp"
#include "assets/asset_server.hpp"
#include "assets/assets.hpp"
#include "assets/handle.hpp"
#include "assets/index.hpp"

namespace epix::assets {
enum class AssetSystems {
    HandleEvents,
    WriteEvents,
};
struct AssetPlugin {
   private:
    std::vector<std::function<void(epix::App&)>> m_assets_inserts;

   public:
    template <typename T>
    AssetPlugin& register_asset() {
        m_assets_inserts.push_back([](epix::App& app) {
            if (app.world_mut().get_resource<Assets<T>>().has_value()) return;
            app.world_mut().init_resource<Assets<T>>();
            app.resource_mut<AssetServer>().register_assets(app.resource<Assets<T>>());
            app.add_events<AssetEvent<T>>();
            app.add_systems(PostStartup,
                            into(into(Assets<T>::handle_events).in_set(AssetSystems::HandleEvents),
                                 into(Assets<T>::asset_events).in_set(AssetSystems::WriteEvents))
                                .chain()
                                .set_names(std::array{std::format("handle {} asset events", meta::type_id<T>::name()),
                                                      std::format("send {} asset events", meta::type_id<T>::name())}));
            app.add_systems(PreStartup,
                            into(Assets<T>::asset_events)
                                .in_set(AssetSystems::WriteEvents)
                                .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
            app.add_systems(First,
                            into(Assets<T>::asset_events)
                                .in_set(AssetSystems::WriteEvents)
                                .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
            app.add_systems(Last,
                            into(Assets<T>::asset_events)
                                .in_set(AssetSystems::WriteEvents)
                                .set_names(std::array{std::format("send {} asset events", meta::type_id<T>::name())}));
            app.add_systems(PostUpdate, into(Assets<T>::handle_events)
                                            .in_set(AssetSystems::HandleEvents)
                                            .set_name(std::format("handle {} asset events", meta::type_id<T>::name())));
        });
        return *this;
    }
    template <typename T>
    AssetPlugin& register_loader(const T& t = T()) {
        m_assets_inserts.push_back([t](epix::App& app) { app.resource_mut<AssetServer>().register_loader(t); });
        return *this;
    }
    void build(epix::App& app);
    void finish(epix::App& app);
};
}  // namespace epix::assets