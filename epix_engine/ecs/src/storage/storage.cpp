#include <epix/ecs/component/detail/registry_state.hpp>
#include <epix/ecs/storage/storage.hpp>

namespace epix::ecs {

Storage::Storage() : sparse_sets(), tables() {}

ComponentSparseSet& SparseSets::get_or_insert(const internal::ComponentInfo& info) {
    return sets.get_mut(info.type_id())
        .or_else([this, &info] -> std::optional<std::reference_wrapper<ComponentSparseSet>> {
            sets.emplace(info.type_id().get(), info.type_index().type_info());
            return std::ref(sets.unsafe_get_mut(info.type_id()));
        })
        .value();
}

void Storage::prepare_component(const internal::ComponentInfo& info) {
    if (info.storage_type() == StorageType::SparseSet) {
        sparse_sets.get_or_insert(info);
    }
}

}  // namespace epix::ecs
