#pragma once

#include <efsw/efsw.hpp>
#include <epix/assets/async_channel.hpp>
#include <epix/assets/io/reader.hpp>
#include <epix/meta.hpp>
#include <epix/utils.hpp>
#include <filesystem>
#include <memory>

namespace epix::assets {
struct FileAssetWatcher : public AssetWatcher {
   private:
    std::unique_ptr<efsw::FileWatcher> m_watcher;
    std::unique_ptr<efsw::FileWatchListener> m_listener;

   public:
    FileAssetWatcher(std::filesystem::path root, async_channel::Sender<AssetSourceEvent> event_sender);
};
}  // namespace epix::assets