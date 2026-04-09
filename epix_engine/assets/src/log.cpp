module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;
import epix.utils;

namespace epix::assets {
std::expected<void, ValidateLogError> validate_transaction_log(const ProcessorTransactionLogFactory& log_factory) {
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

std::expected<std::vector<LogEntry>, std::string> FileTransactionLogFactory::read() const {
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

std::expected<std::unique_ptr<ProcessorTransactionLog>, std::string> FileTransactionLogFactory::create_new_log() const {
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
}  // namespace epix::assets
