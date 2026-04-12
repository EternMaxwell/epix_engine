module;

#include <spdlog/spdlog.h>
#include <zpp_bits.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

export module epix.assets:meta;

import epix.meta;
import :path;

namespace epix::assets {

/** @brief Version string for the meta format. */
export inline constexpr std::string_view META_FORMAT_VERSION = "2.0";

/** @brief Controls when and how asset metadata files are checked.
 *  Matches bevy_asset's AssetMetaCheck. */
export namespace asset_meta_check {
/** @brief Always check for .meta files alongside assets (default). */
struct Always {};
/** @brief Never check for .meta files. */
struct Never {};
/** @brief Only check for .meta files at the specified asset paths.
 *  Matches bevy_asset's AssetMetaCheck::Paths(HashSet<AssetPath>). */
struct Paths {
    std::unordered_set<AssetPath> paths;
};
}  // namespace asset_meta_check
export using AssetMetaCheck = std::variant<asset_meta_check::Always, asset_meta_check::Never, asset_meta_check::Paths>;

/** @brief Controls how unapproved asset paths are handled.
 *  Matches bevy_asset's UnapprovedPathMode. */
export enum class UnapprovedPathMode {
    Allow,  /**< Allow loading from any path. */
    Deny,   /**< Deny unless override method is used. */
    Forbid, /**< Always forbid unapproved paths (default). */
};

/** @brief Hash type used by the asset processing pipeline.
 *  Matches bevy_asset's AssetHash = [u8; 32] (BLAKE3 hash). */
export using AssetHash = std::array<uint8_t, 32>;

/** @brief Information about a processed asset's dependency on another asset. */
export struct ProcessDependencyInfo {
   private:
    friend zpp::bits::access;
    using serialize = zpp::bits::members<2>;

   public:
    /** @brief Full hash of the dependency. */
    AssetHash full_hash = {};
    /** @brief Path of the dependency asset. */
    std::string path;
};

/** @brief Information produced by the asset processor about a processed asset. */
export struct ProcessedInfo {
   private:
    friend zpp::bits::access;
    using serialize = zpp::bits::members<4>;

   public:
    /** @brief Hash of the asset bytes combined with its meta. */
    AssetHash hash = {};
    /** @brief Hash including all transitive process dependencies. */
    AssetHash full_hash = {};
    /** @brief Optional persisted source-file last-modified timestamp in nanoseconds.
     *  Used as a cross-session fast-path to skip hashing when the source file timestamp matches. */
    std::optional<std::int64_t> source_mtime_ns;
    /** @brief Dependencies that contributed to processing. */
    std::vector<ProcessDependencyInfo> process_dependencies;
};

/** @brief Discriminator for what action to take on an asset. */
export enum class AssetActionType {
    Load,    /**< Load the asset using a loader with settings. */
    Process, /**< Process the asset using a processor with settings. */
    Ignore,  /**< Do nothing with this asset. */
};

/** @brief Action to take on an asset, with associated loader/processor settings.
 *  Matches bevy_asset's AssetAction<LS,PS>. */
export template <typename LoaderSettings, typename ProcessSettings>
struct AssetAction {
    struct Load {
        LoaderSettings settings{};
    };
    struct Process {
        ProcessSettings settings{};
        std::string processor;
    };
    struct Ignore {};

    std::variant<Load, Process, Ignore> inner = Load{};

    AssetActionType type() const {
        return std::visit(
            [](auto&& v) -> AssetActionType {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::same_as<V, Load>)
                    return AssetActionType::Load;
                else if constexpr (std::same_as<V, Process>)
                    return AssetActionType::Process;
                else
                    return AssetActionType::Ignore;
            },
            inner);
    }
};

/** @brief Base class for loader/processor settings.
 *  Settings types are plain aggregates; the engine wraps them in SettingsImpl<T>. */
export struct Settings {
    virtual ~Settings() = default;
    template <typename T>
    std::optional<std::reference_wrapper<const T>> try_cast() const;
    template <typename T>
    std::optional<std::reference_wrapper<T>> try_cast();
    template <typename T>
    const T& cast() const;
    template <typename T>
    T& cast();
};

