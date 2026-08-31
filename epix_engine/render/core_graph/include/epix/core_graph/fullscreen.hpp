#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/render.hpp>
#include <utility>
#endif

namespace epix::core_graph {
/** @brief Shared fullscreen-triangle vertex shader used by core-pipeline
 * full-screen passes (Bevy `FullscreenShader`). */
EPIX_EXPORT struct FullscreenShader {
    explicit FullscreenShader(assets::Handle<shader::Shader> shader) : _shader(std::move(shader)) {}

    /** @brief Returns the underlying shader asset handle. */
    assets::Handle<shader::Shader> shader() const { return _shader; }

    /** @brief Returns a vertex state that draws the generated fullscreen
     * triangle with `draw(3, 1, 0, 0)`. */
    render::VertexState to_vertex_state() const {
        return {.shader = _shader, .entry_point = "fullscreen_vertex_shader"};
    }

   private:
    assets::Handle<shader::Shader> _shader;
};

}  // namespace epix::core_graph
