#pragma once


#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <expected>
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif

#include <epix/mesh/mesh.hpp>

namespace epix::mesh {

/** @brief Maximum number of morph-target poses supported by Bevy's mesh shaders. */
inline constexpr std::size_t MAX_MORPH_WEIGHTS = 256;

namespace morph_build_error {
/** @brief The flattened target attributes cannot fit in a 2048x2048 texture layer. */
struct TooManyAttributes {
    std::size_t vertex_count      = 0;
    std::uint32_t component_count = 0;

    bool operator==(const TooManyAttributes&) const = default;
};

/** @brief More than MAX_MORPH_WEIGHTS target poses were supplied. */
struct TooManyTargets {
    std::size_t target_count = 0;

    bool operator==(const TooManyTargets&) const = default;
};
}  // namespace morph_build_error

/** @brief Error returned while creating morph weights or a morph-target image. */
struct MorphBuildError : std::variant<morph_build_error::TooManyAttributes, morph_build_error::TooManyTargets> {
    using Base = std::variant<morph_build_error::TooManyAttributes, morph_build_error::TooManyTargets>;
    using Base::Base;

    /** @brief Bevy `MorphBuildError`'s display text. */
    std::string to_string() const;
};

/** @brief Position, normal, and tangent differences for one morph-target vertex. */
struct MorphAttributes {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 tangent{0.0f};

    static constexpr std::size_t COMPONENT_COUNT = 9;

    constexpr MorphAttributes() = default;
    constexpr MorphAttributes(glm::vec3 position, glm::vec3 normal, glm::vec3 tangent) noexcept
        : position(position), normal(normal), tangent(tangent) {}
    constexpr explicit MorphAttributes(const std::array<glm::vec3, 3>& attributes) noexcept
        : position(attributes[0]), normal(attributes[1]), tangent(attributes[2]) {}

    bool operator==(const MorphAttributes&) const = default;
};

namespace detail {
/** @brief Smallest-padding rectangle search used by Bevy's morph image builder. */
struct MorphTextureRectangle {
    std::uint32_t width   = 0;
    std::uint32_t height  = 0;
    std::uint32_t padding = 0;

    bool operator==(const MorphTextureRectangle&) const = default;
};

std::optional<MorphTextureRectangle> lowest_morph_texture_2d(std::uint32_t minimum_cells,
                                                             std::uint32_t maximum_edge) noexcept;
}  // namespace detail

/** @brief An R32Float 3D image formatted for mesh morph-target rendering. */
struct MorphTargetImage {
    image::Image image;

    /** @brief Generate one 3D-texture layer for each target pose.
     *
     * The outer range must be sized, matching Bevy's `ExactSizeIterator`; each
     * inner input range yields the per-vertex position/normal/tangent deltas.
     */
    template <std::ranges::input_range Targets>
        requires std::ranges::sized_range<Targets> &&
                 std::ranges::input_range<std::ranges::range_reference_t<Targets>> &&
                 std::convertible_to<std::ranges::range_reference_t<std::ranges::range_reference_t<Targets>>,
                                     MorphAttributes>
    static std::expected<MorphTargetImage, MorphBuildError> create(Targets&& targets,
                                                                   std::size_t vertex_count,
                                                                   assets::RenderAssetUsages asset_usage) {
        constexpr std::uint32_t max_texture_width = 2048;
        const std::size_t target_count            = static_cast<std::size_t>(std::ranges::size(targets));
        if (target_count > MAX_MORPH_WEIGHTS) {
            return std::unexpected(MorphBuildError{morph_build_error::TooManyTargets{target_count}});
        }

        const auto component_count = static_cast<std::uint32_t>(vertex_count * MorphAttributes::COMPONENT_COUNT);
        const auto rectangle       = detail::lowest_morph_texture_2d(component_count, max_texture_width);
        if (!rectangle) {
            return std::unexpected(
                MorphBuildError{morph_build_error::TooManyAttributes{vertex_count, component_count}});
        }
        const auto [width, height, padding] = *rectangle;

        const std::size_t layer_float_count = static_cast<std::size_t>(component_count + padding);
        std::vector<float> values;
        values.reserve(layer_float_count * target_count);
        for (auto&& target : targets) {
            std::size_t written_vertices = 0;
            for (auto&& value : target) {
                if (written_vertices == vertex_count) break;
                const MorphAttributes attributes = static_cast<MorphAttributes>(value);
                values.insert(values.end(), {attributes.position.x, attributes.position.y, attributes.position.z,
                                             attributes.normal.x, attributes.normal.y, attributes.normal.z,
                                             attributes.tangent.x, attributes.tangent.y, attributes.tangent.z});
                ++written_vertices;
            }
            values.insert(values.end(), padding, 0.0f);
        }

        std::vector<std::byte> bytes(values.size() * sizeof(float));
        if (!values.empty()) {
            std::memcpy(bytes.data(), values.data(), bytes.size());
        }
        return MorphTargetImage{image::Image{wgpu::Extent3D{width, height, static_cast<std::uint32_t>(target_count)},
                                             wgpu::TextureDimension::e3D, std::move(bytes),
                                             wgpu::TextureFormat::eR32Float, asset_usage}};
    }
};

/** @brief Canonical morph weights on a glTF mesh-node parent. */
class MorphWeights {
   public:
    MorphWeights() = default;

    static std::expected<MorphWeights, MorphBuildError> create(
        std::vector<float> weights, std::optional<assets::Handle<Mesh>> first_mesh = std::nullopt);

    std::optional<std::reference_wrapper<const assets::Handle<Mesh>>> first_mesh() const noexcept {
        if (!first_mesh_) return std::nullopt;
        return std::cref(*first_mesh_);
    }
    std::span<const float> weights() const noexcept { return weights_; }
    std::span<float> weights_mut() noexcept { return weights_; }

    bool operator==(const MorphWeights&) const = default;

   private:
    std::vector<float> weights_;
    std::optional<assets::Handle<Mesh>> first_mesh_;
};

/** @brief Morph weights controlling one concrete mesh primitive. */
class MeshMorphWeights {
   public:
    MeshMorphWeights() = default;

    static std::expected<MeshMorphWeights, MorphBuildError> create(std::vector<float> weights);

    std::span<const float> weights() const noexcept { return weights_; }
    std::span<float> weights_mut() noexcept { return weights_; }
    void clear_weights() noexcept { weights_.clear(); }
    void extend_weights(std::span<const float> weights) {
        weights_.insert(weights_.end(), weights.begin(), weights.end());
    }

    bool operator==(const MeshMorphWeights&) const = default;

   private:
    std::vector<float> weights_;
};

/** @brief PostUpdate set containing render's morph-weight inheritance system. */
struct InheritWeightSystems {};

}  // namespace epix::mesh
