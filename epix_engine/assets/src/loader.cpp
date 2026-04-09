module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;
import epix.meta;
import epix.utils;

namespace epix::assets {
std::optional<std::reference_wrapper<const ErasedLoadedAsset>> ErasedLoadedAsset::get_labeled(
    const std::string& label) const {
    auto it = labeled_assets.find(label);
    if (it == labeled_assets.end()) return std::nullopt;
    return std::cref(it->second.asset);
}
std::optional<std::reference_wrapper<const ErasedLoadedAsset>> ErasedLoadedAsset::get_labeled_by_id(
    const UntypedAssetId& id) const {
    for (const auto& [_, labeled] : labeled_assets) {
        if (labeled.handle.id() == id) return std::cref(labeled.asset);
    }
    return std::nullopt;
}
std::vector<std::string_view> ErasedLoadedAsset::labels() const {
    std::vector<std::string_view> result;
    result.reserve(labeled_assets.size());
    for (const auto& [label, _] : labeled_assets) result.push_back(label);
    return result;
}

std::variant<std::string, std::exception_ptr> format_asset_load_error(const AssetLoadError& error) {
    return std::visit(
        utils::visitor{
            [](const load_error::RequestHandleMismatch& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Type mismatch for '{}': requested '{}' but loader '{}' produces '{}'",
                                   e.path.string(), e.requested_type.short_name(), e.loader_name,
                                   e.actual_type.short_name());
            },
            [](const load_error::MissingAssetLoader& e) -> std::variant<std::string, std::exception_ptr> {
                if (!e.extension.empty()) {
                    return std::format(
                        "No loader for '{}' (extension(s): [{}])", e.path.string(),
                        std::accumulate(std::next(e.extension.begin()), e.extension.end(), e.extension.front(),
                                        [](std::string a, const std::string& b) { return std::move(a) + ", " + b; }));
                } else if (e.asset_type) {
                    return std::format("No loader for '{}' (asset type: '{}')", e.path.string(),
                                       e.asset_type->short_name());
                } else {
                    return std::format("No loader for '{}' (unknown extension and type)", e.path.string());
                }
            },
            [](const load_error::AssetLoaderException& e) -> std::variant<std::string, std::exception_ptr> {
                return e.exception;
            },
            [](const load_error::AssetReaderError& e) -> std::variant<std::string, std::exception_ptr> {
                return std::visit(utils::visitor{
                                      [](const reader_errors::NotFound& r) -> std::string {
                                          return std::format("Asset not found: {}", r.path.string());
                                      },
                                      [](const reader_errors::IoError& r) -> std::string {
                                          return std::format("I/O error: {}", r.code.message());
                                      },
                                      [](const reader_errors::HttpError& r) -> std::string {
                                          return std::format("HTTP error {}", r.status);
                                      },
                                      [](const std::exception_ptr& ep) -> std::string {
                                          try {
                                              std::rethrow_exception(ep);
                                          } catch (const std::exception& ex) {
                                              return ex.what();
                                          } catch (...) {
                                              return "(unknown reader exception)";
                                          }
                                      },
                                  },
                                  e.error);
            },
            [](const load_error::MissingAssetSourceError& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Asset source '{}' does not exist", e.source_id.value_or("default"));
            },
            [](const load_error::MissingProcessedAssetReaderError& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Asset source '{}' has no processed AssetReader", e.source_id.value_or("default"));
            },
            [](const load_error::AssetMetaReadError& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Failed to read asset metadata for '{}'", e.path.string());
            },
            [](const load_error::DeserializeMeta& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Failed to deserialize meta for '{}': {}", e.path.string(), e.error);
            },
            [](const load_error::CannotLoadProcessedAsset& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Asset '{}' is configured to be processed and cannot be loaded directly",
                                   e.path.string());
            },
            [](const load_error::CannotLoadIgnoredAsset& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format("Asset '{}' is configured to be ignored and cannot be loaded", e.path.string());
            },
            [](const load_error::MissingLabel& e) -> std::variant<std::string, std::exception_ptr> {
                return std::format(
                    "Asset '{}' has no labeled sub-asset '{}'; available: [{}]", e.base_path.string(), e.label,
                    std::accumulate(
                        e.all_labels.begin(), e.all_labels.end(), std::string{},
                        [](std::string a, const std::string& b) { return a.empty() ? b : std::move(a) + ", " + b; }));
            },
        },
        error);
}

