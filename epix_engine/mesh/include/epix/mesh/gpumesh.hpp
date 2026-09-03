#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/render.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/mesh/mesh.hpp>

namespace epix::mesh {
/** @brief Tunable mesh-allocator parameters (Bevy 0.18 `MeshAllocatorSettings`).
 *
 * These govern the slab/growth behavior of the mesh GPU memory allocator.
 * Defaults match Bevy: 1 MiB minimum slab, 512 MiB maximum slab, 256 MiB large
 * threshold, and 1.5× growth factor. */
EPIX_EXPORT struct MeshAllocatorSettings {
    std::uint64_t min_slab_size   = 1024 * 1024;       // 1 MiB
    std::uint64_t max_slab_size   = 1024 * 1024 * 512; // 512 MiB
    std::uint64_t large_threshold = 1024 * 1024 * 256; // 256 MiB
    double growth_factor          = 1.5;

    bool operator==(const MeshAllocatorSettings&) const noexcept = default;
};

/** @brief Identifier of a single mesh slab (Bevy `SlabId`, a `NonMaxU32`
 * index with a reserved sentinel). */
EPIX_EXPORT struct SlabId {
    std::uint32_t value = 0;
    constexpr SlabId() noexcept = default;
    constexpr explicit SlabId(std::uint32_t v) noexcept : value(v) {}
    bool operator==(const SlabId&) const noexcept = default;
};

/** @brief Borrowed mesh buffer plus its element range (Bevy
 * `MeshBufferSlice<'a>`). `range` is measured in elements, not bytes. */
EPIX_EXPORT struct MeshBufferSlice {
    const wgpu::Buffer* buffer = nullptr;
    std::uint32_t begin        = 0;
    std::uint32_t end          = 0;
};

/** @brief Element kind stored in a slab (Bevy `ElementClass`). */
enum class ElementClass : std::uint8_t { Vertex, Index };

/** @brief Size/layout info for one element stored in a slab (Bevy
 * `ElementLayout`).
 *
 * Slab objects are allocated in *slots* whose byte size is divisible by both
 * the element size and the 4-byte copy-buffer alignment, so a slot may hold
 * more than one element when the element size isn't a multiple of 4. */
struct ElementLayout {
    ElementClass class_ = ElementClass::Vertex;
    std::uint64_t size  = 0;
    std::uint32_t elements_per_slot = 1;

