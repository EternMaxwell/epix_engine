module;

export module epix.assets:server.loader;

import std;
import epix.meta;

import :store;
import :meta;

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
export template <typename A>
struct LoadedAsset;
export template <typename A>
struct SavedAsset;
export template <typename A>
struct TransformedAsset;
export template <typename A>
struct TransformedSubAsset;
template <typename T>
struct ErasedAssetLoaderImpl;

struct AssetContainer {
    virtual ~AssetContainer()                                         = default;
    virtual meta::type_index type() const                             = 0;
    virtual void insert(const UntypedAssetId& id, core::World& world) = 0;
};
struct LabeledAsset;

/** @brief Type-erased loaded asset with dependency tracking. */
export struct ErasedLoadedAsset {
   private:
    std::unique_ptr<AssetContainer> value;
    std::unordered_set<UntypedAssetId> dependencies;
    std::unordered_map<AssetPath, std::size_t> loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> labeled_assets;

    ErasedLoadedAsset(std::unique_ptr<AssetContainer> v,
                      std::unordered_set<UntypedAssetId> deps,
                      std::unordered_map<AssetPath, std::size_t> loader_deps,
                      std::unordered_map<std::string, LabeledAsset> labeled)
        : value(std::move(v)),
          dependencies(std::move(deps)),
          loader_dependencies(std::move(loader_deps)),
          labeled_assets(std::move(labeled)) {}

    friend struct AssetInfos;
    friend struct ProcessContext;
    friend struct LoadContext;
    template <typename>
    friend struct LoadedAsset;
    template <typename>
    friend struct SavedAsset;
    template <typename>
    friend struct TransformedAsset;
    template <typename>
    friend struct TransformedSubAsset;
    template <typename>
    friend struct ErasedAssetLoaderImpl;

   public:
    /** @brief Construct an ErasedLoadedAsset from a typed value with no dependencies.
     *  Matches bevy_asset's `LoadedAsset::new_with_dependencies(value).into()`. */
    template <typename A>
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
    template <typename A>
    std::optional<A> take() &&;
    /** @brief Cast this loaded asset as the given type. */
    template <typename A>
    std::expected<LoadedAsset<A>, ErasedLoadedAsset> downcast() &&;

    /** @brief Try to downcast the contained asset to type A. Returns nullptr on type mismatch. */
    template <typename A>
    std::optional<std::reference_wrapper<A>> get();
    /** @brief Try to downcast the contained asset to type A (const). Returns nullptr on type mismatch. */
    template <typename A>
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
};

// -- Deferred ErasedLoadedAsset method definitions (need LabeledAsset & AssetContainerImpl) --
template <typename A>
ErasedLoadedAsset ErasedLoadedAsset::from_asset(A asset) {
    return ErasedLoadedAsset{std::make_unique<AssetContainerImpl<A>>(std::move(asset)), {}, {}, {}};
}
inline std::optional<std::reference_wrapper<const ErasedLoadedAsset>> ErasedLoadedAsset::get_labeled(
    const std::string& label) const {
    auto it = labeled_assets.find(label);
    if (it == labeled_assets.end()) return std::nullopt;
    return std::cref(it->second.asset);
}
inline std::optional<std::reference_wrapper<const ErasedLoadedAsset>> ErasedLoadedAsset::get_labeled_by_id(
    const UntypedAssetId& id) const {
    for (const auto& [_, labeled] : labeled_assets) {
        if (labeled.handle.id() == id) return std::cref(labeled.asset);
    }
    return std::nullopt;
}
inline std::vector<std::string_view> ErasedLoadedAsset::labels() const {
    std::vector<std::string_view> result;
    result.reserve(labeled_assets.size());
    for (const auto& [label, _] : labeled_assets) result.push_back(label);
    return result;
}
template <typename A>
std::optional<std::reference_wrapper<A>> ErasedLoadedAsset::get() {
    if (value && value->type() == meta::type_id<A>{}) {
        return std::ref(static_cast<AssetContainerImpl<A>*>(value.get())->asset);
    }
    return std::nullopt;
}
template <typename A>
std::optional<std::reference_wrapper<const A>> ErasedLoadedAsset::get() const {
    if (value && value->type() == meta::type_id<A>{}) {
        return std::cref(static_cast<const AssetContainerImpl<A>*>(value.get())->asset);
    }
    return std::nullopt;
}
template <typename A>
std::optional<A> ErasedLoadedAsset::take() && {
    if (value && value->type() == meta::type_id<A>{}) {
        A result = std::move(static_cast<AssetContainerImpl<A>*>(value.get())->asset);
        value.reset();
        return result;
    }
    return std::nullopt;
}
template <typename A>
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
export template <typename A>
struct LoadedAsset {
   private:
    A value;
    std::unordered_set<UntypedAssetId> dependencies;
    std::unordered_map<AssetPath, std::size_t> loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> labeled_assets;

    friend struct LoadContext;
    friend struct ErasedLoadedAsset;
    template <typename>
    friend struct TransformedAsset;

   public:
    /** @brief Construct from a value. */
    explicit LoadedAsset(A asset) : value(std::move(asset)) {}
    /** @brief Get a const reference to the contained asset. */
    const A& get() const { return value; }
    /** @brief Take the contained asset out. */
    A take() { return std::move(value); }
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

/** @brief A builder for performing nested asset loads within a LoadContext.
 *  Matches bevy_asset's NestedLoader. */
export struct NestedLoader;

export struct LoadContext {
   private:
    const AssetServer& m_server;
    AssetPath m_path;
    std::unordered_set<UntypedAssetId> m_dependencies;
    std::unordered_map<AssetPath, std::size_t> m_loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> m_labeled_assets;

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
    template <typename A>
    Handle<A> add_labeled_asset(const std::string& label, A asset);

