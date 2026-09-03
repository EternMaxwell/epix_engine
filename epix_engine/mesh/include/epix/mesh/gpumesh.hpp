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

EPIX_EXPORT namespace std {
    template <>
    struct hash<epix::assets::AssetId<epix::mesh::Mesh>> {
        std::size_t operator()(const epix::assets::AssetId<epix::mesh::Mesh>& id) const {
            return std::visit([]<typename T>(const T& value) { return std::hash<T>()(value); }, id);
        }
    };
}  // namespace std

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
    bool operator<(const SlabId& o) const noexcept { return value < o.value; }
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

/** @brief A mesh's data allocation within a general slab (Bevy
 * `SlabAllocation`: an `offset_allocator` handle plus slot count). */
struct SlabAllocation {
    std::uint32_t offset     = 0;  // slot offset within the slab
    std::uint32_t slot_count = 0;
    bool operator==(const SlabAllocation&) const noexcept = default;
};

/** @brief A growable slab that packs multiple mesh payloads (Bevy
 * `GeneralSlab`). This is the device-free bookkeeping container; the actual GPU
 * buffer is created separately. A simple sequential-slot allocator is used in
 * place of Bevy's offset-allocator until that is ported. */
struct GeneralSlab {
    ElementLayout element_layout;
    std::uint32_t current_slot_capacity = 0;
    // Sequential allocation cursor (slots).
    std::uint32_t next_slot       = 0;
    std::uint32_t occupied_slots  = 0;
    std::uint32_t allocation_seq  = 0;  // for debugging/order only
    // Backing GPU buffer (created lazily on the device).
    wgpu::Buffer buffer = nullptr;

    static GeneralSlab make(ElementLayout layout, const MeshAllocatorSettings& settings) {
        std::uint32_t cap = static_cast<std::uint32_t>(settings.min_slab_size / layout.slot_size());
        return GeneralSlab{layout, cap, 0, 0, 0, nullptr};
    }
    /** @brief Reserve `slot_count` slots, growing the slab if needed.
     * Returns the allocation (offset + slot_count), or nullopt if the slab
     * cannot grow to fit (reached `max_slab_size`). */
    std::optional<SlabAllocation> allocate(std::uint32_t slot_count, const MeshAllocatorSettings& settings) {
        auto grow = compute_grow_capacity(current_slot_capacity, next_slot + slot_count, settings,
                                          element_layout.slot_size());
        if (grow.second == SlabGrowthResultKind::CantGrow) {
            return std::nullopt;
        }
        current_slot_capacity = grow.first;
        const SlabAllocation alloc{next_slot, slot_count};
        next_slot += slot_count;
        occupied_slots += slot_count;
        ++allocation_seq;
        return alloc;
    }
    bool is_empty() const noexcept { return occupied_slots == 0; }
};

/** @brief Mesh GPU memory allocator (Bevy 0.18 `MeshAllocator`).
 *
 * Tracks which mesh data lives in which slab. This provides the device-free
 * slab bookkeeping (allocate vertex data into the right general slab, query
 * slab count/size). Per-mesh asset bookkeeping (`mesh_vertex_slice`,
 * `mesh_index_slice`, `mesh_slabs`) and the GPU buffer creation + `PrepareAssets`
 * system are still to be wired up. */
