module;

export module epix.assets:transformer;

import std;
import epix.meta;

import :server.loader;

namespace assets {
template <typename A>
struct SavedAsset;

/** @brief A mutable wrapper around an asset and its labeled sub-assets, used during transformation.
 *  Matches bevy_asset's TransformedAsset. */
export template <typename A>
struct TransformedAsset {
   private:
    A m_asset;
    std::unordered_map<std::string, LabeledAsset> m_labeled_assets;

    template <typename>
    friend struct SavedAsset;
    template <typename>
    friend struct TransformedAsset;

   public:
    /** @brief Construct from an asset value. */
    explicit TransformedAsset(A asset) : m_asset(std::move(asset)) {}
    /** @brief Construct from an asset and its labeled sub-assets. */
    TransformedAsset(A asset, std::unordered_map<std::string, LabeledAsset> labeled)
        : m_asset(std::move(asset)), m_labeled_assets(std::move(labeled)) {}

    /** @brief Try to create from a type-erased loaded asset. Returns std::nullopt on type mismatch. */
    static std::optional<TransformedAsset<A>> from_loaded(ErasedLoadedAsset erased) {
        auto loaded = std::move(erased).template downcast<A>();
        if (!loaded) return std::nullopt;
        return TransformedAsset<A>(loaded->take(), std::move(loaded->labeled_assets));
    }

    /** @brief Get a const reference to the wrapped asset. */
    const A& get() const { return m_asset; }
    /** @brief Get a mutable reference to the wrapped asset. */
    A& get_mut() { return m_asset; }
    /** @brief Dereference to the wrapped asset. */
    const A& operator*() const { return m_asset; }
    A& operator*() { return m_asset; }
    const A* operator->() const { return &m_asset; }
    A* operator->() { return &m_asset; }

    /** @brief Replace the contained asset with one of a different type. */
    template <typename B>
    TransformedAsset<B> replace_asset(B new_asset) {
        return TransformedAsset<B>(std::move(new_asset), std::move(m_labeled_assets));
    }

    /** @brief Take labeled assets from another transformed asset. */
    template <typename B>
    void take_labeled_assets(TransformedAsset<B> labeled_source) {
        m_labeled_assets = std::move(labeled_source.m_labeled_assets);
    }

    /** @brief Try to get a labeled sub-asset by label string. */
    template <typename B>
    std::optional<std::reference_wrapper<B>> get_labeled(const std::string& label) {
        auto it = m_labeled_assets.find(label);
        if (it == m_labeled_assets.end()) return std::nullopt;
        return it->second.asset.template get<B>();
    }

    /** @brief Try to get a labeled sub-asset by label string (const). */
    template <typename B>
    std::optional<std::reference_wrapper<const B>> get_labeled(const std::string& label) const {
        auto it = m_labeled_assets.find(label);
        if (it == m_labeled_assets.end()) return std::nullopt;
        return it->second.asset.template get<B>();
    }

    /** @brief Get a type-erased labeled sub-asset by label. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_erased_labeled(const std::string& label) const {
        auto it = m_labeled_assets.find(label);
        if (it == m_labeled_assets.end()) return std::nullopt;
        return std::cref(it->second.asset);
    }

    /** @brief Get a type-erased labeled sub-asset by handle id. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_erased_labeled_by_id(
        const UntypedAssetId& id) const {
        for (const auto& [_, labeled] : m_labeled_assets) {
            if (labeled.handle.id() == id) return std::cref(labeled.asset);
        }
        return std::nullopt;
    }

    /** @brief Try to get a labeled sub-asset by handle id. */
    template <typename B>
    std::optional<std::reference_wrapper<B>> get_labeled_by_id(const UntypedAssetId& id) {
        for (auto& [_, labeled] : m_labeled_assets) {
            if (labeled.handle.id() == id) return labeled.asset.template get<B>();
        }
        return std::nullopt;
    }

    /** @brief Get the untyped handle of a labeled sub-asset. */
    std::optional<UntypedHandle> get_untyped_handle(const std::string& label) const {
        auto it = m_labeled_assets.find(label);
        if (it == m_labeled_assets.end()) return std::nullopt;
        return it->second.handle;
    }

    /** @brief Get the typed handle of a labeled sub-asset. */
    template <typename B>
    std::optional<Handle<B>> get_handle(const std::string& label) const {
        auto handle = get_untyped_handle(label);
        if (!handle) return std::nullopt;
        auto typed = handle->template try_typed<B>();
        if (!typed) return std::nullopt;
        return std::move(*typed);
    }

