#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/render/as_bind_group.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bindless.hpp>
#include <epix/render/render_resource.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>
#endif

#include <epix/pbr/material_bindings.hpp>

template <>
struct std::hash<epix::render::render_resource::BindlessResourceType> {
    /** @brief Bevy derives `Hash` on the resource type; the slab's sampler and texture
     *  arrays are keyed by it. Declared before those maps, since a specialization has to
     *  precede the first use that instantiates it. */
    std::size_t operator()(const epix::render::render_resource::BindlessResourceType& type) const noexcept {
        return std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(type));
    }
};

namespace epix::pbr {

/** @brief The interface to a slab - a single bind group - when it is not bindless
 *  (Bevy `MaterialNonBindlessSlab`, `material_bind_groups.rs:350-355`).
 *
 *  Bevy's variant carries a reference to the prepared bind group or nothing at all;
 *  C++ spells an optional reference as a variant with `std::monostate`. */
EPIX_EXPORT using MaterialNonBindlessSlab =
    std::variant<std::reference_wrapper<const render::render_resource::PreparedBindGroup>, std::monostate>;

/** @brief A single bind group that a `MaterialBindGroupNonBindlessAllocator` is
 *  managing (Bevy `MaterialNonBindlessAllocatedBindGroup`,
 *  `material_bind_groups.rs:192-212`). */
EPIX_EXPORT struct MaterialNonBindlessAllocatedBindGroup {
    /** @brief An unprepared bind group and its layout, waiting for
     *  `prepare_bind_groups`. */
    struct Unprepared {
        render::render_resource::UnpreparedBindGroup bind_group;
        /** @brief The layout of that bind group. */
        render::BindGroupLayoutDescriptor layout;
    };

    /** @brief A bind group that is already prepared. */
    struct Prepared {
        render::render_resource::PreparedBindGroup bind_group;
        /** @brief The uniform buffers the bind group's `Data` entries live in. Bevy
         *  keeps them because only the bind group refers to them. */
        std::vector<wgpu::Buffer> uniform_buffers;
    };

    std::variant<Unprepared, Prepared> value;
};

/** @brief Manages the bind groups of one non-bindless material (Bevy
 *  `MaterialBindGroupNonBindlessAllocator`, `material_bind_groups.rs:177-190`).
 *
 *  Non-bindless materials get a whole bind group each, always at slot 0, so this is
 *  index bookkeeping over a growable list: freed indices are reused before the list
 *  grows, and an unprepared allocation is remembered until it is prepared.
 *
 *  The methods are defined here rather than in a source file because the port's
 *  per-module source list has to be edited for a new `.cpp` to be linked - a
 *  reconfigure alone did not pick one up - and this allocator is a single unit of
 *  bookkeeping. Bevy's `prepare_bind_groups`, the half that needs the device and
 *  packs `Data` into uniform buffers, is not ported yet; it is recorded against
 *  PBR-45, together with the two dispatch types that select between this allocator
 *  and the bindless one (`MaterialBindGroupAllocator` and
 *  `MaterialBindGroupAllocators`). */
EPIX_EXPORT class MaterialBindGroupNonBindlessAllocator {
   public:
    /** @brief Creates an allocator managing the bind groups of one material type
     *  (Bevy `MaterialBindGroupNonBindlessAllocator::new`). */
    explicit MaterialBindGroupNonBindlessAllocator(std::string_view label) : m_label(label) {}

    /** @brief Insert a bind group, prepared or not, and get its binding id (Bevy
     *  `allocate`, `material_bind_groups.rs:1833-1856`). The id is the reused free
     *  index when there is one, so an allocation after a free does not grow the
     *  list. */
    MaterialBindingId allocate(MaterialNonBindlessAllocatedBindGroup bind_group) {
        // Bevy reuses a freed index before growing, then grows the list to cover the
        // index either way.
        MaterialBindGroupIndex group_id{static_cast<std::uint32_t>(m_bind_groups.size())};
        if (!m_free_indices.empty()) {
            group_id = m_free_indices.back();
            m_free_indices.pop_back();
        }

        if (m_bind_groups.size() < static_cast<std::size_t>(group_id.index) + 1) {
            m_bind_groups.resize(static_cast<std::size_t>(group_id.index) + 1, std::nullopt);
        }

        // Only an unprepared group needs preparing; a prepared one is already usable.
        if (std::holds_alternative<MaterialNonBindlessAllocatedBindGroup::Unprepared>(bind_group.value)) {
            m_to_prepare.insert(group_id);
        }

        m_bind_groups[group_id.index] = std::move(bind_group);

        // Bevy returns `slot: default()`, and a non-bindless material's slot is 0.
        return MaterialBindingId{.group = group_id, .slot = MaterialBindGroupSlot{}};
    }

    /** @brief Insert an unprepared bind group (Bevy `allocate_unprepared`). */
    MaterialBindingId allocate_unprepared(render::render_resource::UnpreparedBindGroup unprepared_bind_group,
                                         render::BindGroupLayoutDescriptor bind_group_layout) {
        return allocate(MaterialNonBindlessAllocatedBindGroup{
            .value = MaterialNonBindlessAllocatedBindGroup::Unprepared{.bind_group = std::move(unprepared_bind_group),
                                                                      .layout = std::move(bind_group_layout)}});
    }

    /** @brief Insert a prepared bind group (Bevy `allocate_prepared`). */
    MaterialBindingId allocate_prepared(render::render_resource::PreparedBindGroup prepared_bind_group) {
        return allocate(MaterialNonBindlessAllocatedBindGroup{
            .value = MaterialNonBindlessAllocatedBindGroup::Prepared{.bind_group = std::move(prepared_bind_group),
                                                                     .uniform_buffers = {}}});
    }

    /** @brief Deallocate a bind group and return its index to the free list (Bevy
     *  `free`, `material_bind_groups.rs:1881-1887`). */
    void free(MaterialBindingId binding_id) {
        // Bevy's two `debug_assert`s: the slot is always 0 non-bindless, and the group
        // must actually hold something. `NDEBUG` matches Bevy's `debug_assert` in this
        // preset, as elsewhere in the port.
        assert(binding_id.slot.slot == 0);
        assert(m_bind_groups.at(binding_id.group.index).has_value());

        m_bind_groups[binding_id.group.index] = std::nullopt;
        // A freed group is no longer waiting to be prepared, or it would be prepared
        // after its allocation was gone.
        m_to_prepare.erase(binding_id.group);
        m_free_indices.push_back(binding_id.group);
    }

    /** @brief The slab for a group index, or nothing when it holds no bind group
     *  (Bevy `get`, `material_bind_groups.rs:1890-1901`). */
    std::optional<MaterialNonBindlessSlab> get(MaterialBindGroupIndex group) const {
        const auto& slot = m_bind_groups.at(group.index);
        if (!slot) return std::nullopt;

        if (const auto* prepared = std::get_if<MaterialNonBindlessAllocatedBindGroup::Prepared>(&slot->value)) {
            return MaterialNonBindlessSlab{std::cref(prepared->bind_group)};
        }
        return MaterialNonBindlessSlab{std::monostate{}};
    }

    /** @brief The label the allocator creates its bind groups under (Bevy's
     *  `label`), which `prepare_bind_groups` passes to the device. */
    std::string_view label() const noexcept { return m_label; }

    /** @brief The group indices holding an unprepared bind group, which
     *  `prepare_bind_groups` would take (Bevy's `to_prepare`). */
    const std::unordered_set<MaterialBindGroupIndex>& to_prepare() const noexcept { return m_to_prepare; }

    /** @brief The indices available for reuse (Bevy's `free_indices`). */
    const std::vector<MaterialBindGroupIndex>& free_indices() const noexcept { return m_free_indices; }

    /** @brief How many groups the allocator's list covers (Bevy's `bind_groups`
     *  length). */
    std::size_t len() const noexcept { return m_bind_groups.size(); }

    /** @brief Prepare every bind group still waiting: pack each binding's `OwnedData`
     *  into a uniform buffer, then create the bind group through the layout the
     *  pipeline server holds (Bevy `prepare_bind_groups`,
     *  `material_bind_groups.rs:1909-1975`).
     *
     *  Bevy takes only the device, because `RenderDevice::create_buffer_with_data`
     *  uploads through a mapped-at-creation view the native wgpu C API does not expose
     *  (as this port's buffer types note), so the queue comes in here and the data is
     *  written through it. */
    void prepare_bind_groups(const wgpu::Device& device,
                             const wgpu::Queue& queue,
                             const render::PipelineServer& pipeline_server) {
        // Bevy's `mem::take`: the set is emptied first, so a group freed while this
        // runs cannot be prepared after its allocation is gone.
        const auto to_prepare = std::exchange(m_to_prepare, {});
        for (const MaterialBindGroupIndex bind_group_index : to_prepare) {
            auto& slot = m_bind_groups.at(bind_group_index.index);
            assert(slot.has_value() &&
                   std::holds_alternative<MaterialNonBindlessAllocatedBindGroup::Unprepared>(slot->value));

            auto unprepared = std::get<MaterialNonBindlessAllocatedBindGroup::Unprepared>(std::move(slot->value));
            slot            = std::nullopt;

            // Pack any `Data` into uniform buffers, in binding order.
            std::vector<wgpu::Buffer> uniform_buffers;
            for (const auto& [binding, resource] : unprepared.bind_group.bindings.resources()) {
                const auto* owned_data = std::get_if<render::render_resource::OwnedData>(&resource);
                if (!owned_data) continue;

                const std::string label = std::format("material uniform data {}", binding);
                wgpu::Buffer uniform_buffer =
                    device.createBuffer(wgpu::BufferDescriptor()
                                            .setLabel(label.c_str())
                                            .setSize(owned_data->bytes().size())
                                            .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));
                queue.writeBuffer(uniform_buffer, 0, owned_data->bytes().data(),
                                  static_cast<std::uint64_t>(owned_data->bytes().size()));
                uniform_buffers.push_back(std::move(uniform_buffer));
            }

            // Create the bind group entries, pairing each `Data` binding with the
            // buffer created for it above.
            std::vector<wgpu::BindGroupEntry> entries;
            entries.reserve(unprepared.bind_group.bindings.resources().size());
            auto uniform_buffers_iter = uniform_buffers.begin();
            for (const auto& [binding, resource] : unprepared.bind_group.bindings.resources()) {
                if (std::holds_alternative<render::render_resource::OwnedData>(resource)) {
                    assert(uniform_buffers_iter != uniform_buffers.end());
                    entries.push_back(wgpu::BindGroupEntry()
                                          .setBinding(binding)
                                          .setBuffer(*uniform_buffers_iter)
                                          .setOffset(0)
                                          .setSize(std::numeric_limits<std::uint64_t>::max()));
                    ++uniform_buffers_iter;
                } else if (const auto* buffer = std::get_if<wgpu::Buffer>(&resource)) {
                    entries.push_back(wgpu::BindGroupEntry()
                                          .setBinding(binding)
                                          .setBuffer(*buffer)
                                          .setOffset(0)
                                          .setSize(std::numeric_limits<std::uint64_t>::max()));
                } else if (const auto* texture =
                               std::get_if<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>(&resource)) {
                    entries.push_back(wgpu::BindGroupEntry().setBinding(binding).setTextureView(texture->second));
                } else {
                    const auto* sampler = std::get_if<std::pair<wgpu::SamplerBindingType, wgpu::Sampler>>(&resource);
                    assert(sampler != nullptr);
                    entries.push_back(wgpu::BindGroupEntry().setBinding(binding).setSampler(sampler->second));
                }
            }

            // Create the bind group through the layout the pipeline server holds.
            const wgpu::BindGroup bind_group =
                device.createBindGroup(wgpu::BindGroupDescriptor()
                                           .setLabel(std::string(m_label).c_str())
                                           .setLayout(pipeline_server.get_bind_group_layout(unprepared.layout))
                                           .setEntries(entries));

            m_bind_groups[bind_group_index.index] =
                MaterialNonBindlessAllocatedBindGroup{.value = MaterialNonBindlessAllocatedBindGroup::Prepared{
                                                          .bind_group      = render::render_resource::PreparedBindGroup{
                                                              .bind_group = std::move(bind_group),
                                                              .bindings   = std::move(unprepared.bind_group.bindings)},
                                                          .uniform_buffers = std::move(uniform_buffers)}};
        }
    }