    static ElementLayout make(ElementClass class_, std::uint64_t size) {
        // 4 / gcd(4, size); equivalently [1,4,2,4][size & 3] (COPY_BUFFER_ALIGNMENT = 4).
        constexpr std::array<std::uint32_t, 4> eps{1, 4, 2, 4};
        return ElementLayout{class_, size, eps[size & 3]};
    }
    std::uint64_t slot_size() const noexcept { return size * elements_per_slot; }
    bool operator==(const ElementLayout&) const noexcept = default;
};

/** @brief Result of a slab growth decision (Bevy `SlabGrowthResult`). */
enum class SlabGrowthResultKind : std::uint8_t { NoGrowthNeeded, NeededGrowth, CantGrow };

/** @brief Compute the grown slot capacity for a slab (Bevy
 * `GeneralSlab::grow_if_necessary`). Returns the new capacity and whether the
 * slab had to grow; `max_slot_capacity` is derived from `settings.max_slab_size
 * / layout.slot_size()`. */
EPIX_EXPORT inline std::pair<std::uint32_t, SlabGrowthResultKind> compute_grow_capacity(
    std::uint32_t current_capacity, std::uint32_t new_size_in_slots, const MeshAllocatorSettings& settings,
    std::uint64_t slot_size) {
    const std::uint32_t max_slot_capacity = static_cast<std::uint32_t>(settings.max_slab_size / slot_size);
    if (current_capacity >= new_size_in_slots) {
        return {current_capacity, SlabGrowthResultKind::NoGrowthNeeded};
    }
    std::uint32_t capacity = current_capacity;
    while (capacity < new_size_in_slots) {
        const std::uint32_t grown1 =
            static_cast<std::uint32_t>(std::ceil(static_cast<double>(capacity) * settings.growth_factor));
        const std::uint32_t grown = grown1 < max_slot_capacity ? grown1 : max_slot_capacity;
        if (grown == capacity) {
            return {capacity, SlabGrowthResultKind::CantGrow};
        }
        capacity = grown;
    }
    return {capacity, SlabGrowthResultKind::NeededGrowth};
}

/** @brief Allocation decision for one mesh payload (Bevy
 * `MeshAllocator::allocate`). Returns the number of slots needed and whether the
 * payload is large enough to deserve its own slab. */
EPIX_EXPORT inline std::pair<std::uint32_t, bool> compute_allocation(std::uint64_t data_byte_len,
                                                                      const ElementLayout& layout,
                                                                      const MeshAllocatorSettings& settings) {
    const std::uint32_t data_element_count =
        static_cast<std::uint32_t>((data_byte_len + layout.size - 1) / layout.size);
    const std::uint32_t data_slot_count =
        static_cast<std::uint32_t>((data_element_count + layout.elements_per_slot - 1) / layout.elements_per_slot);
    const std::uint64_t threshold = settings.large_threshold < settings.max_slab_size ? settings.large_threshold
                                                                                      : settings.max_slab_size;
    const bool is_large = static_cast<std::uint64_t>(data_slot_count) * layout.slot_size() >= threshold;
    return {data_slot_count, is_large};
}

/** @brief Element range of a general-slab allocation (Bevy
 * `mesh_slice_in_slab` for a `Slab::General`): the allocation's slot offset/len
 * scaled by `elements_per_slot`. Measured in elements. */
EPIX_EXPORT inline std::pair<std::uint32_t, std::uint32_t> general_slab_element_range(
    std::uint32_t allocation_offset, std::uint32_t slot_count, const ElementLayout& layout) {
    return {allocation_offset * layout.elements_per_slot,
            (allocation_offset + slot_count) * layout.elements_per_slot};
}

/** @brief Mesh GPU memory allocator (Bevy 0.18 `MeshAllocator`).
 *
 * Tracks which mesh data lives in which slab. The packing/growth/free logic,
 * the slabs container, and the `PrepareAssets` system are still to be
 * implemented; the settings, slab identity, and Bevy's public query surface
 * are provided here so the interface can settle first. */
EPIX_EXPORT struct MeshAllocator {    MeshAllocatorSettings settings;
    std::uint64_t next_slab_id = 0;
    /** @brief Number of currently allocated slabs (placeholder until the real
     * slab container is added with the allocator logic). */
    std::size_t slab_count_placeholder = 0;

    /** @brief Buffer + element range of the mesh's vertex data (Bevy
     * `mesh_vertex_slice`). */
    std::optional<MeshBufferSlice> mesh_vertex_slice(const epix::assets::AssetId<Mesh>&) const { return std::nullopt; }
    /** @brief Buffer + element range of the mesh's index data (Bevy
     * `mesh_index_slice`). */
    std::optional<MeshBufferSlice> mesh_index_slice(const epix::assets::AssetId<Mesh>&) const { return std::nullopt; }
    /** @brief (slab for vertex data, slab for index data) (Bevy `mesh_slabs`). */
    std::pair<std::optional<SlabId>, std::optional<SlabId>> mesh_slabs(
        const epix::assets::AssetId<Mesh>&) const {
        return {std::optional<SlabId>{}, {}};
    }
    /** @brief Number of allocated slabs (Bevy `slab_count`). */
    std::size_t slab_count() const noexcept { return slab_count_placeholder; }
    /** @brief Total size in bytes of all slabs. */
    std::size_t slabs_size() const noexcept { return 0; }
    /** @brief Number of mesh allocations. */
    std::size_t allocations() const noexcept { return 0; }
};

/** @brief GPU-side mesh storing vertex/index buffers uploaded from a Mesh.
 *
 * Created from a CPU Mesh via create_from_mesh, and can be bound to a render pass.
 */
EPIX_EXPORT struct GPUMesh {
   public:
    /** @brief Create a GPUMesh from a CPU Mesh, using default device limits.
     *  @param mesh The source mesh.
     *  @param device The wgpu device for buffer allocation. */
    static GPUMesh create_from_mesh(const Mesh& mesh, const wgpu::Device& device);
    /** @brief Create a GPUMesh from a CPU Mesh with explicit device limits.
     *  @param mesh The source mesh.
     *  @param device The wgpu device for buffer allocation.
     *  @param limits Device limits for alignment constraints. */
    static GPUMesh create_from_mesh(const Mesh& mesh, const wgpu::Device& device, const wgpu::Limits& limits);
    /** @brief Re-upload mesh data from a CPU Mesh to existing GPU buffers.
     *  @param mesh The source mesh.
     *  @param device The wgpu device.
     *  @param limits Device limits for alignment constraints. */
    void update_from_mesh(const Mesh& mesh, const wgpu::Device& device, const wgpu::Limits& limits);
    /** @brief Check whether this mesh uses indexed drawing. */
    bool is_indexed() const noexcept { return _index_binding.has_value(); }
    /** @brief Get the number of vertices (or indices if indexed). */
    std::size_t vertex_count() const noexcept { return _vertex_count; }
    /** @brief Get the primitive topology of this mesh. */
    wgpu::PrimitiveTopology primitive_type() const noexcept { return _primitive_type; }
    /** @brief Bind vertex and index buffers to a render pass encoder. */
    void bind_to(const wgpu::RenderPassEncoder& encoder) const;
    /** @brief Iterate over the mesh attribute descriptors. */
    auto iter_attributes() const { return std::views::values(_attributes); }
    /** @brief Check whether this mesh contains a specific attribute. */
    bool contains_attribute(const MeshAttribute& attribute) const noexcept {
        auto it = _attributes.find(attribute.slot);
        return it != _attributes.end() && it->second == attribute;
    }
    /** @brief Get the full attribute layout map. */
    const MeshAttributeLayout& attribute_layout() const noexcept { return _attributes; }

   private:
    GPUMesh() noexcept
        : _primitive_type(wgpu::PrimitiveTopology::eTriangleList),
          _combined_buffer(nullptr),
          _index_buffer(nullptr),
          _vertex_count(0) {}

    struct VertexBindingInfo {
        std::uint32_t shader_location;
        std::size_t offset;
        std::size_t size;
    };
    struct IndexBindingInfo {
        wgpu::IndexFormat format;
        std::uint32_t offset;
        std::size_t size;
    };

    MeshAttributeLayout _attributes;

    wgpu::PrimitiveTopology _primitive_type;
    wgpu::Buffer _combined_buffer;
    wgpu::Buffer _index_buffer;
    std::vector<VertexBindingInfo> _attribute_bindings;
    std::optional<IndexBindingInfo> _index_binding;
    std::size_t _vertex_count;  // or index count if indexed
};
}  // namespace epix::mesh

