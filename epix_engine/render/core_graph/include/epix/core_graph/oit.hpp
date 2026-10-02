#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/render.hpp>
#endif

namespace epix::core_graph {

/** @brief The number of storage buffers OIT needs from the device (Bevy
 *  `OIT_REQUIRED_STORAGE_BUFFERS`, `oit/resolve/mod.rs:31`): the layer buffer
 *  and the layer-id buffer. */
EPIX_EXPORT inline constexpr std::uint32_t OIT_REQUIRED_STORAGE_BUFFERS = 2;

/** @brief The component that marks a camera as using order-independent
 *  transparency and configures it (Bevy
 *  `OrderIndependentTransparencySettings`, `oit/mod.rs:38-49`).
 *
 *  It is also the shader's uniform, so its two fields are the whole type and the
 *  std140 layout is eight bytes. Bevy's `Default` is eight layers and a zero
 *  threshold, which the member initialisers reproduce. */
EPIX_EXPORT struct OrderIndependentTransparencySettings {
    /** @brief How many layers the blending uses. More layers cost more memory
     *  but give better results; 8 is generally recommended, and going above 32
     *  is probably not worth it in the vast majority of cases (Bevy warns on
     *  that here, which is recorded as deferred below). */
    std::int32_t layer_count = 8;
    /** @brief The threshold above which a fragment is added to the blending
     *  layers. Higher values allow fewer layers and better performance at the
     *  cost of quality. */
    float alpha_threshold = 0.0f;
};

/** @brief Whether the platform can run OIT at all (Bevy `is_oit_supported`,
 *  `oit/resolve/mod.rs:65-91`).
 *
 *  Both facts Bevy reads off the device are parameters here, the substitution
 *  the rest of the port records: `fragment_writable_storage` is the adapter's
 *  `DownlevelFlags::FRAGMENT_WRITABLE_STORAGE`, and the limit is the device's
 *  `max_storage_buffers_per_shader_stage`. `warn` issues Bevy's two warnings —
 *  the OIT plugin is not loaded, so the reason is worth naming — and never
 *  changes the answer. */
EPIX_EXPORT EPIX_API bool is_oit_supported(bool fragment_writable_storage,
                                           std::uint32_t max_storage_buffers_per_shader_stage,
                                           bool warn);

}  // namespace epix::core_graph

namespace epix::render::render_resource {
/** @brief The OIT settings are also the shader's uniform (Bevy derives
 *  `ShaderType` on the same struct), and the two fields are the whole layout. */
template <>
struct ShaderTypeInfo<::epix::core_graph::OrderIndependentTransparencySettings>
    : RawShaderType<::epix::core_graph::OrderIndependentTransparencySettings> {};
}  // namespace epix::render::render_resource