   private:
    std::string_view m_label;
    std::vector<std::optional<MaterialNonBindlessAllocatedBindGroup>> m_bind_groups;
    std::unordered_set<MaterialBindGroupIndex> m_to_prepare;
    std::vector<MaterialBindGroupIndex> m_free_indices;
};

}  // namespace epix::pbr

namespace epix::pbr {

/** @brief Whether a retained buffer still has to be uploaded (Bevy
 *  `BufferDirtyState`, `material_bind_groups.rs:297-304`). */
EPIX_EXPORT enum class BufferDirtyState {
    /** @brief The buffer is synchronized between the CPU and GPU. */
    Clean,
    /** @brief The buffer has not been created yet. */
    NeedsReserve,
    /** @brief The buffer exists on both sides, but the GPU's copy is out of date. */
    NeedsUpload,
};

/** @brief The size of the buffer assigned to unused bindless buffer slots, in bytes
 *  (Bevy `DEFAULT_BINDLESS_FALLBACK_BUFFER_SIZE`, `material_bind_groups.rs:399`).
 *
 *  Bevy calls the value essentially arbitrary, and keeps it only because a binding
 *  needs a buffer of some size. */
EPIX_EXPORT inline constexpr std::uint64_t DEFAULT_BINDLESS_FALLBACK_BUFFER_SIZE = 16;

/** @brief Plain old data packed for the GPU, with the lazy-update state (Bevy
 *  `RetainedRawBufferVec<T>`, `material_bind_groups.rs:384-393`).
 *
 *  Bevy derives `Deref`, so the inner buffer's methods read as the wrapper's own; C++
 *  has no deref, so the ones its callers use are forwarded here. */
EPIX_EXPORT template <typename T>
struct RetainedRawBufferVec {
    /** @brief The contents of the buffer. */
    render::render_resource::RawBufferVec<T> buffer;
    /** @brief Whether the contents have been uploaded to the GPU. */
    BufferDirtyState dirty = BufferDirtyState::NeedsUpload;

