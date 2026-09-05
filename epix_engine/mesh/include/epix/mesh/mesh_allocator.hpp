#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/assets.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/mesh/mesh.hpp>
#include <epix/mesh/offset_allocator.hpp>

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

namespace detail {

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

}  // namespace detail

}  // namespace epix::mesh

namespace std {
    template <>
    struct hash<epix::mesh::detail::ElementLayout> {
        std::size_t operator()(const epix::mesh::detail::ElementLayout& layout) const noexcept {
            std::size_t result = static_cast<std::size_t>(layout.class_);
            result ^= static_cast<std::size_t>(layout.size) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^= static_cast<std::size_t>(layout.elements_per_slot) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            return result;
        }
    };
}  // namespace std

namespace epix::mesh {

namespace detail {

/** @brief A slab did not need to grow (Bevy
 * `SlabGrowthResult::NoGrowthNeeded`). */
struct NoSlabGrowthNeeded {};

/** @brief Original capacity recorded when a slab schedules one reallocation
 * (Bevy `SlabToReallocate`). */
struct SlabToReallocate {
    std::uint32_t old_slot_capacity = 0;
};

/** @brief A slab could not grow within the configured maximum (Bevy
 * `SlabGrowthResult::CantGrow`). */
struct SlabCantGrow {};

/** @brief Payload-bearing equivalent of Bevy's `SlabGrowthResult`. */
using SlabGrowthResult = std::variant<NoSlabGrowthNeeded, SlabToReallocate, SlabCantGrow>;

/** @brief Per-frame set of slabs that need one allocation/reallocation. */
using SlabsToReallocate = std::map<SlabId, SlabToReallocate>;

/** @brief Compute the grown slot capacity for a slab (Bevy
 * `GeneralSlab::grow_if_necessary`). Returns the new capacity and the typed
 * growth result; `max_slot_capacity` is derived from
 * `settings.max_slab_size / layout.slot_size()`. */
inline std::pair<std::uint32_t, SlabGrowthResult> compute_grow_capacity(
    std::uint32_t current_capacity, std::uint32_t new_size_in_slots, const MeshAllocatorSettings& settings,
    std::uint64_t slot_size) {
    const std::uint32_t max_slot_capacity = static_cast<std::uint32_t>(settings.max_slab_size / slot_size);
    if (current_capacity >= new_size_in_slots) {
        return {current_capacity, NoSlabGrowthNeeded{}};
    }
    const std::uint32_t initial_capacity = current_capacity;
    std::uint32_t capacity = current_capacity;
    while (capacity < new_size_in_slots) {
        const std::uint32_t grown1 =
            static_cast<std::uint32_t>(std::ceil(static_cast<double>(capacity) * settings.growth_factor));
        const std::uint32_t grown = grown1 < max_slot_capacity ? grown1 : max_slot_capacity;
        if (grown == capacity) {
            return {capacity, SlabCantGrow{}};
        }
        capacity = grown;
    }
    return {capacity, SlabToReallocate{.old_slot_capacity = initial_capacity}};
}

/** @brief Allocation decision for one mesh payload (Bevy
 * `MeshAllocator::allocate`). Returns the number of slots needed and whether the
 * payload is large enough to deserve its own slab. */
inline std::pair<std::uint32_t, bool> compute_allocation(std::uint64_t data_byte_len,
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
inline std::pair<std::uint32_t, std::uint32_t> general_slab_element_range(
    std::uint32_t allocation_offset, std::uint32_t slot_count, const ElementLayout& layout) {
    return {allocation_offset * layout.elements_per_slot,
            (allocation_offset + slot_count) * layout.elements_per_slot};
}

/** @brief A mesh's data allocation within a slab (Bevy `SlabAllocation`: an
 * `offset_allocator::Allocation` handle plus the slot count). The exposed
 * `offset` is the slot offset within the slab. */
struct SlabAllocation {
    offset_allocator::Allocation allocation;
    std::uint32_t slot_count = 0;

    std::uint32_t offset() const noexcept { return allocation.offset; }
    bool operator==(const SlabAllocation&) const noexcept = default;
};

/** @brief A growable slab that packs multiple mesh payloads (Bevy
 * `GeneralSlab`). Space is managed by an `offset_allocator::Allocator` so freed
 * gaps are reused; the actual GPU buffer is created separately. */
struct GeneralSlab {
    offset_allocator::Allocator allocator;
    wgpu::Buffer buffer = nullptr;
    /// Allocations that are already on the GPU (slot ranges).
    std::unordered_map<epix::assets::AssetId<Mesh>, SlabAllocation> resident_allocations;
    /// Allocations that are waiting to be uploaded to the GPU (slot ranges).
    std::unordered_map<epix::assets::AssetId<Mesh>, SlabAllocation> pending_allocations;
    ElementLayout element_layout;
    std::uint32_t current_slot_capacity = 0;

    static GeneralSlab make(ElementLayout layout, std::uint32_t data_slot_count,
                            const MeshAllocatorSettings& settings) {
        const std::uint32_t min_capacity = static_cast<std::uint32_t>(
            (settings.min_slab_size + layout.slot_size() - 1) / layout.slot_size());
        // Bevy GeneralSlab::new: initial capacity is at least the minimum slab
        // size and at least what offset-allocator needs to hold the payload.
        const std::uint32_t initial_capacity =
            min_capacity > offset_allocator::min_allocator_size(data_slot_count)
                ? min_capacity
                : offset_allocator::min_allocator_size(data_slot_count);
        const std::uint32_t max_capacity = std::max(
            static_cast<std::uint32_t>((settings.max_slab_size + layout.slot_size() - 1) / layout.slot_size()),
            offset_allocator::min_allocator_size(data_slot_count));
        return GeneralSlab{offset_allocator::Allocator{max_capacity}, nullptr, {}, {}, layout, initial_capacity};
    }
    /** @brief Check whether the slab is large enough for `new_size_in_slots`,
     * growing it (Bevy `grow_if_necessary`). Returns the growth result kind. */
    SlabGrowthResult grow_if_necessary(std::uint32_t new_size_in_slots,
                                       const MeshAllocatorSettings& settings) {
        auto grow = compute_grow_capacity(current_slot_capacity, new_size_in_slots, settings,
                                          element_layout.slot_size());
        current_slot_capacity = grow.first;
        return grow.second;
    }
    bool is_empty() const noexcept { return resident_allocations.empty() && pending_allocations.empty(); }
};

/** @brief A slab that contains a single (large) object (Bevy
 * `LargeObjectSlab`). */
struct LargeObjectSlab {
    wgpu::Buffer buffer = nullptr;
    ElementLayout element_layout;
};

/** @brief A single hardware buffer variant: either a packed general slab or a
 * dedicated large-object buffer (Bevy `Slab`). */
struct Slab {
    std::variant<GeneralSlab, LargeObjectSlab> value;

    static Slab general(GeneralSlab slab) { return Slab{std::move(slab)}; }
    static Slab large(LargeObjectSlab slab) { return Slab{std::move(slab)}; }

    GeneralSlab* general() noexcept { return std::get_if<GeneralSlab>(&value); }
    const GeneralSlab* general() const noexcept { return std::get_if<GeneralSlab>(&value); }
    LargeObjectSlab* large() noexcept { return std::get_if<LargeObjectSlab>(&value); }
    const LargeObjectSlab* large() const noexcept { return std::get_if<LargeObjectSlab>(&value); }
};

struct MeshAllocatorAccess;
}  // namespace detail

struct MeshAllocatorPlugin;

/** @brief Mesh GPU memory allocator (Bevy 0.18 `MeshAllocator`).
 *
 * Tracks which mesh data lives in which slab, packing vertex/index data into
 * shared general slabs and giving oversized payloads their own large-object
 * slab. Slab capacity grows geometrically; freed slots are reclaimed by the
 * offset-allocator. `MeshAllocatorPlugin` drives this from extracted render
 * meshes in `PrepareAssets`. */
EPIX_EXPORT struct MeshAllocator {
   public:
    /** @brief Additional usage flags applied to every mesh buffer (Bevy
     * `MeshAllocator::extra_buffer_usages`). */
    wgpu::BufferUsage extra_buffer_usages = wgpu::BufferUsage::eNone;

    MeshAllocator(const MeshAllocator&) = delete;
    MeshAllocator(MeshAllocator&&) noexcept = default;
    MeshAllocator& operator=(const MeshAllocator&) = delete;
    MeshAllocator& operator=(MeshAllocator&&) noexcept = default;

    /** @brief Construct after render-device initialization (Bevy `FromWorld`
     * for `MeshAllocator`). */
    static MeshAllocator from_world(epix::ecs::World& world);

   private:
    friend struct MeshAllocatorPlugin;
    friend struct detail::MeshAllocatorAccess;

    explicit MeshAllocator(bool supports_general_vertex_slabs)
        : general_vertex_slabs_supported(supports_general_vertex_slabs) {}

    std::uint32_t next_slab_id = 0;
    /// All slabs keyed by slab id (Bevy `slabs`).
    std::map<SlabId, detail::Slab> slabs;
    /// Maps a layout to the slabs that hold elements of that layout (Bevy
    /// `slab_layouts`), used when allocating to find the right slab fast.
    std::unordered_map<detail::ElementLayout, std::vector<SlabId>> slab_layouts;
    /// Mesh asset id -> slab holding its vertex data (Bevy
    /// `mesh_id_to_vertex_slab`).
    std::unordered_map<epix::assets::AssetId<Mesh>, SlabId> mesh_id_to_vertex_slab;
    /// Mesh asset id -> slab holding its index data (Bevy
    /// `mesh_id_to_index_slab`).
    std::unordered_map<epix::assets::AssetId<Mesh>, SlabId> mesh_id_to_index_slab;

    // wgpu-native's C API does not expose wgpu-core's DownlevelFlags. Epix's
    // temporary renderer-side Vulkan coercion guarantees BASE_VERTEX, so this
    // is true until that workaround and this local fallback can be removed.
    bool general_vertex_slabs_supported = true;

    /** @brief Record which slab holds a mesh's vertex/index data (Bevy
     * `MeshAllocator::record_allocation`). */
    void record_allocation(const epix::assets::AssetId<Mesh>& id, SlabId slab,
                           detail::ElementClass element_class) {
        switch (element_class) {
            case detail::ElementClass::Vertex:
                mesh_id_to_vertex_slab[id] = slab;
                break;
            case detail::ElementClass::Index:
                mesh_id_to_index_slab[id] = slab;
                break;
        }
    }

   public:
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
        if (auto it = mesh_id_to_vertex_slab.find(id); it != mesh_id_to_vertex_slab.end()) vertex = it->second;
        std::optional<SlabId> index;
        if (auto it = mesh_id_to_index_slab.find(id); it != mesh_id_to_index_slab.end()) index = it->second;
        return {vertex, index};
    }
   private:
    /** @brief Internal slice computation for a mesh's vertex/index data (Bevy
     * `mesh_slice_in_slab`). */
    std::optional<MeshBufferSlice> mesh_slice(const epix::assets::AssetId<Mesh>& id, bool is_vertex) const {
        const auto& map = is_vertex ? mesh_id_to_vertex_slab : mesh_id_to_index_slab;
        auto it         = map.find(id);
        if (it == map.end()) return std::nullopt;
        const auto sit = slabs.find(it->second);
        if (sit == slabs.end()) return std::nullopt;
        if (const auto* general = sit->second.general()) {
            const auto alloc_it = general->resident_allocations.find(id);
            if (alloc_it == general->resident_allocations.end() || !general->buffer) return std::nullopt;
            auto range = detail::general_slab_element_range(alloc_it->second.offset(), alloc_it->second.slot_count,
                                                            general->element_layout);
            return MeshBufferSlice{std::addressof(general->buffer), range.first, range.second};
        }
        if (const auto* large = sit->second.large()) {
            if (!large->buffer) return std::nullopt;
            const std::uint32_t element_count =
                static_cast<std::uint32_t>(large->buffer.getSize() / large->element_layout.size);
            return MeshBufferSlice{std::addressof(large->buffer), 0, element_count};
        }
        return std::nullopt;
    }

