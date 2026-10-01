#pragma once
#include <epix/pbr/scattering_medium.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/render/extract.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/view.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::pbr {

/** @brief How the atmosphere is rendered (Bevy `AtmosphereMode`,
 *  `bevy_pbr/src/atmosphere/mod.rs:431-449`).
 *
 *  `LookupTexture` is the default: a set of lookup textures approximates the
 *  scattering integration, which is fast but tapers in accuracy for very
 *  long-distance views. `Raymarched` integrates numerically instead, which is
 *  slower and gives sharp volumetric shadows. Bevy's values are 0 and 1 and the
 *  shader reads them as a `u32`. */
EPIX_EXPORT enum class AtmosphereMode : std::uint32_t {
    /** @brief The lookup-texture solution, Bevy's default. */
    LookupTexture = 0,
    /** @brief The raymarched solution. */
    Raymarched = 1,
};

/** @brief The settings an atmosphere is configured with (Bevy
 *  `AtmosphereSettings`, `bevy_pbr/src/atmosphere/mod.rs:292-365`).
 *
 *  Bevy marks this a component and requires it from `Atmosphere`, which this port
 *  registers from `Atmosphere`'s own hook; the fields and their defaults are the
 *  whole type. */
EPIX_EXPORT struct AtmosphereSettings {
    /** @brief The size of the transmittance LUT. */
    glm::uvec2 transmittance_lut_size{256, 128};
    /** @brief The size of the multiscattering LUT. */
    glm::uvec2 multiscattering_lut_size{32, 32};
    /** @brief The size of the sky-view LUT. */
    glm::uvec2 sky_view_lut_size{400, 200};
    /** @brief The size of the aerial-view LUT. */
    glm::uvec3 aerial_view_lut_size{32, 32, 32};
    /** @brief The number of points sampled along each ray when computing the
     *  transmittance LUT. */
    std::uint32_t transmittance_lut_samples = 40;
    /** @brief The number of rays sampled when computing each pixel of the
     *  multiscattering LUT. */
    std::uint32_t multiscattering_lut_dirs = 64;
    /** @brief The number of points sampled when integrating along each
     *  multiscattering ray. */
    std::uint32_t multiscattering_lut_samples = 20;
    /** @brief The number of points sampled along each ray when computing the
     *  sky-view LUT. */
    std::uint32_t sky_view_lut_samples = 16;
    /** @brief The number of points sampled for each slice along the z-axis of
     *  the aerial-view LUT. */
    std::uint32_t aerial_view_lut_samples = 10;
    /** @brief The maximum distance from the camera to evaluate the aerial-view
     *  LUT, in metres: the slices along the texture's z-axis are distributed
     *  linearly from the camera to this value. */
    float aerial_view_lut_max_distance = 3.2e4f;
    /** @brief A conversion factor between scene units and metres. */
    float scene_units_to_m = 1.0f;
    /** @brief The number of points sampled for each fragment when raymarching
     *  the sky. */
    std::uint32_t sky_max_samples = 16;
    /** @brief The method used to render the atmosphere. */
    AtmosphereMode rendering_method = AtmosphereMode::LookupTexture;
};

/** @brief The shader form of the settings above (Bevy `GpuAtmosphereSettings`,
 *  `bevy_pbr/src/atmosphere/mod.rs:367-409`).
 *
 *  The field order is the WGSL struct's, and the explicit `pad` reproduces the
 *  shader's layout: `vec3<u32>` is sixteen-byte aligned, so it starts at 32
 *  rather than at 24, and the struct is 80 bytes. glm's unaligned gentypes would
 *  otherwise place it at 24 and make the struct 72. */