    /** @brief A new empty buffer with the given usage (Bevy `new`,
     *  `material_bind_groups.rs:736-741`). It starts as `NeedsUpload`: there is nothing
     *  to reserve, but the (empty) contents have not been written either. */
    explicit RetainedRawBufferVec(wgpu::BufferUsage usage) : buffer(usage) {}

    void push(T value) { buffer.push(std::move(value)); }
    void set(std::size_t index, T value) { buffer.set(index, std::move(value)); }
    [[nodiscard]] std::size_t len() const noexcept { return buffer.len(); }
    [[nodiscard]] std::span<const T> values() const noexcept { return buffer.values(); }
    [[nodiscard]] std::span<T> values_mut() noexcept { return buffer.values_mut(); }
    /** @brief The GPU buffer, once it exists (Bevy's deref to `RawBufferVec::buffer`). */
    [[nodiscard]] const wgpu::Buffer* gpu_buffer() const noexcept { return buffer.buffer(); }

    /** @brief Recreate the GPU backing buffer if its capacity no longer covers the
     *  contents (Bevy `prepare`, `:744-753`). */
    void prepare(const wgpu::Device& device) {
        if (dirty != BufferDirtyState::NeedsReserve) return;
        buffer.reserve(buffer.len(), device);
        dirty = BufferDirtyState::NeedsUpload;
    }

    /** @brief Write the contents to the GPU if they are not already there (Bevy `write`,
     *  `:756-764`). Both dirty states write: a resize leaves the contents to be uploaded
     *  as well. */
    void write(const wgpu::Device& device, const wgpu::Queue& queue) {
        if (dirty == BufferDirtyState::Clean) return;
        buffer.write_buffer(device, queue);
        dirty = BufferDirtyState::Clean;
    }
};

/** @brief Untyped plain old data on the GPU, allocated in slots (Bevy
 *  `MaterialDataBuffer`, `material_bind_groups.rs:361-376`), which is what the `#[data]`
 *  attribute of `AsBindGroup` needs: the data is not a GPU resource of its own, so the
 *  allocator has to place it in a buffer and hand the shader a slot. */
EPIX_EXPORT struct MaterialDataBuffer {
    /** @brief The binding the buffer is attached to. */
    render::render_resource::BindingNumber binding_number{};
    /** @brief The data, untyped: `aligned_element_size` gives the element size. */
    RetainedRawBufferVec<std::uint8_t> buffer;
    /** @brief The size of one element, padding and alignment included. */
    std::uint32_t aligned_element_size = 0;
    /** @brief The slots within the buffer that are free. */
    std::vector<std::uint32_t> free_slots;
    /** @brief How many slots are allocated. */
    std::uint32_t len = 0;

    explicit MaterialDataBuffer(render::render_resource::BindingNumber binding, std::uint32_t aligned_size)
        : binding_number(binding),
          buffer(wgpu::BufferUsage::eStorage),
          aligned_element_size(aligned_size) {}

    /** @brief Allocate a slot, copy @p data into it and return the slot (Bevy `insert`,
     *  `material_bind_groups.rs:2017-2043`). @p data must be exactly
     *  `aligned_element_size` bytes, as Bevy's `debug_assert` requires.
     *
     *  The slot is the last freed one when there is one, so a removed element's space is
     *  reused before the buffer grows - the same rule the material allocators above
     *  follow for their group indices. */
    std::uint32_t insert(std::span<const std::uint8_t> data) {
        assert(data.size() == aligned_element_size);

        std::uint32_t slot = len;
        if (!free_slots.empty()) {
            slot = free_slots.back();
            free_slots.pop_back();
        }

        const std::size_t start = static_cast<std::size_t>(slot) * aligned_element_size;
        const std::size_t end   = (static_cast<std::size_t>(slot) + 1) * aligned_element_size;

        // Grow to cover the slot. Bevy calls `reserve_internal` first as an
        // optimization; the push loop that follows is what actually guarantees the
        // length, and this port's buffer has no separate reserve step.
        while (buffer.values().size() < end) buffer.push(0);
        std::copy(data.begin(), data.end(), buffer.values_mut().begin() + static_cast<std::ptrdiff_t>(start));

        ++len;
        buffer.dirty = BufferDirtyState::NeedsReserve;
        return slot;
    }

    /** @brief Mark a slot free again (Bevy `remove`, `material_bind_groups.rs:2046-2049`). */
    void remove(std::uint32_t slot) {
        free_slots.push_back(slot);
        --len;
    }
};

/** @brief A GPU-accessible buffer mapping bindless indices to binding-array slots (Bevy
 *  `MaterialBindlessIndexTable`, `material_bind_groups.rs:122-135`).
 *
 *  Each material gets one slice of the table, of `index_range.length()` entries, and its
 *  entries are sorted by bindless index because the shader binary-searches them. The
 *  table is conventionally bound to binding 0, which the descriptor can move. */
EPIX_EXPORT struct MaterialBindlessIndexTable {
    /** @brief The mappings, one slice per material. */
    RetainedRawBufferVec<std::uint32_t> buffer;
    /** @brief The bindless indices this table covers: if it is M..N, entry *i* of a
     *  slice holds the slot for bindless index *i* + M. */
    render::render_resource::BindlessIndexRange index_range{};
    /** @brief The binding the table itself is bound at. */
    render::render_resource::BindingNumber binding_number{};

    /** @brief A table for one slab, preallocated so a buffer always exists (Bevy `new`,
     *  `:639-655`). */
    explicit MaterialBindlessIndexTable(const render::render_resource::BindlessIndexTableDescriptor& descriptor)
        : buffer(wgpu::BufferUsage::eStorage),
          index_range(descriptor.indices),
          binding_number(descriptor.binding_number) {
        for (std::uint32_t index = index_range.start.index; index < index_range.end.index; ++index) buffer.push(0);
    }

    /** @brief The slice belonging to one material (Bevy `get`, `:662-666`). */
    [[nodiscard]] std::span<const std::uint32_t> get(MaterialBindGroupSlot slot) const {
        const std::size_t struct_size = index_range.length();
        return buffer.values().subspan(struct_size * slot.slot, struct_size);
    }

    /** @brief One entry of a material's slice, or nothing when the index is outside the
     *  table (Bevy `get_binding`, `:669-680`). */
    [[nodiscard]] std::optional<std::uint32_t> get_binding(MaterialBindGroupSlot slot,
                                                           render::render_resource::BindlessIndex bindless_index) const {
        if (!index_range.contains(bindless_index)) return std::nullopt;
        return get(slot)[bindless_index.index - index_range.start.index];
    }

    /** @brief How many bindless indices the table covers (Bevy `table_length`, `:682-684`). */
    [[nodiscard]] std::uint32_t table_length() const noexcept { return index_range.length(); }

    /** @brief Serialize one material's bindless index to slot mapping into its slice, so
     *  the shader can read it (Bevy `set`, `:693-715`). */
    void set(MaterialBindGroupSlot slot,
             const std::unordered_map<render::render_resource::BindlessIndex, std::uint32_t>& allocated_resource_slots) {
        const std::size_t table_len = table_length();
        const std::size_t range_start = static_cast<std::size_t>(slot.slot) * table_len;
        const std::size_t range_end   = (static_cast<std::size_t>(slot.slot) + 1) * table_len;
        while (buffer.values().size() < range_end) buffer.push(0);

        for (const auto& [bindless_index, resource_slot] : allocated_resource_slots) {
            if (!index_range.contains(bindless_index)) continue;
            buffer.set(range_start + bindless_index.index - index_range.start.index, resource_slot);
        }

        // The buffer may have grown, so it has to be recreated before it is written.
        buffer.dirty = BufferDirtyState::NeedsReserve;
    }

    /** @brief The bind group entry for the table itself (Bevy `bind_group_entry`,
     *  `:718-727`). Bevy expects the buffer to exist and panics otherwise; a table always
     *  has one because `new` preallocates. */
    [[nodiscard]] wgpu::BindGroupEntry bind_group_entry() const {
        const wgpu::Buffer* gpu_buffer = buffer.gpu_buffer();
        assert(gpu_buffer != nullptr);
        return wgpu::BindGroupEntry()
            .setBinding(binding_number.number)
            .setBuffer(*gpu_buffer)
            .setOffset(0)
            .setSize(std::numeric_limits<std::uint64_t>::max());
    }
};

/** @brief A single resource in a binding array (Bevy `MaterialBindlessBinding<R>`,
 *  `material_bind_groups.rs:165-174`): the sampler, texture or buffer itself, plus how
 *  many materials in the slab refer to it. The reference count is what lets several
 *  materials share one array slot. */
EPIX_EXPORT template <typename R>
struct MaterialBindlessBinding {
    /** @brief The resource. */
    R resource;
    /** @brief How many materials in the slab use it. */
    std::uint32_t ref_count = 0;
};

/** @brief The identity of one resource, which is how a binding array finds the slot it
 *  has already placed that resource in (Bevy `BindingResourceId`,
 *  `material_bind_groups.rs:228-240`).
 *
 *  Bevy keys on wgpu's own resource ids; this port keys on the handles, which compare
 *  and hash by their raw pointer, and the variant's index keeps two kinds that happen to
 *  share a pointer apart. The texture dimension is part of the identity, as in Bevy: the
 *  same view bound as a different dimension is a different binding. */
EPIX_EXPORT struct BindingResourceId {
    /** @brief A buffer. */
    struct Buffer {
        wgpu::Buffer buffer;
        bool operator==(const Buffer&) const = default;
    };
    /** @brief A texture view, with the dimension it is bound as. */
    struct TextureView {
        wgpu::TextureViewDimension dimension = wgpu::TextureViewDimension::e2D;
        wgpu::TextureView view;
        bool operator==(const TextureView&) const = default;
    };
    /** @brief A sampler. */
    struct Sampler {
        wgpu::Sampler sampler;
        bool operator==(const Sampler&) const = default;
    };
    /** @brief Plain old data, which has no resource of its own until the allocator
     *  creates one for it. */
    struct DataBuffer {
        bool operator==(const DataBuffer&) const = default;
    };

    std::variant<Buffer, TextureView, Sampler, DataBuffer> value;

    bool operator==(const BindingResourceId&) const = default;
};

}  // namespace epix::pbr

