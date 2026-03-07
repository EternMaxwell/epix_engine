module;

#include <spdlog/spdlog.h>

export module epix.mesh:mesh;

import std;
import webgpu;
import epix.core;
import glm;

namespace mesh {
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
export enum class MeshError {
    /// Indicates no attribute at given slot.
    SlotNotFound,
    /// Indicates attribute name does not match the current one at given slot.
    NameMismatch,
    /// Indicates the type of provided data is incompatible with the attribute format. Or the format is compressed.
    TypeIncompatible,
    /// Indicates the format(type) of stored attribute does not match the requested one.
    TypeMismatch,
};
export struct MeshAttribute {
    std::string name;
    std::uint32_t slot;
    wgpu::VertexFormat format;

    bool operator==(const MeshAttribute& other) const {
        return name == other.name && slot == other.slot && format == other.format;
    }
};
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
    void add_attribute(const MeshAttribute& attribute) { this->insert_or_assign(attribute.slot, attribute); }
    std::optional<std::reference_wrapper<const MeshAttribute>> get_attribute(const MeshAttribute& attribute) const {
        auto it = this->find(attribute.slot);
        if (it != this->end() && it->second == attribute) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const MeshAttribute>> get_attribute(std::uint32_t slot) const {
        auto it = this->find(slot);
        if (it != this->end()) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<MeshAttribute>> get_attribute_mut(const MeshAttribute& attribute) {
        auto it = this->find(attribute.slot);
        if (it != this->end() && it->second == attribute) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<MeshAttribute>> get_attribute_mut(std::uint32_t slot) {
        auto it = this->find(slot);
        if (it != this->end()) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
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
export struct MeshAttributeData {
    MeshAttribute attribute;
    core::untyped_vector data;

    std::size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
};
export struct MeshIndices {
   public:
    MeshIndices(const meta::type_info& desc) : data(desc) {}
    MeshIndices(core::untyped_vector&& vec) : data(std::move(vec)) {}

    bool is_u16() const { return data.type_info() == meta::type_info::of<std::uint16_t>(); }
    bool is_u32() const { return data.type_info() == meta::type_info::of<std::uint32_t>(); }
    std::span<const std::uint16_t> as_u16() const { return data.cspan_as<std::uint16_t>(); }
    std::span<const std::uint32_t> as_u32() const { return data.cspan_as<std::uint32_t>(); }
    std::size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }

   public:
    core::untyped_vector data;
};
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

    wgpu::PrimitiveTopology get_primitive_type() const { return primitive_type; }
    void set_primitive_type(wgpu::PrimitiveTopology type) { primitive_type = type; }
    auto&& with_primitive_type(this auto&& self, wgpu::PrimitiveTopology type) {
        self.set_primitive_type(type);
        return std::forward<decltype(self)>(self);
    }
    auto iter_attributes() const { return std::views::values(_attributes); }
    auto iter_attributes_mut() { return std::views::values(_attributes); }

    MeshAttributeLayout attribute_layout() const;

    /// Insert a new attribute with given data to the mesh or replace existing one regardless of type.
    /// Caller should ensure the type of data is compatible with the attribute format, otherwise the behavior is
    /// undefined.
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
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    auto&& with_attribute(this auto&& self, MeshAttribute attribute, T&& data) {
        self.insert_attribute(attribute, std::forward<decltype(data)>(data));
        return std::forward<decltype(self)>(self);
    }
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(
        const MeshAttribute& attribute) const;
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(std::size_t slot) const;
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(
        const MeshAttribute& attribute);
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(std::size_t slot);
    std::expected<MeshAttributeData, MeshError> remove_attribute(const MeshAttribute& attribute);
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
    auto&& with_removed_attribute(this auto&& self,
                                  std::size_t slot,
                                  std::invocable<std::expected<MeshAttributeData, MeshError>> auto&& callback) {
        callback(self.remove_attribute(slot));
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(this auto&& self, std::size_t slot) {
        self.remove_attribute(slot);
        return std::forward<decltype(self)>(self);
    }
    bool contains_attribute(const MeshAttribute& attribute) const { return get_attribute(attribute).has_value(); }
    bool contains_attribute(std::size_t slot) const { return get_attribute(slot).has_value(); }

    /// Insert indices to mesh or replace existing ones regardless of type. The type should be either std::uint16_t or
    /// std::uint32_t.
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
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    auto&& with_indices(this auto&& self, T&& data) {
        self.template insert_indices<V>(std::forward<T>(data));
        return std::forward<decltype(self)>(self);
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
    auto&& with_removed_indices(this auto&& self, std::invocable<std::optional<MeshIndices>> auto&& callback) {
        callback(self.remove_indices());
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_indices(this auto&& self) {
        self.remove_indices();
        return std::forward<decltype(self)>(self);
    }

    /// Count and return the vertex count of the mesh, indices are not considered.
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

/// Make a circle mesh centered at (0,0,0) with given radius and segment count. segment count is the number of line
/// segments used to approximate the circle, if not provided, it will be calculated based on radius.
export Mesh make_circle(float radius,
                        std::optional<glm::vec4> color             = std::nullopt,
                        std::optional<std::uint32_t> segment_count = std::nullopt);
/// Make a box mesh centered at (0,0,0) on the XY plane with given width and height.
export Mesh make_box2d(float width, float height, std::optional<glm::vec4> color = std::nullopt);
/// Make a box mesh centered at (0,0,0) on the XY plane with uv coordinates.
export Mesh make_box2d_uv(float width,
                          float height,
                          glm::vec4 uv_rect                     = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                          std::optional<glm::vec4> vertex_color = std::nullopt);

export struct MeshPlugin {
    void build(core::App& app);
};
}  // namespace mesh