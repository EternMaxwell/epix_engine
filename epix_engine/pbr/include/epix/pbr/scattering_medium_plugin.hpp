#pragma once

/** @brief The plugin side of the scattering medium, kept out of
 *  `scattering_medium.hpp` on purpose.
 *
 *  That header is pulled in wherever the medium data is needed - `atmosphere.hpp`
 *  among them, which many translation units include - and this one needs
 *  `<epix/render.hpp>`, which reaches render-graph internals. Including it from the
 *  data header made `epix::render::graph::RenderContext::finish` multiply defined
 *  across translation units (batch 180), so the plugin lives behind its own include
 *  instead. What that says about `<epix/render.hpp>` is recorded on the tracker. */

#include <epix/pbr/scattering_medium.hpp>
#include <epix/render.hpp>

namespace epix::pbr {
/** @brief The render-world side of a medium (Bevy `impl RenderAsset for
 *  GpuScatteringMedium`, `bevy_pbr/src/medium.rs:418-540`): the device and queue are
 *  its parameters, and the source asset travels unchanged through extraction. */
EPIX_EXPORT EPIX_API void init_scattering_medium_sampler(epix::ecs::Res<wgpu::Device> device,
                                                         epix::ecs::Commands commands);

/** @brief Registers the medium asset, its GPU counterpart and the sampler (Bevy
 *  `ScatteringMediumPlugin`, `bevy_pbr/src/medium.rs:25-40`).
 *
 *  Bevy adds this as one plugin; the port splits its hooks the same way every other
 *  plugin here does - `attach` for the asset and render-asset registration plus the
 *  `RenderStartup` system. */
EPIX_EXPORT struct EPIX_API ScatteringMediumPlugin {
    void attach(epix::app::App& app);
};

/** @brief The port the medium asset is registered under, so the plugin can be found
 *  beside the type it registers. */
EPIX_EXPORT struct EPIX_API ScatteringMediumSamplerPluginParts {
    ScatteringMediumPlugin plugin;
};

}  // namespace epix::pbr

namespace epix::render {

/** @brief `GpuScatteringMedium` from a `ScatteringMedium` (Bevy
 *  `GpuScatteringMedium::prepare_asset`). */
template <>
struct RenderAsset<::epix::pbr::GpuScatteringMedium> {
    using SourceAsset    = ::epix::pbr::ScatteringMedium;
    using ExtractedAsset = ::epix::pbr::ScatteringMedium;
    /** @brief Bevy's `Param` is `(Res<RenderDevice>, Res<RenderQueue>)`. */
    using Param = std::tuple<ecs::Res<wgpu::Device>, ecs::Res<wgpu::Queue>>;

    static std::expected<::epix::pbr::GpuScatteringMedium,
                         PrepareAssetError<::epix::pbr::ScatteringMedium>>
    prepare_asset(::epix::pbr::ScatteringMedium&& source_asset,
                  assets::AssetId<::epix::pbr::ScatteringMedium>, Param param,
                  const ::epix::pbr::GpuScatteringMedium*) {
        const auto& [device, queue] = param;
        return ::epix::pbr::make_gpu_scattering_medium(device.get(), queue.get(), source_asset);
    }

    /** @brief Bevy leaves `asset_usage` at its default, which includes both worlds. */
    static assets::RenderAssetUsages asset_usage(const ::epix::pbr::ScatteringMedium&) noexcept {
        return static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                      assets::RenderAssetUsages::RENDER_WORLD);
    }
};

}  // namespace epix::render

namespace epix::pbr {

void ScatteringMediumPlugin::attach(epix::app::App& app) {
    assets::app_register_asset<ScatteringMedium>(app);
    // One template argument: the port pairs it with the source asset through the
    // `RenderAsset` specialisation above, and a second argument would be an ordering
    // plugin rather than a type.
    app.add_plugins(epix::render::RenderAssetPlugin<GpuScatteringMedium>{});
    if (auto render_app = app.get_sub_app_mut(epix::render::Render)) {
        render_app->get().add_systems(epix::app::ScheduleInfo(epix::render::RenderStartup),
                                      epix::ecs::into(init_scattering_medium_sampler)
                                          .set_name("init scattering medium sampler"));
    }
}

void init_scattering_medium_sampler(epix::ecs::Res<wgpu::Device> device, epix::ecs::Commands commands) {
    commands.insert_resource(make_scattering_medium_sampler(device.get()));
}

}  // namespace epix::pbr