void log_asset_load_error(const AssetLoadError& error, const AssetPath& path) {
    std::visit(
        utils::visitor{
            [&](const load_error::RequestHandleMismatch& e) {
                spdlog::error(
                    "[asset_server] Asset load failed for '{}': type mismatch — requested '{}' "
                    "but loader '{}' produces '{}'",
                    path.string(), e.requested_type.short_name(), e.loader_name, e.actual_type.short_name());
            },
            [&](const load_error::MissingAssetLoader& e) {
                if (!e.extension.empty()) {
                    spdlog::error(
                        "[asset_server] Asset load failed for '{}': no loader registered for extension(s) [{}]",
                        path.string(),
                        std::accumulate(std::next(e.extension.begin()), e.extension.end(), e.extension.front(),
                                        [](std::string a, const std::string& b) { return std::move(a) + ", " + b; }));
                } else if (e.asset_type) {
                    spdlog::error("[asset_server] Asset load failed for '{}': no loader registered for asset type '{}'",
                                  path.string(), e.asset_type->short_name());
                } else {
                    spdlog::error(
                        "[asset_server] Asset load failed for '{}': no loader found (unknown extension and type)",
                        path.string());
                }
            },
            [&](const load_error::AssetLoaderException& e) {
                std::string what = "(unknown exception)";
                if (e.exception) {
                    try {
                        std::rethrow_exception(e.exception);
                    } catch (const std::exception& ex) {
                        what = ex.what();
                    } catch (...) {}
                }
                spdlog::error("[asset_server] Asset load failed for '{}' (loader '{}'): {}", path.string(),
                              e.loader_name, what);
            },
            [&](const load_error::AssetReaderError& e) {
                std::visit(
                    utils::visitor{
                        [&](const reader_errors::NotFound& r) {
                            spdlog::error("[asset_server] Asset '{}' not found: {}", path.string(), r.path.string());
                        },
                        [&](const reader_errors::IoError& r) {
                            spdlog::error("[asset_server] I/O error reading '{}': {}", path.string(), r.code.message());
                        },
                        [&](const reader_errors::HttpError& r) {
                            spdlog::error("[asset_server] HTTP error {} reading '{}'", r.status, path.string());
                        },
                        [&](const std::exception_ptr& ep) {
                            std::string what = "(unknown)";
                            if (ep) {
                                try {
                                    std::rethrow_exception(ep);
                                } catch (const std::exception& ex) {
                                    what = ex.what();
                                } catch (...) {}
                            }
                            spdlog::error("[asset_server] Reader error for '{}': {}", path.string(), what);
                        },
                    },
                    e.error);
            },
            [&](const load_error::MissingAssetSourceError& e) {
                spdlog::error("[asset_server] Asset source '{}' does not exist (loading '{}')",
                              e.source_id.value_or("default"), path.string());
            },
            [&](const load_error::MissingProcessedAssetReaderError& e) {
                spdlog::error("[asset_server] Asset source '{}' has no processed reader (loading '{}')",
                              e.source_id.value_or("default"), path.string());
            },
            [&](const load_error::AssetMetaReadError& e) {
                spdlog::error("[asset_server] Failed to read asset metadata for '{}'", e.path.string());
            },
            [&](const load_error::DeserializeMeta& e) {
                spdlog::error("[asset_server] Failed to deserialize meta for '{}': {}", e.path.string(), e.error);
            },
            [&](const load_error::CannotLoadProcessedAsset& e) {
                spdlog::error("[asset_server] Asset '{}' requires processing; load in Processed mode", e.path.string());
            },
            [&](const load_error::CannotLoadIgnoredAsset& e) {
                spdlog::error("[asset_server] Asset '{}' is marked as ignored; cannot load", e.path.string());
            },
            [&](const load_error::MissingLabel& e) {
                spdlog::error(
                    "[asset_server] Asset '{}' has no sub-asset '{}'; available: [{}]", e.base_path.string(), e.label,
                    std::accumulate(
                        e.all_labels.begin(), e.all_labels.end(), std::string{},
                        [](std::string a, const std::string& b) { return a.empty() ? b : std::move(a) + ", " + b; }));
            },
        },
        error);
}
}  // namespace epix::assets
