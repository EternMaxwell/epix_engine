module;

#include <gtest/gtest.h>

export module epix.assets;

export import :index;
export import :id;
export import :handle;
export import :store;
export import :server;

namespace assets {
export enum class AssetSystems {
    HandleEvents,
    WriteEvents,
};
export struct AssetPlugin {
   private:
    std::vector<std::function<void(App&)>> m_assets_inserts;

   public:
    template <std::movable T>
    AssetPlugin& register_asset();
    template <AssetLoader T>
    AssetPlugin& register_loader(const T& t = T());
    void build(App& app);
    void finish(App& app);
};

template <std::movable T>
AssetPlugin& AssetPlugin::register_asset() {
    m_assets_inserts.push_back([](App& app) {
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
template <AssetLoader T>
AssetPlugin& AssetPlugin::register_loader(const T& t) {
    m_assets_inserts.push_back([t](App& app) { app.resource_mut<AssetServer>().register_loader(t); });
    return *this;
}
}  // namespace assets