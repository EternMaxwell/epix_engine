module;

#ifndef EPIX_IMPORT_STD
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#endif
#include <efsw/efsw.hpp>

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

namespace epix::assets {

FileAssetWatcher::FileAssetWatcher(std::filesystem::path root, async_channel::Sender<AssetSourceEvent> event_sender) {
    m_watcher = std::make_unique<efsw::FileWatcher>();
    struct Listener : public efsw::FileWatchListener {
        async_channel::Sender<AssetSourceEvent> sender;
        Listener(async_channel::Sender<AssetSourceEvent> sender) : sender(std::move(sender)) {}
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
                        (void)sender.try_send(AssetSourceEvent(source_events::AddedDirectory{full_path}));
                    } else if (is_meta) {
                        (void)sender.try_send(AssetSourceEvent(source_events::AddedMeta{full_path}));
                    } else {
                        (void)sender.try_send(AssetSourceEvent(source_events::AddedAsset{full_path}));
                    }
                    break;
                case efsw::Action::Modified:
                    if (!exists) {
                        break;
                    } else if (is_directory) {
                        // we shouldn't have modified events for directories, but just in case
                    } else if (is_meta) {
                        (void)sender.try_send(AssetSourceEvent(source_events::ModifiedMeta{full_path}));
                    } else {
                        (void)sender.try_send(AssetSourceEvent(source_events::ModifiedAsset{full_path}));
                    }
                    break;
                case efsw::Action::Delete:
                    if (is_directory) {
                        (void)sender.try_send(AssetSourceEvent(source_events::RemovedDirectory{full_path}));
                    } else if (is_meta) {
                        (void)sender.try_send(AssetSourceEvent(source_events::RemovedMeta{full_path}));
                    } else {
                        (void)sender.try_send(AssetSourceEvent(source_events::RemovedAsset{full_path}));
                    }
                    break;
                case efsw::Action::Moved:
                    if (old_full_path) {
                        if (is_directory) {
                            (void)sender.try_send(AssetSourceEvent(source_events::RenamedDirectory{*old_full_path, full_path}));
                        } else if (is_meta) {
                            (void)sender.try_send(AssetSourceEvent(source_events::RenamedMeta{*old_full_path, full_path}));
                        } else {
                            (void)sender.try_send(AssetSourceEvent(source_events::RenamedAsset{*old_full_path, full_path}));
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

}  // namespace epix::assets
