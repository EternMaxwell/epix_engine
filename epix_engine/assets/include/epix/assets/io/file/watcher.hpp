#pragma once

#ifndef EPIX_CXX_MODULE
#include <efsw/efsw.hpp>
#include <epix/async_channel.hpp>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <epix/utils.hpp>
#include <filesystem>
#include <memory>
#endif
#include <epix/assets/io/reader.hpp>

namespace epix::assets {
EPIX_EXPORT struct EPIX_API FileAssetWatcher : public AssetWatcher {
   private:
    std::unique_ptr<efsw::FileWatchListener> m_listener;
    std::unique_ptr<efsw::FileWatcher> m_watcher;

   public:
    FileAssetWatcher(std::filesystem::path root, epix::async_channel::Sender<AssetSourceEvent> event_sender);
};
}  // namespace epix::assets
