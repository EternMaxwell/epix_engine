#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/render/as_bind_group.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bindless.hpp>
#include <epix/render/render_resource.hpp>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>
#endif

#include <epix/pbr/material_bindings.hpp>

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

}  // namespace epix::pbr