/** @brief Default empty settings used when no settings are needed.
 *  zpp::bits natively handles empty types via its `empty` concept (std::is_empty_v). */
export struct EmptySettings {};

/** @brief Concept satisfied by any type that zpp::bits can serialize:
 *  - types with an explicit serialize hook (has_serialize)
 *  - containers (std::vector, std::string, std::map, …)
 *  - tuple-like types
 *  - std::variant, std::optional
 *  - empty types (std::is_empty_v — includes EmptySettings)
 *  - byte-serializable fundamentals/enums
 *  - unspecialized aggregates (struct with only serializable members)
 *  All must also be default-constructible. */
export template <typename T>
concept is_settings =
    std::is_default_constructible_v<T> &&
    (zpp::bits::concepts::has_serialize<T> || zpp::bits::concepts::container<T> || zpp::bits::concepts::tuple<T> ||
     zpp::bits::concepts::variant<T> || zpp::bits::concepts::optional<T> || zpp::bits::concepts::empty<T> ||
     zpp::bits::concepts::byte_serializable<T> || (zpp::bits::concepts::unspecialized<T> && std::is_aggregate_v<T>));

/** @brief Engine-internal wrapper that makes any plain aggregate T a Settings subclass.
 *  Users define settings as plain aggregates (no inheritance required).
 *  The engine wraps them via this type for polymorphic storage. */
template <typename T>
struct SettingsImpl : Settings {
    T value{};
};

template <typename T>
std::optional<std::reference_wrapper<const T>> Settings::try_cast() const {
    if (auto* derived = dynamic_cast<const SettingsImpl<T>*>(this)) {
        return std::cref(derived->value);
    }
    return std::nullopt;
}
template <typename T>
std::optional<std::reference_wrapper<T>> Settings::try_cast() {
    if (auto* derived = dynamic_cast<SettingsImpl<T>*>(this)) {
        return std::ref(derived->value);
    }
    return std::nullopt;
}
template <typename T>
const T& Settings::cast() const {
    auto result = try_cast<T>();
    if (!result.has_value()) throw std::bad_cast();
    return result->get();
}
template <typename T>
T& Settings::cast() {
    auto result = try_cast<T>();
    if (!result.has_value()) throw std::bad_cast();
    return result->get();
}

/** @brief Abstract base for type-erased asset metadata, analogous to bevy's AssetMetaDyn. */
export struct AssetMetaDyn {
    virtual ~AssetMetaDyn() = default;
    /** @brief Get the loader name, if this meta specifies a Load action. */
    virtual std::optional<std::string_view> loader_name() const = 0;
    /** @brief Get the processor name, if this meta specifies a Process action. */
    virtual std::optional<std::string_view> processor_name() const = 0;
    /** @brief Get the action type. */
    virtual AssetActionType action_type() const = 0;
    /** @brief Get processed info (const), if available. */
    virtual const ProcessedInfo* processed_info() const = 0;
    /** @brief Get mutable reference to the stored ProcessedInfo optional.
     *  Matches bevy_asset's AssetMetaDyn::processed_info_mut. */
    virtual std::optional<ProcessedInfo>& processed_info_mut() = 0;
    /** @brief Get the loader settings, if this meta specifies a Load action. */
    virtual Settings* loader_settings() = 0;
    /** @brief Get the loader settings (const), if this meta specifies a Load action. */
    virtual const Settings* loader_settings() const = 0;
    /** @brief Get the processor settings, if this meta specifies a Process action.
     *  Matches bevy_asset's AssetMetaDyn::process_settings. */
    virtual Settings* process_settings() = 0;
    /** @brief Get the processor settings (const), if this meta specifies a Process action. */
    virtual const Settings* process_settings() const = 0;
    /** @brief Serialize this meta to binary bytes for writing to disk.
     *  Matches bevy_asset's AssetMetaDyn::serialize. */
    virtual std::vector<std::byte> serialize_bytes() const = 0;
};

/** @brief Type alias for a function that mutates an AssetMetaDyn in place.
 *  Used to override loader/processor settings at handle creation time. */
