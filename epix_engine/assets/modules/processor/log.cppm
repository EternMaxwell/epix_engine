module;

#ifndef EPIX_IMPORT_STD
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

export module epix.assets:processor.log;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.utils;

import :path;

namespace epix::assets {

// ---- LogEntry ----

/** @brief An in-memory representation of a single ProcessorTransactionLog entry.
 *  Matches bevy_asset's LogEntry. */
export enum class LogEntryKind {
    BeginProcessing,
    EndProcessing,
    UnrecoverableError,
};

export struct LogEntry {
    LogEntryKind kind;
    std::optional<AssetPath> path;  // populated for Begin/End

    static LogEntry begin_processing(AssetPath p) { return {LogEntryKind::BeginProcessing, std::move(p)}; }
    static LogEntry end_processing(AssetPath p) { return {LogEntryKind::EndProcessing, std::move(p)}; }
    static LogEntry unrecoverable_error() { return {LogEntryKind::UnrecoverableError, std::nullopt}; }
};

// ---- ProcessorTransactionLogFactory ----

/** @brief A factory of ProcessorTransactionLog that handles the state before the log has been started.
 *  Matches bevy_asset's ProcessorTransactionLogFactory. */
export struct ProcessorTransactionLog;

export struct ProcessorTransactionLogFactory {
    virtual ~ProcessorTransactionLogFactory() = default;
    /** @brief Reads all entries in a previous transaction log if present. */
    virtual std::expected<std::vector<LogEntry>, std::string> read() const = 0;
    /** @brief Creates a new transaction log to write to. Removes previous entries if they exist. */
    virtual std::expected<std::unique_ptr<ProcessorTransactionLog>, std::string> create_new_log() const = 0;
    /** @brief Returns the path of the log file relative to the processed asset root, if applicable.
     *  Returns empty path if the log is not file-based. */
    virtual std::filesystem::path log_path() const { return {}; }
};

// ---- ProcessorTransactionLog ----

/** @brief A "write ahead" logger that helps ensure asset importing is transactional.
 *  Matches bevy_asset's ProcessorTransactionLog. */
export struct ProcessorTransactionLog {
    virtual ~ProcessorTransactionLog() = default;
    /** @brief Log the start of an asset being processed. */
    virtual std::expected<void, std::string> begin_processing(const AssetPath& asset) = 0;
    /** @brief Log the end of an asset being successfully processed. */
    virtual std::expected<void, std::string> end_processing(const AssetPath& asset) = 0;
    /** @brief Log an unrecoverable error. All assets will be reprocessed on next run. */
    virtual std::expected<void, std::string> unrecoverable() = 0;
};

// ---- LogEntryError ----

/** @brief An error that occurs when validating individual ProcessorTransactionLog entries.
 *  Matches bevy_asset's LogEntryError. */
export namespace log_entry_errors {
struct DuplicateTransaction {
    AssetPath path;
};
struct EndedMissingTransaction {
    AssetPath path;
};
struct UnfinishedTransaction {
    AssetPath path;
};
}  // namespace log_entry_errors

export using LogEntryError = std::variant<log_entry_errors::DuplicateTransaction,
                                          log_entry_errors::EndedMissingTransaction,
                                          log_entry_errors::UnfinishedTransaction>;

// ---- ValidateLogError ----

/** @brief An error that occurs when validating the ProcessorTransactionLog fails.
 *  Matches bevy_asset's ValidateLogError. */
export namespace validate_log_errors {
struct UnrecoverableError {};
struct ReadLogError {
    std::string msg;
};
struct EntryErrors {
    std::vector<LogEntryError> errors;
};
}  // namespace validate_log_errors

export using ValidateLogError = std::variant<validate_log_errors::UnrecoverableError,
                                             validate_log_errors::ReadLogError,
                                             validate_log_errors::EntryErrors>;

// ---- validate_transaction_log ----

/** @brief Validate the previous state of the transaction log and determine any assets that need reprocessing.
 *  Matches bevy_asset's validate_transaction_log. */
std::expected<void, ValidateLogError> validate_transaction_log(const ProcessorTransactionLogFactory& log_factory);

// ---- FileTransactionLogFactory ----

inline constexpr std::string_view LOG_PATH          = "imported_assets/log";
inline constexpr std::string_view ENTRY_BEGIN       = "Begin ";
inline constexpr std::string_view ENTRY_END         = "End ";
inline constexpr std::string_view UNRECOVERABLE_ERR = "UnrecoverableError";

/** @brief File-backed ProcessorTransactionLog implementation. */
struct FileProcessorTransactionLog : ProcessorTransactionLog {
    std::filesystem::path file_path;
    std::optional<std::ofstream> log_file;

    explicit FileProcessorTransactionLog(std::filesystem::path path) : file_path(std::move(path)) {}

    std::expected<void, std::string> ensure_open();
    std::expected<void, std::string> write_line(const std::string& line);

    std::expected<void, std::string> begin_processing(const AssetPath& asset) override;
    std::expected<void, std::string> end_processing(const AssetPath& asset) override;
    std::expected<void, std::string> unrecoverable() override;
};

/** @brief A transaction log factory that uses a file as its storage.
 *  Matches bevy_asset's FileTransactionLogFactory. */
export struct FileTransactionLogFactory : ProcessorTransactionLogFactory {
    std::filesystem::path file_path;

    FileTransactionLogFactory() : file_path(LOG_PATH) {}
    explicit FileTransactionLogFactory(std::filesystem::path path) : file_path(std::move(path)) {}

    std::filesystem::path log_path() const override { return file_path.filename(); }

    std::expected<std::vector<LogEntry>, std::string> read() const override;

    std::expected<std::unique_ptr<ProcessorTransactionLog>, std::string> create_new_log() const override;
};

}  // namespace epix::assets
