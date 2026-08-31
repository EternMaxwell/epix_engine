#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <concepts>
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <expected>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <epix/render/as_bind_group.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/extract.hpp>
#include <epix/render/schedule.hpp>

namespace epix::render::erased_render_asset {

/**
 * @brief Error returned by ErasedRenderAsset::prepare_asset (Bevy
 * `PrepareAssetError<E>`, erased_render_asset.rs:20-26): retry the asset next
 * update, or report a bind-group construction failure.
 */
template <typename E>
struct RetryNextUpdate {
    E asset;
};

/** @brief Bevy's tagged preparation error, represented directly as a variant.
 * No parallel discriminator is necessary. */
template <typename E>
using PrepareAssetError = std::variant<RetryNextUpdate<E>, GpuAssetCreationError, render_resource::AsBindGroupError>;

/** @brief Trait type to specialize for enabling erased render asset processing.

 * Specialize `ErasedRenderAsset<A>` and define `SourceAsset`,
 * `ExtractedAsset`, `ErasedAsset`, `Param`, and the static-like
 * `prepare_asset` member to make source assets extractable and processable as
 * type-erased GPU render assets (Bevy `ErasedRenderAsset`). When
 * `ExtractedAsset` differs from `SourceAsset`, `extract(source, id, reason,
 * previous_asset)` defines the compact data transferred to the render world.
 * Epix additionally supplies the prior `ErasedAsset` to `prepare_asset` so a
 * specialization can reuse GPU allocations across updates.
 * @tparam A The adapter type identifying this pipeline. */
template <typename A>
struct ErasedRenderAsset;

/** @brief True when the specialization overrides asset_usage (Bevy
 * ErasedRenderAsset::asset_usage has a default). */
template <typename A>
concept HasErasedAssetUsage =
    requires(ErasedRenderAsset<A> asset, const typename ErasedRenderAsset<A>::SourceAsset& source) {
        { asset.asset_usage(source) } -> std::same_as<RenderAssetUsages>;
    };

/** @brief True when the specialization overrides byte_len (Bevy
 * ErasedRenderAsset::byte_len has a default of None). */
template <typename A>
concept HasErasedByteLen =
    requires(ErasedRenderAsset<A> asset, const typename ErasedRenderAsset<A>::ExtractedAsset& extracted) {
        { asset.byte_len(extracted) } -> std::same_as<std::optional<std::size_t>>;
    };

/** @brief True when the specialization overrides unload_asset (Bevy
 * ErasedRenderAsset::unload_asset has a default of no-op). */
template <typename A>
concept HasErasedUnload = requires(ErasedRenderAsset<A> asset,
                                   const assets::AssetId<typename ErasedRenderAsset<A>::SourceAsset>& id,
                                   typename ErasedRenderAsset<A>::Param& param) {
    { asset.unload_asset(id, param) };
};

/** @brief True when a compact extracted representation is explicitly defined.
 * This is Epix's counterpart to the RenderAsset compact extraction extension. */
template <typename A>
concept HasErasedExtractAsset =
    requires(ErasedRenderAsset<A> asset,
             const typename ErasedRenderAsset<A>::SourceAsset& source,
             assets::AssetId<typename ErasedRenderAsset<A>::SourceAsset> id,
             RenderAssetExtractionReason reason,
             const typename ErasedRenderAsset<A>::ErasedAsset* previous_asset) {
        typename ErasedRenderAsset<A>::ExtractError;
        { asset.extract(source, id, reason, previous_asset) }
            -> std::same_as<std::expected<typename ErasedRenderAsset<A>::ExtractedAsset,
                                           typename ErasedRenderAsset<A>::ExtractError>>;
    };

/** @brief Concept satisfied by valid ErasedRenderAsset specializations. */
template <typename A>
concept ErasedRenderAssetImpl = requires(ErasedRenderAsset<A> asset) {
    requires std::constructible_from<ErasedRenderAsset<A>>;
    requires std::is_empty_v<ErasedRenderAsset<A>>;
    typename ErasedRenderAsset<A>::SourceAsset;
    typename ErasedRenderAsset<A>::ExtractedAsset;
    typename ErasedRenderAsset<A>::ErasedAsset;
    typename ErasedRenderAsset<A>::Param;
    requires ecs::system_param<typename ErasedRenderAsset<A>::Param>;
    {
        asset.prepare_asset(std::declval<typename ErasedRenderAsset<A>::ExtractedAsset&&>(),
                            std::declval<const assets::AssetId<typename ErasedRenderAsset<A>::SourceAsset>&>(),
                            std::declval<typename ErasedRenderAsset<A>::Param&>(),
                            std::declval<const typename ErasedRenderAsset<A>::ErasedAsset*>())
    } -> std::same_as<std::expected<typename ErasedRenderAsset<A>::ErasedAsset,
                                    PrepareAssetError<typename ErasedRenderAsset<A>::ExtractedAsset>>>;
    requires std::same_as<typename ErasedRenderAsset<A>::ExtractedAsset,
                          typename ErasedRenderAsset<A>::SourceAsset> || HasErasedExtractAsset<A>;
};

namespace detail {
template <typename A>
RenderAssetUsages erased_asset_usage(const ErasedRenderAsset<A>& asset,
                                     const typename ErasedRenderAsset<A>::SourceAsset& source) {
    if constexpr (HasErasedAssetUsage<A>) {
        return asset.asset_usage(source);
    } else {
        // Bevy default: RenderAssetUsages::default() (empty).
        (void)asset;
        (void)source;
        return RenderAssetUsages{};
    }
}

template <typename A>
std::optional<std::size_t> erased_asset_byte_len(const ErasedRenderAsset<A>& asset,
                                                 const typename ErasedRenderAsset<A>::ExtractedAsset& extracted) noexcept {
    if constexpr (HasErasedByteLen<A>) {
        return asset.byte_len(extracted);
    } else {
        (void)asset;
        (void)extracted;
        return std::nullopt;
    }
}
}  // namespace detail

/**
 * @brief Storage for processed GPU-ready erased render assets, keyed by
 * untyped asset id (Bevy `ErasedRenderAssets<ERA>`,
 * erased_render_asset.rs:192-224).
 */
template <typename ERA>
struct ErasedRenderAssets {
    std::unordered_map<assets::UntypedAssetId, ERA> assets;

