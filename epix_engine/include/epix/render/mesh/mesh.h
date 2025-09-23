#pragma once

#include <epix/assets.h>
#include <epix/render.h>

namespace epix::mesh {
enum class MeshError {
    AttributeAlreadyExists,
    AttributeNotFound,
    TypeMismatch,
};
struct MeshAttribute {
    std::string name;
    size_t slot;
};
/**
 * Type erased mesh for cpu side storage.
 */
struct Mesh {
    struct MeshAttributeInfo {
        std::shared_ptr<void> data;
        epix::meta::type_index storage_type;
        epix::meta::type_index value_type;
        std::pair<void*, size_t> (*get_data)(void*);
        template <typename T>
        std::expected<std::span<T>, MeshError> get_data_as() {
            if (value_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            auto [ptr, size] = get_data(data.get());
            return std::span<T>(static_cast<T*>(ptr), size);
        }
        template <typename T>
        std::expected<std::span<const T>, MeshError> get_data_as() const {
            if (value_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            auto [ptr, size] = get_data(data.get());
            return std::span<const T>(static_cast<const T*>(ptr), size);
        }
        template <typename T>
        std::expected<T*, MeshError> get_storage() {
            if (storage_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            return static_cast<T*>(data.get());
        }
        template <typename T>
        std::expected<const T*, MeshError> get_storage() const {
            if (storage_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            return static_cast<const T*>(data.get());
        }
    };
    struct MeshIndicesInfo {
        std::shared_ptr<void> data;
        epix::meta::type_index storage_type;
        enum class IndexType {
            UInt16,
            UInt32,
        } value_type;
        std::pair<void*, size_t> (*get_data)(void*);
        std::expected<std::span<uint16_t>, MeshError> get_u16() {
            if (value_type != IndexType::UInt16) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            auto [ptr, size] = get_data(data.get());
            return std::span<uint16_t>(static_cast<uint16_t*>(ptr), size);
        }
        std::expected<std::span<uint32_t>, MeshError> get_u32() {
            if (value_type != IndexType::UInt32) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            auto [ptr, size] = get_data(data.get());
            return std::span<uint32_t>(static_cast<uint32_t*>(ptr), size);
        }
        std::expected<std::span<const uint16_t>, MeshError> get_u16() const {
            if (value_type != IndexType::UInt16) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            auto [ptr, size] = get_data(data.get());
            return std::span<const uint16_t>(static_cast<const uint16_t*>(ptr), size);
        }
        std::expected<std::span<const uint32_t>, MeshError> get_u32() const {
            if (value_type != IndexType::UInt32) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            auto [ptr, size] = get_data(data.get());
            return std::span<const uint32_t>(static_cast<const uint32_t*>(ptr), size);
        }

        template <typename T>
        std::expected<T*, MeshError> get_storage() {
            if (storage_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            return static_cast<T*>(data.get());
        }
        template <typename T>
        std::expected<const T*, MeshError> get_storage() const {
            if (storage_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            return static_cast<const T*>(data.get());
        }
    };

   private:
    std::vector<std::optional<MeshAttributeInfo>> attributes;
    entt::dense_map<std::string, size_t> attribute_name_to_slot;

    std::optional<MeshIndicesInfo> indices;

   public:
    template <typename T>
    std::expected<void, MeshError> insert(MeshAttribute attrib, T&& data)
        requires(std::ranges::contiguous_range<std::remove_cvref_t<T>> &&
                 std::ranges::sized_range<std::remove_cvref_t<T>> &&
                 std::is_trivially_copyable_v<std::ranges::range_value_t<std::remove_cvref_t<T>>>)
    {
        using storage_type = std::remove_cvref_t<T>;
        if (attribute_name_to_slot.contains(attrib.name)) {
            return std::unexpected(MeshError::AttributeAlreadyExists);
        }
        size_t slot = attrib.slot;
        if (slot >= attributes.size()) {
            attributes.resize(slot + 1);
        }
        attribute_name_to_slot[attrib.name] = slot;
        attributes[slot]                    = MeshAttributeInfo{
                               .data         = std::shared_ptr<storage_type>(new storage_type(std::forward<T>(data))),
                               .storage_type = epix::meta::type_id<storage_type>(),
                               .value_type   = epix::meta::type_id<std::ranges::range_value_t<storage_type>>(),
                               .get_data     = [](void* ptr) -> std::pair<void*, size_t> {
                auto vec = static_cast<storage_type*>(ptr);
                return {std::ranges::data(*vec), std::ranges::size(*vec)};
            },
        };
        return {};
    }
    std::expected<std::pair<void*, size_t>, MeshError> get_data(const std::variant<std::string, size_t>& attrib) {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot[name];
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
        }
        auto& attr = attributes[slot];
        return attr->get_data(attr->data.get());
    }
    template <typename T>
    std::expected<std::span<T>, MeshError> get_data_as(const std::variant<std::string, size_t>& attrib) {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot[name];
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
        }
        auto& attr = attributes[slot];
        return attr->template get_data_as<T>();
    }
    template <typename T>
    std::expected<std::span<const T>, MeshError> get_data_as(const std::variant<std::string, size_t>& attrib) const {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot[name];
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
        }
        auto& attr = attributes[slot];
        return attr->template get_data_as<T>();
    }
    template <typename T>
    std::expected<T*, MeshError> get_storage(const std::variant<std::string, size_t>& attrib) {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot[name];
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
        }
        auto& attr = attributes[slot];
        return attr->template get_storage<T>();
    }
    template <typename T>
    std::expected<const T*, MeshError> get_storage(const std::variant<std::string, size_t>& attrib) const {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot[name];
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
        }
        auto& attr = attributes[slot];
        return attr->template get_storage<T>();
    }
    std::expected<void, MeshError> remove(const std::variant<std::string, size_t>& attrib) {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot[name];
            attribute_name_to_slot.erase(name);
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            for (auto it = attribute_name_to_slot.begin(); it != attribute_name_to_slot.end(); ++it) {
                if (it->second == slot) {
                    attribute_name_to_slot.erase(it);
                    break;
                }
            }
        }
        attributes[slot] = std::nullopt;
        return {};
    }

