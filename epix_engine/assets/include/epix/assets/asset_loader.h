#pragma once

#include <epix/app.h>

#include "epix/assets/asset_io.h"
#include "epix/assets/assets.h"

namespace epix::assets {
template <typename T>
struct AssetLoader {
    std::shared_ptr<HandleProvider<T>> m_handle_provider;
    std::deque<std::pair<AssetIndex, std::string>> m_to_load;
    Receiver<std::pair<AssetIndex, T>> m_loaded;

    AssetLoader()
        : m_loaded(std::get<1>(
              epix::utils::async::make_channel<std::pair<AssetIndex, T>>()
          )) {
        m_handle_provider = std::make_shared<HandleProvider<T>>();
    }

    static void get_handle_provider(
        epix::ResMut<Assets<T>> assets, epix::ResMut<AssetLoader<T>> loader
    ) {
        loader->m_handle_provider = assets->get_handle_provider();
    }

    static void load_cached(
        epix::ResMut<AssetIO> io, epix::ResMut<AssetLoader<T>> loader
    ) {
        while (!loader->m_to_load.empty()) {
            auto&& [index, path] = loader->m_to_load.front();
            io->submit([sender = loader->m_loaded.create_sender(), index,
                        path]() mutable {
                if (auto asset = load(path)) {
                    sender.send(std::make_pair(index, std::move(*asset)));
                } else {
                    spdlog::error("Failed to load asset at {}", path);
                }
            });
            loader->m_to_load.pop_front();
        }
    }

    static void loaded(
        epix::ResMut<AssetLoader<T>> loader, epix::ResMut<Assets<T>> assets
    ) {
        while (auto&& opt = loader->m_loaded.try_receive()) {
            auto&& [index, asset] = *opt;
            assets->insert(index, std::move(asset));
        }
    }

    template <typename... Args>
        requires(sizeof...(Args) == 1 || sizeof...(Args) == 0)
    static std::optional<T> load(const std::string& path, Args&&... args);

    Handle<T> reserve(const std::string& path) {
        auto handle = m_handle_provider->reserve();
        m_to_load.emplace_back(handle, path);
        return handle;
    }
};
}  // namespace epix::assets