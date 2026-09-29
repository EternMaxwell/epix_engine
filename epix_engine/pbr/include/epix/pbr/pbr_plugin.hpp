#pragma once

#include <epix/app.hpp>
#include <epix/common.hpp>

namespace epix::pbr {

/** @brief The 3D renderer's plugin, as far as this port implements one (Bevy
 *  `PbrPlugin`, `bevy_pbr/src/lib.rs:185-260`).
 *
 *  **Partial by design, and the reason is recorded here.** Bevy's `PbrPlugin` loads
 *  twenty shader libraries, registers `StandardMaterial`, initialises
 *  `DefaultOpaqueRendererMethod`, and aggregates ten plugins. Three of those plugins
 *  exist in this port - `MeshRenderPlugin`, `LightmapPlugin` and `LightPlugin` - and are
 *  added here; the other seven (`MaterialsPlugin`, `MaterialPlugin<StandardMaterial>`,
 *  `ScreenSpaceAmbientOcclusionPlugin`, `FogPlugin`,
 *  `ExtractResourcePlugin<DefaultOpaqueRendererMethod>`, `LightProbePlugin` and
 *  `GpuMeshPreprocessPlugin`) do not, and each is an open row rather than a silent gap.
 *
 *  This exists because `LightPlugin` needs an owner: without one, nine finished systems
 *  had no caller. It grows as those plugins land, and the shader libraries stay where
 *  this port already loads them - beside the pipelines that import them - rather than
 *  moving here for the sake of matching Bevy's file layout. */
EPIX_EXPORT struct EPIX_API PbrPlugin {
    /** @brief Passed through to `MeshRenderPlugin`, as Bevy passes its own. */
    bool use_gpu_instance_buffer_builder = false;

    /** @brief Whether the device offers the three storage-buffer bindings clustered
     *  forward needs. Bevy asks the device; this port has no `RenderDevice` to ask, so
     *  it is a parameter here, the substitution the layout and decal rows record. */
    bool supports_storage_buffers = false;

    /** @brief Whether clustered decals are usable on this device. Bevy asks the device
     *  and the `pbr_clustered_decals` feature; both are the caller's answer here. */
    bool clustered_decals_are_usable = false;

    void attach(epix::app::App& app);

    /** @brief Builds `GlobalClusterSettings` here rather than in `attach`, because it
     *  reads the render world's device and only the finish half runs after every plugin,
     *  including `RenderPlugin`, has built (Bevy `PbrPlugin::finish`,
     *  `bevy_pbr/src/lib.rs:364-371`). The settings go into the **main** world, since
     *  `assign_objects_to_clusters` is a main-world system. */
    void ready(epix::app::App& app);
};

}  // namespace epix::pbr
