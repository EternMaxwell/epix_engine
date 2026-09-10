#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/meta.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::mesh {

class VertexAttributeValues;

/** @brief Error produced when a tagged vertex-attribute value cannot be
 * converted to the requested vector type (Bevy `FromVertexAttributeError`). */
EPIX_EXPORT struct FromVertexAttributeError {
    std::shared_ptr<VertexAttributeValues> from;
    std::string_view variant;
    std::string_view into;

    std::string to_string() const;
};

namespace detail {
template <typename Target>
std::expected<std::vector<Target>, FromVertexAttributeError> try_convert_vertex_attribute_values(
    VertexAttributeValues&& values);
}

/** @brief Semantically tagged arrays of per-vertex values (Bevy
 * `VertexAttributeValues`).
 *
 * Alternatives with the same CPU element representation remain distinct so,
 * for example, signed and normalized signed data cannot be confused merely
 * because both use `std::array<std::int16_t, N>`.
 */
EPIX_EXPORT class VertexAttributeValues {
   public:
    using Float32x2Value = std::array<float, 2>;
    using Float32x3Value = std::array<float, 3>;
    using Float32x4Value = std::array<float, 4>;
    using Sint32x2Value  = std::array<std::int32_t, 2>;
    using Sint32x3Value  = std::array<std::int32_t, 3>;
    using Sint32x4Value  = std::array<std::int32_t, 4>;
    using Uint32x2Value  = std::array<std::uint32_t, 2>;
    using Uint32x3Value  = std::array<std::uint32_t, 3>;
    using Uint32x4Value  = std::array<std::uint32_t, 4>;
    using Sint16x2Value  = std::array<std::int16_t, 2>;
    using Sint16x4Value  = std::array<std::int16_t, 4>;
    using Uint16x2Value  = std::array<std::uint16_t, 2>;
    using Uint16x4Value  = std::array<std::uint16_t, 4>;
    using Sint8x2Value   = std::array<std::int8_t, 2>;
    using Sint8x4Value   = std::array<std::int8_t, 4>;
    using Uint8x2Value   = std::array<std::uint8_t, 2>;
    using Uint8x4Value   = std::array<std::uint8_t, 4>;

#define EPIX_VERTEX_ATTRIBUTE_ALTERNATIVES(X) \
    X(Float32, float)                         \
    X(Sint32, std::int32_t)                   \
    X(Uint32, std::uint32_t)                  \
    X(Float32x2, Float32x2Value)              \
    X(Sint32x2, Sint32x2Value)                \
    X(Uint32x2, Uint32x2Value)                \
    X(Float32x3, Float32x3Value)              \
    X(Sint32x3, Sint32x3Value)                \
    X(Uint32x3, Uint32x3Value)                \
    X(Float32x4, Float32x4Value)              \
    X(Sint32x4, Sint32x4Value)                \
    X(Uint32x4, Uint32x4Value)                \
    X(Sint16x2, Sint16x2Value)                \
    X(Snorm16x2, Sint16x2Value)               \
    X(Uint16x2, Uint16x2Value)                \
    X(Unorm16x2, Uint16x2Value)               \
    X(Sint16x4, Sint16x4Value)                \
    X(Snorm16x4, Sint16x4Value)               \
    X(Uint16x4, Uint16x4Value)                \
    X(Unorm16x4, Uint16x4Value)               \
    X(Sint8x2, Sint8x2Value)                  \
    X(Snorm8x2, Sint8x2Value)                 \
    X(Uint8x2, Uint8x2Value)                  \
    X(Unorm8x2, Uint8x2Value)                 \
    X(Sint8x4, Sint8x4Value)                  \
    X(Snorm8x4, Sint8x4Value)                 \
    X(Uint8x4, Uint8x4Value)                  \
    X(Unorm8x4, Uint8x4Value)

#define EPIX_DECLARE_VERTEX_ATTRIBUTE_ALTERNATIVE(Name, Element) \
    struct Name {                                                \
        using value_type = Element;                              \
        std::vector<Element> values;                             \
        bool operator==(const Name&) const = default;            \
    };
    EPIX_VERTEX_ATTRIBUTE_ALTERNATIVES(EPIX_DECLARE_VERTEX_ATTRIBUTE_ALTERNATIVE)
#undef EPIX_DECLARE_VERTEX_ATTRIBUTE_ALTERNATIVE

    using Variant = std::variant<Float32,
                                 Sint32,
                                 Uint32,
                                 Float32x2,
                                 Sint32x2,
                                 Uint32x2,
                                 Float32x3,
                                 Sint32x3,
                                 Uint32x3,
                                 Float32x4,
                                 Sint32x4,
                                 Uint32x4,
                                 Sint16x2,
                                 Snorm16x2,
                                 Uint16x2,
                                 Unorm16x2,
                                 Sint16x4,
                                 Snorm16x4,
                                 Uint16x4,
                                 Unorm16x4,
                                 Sint8x2,
                                 Snorm8x2,
                                 Uint8x2,
                                 Unorm8x2,
                                 Sint8x4,
                                 Snorm8x4,
                                 Uint8x4,
                                 Unorm8x4>;

#define EPIX_DECLARE_VERTEX_ATTRIBUTE_CONSTRUCTOR(Name, Element) \
    VertexAttributeValues(Name values) : values_(std::move(values)) {}
    EPIX_VERTEX_ATTRIBUTE_ALTERNATIVES(EPIX_DECLARE_VERTEX_ATTRIBUTE_CONSTRUCTOR)
#undef EPIX_DECLARE_VERTEX_ATTRIBUTE_CONSTRUCTOR

    VertexAttributeValues(std::vector<float> values) : VertexAttributeValues(Float32{std::move(values)}) {}
    VertexAttributeValues(std::vector<std::int32_t> values) : VertexAttributeValues(Sint32{std::move(values)}) {}
    VertexAttributeValues(std::vector<std::uint32_t> values) : VertexAttributeValues(Uint32{std::move(values)}) {}
    VertexAttributeValues(std::vector<Float32x2Value> values) : VertexAttributeValues(Float32x2{std::move(values)}) {}
    VertexAttributeValues(std::vector<Float32x3Value> values) : VertexAttributeValues(Float32x3{std::move(values)}) {}
    VertexAttributeValues(std::vector<Float32x4Value> values) : VertexAttributeValues(Float32x4{std::move(values)}) {}
    VertexAttributeValues(std::vector<Sint32x2Value> values) : VertexAttributeValues(Sint32x2{std::move(values)}) {}
    VertexAttributeValues(std::vector<Sint32x3Value> values) : VertexAttributeValues(Sint32x3{std::move(values)}) {}
    VertexAttributeValues(std::vector<Sint32x4Value> values) : VertexAttributeValues(Sint32x4{std::move(values)}) {}
    VertexAttributeValues(std::vector<Uint32x2Value> values) : VertexAttributeValues(Uint32x2{std::move(values)}) {}
    VertexAttributeValues(std::vector<Uint32x3Value> values) : VertexAttributeValues(Uint32x3{std::move(values)}) {}
    VertexAttributeValues(std::vector<Uint32x4Value> values) : VertexAttributeValues(Uint32x4{std::move(values)}) {}

    VertexAttributeValues(std::vector<glm::vec2> values);
    VertexAttributeValues(std::vector<glm::vec3> values);
    VertexAttributeValues(std::vector<glm::vec4> values);
    VertexAttributeValues(std::vector<glm::ivec2> values);
    VertexAttributeValues(std::vector<glm::ivec3> values);
    VertexAttributeValues(std::vector<glm::ivec4> values);
    VertexAttributeValues(std::vector<glm::uvec2> values);
    VertexAttributeValues(std::vector<glm::uvec3> values);
    VertexAttributeValues(std::vector<glm::uvec4> values);

    std::size_t len() const noexcept;
    bool is_empty() const noexcept { return len() == 0; }
    std::size_t enum_variant_index() const noexcept { return values_.index(); }
    std::string_view enum_variant_name() const noexcept;
    wgpu::VertexFormat format() const noexcept;
    explicit operator wgpu::VertexFormat() const noexcept { return format(); }

    const std::vector<Float32x3Value>* as_float3() const noexcept { return get_if<Float32x3>(); }
    std::span<const std::uint8_t> get_bytes() const noexcept;

    template <typename Alternative>
    const std::vector<typename Alternative::value_type>* get_if() const noexcept {
        if (const auto* alternative = std::get_if<Alternative>(&values_)) return &alternative->values;
        return nullptr;
    }

    template <typename Alternative>
    std::vector<typename Alternative::value_type>* get_if() noexcept {
        if (auto* alternative = std::get_if<Alternative>(&values_)) return &alternative->values;
        return nullptr;
    }

    template <typename Target>
    std::expected<std::vector<Target>, FromVertexAttributeError> try_into() && {
        return detail::try_convert_vertex_attribute_values<Target>(std::move(*this));
    }

    const Variant& variant() const noexcept { return values_; }
    Variant& variant() noexcept { return values_; }
    bool operator==(const VertexAttributeValues&) const = default;

   private:
    Variant values_;
};

