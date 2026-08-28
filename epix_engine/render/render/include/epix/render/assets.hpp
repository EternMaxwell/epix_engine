#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <concepts>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <expected>
#include <exception>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <epix/render/as_bind_group.hpp>
#include <epix/render/extract.hpp>

namespace epix::render {

/** @brief Trait type to specialize for enabling render asset processing.
 *
 * Specialize `RenderAsset<T>` and define `ExtractedAsset`, `ProcessedAsset`,
 * `Param`, `prepare_asset()`, and `usage()` to make type T extractable and
 * processable as a GPU-side render asset. When `ExtractedAsset` differs from
 * T, `extract(const T&, id, reason, previous_asset)` defines the compact data
 * transferred to the render world.
 * @tparam T The source asset type. */
EPIX_EXPORT template <typename T>
struct RenderAsset;

/** @brief Bit flags controlling where a render asset is used
 * (Bevy 0.18 `RenderAssetUsages`). */
EPIX_EXPORT enum RenderAssetUsages : std::uint8_t {
    /** @brief Asset is used in the main world (e.g. CPU access). */
    MAIN_WORLD = 1 << 0,
    /** @brief Asset is used in the render world (e.g. GPU access). */
    RENDER_WORLD = 1 << 1,
};

/** @brief Bevy-compatible failure returned by `take_gpu_data`. */
EPIX_EXPORT enum class AssetExtractionError {
    /** @brief The source's GPU data has already been transferred. */
    AlreadyExtracted,
    /** @brief The render asset does not support RENDER_WORLD-only extraction. */
    NoExtractionImplementation,
};

/** @brief Why a source asset is being copied into the render world. */
EPIX_EXPORT enum class RenderAssetExtractionReason {
    Added,
    Modified,
};

/** @brief Retains an extracted payload for a later preparation attempt
 * (Bevy `PrepareAssetError::RetryNextUpdate`). */
template <typename E>
struct RetryNextAssetUpdate {
    E asset;
};

/** @brief Bevy-shaped preparation failure. The variant itself is the tagged
 * union: no parallel enum discriminator is required. */
template <typename E>
using PrepareAssetError = std::variant<RetryNextAssetUpdate<E>, render_resource::AsBindGroupError>;

/** @brief True when the RenderAsset specialization provides a take_gpu_data
 * hook (Bevy RenderAsset::take_gpu_data): moves heavy data out of the stored
 * asset while retaining its metadata in Assets<T>. Bevy supplies the previous
 * GPU asset so extraction can reject an invalid repeated upload. */
template <typename T>
concept HasTakeGpuData = requires(RenderAsset<T> asset,
                                  T& source,
                                  const typename RenderAsset<T>::ProcessedAsset* previous) {
    { asset.take_gpu_data(source, previous) } -> std::same_as<std::expected<T, AssetExtractionError>>;
};

/** @brief True when a compact extracted representation is explicitly defined. */
template <typename T>
concept HasExtractAsset = requires(RenderAsset<T> asset,
                                   const T& source,
                                   assets::AssetId<T> id,
                                   RenderAssetExtractionReason reason,
                                   const typename RenderAsset<T>::ProcessedAsset* previous_asset) {
    typename RenderAsset<T>::ExtractError;
    { asset.extract(source, id, reason, previous_asset) }
        -> std::same_as<std::expected<typename RenderAsset<T>::ExtractedAsset, typename RenderAsset<T>::ExtractError>>;
};
template <typename T>
concept RenderAssetImpl = requires(RenderAsset<T> asset) {
    requires std::constructible_from<RenderAsset<T>>;
    requires std::is_empty_v<RenderAsset<T>>;
    typename RenderAsset<T>::ProcessedAsset;
    typename RenderAsset<T>::ExtractedAsset;
    typename RenderAsset<T>::Param;
    requires ecs::system_param<typename RenderAsset<T>::Param>;
    {
        asset.prepare_asset(std::declval<typename RenderAsset<T>::ExtractedAsset&&>(),
                            std::declval<assets::AssetId<T>>(),
                            std::declval<typename RenderAsset<T>::Param&>(),
                            std::declval<const typename RenderAsset<T>::ProcessedAsset*>())
    } -> std::same_as<std::expected<typename RenderAsset<T>::ProcessedAsset,
                                    PrepareAssetError<typename RenderAsset<T>::ExtractedAsset>>>;
    { asset.usage(std::declval<const T&>()) } -> std::same_as<RenderAssetUsages>;
    requires std::same_as<typename RenderAsset<T>::ExtractedAsset, T> || HasExtractAsset<T>;
};

/** @brief True when the RenderAsset specialization provides an unload_asset
 * hook called when an asset is removed from RenderAssets (Bevy
 * RenderAsset::unload_asset). */
template <typename T>
concept HasUnloadAsset = requires(RenderAsset<T> asset, assets::AssetId<T> id, typename RenderAsset<T>::Param& param) {
    { asset.unload_asset(id, param) };
};

namespace detail {
template <typename E>
std::string extraction_error_message(const E& error) {
    if constexpr (std::derived_from<std::remove_cvref_t<E>, std::exception>) {
        return error.what();
    } else {
        return std::format("error type {}", meta::type_id<std::remove_cvref_t<E>>().short_name());
    }
}
}  // namespace detail

/** @brief Storage for processed GPU-ready render assets, keyed by asset
 * ID.
 * @tparam T The source asset type (must have a RenderAsset<T>
 * specialization). */
EPIX_EXPORT template <RenderAssetImpl T>
struct RenderAssets {
    using Type = typename RenderAsset<T>::ProcessedAsset;

