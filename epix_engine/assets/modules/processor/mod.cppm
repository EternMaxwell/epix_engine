module;

#include <spdlog/spdlog.h>

#include <asio/awaitable.hpp>

export module epix.assets:processor;

import std;
import epix.meta;
import epix.utils;
import epix.core;

import :path;
import :meta;
import :io.reader;
import :io.source;
import :server.loader;
import :server;
import :processor.process;
import :processor.log;

namespace epix::assets {

// ---- ProcessorState ----

/** @brief The current state of the AssetProcessor.
 *  Matches bevy_asset's ProcessorState. */
export enum class ProcessorState {
    Initializing,
    Processing,
    Finished,
};

// ---- Processors registry ----

/** @brief Entry in the short_type_path_to_processor map.
 *  Matches bevy_asset's ShortTypeProcessorEntry. */
struct ShortTypeProcessorEntry {
    struct Unique {
        std::string_view type_path;
        std::shared_ptr<ErasedProcessor> processor;
    };
    struct Ambiguous {
        std::vector<std::string_view> type_paths;
    };
    std::variant<Unique, Ambiguous> entry;
};

/** @brief Maps processor type names to their type-erased instances, and extensions to default processors.
 *  Matches bevy_asset's Processors. */
struct Processors {
    std::unordered_map<std::string_view, std::shared_ptr<ErasedProcessor>> type_path_to_processor;
    std::unordered_map<std::string_view, ShortTypeProcessorEntry> short_type_path_to_processor;
    std::unordered_map<std::string, std::string_view> file_extension_to_default_processor;
};

// ---- ProcessorAssetInfo ----

/** @brief Per-asset processing state tracked by the processor.
 *  Matches bevy_asset's ProcessorAssetInfo. */
struct ProcessorAssetInfo {
    std::optional<ProcessedInfo> processed_info;
    std::unordered_set<AssetPath> dependents;
    std::optional<ProcessStatus> status;
    /** @brief A lock that controls read/write access to processed asset files.
     *  Shared for both asset bytes and meta bytes. */
    std::shared_ptr<std::shared_mutex> file_transaction_lock = std::make_shared<std::shared_mutex>();
    utils::BroadcastSender<ProcessStatus> status_sender;
    utils::BroadcastReceiver<ProcessStatus> status_receiver;

    ProcessorAssetInfo();

    void update_status(ProcessStatus new_status);
};

// ---- ProcessorAssetInfos ----

/** @brief The "current" in-memory view of the asset space.
 *  Matches bevy_asset's ProcessorAssetInfos. */
struct ProcessorAssetInfos {
    std::unordered_map<AssetPath, ProcessorAssetInfo> infos;
    std::unordered_map<AssetPath, std::unordered_set<AssetPath>> non_existent_dependents;

    ProcessorAssetInfo& get_or_insert(const AssetPath& asset_path);

    ProcessorAssetInfo* get(const AssetPath& asset_path);

    const ProcessorAssetInfo* get(const AssetPath& asset_path) const;

    void add_dependent(const AssetPath& asset_path, AssetPath dependent);

    /** @brief Remove an asset from tracking. Returns the transaction lock if it existed. */
    std::shared_ptr<std::shared_mutex> remove(const AssetPath& asset_path);

    /** @brief Rename an asset in tracking, preserving status and requeueing affected work.
     *  Matches bevy_asset's ProcessorAssetInfos::rename. */
    std::optional<std::pair<std::shared_ptr<std::shared_mutex>, std::shared_ptr<std::shared_mutex>>> rename(
        const AssetPath& old_path,
        const AssetPath& new_path,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& reprocess_sender);

    /** @brief Finalize processing for an asset, incorporating the result. */
    void finish_processing(const AssetPath& asset_path,
                           std::expected<ProcessResult, ProcessError>& result,
                           utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& reprocess_sender);

    void clear_dependencies(const AssetPath& asset_path, const ProcessedInfo& removed_info);
};

// ---- ProcessingState ----

/** @brief The current state of processing, including the overall state and the state of all assets.
 *  Matches bevy_asset's ProcessingState. Uses blocking primitives instead of async. */
struct ProcessingState {
   private:
    mutable utils::RwLock<ProcessorState> m_state{ProcessorState::Initializing};

    utils::BroadcastSender<bool> m_initialized_sender;
    utils::BroadcastReceiver<bool> m_initialized_receiver;
    utils::BroadcastSender<bool> m_finished_sender;
    utils::BroadcastReceiver<bool> m_finished_receiver;

    mutable utils::RwLock<ProcessorAssetInfos> m_asset_infos;

    friend struct AssetProcessor;
    friend struct AssetProcessorData;

   public:
    ProcessingState();