template <>
struct std::hash<epix::pbr::BindingResourceId> {
    /** @brief Hashes the handle by its raw pointer, with the resource kind mixed in so
     *  that two kinds sharing a pointer stay distinct. */
    std::size_t operator()(const epix::pbr::BindingResourceId& id) const noexcept {
        std::size_t seed = id.value.index();
        const auto mix   = [&seed](std::uintptr_t raw) {
            seed ^= std::hash<std::uintptr_t>{}(raw) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };

        std::visit(
            [&mix](const auto& resource) {
                using Resource = std::decay_t<decltype(resource)>;
                if constexpr (std::is_same_v<Resource, epix::pbr::BindingResourceId::Buffer>) {
                    mix(reinterpret_cast<std::uintptr_t>(resource.buffer.raw()));
                } else if constexpr (std::is_same_v<Resource, epix::pbr::BindingResourceId::TextureView>) {
                    mix(reinterpret_cast<std::uintptr_t>(resource.view.raw()));
                    mix(static_cast<std::uintptr_t>(resource.dimension));
                } else if constexpr (std::is_same_v<Resource, epix::pbr::BindingResourceId::Sampler>) {
                    mix(reinterpret_cast<std::uintptr_t>(resource.sampler.raw()));
                } else {
                    mix(0);
                }
            },
            id.value);
        return seed;
    }
};

namespace epix::pbr {

/** @brief The identity of a buffer as a bindless resource (Bevy's `GetBindingResourceId`
 *  for `Buffer`, `material_bind_groups.rs:428-433`): plain old data is its own kind, since
 *  it has no GPU resource of its own. */
[[nodiscard]] inline BindingResourceId binding_resource_id(const wgpu::Buffer& buffer,
                                                          render::render_resource::BindlessResourceType resource_type) {
    if (resource_type == render::render_resource::BindlessResourceType::DataBuffer) {
        return BindingResourceId{BindingResourceId::DataBuffer{}};
    }
    return BindingResourceId{BindingResourceId::Buffer{.buffer = buffer}};
}

/** @brief The identity of a texture view (Bevy's `GetBindingResourceId` for
 *  `TextureView`, `:440-445`).
 *
 *  Bevy reads the view's dimension from its own wrapper; this port's `wgpu::TextureView`
 *  has no such accessor, so the dimension travels with the view - the same pairing
 *  `OwnedBindingResource` uses. */
[[nodiscard]] inline BindingResourceId
binding_resource_id(const std::pair<wgpu::TextureViewDimension, wgpu::TextureView>& texture,
                    render::render_resource::BindlessResourceType) {
    return BindingResourceId{
        BindingResourceId::TextureView{.dimension = texture.first, .view = texture.second}};
}

/** @brief The identity of a sampler (Bevy's `GetBindingResourceId` for `Sampler`,
 *  `:434-439`). */
[[nodiscard]] inline BindingResourceId binding_resource_id(const wgpu::Sampler& sampler,
                                                          render::render_resource::BindlessResourceType) {
    return BindingResourceId{BindingResourceId::Sampler{.sampler = sampler}};
}

/** @brief One binding array of a slab (Bevy `MaterialBindlessBindingArray<R>`,
 *  `material_bind_groups.rs:139-158`): the resources in their slots, the inverse mapping
 *  from resource identity to slot, the free slots, and how many are allocated.
 *
 *  The inverse mapping is what makes the array share slots: a resource already present
 *  is found by identity and its reference count raised rather than a second slot being
 *  taken. */
EPIX_EXPORT template <typename R>
struct MaterialBindlessBindingArray {
    /** @brief The binding the array is attached to. */
    render::render_resource::BindingNumber binding_number{};
    /** @brief Slot index to the resource in it, if any. */
    std::vector<std::optional<MaterialBindlessBinding<R>>> bindings;
    /** @brief The kind of resource stored here. */
    render::render_resource::BindlessResourceType resource_type = render::render_resource::BindlessResourceType::None;
    /** @brief Resource identity to the slot holding it: the inverse of `bindings`. */
    std::unordered_map<BindingResourceId, std::uint32_t> resource_to_slot;
    /** @brief The slots that hold nothing. */
    std::vector<std::uint32_t> free_slots;
    /** @brief How many slots are allocated. */
    std::uint32_t len = 0;