   public:
    RenderAssets()                               = default;
    RenderAssets(const RenderAssets&)            = delete;
    RenderAssets(RenderAssets&&)                 = default;
    RenderAssets& operator=(const RenderAssets&) = delete;
    RenderAssets& operator=(RenderAssets&&)      = default;

    /** @brief Insert a processed asset by id, replacing any existing entry.
     * Returns the previous asset, if any (Bevy RenderAssets::insert). */
    std::optional<Type> insert(const assets::AssetId<T>& id, Type&& asset) {
        auto it = assets.find(id);
        if (it != assets.end()) {
            Type previous = std::move(it->second);
            it->second    = std::move(asset);
            return previous;
        }
        assets.emplace(id, std::move(asset));
        return std::nullopt;
    }
    /** @brief Emplace a processed asset by id, constructing in place. */
    template <typename... Args>
    void emplace(const assets::AssetId<T>& id, Args&&... args) {
        assets.emplace(id, std::forward<Args>(args)...);
    }
    /** @brief Check if a processed asset exists for the given id. */
    bool contains(const assets::AssetId<T>& id) const { return assets.contains(id); }
    /** @brief Remove the processed asset for the given id. Returns the previous
     * asset, if any (Bevy RenderAssets::remove). */
    std::optional<Type> remove(const assets::AssetId<T>& id) {
        auto it = assets.find(id);
        if (it == assets.end()) return std::nullopt;
        Type previous = std::move(it->second);
        assets.erase(it);
        return previous;
    }
    /** @brief Get the processed asset, or nullptr if absent (Bevy
     * RenderAssets::get -> Option<&T>). */
    Type* get(const assets::AssetId<T>& id) { return try_get(id); }
    /** @brief Get the processed asset, or nullptr if absent. */
    const Type* get(const assets::AssetId<T>& id) const { return try_get(id); }
    /** @brief Try to get a mutable pointer to the processed asset. Returns nullptr if not found. */
    Type* try_get(const assets::AssetId<T>& id) {
        if (auto it = assets.find(id); it != assets.end()) {
            return std::addressof(it->second);
        }
        return nullptr;
    }
    /** @brief Try to get a const pointer to the processed asset. Returns nullptr if not found. */
    const Type* try_get(const assets::AssetId<T>& id) const {
        if (auto it = assets.find(id); it != assets.end()) {
            return std::addressof(it->second);
        }
        return nullptr;
    }
    /** @brief Iterate over all stored (id, asset) pairs. */
    auto iter() { return std::views::all(assets); }
    /** @brief Iterate over all stored (id, asset) pairs (const). */
    auto iter() const { return std::views::all(assets); }

   private:
    std::unordered_map<assets::AssetId<T>, Type> assets;
};

template <RenderAssetImpl T>
struct ExtractedAssets {
    /** @brief Assets extracted this frame (added or modified). */
    std::vector<std::pair<assets::AssetId<T>, typename RenderAsset<T>::ExtractedAsset>> extracted;
    /** @brief IDs removed this frame (Bevy ExtractedAssets::removed). */
    std::unordered_set<assets::AssetId<T>> removed;
    /** @brief IDs added this frame (Bevy ExtractedAssets::added). */
    std::unordered_set<assets::AssetId<T>> added;
    /** @brief IDs modified this frame (Bevy ExtractedAssets::modified). */
    std::unordered_set<assets::AssetId<T>> modified;

