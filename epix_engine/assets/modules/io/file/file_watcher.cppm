module;

#ifndef EPIX_IMPORT_STD
#include <filesystem>
#include <memory>
#endif
#include <efsw/efsw.hpp>

export module epix.assets:io.file.watcher;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.utils;

import :io.reader;

namespace epix::assets {
export struct FileAssetWatcher : public AssetWatcher {
   private:
    std::unique_ptr<efsw::FileWatcher> m_watcher;
    std::unique_ptr<efsw::FileWatchListener> m_listener;

   public:
    FileAssetWatcher(std::filesystem::path root, utils::Sender<AssetSourceEvent> event_sender);
};
}  // namespace epix::assets