    template <typename T>
    std::expected<void, MeshError> set_indices(T&& data)
        requires(std::ranges::contiguous_range<std::remove_cvref_t<T>> &&
                 std::ranges::sized_range<std::remove_cvref_t<T>> &&
                 (std::is_same_v<std::ranges::range_value_t<std::remove_cvref_t<T>>, uint16_t> ||
                  std::is_same_v<std::ranges::range_value_t<std::remove_cvref_t<T>>, uint32_t>))
    {
        using storage_type = std::remove_cvref_t<T>;
        using value_type   = std::ranges::range_value_t<storage_type>;
        MeshIndicesInfo::IndexType index_type;
        if constexpr (std::is_same_v<value_type, uint16_t>) {
            index_type = MeshIndicesInfo::IndexType::UInt16;
        } else if constexpr (std::is_same_v<value_type, uint32_t>) {
            index_type = MeshIndicesInfo::IndexType::UInt32;
        } else {
            static_assert(false, "Unsupported index type");
        }
        indices = MeshIndicesInfo{
            .data         = std::shared_ptr<storage_type>(new storage_type(std::forward<T>(data))),
            .storage_type = epix::meta::type_id<storage_type>(),
            .value_type   = index_type,
            .get_data     = [](void* ptr) -> std::pair<void*, size_t> {
                auto vec = static_cast<storage_type*>(ptr);
                return {std::ranges::data(*vec), std::ranges::size(*vec)};
            },
        };
        return {};
    }
    std::expected<void, MeshError> remove_indices() {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        indices = std::nullopt;
        return {};
    }
    std::expected<std::pair<void*, size_t>, MeshError> get_indices() {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        return indices->get_data(indices->data.get());
    }
    std::expected<std::span<uint16_t>, MeshError> get_indices_u16() {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        return indices->get_u16();
    }
    std::expected<std::span<uint32_t>, MeshError> get_indices_u32() {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        return indices->get_u32();
    }
    template <typename T>
    std::expected<T*, MeshError> get_indices_storage() {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        return indices->template get_storage<T>();
    }
    template <typename T>
    std::expected<const T*, MeshError> get_indices_storage() const {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        return indices->template get_storage<T>();
    }
};
struct GPUMesh {
   private:
    std::vector<nvrhi::BufferHandle> vertex_buffers;
    nvrhi::BufferHandle index_buffer;

   public:
};
}  // namespace epix::mesh