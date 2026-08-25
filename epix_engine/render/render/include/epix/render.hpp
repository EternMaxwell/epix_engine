#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/render/alpha.hpp>
#include <epix/render/as_bind_group.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/binned_phase.hpp>
#include <epix/render/color_grading.hpp>
#include <epix/render/erased_render_asset.hpp>
#include <epix/render/extract.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/globals.hpp>
#include <epix/render/gpu_readback.hpp>
#include <epix/render/graph.hpp>
#include <epix/render/image.hpp>
#include <epix/render/label.hpp>
#include <epix/render/manual_texture_view.hpp>
#include <epix/render/occlusion_culling.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_debug.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/schedule.hpp>
#include <epix/render/specialized_pipeline.hpp>
#include <epix/render/specializer.hpp>
#include <epix/render/storage.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/texture_attachment.hpp>
#include <epix/render/view.hpp>
#include <epix/render/visibility_range.hpp>
#include <epix/render/window.hpp>

namespace epix::render {
/**
 * @brief Resource for anonymous surface that is used for requesting adapter/device.
 * Since webgpu requires a surface to request an adapter, we provide this resource to let window implementations to
 * give a functor that creates a surface from the instance. It is recommanded that the functor will destruct the
 * temporary window after we are done with requesting the adapter/device and releasing this resource.
 */
EPIX_EXPORT struct AnonymousSurface {
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
};
/**
 * @brief Priority used when automatically configuring wgpu features/limits
 * (Bevy WgpuSettingsPriority).
 */
EPIX_EXPORT enum class WgpuSettingsPriority {
    Compatibility,
    Functionality,
    WebGL2,
};

/** @brief Reads Bevy's `WGPU_SETTINGS_PRIO` environment setting. Only the
 * case-insensitive `compatibility`, `functionality`, and `webgl2` spellings
 * are recognized; any other value has no effect. */
EPIX_EXPORT std::optional<WgpuSettingsPriority> settings_priority_from_env() noexcept;

/** @brief Backend bitmask used by automatic renderer creation (wgpu
 * `Backends`). It intentionally preserves every backend selected by callers;
 * the current Slang/Vulkan workaround is applied privately when the renderer
 * creates its native instance and adapter. */
EPIX_EXPORT struct Backends {
    enum Bit : std::uint32_t {
        Noop           = 1u << 0,
        Vulkan         = 1u << 1,
        Metal          = 1u << 2,
        Dx12           = 1u << 3,
        Gl             = 1u << 4,
        BrowserWebGpu  = 1u << 5,
    };

    constexpr Backends() = default;
    constexpr Backends(Bit bit) : bits_(static_cast<std::uint32_t>(bit)) {}
    constexpr explicit Backends(std::uint32_t bits) : bits_(bits) {}

    [[nodiscard]] static constexpr Backends empty() noexcept { return {}; }
    [[nodiscard]] static constexpr Backends all() noexcept {
        return Backends{static_cast<std::uint32_t>(Noop) | static_cast<std::uint32_t>(Vulkan) |
                        static_cast<std::uint32_t>(Metal) | static_cast<std::uint32_t>(Dx12) |
                        static_cast<std::uint32_t>(Gl) | static_cast<std::uint32_t>(BrowserWebGpu)};
    }
    [[nodiscard]] static constexpr Backends primary() noexcept {
        return Backends{static_cast<std::uint32_t>(Vulkan) | static_cast<std::uint32_t>(Metal) |
                        static_cast<std::uint32_t>(Dx12) | static_cast<std::uint32_t>(BrowserWebGpu)};
    }
    [[nodiscard]] static constexpr Backends secondary() noexcept { return Backends{Gl}; }
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr bool contains(Backends other) const noexcept {
        return (bits_ & other.bits_) == other.bits_;
    }
    [[nodiscard]] static Backends from_comma_list(std::string_view) noexcept;
    [[nodiscard]] static std::optional<Backends> from_env() noexcept;

    friend constexpr bool operator==(Backends, Backends) = default;
    friend constexpr Backends operator|(Backends lhs, Backends rhs) noexcept {
        return Backends{lhs.bits_ | rhs.bits_};
    }
    friend constexpr Backends operator|(Bit lhs, Bit rhs) noexcept { return Backends{lhs} | Backends{rhs}; }
    friend constexpr Backends operator|(Backends lhs, Bit rhs) noexcept { return lhs | Backends{rhs}; }
    friend constexpr Backends operator|(Bit lhs, Backends rhs) noexcept { return Backends{lhs} | rhs; }

   private:
    std::uint32_t bits_ = 0;
};

/** @brief wgpu instance debugging flags (Bevy `InstanceFlags`). The native
 * wgpu version currently used by Epix accepts Debug, Validation, and
 * DiscardHalLabels; the remaining Bevy bits are retained in this public
 * value for forward-compatible configuration. */
EPIX_EXPORT struct InstanceFlags {
    enum Bit : std::uint32_t {
        Debug                             = 1u << 0,
        Validation                        = 1u << 1,
        DiscardHalLabels                  = 1u << 2,
        AllowUnderlyingNoncompliantAdapter = 1u << 3,
        GpuBasedValidation                = 1u << 4,
        ValidationIndirectCall            = 1u << 5,
        AutomaticTimestampNormalization   = 1u << 6,
    };

    constexpr InstanceFlags() = default;
    constexpr InstanceFlags(Bit bit) : bits_(static_cast<std::uint32_t>(bit)) {}
    constexpr explicit InstanceFlags(std::uint32_t bits) : bits_(bits) {}

    [[nodiscard]] static constexpr InstanceFlags empty() noexcept { return {}; }
    [[nodiscard]] static constexpr InstanceFlags debugging() noexcept {
        return InstanceFlags{Debug} | Validation | ValidationIndirectCall;
    }
    [[nodiscard]] static constexpr InstanceFlags advanced_debugging() noexcept {
        return debugging() | GpuBasedValidation;
    }
    [[nodiscard]] static constexpr InstanceFlags from_build_config() noexcept {
#if defined(_DEBUG)
        return debugging();
#else
        return InstanceFlags{ValidationIndirectCall};
#endif
    }
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr bool contains(InstanceFlags other) const noexcept {
        return (bits_ & other.bits_) == other.bits_;
    }
    [[nodiscard]] constexpr std::uint32_t native_supported_bits() const noexcept {
        auto native = bits_ & (static_cast<std::uint32_t>(Debug) | static_cast<std::uint32_t>(Validation) |
                               static_cast<std::uint32_t>(DiscardHalLabels));
        // Native wgpu v25 has no GPU-assisted validation flag. Its closest
        // available behavior is ordinary backend validation.
        if ((bits_ & static_cast<std::uint32_t>(GpuBasedValidation)) != 0)
            native |= static_cast<std::uint32_t>(Validation);
        return native;
    }
    [[nodiscard]] InstanceFlags with_env() const noexcept {
        auto result = *this;
        const auto set_from_env = [&result](Bit bit, const char* name) {
            const char* value = std::getenv(name);
            if (!value) return;
            if (std::string_view(value) == "0")
                result.bits_ &= ~static_cast<std::uint32_t>(bit);
            else
                result.bits_ |= static_cast<std::uint32_t>(bit);
        };
        set_from_env(Validation, "WGPU_VALIDATION");
        set_from_env(Debug, "WGPU_DEBUG");
        set_from_env(DiscardHalLabels, "WGPU_DISCARD_HAL_LABELS");
        set_from_env(AllowUnderlyingNoncompliantAdapter, "WGPU_ALLOW_UNDERLYING_NONCOMPLIANT_ADAPTER");
        set_from_env(GpuBasedValidation, "WGPU_GPU_BASED_VALIDATION");
        set_from_env(ValidationIndirectCall, "WGPU_VALIDATION_INDIRECT_CALL");
        return result;
    }

    friend constexpr bool operator==(InstanceFlags, InstanceFlags) = default;
    friend constexpr InstanceFlags operator|(InstanceFlags lhs, InstanceFlags rhs) noexcept {
        return InstanceFlags{lhs.bits_ | rhs.bits_};
    }
    friend constexpr InstanceFlags operator|(Bit lhs, Bit rhs) noexcept { return InstanceFlags{lhs} | InstanceFlags{rhs}; }
    friend constexpr InstanceFlags operator|(InstanceFlags lhs, Bit rhs) noexcept { return lhs | InstanceFlags{rhs}; }
    friend constexpr InstanceFlags operator|(Bit lhs, InstanceFlags rhs) noexcept { return InstanceFlags{lhs} | rhs; }

   private:
    std::uint32_t bits_ = 0;
};

/**
 * @brief Renderer configuration for adapter/device creation (Bevy
 * WgpuSettings). The env vars
 * WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO are honored by
 * apply_env_overrides like Bevy's settings_priority_from_env.
 */
EPIX_EXPORT struct WgpuSettings {
    /** @brief Optional debug label for the render device. */
    std::optional<std::string> device_label;
    /** @brief Backends enabled for instance creation (Bevy default
     * `Some(Backends::all())`). */
    std::optional<Backends> backends = Backends::all();
    /** @brief Power preference (Bevy default HighPerformance). */
    wgpu::PowerPreference power_preference = wgpu::PowerPreference::eHighPerformance;
    /** @brief Feature/limit priority (Bevy default Functionality). */
    WgpuSettingsPriority priority = WgpuSettingsPriority::Functionality;
    /** @brief If true, prefer a software renderer when available (Bevy
     * force_fallback_adapter). */
    bool force_fallback_adapter = false;
    /** @brief Adapter name filter; when set, the adapter whose info.device
     * matches this substring wins (Bevy adapter_name). The WGPU_ADAPTER_NAME
     * env var takes precedence (Bevy renderer/mod.rs:243-245). */
    std::optional<std::string> adapter_name;
    /** @brief Features to ensure are enabled regardless of adapter support.
     * Mirrors Bevy's default TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES. */
    std::vector<wgpu::FeatureName> features{
        wgpu::FeatureName(wgpu::NativeFeature::eTextureAdapterSpecificFormatFeatures)};
    /** @brief Features to remove from the automatically selected set. */
    std::optional<std::vector<wgpu::FeatureName>> disabled_features;
    /** @brief Imposed device limits (Bevy `WgpuLimits::default()`). */
    wgpu::Limits limits = [] {
        wgpu::Limits result;
        result.setMaxTextureDimension1D(8192)
            .setMaxTextureDimension2D(8192)
            .setMaxTextureDimension3D(2048)
            .setMaxTextureArrayLayers(256)
            .setMaxBindGroups(4)
            .setMaxBindGroupsPlusVertexBuffers(24)
            .setMaxBindingsPerBindGroup(1000)
            .setMaxDynamicUniformBuffersPerPipelineLayout(8)
            .setMaxDynamicStorageBuffersPerPipelineLayout(4)
            .setMaxSampledTexturesPerShaderStage(16)
            .setMaxSamplersPerShaderStage(16)
            .setMaxStorageBuffersPerShaderStage(8)
            .setMaxStorageTexturesPerShaderStage(4)
            .setMaxUniformBuffersPerShaderStage(12)
            .setMaxUniformBufferBindingSize(64ull << 10)
            .setMaxStorageBufferBindingSize(128ull << 20)
            .setMinUniformBufferOffsetAlignment(256)
            .setMinStorageBufferOffsetAlignment(256)
            .setMaxVertexBuffers(8)
            .setMaxBufferSize(256ull << 20)
            .setMaxVertexAttributes(16)
            .setMaxVertexBufferArrayStride(2048)
            .setMaxInterStageShaderVariables(16)
            .setMaxColorAttachments(8)
            .setMaxColorAttachmentBytesPerSample(32)
            .setMaxComputeWorkgroupStorageSize(16384)
            .setMaxComputeInvocationsPerWorkgroup(256)
            .setMaxComputeWorkgroupSizeX(256)
            .setMaxComputeWorkgroupSizeY(256)
            .setMaxComputeWorkgroupSizeZ(64)
            .setMaxComputeWorkgroupsPerDimension(65535);
        return result;
    }();
    /** @brief Upper/lower bounds applied to the selected adapter limits. */
    std::optional<wgpu::Limits> constrained_limits;
    /** @brief DX12 shader compiler used while creating the wgpu instance. */
    wgpu::Dx12Compiler dx12_shader_compiler = wgpu::Dx12Compiler::eFxc;
    /** @brief Requested GLES 3 minor version for the GL backend. */
    wgpu::Gles3MinorVersion gles3_minor_version = wgpu::Gles3MinorVersion::eAutomatic;
    /** @brief wgpu instance debug/validation flags. */
    InstanceFlags instance_flags = InstanceFlags::from_build_config();

    /** @brief Matches Bevy `WgpuSettings::default`: environment overrides
     * participate in construction, including WebGL2's lower default limits. */
    WgpuSettings() {
        apply_env_overrides();
        if (priority == WgpuSettingsPriority::WebGL2) {
            limits.setMaxTextureDimension1D(2048)
                .setMaxTextureDimension2D(2048)
                .setMaxTextureDimension3D(256)
                .setMaxDynamicStorageBuffersPerPipelineLayout(0)
                .setMaxStorageBuffersPerShaderStage(0)
                .setMaxStorageTexturesPerShaderStage(0)
                .setMaxUniformBuffersPerShaderStage(11)
                .setMaxUniformBufferBindingSize(16ull << 10)
                .setMaxStorageBufferBindingSize(0)
                .setMaxVertexBufferArrayStride(255)
                .setMaxInterStageShaderVariables(15)
                .setMaxColorAttachments(4)
                .setMaxComputeWorkgroupStorageSize(0)
                .setMaxComputeInvocationsPerWorkgroup(0)
                .setMaxComputeWorkgroupSizeX(0)
                .setMaxComputeWorkgroupSizeY(0)
                .setMaxComputeWorkgroupSizeZ(0)
                .setMaxComputeWorkgroupsPerDimension(0);
        }
    }

    /** @brief Apply the WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO
     * environment variables on top of the current values (Bevy
     * settings_priority_from_env / PowerPreference::from_env / Backends
     * from_env). */
    void apply_env_overrides() {
        if (const auto configured_backends = Backends::from_env()) backends = *configured_backends;
        if (const char* power = std::getenv("WGPU_POWER_PREF")) {
            std::string p(power);
            std::ranges::transform(p, p.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (p == "low")
                power_preference = wgpu::PowerPreference::eLowPower;
            else if (p == "high")
                power_preference = wgpu::PowerPreference::eHighPerformance;
            else if (p == "none")
                power_preference = wgpu::PowerPreference::eUndefined;
        }
        if (auto configured_priority = settings_priority_from_env()) priority = *configured_priority;
        if (const char* compiler = std::getenv("WGPU_DX12_COMPILER")) {
            std::string value(compiler);
            std::ranges::transform(value, value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (value == "dxc" || value == "dynamicdxc" || value == "staticdxc")
                dx12_shader_compiler = wgpu::Dx12Compiler::eDxc;
            else if (value == "fxc" || value == "auto")
                dx12_shader_compiler = wgpu::Dx12Compiler::eFxc;
        }
        if (const char* gles = std::getenv("WGPU_GLES_MINOR_VERSION")) {
            std::string_view value(gles);
            if (value == "0")
                gles3_minor_version = wgpu::Gles3MinorVersion::eVersion0;
            else if (value == "1")
                gles3_minor_version = wgpu::Gles3MinorVersion::eVersion1;
            else if (value == "2")
                gles3_minor_version = wgpu::Gles3MinorVersion::eVersion2;
            else if (value == "automatic")
                gles3_minor_version = wgpu::Gles3MinorVersion::eAutomatic;
        }
        instance_flags = instance_flags.with_env();
    }
};

/** @brief Stable adapter information exposed to render systems (Bevy
 * `RenderAdapterInfo`). Native `wgpu::AdapterInfo` uses borrowed string
 * views, so Epix owns copies of those strings. */
EPIX_EXPORT struct RenderAdapterInfo {
    std::string vendor;
    std::string architecture;
    std::string device;
    std::string description;
    wgpu::BackendType backend_type = wgpu::BackendType::eUndefined;
    wgpu::AdapterType adapter_type = wgpu::AdapterType::eUnknown;
    std::uint32_t vendor_id        = 0;
    std::uint32_t device_id        = 0;

    static RenderAdapterInfo from_adapter(const wgpu::Adapter& adapter);
};

/** @brief Returns the Qualcomm Adreno model number when available (Bevy
 * `get_adreno_model`). Non-Android builds intentionally return nullopt. */
EPIX_EXPORT std::optional<std::uint32_t> get_adreno_model(const RenderAdapterInfo& adapter_info) noexcept;
/** @brief Returns the Mali `v1.rNNp` driver revision when available (Bevy
 * `get_mali_driver_version`). Non-Android builds intentionally return nullopt. */
EPIX_EXPORT std::optional<std::uint32_t> get_mali_driver_version(const RenderAdapterInfo& adapter_info) noexcept;

/** @brief Renderer resources supplied by an embedding application (Bevy
 * `RenderResources`). Epix deliberately exposes the native wgpu handles;
 * their ownership follows the normal wgpu reference-counting rules. */
EPIX_EXPORT struct RenderResources {
    wgpu::Device device;
    wgpu::Queue queue;
    RenderAdapterInfo adapter_info;
    wgpu::Adapter adapter;
    wgpu::Instance instance;
};

/** @brief Chooses whether RenderPlugin creates wgpu resources itself or uses
 * resources supplied by the host (Bevy `RenderCreation`). */
EPIX_EXPORT struct RenderCreation {
    std::variant<WgpuSettings, RenderResources> value{WgpuSettings{}};

    RenderCreation() = default;
    explicit RenderCreation(WgpuSettings settings) : value(std::move(settings)) {}
    explicit RenderCreation(RenderResources resources) : value(std::move(resources)) {}

    static RenderCreation automatic(WgpuSettings settings = {}) { return RenderCreation{std::move(settings)}; }
    static RenderCreation manual(RenderResources resources) { return RenderCreation{std::move(resources)}; }
    static RenderCreation manual(wgpu::Device device,
                                 wgpu::Queue queue,
                                 RenderAdapterInfo adapter_info,
                                 wgpu::Adapter adapter,
                                 wgpu::Instance instance) {
        return manual(RenderResources{std::move(device), std::move(queue), std::move(adapter_info), std::move(adapter),
                                      std::move(instance)});
    }
    bool is_manual() const noexcept { return std::holds_alternative<RenderResources>(value); }
    const WgpuSettings* automatic_settings() const noexcept { return std::get_if<WgpuSettings>(&value); }
    WgpuSettings* automatic_settings() noexcept { return std::get_if<WgpuSettings>(&value); }
    const RenderResources* manual_resources() const noexcept { return std::get_if<RenderResources>(&value); }
    RenderResources* manual_resources() noexcept { return std::get_if<RenderResources>(&value); }
};

/** @brief Plugin that initializes the WebGPU rendering subsystem. */
EPIX_EXPORT struct RenderPlugin {
    /** @brief Automatic or host-provided renderer creation (Bevy
     * `RenderPlugin::render_creation`). */
    RenderCreation render_creation;
    /** @brief Debugging flags (Bevy RenderPlugin::debug_flags). */
    RenderDebugFlags debug_flags;
    /** @brief When true, compile queued pipelines on the render thread rather
     * than the background task pool (Bevy
     * RenderPlugin::synchronous_pipeline_compilation). */
    bool synchronous_pipeline_compilation = true;
    void attach(app::App&);
    void detach(app::App&) noexcept;
};
void render_system(ecs::World& world);
}  // namespace epix::render

// batching.hpp's BatchingPlugin stores RenderDebugFlags by value, so it is
// included after the RenderDebugFlags definition above.
#include <epix/render/batching.hpp>