    /** @brief Add a labeled sub-asset. */
    template <typename B>
    void insert_labeled(const std::string& label, UntypedHandle handle, ErasedLoadedAsset asset) {
        m_labeled_assets.insert_or_assign(label, LabeledAsset{std::move(asset), handle});
    }

    /** @brief Get a range over all label strings. */
    auto labels() const {
        return m_labeled_assets |
               std::views::transform([](const auto& pair) -> const std::string& { return pair.first; });
    }
};

/** @brief A sub-asset reference for use during transformation.
 *  Matches bevy_asset's TransformedSubAsset. */
export template <typename A>
struct TransformedSubAsset {
   private:
    std::reference_wrapper<A> m_asset;
    std::reference_wrapper<std::unordered_map<std::string, LabeledAsset>> m_labeled;

   public:
    TransformedSubAsset(A& asset, std::unordered_map<std::string, LabeledAsset>& labeled)
        : m_asset(asset), m_labeled(labeled) {}

    static std::optional<TransformedSubAsset<A>> from_loaded(ErasedLoadedAsset& asset) {
        auto value = asset.template get<A>();
        if (!value) return std::nullopt;
        return TransformedSubAsset<A>(value->get(), asset.labeled_assets);
    }

    const A& get() const { return m_asset.get(); }
    A& get_mut() { return m_asset.get(); }
    const A& operator*() const { return m_asset.get(); }
    A& operator*() { return m_asset.get(); }
    const A* operator->() const { return &m_asset.get(); }
    A* operator->() { return &m_asset.get(); }

    /** @brief Try to get a nested labeled sub-asset by label string. */
    template <typename B>
    std::optional<TransformedSubAsset<B>> get_labeled(const std::string& label) {
        auto it = m_labeled.get().find(label);
        if (it == m_labeled.get().end()) return std::nullopt;
        auto value = it->second.asset.template get<B>();
        if (!value) return std::nullopt;
        return TransformedSubAsset<B>(value->get(), it->second.asset.labeled_assets);
    }

    /** @brief Get a type-erased nested labeled sub-asset by label. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_erased_labeled(const std::string& label) const {
        auto it = m_labeled.get().find(label);
        if (it == m_labeled.get().end()) return std::nullopt;
        return std::cref(it->second.asset);
    }

    /** @brief Try to get a nested labeled sub-asset by handle id. */
    template <typename B>
    std::optional<TransformedSubAsset<B>> get_labeled_by_id(const UntypedAssetId& id) {
        for (auto& [_, labeled] : m_labeled.get()) {
            if (labeled.handle.id() != id) continue;
            auto value = labeled.asset.template get<B>();
            if (!value) return std::nullopt;
            return TransformedSubAsset<B>(value->get(), labeled.asset.labeled_assets);
        }
        return std::nullopt;
    }

    /** @brief Get a type-erased nested labeled sub-asset by handle id. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_erased_labeled_by_id(
        const UntypedAssetId& id) const {
        for (const auto& [_, labeled] : m_labeled.get()) {
            if (labeled.handle.id() == id) return std::cref(labeled.asset);
        }
        return std::nullopt;
    }

    /** @brief Get the untyped handle of a nested labeled sub-asset. */
    std::optional<UntypedHandle> get_untyped_handle(const std::string& label) const {
        auto it = m_labeled.get().find(label);
        if (it == m_labeled.get().end()) return std::nullopt;
        return it->second.handle;
    }

    /** @brief Get the typed handle of a nested labeled sub-asset. */
    template <typename B>
    std::optional<Handle<B>> get_handle(const std::string& label) const {
        auto handle = get_untyped_handle(label);
        if (!handle) return std::nullopt;
        auto typed = handle->template try_typed<B>();
        if (!typed) return std::nullopt;
        return std::move(*typed);
    }

    /** @brief Insert or replace a nested labeled sub-asset. */
    void insert_labeled(const std::string& label, UntypedHandle handle, ErasedLoadedAsset asset) {
        m_labeled.get().insert_or_assign(label, LabeledAsset{std::move(asset), handle});
    }

    /** @brief Get a range over all nested label strings. */
    auto labels() const {
        return m_labeled.get() |
               std::views::transform([](const auto& pair) -> const std::string& { return pair.first; });
    }
};

/** @brief An identity transformer that passes through the input asset unchanged.
 *  Matches bevy_asset's IdentityAssetTransformer. */
export template <typename A>
struct IdentityAssetTransformer {
    using AssetInput  = A;
    using AssetOutput = A;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    std::expected<TransformedAsset<A>, Error> transform(TransformedAsset<A> asset, const Settings&) const {
        return asset;
    }
};

}  // namespace assets
