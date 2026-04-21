module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <asio/awaitable.hpp>

export module epix.assets:server.loader;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.utils;
import :concepts;

import :store;
import :handle;
import :meta;
import :io.reader;

namespace epix::assets {
template <typename E>
std::exception_ptr asset_loader_error_to_exception(const E& err) {
    if constexpr (std::same_as<std::remove_cvref_t<E>, std::exception_ptr>) {
        return err;
    } else if constexpr (std::derived_from<std::remove_cvref_t<E>, std::exception>) {
        return std::make_exception_ptr(err);
    } else if constexpr (requires(const E& e) {
                             { to_exception_ptr(e) } -> std::same_as<std::exception_ptr>;
                         }) {
        return to_exception_ptr(err);
    } else {
        static_assert(sizeof(E) == 0,
                      "AssetLoader::Error must be convertible to std::exception_ptr (directly, via std::exception, "
                      "or via to_exception_ptr(error)).");
    }
}

export struct AssetServer;
struct AssetInfos;
export struct ProcessContext;
export struct LoadContext;
export template <Asset A>
struct LoadedAsset;
export template <Asset A>
struct SavedAsset;
export template <Asset A>
struct TransformedAsset;
export template <Asset A>
struct TransformedSubAsset;
template <typename T>
struct ErasedAssetLoaderImpl;

export struct AssetContainer {
    virtual ~AssetContainer()                                         = default;
    virtual meta::type_index type() const                             = 0;
    virtual void insert(const UntypedAssetId& id, core::World& world) = 0;
    /** @brief Visit all asset handle dependencies within this container.
     *  Matches bevy_asset's VisitAssetDependencies::visit_dependencies. */
    virtual void visit_dependencies(utils::function_ref<void(UntypedAssetId)> visit) const {}
};
struct LabeledAsset;

/** @brief Type-erased loaded asset with dependency tracking. */
export struct ErasedLoadedAsset {
   private:
    std::unique_ptr<AssetContainer> value;
    std::unordered_set<UntypedAssetId> dependencies;
    std::unordered_map<AssetPath, AssetHash> loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> labeled_assets;

    ErasedLoadedAsset(std::unique_ptr<AssetContainer> v,
                      std::unordered_set<UntypedAssetId> deps,
                      std::unordered_map<AssetPath, AssetHash> loader_deps,
                      std::unordered_map<std::string, LabeledAsset> labeled)
        : value(std::move(v)),
          dependencies(std::move(deps)),
          loader_dependencies(std::move(loader_deps)),
          labeled_assets(std::move(labeled)) {}

    friend struct AssetInfos;
    friend struct ProcessContext;
    friend struct LoadContext;
    template <Asset>
    friend struct LoadedAsset;
    template <Asset>
    friend struct SavedAsset;
    template <Asset>
    friend struct TransformedAsset;
    template <Asset>
    friend struct TransformedSubAsset;
    template <typename>
    friend struct ErasedAssetLoaderImpl;

   public:
    /** @brief Construct an ErasedLoadedAsset from a typed value with no dependencies.
     *  Matches bevy_asset's `LoadedAsset::new_with_dependencies(value).into()`. */
    template <Asset A>
    static ErasedLoadedAsset from_asset(A asset);

    /** @brief Get the type id of the contained asset. */
    meta::type_index asset_type_id() const { return value->type(); }
    /** @brief Get the type name of the contained asset. */
    std::string_view asset_type_name() const { return value ? value->type().name() : std::string_view{}; }

    /** @brief Get a labeled sub-asset by label string. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled(const std::string& label) const;
    /** @brief Get a labeled sub-asset by handle id. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled_by_id(const UntypedAssetId& id) const;
    /** @brief Get a range over all label strings. */
    std::vector<std::string_view> labels() const;
    /** @brief Take the contained asset, moving it out. Returns std::nullopt on type mismatch. */
    template <Asset A>
    std::optional<A> take() &&;
    /** @brief Cast this loaded asset as the given type. */
    template <Asset A>
    std::expected<LoadedAsset<A>, ErasedLoadedAsset> downcast() &&;