    ERA* get(const assets::UntypedAssetId& id) {
        if (auto it = assets.find(id); it != assets.end()) return &it->second;
        return nullptr;
    }
    const ERA* get(const assets::UntypedAssetId& id) const {
        if (auto it = assets.find(id); it != assets.end()) return &it->second;
        return nullptr;
    }
    ERA* get_mut(const assets::UntypedAssetId& id) {
        if (auto it = assets.find(id); it != assets.end()) return &it->second;
        return nullptr;
    }
    /** @brief Insert a processed asset, returning the previous one (Bevy
     * ErasedRenderAssets::insert). */
    std::optional<ERA> insert(const assets::UntypedAssetId& id, ERA value) {
        auto it = assets.find(id);
        if (it != assets.end()) {
            ERA previous = std::move(it->second);
            it->second   = std::move(value);
            return previous;
        }
        assets.emplace(id, std::move(value));
        return std::nullopt;
    }
    /** @brief Remove a processed asset, returning the previous one (Bevy
     * ErasedRenderAssets::remove). */
    std::optional<ERA> remove(const assets::UntypedAssetId& id) {
        auto it = assets.find(id);
        if (it == assets.end()) return std::nullopt;
        ERA previous = std::move(it->second);
        assets.erase(it);
        return previous;
    }
    auto iter() { return std::views::all(assets); }
    auto iter() const { return std::views::all(assets); }
};

/** @brief Temporarily stores the extracted and removed assets of the current
 * frame (Bevy `ExtractedAssets<A>`, erased_render_asset.rs:159-187). */
template <ErasedRenderAssetImpl A>
struct ExtractedAssets {
    using SourceAsset = typename ErasedRenderAsset<A>::SourceAsset;
    using ExtractedAsset = typename ErasedRenderAsset<A>::ExtractedAsset;

