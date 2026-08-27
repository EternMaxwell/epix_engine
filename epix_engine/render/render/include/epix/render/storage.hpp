#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <optional>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/assets.hpp>

namespace epix::render {
/**
 * @brief A storage buffer asset that is prepared as a `RenderAsset` and
 * uploaded to the GPU (Bevy `ShaderStorageBuffer`).
 */
EPIX_EXPORT struct ShaderStorageBuffer {
    /** @brief Optional initial data; cleared after being taken for GPU upload. */
    std::optional<std::vector<std::uint8_t>> data;
    /** @brief Buffer size in bytes. */
    std::uint64_t size = 0;
    /** @brief Buffer usages (STORAGE by default). */
    wgpu::BufferUsage usage = wgpu::BufferUsage::eStorage;
    /** @brief Debug label. */
    std::string label = "ShaderStorageBuffer";
    /** @brief Asset usage flags (Bevy RenderAssetUsages::default() = MAIN_WORLD | RENDER_WORLD). */
    RenderAssetUsages asset_usage = static_cast<RenderAssetUsages>(MAIN_WORLD | RENDER_WORLD);

    ShaderStorageBuffer() = default;

    /** @brief Set typed data, serializing the value into the byte buffer
     * (Bevy ShaderStorageBuffer::set_data). */
    template <typename T>
    void set_data(const T& value) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(std::addressof(value));
        data              = std::vector<std::uint8_t>(begin, begin + sizeof(T));
        size              = sizeof(T);
    }

    /** @brief Create from a typed value (Bevy From<T> for ShaderStorageBuffer). */
    template <typename T>
    static ShaderStorageBuffer from(const T& value) {
        ShaderStorageBuffer s;
        s.set_data(value);
        return s;
    }

    /** @brief Create from raw bytes. */
    static ShaderStorageBuffer with_data(std::span<const std::uint8_t> bytes, RenderAssetUsages usage) {
        ShaderStorageBuffer s;
        s.data        = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        s.size        = bytes.size();
        s.asset_usage = usage;
        return s;
    }

    /** @brief Create with a size but no data. */
    static ShaderStorageBuffer with_size(std::uint64_t size_bytes, RenderAssetUsages usage) {
        ShaderStorageBuffer s;
        s.size        = size_bytes;
        s.asset_usage = usage;
        return s;
    }
};

/**
 * @brief The GPU-side representation of a `ShaderStorageBuffer` (Bevy
 * `GpuShaderStorageBuffer`).
 */
EPIX_EXPORT struct GpuShaderStorageBuffer {
    /** @brief The uploaded GPU buffer. */
    wgpu::Buffer buffer;
    /** @brief True if the source asset provided data on the last upload. */
    bool had_data = false;
};

template <>
struct RenderAsset<ShaderStorageBuffer> {
    using Param          = std::tuple<epix::ecs::Res<wgpu::Device>, epix::ecs::Res<wgpu::Queue>>;
    using ProcessedAsset = GpuShaderStorageBuffer;
    using ExtractedAsset = ShaderStorageBuffer;

    ProcessedAsset prepare_asset(ShaderStorageBuffer&& asset,
                                 assets::AssetId<ShaderStorageBuffer> id,
                                 Param param,
                                 const ProcessedAsset* previous);
    RenderAssetUsages usage(const ShaderStorageBuffer& asset) noexcept;

    /** @brief Move the data out of the stored asset so it stays in Assets<T>
     * (Bevy RenderAsset::take_gpu_data).
     *
     * The declared descriptor is retained in the source. As in Bevy, a second
     * extraction without replacement data is rejected when the previous GPU
     * buffer already owns uploaded data. */
    std::expected<ShaderStorageBuffer, AssetExtractionError> take_gpu_data(
        ShaderStorageBuffer& source, const GpuShaderStorageBuffer* previous_gpu_asset) const {
        const bool valid_upload = source.data.has_value() || !previous_gpu_asset || !previous_gpu_asset->had_data;
        if (!valid_upload) return std::unexpected(AssetExtractionError::AlreadyExtracted);
        ShaderStorageBuffer out;
        out.data        = std::move(source.data);
        out.size        = source.size;
        out.usage       = source.usage;
        out.label       = source.label;
        out.asset_usage = source.asset_usage;
        source.data.reset();
        return out;
    }
};

/**
 * @brief Plugin that registers `ShaderStorageBuffer` as an asset and sets up
 * its extraction/upload (Bevy `StoragePlugin`).
 */
EPIX_EXPORT struct StoragePlugin {
    void attach(app::App& app);
};

}  // namespace epix::render
