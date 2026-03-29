module;

#include <efsw/efsw.hpp>

export module epix.assets:io.file.watcher;

import std;
import epix.meta;
import epix.utils;

import :io.reader;

namespace epix::assets {
export struct FileAssetWatcher : public AssetWatcher {
   private:
    std::unique_ptr<efsw::FileWatcher> m_watcher;
    std::unique_ptr<efsw::FileWatchListener> m_listener;

   public:
    FileAssetWatcher(std::filesystem::path root, utils::Sender<AssetSourceEvent> event_sender) {
        m_watcher = std::make_unique<efsw::FileWatcher>();
        struct Listener : public efsw::FileWatchListener {
            utils::Sender<AssetSourceEvent> sender;
            Listener(utils::Sender<AssetSourceEvent> sender) : sender(std::move(sender)) {}
            void handleFileAction(efsw::WatchID watchid,
                                  const std::string& dir,
                                  const std::string& filename,
                                  efsw::Action action,
                                  std::string oldFilename) override {
                std::filesystem::path dir_path(dir);
                auto full_path = dir_path / filename;
                std::optional<std::filesystem::path> old_full_path;
                if (!oldFilename.empty()) {
                    old_full_path = dir_path / oldFilename;
                }
                bool is_meta      = full_path.extension() == ".meta";
                bool is_directory = std::filesystem::is_directory(full_path);
                bool exists       = std::filesystem::exists(full_path);
                switch (action) {
                    case efsw::Action::Add:
                        if (!exists) {
                            break;
                        } else if (is_directory) {
                            sender.send(AssetSourceEvent(source_events::AddedDirectory{full_path}));
                        } else if (is_meta) {
                            sender.send(AssetSourceEvent(source_events::AddedMeta{full_path}));
                        } else {
                            sender.send(AssetSourceEvent(source_events::AddedAsset{full_path}));
                        }
                        break;
                    case efsw::Action::Modified:
                        if (!exists) {
                            break;
                        } else if (is_directory) {
                            // we shouldn't have modified events for directories, but just in case
                        } else if (is_meta) {
                            sender.send(AssetSourceEvent(source_events::ModifiedMeta{full_path}));
                        } else {
                            sender.send(AssetSourceEvent(source_events::ModifiedAsset{full_path}));
                        }
                        break;
                    case efsw::Action::Delete:
                        if (is_directory) {
                            sender.send(AssetSourceEvent(source_events::RemovedDirectory{full_path}));
                        } else if (is_meta) {
                            sender.send(AssetSourceEvent(source_events::RemovedMeta{full_path}));
                        } else {
                            sender.send(AssetSourceEvent(source_events::RemovedAsset{full_path}));
                        }
                        break;
                    case efsw::Action::Moved:
                        if (old_full_path) {
                            if (is_directory) {
                                sender.send(
                                    AssetSourceEvent(source_events::RenamedDirectory{*old_full_path, full_path}));
                            } else if (is_meta) {
                                sender.send(AssetSourceEvent(source_events::RenamedMeta{*old_full_path, full_path}));
                            } else {
                                sender.send(AssetSourceEvent(source_events::RenamedAsset{*old_full_path, full_path}));
                            }
                        }
                        break;
                }
            }
        };
        m_listener = std::make_unique<Listener>(std::move(event_sender));
        m_watcher->addWatch(root.string(), m_listener.get(), true);
        m_watcher->watch();
    }
};
}  // namespace assets