    /** @brief Set the overall state of processing and broadcast appropriate events. */
    void set_state(ProcessorState state);
    /** @brief Retrieve the current ProcessorState. */
    ProcessorState get_state() const;

    /** @brief Block until the path has been processed. Returns the ProcessStatus. */
    ProcessStatus wait_until_processed(const AssetPath& path) const;
    /** @brief Get a transaction lock for the given asset path (shared read lock).
     *  Used by ProcessorGatedReader to hold the lock while reading. */
    std::expected<std::shared_ptr<std::shared_mutex>, AssetReaderError> get_transaction_lock(
        const AssetPath& path) const;
    /** @brief Block until the processor has been initialized. */
    void wait_until_initialized() const;
    /** @brief Block until processing has finished. */
    void wait_until_finished() const;
    /** @brief Close all wait channels so blocked receivers wake and exit. */
    void shutdown();
};

// ---- AssetProcessorData ----

/** @brief Error when updating the transaction log factory after the processor has started.
 *  Matches bevy_asset's SetTransactionLogFactoryError. */
export namespace set_transaction_log_factory_errors {
struct AlreadyInUse {};
}  // namespace set_transaction_log_factory_errors

export using SetTransactionLogFactoryError = std::variant<set_transaction_log_factory_errors::AlreadyInUse>;

/** @brief Shared data for the AssetProcessor, accessible across threads.
 *  Matches bevy_asset's AssetProcessorData. */
export struct AssetProcessorData {
   private:
    std::shared_ptr<ProcessingState> processing_state;
    utils::Mutex<std::optional<std::unique_ptr<ProcessorTransactionLogFactory>>> log_factory;
    utils::RwLock<std::optional<std::unique_ptr<ProcessorTransactionLog>>> log;
    utils::RwLock<Processors> processors;
    std::shared_ptr<AssetSources> sources;
    struct TaskSenderState {
        std::optional<utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>> sender;
        bool shutdown_requested = false;
    };
    utils::Mutex<TaskSenderState> task_sender;

    AssetProcessorData(std::shared_ptr<AssetSources> sources, std::shared_ptr<ProcessingState> processing_state);
    AssetProcessorData(std::shared_ptr<AssetSources> sources,
                       std::shared_ptr<ProcessingState> processing_state,
                       std::unique_ptr<ProcessorTransactionLogFactory> log_factory);

    friend struct AssetProcessor;

    void set_task_sender(utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> sender) const;
    void shutdown() const;

   public:
    std::expected<void, SetTransactionLogFactoryError> set_log_factory(
        std::unique_ptr<ProcessorTransactionLogFactory> factory) const;
    ProcessStatus wait_until_processed(const AssetPath& path) const;
    void wait_until_initialized() const;
    void wait_until_finished() const;
    ProcessorState state() const;
};

// ---- AssetProcessor ----

/** @brief The main asset processor. Processes assets from source readers into processed writers.
 *  Matches bevy_asset's AssetProcessor. */
export struct AssetProcessor {
   private:
    AssetServer server;
    std::shared_ptr<AssetProcessorData> data;
    bool m_owns_shutdown = true;

    AssetProcessor(AssetServer srv, std::shared_ptr<AssetProcessorData> proc_data);