template <>
struct epix::render::RenderAsset<epix::mesh::Mesh> {
    using ProcessedAsset = epix::mesh::GPUMesh;
    using ExtractedAsset = epix::mesh::Mesh;
    using Param          = epix::ecs::ParamSet<epix::ecs::Res<wgpu::Device>, epix::ecs::Res<wgpu::Limits>>;

    std::expected<ProcessedAsset, epix::render::PrepareAssetError<epix::mesh::Mesh>> prepare_asset(
        epix::mesh::Mesh&& mesh, epix::assets::AssetId<epix::mesh::Mesh>, Param params, const ProcessedAsset*) {
        auto&& [device, limits] = params.get();
        return ProcessedAsset::create_from_mesh(mesh, *device, *limits);
    }

    epix::render::RenderAssetUsages usage(const epix::mesh::Mesh& mesh) noexcept {
        (void)mesh;
        return epix::render::RenderAssetUsages::RENDER_WORLD;
    }

    /** @brief Estimated GPU payload in bytes (Bevy `RenderAsset::byte_len` for
     * `RenderMesh`). Sums the per-vertex attribute stride over the vertex count,
     * plus the index bytes. Used by the render-asset byte limiter. */
    std::optional<std::size_t> byte_len(const epix::mesh::Mesh& mesh) const {
        std::size_t vertex_size = 0;
        for (const auto& data : mesh.iter_attributes()) {
            vertex_size += epix::mesh::vertex_format_size(data.attribute.format);
        }
        const std::size_t vertex_count = mesh.count_vertices();
        std::size_t index_bytes        = 0;
        if (auto indices = mesh.get_indices(); indices) {
            const auto& index = indices->get();
            index_bytes       = index.size() * (index.is_u16() ? sizeof(std::uint16_t) : sizeof(std::uint32_t));
        }
        return vertex_size * vertex_count + index_bytes;
    }

    std::expected<epix::mesh::Mesh, epix::render::AssetExtractionError> take_gpu_data(
        epix::mesh::Mesh& source, const ProcessedAsset*) const {
        if (auto extracted = source.take_gpu_data()) {
            return std::move(*extracted);
        }
        return std::unexpected(epix::render::AssetExtractionError::AlreadyExtracted);
    }
};

EPIX_EXPORT namespace std {
    template <>
    struct hash<epix::assets::AssetId<epix::mesh::Mesh>> {
        std::size_t operator()(const epix::assets::AssetId<epix::mesh::Mesh>& id) const {
            return std::visit([]<typename T>(const T& value) { return std::hash<T>()(value); }, id);
        }
    };
}  // namespace std
