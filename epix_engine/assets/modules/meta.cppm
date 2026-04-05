module;

#include <spdlog/spdlog.h>

export module epix.assets:meta;

import std;
import epix.meta;
import :path;

namespace epix::assets {

/** @brief Version string for the meta format. */
export inline constexpr std::string_view META_FORMAT_VERSION = "1.0";

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
    /** @brief Full hash of the dependency. */
    AssetHash full_hash = {};
    /** @brief Path of the dependency asset. */
    std::string path;
};

/** @brief Information produced by the asset processor about a processed asset. */
export struct ProcessedInfo {
    /** @brief Hash of the asset bytes combined with its meta. */
    AssetHash hash = {};
    /** @brief Hash including all transitive process dependencies. */
    AssetHash full_hash = {};
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

/** @brief Base class for loader/processor settings, analogous to bevy's Settings trait.
 *  All settings types must derive from this. */
export struct Settings {
    virtual ~Settings() = default;
};

/** @brief Abstract base for type-erased asset metadata, analogous to bevy's AssetMetaDyn. */
export struct AssetMetaDyn {
    virtual ~AssetMetaDyn() = default;
    /** @brief Get the loader name, if this meta specifies a Load action. */
    virtual std::optional<std::string_view> loader_name() const = 0;
    /** @brief Get the processor name, if this meta specifies a Process action. */
    virtual std::optional<std::string_view> processor_name() const = 0;
    /** @brief Get the action type. */
    virtual AssetActionType action_type() const = 0;
    /** @brief Get processed info, if available. */
    virtual const ProcessedInfo* processed_info() const = 0;
    /** @brief Get the loader settings, if this meta specifies a Load action. */
    virtual Settings* loader_settings() = 0;
    /** @brief Get the loader settings (const), if this meta specifies a Load action. */
    virtual const Settings* loader_settings() const = 0;
};

/** @brief Type alias for a function that mutates an AssetMetaDyn in place.
 *  Used to override loader/processor settings at handle creation time. */
export using MetaTransform = std::function<void(AssetMetaDyn&)>;

/** @brief Concrete metadata for an asset, parameterised on loader and processor settings types.
 *  @tparam LoaderSettings  The settings type for the asset loader.
 *  @tparam ProcessSettings The settings type for the asset processor. */
export template <typename LoaderSettings, typename ProcessSettings>
struct AssetMeta : AssetMetaDyn {
    /** @brief Meta format version string. */
    std::string meta_format_version = std::string(META_FORMAT_VERSION);
    /** @brief Optional information about prior processing. */
    std::optional<ProcessedInfo> processed;
    /** @brief The action to perform and its associated settings. */
    AssetActionType action = AssetActionType::Load;
    /** @brief Name of the loader (when action == Load). */
    std::string loader;
    /** @brief Loader-specific settings. */
    LoaderSettings loader_settings_value{};
    /** @brief Name of the processor (when action == Process). */
    std::string processor;
    /** @brief Processor-specific settings. */
    ProcessSettings processor_settings{};

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
    Settings* loader_settings() override {
        if constexpr (std::derived_from<LoaderSettings, Settings>) {
            if (action == AssetActionType::Load) return &loader_settings_value;
        }
        return nullptr;
    }
    const Settings* loader_settings() const override {
        if constexpr (std::derived_from<LoaderSettings, Settings>) {
            if (action == AssetActionType::Load) return &loader_settings_value;
        }
        return nullptr;
    }
};

/** @brief Creates a MetaTransform that downcasts the loader settings to type S
 *  and applies the given mutator function.
 *  Matches bevy_asset's loader_settings_meta_transform. */
export template <std::derived_from<Settings> S>
MetaTransform loader_settings_meta_transform(std::function<void(S&)> settings_fn) {
    return [settings_fn = std::move(settings_fn)](AssetMetaDyn& meta) {
        if (auto* s = meta.loader_settings()) {
            if (auto* concrete = dynamic_cast<S*>(s)) {
                settings_fn(*concrete);
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

}  // namespace epix::assets