    /** @brief An empty array of one resource kind at a binding (Bevy `new`,
     *  `material_bind_groups.rs:1584-1596`). */
    static MaterialBindlessBindingArray make(render::render_resource::BindingNumber binding_number,
                                             render::render_resource::BindlessResourceType resource_type) {
        MaterialBindlessBindingArray array;
        array.binding_number = binding_number;
        array.resource_type  = resource_type;
        return array;
    }

    /** @brief The slot holding @p id, or nothing when the array does not hold it (Bevy
     *  `find`, `:1602-1604`). */
    [[nodiscard]] std::optional<std::uint32_t> find(const BindingResourceId& id) const {
        const auto found = resource_to_slot.find(id);
        if (found == resource_to_slot.end()) return std::nullopt;
        return found->second;
    }

    /** @brief Add a resource, or count one more user of one already present (Bevy
     *  `insert`, `:1608-1633`).
     *
     *  The second case is what makes slots shared: two materials using the same texture
     *  occupy one slot in the array rather than two, which is the whole point of a
     *  bindless array. */
    std::uint32_t insert(const BindingResourceId& id, R resource) {
        if (const auto found = resource_to_slot.find(id); found != resource_to_slot.end()) {
            const std::uint32_t slot = found->second;
            assert(bindings.at(slot).has_value());
            bindings.at(slot)->ref_count += 1;
            return slot;
        }

        std::uint32_t slot = len;
        if (!free_slots.empty()) {
            slot = free_slots.back();
            free_slots.pop_back();
        }
        resource_to_slot.emplace(id, slot);

        if (bindings.size() < static_cast<std::size_t>(slot) + 1) bindings.resize(static_cast<std::size_t>(slot) + 1);
        bindings.at(slot) = MaterialBindlessBinding<R>{.resource = std::move(resource), .ref_count = 1};

        ++len;
        return slot;
    }