    /** @brief Allocate space for a mesh payload of `layout`, creating a slab if
     * necessary (Bevy `MeshAllocator::allocate`). The allocation is recorded as
     * pending; `copy_element_data` later writes it into the GPU buffer and
     * moves it to resident. */
    void allocate(const epix::assets::AssetId<Mesh>& id, std::uint64_t data_byte_len,
                  const detail::ElementLayout& layout, detail::SlabsToReallocate& slabs_to_reallocate,
                  const MeshAllocatorSettings& settings) {
        const auto [data_slot_count, is_large] = detail::compute_allocation(data_byte_len, layout, settings);
        // Too large for a general slab: give it a slab of its own.
        if (is_large) {
            allocate_large(id, layout);
            return;
        }
        allocate_general(id, data_slot_count, layout, slabs_to_reallocate, settings);
    }

    /** @brief Ensure a slab's backing GPU buffer exists at its current capacity
     * and return it. General slabs create buffers lazily; when a slab has grown
     * since its buffer was created, the buffer is reallocated and the old
     * contents copied across (Bevy `reallocate_slab`). */
    void reallocate_slab(const wgpu::Device& device, const wgpu::Queue& queue, SlabId slab_id,
                         detail::SlabToReallocate slab_to_reallocate) {
        auto it = slabs.find(slab_id);
        if (it == slabs.end()) return;
        auto* general = it->second.general();
        if (!general) return;
        const std::uint64_t needed_size =
            static_cast<std::uint64_t>(general->current_slot_capacity) * general->element_layout.slot_size();
        auto usage = wgpu::BufferUsage::eCopySrc | wgpu::BufferUsage::eCopyDst | extra_buffer_usages;
        usage = usage | (general->element_layout.class_ == detail::ElementClass::Vertex
                             ? wgpu::BufferUsage::eVertex
                             : wgpu::BufferUsage::eIndex);
        auto new_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setSize(needed_size)
                                                  .setLabel("general mesh slab")
                                                  .setUsage(usage));
        const auto old_buffer = std::exchange(general->buffer, new_buffer);
        if (!old_buffer) return;

