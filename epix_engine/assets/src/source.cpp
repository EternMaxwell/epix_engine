module;
#ifndef EPIX_IMPORT_STD
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#endif
module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

namespace epix::assets {

// ---- AssetSource --------------------------------------------------------

std::optional<std::reference_wrapper<const AssetWriter>> AssetSource::writer() const {
    if (m_writer) return *m_writer;
    return std::nullopt;
}

std::optional<std::reference_wrapper<const AssetReader>> AssetSource::processed_reader() const {
    if (m_processed_reader) return *m_processed_reader;
    return std::nullopt;
}

std::optional<std::reference_wrapper<const AssetReader>> AssetSource::ungated_processed_reader() const {
    if (m_ungated_processed_reader) return *m_ungated_processed_reader;
    return std::nullopt;
}

std::optional<std::reference_wrapper<const AssetWriter>> AssetSource::processed_writer() const {
    if (m_processed_writer) return *m_processed_writer;
    return std::nullopt;
}

std::optional<std::reference_wrapper<const utils::Receiver<AssetSourceEvent>>> AssetSource::event_receiver() const {
    if (m_event_receiver) return *m_event_receiver;
    return std::nullopt;
}

std::optional<std::reference_wrapper<const utils::Receiver<AssetSourceEvent>>> AssetSource::processed_event_receiver()
    const {
    if (m_processed_event_receiver) return *m_processed_event_receiver;
    return std::nullopt;
}

void AssetSource::gate_on_processor(
    utils::function_ref<std::unique_ptr<AssetReader>(AssetSourceId, const AssetReader&)> factory) {
    if (m_processed_reader) {
        m_ungated_processed_reader = std::move(m_processed_reader);
        m_processed_reader         = factory(m_id, *m_ungated_processed_reader);
    }
}

std::function<std::unique_ptr<AssetReader>()> AssetSource::get_default_reader(std::filesystem::path path) {
    return
        [path = std::move(path)]() -> std::unique_ptr<AssetReader> { return std::make_unique<FileAssetReader>(path); };
}

std::function<std::unique_ptr<AssetWriter>()> AssetSource::get_default_writer(std::filesystem::path path) {
    return
        [path = std::move(path)]() -> std::unique_ptr<AssetWriter> { return std::make_unique<FileAssetWriter>(path); };
}

std::function<std::unique_ptr<AssetWatcher>(utils::Sender<AssetSourceEvent>)> AssetSource::get_default_watcher(
    std::filesystem::path path) {
    return [path = std::move(path)](utils::Sender<AssetSourceEvent> sender) -> std::unique_ptr<AssetWatcher> {
        return std::make_unique<FileAssetWatcher>(path, std::move(sender));
    };
}

// ---- AssetSourceBuilder -------------------------------------------------

AssetSource AssetSourceBuilder::build(AssetSourceId id, bool watch, bool watch_processed) {
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

AssetSourceBuilder AssetSourceBuilder::platform_default(std::filesystem::path path,
                                                        std::optional<std::filesystem::path> processed_path) {
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

// ---- AssetSources -------------------------------------------------------

std::optional<std::reference_wrapper<const AssetSource>> AssetSources::get(AssetSourceId name) const {
    if (name.is_default()) return m_default;
    if (auto it = m_sources.find(name.value()); it != m_sources.end()) {
        return it->second;
    }
    return std::nullopt;
}

void AssetSources::gate_on_processor(
    utils::function_ref<std::unique_ptr<AssetReader>(AssetSourceId, const AssetReader&)> factory) {
    for (auto& source : iter_processed_mut()) {
        source.gate_on_processor(factory);
    }
}

// ---- AssetSourceBuilders ------------------------------------------------

void AssetSourceBuilders::insert(AssetSourceId id, AssetSourceBuilder builder) {
    if (!id.has_value()) {
        m_default = std::move(builder);
    } else {
        m_sources.emplace(std::move(id.value()), std::move(builder));
    }
}

std::optional<std::reference_wrapper<AssetSourceBuilder>> AssetSourceBuilders::get(const AssetSourceId& id) {
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

AssetSources AssetSourceBuilders::build_sources(bool watch, bool watch_processed) {
    AssetSources sources;
    if (m_default) {
        sources.m_default = m_default->build(std::nullopt, watch, watch_processed);
    }
    for (auto& [id, builder] : m_sources) {
        sources.m_sources.emplace(id, builder.build(id, watch, watch_processed));
    }
    return sources;
}

}  // namespace epix::assets