    /** @brief Try to downcast the contained asset to type A. Returns nullptr on type mismatch. */
    template <Asset A>
    std::optional<std::reference_wrapper<A>> get();
    /** @brief Try to downcast the contained asset to type A (const). Returns nullptr on type mismatch. */
    template <Asset A>
    std::optional<std::reference_wrapper<const A>> get() const;
};

struct LabeledAsset {
    ErasedLoadedAsset asset;
    UntypedHandle handle;
};

template <typename T>
struct AssetContainerImpl : AssetContainer {
    using asset_type = T;
    T asset;
    AssetContainerImpl(const T& asset) : asset(asset) {}
    AssetContainerImpl(T&& asset) : asset(std::move(asset)) {}
    ~AssetContainerImpl() override = default;
    meta::type_index type() const override { return meta::type_id<T>{}; }
    void insert(const UntypedAssetId& id, core::World& world) override {
        auto&& assets                       = world.resource_mut<Assets<T>>();
        [[maybe_unused]] auto insert_result = assets.insert(id.typed<T>(), std::move(asset));
    }
    void visit_dependencies(utils::function_ref<void(UntypedAssetId)> visit) const override {
        if constexpr (VisitAssetDependencies<T>) {
            asset.visit_dependencies(visit);
        }
    }
};

// -- Deferred ErasedLoadedAsset method definitions (need LabeledAsset & AssetContainerImpl) --
template <Asset A>
ErasedLoadedAsset ErasedLoadedAsset::from_asset(A asset) {
    std::unordered_set<UntypedAssetId> deps;
    if constexpr (VisitAssetDependencies<A>) {
        asset.visit_dependencies([&deps](UntypedAssetId id) { deps.insert(id); });
    }
    return ErasedLoadedAsset{std::make_unique<AssetContainerImpl<A>>(std::move(asset)), std::move(deps), {}, {}};
}
template <Asset A>
std::optional<std::reference_wrapper<A>> ErasedLoadedAsset::get() {
    if (value && value->type() == meta::type_id<A>{}) {
        return std::ref(static_cast<AssetContainerImpl<A>*>(value.get())->asset);
    }
    return std::nullopt;
}
template <Asset A>
std::optional<std::reference_wrapper<const A>> ErasedLoadedAsset::get() const {
    if (value && value->type() == meta::type_id<A>{}) {
        return std::cref(static_cast<const AssetContainerImpl<A>*>(value.get())->asset);
    }
    return std::nullopt;
}
template <Asset A>
std::optional<A> ErasedLoadedAsset::take() && {
    if (value && value->type() == meta::type_id<A>{}) {
        A result = std::move(static_cast<AssetContainerImpl<A>*>(value.get())->asset);
        value.reset();
        return result;
    }
    return std::nullopt;
}
template <Asset A>
std::expected<LoadedAsset<A>, ErasedLoadedAsset> ErasedLoadedAsset::downcast() && {
    auto value_opt = std::move(*this).template take<A>();
    if (!value_opt) {
        return std::unexpected(std::move(*this));
    }
    LoadedAsset<A> result(std::move(*value_opt));
    result.dependencies        = std::move(dependencies);
    result.loader_dependencies = std::move(loader_dependencies);
    result.labeled_assets      = std::move(labeled_assets);
    return result;
}

/** @brief Typed wrapper for a loaded asset with dependency tracking.
 *  @tparam A The asset type. */
export template <Asset A>
struct LoadedAsset {
   private:
    A value;
    std::unordered_set<UntypedAssetId> dependencies;
    std::unordered_map<AssetPath, AssetHash> loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> labeled_assets;

    friend struct LoadContext;
    friend struct ErasedLoadedAsset;
    template <Asset>
    friend struct TransformedAsset;

