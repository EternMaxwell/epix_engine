module;

#include <spdlog/spdlog.h>

export module epix.mesh:mesh;

import std;
import webgpu;
import epix.core;
import glm;

namespace epix::mesh {
constexpr std::size_t vertex_format_size(wgpu::VertexFormat format) {
    switch (format) {
        case wgpu::VertexFormat::eFloat32:
            return sizeof(float);
        case wgpu::VertexFormat::eFloat32x2:
            return sizeof(float) * 2;
        case wgpu::VertexFormat::eFloat32x3:
            return sizeof(float) * 3;
        case wgpu::VertexFormat::eFloat32x4:
            return sizeof(float) * 4;
        default:
            return 0;
    }
}
/** @brief Error codes for mesh attribute operations. */
export enum class MeshError {
    /** @brief No attribute at given slot. */
    SlotNotFound,
    /** @brief Attribute name does not match the current one at given slot. */
    NameMismatch,
    /** @brief The type of provided data is incompatible with the attribute format. */
    TypeIncompatible,
    /** @brief The format of stored attribute does not match the requested one. */
    TypeMismatch,
};
/** @brief Describes a single vertex attribute in a mesh (name, slot, format). */
export struct MeshAttribute {
    std::string name;
    std::uint32_t slot;
    wgpu::VertexFormat format;

    bool operator==(const MeshAttribute& other) const {
        return name == other.name && slot == other.slot && format == other.format;
    }
};
/** @brief Ordered map of slot->MeshAttribute describing the complete vertex layout. */
export struct MeshAttributeLayout : std::map<std::uint32_t, MeshAttribute> {
    wgpu::PrimitiveTopology primitive_type = wgpu::PrimitiveTopology::eTriangleList;

    bool operator==(const MeshAttributeLayout& other) const {
        return primitive_type == other.primitive_type &&
               static_cast<const std::map<std::uint32_t, MeshAttribute>&>(*this) ==
                   static_cast<const std::map<std::uint32_t, MeshAttribute>&>(other);
    }

