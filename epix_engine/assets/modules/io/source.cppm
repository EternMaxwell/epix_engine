module;

export module epix.assets:io.source;

import std;
import epix.meta;
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

    /** @brief Gate the processed reader through a factory function.
     *  Moves the current processed_reader to ungated_processed_reader,
     *  then creates a new gated processed_reader via the factory.
     *  The factory receives (source_id, reference_to_ungated_reader). */
    void gate_on_processor(std::function<std::unique_ptr<AssetReader>(AssetSourceId, const AssetReader&)> factory) {
        if (m_processed_reader) {
            m_ungated_processed_reader = std::move(m_processed_reader);
            m_processed_reader         = factory(m_id, *m_ungated_processed_reader);
        }
    }

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

    AssetSource build(AssetSourceId id, bool watch, bool watch_processed) {
        AssetSource source;
        source.m_id     = std::move(id);
        source.m_reader = reader_factory();
        if (writer_factory) {
            source.m_writer = (*writer_factory)();
        }
        if (processed_reader_factory) {
            source.m_processed_reader = (*processed_reader_factory)();
        }
        if (ungated_processed_reader_factory) {
            source.m_ungated_processed_reader = (*ungated_processed_reader_factory)();
        }
        if (processed_writer_factory) {
            source.m_processed_writer = (*processed_writer_factory)();
        }
        if (watch && watcher_factory) {
            auto [sender, receiver] = utils::make_channel<AssetSourceEvent>();
            source.m_watcher        = (*watcher_factory)(std::move(sender));
            source.m_event_receiver = std::move(receiver);
        }
        if (watch_processed && processed_watcher_factory) {
            auto [sender, receiver]           = utils::make_channel<AssetSourceEvent>();
            source.m_processed_watcher        = (*processed_watcher_factory)(std::move(sender));
            source.m_processed_event_receiver = std::move(receiver);
        }
        return source;
    }

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
                                               std::optional<std::filesystem::path> processed_path = std::nullopt) {
        auto builder = AssetSourceBuilder::create(AssetSource::get_default_reader(path))
                           .with_writer(AssetSource::get_default_writer(path))
                           .with_watcher(AssetSource::get_default_watcher(path));
        if (processed_path) {
            builder.with_processed_reader(AssetSource::get_default_reader(*processed_path))
                .with_processed_writer(AssetSource::get_default_writer(*processed_path))
                .with_processed_watcher(AssetSource::get_default_watcher(*processed_path));
        }
        return builder;
    }
};

export struct AssetSources {
   private:
    AssetSource m_default;
    std::unordered_map<std::string, AssetSource> m_sources;

    friend struct AssetSourceBuilders;

   public:
    std::optional<std::reference_wrapper<const AssetSource>> get(AssetSourceId name) const {
        if (name.is_default()) return m_default;
        if (auto it = m_sources.find(name.value()); it != m_sources.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    auto iter() const {
        return std::views::join(std::ranges::owning_view(std::array<utils::input_iterable<const AssetSource&>, 2>{
            utils::input_iterable<const AssetSource&>(std::span(&m_default, &m_default + 1)),
            utils::input_iterable<const AssetSource&>(m_sources | std::views::values)}));
    }
    auto iter_mut() {
        return std::views::join(std::ranges::owning_view(std::array<utils::input_iterable<AssetSource&>, 2>{
            utils::input_iterable<AssetSource&>(std::span(&m_default, &m_default + 1)),
            utils::input_iterable<AssetSource&>(m_sources | std::views::values)}));
    }
    auto iter_processed() const {
        return iter() | std::views::filter([](const AssetSource& source) { return source.should_process(); });
    }
    auto iter_processed_mut() {
        return iter_mut() | std::views::filter([](const AssetSource& source) { return source.should_process(); });
    }
    auto ids() const {
        return iter() | std::views::transform([](const AssetSource& source) { return source.id(); });
    }
    /** @brief Gate all processed sources through the given factory. */
    void gate_on_processor(std::function<std::unique_ptr<AssetReader>(AssetSourceId, const AssetReader&)> factory) {
        for (auto& source : iter_processed_mut()) {
            source.gate_on_processor(factory);
        }
    }
};

export struct AssetSourceBuilders {
   private:
    std::unordered_map<std::string, AssetSourceBuilder> m_sources;
    std::optional<AssetSourceBuilder> m_default;

   public:
    void insert(AssetSourceId id, AssetSourceBuilder builder) {
        if (!id.has_value()) {
            m_default = std::move(builder);
        } else {
            m_sources.emplace(std::move(id.value()), std::move(builder));
        }
    }
    std::optional<std::reference_wrapper<AssetSourceBuilder>> get(const AssetSourceId& id) {
        if (!id.has_value()) {
            if (m_default) return *m_default;
            return std::nullopt;
        }
        auto it = m_sources.find(id.value());
        if (it != m_sources.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    void init_default(std::filesystem::path path, std::optional<std::filesystem::path> processed_path = std::nullopt) {
        m_default = AssetSourceBuilder::platform_default(std::move(path), std::move(processed_path));
    }
    AssetSources build_sources(bool watch, bool watch_processed) {
        AssetSources sources;
        if (m_default) {
            sources.m_default = m_default->build(std::nullopt, watch, watch_processed);
        }
        for (auto& [id, builder] : m_sources) {
            sources.m_sources.emplace(id, builder.build(id, watch, watch_processed));
        }
        return sources;
    }
};

}  // namespace epix::assets