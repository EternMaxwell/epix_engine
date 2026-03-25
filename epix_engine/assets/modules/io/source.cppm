module;

export module epix.assets:io.source;

import std;
import epix.meta;
import epix.utils;

import :path;
import :io.reader;
import :io.file.asset;
import :io.file.watcher;

namespace assets {
export struct AssetSource {
   private:
    AssetSourceId m_id;
    std::unique_ptr<AssetReader> m_reader;                    // required
    std::unique_ptr<AssetWriter> m_writer;                    // optional
    std::unique_ptr<AssetReader> m_processed_reader;          // optional
    std::unique_ptr<AssetReader> m_ungated_processed_reader;  // optional
    std::unique_ptr<AssetWriter> m_processed_writer;          // optional
    std::unique_ptr<AssetWatcher> m_watcher;                  // optional
    std::unique_ptr<AssetWatcher> m_processed_watcher;        // optional
    std::optional<utils::Receiver<AssetSourceEvent>> m_event_receiver;
    std::optional<utils::Receiver<AssetSourceEvent>> m_processed_event_receiver;

   public:
    AssetSourceId id() const { return m_id; }
    const AssetReader& reader() const { return *m_reader; }  // this is always present
    std::optional<std::reference_wrapper<const AssetWriter>> writer() const {
        if (m_writer) return *m_writer;
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const AssetReader>> processed_reader() const {
        if (m_processed_reader) return *m_processed_reader;
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const AssetReader>> ungated_processed_reader() const {
        if (m_ungated_processed_reader) return *m_ungated_processed_reader;
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const AssetWriter>> processed_writer() const {
        if (m_processed_writer) return *m_processed_writer;
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const utils::Receiver<AssetSourceEvent>>> event_receiver() const {
        if (m_event_receiver) return *m_event_receiver;
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const utils::Receiver<AssetSourceEvent>>> processed_event_receiver() const {
        if (m_processed_event_receiver) return *m_processed_event_receiver;
        return std::nullopt;
    }
    bool should_process() const { return m_processed_writer != nullptr; }

    static std::function<std::unique_ptr<AssetReader>()> get_default_reader(std::filesystem::path path) {
        return [path = std::move(path)]() -> std::unique_ptr<AssetReader> {
            return std::make_unique<FileAssetReader>(path);
        };
    }
    static std::function<std::unique_ptr<AssetWriter>()> get_default_writer(std::filesystem::path path) {
        return [path = std::move(path)]() -> std::unique_ptr<AssetWriter> {
            return std::make_unique<FileAssetWriter>(path);
        };
    }
    static std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)> get_default_watcher(
        std::filesystem::path path) {
        return [path = std::move(path)](utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
            return std::make_unique<FileAssetWatcher>(path, std::move(sender));
        };
    }
};

export struct AssetSources {
   private:
    AssetSource m_default;
    std::unordered_map<std::string, AssetSource> m_sources;

   public:
    std::optional<std::reference_wrapper<const AssetSource>> get_source(const std::string_view& name) const {
        if (name == "default") {
            return m_default;
        }
        auto it = m_sources.find(name.data());
        if (it != m_sources.end()) {
            return it->second;
        }
        return std::nullopt;
    }
};
}  // namespace assets