    /** @brief Add a pre-loaded labeled asset and return a handle to it.
     *  @tparam A The asset type.
     *  @param label The label string.
     *  @param loaded The already-loaded asset. */
    template <typename A>
    Handle<A> add_loaded_labeled_asset(const std::string& label, LoadedAsset<A> loaded);

    /** @brief Get a nested loader for loading sub-assets from within this load.
     *  Matches bevy_asset's LoadContext::loader(). */
    NestedLoader loader();

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
    template <typename A, typename F>
        requires std::invocable<F, LoadContext&>
    Handle<A> labeled_asset_scope(const std::string& label, F&& fn);

    /** @brief Finish loading, producing a typed LoadedAsset.
     *  @tparam A The asset type.
     *  @param value The loaded asset value. */
    template <typename A>
    LoadedAsset<A> finish(A value) {
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

   public:
    explicit NestedLoader(LoadContext& context) : m_context(context) {}

    /** @brief Load a sub-asset and register it as a dependency.
     *  @tparam A Asset type.
     *  @param path Path of the sub-asset. */
    template <typename A>
    Handle<A> load(const AssetPath& path);

    /** @brief Load a sub-asset by resolving a relative path against the parent's directory.
     *  @tparam A Asset type.
     *  @param relative_path Relative path string. */
    template <typename A>
    Handle<A> load_relative(std::string_view relative_path) {
        return load<A>(m_context.path().resolve(relative_path));
    }
};
export template <typename T>
concept AssetLoader = requires(const T& t, std::istream& stream, LoadContext& context) {
    typename T::Asset;
    typename T::Settings;
    typename T::Error;
    requires std::derived_from<typename T::Settings, Settings>;
    requires std::is_default_constructible_v<typename T::Settings>;
    { t.extensions() } -> std::same_as<std::span<std::string_view>>;
    {
        t.load(stream, std::declval<const typename T::Settings&>(), context)
    } -> std::same_as<std::expected<typename T::Asset, typename T::Error>>;
    { asset_loader_error_to_exception(std::declval<const typename T::Error&>()) } -> std::same_as<std::exception_ptr>;
};
struct ErasedAssetLoader {
    virtual ~ErasedAssetLoader()                                                                  = default;
    virtual std::span<std::string_view> extensions() const                                        = 0;
    virtual meta::type_index loader_type() const                                                  = 0;
    virtual meta::type_index asset_type() const                                                   = 0;
    virtual std::unique_ptr<Settings> default_settings() const                                    = 0;
    virtual std::expected<ErasedLoadedAsset, std::exception_ptr> load(std::istream& stream,
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
    std::unique_ptr<Settings> default_settings() const override { return std::make_unique<typename T::Settings>(); }
    std::expected<ErasedLoadedAsset, std::exception_ptr> load(std::istream& stream,
                                                              const Settings& settings,
                                                              LoadContext& context) const override {
        try {
            auto* settings_ptr = dynamic_cast<const typename T::Settings*>(&settings);
            if (!settings_ptr) {
                throw std::runtime_error("Invalid settings type for loader " + std::string(loader_type().short_name()));
            }
            auto loaded_asset = as_concrete().load(stream, *settings_ptr, context);
            if (!loaded_asset) {
                return std::unexpected(asset_loader_error_to_exception(loaded_asset.error()));
            }
            auto erased_asset = std::make_unique<AssetContainerImpl<typename T::Asset>>(std::move(*loaded_asset));
            return ErasedLoadedAsset{std::move(erased_asset), std::move(context.m_dependencies),
                                     std::move(context.m_loader_dependencies), std::move(context.m_labeled_assets)};
        } catch (...) {
            return std::unexpected(std::current_exception());
        }
    }
};

/** @brief Concept for an asset saver. Savers write assets to a stream.
 *  Implementations must provide: AssetType, Settings, save(). */
export template <typename T>
concept AssetSaver = requires(const T& t, std::ostream& writer, const typename T::Settings& settings) {
    typename T::AssetType;
    typename T::Settings;
    typename T::OutputLoader;
    typename T::Error;
    requires std::derived_from<typename T::Settings, Settings>;
    requires std::is_default_constructible_v<typename T::Settings>;
    {
        t.save(writer, std::declval<SavedAsset<typename T::AssetType>>(), settings, std::declval<const AssetPath&>())
    } -> std::same_as<std::expected<typename T::OutputLoader::Settings, typename T::Error>>;
};

/** @brief Concept for an asset transformer. Transforms one asset type to another.
 *  Implementations must provide: AssetInput, AssetOutput, Settings, transform(). */
export template <typename T>
concept AssetTransformer = requires(const T& t, const typename T::Settings& settings) {
    typename T::AssetInput;
    typename T::AssetOutput;
    typename T::Settings;
    typename T::Error;
    requires std::derived_from<typename T::Settings, Settings>;
    requires std::is_default_constructible_v<typename T::Settings>;
    {
        t.transform(std::declval<TransformedAsset<typename T::AssetInput>>(), settings)
    } -> std::same_as<std::expected<TransformedAsset<typename T::AssetOutput>, typename T::Error>>;
};

// --- Template implementations for LoadContext / NestedLoader ---

inline LoadContext::LoadContext(const AssetServer& server, AssetPath path)
    : m_server(server), m_path(std::move(path)) {}

inline NestedLoader LoadContext::loader() { return NestedLoader(*this); }

template <typename A, typename F>
    requires std::invocable<F, LoadContext&>
Handle<A> LoadContext::labeled_asset_scope(const std::string& label, F&& fn) {
    auto sub_context = begin_labeled_asset();
    A asset          = std::forward<F>(fn)(sub_context);
    return add_labeled_asset<A>(label, std::move(asset));
}

}  // namespace epix::assets