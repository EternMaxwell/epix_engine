#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
#endif

namespace epix::pbr {
EPIX_EXPORT inline constexpr std::string_view PREPASS_SHADER_PATH = "pbr/prepass.slang";
EPIX_EXPORT inline constexpr std::string_view PREPASS_IO_SHADER_PATH = "pbr/prepass_io.slang";
EPIX_EXPORT inline constexpr std::string_view PREPASS_UTILS_SHADER_PATH = "pbr/prepass_utils.slang";
EPIX_EXPORT inline constexpr std::string_view RGB9E5_SHADER_PATH = "pbr/rgb9e5.slang";

/** @brief Per-app ownership of the prepass libraries and default shader. */
EPIX_EXPORT struct PrepassShaderHandles {
    assets::Handle<shader::Shader> io;
    assets::Handle<shader::Shader> utils;
    assets::Handle<shader::Shader> rgb9e5;
    assets::Handle<shader::Shader> prepass;
};

EPIX_EXPORT EPIX_API void register_prepass_shaders(ecs::World& world);
}  // namespace epix::pbr
