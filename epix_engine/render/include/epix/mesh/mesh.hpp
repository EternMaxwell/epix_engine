#pragma once

#include <epix/core.hpp>
#include <epix/render/vulkan.hpp>
#include <map>
#include <ranges>

#include "epix/core/meta/info.hpp"
#include "epix/core/storage/untypedvec.hpp"
#include "nvrhi/nvrhi.h"

namespace epix::mesh {
enum class MeshError {
    SlotNotFound,
    NameMismatch,
    TypeIncompatible,
    TypeMismatch,
};
struct MeshAttribute {
    std::string name;
    size_t slot;
    nvrhi::Format format;
};
struct MeshAttributeData {
    MeshAttribute attribute;
    core::storage::untyped_vector data;
};
struct MeshIndices {
   public:
    MeshIndices(const meta::type_info& desc) : data(desc) {}
    MeshIndices(core::storage::untyped_vector&& vec) : data(std::move(vec)) {}

    bool is_u16() const { return data.type_info() == meta::type_info::of<uint16_t>(); }
    bool is_u32() const { return data.type_info() == meta::type_info::of<uint32_t>(); }
    std::span<const uint16_t> as_u16() const { return data.cspan_as<uint16_t>(); }
    std::span<const uint32_t> as_u32() const { return data.cspan_as<uint32_t>(); }

   public:
    core::storage::untyped_vector data;
};
struct Mesh {
   public:
    static const MeshAttribute ATTRIBUTE_POSITION;
    static const MeshAttribute ATTRIBUTE_COLOR;
    static const MeshAttribute ATTRIBUTE_NORMAL;
    static const MeshAttribute ATTRIBUTE_UV0;
    static const MeshAttribute ATTRIBUTE_UV1;

   public:
    /// Insert a new attribute with given data to the mesh or replace existing one regardless of type.
    template <std::ranges::viewable_range T>
    std::expected<void, MeshError> insert_attribute(MeshAttribute attribute, T&& data) {
        using value_type = std::ranges::range_value_t<T>;
        if (nvrhi::getFormatInfo(attribute.format).bytesPerBlock != sizeof(value_type) ||
            nvrhi::getFormatInfo(attribute.format).blockSize != 1) {
            return std::unexpected(MeshError::TypeIncompatible);
        }
        MeshAttributeData attribute_data{
            .attribute = attribute,
            .data      = std::ranges::to<core::storage::untyped_vector>(std::forward<T>(data),
                                                                        meta::type_info::of<value_type>()),
        };
        auto [it, inserted] = _attributes.insert_or_assign(attribute.slot, std::move(attribute_data));
        return {};
    }
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(
        const MeshAttribute& attribute) const;
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(size_t slot) const;
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(
        const MeshAttribute& attribute);
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(size_t slot);
    std::expected<MeshAttributeData, MeshError> remove_attribute(const MeshAttribute& attribute);
    std::expected<MeshAttributeData, MeshError> remove_attribute(size_t slot);
    bool contains_attribute(const MeshAttribute& attribute) const { return get_attribute(attribute).has_value(); }
    bool contains_attribute(size_t slot) const { return get_attribute(slot).has_value(); }

    /// Insert indices to mesh or replace existing ones regardless of type. The type should be either uint16_t or
    /// uint32_t.
    template <typename V = uint16_t, std::ranges::viewable_range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, uint16_t> || std::same_as<V, uint32_t>)
    void insert_indices(T&& data) {
        using value_type = std::ranges::range_value_t<T>;
        _indices.emplace(MeshIndices(meta::type_info::of<V>()));
        if constexpr (std::ranges::sized_range<T>) {
            _indices->data.reserve(static_cast<size_t>(std::ranges::size(data)));
        }
        std::ranges::for_each(std::forward<T>(data),
                              [&](auto&& v) { _indices->data.push_back(std::forward<decltype(v)>(v)); });
    }
    std::optional<std::reference_wrapper<const MeshIndices>> get_indices() const {
        return _indices.transform([](const MeshIndices& indices) { return std::cref(indices); });
    }
    std::optional<std::reference_wrapper<MeshIndices>> get_indices_mut() {
        return _indices.transform([](MeshIndices& indices) { return std::ref(indices); });
    }
    std::optional<MeshIndices> remove_indices() {
        std::optional<MeshIndices> res;
        std::swap(res, _indices);
        return res;
    }

   private:
    nvrhi::PrimitiveType primitive_type;
    std::map<size_t, MeshAttributeData> _attributes;
    std::optional<MeshIndices> _indices;
};
inline const MeshAttribute Mesh::ATTRIBUTE_POSITION{"position", 0, nvrhi::Format::RGB32_FLOAT};
inline const MeshAttribute Mesh::ATTRIBUTE_COLOR{"color", 1, nvrhi::Format::RGBA32_FLOAT};
inline const MeshAttribute Mesh::ATTRIBUTE_NORMAL{"normal", 2, nvrhi::Format::RGB32_FLOAT};
inline const MeshAttribute Mesh::ATTRIBUTE_UV0{"uv0", 3, nvrhi::Format::RG32_FLOAT};
inline const MeshAttribute Mesh::ATTRIBUTE_UV1{"uv1", 4, nvrhi::Format::RG32_FLOAT};
}  // namespace epix::mesh