EPIX_EXPORT struct GpuAtmosphereSettings {
    glm::uvec2 transmittance_lut_size{256, 128};
    glm::uvec2 multiscattering_lut_size{32, 32};
    glm::uvec2 sky_view_lut_size{400, 200};
    /** @brief Padding: `aerial_view_lut_size` is sixteen-byte aligned. */
    std::uint32_t pad[2]{};
    glm::uvec3 aerial_view_lut_size{32, 32, 32};
    std::uint32_t transmittance_lut_samples   = 40;
    std::uint32_t multiscattering_lut_dirs    = 64;
    std::uint32_t multiscattering_lut_samples = 20;
    std::uint32_t sky_view_lut_samples        = 16;
    std::uint32_t aerial_view_lut_samples     = 10;
    float aerial_view_lut_max_distance        = 3.2e4f;
    float scene_units_to_m                    = 1.0f;
    std::uint32_t sky_max_samples             = 16;
    std::uint32_t rendering_method            = 0;

    /** @brief The conversion Bevy spells `impl From<AtmosphereSettings>`, with
     *  the rendering method widened to a `u32`. */
    static GpuAtmosphereSettings from(const AtmosphereSettings& settings) {
        return GpuAtmosphereSettings{
            .transmittance_lut_size       = settings.transmittance_lut_size,
            .multiscattering_lut_size     = settings.multiscattering_lut_size,
            .sky_view_lut_size            = settings.sky_view_lut_size,
            .aerial_view_lut_size         = settings.aerial_view_lut_size,
            .transmittance_lut_samples    = settings.transmittance_lut_samples,
            .multiscattering_lut_dirs     = settings.multiscattering_lut_dirs,
            .multiscattering_lut_samples  = settings.multiscattering_lut_samples,
            .sky_view_lut_samples         = settings.sky_view_lut_samples,
            .aerial_view_lut_samples      = settings.aerial_view_lut_samples,
            .aerial_view_lut_max_distance = settings.aerial_view_lut_max_distance,
            .scene_units_to_m             = settings.scene_units_to_m,
            .sky_max_samples              = settings.sky_max_samples,
            .rendering_method             = static_cast<std::uint32_t>(settings.rendering_method),
        };
    }

    /** @brief Bevy's `Default` is `AtmosphereSettings::default().into()`. */
    static GpuAtmosphereSettings defaulted() { return from(AtmosphereSettings{}); }
};

/** @brief The shader form of an atmosphere (Bevy `GpuAtmosphere`,
 *  `bevy_pbr/src/atmosphere/resources.rs:475-482`).
 *
 *  `vec3<f32>` is sixteen-byte aligned, so the struct is 32 bytes rather than
 *  glm's 20. Bevy derives no `Default`: the values come from the `Atmosphere`
 *  component, which Epix does not port yet because it carries a
 *  `ScatteringMedium` asset handle. */
EPIX_EXPORT struct GpuAtmosphere {
    /** @brief An approximation of the planet surface's average albedo, used
     *  when calculating multiscattering. */
    glm::vec3 ground_albedo{0.0f};
    /** @brief The radius of the planet, in metres. */
    float bottom_radius = 0.0f;
    /** @brief The radius at which the atmosphere is considered to end, in
     *  metres, measured from the planet's centre. */
    float top_radius = 0.0f;
    /** @brief Padding to the shader's sixteen-byte aligned size. */
    std::uint32_t pad[3]{};
};

/** @brief What the atmosphere storage buffer holds (Bevy `AtmosphereData`,
 *  `bevy_pbr/src/atmosphere/resources.rs:769-774`).
 *
 *  It is the single buffer the mesh view layout binds at binding 31 as a
 *  read-only storage buffer, so its 112 bytes are that binding's minimum. */
EPIX_EXPORT struct AtmosphereData {
    GpuAtmosphere atmosphere;
    GpuAtmosphereSettings settings;
};

// The WGSL layout these three reproduce: three `vec2<u32>` then a 16-byte-aligned
// `vec3<u32>` and nine scalars (80), a 16-byte-aligned `vec3<f32>` and two
// scalars (32), and the two together (112).
static_assert(sizeof(GpuAtmosphere) == 32, "GpuAtmosphere must match the shader's 32 bytes");
static_assert(sizeof(GpuAtmosphereSettings) == 80, "GpuAtmosphereSettings must match the shader's 80 bytes");
static_assert(sizeof(AtmosphereData) == 112, "AtmosphereData must match the shader's 112 bytes");