export using MetaTransform = std::function<void(AssetMetaDyn&)>;

/** @brief Concrete metadata for an asset, parameterised on loader and processor settings types.
 *  @tparam LoaderSettings  The aggregate settings type for the asset loader.
 *  @tparam ProcessSettings The aggregate settings type for the asset processor. */
export template <typename LoaderSettings, typename ProcessSettings = EmptySettings>
struct AssetMeta : AssetMetaDyn {
    /** @brief Meta format version string. */
    std::string meta_format_version = std::string(META_FORMAT_VERSION);
    /** @brief Optional information about prior processing. */
    std::optional<ProcessedInfo> processed;
    /** @brief The action to perform and its associated settings. */
    AssetActionType action = AssetActionType::Load;
    /** @brief Name of the loader (when action == Load). */
    std::string loader;
    /** @brief Loader-specific settings (wrapped in SettingsImpl for polymorphism). */
    SettingsImpl<LoaderSettings> loader_settings_storage{};
    /** @brief Name of the processor (when action == Process). */
    std::string processor;
    /** @brief Processor-specific settings (wrapped in SettingsImpl for polymorphism). */
    SettingsImpl<ProcessSettings> processor_settings_storage{};

    std::optional<std::string_view> loader_name() const override {
        if (action == AssetActionType::Load) return loader;
        return std::nullopt;
    }
    std::optional<std::string_view> processor_name() const override {
        if (action == AssetActionType::Process) return processor;
        return std::nullopt;
    }
    AssetActionType action_type() const override { return action; }
    const ProcessedInfo* processed_info() const override {
        return processed.has_value() ? &processed.value() : nullptr;
    }
    std::optional<ProcessedInfo>& processed_info_mut() override { return processed; }
    Settings* loader_settings() override {
        if (action == AssetActionType::Load) return &loader_settings_storage;
        return nullptr;
    }
    const Settings* loader_settings() const override {
        if (action == AssetActionType::Load) return &loader_settings_storage;
        return nullptr;
    }
    Settings* process_settings() override {
        if (action == AssetActionType::Process) return &processor_settings_storage;
        return nullptr;
    }
    const Settings* process_settings() const override {
        if (action == AssetActionType::Process) return &processor_settings_storage;
        return nullptr;
    }
    std::vector<std::byte> serialize_bytes() const override {
        auto result = serialize_asset_meta(*this);
        if (!result) {
            spdlog::error("Failed to serialize AssetMeta: {}", std::make_error_code(result.error()).message());
            return {};
        }
        return std::move(*result);
    }
};

/** @brief Creates a MetaTransform that downcasts the loader settings to SettingsImpl<S>
 *  and applies the given mutator function to the wrapped value.
 *  Matches bevy_asset's loader_settings_meta_transform. */
template <typename S>
MetaTransform loader_settings_meta_transform(std::function<void(S&)> settings_fn) {
    return [settings_fn = std::move(settings_fn)](AssetMetaDyn& meta) {
        if (auto* s = meta.loader_settings()) {
            if (auto* concrete = dynamic_cast<SettingsImpl<S>*>(s)) {
                settings_fn(concrete->value);
            } else {
                spdlog::error("Configured settings type does not match AssetLoader settings type");
            }
        }
    };
}

/** @brief Minimal action descriptor parsed from a bare .meta file.
 *  Only contains the discriminant (load/process/ignore) and the loader/processor name.
 *  Matches bevy_asset's AssetActionMinimal. */
export struct AssetActionMinimal {
    /** @brief The kind of action this meta record specifies. */
    AssetActionType action = AssetActionType::Load;
    /** @brief The loader type name when action == Load; empty otherwise. */
    std::string loader;
    /** @brief The processor type name when action == Process; empty otherwise. */
    std::string processor;
};

/** @brief Minimal meta record sufficient to identify the loader or processor for an asset path.
 *  Used when deserializing .meta files before full settings are needed.
 *  Matches bevy_asset's AssetMetaMinimal. */
export struct AssetMetaMinimal {
    /** @brief Meta format version string as stored in the .meta file. */
    std::string meta_format_version;
    /** @brief The minimal action information. */
    AssetActionMinimal asset;
};

