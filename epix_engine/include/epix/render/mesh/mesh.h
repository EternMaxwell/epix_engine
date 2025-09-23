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
        void* (*get_data)(void*);
        size_t (*get_size)(void*);
        size_t (*get_byte_size)(void*);
        std::expected<std::pair<void*, size_t>, MeshError> get_raw_data() {
            return std::pair{get_data(data.get()), get_byte_size(data.get())};
        }
        std::expected<std::pair<const void*, size_t>, MeshError> get_raw_data() const {
            return std::pair{get_data(data.get()), get_byte_size(data.get())};
        }
        template <typename T>
        std::expected<std::span<T>, MeshError> get_data_as() {
            if (value_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            return std::span<T>(static_cast<T*>(get_data(data.get())), get_size(data.get()));
        }
        template <typename T>
        std::expected<std::span<const T>, MeshError> get_data_as() const {
            if (value_type != epix::meta::type_id<std::remove_const_t<T>>()) {
                return std::unexpected(MeshError::TypeMismatch);
            }
            return std::span<const T>(static_cast<const T*>(get_data(data.get())), get_size(data.get()));
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
    size_t attribute_count() const { return attribute_name_to_slot.size(); }
    auto iter_attributes() const { return attribute_name_to_slot | std::views::all; }
    auto attribute_slots() const { return attribute_name_to_slot | std::views::values; }
    auto attribute_names() const { return attribute_name_to_slot | std::views::keys; }
    bool has_attribute(const std::variant<std::string, size_t>& attrib) const {
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            return attribute_name_to_slot.contains(name);
        } else {
            size_t slot = std::get<size_t>(attrib);
            return slot < attributes.size() && attributes[slot].has_value();
        }
    }
    bool has_indices() const { return indices.has_value(); }
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
        attributes[slot] =
            MeshAttributeInfo{.data         = std::shared_ptr<storage_type>(new storage_type(std::forward<T>(data))),
                              .storage_type = epix::meta::type_id<storage_type>(),
                              .value_type   = epix::meta::type_id<std::ranges::range_value_t<storage_type>>(),
                              .get_data     = [](void* ptr) -> void* {
                                  auto vec = static_cast<storage_type*>(ptr);
                                  return std::ranges::data(*vec);
                              },
                              .get_size = [](void* ptr) -> size_t {
                                  auto vec = static_cast<storage_type*>(ptr);
                                  return std::ranges::size(*vec);
                              },
                              .get_byte_size = [](void* ptr) -> size_t {
                                  auto vec = static_cast<storage_type*>(ptr);
                                  return std::ranges::size(*vec) * sizeof(std::ranges::range_value_t<storage_type>);
                              }};
        return {};
    }
    std::expected<std::pair<const void*, size_t>, MeshError> get_data(
        const std::variant<std::string, size_t>& attrib) const {
        size_t slot;
        if (std::holds_alternative<std::string>(attrib)) {
            auto name = std::get<std::string>(attrib);
            if (!attribute_name_to_slot.contains(name)) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
            slot = attribute_name_to_slot.at(name);
        } else {
            slot = std::get<size_t>(attrib);
            if (slot >= attributes.size() || !attributes[slot]) {
                return std::unexpected(MeshError::AttributeNotFound);
            }
        }
        auto& attr = attributes[slot];
        return attr->get_raw_data();
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
            slot = attribute_name_to_slot.at(name);
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
            slot = attribute_name_to_slot.at(name);
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
    std::expected<std::pair<const void*, size_t>, MeshError> get_indices() const {
        return indices->get_u16()
            .transform([](auto span) {
                return std::make_pair(static_cast<const void*>(span.data()), span.size() * sizeof(uint16_t));
            })
            .or_else([this](auto) -> std::expected<std::pair<const void*, size_t>, MeshError> {
                return indices->get_u32().transform([](auto span) {
                    return std::make_pair(static_cast<const void*>(span.data()), span.size() * sizeof(uint32_t));
                });
            });
    }
    std::expected<std::span<uint16_t>, MeshError> get_indices_u16() {
        if (!indices) {
            return std::unexpected(MeshError::AttributeNotFound);
        }
        return indices->get_u16();
    }
    std::expected<std::span<const uint16_t>, MeshError> get_indices_u16() const {
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
    std::expected<std::span<const uint32_t>, MeshError> get_indices_u32() const {
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

    GPUMesh() = default;

   public:
    void update(nvrhi::DeviceHandle device, const Mesh& mesh) {
        // This function assumes that the mesh data is changed entirely.
        // New buffer will be created if the previous one is not large enough, or if the previous one is too large.
        // Otherwise, the existing buffer will be updated in place.

        // We are currently not reusing buffers from different slots. But should be done in the future.

        auto commandlist = device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
        commandlist->open();

        // Vertex buffers
        for (auto&& slot : mesh.attribute_slots()) {
            auto attr = mesh.get_data(slot);
            if (!attr) {
                continue;
            }
            auto [data_ptr, data_size] = *attr;
            vertex_buffers.resize(std::max(vertex_buffers.size(), slot + 1));
            if (!vertex_buffers[slot] || vertex_buffers[slot]->getDesc().byteSize < data_size ||
                vertex_buffers[slot]->getDesc().byteSize > data_size * 2) {
                auto desc = nvrhi::BufferDesc()
                                .setByteSize(data_size * 1.5f)  // Add some padding to avoid frequent reallocations
                                .setIsVertexBuffer(true)
                                .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                .setKeepInitialState(true);
                vertex_buffers[slot] = device->createBuffer(desc);
            }
            commandlist->writeBuffer(vertex_buffers[slot], data_ptr, data_size);
        }
        // Index buffer
        if (mesh.has_indices()) {
            auto attr = mesh.get_indices();
            if (attr) {
                auto [data_ptr, data_size] = *attr;
                if (!index_buffer || index_buffer->getDesc().byteSize < data_size ||
                    index_buffer->getDesc().byteSize > data_size * 2) {
                    auto desc = nvrhi::BufferDesc()
                                    .setByteSize(data_size * 1.5f)  // Add some padding to avoid frequent reallocations
                                    .setIsIndexBuffer(true)
                                    .setFormat(mesh.get_indices_u16().has_value() ? nvrhi::Format::R16_UINT
                                                                                  : nvrhi::Format::R32_UINT)
                                    .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                    .setKeepInitialState(true);
                    index_buffer = device->createBuffer(desc);
                }
                commandlist->writeBuffer(index_buffer, data_ptr, data_size);
            }
        } else {
            index_buffer = nullptr;
        }

        commandlist->close();
        device->executeCommandList(commandlist);
    }
    static GPUMesh create(nvrhi::DeviceHandle device, const Mesh& mesh) {
        GPUMesh gpu_mesh;
        gpu_mesh.update(device, mesh);
        return gpu_mesh;
    }
    /**
     * @brief Bind the GPU mesh to the graphics state. This will not reset the previously bound vertex buffers. So
     * manually reset them if needed.
     *
     * @param state The graphics state to bind the mesh to.
     */
    void bind(nvrhi::GraphicsState& state) const {
        for (auto&& [slot, vb] : std::views::enumerate(vertex_buffers) |
                                     std::views::filter([](auto&& pair) { return std::get<1>(pair) != nullptr; })) {
            state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vb).setSlot(slot));
        }
        if (index_buffer) {
            state.setIndexBuffer(
                nvrhi::IndexBufferBinding().setBuffer(index_buffer).setFormat(index_buffer->getDesc().format));
        } else {
            state.setIndexBuffer(nvrhi::IndexBufferBinding());
        }
    }
    auto iter_vertex_buffers() const {
        return vertex_buffers | std::views::transform([](auto&& vb) -> nvrhi::BufferHandle {
                   return vb;
               })  // Make a copy to avoid dangling reference
               | std::views::enumerate | std::views::filter([](auto&& pair) { return std::get<1>(pair) != nullptr; });
    }
    nvrhi::BufferHandle get_index_buffer() const { return index_buffer; }
};
}  // namespace epix::mesh