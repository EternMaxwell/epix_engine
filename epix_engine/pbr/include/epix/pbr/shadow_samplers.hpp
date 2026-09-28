#pragma once

#include <epix/common.hpp>
#include <epix/render.hpp>

namespace epix::pbr {

/** @brief The samplers a shadow map is read with (Bevy `ShadowSamplers`,
 *  `bevy_pbr/src/render/light.rs:195-203`).
 *
 *  Comparison samplers, because shadow lookups compare a depth against the map rather
 *  than filtering a colour. Bevy also carries two PCSS-only linear samplers behind the
 *  `experimental_pbr_pcss` feature; this port has no such feature, so they are omitted
 *  rather than built unconditionally - which is the one part of the type Bevy itself
 *  considers optional. */
EPIX_EXPORT struct ShadowSamplers {
    wgpu::Sampler point_light_comparison_sampler;
    wgpu::Sampler directional_light_comparison_sampler;
};

/** @brief The descriptor every shadow sampler starts from (Bevy `base_sampler_descriptor`,
 *  `light.rs:206-214`). Declared rather than hidden so a test can check the choices -
 *  clamp on all three axes, linear magnification and minification, nearest mipmapping -
 *  without a device, which is where the fidelity of this type actually lives. */
EPIX_EXPORT EPIX_API wgpu::SamplerDescriptor shadow_sampler_base_descriptor();

/** @brief That descriptor with `label` and the shadow comparison (Bevy's two
 *  `compare: Some(CompareFunction::GreaterEqual)` samplers). */
EPIX_EXPORT EPIX_API wgpu::SamplerDescriptor shadow_sampler_comparison_descriptor(const char* label);
/** @brief Build the shadow samplers on a device (Bevy `init_shadow_samplers`,
 *  `light.rs:205-230`): clamp to edge on all three axes, linear magnification and
 *  minification, nearest mipmapping, and `GreaterEqual` comparison - reverse-Z, the
 *  same comparison the depth state uses. */
EPIX_EXPORT EPIX_API ShadowSamplers make_shadow_samplers(const wgpu::Device& device);

/** @brief The system that inserts them (Bevy `init_shadow_samplers`). */
EPIX_EXPORT EPIX_API void init_shadow_samplers(epix::ecs::Res<wgpu::Device> device,
                                               epix::ecs::Commands commands);

}  // namespace epix::pbr