   public:
    /** @brief Construct from a value. */
    explicit LoadedAsset(A asset) : value(std::move(asset)) {}
    /** @brief Get a const reference to the contained asset. */
    const A& get() const { return value; }
    /** @brief Take the contained asset out. */
    A take() && { return std::move(value); }
    /** @brief Get a labeled sub-asset by label. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled(const std::string& label) const {
        auto it = labeled_assets.find(label);
        if (it == labeled_assets.end()) return std::nullopt;
        return std::cref(it->second.asset);
    }
    /** @brief Get a labeled sub-asset by handle id. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled_by_id(const UntypedAssetId& id) const {
        for (const auto& [_, labeled] : labeled_assets) {
            if (labeled.handle.id() == id) return std::cref(labeled.asset);
        }
        return std::nullopt;
    }
    /** @brief Get a range over all label strings. */
    auto labels() const {
        return labeled_assets |
               std::views::transform([](const auto& pair) -> const std::string& { return pair.first; });
    }
};

// --- Error types and load-state types ---
// Moved here from server.info so that LoadContext can use AssetLoadError
// without creating a circular module dependency (server.info imports server.loader).

export namespace load_error {
/** @brief Requested typed handle but loader produced a different asset type.
 *  Matches bevy_asset AssetLoadError::RequestedHandleTypeMismatch. */
struct RequestHandleMismatch {
    AssetPath path;
    meta::type_index requested_type;
    meta::type_index actual_type;
    std::string_view loader_name;
};
/** @brief No asset loader is registered for this extension/type.
 *  Matches bevy_asset AssetLoadError::MissingAssetLoader. */
struct MissingAssetLoader {
    std::optional<std::string> loader_name;
    std::optional<meta::type_index> asset_type;
    AssetPath path;
    std::vector<std::string> extension;
};
/** @brief The asset loader returned an error or threw an exception.
 *  Merges bevy_asset's AssetLoaderError and AssetLoaderPanic (no panic in C++). */
struct AssetLoaderException {
    std::exception_ptr exception;
    AssetPath path;
    std::string_view loader_name;
};
/** @brief The AssetReader returned an error while reading the asset file.
 *  Matches bevy_asset AssetLoadError::AssetReaderError. */
struct AssetReaderError {
    epix::assets::AssetReaderError error;
};
/** @brief The asset source does not exist.
 *  Matches bevy_asset AssetLoadError::MissingAssetSourceError. */
struct MissingAssetSourceError {
    AssetSourceId source_id;
};
/** @brief No processed AssetReader is configured for the source.
 *  Matches bevy_asset AssetLoadError::MissingProcessedAssetReaderError. */
struct MissingProcessedAssetReaderError {
    AssetSourceId source_id;
};
/** @brief Failed to read the asset metadata bytes from the reader.
 *  Matches bevy_asset AssetLoadError::AssetMetaReadError. */
struct AssetMetaReadError {
    AssetPath path;
};
/** @brief Failed to deserialize asset meta.
 *  Matches bevy_asset AssetLoadError::DeserializeMeta. */
struct DeserializeMeta {
    AssetPath path;
    std::string error;
};
/** @brief Asset is configured to be processed and cannot be loaded directly.
 *  Matches bevy_asset AssetLoadError::CannotLoadProcessedAsset. */
struct CannotLoadProcessedAsset {
    AssetPath path;
};
/** @brief Asset is configured to be ignored and cannot be loaded.
 *  Matches bevy_asset AssetLoadError::CannotLoadIgnoredAsset. */
struct CannotLoadIgnoredAsset {
    AssetPath path;
};
/** @brief The loaded asset contains no labeled sub-asset matching the requested label.
 *  Matches bevy_asset AssetLoadError::MissingLabel. */
struct MissingLabel {
    AssetPath base_path;
    std::string label;
    std::vector<std::string> all_labels;
};
}  // namespace load_error

/** @brief Union of all asset load error variants. Matches bevy_asset's AssetLoadError. */
export using AssetLoadError = std::variant<load_error::RequestHandleMismatch,
                                           load_error::MissingAssetLoader,
                                           load_error::AssetLoaderException,
                                           load_error::AssetReaderError,
                                           load_error::MissingAssetSourceError,
                                           load_error::MissingProcessedAssetReaderError,
                                           load_error::AssetMetaReadError,
                                           load_error::DeserializeMeta,
                                           load_error::CannotLoadProcessedAsset,
                                           load_error::CannotLoadIgnoredAsset,
                                           load_error::MissingLabel>;

/** @brief Simple load-state discriminant without associated error data. */
export enum LoadStateOK { NotLoaded, Loading, Loaded };

/** @brief Current state of an asset's loading lifecycle.
 *  Failed uses shared_ptr to share the same error across the dependency tree (matches Bevy's Arc<AssetLoadError>). */
export using LoadState = std::variant<LoadStateOK, std::shared_ptr<AssetLoadError>>;

/** @brief Load state of an asset's direct dependencies. */
export struct DependencyLoadState : std::variant<LoadStateOK, std::shared_ptr<AssetLoadError>> {
    using base = std::variant<LoadStateOK, std::shared_ptr<AssetLoadError>>;
    using base::base;
    DependencyLoadState() = default;
    bool is_loading() const {
        return std::holds_alternative<LoadStateOK>(*this) && std::get<LoadStateOK>(*this) == LoadStateOK::Loading;
    }
    bool is_loaded() const {
        return std::holds_alternative<LoadStateOK>(*this) && std::get<LoadStateOK>(*this) == LoadStateOK::Loaded;
    }
    bool is_failed() const { return std::holds_alternative<std::shared_ptr<AssetLoadError>>(*this); }
    /** @brief Returns the shared error pointer if failed, null otherwise. */
    std::shared_ptr<AssetLoadError> error() const {
        if (auto* p = std::get_if<std::shared_ptr<AssetLoadError>>(this)) return *p;
        return nullptr;
    }
};

/** @brief Load state of an asset's full recursive dependency tree. */
export struct RecursiveDependencyLoadState : std::variant<LoadStateOK, std::shared_ptr<AssetLoadError>> {
    using base = std::variant<LoadStateOK, std::shared_ptr<AssetLoadError>>;
    using base::base;
    RecursiveDependencyLoadState() = default;
    bool is_loading() const {
        return std::holds_alternative<LoadStateOK>(*this) && std::get<LoadStateOK>(*this) == LoadStateOK::Loading;
    }
    bool is_loaded() const {
        return std::holds_alternative<LoadStateOK>(*this) && std::get<LoadStateOK>(*this) == LoadStateOK::Loaded;
    }
    bool is_failed() const { return std::holds_alternative<std::shared_ptr<AssetLoadError>>(*this); }
    /** @brief Returns the shared error pointer if failed, null otherwise. */
    std::shared_ptr<AssetLoadError> error() const {
        if (auto* p = std::get_if<std::shared_ptr<AssetLoadError>>(this)) return *p;
        return nullptr;
    }
};

export namespace wait_for_asset_error {
struct NotLoaded {};
struct Failed {
    std::shared_ptr<AssetLoadError> error;
};
struct DependencyFailed {
    std::shared_ptr<AssetLoadError> error;
};
}  // namespace wait_for_asset_error
export using WaitForAssetError =
    std::variant<wait_for_asset_error::NotLoaded, wait_for_asset_error::Failed, wait_for_asset_error::DependencyFailed>;

/** @brief Format an AssetLoadError into either a human-readable string or an exception_ptr.
 *  Used to populate UntypedAssetLoadFailedEvent and AssetLoadFailedEvent. */
export std::variant<std::string, std::exception_ptr> format_asset_load_error(const AssetLoadError& error);

/** @brief Log an AssetLoadError via spdlog::error with a consistent "[asset_server]" prefix.
 *  @param error    The load error.
 *  @param path     The asset path being loaded (used for context in multi-part errors). */
export void log_asset_load_error(const AssetLoadError& error, const AssetPath& path);

namespace internal_asset_event {
struct Loaded {
    UntypedAssetId id;
    ErasedLoadedAsset asset;
};
struct LoadedWithDeps {
    UntypedAssetId id;
};
struct Failed {
    UntypedAssetId id;
    AssetPath path;
    AssetLoadError error;
};
}  // namespace internal_asset_event
using InternalAssetEvent =
    std::variant<internal_asset_event::Loaded, internal_asset_event::LoadedWithDeps, internal_asset_event::Failed>;

/** @brief A builder for performing nested asset loads within a LoadContext.
 *  Matches bevy_asset's NestedLoader. */
export struct NestedLoader;

export struct LoadContext {
   private:
    const AssetServer& m_server;
    AssetPath m_path;
    std::unordered_set<UntypedAssetId> m_dependencies;
    std::unordered_map<AssetPath, AssetHash> m_loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> m_labeled_assets;
    /// When false, NestedLoader::load() only reserves a handle without scheduling a load task.
    /// Matches bevy_asset's LoadContext::should_load_dependencies.
    bool m_should_load_dependencies = true;

