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

    void attach(epix::app::App& app);
};

}  // namespace epix::pbr