    ExtractedAssets()                                  = default;
    ExtractedAssets(const ExtractedAssets&)            = delete;
    ExtractedAssets(ExtractedAssets&&)                 = default;
    ExtractedAssets& operator=(const ExtractedAssets&) = delete;
    ExtractedAssets& operator=(ExtractedAssets&&)      = default;
};

template <RenderAssetImpl T>
void extract_render_asset(ecs::ResMut<ExtractedAssets<T>> cache,
                          app::Extract<ecs::ResMut<assets::Assets<T>>> assets,
                          app::Extract<ecs::EventReader<assets::AssetEvent<T>>> events,
                          ecs::Res<RenderAssets<T>> render_assets) {
    std::unordered_map<assets::AssetId<T>, RenderAssetExtractionReason> changed_assets;
    std::unordered_set<assets::AssetId<T>> removed;
    std::unordered_set<assets::AssetId<T>> added;
    std::unordered_set<assets::AssetId<T>> modified;
    // Bevy render_asset.rs:123-158: Added/Modified extract, Unused removes,
    // Removed is ignored (an asset leaves RenderAssets only via Unused), and
    // LoadedWithDependencies is a TODO (ignored).
    for (const auto& event : events.read()) {
        if (event.is_added()) {
            changed_assets.try_emplace(event.id, RenderAssetExtractionReason::Added);
        } else if (event.is_modified()) {
            // An asset added and then modified before extraction still needs
            // an initial full upload, so retain the earlier Added reason.
            changed_assets.try_emplace(event.id, RenderAssetExtractionReason::Modified);
            modified.insert(event.id);
        } else if (event.is_unused()) {
            changed_assets.erase(event.id);
            modified.erase(event.id);
            removed.insert(event.id);
        }
    }
    std::vector<std::pair<assets::AssetId<T>, typename RenderAsset<T>::ExtractedAsset>> extracted_assets;
    RenderAsset<T> render_asset_impl;
    for (const auto& [id, reason] : changed_assets) {
        if (auto asset = assets->get(id)) {
            const T& source = asset->get();
            const auto usage = render_asset_impl.usage(source);
            const auto previous_asset = render_assets->get(id);
            if (!(usage & RENDER_WORLD)) continue;
            if (usage & MAIN_WORLD) {
                // This asset remains in the main world. A compact
                // ExtractedAsset avoids copying the complete source asset.
                if constexpr (std::same_as<typename RenderAsset<T>::ExtractedAsset, T> &&
                              !std::is_copy_constructible_v<T>) {
                    spdlog::error("Asset [{}] is not copyable; a dual-world RenderAsset with ExtractedAsset == T must be "
                                  "copyable or define a compact ExtractedAsset",
                                  id.to_string());
                } else {
                    if constexpr (std::same_as<typename RenderAsset<T>::ExtractedAsset, T>) {
                        extracted_assets.emplace_back(id, source);
                        added.insert(id);
                    } else {
                        auto payload = render_asset_impl.extract(source, id, reason, previous_asset);
                        if (payload) {
                            extracted_assets.emplace_back(id, std::move(*payload));
                            added.insert(id);
                        } else {
                            spdlog::error("Asset [{}] compact extraction failed: {}", id.to_string(),
                                          detail::extraction_error_message(payload.error()));
                        }
                    }
                }
            } else {
                // Bevy 0.18 RENDER_WORLD-only semantics: take_gpu_data keeps
                // source metadata in Assets<T> and receives the previous GPU
                // asset. There is no remove-untracked fallback.
                if constexpr (HasTakeGpuData<T>) {
                    auto stored = assets->get_mut_untracked(id);
                    if (stored) {
                        auto data = render_asset_impl.take_gpu_data(stored->get(), previous_asset);
                        if (data) {
                            if constexpr (std::same_as<typename RenderAsset<T>::ExtractedAsset, T>) {
                                extracted_assets.emplace_back(id, std::move(*data));
                                added.insert(id);
                            } else {
                                auto payload = render_asset_impl.extract(*data, id, reason, previous_asset);
                                if (payload) {
                                    extracted_assets.emplace_back(id, std::move(*payload));
                                    added.insert(id);
                                } else {
                                    spdlog::error("Asset [{}] compact extraction failed: {}", id.to_string(),
                                                  detail::extraction_error_message(payload.error()));
                                }
                            }
                        } else {
                            spdlog::error("Asset [{}] with RENDER_WORLD usage cannot be extracted: {}", id.to_string(),
                                          data.error() == AssetExtractionError::AlreadyExtracted
                                              ? "already extracted"
                                              : "no extraction implementation");
                        }
                    }
                } else {
                    spdlog::error("Asset [{}] with RENDER_WORLD usage has no take_gpu_data implementation", id.to_string());
                }
            }
        }
    }

    cache->extracted = std::move(extracted_assets);
    cache->removed   = std::move(removed);
    cache->added     = std::move(added);
    cache->modified  = std::move(modified);

}

/**
 * @brief Temporarily stores assets that were extracted but could not be
 * processed this frame due to the per-frame upload budget (Bevy
 * `PrepareNextFrameAssets<A>`).
 */
template <RenderAssetImpl T>
struct PrepareNextFrameAssets {
    /** @brief (id, asset) pairs deferred to the next frame. */
    std::vector<std::pair<assets::AssetId<T>, typename RenderAsset<T>::ExtractedAsset>> pending;

