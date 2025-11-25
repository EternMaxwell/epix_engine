#pragma once

#include <epix/core.hpp>
#include <epix/render/vulkan.hpp>
#include <glm/glm.hpp>
#include <map>
#include <ranges>
#include <type_traits>

namespace epix::mesh {
enum class MeshError {
    /// Indicates no attribute at given slot.
    SlotNotFound,
    /// Indicates attribute name does not match the current one at given slot.
    NameMismatch,
    /// Indicates the type of provided data is incompatible with the attribute format. Or the format is compressed.
    TypeIncompatible,
    /// Indicates the format(type) of stored attribute does not match the requested one.
    TypeMismatch,
};
struct MeshAttribute {
    std::string name;
    uint32_t slot;
    nvrhi::Format format;

    bool operator==(const MeshAttribute& other) const {
        return name == other.name && slot == other.slot && format == other.format;
    }
};
struct MeshAttributeLayout : std::map<uint32_t, MeshAttribute> {
    nvrhi::PrimitiveType primitive_type;
    bool contains_attribute(const MeshAttribute& attribute) const {
        auto it = this->find(attribute.slot);
        return it != this->end() && it->second == attribute;
    }
    std::optional<std::reference_wrapper<const MeshAttribute>> get_attribute(const MeshAttribute& attribute) const {
        auto it = this->find(attribute.slot);
        if (it != this->end() && it->second == attribute) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const MeshAttribute>> get_attribute(uint32_t slot) const {
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
    std::optional<std::reference_wrapper<MeshAttribute>> get_attribute_mut(uint32_t slot) {
        auto it = this->find(slot);
        if (it != this->end()) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    std::string to_string() const {
        std::stringstream ss;
        std::println(ss, "MeshAttributeLayout {{");
        for (const auto& [slot, attribute] : *this) {
            std::println(ss, "  Slot {}: Name='{}', Format={}", slot, attribute.name,
                         nvrhi::getFormatInfo(attribute.format).name);
        }
        std::println(ss, "}}");
        return ss.str();
    }
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
    Mesh() : primitive_type(nvrhi::PrimitiveType::TriangleList) {}
    Mesh(nvrhi::PrimitiveType primitive_type) : primitive_type(primitive_type) {}
    Mesh(const Mesh&)            = delete;
    Mesh(Mesh&&)                 = default;
    Mesh& operator=(const Mesh&) = delete;
    Mesh& operator=(Mesh&&)      = default;

    nvrhi::PrimitiveType get_primitive_type() const { return primitive_type; }
    void set_primitive_type(nvrhi::PrimitiveType type) { primitive_type = type; }
    auto&& with_primitive_type(this auto&& self, nvrhi::PrimitiveType type) {
        self.set_primitive_type(type);
        return std::forward<decltype(self)>(self);
    }
    auto iter_attributes() const { return std::views::values(_attributes); }
    auto iter_attributes_mut() { return std::views::values(_attributes); }

    MeshAttributeLayout attribute_layout() const;

    /// Insert a new attribute with given data to the mesh or replace existing one regardless of type.
    template <std::ranges::viewable_range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
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
    template <std::ranges::viewable_range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    auto&& with_attribute(this auto&& self,
                          MeshAttribute attribute,
                          T&& data,
                          std::invocable<std::expected<void, MeshError>> auto&& callback) {
        callback(self.insert_attribute(attribute, std::forward<decltype(data)>(data)));
        return std::forward<decltype(self)>(self);
    }
    template <std::ranges::viewable_range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    auto&& with_attribute(this auto&& self, MeshAttribute attribute, T&& data) {
        self.insert_attribute(attribute, std::forward<decltype(data)>(data));
        return std::forward<decltype(self)>(self);
    }
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(
        const MeshAttribute& attribute) const;
    std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> get_attribute(size_t slot) const;
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(
        const MeshAttribute& attribute);
    std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> get_attribute_mut(size_t slot);
    std::expected<MeshAttributeData, MeshError> remove_attribute(const MeshAttribute& attribute);
    std::expected<MeshAttributeData, MeshError> remove_attribute(size_t slot);
    auto&& with_removed_attribute(
        this auto&& self,
        const MeshAttribute& attribute,
        std::invocable<std::expected<MeshAttributeData, MeshError>> auto&& callback =
            [](std::expected<MeshAttributeData, MeshError>) {}) {
        callback(self.remove_attribute(attribute));
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(this auto&& self, MeshAttribute attribute) {
        self.remove_attribute(attribute);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(
        this auto&& self,
        size_t slot,
        std::invocable<std::expected<MeshAttributeData, MeshError>> auto&& callback =
            [](std::expected<MeshAttributeData, MeshError>) {}) {
        callback(self.remove_attribute(slot));
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(this auto&& self, size_t slot) {
        self.remove_attribute(slot);
        return std::forward<decltype(self)>(self);
    }
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
    template <typename V = uint16_t, std::ranges::viewable_range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, uint16_t> || std::same_as<V, uint32_t>)
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
    auto&& with_removed_indices(
        this auto&& self,
        std::invocable<std::optional<MeshIndices>> auto&& callback = [](std::optional<MeshIndices>) {}) {
        callback(self.remove_indices());
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_indices(this auto&& self) {
        self.remove_indices();
        return std::forward<decltype(self)>(self);
    }

    /// Count and return the vertex count of the mesh, indices are not considered.
    size_t count_vertices() const {
        if (_attributes.empty()) {
            return 0;
        }
        std::optional<size_t> count;
        for (auto&& [slot, attribute_data] : _attributes) {
            size_t attribute_count = attribute_data.data.size();
            if (count.has_value() && attribute_count != *count) {
                spdlog::warn("Mesh::count_vertices(): attribute [{}:{}] has different count with previous ({} vs {})",
                             slot, attribute_data.attribute.name, *count, attribute_count);
            }
            count = count.has_value() ? std::min(*count, attribute_count) : attribute_count;
        }
        return count.value_or(0);
    };

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

/// Make a circle mesh centered at (0,0,0) with given radius and segment count. segment count is the number of line
/// segments used to approximate the circle, if not provided, it will be calculated based on radius.
Mesh make_circle(float radius,
                 std::optional<glm::vec4> color        = std::nullopt,
                 std::optional<uint32_t> segment_count = std::nullopt);
/// Make a box mesh centered at (0,0,0) on the XY plane with given width and height.
Mesh make_box2d(float width, float height, std::optional<glm::vec4> color = std::nullopt);

struct MeshPlugin {
    void build(epix::App& app);
};
}  // namespace epix::mesh