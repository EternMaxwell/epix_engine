#pragma once

#include <epix/app.h>
#include <epix/assets.h>
#include <epix/vulkan.h>

#include "common.h"

namespace epix::render::assets {
using namespace epix::assets;

template <typename T>
struct RenderAsset;

enum RenderAssetUsageBits : uint8_t {
    MAIN_WORLD   = 1 << 0,  // used in main world(e.g. for cpu access)
    RENDER_WORLD = 1 << 1,  // used for rendering(e.g. for gpu access)
};
using RenderAssetUsage = std::underlying_type_t<RenderAssetUsageBits>;

template <typename T>
concept RenderAssetImpl = requires(RenderAsset<T> asset) {
    std::constructible_from<RenderAsset<T>>;
    std::is_empty_v<RenderAsset<T>>;
    typename RenderAsset<T>::ProcessedAsset;
    typename RenderAsset<T>::Param;
    epix::app::ValidParam<typename RenderAsset<T>::Param>;
    {
        asset.process(std::declval<T&&>(),
                      std::declval<typename RenderAsset<T>::Param&>())
    } -> std::same_as<typename RenderAsset<T>::ProcessedAsset>;
    { asset.usage(std::declval<const T&>()) } -> std::same_as<RenderAssetUsage>;
};

template <RenderAssetImpl T>
struct RenderAssets {
    using Type = typename RenderAsset<T>::ProcessedAsset;

   public:
    void insert(const epix::assets::AssetId<T>& id, Type&& asset) {
        assets.emplace(id, std::move(asset));
    }
    template <typename... Args>
    void emplace(const epix::assets::AssetId<T>& id, Args&&... args) {
        assets.emplace(id, std::forward<Args>(args)...);
    }
    bool contains(const epix::assets::AssetId<T>& id) const {
        return assets.contains(id);
    }
    bool remove(const epix::assets::AssetId<T>& id) {
        return assets.erase(id) > 0;
    }
    Type& get(const epix::assets::AssetId<T>& id) {
        if (auto ptr = try_get(id)) {
            return *ptr;
        }
        throw std::runtime_error("Render asset not found: " + id.to_string());
    }
    const Type& get(const epix::assets::AssetId<T>& id) const {
        if (auto ptr = try_get(id)) {
            return *ptr;
        }
        throw std::runtime_error("Render asset not found: " + id.to_string());
    }
    Type* try_get(const epix::assets::AssetId<T>& id) {
        if (auto it = assets.find(id); it != assets.end()) {
            return &it->second;
        }
        return nullptr;
    }
    const Type* try_get(const epix::assets::AssetId<T>& id) const {
        if (auto it = assets.find(id); it != assets.end()) {
            return &it->second;
        }
        return nullptr;
    }
    auto iter() { return std::views::all(assets); }
    auto iter() const { return std::views::all(assets); }

   private:
    entt::dense_map<epix::assets::AssetId<T>, Type> assets;
};

template <RenderAssetImpl T>
struct CachedExtractedAssets {
    std::vector<std::pair<epix::assets::AssetId<T>, T>> extracted_assets;
    entt::dense_set<epix::assets::AssetId<T>> removed;
};

template <RenderAssetImpl T>
void extract_assets(Commands& commands,
                    Extract<ResMut<epix::assets::Assets<T>>>& assets,
                    Extract<EventReader<epix::assets::AssetEvent<T>>>& events) {
    std::vector<epix::assets::AssetId<T>> changed_ids;
    entt::dense_set<epix::assets::AssetId<T>> removed;
    for (const auto& event : events.read()) {
        if (event.is_added() || event.is_modified()) {
            changed_ids.push_back(event.id);
        } else if (event.is_unused()) {
            removed.insert(event.id);
        }
    }
    std::vector<std::pair<epix::assets::AssetId<T>, T>> extracted_assets;
    RenderAsset<T> render_asset_impl;
    std::vector<std::string> errors;
    for (const auto& id : changed_ids) {
        if (auto asset = assets->get(id);
            asset && render_asset_impl.usage(*asset) & RENDER_WORLD) {
            if (render_asset_impl.usage(*asset) & MAIN_WORLD) {
                // this asset is still used in main world, copy it
                if constexpr (std::is_copy_constructible_v<T>) {
                    extracted_assets.emplace_back(id, *asset);
                } else {
                    // T is not copyable, so this asset is not allowed to
                    // be used both in main world and render world
                    errors.emplace_back(std::format(
                        "Asset [{}] is not copyable, as a render asset, it "
                        "should not have a usage of MAIN_WORLD & RENDER_WORLD",
                        id.to_string()));
                }
            } else {
                // this asset is only used in render world, move it
                extracted_assets.emplace_back(id, std::move(*asset));
                assets->remove(id);
            }
        }
    }

    commands.insert_resource(CachedExtractedAssets<T>{
        std::move(extracted_assets), std::move(removed)});

    if (!errors.empty()) {
        std::stringstream ss;
        for (const auto& error : errors) {
            ss << "\t" << error << "\n";
        }
        throw std::runtime_error(
            "Errors occurred while extracting render assets:\n" + ss.str());
    }
}

template <RenderAssetImpl T>
void process_render_assets(Commands& commands,
                           typename RenderAsset<T>::Param& param,
                           ResMut<RenderAssets<T>>& render_assets,
                           ResMut<CachedExtractedAssets<T>>& extracted_assets) {
    RenderAsset<T> render_asset_impl;
    std::vector<std::pair<epix::assets::AssetId<T>, std::exception_ptr>>
        exceptions;
    for (auto&& [id, asset] : extracted_assets->extracted_assets) {
        try {
            render_assets->insert(
                id, render_asset_impl.process(std::move(asset), param));
        } catch (...) {
            exceptions.emplace_back(id, std::current_exception());
        }
    }
    for (const auto& id : extracted_assets->removed) {
        render_assets->remove(id);
    }
    commands.remove_resource<CachedExtractedAssets<T>>();
    if (!exceptions.empty()) {
        // Handle exceptions
        std::stringstream ss;
        for (const auto& [id, ex_ptr] : exceptions) {
            try {
                std::rethrow_exception(ex_ptr);
            } catch (const std::exception& e) {
                ss << "\tError processing asset " << id.to_string() << ": "
                   << e.what() << "\n";
            } catch (...) {
                ss << "\tUnknown error processing asset " << id.to_string()
                   << "\n";
            }
        }
        throw std::runtime_error(
            "Errors occurred while processing render assets:\n" + ss.str());
    }
}

enum class ExtractAssetSet {
    Extract,
    Process,
};

template <RenderAssetImpl T>
struct ExtractAssetPlugin {
    void build(App& app) {
        if (auto render_app = app.get_sub_app(Render)) {
            render_app->init_resource<RenderAssets<T>>();
            render_app->configure_sets(
                sets(ExtractAssetSet::Extract, ExtractAssetSet::Process)
                    .chain());
            render_app->add_systems(
                ExtractSchedule,
                into(extract_assets<T>)
                    .in_set(ExtractAssetSet::Extract)
                    .set_name(std::format("extract render asset<{}>",
                                          typeid(T).name())));
            render_app->add_systems(
                ExtractSchedule,
                into(process_render_assets<T>)
                    .in_set(ExtractAssetSet::Process)
                    .set_name(std::format("process render asset<{}>",
                                          typeid(T).name())));
        }
    }
};
};  // namespace epix::render::assets