#undef EPIX_VERTEX_ATTRIBUTE_ALTERNATIVES

namespace detail {

template <typename Target>
struct VertexAttributeConversion;

#define EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(Target, Alternative)                  \
    template <>                                                                             \
    struct VertexAttributeConversion<Target> {                                              \
        static std::optional<std::vector<Target>> convert(VertexAttributeValues&& values) { \
            auto* source = values.get_if<VertexAttributeValues::Alternative>();             \
            if (!source) return std::nullopt;                                               \
            return std::move(*source);                                                      \
        }                                                                                   \
    };

EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(float, Float32)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Float32x2Value, Float32x2)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Float32x3Value, Float32x3)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Float32x4Value, Float32x4)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(std::int32_t, Sint32)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint32x2Value, Sint32x2)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint32x3Value, Sint32x3)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint32x4Value, Sint32x4)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(std::uint32_t, Uint32)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint32x2Value, Uint32x2)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint32x3Value, Uint32x3)
EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint32x4Value, Uint32x4)

#undef EPIX_DEFINE_EXACT_VERTEX_ATTRIBUTE_CONVERSION

#define EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(Target, First, Second)                 \
    template <>                                                                             \
    struct VertexAttributeConversion<Target> {                                              \
        static std::optional<std::vector<Target>> convert(VertexAttributeValues&& values) { \
            if (auto* source = values.get_if<VertexAttributeValues::First>()) {             \
                return std::move(*source);                                                  \
            }                                                                               \
            if (auto* source = values.get_if<VertexAttributeValues::Second>()) {            \
                return std::move(*source);                                                  \
            }                                                                               \
            return std::nullopt;                                                            \
        }                                                                                   \
    };

EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint16x2Value, Sint16x2, Snorm16x2)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint16x4Value, Sint16x4, Snorm16x4)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint16x2Value, Uint16x2, Unorm16x2)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint16x4Value, Uint16x4, Unorm16x4)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint8x2Value, Sint8x2, Snorm8x2)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Sint8x4Value, Sint8x4, Snorm8x4)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint8x2Value, Uint8x2, Unorm8x2)
EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION(VertexAttributeValues::Uint8x4Value, Uint8x4, Unorm8x4)

#undef EPIX_DEFINE_DUAL_VERTEX_ATTRIBUTE_CONVERSION

template <typename Vector, typename Array>
std::vector<Vector> vertex_arrays_to_vectors(std::vector<Array>&& source) {
    std::vector<Vector> result;
    result.reserve(source.size());
    for (const auto& value : source) {
        if constexpr (std::tuple_size_v<Array> == 2) {
            result.emplace_back(value[0], value[1]);
        } else if constexpr (std::tuple_size_v<Array> == 3) {
            result.emplace_back(value[0], value[1], value[2]);
        } else {
            result.emplace_back(value[0], value[1], value[2], value[3]);
        }
    }
    return result;
}

#define EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(Target, Array)                       \
    template <>                                                                             \
    struct VertexAttributeConversion<Target> {                                              \
        static std::optional<std::vector<Target>> convert(VertexAttributeValues&& values) { \
            auto source = VertexAttributeConversion<Array>::convert(std::move(values));     \
            if (!source) return std::nullopt;                                               \
            return vertex_arrays_to_vectors<Target>(std::move(*source));                    \
        }                                                                                   \
    };

EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::vec2, VertexAttributeValues::Float32x2Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::vec3, VertexAttributeValues::Float32x3Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::vec4, VertexAttributeValues::Float32x4Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::ivec2, VertexAttributeValues::Sint32x2Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::ivec3, VertexAttributeValues::Sint32x3Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::ivec4, VertexAttributeValues::Sint32x4Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::uvec2, VertexAttributeValues::Uint32x2Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::uvec3, VertexAttributeValues::Uint32x3Value)
EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION(glm::uvec4, VertexAttributeValues::Uint32x4Value)

#undef EPIX_DEFINE_VECTOR_VERTEX_ATTRIBUTE_CONVERSION

template <typename Target>
std::expected<std::vector<Target>, FromVertexAttributeError> try_convert_vertex_attribute_values(
    VertexAttributeValues&& values) {
    const auto variant = values.enum_variant_name();
    if (auto converted = VertexAttributeConversion<Target>::convert(std::move(values))) {
        return std::move(*converted);
    }
    return std::unexpected(FromVertexAttributeError{
        .from    = std::make_shared<VertexAttributeValues>(std::move(values)),
        .variant = variant,
        .into    = meta::type_info::of<std::vector<Target>>().name,
    });
}

}  // namespace detail
}  // namespace epix::mesh