EPIX_EXPORT struct MeshAllocator {
    MeshAllocatorSettings settings;
    std::uint64_t next_slab_id = 0;
    /** @brief General slabs keyed by slab id. */
    std::map<SlabId, GeneralSlab> slabs;
    /// Mesh asset id -> (slab, allocation) holding its vertex data.
    std::unordered_map<epix::assets::AssetId<Mesh>, std::pair<SlabId, SlabAllocation>> mesh_id_to_vertex_slab;
    /// Mesh asset id -> (slab, allocation) holding its index data.
    std::unordered_map<epix::assets::AssetId<Mesh>, std::pair<SlabId, SlabAllocation>> mesh_id_to_index_slab;

    /** @brief Record which slab + allocation holds a mesh's vertex/index data
     * (Bevy `MeshAllocator::record_allocation`). */
    void record_allocation(const epix::assets::AssetId<Mesh>& id, SlabId slab, SlabAllocation alloc, bool is_vertex) {
        (is_vertex ? mesh_id_to_vertex_slab : mesh_id_to_index_slab)[id] = {slab, alloc};
    }

    /** @brief Ensure a slab's backing GPU buffer exists and return it (Bevy
     * creates slab buffers lazily at allocate time; here it is explicit). */
    wgpu::Buffer ensure_slab_buffer(const wgpu::Device& device, SlabId slab_id, wgpu::BufferUsage usage) {
        auto it = slabs.find(slab_id);
        if (it == slabs.end()) return nullptr;
        auto& slab = it->second;
        if (!slab.buffer) {
            slab.buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setSize(static_cast<std::uint64_t>(slab.current_slot_capacity) *
                                                           slab.element_layout.slot_size())
                                                  .setLabel("MeshAllocator-slab")
                                                  .setUsage(usage | wgpu::BufferUsage::eCopyDst));
        }
        return slab.buffer;
    }
    /** @brief Upload fixed bytes into a slab allocation (the caller supplies the
     * packed element bytes). */
    void upload_to_slab(const wgpu::Queue& queue, SlabId slab_id, SlabAllocation alloc, const void* data,
                        std::size_t bytes) {
        auto it = slabs.find(slab_id);
        if (it == slabs.end() || !it->second.buffer) return;
        queue.writeBuffer(it->second.buffer,
                          static_cast<std::uint64_t>(alloc.offset) * it->second.element_layout.slot_size(), data,
                          bytes);
    }

    /** @brief Buffer + element range of the mesh's vertex data (Bevy
     * `mesh_vertex_slice`). */
    std::optional<MeshBufferSlice> mesh_vertex_slice(const epix::assets::AssetId<Mesh>& id) const {
        return mesh_slice(id, true);
    }
    /** @brief Buffer + element range of the mesh's index data (Bevy
     * `mesh_index_slice`). */
    std::optional<MeshBufferSlice> mesh_index_slice(const epix::assets::AssetId<Mesh>& id) const {
        return mesh_slice(id, false);
    }
    /** @brief (slab for vertex data, slab for index data) (Bevy `mesh_slabs`). */
    std::pair<std::optional<SlabId>, std::optional<SlabId>> mesh_slabs(
        const epix::assets::AssetId<Mesh>& id) const {
        std::optional<SlabId> vertex;
        if (auto it = mesh_id_to_vertex_slab.find(id); it != mesh_id_to_vertex_slab.end()) vertex = it->second.first;
        std::optional<SlabId> index;
        if (auto it = mesh_id_to_index_slab.find(id); it != mesh_id_to_index_slab.end()) index = it->second.first;
        return {vertex, index};
    }
    /** @brief Internal slice computation for a mesh's vertex/index data. */
    std::optional<MeshBufferSlice> mesh_slice(const epix::assets::AssetId<Mesh>& id, bool is_vertex) const {
        const auto& map = is_vertex ? mesh_id_to_vertex_slab : mesh_id_to_index_slab;
        auto it         = map.find(id);
        if (it == map.end()) return std::nullopt;
        const auto& [slab_id, alloc] = it->second;
        auto sit                     = slabs.find(slab_id);
        if (sit == slabs.end() || !sit->second.buffer) return std::nullopt;
        auto range = general_slab_element_range(alloc.offset, alloc.slot_count, sit->second.element_layout);
        return MeshBufferSlice{std::addressof(sit->second.buffer), range.first, range.second};
    }

    /** @brief Reserve `slot_count` slots for a payload of `layout` in a general
     * slab, creating one if needed. Returns the chosen slab id + allocation, or
     * nullopt when no existing/created slab can fit (reached max size). */
    std::optional<std::pair<SlabId, SlabAllocation>> allocate(std::uint32_t slot_count, const ElementLayout& layout) {
        for (auto& [slab_id, slab] : slabs) {
            if (slab.element_layout == layout) {
                if (auto a = slab.allocate(slot_count, settings)) return std::pair{slab_id, *a};
            }
        }
        auto slab = GeneralSlab::make(layout, settings);
        auto a    = slab.allocate(slot_count, settings);
        if (!a) return std::nullopt;
        const SlabId new_id{static_cast<std::uint32_t>(next_slab_id++)};
        slabs.emplace(new_id, std::move(slab));
        return std::pair{new_id, *a};
    }
    /** @brief Number of allocated slabs (Bevy `slab_count`). */
    std::size_t slab_count() const noexcept { return slabs.size(); }
    /** @brief Total size in bytes of all slabs. */
    std::size_t slabs_size() const noexcept {
        std::size_t total = 0;
        for (const auto& [id, slab] : slabs) {
            (void)id;
            total += static_cast<std::size_t>(slab.current_slot_capacity) * slab.element_layout.slot_size();
        }
        return total;
    }
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