    asio::awaitable<void> initialize() const;
    asio::awaitable<void> process_asset(
        const AssetSourceId& source,
        const std::filesystem::path& path,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> reprocess_sender) const;
    asio::awaitable<std::expected<ProcessResult, ProcessError>> process_asset_internal(
        const AssetSource& source, const AssetPath& asset_path) const;
    asio::awaitable<void> handle_asset_source_event(
        const AssetSource& source,
        const AssetSourceEvent& event,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    asio::awaitable<void> handle_added_folder(
        const AssetSource& source,
        const std::filesystem::path& path,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    void handle_removed_meta(const AssetSource& source,
                             const std::filesystem::path& path,
                             utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    asio::awaitable<void> handle_removed_asset(const AssetSource& source, const std::filesystem::path& path) const;
    asio::awaitable<void> handle_removed_folder(const AssetSource& source, const std::filesystem::path& path) const;
    asio::awaitable<void> handle_renamed_asset(
        const AssetSource& source,
        const std::filesystem::path& old_path,
        const std::filesystem::path& new_path,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    asio::awaitable<void> queue_processing_tasks_for_folder(
        const AssetSource& source,
        const std::filesystem::path& folder,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    asio::awaitable<void> queue_initial_processing_tasks(
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    void spawn_source_change_event_listeners(
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;
    void execute_processing_tasks(utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> new_task_sender,
                                  utils::Receiver<std::pair<AssetSourceId, std::filesystem::path>>& receiver) const;
    void log_begin_processing(const AssetPath& path) const;
    void log_end_processing(const AssetPath& path) const;
    void log_unrecoverable() const;
    asio::awaitable<std::filesystem::path> validate_transaction_log_and_recover() const;
    asio::awaitable<void> remove_processed_asset_and_meta(const AssetSource& source,
                                                          const std::filesystem::path& path) const;
    asio::awaitable<void> clean_empty_processed_ancestor_folders(const AssetSource& source,
                                                                 const std::filesystem::path& path) const;
    asio::awaitable<void> write_default_meta_file_for_path(const AssetSource& source,
                                                           const AssetPath& asset_path) const;

   public:
    AssetProcessor(const AssetProcessor& other);
    AssetProcessor(AssetProcessor&& other) noexcept;
    AssetProcessor& operator=(const AssetProcessor& other);
    AssetProcessor& operator=(AssetProcessor&& other) noexcept;
    ~AssetProcessor();

    /** @brief Construct a new processor from source builders and an explicit transaction log factory. */
    AssetProcessor(
        std::reference_wrapper<AssetSourceBuilders> builders,
        bool watching_for_changes,
        std::unique_ptr<ProcessorTransactionLogFactory> log_factory = std::make_unique<FileTransactionLogFactory>());

    /** @brief Get a reference to the internal AssetServer. */
    const AssetServer& get_server() const;

    /** @brief Get a reference to the processor data. */
    const std::shared_ptr<AssetProcessorData>& get_data() const;

    /** @brief Get a source by id from the internal server. */
    std::optional<std::reference_wrapper<const AssetSource>> get_source(const AssetSourceId& source_id) const {
        return server.get_source(source_id);
    }

    /** @brief Get all sources from the internal server. */
    const std::shared_ptr<AssetSources>& sources() const;

    // ---- Processor Registration (templates, inline) ----

    /** @brief Register a Process implementation.
     *  Matches bevy_asset's AssetProcessor::register_processor. */
    template <Process P>
    void register_processor(P processor) const {
        auto erased = std::make_shared<ErasedProcessorImpl<P>>(std::move(processor));
        auto guard  = data->processors.write();
        auto tp     = erased->type_path();
        auto stp    = erased->short_type_path();
        guard->type_path_to_processor.emplace(tp, erased);
        auto it = guard->short_type_path_to_processor.find(stp);
        if (it == guard->short_type_path_to_processor.end()) {
            guard->short_type_path_to_processor.emplace(
                stp, ShortTypeProcessorEntry{ShortTypeProcessorEntry::Unique{tp, erased}});
        } else {
            std::visit(utils::visitor{
                           [&](ShortTypeProcessorEntry::Unique& u) {
                               auto old_tp = u.type_path;
                               it->second  = ShortTypeProcessorEntry{ShortTypeProcessorEntry::Ambiguous{{old_tp, tp}}};
                           },
                           [&](ShortTypeProcessorEntry::Ambiguous& a) { a.type_paths.push_back(tp); },
                       },
                       it->second.entry);
        }
    }

    /** @brief Set the default processor for a given extension.
     *  Matches bevy_asset's AssetProcessor::set_default_processor. */
    template <Process P>
    void set_default_processor(std::string_view extension) const {
        auto guard = data->processors.write();
        // Find the processor name for P
        for (auto& [name, proc] : guard->type_path_to_processor) {
            // If P is registered, map extension to its name
            if (auto* impl = dynamic_cast<ErasedProcessorImpl<P>*>(proc.get())) {
                guard->file_extension_to_default_processor[std::string(extension)] = name;
                return;
            }
        }
        spdlog::warn("Cannot set default processor: type not registered");
    }

    /** @brief Get the default processor for a given extension. */
    std::shared_ptr<ErasedProcessor> get_default_processor(std::string_view extension) const;

    /** @brief Check if an extension has a registered default processor. */
    bool has_processor_for_extension(std::string_view ext) const { return bool(get_default_processor(ext)); }

    /** @brief Get a processor by type name (supports both short and full type path).
     *  Matches bevy_asset's AssetProcessor::get_processor. */
    std::expected<std::shared_ptr<ErasedProcessor>, GetProcessorError> get_processor(std::string_view type_name) const;

    // ---- Processing lifecycle (declared, defined in .cpp) ----

    /** @brief Start the processor. This is the main entry point, typically called as a system.
     *  Spawns background tasks to initialize and process assets.
     *  Matches bevy_asset's AssetProcessor::start. */
    static void start(core::Res<AssetProcessor> processor);
};

inline const AssetServer& ProcessContext::asset_server() const { return m_processor->get_server(); }

}  // namespace epix::assets