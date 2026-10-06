#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/render/as_bind_group.hpp>
#include <epix/render/bind_group_layout.hpp>
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
