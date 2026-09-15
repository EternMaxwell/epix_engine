#include <epix/mesh/morph.hpp>
#include <format>

namespace epix::mesh {

std::string MorphBuildError::to_string() const {
    return std::visit(
        []<typename Error>(const Error& error) {
            if constexpr (std::same_as<Error, morph_build_error::TooManyAttributes>) {
                return std::format("Too many vertex×components in morph target, max is {}, got {}×{} = {}",
                                   std::uint64_t{2048} * 2048, error.vertex_count, error.component_count,
                                   error.vertex_count * static_cast<std::size_t>(error.component_count));
            } else {
                return std::format(
                    "Bevy only supports up to {} morph targets (individual poses), tried to create a model with {} "
                    "morph targets",
                    MAX_MORPH_WEIGHTS, error.target_count);
            }
        },
        static_cast<const Base&>(*this));
}

std::expected<MorphWeights, MorphBuildError> MorphWeights::create(std::vector<float> weights,
                                                                  std::optional<assets::Handle<Mesh>> first_mesh) {
    if (weights.size() > MAX_MORPH_WEIGHTS) {
        return std::unexpected(MorphBuildError{morph_build_error::TooManyTargets{weights.size()}});
    }
    MorphWeights result;
    result.weights_    = std::move(weights);
    result.first_mesh_ = std::move(first_mesh);
    return result;
}

std::expected<MeshMorphWeights, MorphBuildError> MeshMorphWeights::create(std::vector<float> weights) {
    if (weights.size() > MAX_MORPH_WEIGHTS) {
        return std::unexpected(MorphBuildError{morph_build_error::TooManyTargets{weights.size()}});
    }
    MeshMorphWeights result;
    result.weights_ = std::move(weights);
    return result;
}

std::optional<detail::MorphTextureRectangle> detail::lowest_morph_texture_2d(std::uint32_t minimum_cells,
                                                                             std::uint32_t maximum_edge) noexcept {
    std::optional<MorphTextureRectangle> result;
    for (std::uint32_t width = 1; width <= maximum_edge; ++width) {
        const std::uint32_t height = minimum_cells / width + (minimum_cells % width != 0 ? 1u : 0u);
        const auto cells           = static_cast<std::uint64_t>(width) * height;
        if (cells < minimum_cells || height > maximum_edge) continue;
        const auto padding = static_cast<std::uint32_t>(cells - minimum_cells);
        if (!result || padding < result->padding) {
            result = MorphTextureRectangle{.width = width, .height = height, .padding = padding};
        }
    }
    return result;
}

}  // namespace epix::mesh