// ---------------------------------------------------------------------------
// Binary serialize/deserialize helpers
// ---------------------------------------------------------------------------

/** @brief Serialize an AssetMetaMinimal to a byte vector using zpp::bits. */
export std::expected<std::vector<std::byte>, std::errc> serialize_meta_minimal(const AssetMetaMinimal& minimal);

/** @brief Deserialize an AssetMetaMinimal from a byte span using zpp::bits. */
export std::expected<AssetMetaMinimal, std::errc> deserialize_meta_minimal(std::span<const std::byte> bytes);

/** @brief Overload accepting a std::vector<std::byte>. */
export inline std::expected<AssetMetaMinimal, std::errc> deserialize_meta_minimal(const std::vector<std::byte>& bytes) {
    return deserialize_meta_minimal(std::span<const std::byte>(bytes.data(), bytes.size()));
}

/** @brief Serialize a full AssetMeta<LS,PS> to a byte vector using zpp::bits.
 *  Fields are passed individually to avoid zpp_bits trying to reflect on
 *  AssetMeta (which has virtual methods — MSVC cannot handle that via pfr).
 *  Uses explicit vector+out construction to avoid the data_out() local-struct
 *  duplicate-COMDAT MSVC issue. */
export template <typename LS, typename PS>
std::expected<std::vector<std::byte>, std::errc> serialize_asset_meta(const AssetMeta<LS, PS>& meta) {
    std::vector<std::byte> data;
    zpp::bits::out out{data};
    auto result = out(meta.meta_format_version, meta.processed, meta.action, meta.loader,
                      meta.loader_settings_storage.value, meta.processor, meta.processor_settings_storage.value);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return data;
}

/** @brief Deserialize a full AssetMeta<LS,PS> from a byte span using zpp::bits. */
export template <typename LS, typename PS>
std::expected<AssetMeta<LS, PS>, std::errc> deserialize_asset_meta(std::span<const std::byte> bytes) {
    AssetMeta<LS, PS> meta;
    zpp::bits::in in(bytes);
    auto result = in(meta.meta_format_version, meta.processed, meta.action, meta.loader,
                     meta.loader_settings_storage.value, meta.processor, meta.processor_settings_storage.value);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return meta;
}

/** @brief Extract only the ProcessedInfo from serialized AssetMeta bytes.
 *  Reads just the first two fields (version + processed) and stops, which is
 *  valid with zpp::bits sequential deserialization.  Used during processor
 *  start-up to restore the stored hash without needing to know the settings types.
 *  Matches bevy_asset's ProcessedInfoMinimal concept. */
export std::expected<std::optional<ProcessedInfo>, std::errc> deserialize_processed_info(
    std::span<const std::byte> bytes);

/** @brief Overload accepting std::vector<std::byte>. */
export inline std::expected<std::optional<ProcessedInfo>, std::errc> deserialize_processed_info(
    const std::vector<std::byte>& bytes) {
    return deserialize_processed_info(std::span<const std::byte>(bytes.data(), bytes.size()));
}

// ---------------------------------------------------------------------------
// Asset hashing utilities
// Matches bevy_asset's get_asset_hash / get_full_asset_hash.
// Uses a simple streaming 32-byte hash (4 x 64-bit lanes, FNV-inspired) since
// BLAKE3 is not available as a standalone dependency.  The format is internal
// to epix_engine and does not need to interoperate with bevy's binary format.
// ---------------------------------------------------------------------------

/** @brief Compute a 32-byte asset hash from meta bytes + asset stream contents.
 *  NOTE: changing the hashing algorithm requires a META_FORMAT_VERSION bump.
 *  Matches bevy_asset's get_asset_hash (synchronous version). */
AssetHash get_asset_hash(std::span<const std::byte> meta_bytes, std::istream& reader);

/** @brief Compute the full_hash by chaining an asset hash with all dependency full_hashes.
 *  Matches bevy_asset's get_full_asset_hash. */
AssetHash get_full_asset_hash(AssetHash asset_hash, const std::vector<AssetHash>& dependency_hashes);

}  // namespace epix::assets
