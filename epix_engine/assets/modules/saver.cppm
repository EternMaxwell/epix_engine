module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <exception>
#include <expected>
#include <functional>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#endif
#include <asio/awaitable.hpp>

export module epix.assets:saver;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import :concepts;

import :server.loader;
import :transformer;

namespace epix::assets {

/** @brief A read-only view of an asset and its labeled sub-assets, used during saving.
 *  Matches bevy_asset's SavedAsset. */
export template <Asset A>
struct SavedAsset {
   private:
    std::reference_wrapper<const A> m_asset;
    std::reference_wrapper<const std::unordered_map<std::string, LabeledAsset>> m_labeled;

    SavedAsset(const A& asset, const std::unordered_map<std::string, LabeledAsset>& labeled)
        : m_asset(asset), m_labeled(labeled) {}

   public:
    /** @brief Create from a type-erased loaded asset. Throws on type mismatch. */
    static SavedAsset from_loaded(const ErasedLoadedAsset& loaded) {
        auto typed = loaded.template get<A>();
        if (!typed) throw std::runtime_error("SavedAsset::from_loaded type mismatch");
        return SavedAsset(typed->get(), loaded.labeled_assets);
    }

    /** @brief Create from a transformed asset. */
    static SavedAsset from_transformed(const TransformedAsset<A>& ta) {
        return SavedAsset(ta.m_asset, ta.m_labeled_assets);
    }

    /** @brief Create from just the asset value with no labeled assets. */
    static SavedAsset from_asset(const A& asset) {
        static const std::unordered_map<std::string, LabeledAsset> empty_labeled;
        return SavedAsset(asset, empty_labeled);
    }

    /** @brief Access the underlying asset. */
    const A& get() const { return m_asset.get(); }
    /** @brief Dereference to the underlying asset. */
    const A& operator*() const { return m_asset.get(); }
    const A* operator->() const { return &m_asset.get(); }

    /** @brief Try to get a labeled sub-asset by label. */
    template <Asset B>
    std::optional<SavedAsset<B>> get_labeled(const std::string& label) const {
        auto it = m_labeled.get().find(label);
        if (it == m_labeled.get().end()) return std::nullopt;
        return SavedAsset<B>::from_loaded(it->second.asset);
    }

    /** @brief Get a type-erased labeled sub-asset by label. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_erased_labeled(const std::string& label) const {
        auto it = m_labeled.get().find(label);
        if (it == m_labeled.get().end()) return std::nullopt;
        return std::cref(it->second.asset);
    }

    /** @brief Try to get a labeled sub-asset by handle id. */
    template <Asset B>
    std::optional<SavedAsset<B>> get_labeled_by_id(const UntypedAssetId& id) const {
        for (const auto& [_, labeled] : m_labeled.get()) {
            if (labeled.handle.id() == id) return SavedAsset<B>::from_loaded(labeled.asset);
        }
        return std::nullopt;
    }

    /** @brief Get a type-erased labeled sub-asset by handle id. */
    std::optional<std::reference_wrapper<const ErasedLoadedAsset>> get_erased_labeled_by_id(
        const UntypedAssetId& id) const {
        for (const auto& [_, labeled] : m_labeled.get()) {
            if (labeled.handle.id() == id) return std::cref(labeled.asset);
        }
        return std::nullopt;
    }

    /** @brief Get the untyped handle of a labeled sub-asset by label. */
    std::optional<UntypedHandle> get_untyped_handle(const std::string& label) const {
        auto it = m_labeled.get().find(label);
        if (it == m_labeled.get().end()) return std::nullopt;
        return it->second.handle;
    }

    /** @brief Get the typed handle of a labeled sub-asset by label. */
    template <Asset B>
    std::optional<Handle<B>> get_handle(const std::string& label) const {
        auto handle = get_untyped_handle(label);
        if (!handle) return std::nullopt;
        auto typed = handle->template try_typed<B>();
        if (!typed) return std::nullopt;
        return std::move(*typed);
    }

    /** @brief Get a range over all label strings. */
    auto labels() const {
        return m_labeled.get() |
               std::views::transform([](const auto& pair) -> const std::string& { return pair.first; });
    }
};

/** @brief Type-erased asset saver interface, analogous to bevy_asset's ErasedAssetSaver. */
export struct ErasedAssetSaver {
    virtual ~ErasedAssetSaver() = default;
    /** @brief Save a type-erased loaded asset to an async Writer. */
    virtual asio::awaitable<std::expected<void, std::exception_ptr>> save(Writer& writer,
                                                                          const ErasedLoadedAsset& asset,
                                                                          const Settings& settings,
                                                                          const AssetPath& asset_path) const = 0;
    /** @brief Get the type name of this saver. */
    virtual std::string_view type_name() const = 0;
};

/** @brief Blanket implementation of ErasedAssetSaver for any type satisfying the AssetSaver concept. */
template <AssetSaver T>
struct ErasedAssetSaverImpl : T, ErasedAssetSaver {
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    ErasedAssetSaverImpl(Args&&... args) : T(std::forward<Args>(args)...) {}

    const T& as_concrete() const { return static_cast<const T&>(*this); }

    asio::awaitable<std::expected<void, std::exception_ptr>> save(Writer& writer,
                                                                  const ErasedLoadedAsset& asset,
                                                                  const Settings& settings,
                                                                  const AssetPath& asset_path) const override {
        try {
            auto* typed_settings = dynamic_cast<const typename T::Settings*>(&settings);
            if (!typed_settings) {
                throw std::runtime_error("Settings type mismatch in saver");
            }
            auto saved  = SavedAsset<typename T::Asset>::from_loaded(asset);
            auto result = co_await as_concrete().save(writer, saved, *typed_settings, asset_path);
            if (!result) {
                throw std::runtime_error("Asset saver failed");
            }
            co_return std::expected<void, std::exception_ptr>{};
        } catch (...) {
            co_return std::unexpected(std::current_exception());
        }
    }

    std::string_view type_name() const override { return meta::type_id<T>{}.short_name(); }
};

}  // namespace epix::assets
