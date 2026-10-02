#include <epix/render.hpp>

namespace epix::render::phase {

void UnbatchableBinnedEntityIndexSet::add(UnbatchableBinnedEntityIndices indices) {
    if (std::holds_alternative<NoEntities>(m_storage)) {
        switch (indices.extra_index.type) {
            case PhaseItemExtraIndex::Type::DynamicOffset:
                m_storage = Dense{std::move(indices)};
                return;
            case PhaseItemExtraIndex::Type::None:
                m_storage = Sparse{{indices.instance_index, indices.instance_index + 1}, std::nullopt};
                return;
            case PhaseItemExtraIndex::Type::IndirectParametersIndex:
                m_storage = Sparse{{indices.instance_index, indices.instance_index + 1},
                                   indices.extra_index.indirect_range.first};
                return;
        }
    }

    if (auto* sparse = std::get_if<Sparse>(&m_storage)) {
        const auto expected_indirect_index = sparse->first_indirect_parameters_index
                                                 ? *sparse->first_indirect_parameters_index +
                                                       sparse->instance_range.second - sparse->instance_range.first
                                                 : 0;
        const bool consecutive             = sparse->instance_range.second == indices.instance_index;
        const bool matches_none =
            !sparse->first_indirect_parameters_index && indices.extra_index.type == PhaseItemExtraIndex::Type::None;
        const bool matches_indirect =
            sparse->first_indirect_parameters_index &&
            indices.extra_index.type == PhaseItemExtraIndex::Type::IndirectParametersIndex &&
            indices.extra_index.indirect_range.first == expected_indirect_index;
        if (consecutive && (matches_none || matches_indirect)) {
            ++sparse->instance_range.second;
            return;
        }

        Dense dense;
        dense.reserve(sparse->instance_range.second - sparse->instance_range.first + 1);
        for (std::uint32_t entity_index = 0;
             entity_index < sparse->instance_range.second - sparse->instance_range.first; ++entity_index) {
            if (const auto existing = indices_for_entity_index(entity_index)) dense.push_back(*existing);
        }
        dense.push_back(std::move(indices));
        m_storage = std::move(dense);
        return;
    }

    std::get<Dense>(m_storage).push_back(std::move(indices));
}

}  // namespace epix::render::phase
