module;

#include <spdlog/spdlog.h>

export module epix.assets:processor.log;

import std;
import epix.utils;

import :path;

namespace assets {

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
export inline std::expected<void, ValidateLogError> validate_transaction_log(
    const ProcessorTransactionLogFactory& log_factory) {
    auto entries_result = log_factory.read();
    if (!entries_result) {
        return std::unexpected(ValidateLogError{validate_log_errors::ReadLogError{std::move(entries_result.error())}});
    }

    std::unordered_set<AssetPath> transactions;
    std::vector<LogEntryError> errors;

    for (auto& entry : *entries_result) {
        switch (entry.kind) {
            case LogEntryKind::BeginProcessing: {
                if (!transactions.insert(*entry.path).second) {
                    errors.push_back(LogEntryError{log_entry_errors::DuplicateTransaction{*entry.path}});
                }
                break;
            }
            case LogEntryKind::EndProcessing: {
                if (transactions.erase(*entry.path) == 0) {
                    errors.push_back(LogEntryError{log_entry_errors::EndedMissingTransaction{*entry.path}});
                }
                break;
            }
            case LogEntryKind::UnrecoverableError:
                return std::unexpected(ValidateLogError{validate_log_errors::UnrecoverableError{}});
        }
    }
    for (auto& transaction : transactions) {
        errors.push_back(LogEntryError{log_entry_errors::UnfinishedTransaction{transaction}});
    }
    if (!errors.empty()) {
        return std::unexpected(ValidateLogError{validate_log_errors::EntryErrors{std::move(errors)}});
    }
    return {};
}

// ---- FileTransactionLogFactory ----

inline constexpr std::string_view LOG_PATH          = "imported_assets/log";
inline constexpr std::string_view ENTRY_BEGIN       = "Begin ";
inline constexpr std::string_view ENTRY_END         = "End ";
inline constexpr std::string_view UNRECOVERABLE_ERR = "UnrecoverableError";

/** @brief File-backed ProcessorTransactionLog implementation. */
struct FileProcessorTransactionLog : ProcessorTransactionLog {
    std::ofstream log_file;

    explicit FileProcessorTransactionLog(std::ofstream file) : log_file(std::move(file)) {}

    std::expected<void, std::string> write_line(const std::string& line) {
        log_file << line << '\n';
        log_file.flush();
        if (!log_file.good()) {
            return std::unexpected("Failed to write to transaction log");
        }
        return {};
    }

    std::expected<void, std::string> begin_processing(const AssetPath& asset) override {
        return write_line(std::format("{}{}", ENTRY_BEGIN, asset.string()));
    }

    std::expected<void, std::string> end_processing(const AssetPath& asset) override {
        return write_line(std::format("{}{}", ENTRY_END, asset.string()));
    }

    std::expected<void, std::string> unrecoverable() override { return write_line(std::string(UNRECOVERABLE_ERR)); }
};

/** @brief A transaction log factory that uses a file as its storage.
 *  Matches bevy_asset's FileTransactionLogFactory. */
export struct FileTransactionLogFactory : ProcessorTransactionLogFactory {
    std::filesystem::path file_path;

    FileTransactionLogFactory() : file_path(LOG_PATH) {}
    explicit FileTransactionLogFactory(std::filesystem::path path) : file_path(std::move(path)) {}

    std::expected<std::vector<LogEntry>, std::string> read() const override {
        std::vector<LogEntry> log_lines;
        std::ifstream file(file_path);
        if (!file.is_open()) {
            // If the log file doesn't exist, this is equivalent to an empty file
            return log_lines;
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            if (line.starts_with(ENTRY_BEGIN)) {
                auto path_str = line.substr(ENTRY_BEGIN.size());
                log_lines.push_back(LogEntry::begin_processing(AssetPath(std::move(path_str))));
            } else if (line.starts_with(ENTRY_END)) {
                auto path_str = line.substr(ENTRY_END.size());
                log_lines.push_back(LogEntry::end_processing(AssetPath(std::move(path_str))));
            } else if (line == UNRECOVERABLE_ERR) {
                log_lines.push_back(LogEntry::unrecoverable_error());
            } else {
                return std::unexpected(std::format("Encountered an invalid log line: '{}'", line));
            }
        }
        return log_lines;
    }

    std::expected<std::unique_ptr<ProcessorTransactionLog>, std::string> create_new_log() const override {
        // Remove previous log file if it exists
        std::error_code ec;
        std::filesystem::remove(file_path, ec);
        // Ignore NotFound
        if (ec && ec != std::errc::no_such_file_or_directory) {
            spdlog::error("Failed to remove previous log file: {}", ec.message());
        }
        // Create parent directories
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path(), ec);
            if (ec) {
                return std::unexpected(std::format("Failed to create log directory: {}", ec.message()));
            }
        }
        std::ofstream file(file_path, std::ios::trunc);
        if (!file.is_open()) {
            return std::unexpected("Failed to create transaction log file");
        }
        return std::make_unique<FileProcessorTransactionLog>(std::move(file));
    }
};

}  // namespace assets
