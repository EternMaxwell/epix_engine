module;

export module epix.assets:server.loader;

import std;
import epix.meta;

import :store;

namespace assets {
struct AssetServer;

struct AssetContainer {
    virtual ~AssetContainer()                                         = default;
    virtual meta::type_index type() const                             = 0;
    virtual void insert(const UntypedAssetId& id, core::World& world) = 0;
};
struct LabeledAsset;
struct ErasedLoadedAsset {
    std::unique_ptr<AssetContainer> value;
    std::unordered_set<UntypedAssetId> dependencies;
    std::unordered_map<AssetPath, std::size_t> loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> labeled_assets;
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
        world.resource_mut<Assets<T>>().insert(id.typed<T>(), std::move(asset));
    }
};
export struct LoadContext {
   private:
    const AssetServer& m_server;
    std::filesystem::path m_path;
    std::unordered_map<std::filesystem::path, UntypedAssetId> m_dependencies;
    std::unordered_map<AssetPath, std::size_t> m_loader_dependencies;
    std::unordered_map<std::string, LabeledAsset> m_labeled_assets;

    LoadContext(const AssetServer& server, std::filesystem::path path) : m_server(server), m_path(std::move(path)) {}

    friend struct AssetServer;

   public:
};
export struct Settings {
    virtual ~Settings() = default;
};
export template <typename T>
concept AssetLoader = requires(const T& t, std::istream& stream, const LoadContext& context) {
    typename T::AssetType;
    typename T::Settings;
    requires std::derived_from<typename T::Settings, Settings>;
    requires std::is_default_constructible_v<typename T::Settings>;
    { t.extensions() } -> std::same_as<std::span<std::string_view>>;
    { t.load(stream, std::declval<const typename T::Settings&>(), context) } -> std::same_as<T::AssetType>;
};
struct ErasedAssetLoader {
    virtual ~ErasedAssetLoader()                                                                  = default;
    virtual std::span<std::string_view> extensions() const                                        = 0;
    virtual meta::type_index loader_type() const                                                  = 0;
    virtual meta::type_index asset_type() const                                                   = 0;
    virtual std::expected<ErasedLoadedAsset, std::exception_ptr> load(std::istream& stream,
                                                                      const Settings& settings,
                                                                      LoadContext& context) const = 0;
};
template <AssetLoader T>
struct ErasedAssetLoaderImpl : T, ErasedAssetLoader {
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    ErasedAssetLoaderImpl(Args&&... args) : T(std::forward<Args>(args)...) {}
    const T& as_concrete() const { return static_cast<const T&>(*this); }
    std::span<std::string_view> extensions() const override { return as_concrete().extensions(); }
    meta::type_index loader_type() const override { return meta::type_id<T>{}; }
    meta::type_index asset_type() const override { return meta::type_id<typename T::AssetType>{}; }
    std::expected<ErasedLoadedAsset, std::exception_ptr> load(std::istream& stream,
                                                              const Settings& settings,
                                                              LoadContext& context) const override {
        try {
            auto* settings_ptr = dynamic_cast<const typename T::Settings*>(&settings);
            if (!settings_ptr) {
                throw std::runtime_error("Invalid settings type for loader " + std::string(loader_type().short_name()));
            }
            auto erased_asset =
                std::make_unique<AssetContainerImpl<T::AssetType>>(as_concrete().load(stream, *settings_ptr, context));
            return ErasedLoadedAsset{std::move(erased_asset), std::move(context.m_dependencies),
                                     std::move(context.m_loader_dependencies), std::move(context.m_labeled_assets)};
        } catch (...) {
            return std::current_exception();
        }
    }
};
}  // namespace assets