#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <efsw/efsw.hpp>
#include <filesystem>
#include <memory>
#endif

#ifndef EPIX_CXX_MODULE
#include <epix/meta.hpp>
#endif
#ifndef EPIX_CXX_MODULE
#include <epix/utils.hpp>
#endif
#include <epix/assets/async_channel.hpp>
#include <epix/assets/io/reader.hpp>

namespace epix::assets {
EPIX_EXPORT struct FileAssetWatcher : public AssetWatcher {
   private:
    std::unique_ptr<efsw::FileWatcher> m_watcher;
    std::unique_ptr<efsw::FileWatchListener> m_listener;

   public:
    FileAssetWatcher(std::filesystem::path root, async_channel::Sender<AssetSourceEvent> event_sender);
};
}  // namespace epix::assets