    friend struct NestedLoader;
    template <typename>
    friend struct ErasedAssetLoaderImpl;

   public:
    LoadContext(const AssetServer& server, AssetPath path);

   public:
    /** @brief Get the asset path being loaded. */
    const AssetPath& path() const { return m_path; }
    /** @brief Get a reference to the asset server. */
    const AssetServer& asset_server() const { return m_server; }

    /** @brief Register an asset id as a direct dependency of this load.
     *  Used by loaders that call AssetServer::load directly instead of NestedLoader. */
    void track_dependency(const UntypedAssetId& id) { m_dependencies.insert(id); }

    /** @brief Whether nested loads should actually schedule load tasks.
     *  When false, NestedLoader::load only reserves/looks up handles without scheduling.
     *  Matches bevy_asset's LoadContext::should_load_dependencies. */
    bool should_load_dependencies() const { return m_should_load_dependencies; }
    /** @brief Set whether nested loads schedule load tasks.
     *  Matches bevy_asset's LoadContext::should_load_dependencies field. */
    void set_should_load_dependencies(bool v) { m_should_load_dependencies = v; }

    /** @brief Check whether a labeled asset with the given label exists. */
    bool has_labeled_asset(const std::string& label) const { return m_labeled_assets.contains(label); }

    /** @brief Get a labeled sub-asset by label. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_labeled(const std::string& label) const {
        auto it = m_labeled_assets.find(label);
        if (it == m_labeled_assets.end()) return std::nullopt;
        return std::cref(it->second.asset);
    }

    /** @brief Add a labeled asset and return a handle to it.
     *  @tparam A The asset type.
     *  @param label The label string.
     *  @param asset The asset value. */
    template <Asset A>
    Handle<A> add_labeled_asset(const std::string& label, A asset);

