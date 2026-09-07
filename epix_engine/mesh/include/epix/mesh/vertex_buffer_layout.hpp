#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::mesh {

/** @brief Unique id of a vertex attribute (Bevy `MeshVertexAttributeId`). */
struct MeshVertexAttributeId {
    std::uint64_t value                                           = 0;
    auto operator<=>(const MeshVertexAttributeId&) const noexcept = default;
};

/** @brief One concrete attribute in an interleaved GPU vertex buffer. */
struct VertexAttribute {
    std::uint64_t offset          = 0;
    wgpu::VertexFormat format     = wgpu::VertexFormat::eFloat32;
    std::uint32_t shader_location = 0;

    bool operator==(const VertexAttribute&) const noexcept = default;
};

/** @brief Pipeline request for a mesh attribute at a shader location. */
struct VertexAttributeDescriptor {
    std::uint32_t shader_location = 0;
    MeshVertexAttributeId id;
    std::string_view name;
};

/** @brief Error returned when a pipeline requests an attribute absent from a mesh layout. */
struct MissingVertexAttributeError {
    std::optional<std::string_view> pipeline_type;
    MeshVertexAttributeId id;
    std::string_view name;
};

/** @brief A vertex buffer layout: interleaved attribute strides + attributes
 * (Bevy bevy_mesh `VertexBufferLayout`, wgpu-types). */
struct VertexBufferLayout {
    std::uint64_t array_stride     = 0;
    wgpu::VertexStepMode step_mode = wgpu::VertexStepMode::eVertex;
    std::vector<VertexAttribute> attributes;

    bool operator==(const VertexBufferLayout& other) const noexcept {
        return array_stride == other.array_stride && step_mode == other.step_mode && attributes == other.attributes;
    }
};

/** @brief Interleaved vertex-buffer layout of one mesh (Bevy bevy_mesh
 * `MeshVertexBufferLayout`): the attribute ids in id order plus the concrete
 * wgpu layout (array stride, per-attribute offsets). Interned per-mesh by
 * `MeshVertexBufferLayouts`. */
struct MeshVertexBufferLayout {
    MeshVertexBufferLayout(std::vector<MeshVertexAttributeId> attribute_ids, VertexBufferLayout layout)
        : _attribute_ids(std::move(attribute_ids)), _layout(std::move(layout)) {}

    bool contains(MeshVertexAttributeId attribute_id) const noexcept {
        return std::ranges::find(_attribute_ids, attribute_id) != _attribute_ids.end();
    }

    std::span<const MeshVertexAttributeId> attribute_ids() const noexcept { return _attribute_ids; }

    const VertexBufferLayout& layout() const noexcept { return _layout; }

    std::expected<VertexBufferLayout, MissingVertexAttributeError> get_layout(
        std::span<const VertexAttributeDescriptor> attribute_descriptors) const {
        std::vector<VertexAttribute> attributes;
        attributes.reserve(attribute_descriptors.size());
        for (const auto& descriptor : attribute_descriptors) {
            const auto it = std::ranges::find(_attribute_ids, descriptor.id);
            if (it == _attribute_ids.end()) {
                return std::unexpected(MissingVertexAttributeError{
                    .pipeline_type = std::nullopt,
                    .id            = descriptor.id,
                    .name          = descriptor.name,
                });
            }
            const auto index = static_cast<std::size_t>(std::distance(_attribute_ids.begin(), it));
            const auto& raw  = _layout.attributes[index];
            attributes.push_back(VertexAttribute{
                .offset          = raw.offset,
                .format          = raw.format,
                .shader_location = descriptor.shader_location,
            });
        }
        return VertexBufferLayout{
            .array_stride = _layout.array_stride,
            .step_mode    = _layout.step_mode,
            .attributes   = std::move(attributes),
        };
    }

    bool operator==(const MeshVertexBufferLayout&) const noexcept = default;

   private:
    std::vector<MeshVertexAttributeId> _attribute_ids;
    VertexBufferLayout _layout;
};