    /** @brief Drop one reference to a slot's resource, freeing the slot when the last one
     *  goes (Bevy `remove`, `:1640-1658`). Returns whether the slot was freed.
     *
     *  The identity is rebuilt from the stored resource, which is why the caller's
     *  resource type comes in: a `DataBuffer` slot has no GPU resource of its own. */
    bool remove(std::uint32_t slot) {
        assert(bindings.at(slot).has_value());
        auto& binding = *bindings.at(slot);

        binding.ref_count -= 1;
        if (binding.ref_count != 0) return false;

        resource_to_slot.erase(binding_resource_id(binding.resource, resource_type));
        bindings.at(slot) = std::nullopt;
        free_slots.push_back(slot);
        --len;
        return true;
    }
};

/** @brief Whether a slab could hold a material, and what that would cost (Bevy
 *  `BindlessAllocationCandidate`, `material_bind_groups.rs:308-330`): which of the
 *  material's bindless indices already have a slot in the slab, and how many free slots
 *  the rest would need. */
EPIX_EXPORT struct BindlessAllocationCandidate {
    /** @brief Bindless index to the slot already holding that resource here. */
    std::unordered_map<render::render_resource::BindlessIndex, std::uint32_t> pre_existing_resources;
    /** @brief How many slots the resources not already present would need. */
    std::uint32_t needed_free_slots = 0;
};

/** @brief Dummy resources that fill the slots of a binding array no material uses (Bevy
 *  `FallbackBindlessResources`, `material_bind_groups.rs:216-224`).
 *
 *  A binding array is declared with as many slots as the platform's limit allows, and a
 *  shader may index any of them, so the unused slots still have to hold something valid -
 *  otherwise a material that indexes a slot it does not itself occupy reads garbage. Bevy
 *  keeps one sampler of each kind for that, and one fallback buffer per bindless buffer
 *  (the allocator's `fallback_buffers`). */
EPIX_EXPORT struct FallbackBindlessResources {
    /** @brief A dummy filtering sampler. */
    wgpu::Sampler filtering_sampler;
    /** @brief A dummy non-filtering sampler: nearest on all three filters. */
    wgpu::Sampler non_filtering_sampler;
    /** @brief A dummy comparison sampler, comparing with `Always` so it never rejects. */
    wgpu::Sampler comparison_sampler;
};

/** @brief Create the fallback bindless resources on the render device (Bevy
 *  `init_fallback_bindless_resources`, `material_bind_groups.rs:1795-1814`, registered in
 *  the same list as `init_shadow_samplers`, `bevy_pbr/src/lib.rs:319`).
 *
 *  Defined in this header rather than a source file to keep the type and its initializer
 *  together: the port's module sources are listed rather than globbed, so a new `.cpp`
 *  would not be linked. */
inline void init_fallback_bindless_resources(ecs::Commands commands, ecs::Res<wgpu::Device> device) {
    const wgpu::Device& render_device = device.get();

    // Written as separate setters on named descriptors: this wrapper's sampler setters are
    // ref-qualified on their member, so chaining them off a temporary does not compile.
    wgpu::SamplerDescriptor filtering;
    filtering.setLabel("fallback filtering sampler");

    wgpu::SamplerDescriptor non_filtering;
    non_filtering.setLabel("fallback non-filtering sampler");
    non_filtering.setMagFilter(wgpu::FilterMode::eNearest);
    non_filtering.setMinFilter(wgpu::FilterMode::eNearest);
    non_filtering.setMipmapFilter(wgpu::MipmapFilterMode::eNearest);

    wgpu::SamplerDescriptor comparison;
    comparison.setLabel("fallback comparison sampler");
    comparison.setCompare(wgpu::CompareFunction::eAlways);

    commands.insert_resource(FallbackBindlessResources{
        .filtering_sampler     = render_device.createSampler(filtering),
        .non_filtering_sampler = render_device.createSampler(non_filtering),
        .comparison_sampler    = render_device.createSampler(comparison),
    });
}

/** @brief One bind group of a bindless allocator, with the bookkeeping needed to
 *  allocate into it (Bevy `MaterialBindlessSlab`, `material_bind_groups.rs:80-114`). */
EPIX_EXPORT struct MaterialBindlessSlab {
    /** @brief The bind group, or nothing when it is dirty and has to be regenerated. */
    std::optional<wgpu::BindGroup> bind_group;
    /** @brief The buffers mapping bindless index to slot. The shader binary-searches
     *  them, so their entries have to stay sorted by bindless index. */
    std::vector<MaterialBindlessIndexTable> bindless_index_tables;
    /** @brief The sampler binding arrays, by resource type. */
    std::unordered_map<render::render_resource::BindlessResourceType, MaterialBindlessBindingArray<wgpu::Sampler>>
        samplers;
    /** @brief The texture binding arrays, by resource type. Bevy stores the view alone
     *  and reads its dimension when it needs the resource's identity; this port's
     *  `wgpu::TextureView` has no dimension accessor, so the pairing
     *  `OwnedBindingResource` uses travels here instead. */
    std::unordered_map<render::render_resource::BindlessResourceType,
                       MaterialBindlessBindingArray<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>>
        textures;
    /** @brief The buffer binding arrays, by bindless index. */
    std::unordered_map<render::render_resource::BindlessIndex, MaterialBindlessBindingArray<wgpu::Buffer>> buffers;
    /** @brief The buffers holding plain old data, by bindless index. */
    std::unordered_map<render::render_resource::BindlessIndex, MaterialDataBuffer> data_buffers;
    /** @brief The slot ids that are free. */
    std::vector<MaterialBindGroupSlot> free_slots;
    /** @brief How many materials are allocated in this slab. */
    std::uint32_t live_allocation_count = 0;
    /** @brief How many resources are allocated across its binding arrays. */
    std::uint32_t allocated_resource_count = 0;

    /** @brief Build a slab for a material with the given bindless descriptor (Bevy
     *  `MaterialBindlessSlab::new`, `material_bind_groups.rs:1695-1792`).
     *
     *  Every bindless resource in the descriptor gets the array it belongs in: keyed by
     *  resource type for samplers and textures, and by bindless index for buffers and
     *  data buffers, since buffers are not interchangeable with one another and so cannot
     *  share an array. The index tables come from the descriptor's own list. */
    static MaterialBindlessSlab make(const render::render_resource::BindlessDescriptor& descriptor) {
        using render::render_resource::BindlessIndex;
        using render::render_resource::BindlessResourceType;

        MaterialBindlessSlab slab;

        for (std::size_t index = 0; index < descriptor.resources.size(); ++index) {
            const BindlessIndex bindless_index{static_cast<std::uint32_t>(index)};
            const BindlessResourceType resource_type = descriptor.resources[index];

            // A buffer's descriptor carries the binding number the array binds at, and for
            // a data buffer the element size as well.
            const auto buffer_descriptor = std::ranges::find_if(
                descriptor.buffers, [bindless_index](const auto& candidate) {
                    return candidate.bindless_index == bindless_index;
                });

            const auto array_binding_number = [resource_type]() -> render::render_resource::BindingNumber {
                const auto* binding_number = render::render_resource::bindless_binding_number(resource_type);
                assert(binding_number != nullptr);
                return *binding_number;
            };

            switch (resource_type) {
                case BindlessResourceType::None: break;
                case BindlessResourceType::Buffer:
                    assert(buffer_descriptor != descriptor.buffers.end());
                    slab.buffers.emplace(bindless_index, MaterialBindlessBindingArray<wgpu::Buffer>::make(
                                                             buffer_descriptor->binding_number, resource_type));
                    break;
                case BindlessResourceType::DataBuffer:
                    assert(buffer_descriptor != descriptor.buffers.end());
                    assert(buffer_descriptor->size.has_value());
                    slab.data_buffers.emplace(
                        bindless_index, MaterialDataBuffer{buffer_descriptor->binding_number,
                                                           static_cast<std::uint32_t>(*buffer_descriptor->size)});
                    break;
                case BindlessResourceType::SamplerFiltering:
                case BindlessResourceType::SamplerNonFiltering:
                case BindlessResourceType::SamplerComparison:
                    slab.samplers.emplace(
                        resource_type,
                        MaterialBindlessBindingArray<wgpu::Sampler>::make(array_binding_number(), resource_type));
                    break;
                case BindlessResourceType::Texture1d:
                case BindlessResourceType::Texture2d:
                case BindlessResourceType::Texture2dArray:
                case BindlessResourceType::Texture3d:
                case BindlessResourceType::TextureCube:
                case BindlessResourceType::TextureCubeArray:
                    slab.textures.emplace(
                        resource_type,
                        MaterialBindlessBindingArray<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>::make(
                            array_binding_number(), resource_type));
                    break;
            }
        }

        for (const auto& table : descriptor.index_tables) slab.bindless_index_tables.emplace_back(table);
        return slab;
    }

    /** @brief Whether this slab could hold @p unprepared_bind_group, and what that would
     *  need (Bevy `check_allocation`, `material_bind_groups.rs:962-1039`).
     *
     *  Nothing when a resource's binding array is missing - Bevy logs and gives up rather
     *  than placing the material somewhere wrong. Plain old data contributes no free slot:
     *  it lives in its own buffer per bindless index rather than in a bounded array, which
     *  is why Bevy calls its size unlimited. */
    [[nodiscard]] std::optional<BindlessAllocationCandidate> check_allocation(
        const render::render_resource::UnpreparedBindGroup& unprepared_bind_group) const {
        using render::render_resource::BindlessIndex;
        using render::render_resource::BindlessResourceType;

        BindlessAllocationCandidate candidate;

        for (const auto& [binding_number, resource] : unprepared_bind_group.bindings.resources()) {
            const BindlessIndex bindless_index{binding_number};

            const auto record = [&candidate, bindless_index](const std::optional<std::uint32_t>& slot) {
                if (slot.has_value()) {
                    candidate.pre_existing_resources.emplace(bindless_index, *slot);
                } else {
                    candidate.needed_free_slots += 1;
                }
            };

            if (const auto* buffer = std::get_if<wgpu::Buffer>(&resource)) {
                const auto array = buffers.find(bindless_index);
                if (array == buffers.end()) {
                    spdlog::error("[pbr] no bindless array for the buffer at index {}", bindless_index.index);
                    return std::nullopt;
                }
                record(array->second.find(binding_resource_id(*buffer, array->second.resource_type)));
                continue;
            }
            if (std::holds_alternative<render::render_resource::OwnedData>(resource)) continue;

            if (const auto* texture =
                    std::get_if<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>(&resource)) {
                const BindlessResourceType resource_type =
                    render::render_resource::bindless_resource_type_from(texture->first);
                const auto array = textures.find(resource_type);
                if (array == textures.end()) {
                    spdlog::error("[pbr] no bindless array for the texture at index {}", bindless_index.index);
                    return std::nullopt;
                }
                record(array->second.find(binding_resource_id(*texture, resource_type)));
                continue;
            }

            const auto* sampler = std::get_if<std::pair<wgpu::SamplerBindingType, wgpu::Sampler>>(&resource);
            assert(sampler != nullptr);
            const BindlessResourceType resource_type =
                render::render_resource::bindless_resource_type_from(sampler->first);
            const auto array = samplers.find(resource_type);
            if (array == samplers.end()) {
                spdlog::error("[pbr] no bindless array for the sampler at index {}", bindless_index.index);
                return std::nullopt;
            }
            record(array->second.find(binding_resource_id(sampler->second, resource_type)));
        }
        return candidate;
    }

    /** @brief Place @p bindings into this slab's arrays and report each bindless index's
     *  slot (Bevy `insert_resources`, `material_bind_groups.rs:1045-1134`).
     *
     *  A resource the candidate found already present has to land in the same slot it was
     *  found in - the assert Bevy makes - and does not count as a newly allocated
     *  resource, which is what keeps `allocated_resource_count` measuring the slab's
     *  occupancy rather than the number of materials. */
    std::unordered_map<render::render_resource::BindlessIndex, std::uint32_t> insert_resources(
        render::render_resource::BindingResources bindings, const BindlessAllocationCandidate& candidate) {
        using render::render_resource::BindlessIndex;
        using render::render_resource::BindlessResourceType;
        using render::render_resource::OwnedData;

        std::unordered_map<BindlessIndex, std::uint32_t> allocated_resource_slots;

        for (auto& [binding_number, resource] : bindings.resources_mut()) {
            const BindlessIndex bindless_index{binding_number};
            const auto pre_existing = candidate.pre_existing_resources.find(bindless_index);

            bool counts_as_new = false;
            std::uint32_t slot = 0;

            if (auto* buffer = std::get_if<wgpu::Buffer>(&resource)) {
                auto array = buffers.find(bindless_index);
                assert(array != buffers.end());
                slot         = array->second.insert(binding_resource_id(*buffer, array->second.resource_type), *buffer);
                if (pre_existing != candidate.pre_existing_resources.end()) {
                    // Bevy's assert: a resource the candidate found in this slab has to be
                    // inserted into the same slot it was found in.
                    assert(pre_existing->second == slot);
                    counts_as_new = false;
                } else {
                    counts_as_new = true;
                }
            } else if (auto* data = std::get_if<OwnedData>(&resource)) {
                assert(pre_existing == candidate.pre_existing_resources.end());
                auto array = data_buffers.find(bindless_index);
                assert(array != data_buffers.end());
                const std::span<const std::byte> bytes = data->bytes();
                slot = array->second.insert(
                    {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
            } else if (auto* texture = std::get_if<std::pair<wgpu::TextureViewDimension, wgpu::TextureView>>(&resource)) {
                const BindlessResourceType resource_type =
                    render::render_resource::bindless_resource_type_from(texture->first);
                auto array = textures.find(resource_type);
                assert(array != textures.end());
                slot          = array->second.insert(binding_resource_id(*texture, resource_type), *texture);
                if (pre_existing != candidate.pre_existing_resources.end()) {
                    // Bevy's assert: a resource the candidate found in this slab has to be
                    // inserted into the same slot it was found in.
                    assert(pre_existing->second == slot);
                    counts_as_new = false;
                } else {
                    counts_as_new = true;
                }
            } else {
                auto* sampler = std::get_if<std::pair<wgpu::SamplerBindingType, wgpu::Sampler>>(&resource);
                assert(sampler != nullptr);
                const BindlessResourceType resource_type =
                    render::render_resource::bindless_resource_type_from(sampler->first);
                auto array = samplers.find(resource_type);
                assert(array != samplers.end());
                slot          = array->second.insert(binding_resource_id(sampler->second, resource_type), sampler->second);
                if (pre_existing != candidate.pre_existing_resources.end()) {
                    // Bevy's assert: a resource the candidate found in this slab has to be
                    // inserted into the same slot it was found in.
                    assert(pre_existing->second == slot);
                    counts_as_new = false;
                } else {
                    counts_as_new = true;
                }
            }

            allocated_resource_slots.emplace(bindless_index, slot);
            if (counts_as_new) allocated_resource_count += 1;
        }

        return allocated_resource_slots;
    }

    /** @brief Try to allocate a material in this slab (Bevy `try_allocate`,
     *  `material_bind_groups.rs:910-958`): the slot it took, or the bind group back when
     *  the slab is full, so the caller can try the next one.
     *
     *  One rule here is a special case rather than an accident: an *empty* slab always
     *  accepts a material, however many resources it needs. That is what lets a platform
     *  whose bindless limit is too low to hold even one material still place each of them
     *  in a slab of its own, instead of failing outright. */
    std::expected<MaterialBindGroupSlot, render::render_resource::UnpreparedBindGroup> try_allocate(
        render::render_resource::UnpreparedBindGroup unprepared_bind_group, std::uint32_t slot_capacity) {
        const auto candidate = check_allocation(unprepared_bind_group);
        if (!candidate.has_value()) return std::unexpected(std::move(unprepared_bind_group));

        if (allocated_resource_count > 0 && allocated_resource_count + candidate->needed_free_slots > slot_capacity) {
            return std::unexpected(std::move(unprepared_bind_group));
        }

        MaterialBindGroupSlot slot{live_allocation_count};
        if (!free_slots.empty()) {
            slot = free_slots.back();
            free_slots.pop_back();
        }
        live_allocation_count += 1;

        const auto allocated_resource_slots =
            insert_resources(std::move(unprepared_bind_group.bindings), *candidate);
        for (auto& table : bindless_index_tables) table.set(slot, allocated_resource_slots);

        // The cached bind group no longer describes this slab's contents.
        bind_group = std::nullopt;
        return slot;
    }

    /** @brief This slab's bind group, once it has been prepared (Bevy
     *  `MaterialBindlessSlab::bind_group`, `material_bind_groups.rs:1553-1555`). Nothing
     *  while the slab is dirty.
     *
     *  Bevy names this method after the field it returns; C++ cannot have both under one
     *  name, so the field keeps Bevy's name and the accessor takes a `get_` prefix. */
    [[nodiscard]] const wgpu::BindGroup* get_bind_group() const {
        return bind_group.has_value() ? &*bind_group : nullptr;
    }

    /** @brief The index table covering @p bindless_index, if one does (Bevy
     *  `get_bindless_index_table`, `:1558-1575`).
     *
     *  The tables are kept sorted by their ranges, which is what makes this a binary
     *  search; an unsorted list would silently answer with the wrong table rather than
     *  fail, since the search only compares ranges. */
    [[nodiscard]] const MaterialBindlessIndexTable* get_bindless_index_table(
        render::render_resource::BindlessIndex bindless_index) const {
        const auto table = std::lower_bound(bindless_index_tables.begin(), bindless_index_tables.end(), bindless_index,
                                           [](const MaterialBindlessIndexTable& candidate,
                                              render::render_resource::BindlessIndex wanted) {
                                               // Bevy's ordering: a table that ends at or before the wanted index
                                               // orders before it, so the search lands on the table *containing*
                                               // the index rather than on the first one that starts after it - which
                                               // is what `contains` below then confirms or rejects for the gaps.
                                               return candidate.index_range.end.index <= wanted.index;
                                           });
        if (table == bindless_index_tables.end() || !table->index_range.contains(bindless_index)) return nullptr;
        return &*table;
    }
};

/** @brief A slab of either kind, as the dispatch hands it out (Bevy `MaterialSlab` and
 *  `MaterialSlabImpl`, `material_bind_groups.rs:334-346`). */
EPIX_EXPORT struct MaterialSlab {
    std::variant<const MaterialBindlessSlab*, MaterialNonBindlessSlab> value;

    /** @brief The bind group, when the slab has one (Bevy `MaterialSlab::bind_group`,
     *  `:1985-1996`). Nothing means the slab is not prepared yet, which is what the draw
     *  path checks before binding anything. */
    [[nodiscard]] const wgpu::BindGroup* bind_group() const {
        if (const auto* bindless = std::get_if<const MaterialBindlessSlab*>(&value)) {
            return *bindless != nullptr ? (*bindless)->get_bind_group() : nullptr;
        }
        const auto& non_bindless = std::get<MaterialNonBindlessSlab>(value);
        if (const auto* prepared =
                std::get_if<std::reference_wrapper<const render::render_resource::PreparedBindGroup>>(&non_bindless)) {
            return &prepared->get().bind_group;
        }
        return nullptr;
    }
};

/** @brief The allocator that places bindless materials into bind groups (Bevy
 *  `MaterialBindGroupBindlessAllocator`, `material_bind_groups.rs:53-77`).
 *
 *  Its allocation logic is not ported yet (PBR-46), so this is the shape the logic will
 *  work on: the slabs, the layout they are built from, the descriptor saying which
 *  resources are bindless, and one fallback buffer per bindless buffer to fill the slots
 *  no material uses. */
EPIX_EXPORT struct MaterialBindGroupBindlessAllocator {
    /** @brief The label its bind groups are created under. */
    std::string_view label;
    /** @brief The slabs, each holding one bind group. */
    std::vector<MaterialBindlessSlab> slabs;
    /** @brief The layout of the bind groups it produces. */
    render::BindGroupLayoutDescriptor bind_group_layout;
    /** @brief Which resources of the material are bindless, and where. */
    render::render_resource::BindlessDescriptor bindless_descriptor;
    /** @brief One dummy buffer per bindless buffer, sized for one element, filling the
     *  slots no material uses. */
    std::unordered_map<render::render_resource::BindlessIndex, wgpu::Buffer> fallback_buffers;
    /** @brief The maximum number of resources one slab holds (Bevy's `SLAB_CAPACITY`). */
    std::uint32_t slab_capacity = 0;

    /** @brief Create the allocator for one bindless material (Bevy's `new`,
     *  `material_bind_groups.rs:770-806`): one fallback buffer per bindless buffer, sized
     *  by the descriptor or by the standard fallback size, and the slab capacity resolved
     *  from the material's limit.
     *
     *  Bevy expects a capacity here - `slab_capacity.expect(..)` - because a material
     *  without one is non-bindless and should never reach this allocator; that is an
     *  assert rather than a fallback, and the dispatch below is what guarantees it. */
    static MaterialBindGroupBindlessAllocator make(
        const wgpu::Device& device,
        std::string_view label,
        render::render_resource::BindlessDescriptor bindless_descriptor,
        render::BindGroupLayoutDescriptor bind_group_layout,
        std::optional<render::render_resource::BindlessSlabResourceLimit> slab_capacity) {
        assert(slab_capacity.has_value());

        MaterialBindGroupBindlessAllocator allocator;
        allocator.label               = label;
        allocator.bind_group_layout   = std::move(bind_group_layout);
        allocator.bindless_descriptor = std::move(bindless_descriptor);
        allocator.slab_capacity       = slab_capacity->resolve();

        for (const auto& bindless_buffer : allocator.bindless_descriptor.buffers) {
            const std::uint64_t size =
                bindless_buffer.size.has_value() ? *bindless_buffer.size : DEFAULT_BINDLESS_FALLBACK_BUFFER_SIZE;
            allocator.fallback_buffers.emplace(
                bindless_buffer.bindless_index,
                device.createBuffer(wgpu::BufferDescriptor()
                                        .setLabel("bindless fallback buffer")
                                        .setSize(size)
                                        .setUsage(wgpu::BufferUsage::eStorage)));
        }
        return allocator;
    }

    /** @brief The slab at @p group, if this allocator has one there (Bevy
     *  `MaterialBindGroupBindlessAllocator::get`, `material_bind_groups.rs:864-866`). */
    [[nodiscard]] const MaterialBindlessSlab* get(MaterialBindGroupIndex group) const {
        if (group.index >= slabs.size()) return nullptr;
        return &slabs[group.index];
    }

    /** @brief Allocate a material, creating a slab when none can take it (Bevy
     *  `MaterialBindGroupBindlessAllocator::allocate_unprepared`,
     *  `material_bind_groups.rs:816-848`). The first slab that accepts decides the group;
     *  the slot within it is the slab's to choose. */
    MaterialBindingId allocate_unprepared(render::render_resource::UnpreparedBindGroup unprepared_bind_group) {
        for (std::size_t slab_index = 0; slab_index < slabs.size(); ++slab_index) {
            auto attempt = slabs[slab_index].try_allocate(std::move(unprepared_bind_group), slab_capacity);
            if (attempt) {
                return MaterialBindingId{.group = MaterialBindGroupIndex{static_cast<std::uint32_t>(slab_index)},
                                         .slot  = *attempt};
            }
            // The slab handed the material back, so the next one can be tried.
            unprepared_bind_group = std::move(attempt.error());
        }

        const MaterialBindGroupIndex group{static_cast<std::uint32_t>(slabs.size())};
        slabs.emplace_back(MaterialBindlessSlab::make(bindless_descriptor));

        // An empty slab accepts any material, so this cannot fail; Bevy panics here for
        // the same reason.
        auto slot = slabs.back().try_allocate(std::move(unprepared_bind_group), slab_capacity);
        assert(slot.has_value());
        return MaterialBindingId{.group = group, .slot = *slot};
    }
};

/** @brief The allocator for one material type: the bindless one or the non-bindless one
 *  (Bevy `MaterialBindGroupAllocator`, `material_bind_groups.rs:44-49`).
 *
 *  Bevy's two-arm enum with boxed payloads becomes a variant of the two allocators; the
 *  arms are named in Bevy's comments above each accessor rather than by the variant,
 *  which is what C++ has. */
EPIX_EXPORT struct MaterialBindGroupAllocator {
    std::variant<MaterialBindGroupBindlessAllocator, MaterialBindGroupNonBindlessAllocator> value;

    /** @brief Choose the allocator for one material type (Bevy
     *  `MaterialBindGroupAllocator::new`, `material_bind_groups.rs:458-478`): the
     *  bindless one when the material has a bindless descriptor, the non-bindless one
     *  when it does not.
     *
     *  This is the whole dispatch, and it is decided once per material type rather than
     *  per material. */
    static MaterialBindGroupAllocator make(
        const wgpu::Device& device,
        std::string_view label,
        std::optional<render::render_resource::BindlessDescriptor> bindless_descriptor,
        render::BindGroupLayoutDescriptor bind_group_layout,
        std::optional<render::render_resource::BindlessSlabResourceLimit> slab_capacity) {
        if (bindless_descriptor.has_value()) {
            return MaterialBindGroupAllocator{MaterialBindGroupBindlessAllocator::make(
                device, label, std::move(*bindless_descriptor), std::move(bind_group_layout), slab_capacity)};
        }
        return MaterialBindGroupAllocator{MaterialBindGroupNonBindlessAllocator{label}};
    }

    /** @brief The slab at @p group, if this allocator has one there (Bevy
     *  `MaterialBindGroupAllocator::get`, `material_bind_groups.rs:481-492`): the one
     *  place the two allocators are reached through the same interface. */
    [[nodiscard]] std::optional<MaterialSlab> get(MaterialBindGroupIndex group) const {
        if (const auto* bindless = std::get_if<MaterialBindGroupBindlessAllocator>(&value)) {
            const MaterialBindlessSlab* slab = bindless->get(group);
            if (slab == nullptr) return std::nullopt;
            return MaterialSlab{.value = slab};
        }
        // Bound by reference: the slab handed back points into this allocator, so taking a
        // copy here would return a reference into a temporary that dies with the expression.
        const auto& non_bindless = std::get<MaterialBindGroupNonBindlessAllocator>(value);
        if (const auto slab = non_bindless.get(group)) {
            return MaterialSlab{.value = *slab};
        }
        return std::nullopt;
    }
};

/** @brief One allocator per material type (Bevy `MaterialBindGroupAllocators`,
 *  `material_bind_groups.rs:36`).
 *
 *  Keyed by the material's type because each type decides for itself whether it is
 *  bindless, and so which allocator it needs. */
EPIX_EXPORT struct MaterialBindGroupAllocators {
    std::unordered_map<meta::type_index, MaterialBindGroupAllocator> allocators;
};

}  // namespace epix::pbr