    /** @brief Add a pre-loaded labeled asset and return a handle to it.
     *  @tparam A The asset type.
     *  @param label The label string.
     *  @param loaded The already-loaded asset. */
    template <Asset A>
    Handle<A> add_loaded_labeled_asset(const std::string& label, LoadedAsset<A> loaded);

    /** @brief Get a nested loader for loading sub-assets from within this load.
     *  Matches bevy_asset's LoadContext::loader(). */
    NestedLoader loader();

    /** @brief Directly load an asset at path, running the loader pipeline asynchronously.
     *  @tparam A The expected asset type.
     *  Matches bevy_asset's LoadContext::load_direct (async). */
    template <Asset A>
    asio::awaitable<std::expected<LoadedAsset<A>, AssetLoadError>> load_direct(const AssetPath& path) const;

    /** @brief Load an asset directly using an already-open reader.
     *  @tparam A The expected asset type.
     *  Matches bevy_asset's LoadContext::load_direct_with_reader. */
    template <Asset A>
    asio::awaitable<std::expected<LoadedAsset<A>, AssetLoadError>> load_direct_with_reader(const AssetPath& path,
                                                                                           Reader& reader) const;

    /** @brief Begin a labeled asset scope, returning a new LoadContext
     *  whose path includes the given label. The caller is responsible for
     *  adding the resulting loaded asset via add_loaded_labeled_asset.
     *  Matches bevy_asset's LoadContext::begin_labeled_asset. */
    LoadContext begin_labeled_asset() const { return LoadContext(m_server, m_path); }

    /** @brief Execute a callback with a scoped LoadContext for a labeled asset,
     *  and register the result under the given label.
     *  Matches bevy_asset's LoadContext::labeled_asset_scope.
     *  @tparam A  Asset type.
     *  @param label The sub-asset label.
     *  @param fn    Callback: fn(LoadContext&) -> A. */
    template <Asset A, typename F>
        requires std::invocable<F, LoadContext&>
    Handle<A> labeled_asset_scope(const std::string& label, F&& fn);

    /** @brief Finish loading, producing a typed LoadedAsset.
     *  @tparam A The asset type.
     *  @param value The loaded asset value. */
    template <Asset A>
    LoadedAsset<A> finish(A value) && {
        LoadedAsset<A> result(std::move(value));
        result.dependencies        = std::move(m_dependencies);
        result.loader_dependencies = std::move(m_loader_dependencies);
        result.labeled_assets      = std::move(m_labeled_assets);
        return result;
    }
};

/** @brief A builder for performing nested asset loads within a LoadContext.
 *  Matches bevy_asset's NestedLoader. */
export struct NestedLoader {
   private:
    LoadContext& m_context;
    std::optional<MetaTransform> m_meta_transform = std::nullopt;