/** @brief An atmosphere on a camera (Bevy `Atmosphere`, `bevy_pbr/src/atmosphere/mod.rs:210-231`).
 *
 *  Bevy derives no `Default` for it, because a medium handle cannot be defaulted;
 *  `earthlike` is the constructor that supplies one. */
EPIX_EXPORT struct Atmosphere {
    float bottom_radius = 0.0f;
    float top_radius = 0.0f;
    glm::vec3 ground_albedo{0.0f};
    assets::Handle<ScatteringMedium> medium;

    /** @brief Earth numbers, with the caller medium (Bevy `Atmosphere::earthlike`,
     *  `:233-245`). */
    static Atmosphere earthlike(assets::Handle<ScatteringMedium> medium) {
        return Atmosphere{.bottom_radius = 6'360'000.0f,
                          .top_radius    = 6'460'000.0f,
                          .ground_albedo = glm::vec3{0.3f},
                          .medium        = std::move(medium)};
    }

    /** @brief Bevy's `#[require(AtmosphereSettings, Hdr)]`
     *  (`bevy_pbr/src/atmosphere/mod.rs:209`). The settings carry the LUT sizes and
     *  sample counts the atmosphere is built from, and `Hdr` is what makes the
     *  camera render into a float target instead of clipping the sky. */
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<AtmosphereSettings>([] { return AtmosphereSettings{}; });
        registrator.template register_required<render::view::Hdr>([] { return render::view::Hdr{}; });
    }
};

/** @brief The render-world representation of an `Atmosphere`, before it becomes
 *  shader uniforms (Bevy `ExtractedAtmosphere`, `:264-272`). */
EPIX_EXPORT struct ExtractedAtmosphere {
    float bottom_radius = 0.0f;
    float top_radius = 0.0f;
    glm::vec3 ground_albedo{0.0f};
    assets::AssetId<ScatteringMedium> medium;

    bool operator==(const ExtractedAtmosphere&) const = default;
};

}  // namespace epix::pbr

namespace epix::render {

/** @brief Extracts the atmosphere onto the view, keeping the medium by id (Bevy
 *  `impl ExtractComponent for Atmosphere`, `bevy_pbr/src/atmosphere/mod.rs:247-262`):
 *  the query is filtered to 3D cameras, and the handle becomes an `AssetId` because
 *  the render world holds no handles. */
template <>
struct ExtractComponent<::epix::pbr::Atmosphere> {
    using QueryData   = const ::epix::pbr::Atmosphere&;
    using QueryFilter = epix::ecs::Filter<epix::ecs::With<::epix::camera::Camera3d>>;
    using Out         = ::epix::pbr::ExtractedAtmosphere;

    static std::optional<Out> extract_component(const ::epix::pbr::Atmosphere& item) {
        return Out{.bottom_radius = item.bottom_radius,
                   .top_radius    = item.top_radius,
                   .ground_albedo = item.ground_albedo,
                   .medium        = item.medium.id()};
    }
};

}  // namespace epix::render

namespace epix::render::render_resource {
/** @brief The three atmosphere shader types carry their layout explicitly, so
 *  the raw encoder applies (Bevy derives `ShaderType` on the same structs). */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuAtmosphere> : RawShaderType<::epix::pbr::GpuAtmosphere> {};
template <>
struct ShaderTypeInfo<::epix::pbr::GpuAtmosphereSettings> : RawShaderType<::epix::pbr::GpuAtmosphereSettings> {};
template <>
struct ShaderTypeInfo<::epix::pbr::AtmosphereData> : RawShaderType<::epix::pbr::AtmosphereData> {};
}  // namespace epix::render::render_resource
