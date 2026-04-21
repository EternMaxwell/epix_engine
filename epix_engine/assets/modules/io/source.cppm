module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#endif

export module epix.assets:io.source;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

import :path;
import :io.reader;
import :io.file.asset;
import :io.file.watcher;

namespace epix::assets {
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

    friend struct AssetSourceBuilder;

   public:
    AssetSourceId id() const { return m_id; }
    const AssetReader& reader() const { return *m_reader; }  // this is always present
    std::optional<std::reference_wrapper<const AssetWriter>> writer() const;
    std::optional<std::reference_wrapper<const AssetReader>> processed_reader() const;
    std::optional<std::reference_wrapper<const AssetReader>> ungated_processed_reader() const;
    std::optional<std::reference_wrapper<const AssetWriter>> processed_writer() const;
    std::optional<std::reference_wrapper<const utils::Receiver<AssetSourceEvent>>> event_receiver() const;
    std::optional<std::reference_wrapper<const utils::Receiver<AssetSourceEvent>>> processed_event_receiver() const;
    bool should_process() const { return m_processed_writer != nullptr; }

    /** @brief Gate the processed reader through a factory function.
     *  Moves the current processed_reader to ungated_processed_reader,
     *  then creates a new gated processed_reader via the factory.
     *  The factory receives (source_id, reference_to_ungated_reader). */
    void gate_on_processor(
        utils::function_ref<std::unique_ptr<AssetReader>(AssetSourceId, const AssetReader&)> factory);

    static std::function<std::unique_ptr<AssetReader>()> get_default_reader(std::filesystem::path path);
    static std::function<std::unique_ptr<AssetWriter>()> get_default_writer(std::filesystem::path path);
    static std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)> get_default_watcher(
        std::filesystem::path path);
};

export struct AssetSourceBuilder {
    std::function<std::unique_ptr<AssetReader>()> reader_factory;                                   // required
    std::optional<std::function<std::unique_ptr<AssetWriter>()>> writer_factory;                    // optional
    std::optional<std::function<std::unique_ptr<AssetReader>()>> processed_reader_factory;          // optional
    std::optional<std::function<std::unique_ptr<AssetReader>()>> ungated_processed_reader_factory;  // optional
    std::optional<std::function<std::unique_ptr<AssetWriter>()>> processed_writer_factory;          // optional
    std::optional<std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)>>
        watcher_factory;  // optional
    std::optional<std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)>>
        processed_watcher_factory;  // optional

   public:
    AssetSourceBuilder(std::function<std::unique_ptr<AssetReader>()> reader_factory)
        : reader_factory(std::move(reader_factory)) {}
    static AssetSourceBuilder create(std::function<std::unique_ptr<AssetReader>()> reader_factory) {
        return AssetSourceBuilder(std::move(reader_factory));
    }

    AssetSource build(AssetSourceId id, bool watch, bool watch_processed);

    auto&& with_reader(this auto&& self, std::function<std::unique_ptr<AssetReader>()> factory) {
        self.reader_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_writer(this auto&& self, std::function<std::unique_ptr<AssetWriter>()> factory) {
        self.writer_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_processed_reader(this auto&& self, std::function<std::unique_ptr<AssetReader>()> factory) {
        self.processed_reader_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_ungated_processed_reader(this auto&& self, std::function<std::unique_ptr<AssetReader>()> factory) {
        self.ungated_processed_reader_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_processed_writer(this auto&& self, std::function<std::unique_ptr<AssetWriter>()> factory) {
        self.processed_writer_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_watcher(this auto&& self,
                        std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)> factory) {
        self.watcher_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_processed_watcher(
        this auto&& self, std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)> factory) {
        self.processed_watcher_factory = std::move(factory);
        return std::forward<decltype(self)>(self);
    }

    static AssetSourceBuilder platform_default(std::filesystem::path path,
                                               std::optional<std::filesystem::path> processed_path = std::nullopt);
};

export struct AssetSources {
   private:
    AssetSource m_default;
    std::unordered_map<std::string, AssetSource> m_sources;

    friend struct AssetSourceBuilders;

   public:
    std::optional<std::reference_wrapper<const AssetSource>> get(AssetSourceId name) const;
    auto iter() const {
        return std::views::join(std::ranges::owning_view(std::array<utils::input_iterable<const AssetSource&>, 2>{
            utils::input_iterable<const AssetSource&>(std::span(&m_default, &m_default + 1)),
            utils::input_iterable<const AssetSource&>(std::views::values(m_sources))}));
    }
    auto iter_mut() {
        return std::views::join(std::ranges::owning_view(std::array<utils::input_iterable<AssetSource&>, 2>{
            utils::input_iterable<AssetSource&>(std::span(&m_default, &m_default + 1)),
            utils::input_iterable<AssetSource&>(std::views::values(m_sources))}));
    }
    auto iter_processed() const {
        return std::views::filter(iter(), [](const AssetSource& source) { return source.should_process(); });
    }
    auto iter_processed_mut() {
        return std::views::filter(iter_mut(), [](const AssetSource& source) { return source.should_process(); });
    }
    auto ids() const {
        return std::views::transform(iter(), [](const AssetSource& source) { return source.id(); });
    }
    /** @brief Gate all processed sources through the given factory. */
    void gate_on_processor(
        utils::function_ref<std::unique_ptr<AssetReader>(AssetSourceId, const AssetReader&)> factory);
};

export struct AssetSourceBuilders {
   private:
    std::unordered_map<std::string, AssetSourceBuilder> m_sources;
    std::optional<AssetSourceBuilder> m_default;

   public:
    void insert(AssetSourceId id, AssetSourceBuilder builder);
    std::optional<std::reference_wrapper<AssetSourceBuilder>> get(const AssetSourceId& id);
    void init_default(std::filesystem::path path, std::optional<std::filesystem::path> processed_path = std::nullopt) {
        m_default = AssetSourceBuilder::platform_default(std::move(path), std::move(processed_path));
    }
    AssetSources build_sources(bool watch, bool watch_processed);
};

}  // namespace epix::assets