    bool contains_attribute(const MeshAttribute& attribute) const {
        auto it = this->find(attribute.slot);
        return it != this->end() && it->second == attribute;
    }
    /** @brief Add or replace an attribute in the layout. */
    void add_attribute(const MeshAttribute& attribute) { this->insert_or_assign(attribute.slot, attribute); }
    /** @brief Get a const reference to an attribute by descriptor. */
    std::optional<std::reference_wrapper<const MeshAttribute>> get_attribute(const MeshAttribute& attribute) const {
        auto it = this->find(attribute.slot);
        if (it != this->end() && it->second == attribute) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a const reference to an attribute by slot index. */
    std::optional<std::reference_wrapper<const MeshAttribute>> get_attribute(std::uint32_t slot) const {
        auto it = this->find(slot);
        if (it != this->end()) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a mutable reference to an attribute by descriptor. */
    std::optional<std::reference_wrapper<MeshAttribute>> get_attribute_mut(const MeshAttribute& attribute) {
        auto it = this->find(attribute.slot);
        if (it != this->end() && it->second == attribute) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a mutable reference to an attribute by slot index. */
    std::optional<std::reference_wrapper<MeshAttribute>> get_attribute_mut(std::uint32_t slot) {
        auto it = this->find(slot);
        if (it != this->end()) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a human-readable string representation of this layout. */
    std::string to_string() const {
        std::stringstream ss;
        std::println(ss, "MeshAttributeLayout {{ primitive_type = {}", wgpu::to_string(primitive_type));
        for (const auto& [slot, attribute] : *this) {
            std::println(ss, "  Slot {}: Name='{}', Format={}", slot, attribute.name,
                         wgpu::to_string(attribute.format));
        }
        std::println(ss, "}}");
        return ss.str();
    }
};
/** @brief Pairs a MeshAttribute descriptor with its raw vertex data buffer. */
export struct MeshAttributeData {
    MeshAttribute attribute;
    core::untyped_vector data;

    std::size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
};
/** @brief Index buffer data, stored as either uint16 or uint32 values. */
export struct MeshIndices {
   public:
    MeshIndices(const meta::type_info& desc) : data(desc) {}
    MeshIndices(core::untyped_vector&& vec) : data(std::move(vec)) {}

    /** @brief Check if this is a uint16 index buffer. */
    bool is_u16() const { return data.type_info() == meta::type_info::of<std::uint16_t>(); }
    /** @brief Check if this is a uint32 index buffer. */
    bool is_u32() const { return data.type_info() == meta::type_info::of<std::uint32_t>(); }
    /** @brief Get the index data as a span of uint16 values. */
    std::span<const std::uint16_t> as_u16() const { return data.cspan_as<std::uint16_t>(); }
    /** @brief Get the index data as a span of uint32 values. */
    std::span<const std::uint32_t> as_u32() const { return data.cspan_as<std::uint32_t>(); }
    /** @brief Number of indices. */
    std::size_t size() const { return data.size(); }
    /** @brief Whether the index buffer is empty. */
    bool empty() const { return data.empty(); }

   public:
    core::untyped_vector data;
};
/** @brief CPU-side mesh asset storing vertex attributes and optional index data.
 *
 * Meshes are move-only. Use insert_attribute/with_attribute to add vertex data
 * and insert_indices/with_indices to add index data. Standard attribute constants
 * (ATTRIBUTE_POSITION, etc.) are provided.
 */
export struct Mesh {
   public:
    static const MeshAttribute ATTRIBUTE_POSITION;
    static const MeshAttribute ATTRIBUTE_COLOR;
    static const MeshAttribute ATTRIBUTE_NORMAL;
    static const MeshAttribute ATTRIBUTE_UV0;
    static const MeshAttribute ATTRIBUTE_UV1;

   public:
    Mesh() : primitive_type(wgpu::PrimitiveTopology::eTriangleList) {}
    Mesh(wgpu::PrimitiveTopology primitive_type) : primitive_type(primitive_type) {}
    Mesh(const Mesh&)            = delete;
    Mesh(Mesh&&)                 = default;
    Mesh& operator=(const Mesh&) = delete;
    Mesh& operator=(Mesh&&)      = default;

    /** @brief Get the primitive topology. */
    wgpu::PrimitiveTopology get_primitive_type() const { return primitive_type; }
    /** @brief Set the primitive topology. */
    void set_primitive_type(wgpu::PrimitiveTopology type) { primitive_type = type; }
    /** @brief Builder-style setter for primitive topology. */
    auto&& with_primitive_type(this auto&& self, wgpu::PrimitiveTopology type) {
        self.set_primitive_type(type);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Iterate over all attributes (const). */
    auto iter_attributes() const { return std::views::values(_attributes); }
    /** @brief Iterate over all attributes (mutable). */
    auto iter_attributes_mut() { return std::views::values(_attributes); }

    /** @brief Build a MeshAttributeLayout from the current attributes. */
    MeshAttributeLayout attribute_layout() const;

    /** @brief Insert a new attribute with given data, or replace existing one at that slot.
     *  Caller must ensure the data type matches the attribute format. */
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    std::expected<void, MeshError> insert_attribute(MeshAttribute attribute, T&& data) {
        using value_type = std::ranges::range_value_t<T>;
        if (vertex_format_size(attribute.format) != sizeof(value_type)) {
            return std::unexpected(MeshError::TypeIncompatible);
        }
        MeshAttributeData attribute_data{
            .attribute = attribute,
            .data = std::ranges::to<core::untyped_vector>(std::forward<T>(data), meta::type_info::of<value_type>()),
        };
        auto [it, inserted] = _attributes.insert_or_assign(attribute.slot, std::move(attribute_data));
        return {};
    }
    /** @brief Builder-style insert_attribute with error callback. */
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    auto&& with_attribute(this auto&& self,
                          MeshAttribute attribute,
                          T&& data,
                          std::invocable<std::expected<void, MeshError>> auto&& callback) {
        callback(self.insert_attribute(attribute, std::forward<decltype(data)>(data)));
        return std::forward<decltype(self)>(self);
    }
    /** @brief Builder-style insert_attribute (errors silently ignored). */
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    auto&& with_attribute(this auto&& self, MeshAttribute attribute, T&& data) {
        self.insert_attribute(attribute, std::forward<decltype(data)>(data));
        return std::forward<decltype(self)>(self);
    }
    /** @brief Get a const attribute data by descriptor. */
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(
        const MeshAttribute& attribute) const;
    /** @brief Get a const attribute data by slot index. */
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(std::size_t slot) const;
    /** @brief Get a mutable attribute data by descriptor. */
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(
        const MeshAttribute& attribute);
    /** @brief Get a mutable attribute data by slot index. */
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(std::size_t slot);
    /** @brief Remove an attribute by descriptor, returning the data. */
    std::expected<MeshAttributeData, MeshError> remove_attribute(const MeshAttribute& attribute);
    /** @brief Remove an attribute by slot, returning the data. */
    std::expected<MeshAttributeData, MeshError> remove_attribute(std::size_t slot);
    auto&& with_removed_attribute(this auto&& self,
                                  const MeshAttribute& attribute,
                                  std::invocable<std::expected<MeshAttributeData, MeshError>> auto&& callback) {
        callback(self.remove_attribute(attribute));
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(this auto&& self, MeshAttribute attribute) {
        self.remove_attribute(attribute);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Builder-style remove_attribute by slot with callback. */
    auto&& with_removed_attribute(this auto&& self,
                                  std::size_t slot,
                                  std::invocable<std::expected<MeshAttributeData, MeshError>> auto&& callback) {
        callback(self.remove_attribute(slot));
        return std::forward<decltype(self)>(self);
    }
    /** @brief Builder-style remove_attribute by slot. */
    auto&& with_removed_attribute(this auto&& self, std::size_t slot) {
        self.remove_attribute(slot);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Check if an attribute matching the descriptor is present. */
    bool contains_attribute(const MeshAttribute& attribute) const { return get_attribute(attribute).has_value(); }
    /** @brief Check if an attribute at the given slot is present. */
    bool contains_attribute(std::size_t slot) const { return get_attribute(slot).has_value(); }

    /** @brief Insert indices, replacing any existing ones.
     *  @tparam V Index type (`std::uint16_t` or `std::uint32_t`). */
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    void insert_indices(T&& data) {
        using value_type = std::ranges::range_value_t<T>;
        _indices.emplace(MeshIndices(meta::type_info::of<V>()));
        if constexpr (std::ranges::sized_range<T>) {
            _indices->data.reserve(static_cast<std::size_t>(std::ranges::size(data)));
        }
        std::ranges::for_each(std::forward<T>(data),
                              [&](auto&& v) { _indices->data.emplace_back<V>(std::forward<decltype(v)>(v)); });
    }
    /** @brief Builder-style insert indices. */
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    auto&& with_indices(this auto&& self, T&& data) {
        self.template insert_indices<V>(std::forward<T>(data));
        return std::forward<decltype(self)>(self);
    }
    /** @brief Get a const reference to the index data, if present. */
    std::optional<std::reference_wrapper<const MeshIndices>> get_indices() const {
        return _indices.transform([](const MeshIndices& indices) { return std::cref(indices); });
    }
    /** @brief Get a mutable reference to the index data, if present. */
    std::optional<std::reference_wrapper<MeshIndices>> get_indices_mut() {
        return _indices.transform([](MeshIndices& indices) { return std::ref(indices); });
    }
    /** @brief Remove and return the index data. */
    std::optional<MeshIndices> remove_indices() {
        std::optional<MeshIndices> res;
        std::swap(res, _indices);
        return res;
    }
    /** @brief Builder-style remove_indices with callback. */
    auto&& with_removed_indices(this auto&& self, std::invocable<std::optional<MeshIndices>> auto&& callback) {
        callback(self.remove_indices());
        return std::forward<decltype(self)>(self);
    }
    /** @brief Builder-style remove_indices (silently discards). */
    auto&& with_removed_indices(this auto&& self) {
        self.remove_indices();
        return std::forward<decltype(self)>(self);
    }

    /** @brief Count vertices (not indices) in the mesh. */
    std::size_t count_vertices() const {
        if (_attributes.empty()) {
            return 0;
        }
        std::optional<std::size_t> count;
        for (auto&& [slot, attribute_data] : _attributes) {
            std::size_t attribute_count = attribute_data.data.size();
            if (count.has_value() && attribute_count != *count) {
                spdlog::warn("Mesh::count_vertices(): attribute [{}:{}] has different count with previous ({} vs {})",
                             slot, attribute_data.attribute.name, *count, attribute_count);
            }
            count = count.has_value() ? std::min(*count, attribute_count) : attribute_count;
        }
        return count.value_or(0);
    };

   private:
    wgpu::PrimitiveTopology primitive_type;
    std::map<std::size_t, MeshAttributeData> _attributes;
    std::optional<MeshIndices> _indices;
};
export const MeshAttribute Mesh::ATTRIBUTE_POSITION{"position", 0, wgpu::VertexFormat::eFloat32x3};
export const MeshAttribute Mesh::ATTRIBUTE_COLOR{"color", 1, wgpu::VertexFormat::eFloat32x4};
export const MeshAttribute Mesh::ATTRIBUTE_NORMAL{"normal", 2, wgpu::VertexFormat::eFloat32x3};
export const MeshAttribute Mesh::ATTRIBUTE_UV0{"uv0", 3, wgpu::VertexFormat::eFloat32x2};
export const MeshAttribute Mesh::ATTRIBUTE_UV1{"uv1", 4, wgpu::VertexFormat::eFloat32x2};

/** @brief Create a circle mesh centered at origin with given radius.
 * @param segment_count Number of line segments; auto-calculated if not provided.
 */
export Mesh make_circle(float radius,
                        std::optional<glm::vec4> color             = std::nullopt,
                        std::optional<std::uint32_t> segment_count = std::nullopt);
/** @brief Create a box mesh on the XY plane. */
export Mesh make_box2d(float width, float height, std::optional<glm::vec4> color = std::nullopt);
/** @brief Create a box mesh on the XY plane with UV coordinates. */
export Mesh make_box2d_uv(float width,
                          float height,
                          glm::vec4 uv_rect                     = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                          std::optional<glm::vec4> vertex_color = std::nullopt);

/** @brief Plugin that registers mesh asset loading and GPU upload systems. */
export struct MeshPlugin {
    void build(core::App& app);
};
}  // namespace mesh