    /** @brief Assets extracted this frame (added or modified). */
    std::vector<std::pair<assets::AssetId<SourceAsset>, ExtractedAsset>> extracted;
    /** @brief IDs of the assets removed this frame (via Unused). */
    std::unordered_set<assets::AssetId<SourceAsset>> removed;
    /** @brief IDs of the assets modified this frame. */
    std::unordered_set<assets::AssetId<SourceAsset>> modified;
    /** @brief IDs of the assets added this frame. */
    std::unordered_set<assets::AssetId<SourceAsset>> added;
};

/** @brief All assets that should be prepared next frame (Bevy
 * `PrepareNextFrameAssets<A>`, erased_render_asset.rs:317-327). */
template <ErasedRenderAssetImpl A>
struct PrepareNextFrameAssets {
    using SourceAsset = typename ErasedRenderAsset<A>::SourceAsset;
    using ExtractedAsset = typename ErasedRenderAsset<A>::ExtractedAsset;

    std::vector<std::pair<assets::AssetId<SourceAsset>, ExtractedAsset>> assets;
};

/**
 * @brief Extracts all created or modified assets of the corresponding
 * ErasedRenderAsset source type into the render world (Bevy
 * `extract_erased_render_asset`, erased_render_asset.rs:244-312).
 */
template <ErasedRenderAssetImpl A>
void extract_erased_render_asset(
    ecs::ResMut<ExtractedAssets<A>> cache,
    app::Extract<ecs::ResMut<assets::Assets<typename ErasedRenderAsset<A>::SourceAsset>>> assets,
    app::Extract<ecs::EventReader<assets::AssetEvent<typename ErasedRenderAsset<A>::SourceAsset>>> events,
    ecs::Res<ErasedRenderAssets<typename ErasedRenderAsset<A>::ErasedAsset>> render_assets) {
    using SourceAsset = typename ErasedRenderAsset<A>::SourceAsset;
    using ExtractedAsset = typename ErasedRenderAsset<A>::ExtractedAsset;
    using ErasedAsset = typename ErasedRenderAsset<A>::ErasedAsset;

    std::unordered_map<assets::AssetId<SourceAsset>, RenderAssetExtractionReason> changed_assets;
    std::unordered_set<assets::AssetId<SourceAsset>> removed;
    std::unordered_set<assets::AssetId<SourceAsset>> modified;
    for (const auto& event : events.read()) {
        if (event.is_added()) {
            changed_assets.try_emplace(event.id, RenderAssetExtractionReason::Added);
        } else if (event.is_modified()) {
            // Preserve Added when both events occur before this extraction.
            changed_assets.try_emplace(event.id, RenderAssetExtractionReason::Modified);
            modified.insert(event.id);
        } else if (event.is_removed()) {
            // An asset only leaves ErasedRenderAssets when its last handle is
            // dropped (AssetEvent::Unused); Removed is ignored.
        } else if (event.is_unused()) {
            changed_assets.erase(event.id);
            modified.erase(event.id);
            removed.insert(event.id);
        }
    }

    std::vector<std::pair<assets::AssetId<SourceAsset>, ExtractedAsset>> extracted;
    std::unordered_set<assets::AssetId<SourceAsset>> added;
    ErasedRenderAsset<A> impl;
    for (const auto& [id, reason] : changed_assets) {
        if (auto asset = assets->get(id)) {
            const RenderAssetUsages asset_usage = detail::erased_asset_usage(impl, *asset);
            const ErasedAsset* previous_asset = render_assets->get(assets::UntypedAssetId(id));
            if (asset_usage & RenderAssetUsages::RENDER_WORLD) {
                if (asset_usage == RenderAssetUsages::RENDER_WORLD) {
                    // Bevy's RENDER_WORLD-only erased asset semantics move
                    // the source out of Assets. A compact payload is then
                    // derived from that moved source before it is discarded.
                    if (auto moved = assets->remove_untracked(id)) {
                        if constexpr (std::same_as<ExtractedAsset, SourceAsset>) {
                            extracted.emplace_back(id, std::move(*moved));
                            added.insert(id);
                        } else {
                            auto payload = impl.extract(*moved, id, reason, previous_asset);
                            if (payload) {
                                extracted.emplace_back(id, std::move(*payload));
                                added.insert(id);
                            } else {
                                spdlog::error("Erased render asset [{}] compact extraction failed: {}", id.to_string(),
                                              ::epix::render::detail::extraction_error_message(payload.error()));
                            }
                        }
                    }
                } else {
                    // A source retained by the main world needs either a
                    // copy of itself or a compact extraction payload.
                    if constexpr (std::same_as<ExtractedAsset, SourceAsset> && !std::is_copy_constructible_v<SourceAsset>) {
                        spdlog::error("Erased render asset [{}] is not copyable; a dual-world asset with "
                                      "ExtractedAsset == SourceAsset must be copyable or define a compact ExtractedAsset",
                                      id.to_string());
                    } else if constexpr (std::same_as<ExtractedAsset, SourceAsset>) {
                        extracted.emplace_back(id, *asset);
                        added.insert(id);
                    } else {
                        auto payload = impl.extract(*asset, id, reason, previous_asset);
                        if (payload) {
                            extracted.emplace_back(id, std::move(*payload));
                            added.insert(id);
                        } else {
                            spdlog::error("Erased render asset [{}] compact extraction failed: {}", id.to_string(),
                                          ::epix::render::detail::extraction_error_message(payload.error()));
                        }
                    }
                }
            }
        }
    }

    cache->extracted = std::move(extracted);
    cache->removed   = std::move(removed);
    cache->modified  = std::move(modified);
    cache->added     = std::move(added);
}

/**
 * @brief Prepares all assets of the corresponding ErasedRenderAsset source
 * type which were extracted this frame for the GPU (Bevy
 * `prepare_erased_assets`, erased_render_asset.rs:331-426).
 */
template <ErasedRenderAssetImpl A>
void prepare_erased_assets(typename ErasedRenderAsset<A>::Param param,
                           ecs::ResMut<ExtractedAssets<A>> extracted_assets,
                           ecs::ResMut<ErasedRenderAssets<typename ErasedRenderAsset<A>::ErasedAsset>> render_assets,
                           ecs::ResMut<PrepareNextFrameAssets<A>> prepare_next_frame,
                           ecs::ResMut<RenderAssetBytesPerFrameLimiter> bytes_per_frame_limiter) {
    using SourceAsset = typename ErasedRenderAsset<A>::SourceAsset;
    using ExtractedAsset = typename ErasedRenderAsset<A>::ExtractedAsset;
    ErasedRenderAsset<A> impl;
    std::size_t wrote_asset_count = 0;

    // Bevy erased_render_asset.rs:348-360: an asset with byte_len Some(_) is
    // deferred when the per-frame budget is exhausted (even Some(0) defers);
    // byte_len None is never throttled. std::nullopt below means "defer".
    auto write_bytes = [&](const ExtractedAsset& asset) -> std::optional<std::size_t> {
        const auto byte_len = detail::erased_asset_byte_len(impl, asset);
        if (byte_len.has_value() && bytes_per_frame_limiter.get().exhausted()) {
            return std::nullopt;
        }
        return byte_len.value_or(0);
    };

    // First prepare the assets deferred from the previous frame.
    auto queued = std::move(prepare_next_frame->assets);
    for (auto&& [id, asset] : queued) {
        // Bevy skips queued assets only when they were REMOVED or ADDED this
        // frame (erased_render_asset.rs:343-346).
        bool superseded = extracted_assets->removed.contains(id) || extracted_assets->added.contains(id);
        if (superseded) {
            continue;
        }
        const auto write = write_bytes(asset);
        if (!write.has_value()) {
            prepare_next_frame->assets.emplace_back(id, std::move(asset));
            continue;
        }
        auto result = impl.prepare_asset(std::move(asset), id, param,
                                         render_assets->get(assets::UntypedAssetId(id)));
        if (result.has_value()) {
            render_assets->insert(assets::UntypedAssetId(id), std::move(*result));
            bytes_per_frame_limiter.get_mut().write_bytes(*write);
            ++wrote_asset_count;
        } else if (auto* retry = std::get_if<RetryNextUpdate<ExtractedAsset>>(&result.error())) {
            prepare_next_frame->assets.emplace_back(id, std::move(retry->asset));
        } else if (const auto* creation_error = std::get_if<GpuAssetCreationError>(&result.error())) {
            spdlog::error("ErasedRenderAsset<{}> GPU creation failed: {}", meta::type_id<A>().short_name(),
                          gpu_asset_creation_error_message(*creation_error));
        } else {
            spdlog::error("ErasedRenderAsset<{}> bind group construction failed: {}", meta::type_id<A>().short_name(),
                          render_resource::as_bind_group_error_message(
                              std::get<render_resource::AsBindGroupError>(result.error())));
        }
    }

    for (const auto& id : extracted_assets->removed) {
        render_assets->remove(assets::UntypedAssetId(id));
        if constexpr (HasErasedUnload<A>) {
            impl.unload_asset(id, param);
        }
    }

    for (auto&& [id, asset] : extracted_assets->extracted) {
        // Remove the previous version first so users never see the old asset
        // after a new one is extracted, even if it is not ready or the budget
        // is exhausted (Bevy erased_render_asset.rs:385-389).
        auto previous_asset = render_assets->remove(assets::UntypedAssetId(id));
        const auto write = write_bytes(asset);
        if (!write.has_value()) {
            prepare_next_frame->assets.emplace_back(id, std::move(asset));
            continue;
        }
        auto result = impl.prepare_asset(std::move(asset), id, param, previous_asset ? &*previous_asset : nullptr);
        if (result.has_value()) {
            render_assets->insert(assets::UntypedAssetId(id), std::move(*result));
            bytes_per_frame_limiter.get_mut().write_bytes(*write);
            ++wrote_asset_count;
        } else if (auto* retry = std::get_if<RetryNextUpdate<ExtractedAsset>>(&result.error())) {
            prepare_next_frame->assets.emplace_back(id, std::move(retry->asset));
        } else if (const auto* creation_error = std::get_if<GpuAssetCreationError>(&result.error())) {
            spdlog::error("ErasedRenderAsset<{}> GPU creation failed: {}", meta::type_id<A>().short_name(),
                          gpu_asset_creation_error_message(*creation_error));
        } else {
            spdlog::error("ErasedRenderAsset<{}> bind group construction failed: {}", meta::type_id<A>().short_name(),
                          render_resource::as_bind_group_error_message(
                              std::get<render_resource::AsBindGroupError>(result.error())));
        }
    }

    extracted_assets->extracted.clear();
    extracted_assets->removed.clear();
    extracted_assets->added.clear();
    extracted_assets->modified.clear();
    (void)wrote_asset_count;
}

/**
 * @brief Plugin that extracts the changed assets of type A from the app world
 * into the render world and prepares them for the GPU (Bevy
 * `ErasedRenderAssetPlugin<A, AFTER>`, erased_render_asset.rs:99-139). The
 * AFTER template parameter orders this pipeline's prepare_erased_assets after
 * another's.
 */
template <ErasedRenderAssetImpl A, typename AFTER = void>
struct ErasedRenderAssetPlugin {
    void attach(app::App& app) {
        if (auto render_app = app.get_sub_app_mut(Render)) {
            render_app->get().world_mut().init_resource<ExtractedAssets<A>>();
            render_app->get()
                .world_mut()
                .init_resource<ErasedRenderAssets<typename ErasedRenderAsset<A>::ErasedAsset>>();
            render_app->get().world_mut().init_resource<PrepareNextFrameAssets<A>>();
            render_app->get().add_systems(ExtractSchedule, ecs::into(extract_erased_render_asset<A>)
                                                               .in_set(AssetExtractionSystems{})
                                                               .set_name(std::format("extract erased render asset<{}>",
                                                                                     meta::type_id<A>().short_name())));
            auto prepare = ecs::into(prepare_erased_assets<A>)
                               .in_set(RenderSystems::PrepareAssets)
                               .set_name(std::format("prepare erased assets<{}>", meta::type_id<A>().short_name()));
            // Bevy ErasedRenderAssetDependency: the AFTER pipeline's
            // prepare_erased_assets runs first (erased_render_asset.rs:142-156).
            if constexpr (!std::is_void_v<AFTER>) {
                static_assert(ErasedRenderAssetImpl<AFTER>, "AFTER must be an erased render asset type");
                render_app->get().add_systems(Render,
                                              std::move(prepare).after(ecs::into(prepare_erased_assets<AFTER>)));
            } else {
                render_app->get().add_systems(Render, std::move(prepare));
            }
        }
    }
};

}  // namespace epix::render::erased_render_asset
