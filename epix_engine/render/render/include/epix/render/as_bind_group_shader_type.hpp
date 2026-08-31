#pragma once

#include <epix/common.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/render_resource.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#endif

namespace epix::render::render_resource {

/**
 * @brief Converts a CPU bind-group value to its shader-compatible value.
 *
 * This is the C++ counterpart to Bevy 0.18's `AsBindGroupShaderType<T>`.
 * Specialize it when conversion needs render-world image metadata; the
 * `RenderAssets<image::Image>` argument contains the processed `GpuImage`
 * values. The default implementation covers normal implicit conversions and
 * deliberately ignores that resource.
 *
 * @tparam C Source bind-group value type.
 * @tparam T Shader-compatible destination type.
 */
EPIX_EXPORT template <typename C, ShaderType T>
struct AsBindGroupShaderType {
    /** @brief Convert @p value to its shader representation. */
    static T as_bind_group_shader_type(const C& value, const RenderAssets<image::Image>& images)
        requires std::convertible_to<const C&, T>
    {
        (void)images;
        return value;
    }
};

/** @brief Invoke `AsBindGroupShaderType` without spelling its specialization.
 * This is the C++ call-site counterpart to Bevy's trait method. */
EPIX_EXPORT template <ShaderType T, typename C>
T as_bind_group_shader_type(const C& value, const RenderAssets<image::Image>& images) {
    return AsBindGroupShaderType<C, T>::as_bind_group_shader_type(value, images);
}

}  // namespace epix::render::render_resource
