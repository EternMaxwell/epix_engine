#pragma once

#include <nvrhi/nvrhi.h>

#include <epix/core.hpp>
#include <map>

#include "epix/core/type_system/type_registry.hpp"

namespace epix::mesh {
enum class MeshError {
    AttributeAlreadyExists,
    AttributeNotFound,
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
struct Mesh {
   public:
    template <typename T>
    std::expected<void, MeshError> insert_attribute(MeshAttribute attribute, std::span<const T> data) {
        MeshAttributeData attribute_data{
            .attribute = attribute,
            .data      = core::storage::untyped_vector(core::type_system::TypeInfo::get_info<T>(), data.size()),
        };
        for (auto&& item : data) {
            attribute_data.data.push_back_copy(item);
        }
        auto [it, inserted] = attributes.insert_or_assign(attribute.slot, std::move(attribute_data));
        return {};
    }

   private:
    std::map<size_t, MeshAttributeData> attributes;
    std::optional<core::storage::untyped_vector> indices;
};
}  // namespace epix::mesh