/** @brief Shared reference to an interned `MeshVertexBufferLayout` (Bevy
 * `MeshVertexBufferLayoutRef`: an `Arc` wrapper with pointer equality). */
struct MeshVertexBufferLayoutRef {
    std::shared_ptr<const MeshVertexBufferLayout> value;

    bool operator==(const MeshVertexBufferLayoutRef& other) const noexcept { return value.get() == other.value.get(); }
};

/** @brief Render-world store holding a single copy of each mesh vertex buffer
 * layout (Bevy `MeshVertexBufferLayouts`). Layouts are interned on insert so
 * meshes sharing a layout share one reference. */
struct MeshVertexBufferLayouts {
    /** @brief Insert a layout, reusing the existing instance when one with the
     * same contents is already stored (Bevy `insert`, structural compare). */
    MeshVertexBufferLayoutRef insert(MeshVertexBufferLayout layout) {
        if (const auto existing = _layouts.find(layout); existing != _layouts.end()) {
            return MeshVertexBufferLayoutRef{*existing};
        }
        auto value = std::make_shared<const MeshVertexBufferLayout>(std::move(layout));
        const auto [inserted, _] = _layouts.insert(std::move(value));
        return MeshVertexBufferLayoutRef{*inserted};
    }

   private:
    struct StructuralHash {
        using is_transparent = void;

        static std::size_t combine(std::size_t seed, std::size_t value) noexcept {
            return seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u));
        }

        static std::size_t hash(const MeshVertexBufferLayout& value) noexcept {
            std::size_t result = 0;
            for (const auto id : value.attribute_ids()) {
                result = combine(result, std::hash<std::uint64_t>{}(id.value));
            }
            const auto& layout = value.layout();
            result = combine(result, std::hash<std::uint64_t>{}(layout.array_stride));
            result = combine(result, std::hash<std::underlying_type_t<wgpu::VertexStepMode>>{}(
                                         static_cast<std::underlying_type_t<wgpu::VertexStepMode>>(layout.step_mode)));
            for (const auto& attribute : layout.attributes) {
                result = combine(result, std::hash<std::uint64_t>{}(attribute.offset));
                result = combine(result, std::hash<std::underlying_type_t<wgpu::VertexFormat>>{}(
                                             static_cast<std::underlying_type_t<wgpu::VertexFormat>>(attribute.format)));
                result = combine(result, std::hash<std::uint32_t>{}(attribute.shader_location));
            }
            return result;
        }

        std::size_t operator()(const MeshVertexBufferLayout& value) const noexcept { return hash(value); }
        std::size_t operator()(const std::shared_ptr<const MeshVertexBufferLayout>& value) const noexcept {
            return hash(*value);
        }
    };

    struct StructuralEqual {
        using is_transparent = void;

        bool operator()(const MeshVertexBufferLayout& lhs, const MeshVertexBufferLayout& rhs) const noexcept {
            return lhs == rhs;
        }
        bool operator()(const std::shared_ptr<const MeshVertexBufferLayout>& lhs,
                        const std::shared_ptr<const MeshVertexBufferLayout>& rhs) const noexcept {
            return *lhs == *rhs;
        }
        bool operator()(const std::shared_ptr<const MeshVertexBufferLayout>& lhs,
                        const MeshVertexBufferLayout& rhs) const noexcept {
            return *lhs == rhs;
        }
        bool operator()(const MeshVertexBufferLayout& lhs,
                        const std::shared_ptr<const MeshVertexBufferLayout>& rhs) const noexcept {
            return lhs == *rhs;
        }
    };

    std::unordered_set<std::shared_ptr<const MeshVertexBufferLayout>, StructuralHash, StructuralEqual> _layouts;
};

}  // namespace epix::mesh

template <>
struct std::hash<epix::mesh::MeshVertexBufferLayoutRef> {
    std::size_t operator()(const epix::mesh::MeshVertexBufferLayoutRef& value) const noexcept {
        return std::hash<const epix::mesh::MeshVertexBufferLayout*>{}(value.value.get());
    }
};
