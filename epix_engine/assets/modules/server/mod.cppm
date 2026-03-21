module;

export module epix.assets:server;

import std;
import epix.meta;
import epix.utils;

import :server.info;
import :server.loader;
import :server.loaders;

namespace assets {
struct AssetServerData {
    utils::RwLock<AssetInfos> infos;
    utils::RwLock<AssetLoaders> loaders;
    utils::Sender<InternalAssetEvent> asset_event_sender;
    utils::Receiver<InternalAssetEvent> asset_event_receiver;
};
struct AssetServer {
    std::shared_ptr<AssetServerData> data;

    AssetServer() : data(std::make_shared<AssetServerData>()) {}
    AssetServer(const AssetServer&)            = default;
    AssetServer(AssetServer&&)                 = default;
    AssetServer& operator=(const AssetServer&) = default;
    AssetServer& operator=(AssetServer&&)      = default;
};
}  // namespace assets