        const std::uint64_t bytes_to_copy = static_cast<std::uint64_t>(slab_to_reallocate.old_slot_capacity) *
                                            general->element_layout.slot_size();
        if (bytes_to_copy == 0) return;
        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("slab resize encoder"));
        encoder.copyBufferToBuffer(old_buffer, 0, new_buffer, 0, bytes_to_copy);
        queue.submit(std::array{encoder.finish()});
    }

    /** @brief Upload fixed bytes into a slab allocation, moving it from pending
     * to resident (Bevy `copy_element_data`). General slabs write into the
     * existing buffer at the allocation's byte offset; large-object slabs create
     * their dedicated buffer with mapped-at-creation and fill it in one go. */
    void copy_element_data(const wgpu::Device& device, const wgpu::Queue& queue, SlabId slab_id,
                           const epix::assets::AssetId<Mesh>& id, const void* data, std::size_t bytes,
                           wgpu::BufferUsage usage) {
        auto it = slabs.find(slab_id);
        if (it == slabs.end()) return;
        if (auto* large = it->second.large()) {
            // Bevy copy_element_data for Slab::LargeObject: create the buffer
            // and its data in one go.
            if (!large->buffer) {
                large->buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                        .setSize(bytes)
                                                        .setLabel("large mesh slab")
                                                        .setUsage(usage | wgpu::BufferUsage::eCopyDst |
                                                                  extra_buffer_usages)
                                                        .setMappedAtCreation(true));
                if (auto* range = static_cast<std::uint8_t*>(large->buffer.getMappedRange(0, bytes))) {
                    std::memcpy(range, data, bytes);
                }
                large->buffer.unmap();
            }
            return;
        }
        auto* general = it->second.general();
        if (!general || !general->buffer) return;
        // Bevy GeneralSlab::copy_element_data: the write size is rounded up to
        // the slot size (a multiple of COPY_BUFFER_ALIGNMENT) so wgpu accepts
        // the copy; only the payload bytes are meaningful.
        const auto pending = general->pending_allocations.find(id);
        if (pending == general->pending_allocations.end()) return;
        const auto allocation = pending->second;
        const std::uint64_t slot_size = general->element_layout.slot_size();
        const std::uint64_t aligned = (bytes + slot_size - 1) / slot_size * slot_size;
        if (aligned == bytes) {
            queue.writeBuffer(general->buffer, static_cast<std::uint64_t>(allocation.offset()) * slot_size, data,
                              bytes);
        } else {
            std::vector<std::uint8_t> padded(static_cast<std::size_t>(aligned), 0);
            std::memcpy(padded.data(), data, bytes);
            queue.writeBuffer(general->buffer, static_cast<std::uint64_t>(allocation.offset()) * slot_size,
                              padded.data(), padded.size());
        }
        general->resident_allocations[id] = allocation;
        general->pending_allocations.erase(pending);
    }

    /** @brief Allocate within general slabs, growing an existing one or creating
     * a new one (Bevy `MeshAllocator::allocate_general`). The payload is
     * recorded as pending and the mesh->slab mapping is recorded. */
    void allocate_general(const epix::assets::AssetId<Mesh>& id, std::uint32_t data_slot_count,
                          const detail::ElementLayout& layout, detail::SlabsToReallocate& slabs_to_reallocate,
                          const MeshAllocatorSettings& settings) {
        // Loop through the slabs that accept the layout, trying the first one
        // that can fit the payload (Bevy slab_layouts candidate search).
        auto& candidate_slabs = slab_layouts[layout];
        std::optional<std::pair<SlabId, detail::SlabAllocation>> mesh_allocation;
        for (const auto& slab_id : candidate_slabs) {
            auto it = slabs.find(slab_id);
            if (it == slabs.end()) continue;
            auto* general = it->second.general();
            if (!general) continue;
            auto allocation = general->allocator.allocate(data_slot_count);
            if (!allocation) continue;
            const auto growth = general->grow_if_necessary(allocation->offset + data_slot_count, settings);
            if (std::holds_alternative<detail::SlabCantGrow>(growth)) {
                continue;
            }
            if (const auto* reallocate = std::get_if<detail::SlabToReallocate>(&growth)) {
                // Preserve the capacity at the beginning of the frame if this
                // slab grows more than once before reallocation.
                slabs_to_reallocate.try_emplace(slab_id, *reallocate);
            }
            mesh_allocation = std::pair{slab_id, detail::SlabAllocation{*allocation, data_slot_count}};
            break;
        }
        // No existing slab fit: create a new one big enough for the payload.
        if (!mesh_allocation) {
            const SlabId new_id{next_slab_id};
            next_slab_id = next_slab_id == UINT32_MAX - 1 ? 0 : next_slab_id + 1;
            auto new_slab   = detail::GeneralSlab::make(layout, data_slot_count, settings);
            auto allocation = new_slab.allocator.allocate(data_slot_count);
            if (!allocation) return;
            mesh_allocation = std::pair{new_id, detail::SlabAllocation{*allocation, data_slot_count}};
            slabs.emplace(new_id, detail::Slab::general(std::move(new_slab)));
            candidate_slabs.push_back(new_id);
            slabs_to_reallocate.try_emplace(new_id, detail::SlabToReallocate{});
        }
        const auto& [slab_id, slab_allocation] = *mesh_allocation;
        // Mark the allocation as pending; do not copy it in yet (Bevy
        // allocate_meshes batches uploads after all allocations).
        if (auto* general = slabs.at(slab_id).general()) {
            general->pending_allocations[id] = slab_allocation;
        }
        record_allocation(id, slab_id, layout.class_);
    }

    /** @brief Allocate a payload into its own dedicated large-object slab
     * (Bevy `MeshAllocator::allocate_large`). */
    void allocate_large(const epix::assets::AssetId<Mesh>& id, const detail::ElementLayout& layout) {
        const SlabId new_id{next_slab_id};
        next_slab_id = next_slab_id == UINT32_MAX - 1 ? 0 : next_slab_id + 1;
        record_allocation(id, new_id, layout.class_);
        slabs.emplace(new_id, detail::Slab::large(detail::LargeObjectSlab{nullptr, layout}));
    }

    /** @brief Release a mesh's allocation (vertex or index) and remove the slab
     * once it becomes empty (Bevy `MeshAllocator::free_meshes` +
     * `free_allocation_in_slab`). */
    void free(const epix::assets::AssetId<Mesh>& id, bool is_vertex) {
        auto& map = is_vertex ? mesh_id_to_vertex_slab : mesh_id_to_index_slab;
        auto it   = map.find(id);
        if (it == map.end()) return;
        const SlabId slab_id = it->second;
        map.erase(it);
        auto sit = slabs.find(slab_id);
        if (sit == slabs.end()) return;
        if (sit->second.large()) {
            // Large-object slabs hold a single mesh and are removed with it
            // (Bevy free_allocation_in_slab for Slab::LargeObject).
            slabs.erase(sit);
            return;
        }
        auto* general = sit->second.general();
        if (!general) return;
        detail::SlabAllocation slab_allocation;
        if (auto resident = general->resident_allocations.find(id); resident != general->resident_allocations.end()) {
            slab_allocation = resident->second;
            general->resident_allocations.erase(resident);
        } else if (auto pending = general->pending_allocations.find(id);
                   pending != general->pending_allocations.end()) {
            slab_allocation = pending->second;
            general->pending_allocations.erase(pending);
        } else {
            return;
        }
        general->allocator.free(slab_allocation.allocation);
        if (general->is_empty()) {
            remove_empty_slab(slab_id);
        }
    }
    /** @brief Free both vertex and index allocations for a mesh. */
    void free_all(const epix::assets::AssetId<Mesh>& id) {
        free(id, true);
        free(id, false);
    }

   public:
    /** @brief Number of index allocations (Bevy `allocations()`:
     * `mesh_id_to_index_slab.len()`). */
    std::size_t allocations() const noexcept { return mesh_id_to_index_slab.size(); }
    /** @brief Number of allocated slabs (Bevy `slab_count`). */
    std::size_t slab_count() const noexcept { return slabs.size(); }
    /** @brief Total size in bytes of all allocated slab buffers (Bevy
     * `slabs_size`). */
    std::uint64_t slabs_size() const noexcept {
        std::uint64_t total = 0;
        for (const auto& [id, slab] : slabs) {
            (void)id;
            if (const auto* general = slab.general()) {
                total += general->buffer ? general->buffer.getSize() : 0;
            } else if (const auto* large = slab.large()) {
                total += large->buffer ? large->buffer.getSize() : 0;
            }
        }
        return total;
    }

   private:
    void remove_empty_slab(SlabId slab_id) {
        auto it = slabs.find(slab_id);
        if (it == slabs.end()) return;
        if (const auto* general = it->second.general()) {
            auto lit = slab_layouts.find(general->element_layout);
            if (lit != slab_layouts.end()) {
                auto& ids = lit->second;
                ids.erase(std::remove(ids.begin(), ids.end(), slab_id), ids.end());
            }
        }
        slabs.erase(it);
    }
};
}  // namespace epix::mesh