    PrepareNextFrameAssets()                                         = default;
    PrepareNextFrameAssets(const PrepareNextFrameAssets&)            = delete;
    PrepareNextFrameAssets(PrepareNextFrameAssets&&)                 = default;
    PrepareNextFrameAssets& operator=(const PrepareNextFrameAssets&) = delete;
    PrepareNextFrameAssets& operator=(PrepareNextFrameAssets&&)      = default;
};

/**
 * @brief Resource defining the amount of data allowed to be transferred from
 * CPU to GPU each frame, preventing choppy frames at the cost of waiting
 * longer for GPU assets to become available (Bevy 'RenderAssetBytesPerFrame').
 */
EPIX_EXPORT struct RenderAssetBytesPerFrame {
    /** @brief Maximum bytes allowed to be written per frame; nullopt = unlimited. */
    std::optional<std::size_t> max_bytes = std::nullopt;

    /** @brief Create a budget with a byte cap (Bevy
     * RenderAssetBytesPerFrame::new). */
    static RenderAssetBytesPerFrame new_with_max_bytes(std::size_t max_bytes) {
        RenderAssetBytesPerFrame bytes_per_frame;
        bytes_per_frame.max_bytes = max_bytes;
        return bytes_per_frame;
    }
};

/**
 * @brief Render-world resource limiting render asset uploads per frame (Bevy
 * 'RenderAssetBytesPerFrameLimiter'). Populated during extraction from the
 * main-world 'RenderAssetBytesPerFrame'.
 */
EPIX_EXPORT struct RenderAssetBytesPerFrameLimiter {
    /** @brief Maximum bytes allowed per frame; nullopt = unlimited.
     * Copied from 'RenderAssetBytesPerFrame' during extraction. */
    std::optional<std::size_t> max_bytes = std::nullopt;
    /** @brief Bytes written this frame. */
    std::size_t bytes_written = 0;

    /** @brief Reset the bytes-written counter. Only resets when a limit is set (Bevy reset). */
    void reset() noexcept {
        if (max_bytes.has_value()) {
            bytes_written = 0;
        }
    }
    /** @brief True when the per-frame budget has been fully consumed. */
    bool exhausted() const noexcept { return max_bytes.has_value() && bytes_written >= *max_bytes; }
    /** @brief Bytes still available this frame, or SIZE_MAX when unlimited
     * (Bevy RenderAssetBytesPerFrameLimiter::available_bytes). */
    std::size_t available_bytes() const noexcept {
        if (!max_bytes.has_value()) return std::numeric_limits<std::size_t>::max();
        return bytes_written < *max_bytes ? *max_bytes - bytes_written : 0;
    }
    /** @brief Record that `bytes` were written to the GPU this frame. */
    void write_bytes(std::size_t bytes) noexcept {
        if (max_bytes.has_value() && bytes > 0) {
            bytes_written += bytes;
        }
    }
};

/**
 * @brief Extracts the byte counter from the main world into the render world
 * (Bevy `extract_render_asset_bytes_per_frame`).
 */
inline void extract_render_asset_bytes_per_frame(app::Extract<ecs::Res<RenderAssetBytesPerFrame>> bytes_per_frame,
                                                 ecs::ResMut<RenderAssetBytesPerFrameLimiter> limiter) {
    limiter.get_mut().max_bytes = bytes_per_frame.get().max_bytes;
}

/**
 * @brief Resets the per-frame byte budget counter in `RenderSystems::Cleanup`
 * (Bevy `reset_render_asset_bytes_per_frame`).
 */
inline void reset_render_asset_bytes_per_frame(ecs::ResMut<RenderAssetBytesPerFrameLimiter> limiter) {
    limiter.get_mut().reset();
}

namespace detail {
/** @brief True when the RenderAsset specialization reports a byte size
 * (Bevy RenderAsset::byte_len -> Option<usize>). */
template <typename T>
concept HasRenderAssetByteLen = requires(RenderAsset<T> asset, const typename RenderAsset<T>::ExtractedAsset& t) {
    { asset.byte_len(t) } -> std::same_as<std::optional<std::size_t>>;
};

template <typename T>
std::optional<std::size_t> render_asset_byte_len(const RenderAsset<T>& asset,
                                                  const typename RenderAsset<T>::ExtractedAsset& t) noexcept {
    if constexpr (HasRenderAssetByteLen<T>) {
        return asset.byte_len(t);
    } else {
        return std::nullopt;
    }
}
}  // namespace detail

template <RenderAssetImpl T>
void prepare_assets(typename RenderAsset<T>::Param param,
                    ecs::ResMut<RenderAssets<T>> render_assets,
                    ecs::ResMut<ExtractedAssets<T>> extracted_assets,
                    ecs::ResMut<PrepareNextFrameAssets<T>> prepare_next_frame_assets,
                    ecs::ResMut<RenderAssetBytesPerFrameLimiter> bytes_per_frame_limiter) {
    RenderAsset<T> render_asset_impl;
    std::size_t wrote_asset_count = 0;

    // Bevy 'prepare_assets': an asset with byte_len Some(_) is deferred when
    // the per-frame budget is exhausted (even Some(0) still defers), while
    // byte_len None is never throttled. std::nullopt below means "defer".
    auto write_bytes = [&](const typename RenderAsset<T>::ExtractedAsset& asset) -> std::optional<std::size_t> {
        const auto byte_len = detail::render_asset_byte_len(render_asset_impl, asset);
        if (byte_len.has_value() && bytes_per_frame_limiter.get().exhausted()) {
            return std::nullopt;
        }
        return byte_len.value_or(0);
    };

    // First process the assets deferred from the previous frame.
    auto pending = std::move(prepare_next_frame_assets->pending);
    for (auto&& [id, asset] : pending) {
        // Bevy skips queued assets only when they were REMOVED or ADDED this
        // frame (render_asset.rs:361-365); a Modified pending asset is still
        // prepared (old version) and then replaced by the new one.
        bool superseded = extracted_assets->removed.contains(id) || extracted_assets->added.contains(id);
        if (superseded) {
            continue;
        }
        const auto write = write_bytes(asset);
        if (!write.has_value()) {
            // Budget exhausted: keep the old asset visible and retry next frame.
            spdlog::debug("Deferring render asset {} to next frame (budget exhausted, {} bytes available)",
                          id.to_string(), bytes_per_frame_limiter.get().available_bytes());
            prepare_next_frame_assets->pending.emplace_back(id, std::move(asset));
            continue;
        }
        // Bevy passes the previous GPU asset to prepare_asset.
        auto previous = render_assets->get(id);
        auto result   = render_asset_impl.prepare_asset(std::move(asset), id, param, previous);
        if (result) {
            render_assets->insert(id, std::move(*result));
            bytes_per_frame_limiter.get_mut().write_bytes(*write);
            ++wrote_asset_count;
        } else if (auto* retry = std::get_if<RetryNextAssetUpdate<typename RenderAsset<T>::ExtractedAsset>>(&result.error())) {
            prepare_next_frame_assets->pending.emplace_back(id, std::move(retry->asset));
        } else {
            spdlog::error("Render asset {} bind-group construction failed: {}", id.to_string(),
                          static_cast<int>(std::get<render_resource::AsBindGroupError>(result.error())));
        }
    }

    for (const auto& id : extracted_assets->removed) {
        render_assets->remove(id);
        // Bevy calls unload_asset when an asset is removed (render_asset.rs:394-396).
        if constexpr (HasUnloadAsset<T>) {
            render_asset_impl.unload_asset(id, param);
        }
    }
    for (auto&& [id, asset] : extracted_assets->extracted) {
        // Remove the previous version here to ensure that if we are updating
        // the asset then any users will not see the old asset after a new
        // asset is extracted, even if the new asset is not yet ready or we
        // are out of bytes to write (Bevy 'prepare_assets'). The removed
        // value is Bevy's previous_asset, consumed by prepare_asset.
        // The removed value is Bevy's previous_asset, passed to prepare_asset
        // (render_asset.rs:412) so specializations can e.g. copy_on_resize.
        auto previous_asset = render_assets->remove(id);
        const auto write    = write_bytes(asset);
        if (!write.has_value()) {
            spdlog::debug("Deferring render asset {} to next frame (budget exhausted, {} bytes available)",
                          id.to_string(), bytes_per_frame_limiter.get().available_bytes());
            prepare_next_frame_assets->pending.emplace_back(id, std::move(asset));
            continue;
        }
        auto result = render_asset_impl.prepare_asset(std::move(asset), id, param,
                                                      previous_asset ? &*previous_asset : nullptr);
        if (result) {
            render_assets->insert(id, std::move(*result));
            bytes_per_frame_limiter.get_mut().write_bytes(*write);
            ++wrote_asset_count;
        } else if (auto* retry = std::get_if<RetryNextAssetUpdate<typename RenderAsset<T>::ExtractedAsset>>(&result.error())) {
            prepare_next_frame_assets->pending.emplace_back(id, std::move(retry->asset));
        } else {
            spdlog::error("Render asset {} bind-group construction failed: {}", id.to_string(),
                          static_cast<int>(std::get<render_resource::AsBindGroupError>(result.error())));
        }
    }
    extracted_assets->extracted.clear();
    extracted_assets->removed.clear();
    extracted_assets->added.clear();
    extracted_assets->modified.clear();
    (void)wrote_asset_count;
}

/** @brief System set for the asset extraction phase (Bevy 0.18
 * `AssetExtractionSystems`). */
EPIX_EXPORT struct AssetExtractionSystems {};

/** @brief Plugin that sets up extraction and processing of render assets
 * for type T.
 * @tparam T The source asset type (must have a RenderAsset<T>
 * specialization). */
EPIX_EXPORT template <RenderAssetImpl T, typename AFTER = void>
struct RenderAssetPlugin {
    void attach(app::App& app) {
        if (auto render_app = app.get_sub_app_mut(Render)) {
            render_app->get().world_mut().init_resource<RenderAssets<T>>();
            render_app->get().world_mut().init_resource<ExtractedAssets<T>>();
            render_app->get().world_mut().init_resource<PrepareNextFrameAssets<T>>();
            // Bevy: extract_render_asset in ExtractSchedule (AssetExtractionSystems),
            // prepare_assets in the Render schedule's RenderSystems::PrepareAssets
            // (render_asset.rs:142-149) so GPU uploads happen after extract
            // commands are applied and systems can order against preparation.
            render_app->get().add_systems(ExtractSchedule, ecs::into(extract_render_asset<T>)
                                                               .in_set(AssetExtractionSystems{})
                                                               .set_name(std::format("extract render asset<{}>",
                                                                                     meta::type_id<T>().short_name())));
            auto prepare = ecs::into(prepare_assets<T>)
                               .in_set(RenderSystems::PrepareAssets)
                               .set_name(std::format("process render asset<{}>", meta::type_id<T>().short_name()));
            // Bevy RenderAssetDependency: the AFTER asset's prepare_assets runs
            // first (render_asset.rs:101-108).
            if constexpr (!std::is_void_v<AFTER>) {
                static_assert(RenderAssetImpl<AFTER>, "AFTER must be a render asset type");
                render_app->get().add_systems(Render, std::move(prepare).after(ecs::into(prepare_assets<AFTER>)));
            } else {
                render_app->get().add_systems(Render, std::move(prepare));
            }
        }
    }
};
/** @brief `AFTER` orders this asset's prepare_assets after another
 * render asset's (Bevy RenderAssetPlugin<A, AFTER>).
 */

}  // namespace epix::render