   public:
    explicit NestedLoader(LoadContext& context) : m_context(context) {}

    /** @brief Set a settings function that modifies the loader meta for the next load call.
     *  Matches bevy_asset's NestedLoader::with_settings. */
    template <typename S>
    NestedLoader& with_settings(std::function<void(S&)> fn) {
        m_meta_transform = loader_settings_meta_transform<S>(std::move(fn));
        return *this;
    }

    /** @brief Load a sub-asset and register it as a dependency.
     *  @tparam A Asset type.
     *  @param path Path of the sub-asset. */
    template <Asset A>
    Handle<A> load(const AssetPath& path);

    /** @brief Load a sub-asset with unknown type (dynamic dispatch), registering it as a dependency.
     *  Matches bevy_asset's DynamicTyped NestedLoader::load. */
    UntypedHandle load_untyped(const AssetPath& path);

    /** @brief Load a sub-asset by resolving a relative path against the parent's directory.
     *  @tparam A Asset type.
     *  @param relative_path Relative path string. */
    template <Asset A>
    Handle<A> load_relative(std::string_view relative_path) {
        return load<A>(m_context.path().resolve(relative_path));
    }
};
export template <typename T>
concept AssetLoader = requires(const T& t, Reader& reader, LoadContext& context) {
    typename T::Asset;
    typename T::Settings;
    typename T::Error;
    requires Asset<typename T::Asset>;
    requires is_settings<typename T::Settings>;
    requires std::is_default_constructible_v<typename T::Settings>;
    { t.extensions() } -> std::same_as<std::span<std::string_view>>;
    {
        t.load(reader, std::declval<const typename T::Settings&>(), context)
    } -> std::same_as<asio::awaitable<std::expected<typename T::Asset, typename T::Error>>>;
    { asset_loader_error_to_exception(std::declval<const typename T::Error&>()) } -> std::same_as<std::exception_ptr>;
};
export struct ErasedAssetLoader {
    virtual ~ErasedAssetLoader()                           = default;
    virtual std::span<std::string_view> extensions() const = 0;
    virtual meta::type_index loader_type() const           = 0;
    virtual meta::type_index asset_type() const            = 0;
    /** @brief The short name of the asset type (e.g. "Image").
     *  Matches bevy_asset's ErasedAssetLoader::asset_type_name. */
    std::string_view asset_type_name() const { return asset_type().short_name(); }
    /** @brief The type_index of the asset type.
     *  Matches bevy_asset's ErasedAssetLoader::asset_type_id. */
    meta::type_index asset_type_id() const { return asset_type(); }
    /** @brief The full qualified name of the loader type.
     *  Matches bevy_asset's ErasedAssetLoader::type_path. */
    std::string_view type_path() const { return loader_type().name(); }
    virtual std::unique_ptr<Settings> default_settings() const = 0;
    /** @brief Return a heap-allocated default AssetMetaDyn for this loader.
     *  Matches bevy_asset's ErasedAssetLoader::default_meta. */
    virtual std::unique_ptr<AssetMetaDyn> default_meta() const = 0;
    /** @brief Deserialize a .meta file's bytes into a typed AssetMetaDyn for this loader.
     *  Returns an error string on failure; use default_meta() when bytes are unavailable.
     *  Matches bevy_asset's ErasedAssetLoader::deserialize_meta. */
    virtual std::expected<std::unique_ptr<AssetMetaDyn>, std::string> deserialize_meta(
        std::span<const std::byte> bytes) const                                                                    = 0;
    virtual asio::awaitable<std::expected<ErasedLoadedAsset, std::exception_ptr>> load(Reader& reader,
                                                                                       const Settings& settings,
                                                                                       LoadContext& context) const = 0;
};
template <typename T>
struct ErasedAssetLoaderImpl : T, ErasedAssetLoader {
    static_assert(AssetLoader<T>);

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    ErasedAssetLoaderImpl(Args&&... args) : T(std::forward<Args>(args)...) {}
    const T& as_concrete() const { return static_cast<const T&>(*this); }
    std::span<std::string_view> extensions() const override { return as_concrete().extensions(); }
    meta::type_index loader_type() const override { return meta::type_id<T>{}; }
    meta::type_index asset_type() const override { return meta::type_id<typename T::Asset>{}; }
    std::unique_ptr<Settings> default_settings() const override {
        return std::make_unique<SettingsImpl<typename T::Settings>>();
    }
    std::unique_ptr<AssetMetaDyn> default_meta() const override {
        auto m    = std::make_unique<AssetMeta<typename T::Settings, EmptySettings>>();
        m->action = AssetActionType::Load;
        m->loader = std::string(loader_type().name());
        return m;
    }
    std::expected<std::unique_ptr<AssetMetaDyn>, std::string> deserialize_meta(
        std::span<const std::byte> bytes) const override {
        auto result = deserialize_asset_meta<typename T::Settings, EmptySettings>(bytes);
        if (!result) {
            return std::unexpected(std::make_error_code(result.error()).message());
        }
        return std::make_unique<AssetMeta<typename T::Settings, EmptySettings>>(std::move(*result));
    }
    asio::awaitable<std::expected<ErasedLoadedAsset, std::exception_ptr>> load(Reader& reader,
                                                                               const Settings& settings,
                                                                               LoadContext& context) const override {
        try {
            auto* settings_ptr = dynamic_cast<const SettingsImpl<typename T::Settings>*>(&settings);
            if (!settings_ptr) {
                throw std::runtime_error("Invalid settings type for loader " + std::string(loader_type().short_name()));
            }
            auto loaded_asset = co_await as_concrete().load(reader, settings_ptr->value, context);
            if (!loaded_asset) {
                co_return std::unexpected(asset_loader_error_to_exception(loaded_asset.error()));
            }
            auto erased_asset = std::make_unique<AssetContainerImpl<typename T::Asset>>(std::move(*loaded_asset));
            co_return ErasedLoadedAsset{std::move(erased_asset), std::move(context.m_dependencies),
                                        std::move(context.m_loader_dependencies), std::move(context.m_labeled_assets)};
        } catch (...) {
            co_return std::unexpected(std::current_exception());
        }
    }
};

/** @brief Concept for an asset saver. Savers write assets to a Writer.
 *  Implementations must provide: Asset, Settings, save().
 *  Matches bevy_asset's AssetSaver trait — associated type is named `Asset`. */
export template <typename T>
concept AssetSaver = requires(const T& t, Writer& writer, const typename T::Settings& settings) {
    typename T::Asset;
    typename T::Settings;
    typename T::OutputLoader;
    typename T::Error;
    requires Asset<typename T::Asset>;
    requires std::is_default_constructible_v<typename T::Settings>;
    {
        t.save(writer, std::declval<SavedAsset<typename T::Asset>>(), settings, std::declval<const AssetPath&>())
    } -> std::same_as<asio::awaitable<std::expected<typename T::OutputLoader::Settings, typename T::Error>>>;
};

/** @brief Concept for an asset transformer. Transforms one asset type to another.
 *  Implementations must provide: AssetInput, AssetOutput, Settings, transform(). */
export template <typename T>
concept AssetTransformer = requires(const T& t, const typename T::Settings& settings) {
    typename T::AssetInput;
    typename T::AssetOutput;
    typename T::Settings;
    typename T::Error;
    requires Asset<typename T::AssetInput>;
    requires Asset<typename T::AssetOutput>;
    requires is_settings<typename T::Settings>;
    requires std::is_default_constructible_v<typename T::Settings>;
    {
        t.transform(std::declval<TransformedAsset<typename T::AssetInput>>(), settings)
    } -> std::same_as<asio::awaitable<std::expected<TransformedAsset<typename T::AssetOutput>, typename T::Error>>>;
};

// --- Template implementations for LoadContext / NestedLoader ---

inline LoadContext::LoadContext(const AssetServer& server, AssetPath path)
    : m_server(server), m_path(std::move(path)) {}

inline NestedLoader LoadContext::loader() { return NestedLoader(*this); }

template <Asset A, typename F>
    requires std::invocable<F, LoadContext&>
Handle<A> LoadContext::labeled_asset_scope(const std::string& label, F&& fn) {
    auto sub_context = begin_labeled_asset();
    A asset          = std::forward<F>(fn)(sub_context);
    return add_labeled_asset<A>(label, std::move(asset));
}

}  // namespace epix::assets