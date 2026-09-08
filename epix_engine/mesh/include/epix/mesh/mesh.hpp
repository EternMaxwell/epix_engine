#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <epix/meta.hpp>
#include <epix/assets/render_asset_usages.hpp>
#include <expected>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::mesh {
constexpr std::size_t vertex_format_size(wgpu::VertexFormat format) noexcept {
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
/** @brief Error from accessing mesh vertex attributes or indices. */
EPIX_EXPORT enum class MeshAccessError {
    /** @brief The mesh payload was transferred to the render world. */
    ExtractedToRenderWorld,
    /** @brief The requested vertex or index data is absent. */
    NotFound,
};
/** @brief Describes one named vertex attribute and its stable mesh ID. */
EPIX_EXPORT struct MeshVertexAttribute {
    std::string_view name;
    MeshVertexAttributeId id;
    wgpu::VertexFormat format;

    operator MeshVertexAttributeId() const noexcept { return id; }

    VertexAttributeDescriptor at_shader_location(std::uint32_t shader_location) const noexcept {
        return VertexAttributeDescriptor{
            .shader_location = shader_location,
            .id              = id,
            .name            = name,
        };
    }

    bool operator==(const MeshVertexAttribute&) const noexcept = default;
};
/** @brief Ordered map of ID to attribute describing the complete vertex layout. */
EPIX_EXPORT struct MeshAttributeLayout : std::map<MeshVertexAttributeId, MeshVertexAttribute> {
    wgpu::PrimitiveTopology primitive_type = wgpu::PrimitiveTopology::eTriangleList;

    bool operator==(const MeshAttributeLayout& other) const noexcept {
        return primitive_type == other.primitive_type &&
               static_cast<const std::map<MeshVertexAttributeId, MeshVertexAttribute>&>(*this) ==
                   static_cast<const std::map<MeshVertexAttributeId, MeshVertexAttribute>&>(other);
    }

    bool contains_attribute(const MeshVertexAttribute& attribute) const noexcept {
        auto it = this->find(attribute.id);
        return it != this->end() && it->second == attribute;
    }
    /** @brief Add or replace an attribute in the layout. */
    void add_attribute(const MeshVertexAttribute& attribute) { this->insert_or_assign(attribute.id, attribute); }
    /** @brief Get a const reference to an attribute by descriptor. */
    std::optional<std::reference_wrapper<const MeshVertexAttribute>> get_attribute(
        const MeshVertexAttribute& attribute) const {
        auto it = this->find(attribute.id);
        if (it != this->end() && it->second == attribute) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a const reference to an attribute by ID. */
    std::optional<std::reference_wrapper<const MeshVertexAttribute>> get_attribute(MeshVertexAttributeId id) const {
        auto it = this->find(id);
        if (it != this->end()) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a mutable reference to an attribute by descriptor. */
    std::optional<std::reference_wrapper<MeshVertexAttribute>> get_attribute_mut(const MeshVertexAttribute& attribute) {
        auto it = this->find(attribute.id);
        if (it != this->end() && it->second == attribute) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a mutable reference to an attribute by ID. */
    std::optional<std::reference_wrapper<MeshVertexAttribute>> get_attribute_mut(MeshVertexAttributeId id) {
        auto it = this->find(id);
        if (it != this->end()) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a human-readable string representation of this layout. */
    std::string to_string() const {
        std::stringstream ss;
        std::println(ss, "MeshAttributeLayout {{ primitive_type = {}", wgpu::to_string(primitive_type));
        for (const auto& [id, attribute] : *this) {
            std::println(ss, "  Id {}: Name='{}', Format={}", id.value, attribute.name,
                         wgpu::to_string(attribute.format));
        }
        std::println(ss, "}}");
        return ss.str();
    }
};
/** @brief Pairs a MeshVertexAttribute descriptor with its raw vertex data buffer. */
EPIX_EXPORT struct MeshAttributeData {
    MeshVertexAttribute attribute;
    ecs::untyped_vector data;

    std::size_t size() const noexcept { return data.size(); }
    bool empty() const noexcept { return data.empty(); }
};
/** @brief Index buffer data, stored as either uint16 or uint32 values. */
EPIX_EXPORT struct MeshIndices {
   public:
    MeshIndices(const meta::type_info& desc) noexcept : data(desc) {}
    MeshIndices(ecs::untyped_vector&& vec) noexcept : data(std::move(vec)) {}

    /** @brief Check if this is a uint16 index buffer. */
    bool is_u16() const noexcept { return data.type_info() == meta::type_info::of<std::uint16_t>(); }
    /** @brief Check if this is a uint32 index buffer. */
    bool is_u32() const noexcept { return data.type_info() == meta::type_info::of<std::uint32_t>(); }
    /** @brief Get the index data as a span of uint16 values. */
    std::span<const std::uint16_t> as_u16() const noexcept { return data.cspan_as<std::uint16_t>(); }
    /** @brief Get the index data as a span of uint32 values. */
    std::span<const std::uint32_t> as_u32() const noexcept { return data.cspan_as<std::uint32_t>(); }
    /** @brief Number of indices. */
    std::size_t size() const noexcept { return data.size(); }
    /** @brief Whether the index buffer is empty. */
    bool empty() const noexcept { return data.empty(); }

   public:
    ecs::untyped_vector data;
};

namespace detail {
struct ExtractedToRenderWorld {};

struct ConstMeshAttributeRefs {
    auto operator()(const MeshAttributeData& data) const {
        return std::pair<const MeshVertexAttribute&, const ecs::untyped_vector&>{data.attribute, data.data};
    }
};

struct MeshAttributeRefs {
    auto operator()(MeshAttributeData& data) const {
        return std::pair<const MeshVertexAttribute&, ecs::untyped_vector&>{data.attribute, data.data};
    }
};

/** @brief Storage that distinguishes present, absent, and extracted mesh data.
 *
 * This is the C++ counterpart of Bevy 0.18's private
 * `MeshExtractableData<T>` state machine.
 */
template <typename T>
class MeshExtractableData {
   public:
    static MeshExtractableData data(T value) {
        MeshExtractableData result;
        result._value.template emplace<1>(std::move(value));
        return result;
    }

    static MeshExtractableData extracted_to_render_world() {
        MeshExtractableData result;
        result._value.template emplace<2>();
        return result;
    }

    std::expected<std::reference_wrapper<const T>, MeshAccessError> as_ref() const {
        if (const auto* value = std::get_if<T>(&_value)) return std::cref(*value);
        return std::unexpected(std::holds_alternative<ExtractedToRenderWorld>(_value)
                                   ? MeshAccessError::ExtractedToRenderWorld
                                   : MeshAccessError::NotFound);
    }

    std::expected<std::optional<std::reference_wrapper<const T>>, MeshAccessError> as_ref_option() const {
        if (const auto* value = std::get_if<T>(&_value)) return std::optional{std::cref(*value)};
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        return std::nullopt;
    }

    std::expected<std::reference_wrapper<T>, MeshAccessError> as_mut() {
        if (auto* value = std::get_if<T>(&_value)) return std::ref(*value);
        return std::unexpected(std::holds_alternative<ExtractedToRenderWorld>(_value)
                                   ? MeshAccessError::ExtractedToRenderWorld
                                   : MeshAccessError::NotFound);
    }

    std::expected<std::optional<std::reference_wrapper<T>>, MeshAccessError> as_mut_option() {
        if (auto* value = std::get_if<T>(&_value)) return std::optional{std::ref(*value)};
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        return std::nullopt;
    }

    std::expected<MeshExtractableData, MeshAccessError> extract() {
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        MeshExtractableData extracted = std::move(*this);
        _value.template emplace<2>();
        return extracted;
    }

    std::expected<std::optional<T>, MeshAccessError> replace(std::optional<T> value) {
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        std::optional<T> previous;
        if (auto* current = std::get_if<T>(&_value)) previous.emplace(std::move(*current));
        if (value) {
            _value.template emplace<1>(std::move(*value));
        } else {
            _value.template emplace<0>();
        }
        return previous;
    }

   private:
    std::variant<std::monostate, T, ExtractedToRenderWorld> _value;
};
}  // namespace detail
/** @brief CPU-side mesh asset storing vertex attributes and optional index data.
 *
 * Use insert_attribute/with_inserted_attribute to add vertex data and
 * insert_indices/with_inserted_indices to add index data. Standard attribute constants
 * (ATTRIBUTE_POSITION, etc.) are provided.
 */
EPIX_EXPORT struct Mesh {
   public:
    using AttributeMap = std::map<MeshVertexAttributeId, MeshAttributeData>;

    static inline const MeshVertexAttribute ATTRIBUTE_POSITION{"Vertex_Position", MeshVertexAttributeId{0},
                                                               wgpu::VertexFormat::eFloat32x3};
    static inline const MeshVertexAttribute ATTRIBUTE_NORMAL{"Vertex_Normal", MeshVertexAttributeId{1},
                                                             wgpu::VertexFormat::eFloat32x3};
    static inline const MeshVertexAttribute ATTRIBUTE_UV_0{"Vertex_Uv", MeshVertexAttributeId{2},
                                                           wgpu::VertexFormat::eFloat32x2};
    static inline const MeshVertexAttribute ATTRIBUTE_UV_1{"Vertex_Uv_1", MeshVertexAttributeId{3},
                                                           wgpu::VertexFormat::eFloat32x2};
    static inline const MeshVertexAttribute ATTRIBUTE_TANGENT{"Vertex_Tangent", MeshVertexAttributeId{4},
                                                              wgpu::VertexFormat::eFloat32x4};
    static inline const MeshVertexAttribute ATTRIBUTE_COLOR{"Vertex_Color", MeshVertexAttributeId{5},
                                                            wgpu::VertexFormat::eFloat32x4};
    static inline const MeshVertexAttribute ATTRIBUTE_JOINT_WEIGHT{"Vertex_JointWeight", MeshVertexAttributeId{6},
                                                                   wgpu::VertexFormat::eFloat32x4};
    static inline const MeshVertexAttribute ATTRIBUTE_JOINT_INDEX{"Vertex_JointIndex", MeshVertexAttributeId{7},
                                                                  wgpu::VertexFormat::eUint16x4};
    static constexpr std::uint64_t FIRST_AVAILABLE_CUSTOM_ATTRIBUTE = 8;

   public:
    Mesh(wgpu::PrimitiveTopology primitive_type, assets::RenderAssetUsages asset_usage) noexcept
        : asset_usage(asset_usage),
          primitive_type(primitive_type),
          _attributes(detail::MeshExtractableData<AttributeMap>::data({})) {}
    Mesh(const Mesh&);
    Mesh(Mesh&&) = default;
    Mesh& operator=(const Mesh&);
    Mesh& operator=(Mesh&&) = default;

    /** @brief Worlds in which this mesh's asset data is retained. */
    assets::RenderAssetUsages asset_usage;

    /** @brief Get the primitive topology. */
    wgpu::PrimitiveTopology get_primitive_type() const noexcept { return primitive_type; }
    /** @brief Set the primitive topology. */
    void set_primitive_type(wgpu::PrimitiveTopology type) noexcept { primitive_type = type; }
    /** @brief Builder-style setter for primitive topology. */
    auto&& with_primitive_type(this auto&& self, wgpu::PrimitiveTopology type) {
        self.set_primitive_type(type);
        return std::forward<decltype(self)>(self);
    }
    /** @brief Fallible attribute iteration (Bevy `Mesh::try_attributes`). */
    auto try_attributes() const {
        using View        = decltype(std::views::values(std::declval<const AttributeMap&>()) |
                                     std::views::transform(detail::ConstMeshAttributeRefs{}));
        const auto values = _attributes.as_ref();
        if (!values) return std::expected<View, MeshAccessError>{std::unexpected(values.error())};
        return std::expected<View, MeshAccessError>{std::views::values(values->get()) |
                                                    std::views::transform(detail::ConstMeshAttributeRefs{})};
    }
    /** @brief Iterate over all attributes. Throws after render-world extraction. */
    auto attributes() const {
        auto result = try_attributes();
        if (!result) throw_access_error(result.error());
        return std::move(result).value();
    }
    /** @brief Fallible mutable attribute iteration (Bevy `Mesh::try_attributes_mut`). */
    auto try_attributes_mut() {
        using View  = decltype(std::views::values(std::declval<AttributeMap&>()) |
                               std::views::transform(detail::MeshAttributeRefs{}));
        auto values = _attributes.as_mut();
        if (!values) return std::expected<View, MeshAccessError>{std::unexpected(values.error())};
        return std::expected<View, MeshAccessError>{std::views::values(values->get()) |
                                                    std::views::transform(detail::MeshAttributeRefs{})};
    }
    /** @brief Iterate mutably over all attributes. Throws after render-world extraction. */
    auto attributes_mut() {
        auto result = try_attributes_mut();
        if (!result) throw_access_error(result.error());
        return std::move(result).value();
    }

    /** @brief Build a MeshAttributeLayout from the current attributes. */
    MeshAttributeLayout attribute_layout() const;

    /** @brief Get this mesh's interleaved vertex-buffer layout, interning it in
     * `mesh_vertex_buffer_layouts` (Bevy `Mesh::get_mesh_vertex_buffer_layout`).
     * The attribute IDs are in ascending ID order and each attribute's offset
     * accumulates the previous attribute sizes. */
    MeshVertexBufferLayoutRef get_mesh_vertex_buffer_layout(MeshVertexBufferLayouts& mesh_vertex_buffer_layouts) const;

    /** @brief Insert or replace an attribute. Throws on incompatible format or extracted data. */
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    void insert_attribute(MeshVertexAttribute attribute, T&& data) {
        auto result = try_insert_attribute(std::move(attribute), std::forward<T>(data));
        if (!result) throw_access_error(result.error());
    }
    /** @brief Fallible insertion. Format mismatch remains a programmer error, as in Bevy. */
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    std::expected<void, MeshAccessError> try_insert_attribute(MeshVertexAttribute attribute, T&& data) {
        using value_type = std::ranges::range_value_t<T>;
        if (vertex_format_size(attribute.format) != sizeof(value_type)) {
            throw std::invalid_argument("Mesh attribute data does not match its vertex format");
        }
        MeshAttributeData attribute_data{
            .attribute = attribute,
            .data      = std::ranges::to<ecs::untyped_vector>(std::forward<T>(data), meta::type_info::of<value_type>()),
        };
        auto attributes = _attributes.as_mut();
        if (!attributes) return std::unexpected(attributes.error());
        attributes->get().insert_or_assign(attribute.id, std::move(attribute_data));
        return {};
    }
    /** @brief Builder-style attribute insertion (Bevy `with_inserted_attribute`). */
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    auto&& with_inserted_attribute(this auto&& self, MeshVertexAttribute attribute, T&& data) {
        self.insert_attribute(std::move(attribute), std::forward<T>(data));
        return std::forward<decltype(self)>(self);
    }
    template <std::ranges::range T>
        requires(std::is_trivially_copyable_v<std::ranges::range_value_t<T>> &&
                 std::is_trivially_destructible_v<std::ranges::range_value_t<T>>)
    std::expected<Mesh, MeshAccessError> try_with_inserted_attribute(MeshVertexAttribute attribute, T&& data) && {
        auto result = try_insert_attribute(std::move(attribute), std::forward<T>(data));
        if (!result) return std::unexpected(result.error());
        return std::move(*this);
    }
    std::optional<std::reference_wrapper<const ecs::untyped_vector>> attribute(
        const MeshVertexAttribute& attribute) const;
    std::optional<std::reference_wrapper<const ecs::untyped_vector>> attribute(MeshVertexAttributeId id) const;
    std::expected<std::reference_wrapper<const ecs::untyped_vector>, MeshAccessError> try_attribute(
        const MeshVertexAttribute& attribute) const;
    std::expected<std::reference_wrapper<const ecs::untyped_vector>, MeshAccessError> try_attribute(
        MeshVertexAttributeId id) const;
    std::expected<std::optional<std::reference_wrapper<const ecs::untyped_vector>>, MeshAccessError>
    try_attribute_option(const MeshVertexAttribute& attribute) const;
    std::expected<std::optional<std::reference_wrapper<const ecs::untyped_vector>>, MeshAccessError>
    try_attribute_option(MeshVertexAttributeId id) const;

    std::optional<std::reference_wrapper<ecs::untyped_vector>> attribute_mut(const MeshVertexAttribute& attribute);
    std::optional<std::reference_wrapper<ecs::untyped_vector>> attribute_mut(MeshVertexAttributeId id);
    std::expected<std::reference_wrapper<ecs::untyped_vector>, MeshAccessError> try_attribute_mut(
        const MeshVertexAttribute& attribute);
    std::expected<std::reference_wrapper<ecs::untyped_vector>, MeshAccessError> try_attribute_mut(
        MeshVertexAttributeId id);
    std::expected<std::optional<std::reference_wrapper<ecs::untyped_vector>>, MeshAccessError> try_attribute_mut_option(
        const MeshVertexAttribute& attribute);
    std::expected<std::optional<std::reference_wrapper<ecs::untyped_vector>>, MeshAccessError> try_attribute_mut_option(
        MeshVertexAttributeId id);

    std::optional<ecs::untyped_vector> remove_attribute(const MeshVertexAttribute& attribute);
    std::optional<ecs::untyped_vector> remove_attribute(MeshVertexAttributeId id);
    std::expected<ecs::untyped_vector, MeshAccessError> try_remove_attribute(const MeshVertexAttribute& attribute);
    std::expected<ecs::untyped_vector, MeshAccessError> try_remove_attribute(MeshVertexAttributeId id);
    auto&& with_removed_attribute(this auto&& self, const MeshVertexAttribute& attribute) {
        self.remove_attribute(attribute);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(this auto&& self, MeshVertexAttributeId id) {
        self.remove_attribute(id);
        return std::forward<decltype(self)>(self);
    }
    std::expected<Mesh, MeshAccessError> try_with_removed_attribute(const MeshVertexAttribute& attribute) &&;
    std::expected<Mesh, MeshAccessError> try_with_removed_attribute(MeshVertexAttributeId id) &&;

    bool contains_attribute(const MeshVertexAttribute& attribute) const;
    bool contains_attribute(MeshVertexAttributeId id) const;
    std::expected<bool, MeshAccessError> try_contains_attribute(const MeshVertexAttribute& attribute) const;
    std::expected<bool, MeshAccessError> try_contains_attribute(MeshVertexAttributeId id) const;

    /** @brief Insert indices, replacing any existing ones.
     *  @tparam V Index type (`std::uint16_t` or `std::uint32_t`). */
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    void insert_indices(T&& data) {
        auto result = try_insert_indices<V>(std::forward<T>(data));
        if (!result) throw_access_error(result.error());
    }
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    std::expected<void, MeshAccessError> try_insert_indices(T&& data) {
        MeshIndices indices(meta::type_info::of<V>());
        if constexpr (std::ranges::sized_range<T>) {
            indices.data.reserve(static_cast<std::size_t>(std::ranges::size(data)));
        }
        std::ranges::for_each(std::forward<T>(data),
                              [&](auto&& v) { indices.data.emplace_back<V>(std::forward<decltype(v)>(v)); });
        auto replaced = _indices.replace(std::optional<MeshIndices>{std::move(indices)});
        if (!replaced) return std::unexpected(replaced.error());
        return {};
    }
    /** @brief Builder-style insert indices (Bevy `with_inserted_indices`). */
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    auto&& with_inserted_indices(this auto&& self, T&& data) {
        self.template insert_indices<V>(std::forward<T>(data));
        return std::forward<decltype(self)>(self);
    }
    template <typename V = std::uint16_t, std::ranges::range T>
        requires std::convertible_to<std::ranges::range_value_t<T>, V> &&
                 (std::same_as<V, std::uint16_t> || std::same_as<V, std::uint32_t>)
    std::expected<Mesh, MeshAccessError> try_with_inserted_indices(T&& data) && {
        auto result = try_insert_indices<V>(std::forward<T>(data));
        if (!result) return std::unexpected(result.error());
        return std::move(*this);
    }

    std::optional<std::reference_wrapper<const MeshIndices>> indices() const;
    std::expected<std::reference_wrapper<const MeshIndices>, MeshAccessError> try_indices() const;
    std::expected<std::optional<std::reference_wrapper<const MeshIndices>>, MeshAccessError> try_indices_option() const;
    std::optional<std::reference_wrapper<MeshIndices>> indices_mut();
    std::expected<std::reference_wrapper<MeshIndices>, MeshAccessError> try_indices_mut();
    std::expected<std::optional<std::reference_wrapper<MeshIndices>>, MeshAccessError> try_indices_mut_option();

    std::optional<MeshIndices> remove_indices();
    std::expected<std::optional<MeshIndices>, MeshAccessError> try_remove_indices();
    auto&& with_removed_indices(this auto&& self) {
        self.remove_indices();
        return std::forward<decltype(self)>(self);
    }
    std::expected<Mesh, MeshAccessError> try_with_removed_indices() &&;

    /** @brief Count vertices (not indices) in the mesh. */
    std::size_t count_vertices() const {
        std::optional<std::size_t> count;
        const auto stored_attributes = _attributes.as_ref();
        if (!stored_attributes) throw_access_error(stored_attributes.error());
        for (auto&& [id, attribute_data] : stored_attributes->get()) {
            std::size_t attribute_count = attribute_data.data.size();
            if (count.has_value() && attribute_count != *count) {
                spdlog::warn("Mesh::count_vertices(): attribute [{}:{}] has different count with previous ({} vs {})",
                             id.value, attribute_data.attribute.name, *count, attribute_count);
            }
            count = count.has_value() ? std::min(*count, attribute_count) : attribute_count;
        }
        return count.value_or(0);
    };

    /** @brief Move the vertex/index payload into a render-world copy while
     * retaining this asset's metadata (Bevy `Mesh::take_gpu_data`). */
    std::expected<Mesh, MeshAccessError> take_gpu_data();

   private:
    [[noreturn]] static void throw_access_error(MeshAccessError error);

    wgpu::PrimitiveTopology primitive_type;
    detail::MeshExtractableData<AttributeMap> _attributes;
    detail::MeshExtractableData<MeshIndices> _indices;
};
/** @brief Create a circle mesh centered at origin with given radius.
 * @param segment_count Number of line segments; auto-calculated if not provided.
 */
EPIX_EXPORT Mesh make_circle(float radius,
                             std::optional<glm::vec4> color             = std::nullopt,
                             std::optional<std::uint32_t> segment_count = std::nullopt);
/** @brief Create a box mesh on the XY plane. */
EPIX_EXPORT Mesh make_box2d(float width, float height, std::optional<glm::vec4> color = std::nullopt);
/** @brief Create a box mesh on the XY plane with UV coordinates. */
EPIX_EXPORT Mesh make_box2d_uv(float width,
                               float height,
                               glm::vec4 uv_rect                     = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                               std::optional<glm::vec4> vertex_color = std::nullopt);

/** @brief Plugin that registers mesh asset loading and GPU upload systems. */
EPIX_EXPORT struct MeshPlugin {
    void attach(app::App& app);
};
}  // namespace epix::mesh
