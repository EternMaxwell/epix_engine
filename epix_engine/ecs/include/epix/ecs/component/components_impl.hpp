#pragma once

#ifndef EPIX_CXX_MODULE
#include <memory>
#include <stdexcept>
#include <utility>
#endif

#include <epix/ecs/component/components.hpp>
#include <epix/ecs/component/register.hpp>
#include <epix/ecs/component/required_component.hpp>
#include <epix/ecs/storage/dense.hpp>
#include <epix/ecs/storage/sparse_set.hpp>
#include <epix/ecs/storage/table.hpp>

namespace epix::ecs {

template <typename C, typename F>
RequiredComponentConstructor RequiredComponentConstructor::create(TypeId component_id, F&& constructor)
    requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>
{
    return RequiredComponentConstructor(std::make_shared<Function>(
        [constructor = std::forward<F>(constructor), component_id](Table& table, SparseSets& sparse_sets, Tick tick,
                                                                   TableRow row, Entity entity) mutable {
            if (storage_type_of<C>() == StorageType::Table) {
                auto& dense = table.unsafe_dense_mut(component_id);
                dense.initialize_emplace<C>(row.get(), ComponentTicks{tick, tick}, constructor());
            } else {
                sparse_sets.unsafe_get_mut(component_id).emplace<C>(entity, tick, constructor());
            }
        }));
}

template <typename R, typename F>
std::expected<void, RequiredComponentsError> Components::register_required_components(TypeId requiree,
                                                                                      TypeId required,
                                                                                      F&& constructor)
    requires std::invocable<F> && std::same_as<R, std::invoke_result_t<F>>
{
    const auto registered_type = get_index(required);
    if (!registered_type || *registered_type != meta::type_id<R>{}) {
        throw std::logic_error("required component id does not match its constructor result type");
    }
    return register_required_components(
        requiree, required, RequiredComponentConstructor::create<R>(required, std::forward<F>(constructor)));
}

template <typename C, typename F>
void RequiredComponentsRegistrator::register_required(F&& constructor)
    requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>
{
    register_required_by_id<C>(components_->template register_component<C>(), std::forward<F>(constructor));
}

template <typename C, typename F>
void RequiredComponentsRegistrator::register_required_by_id(TypeId component_id, F&& constructor)
    requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>
{
    const auto registered_type = static_cast<const Components&>(*components_).get_index(component_id);
    if (!registered_type || *registered_type != meta::type_id<C>{}) {
        throw std::logic_error("required component id does not match its constructor result type");
    }
    register_required_dynamic(component_id,
                              RequiredComponentConstructor::create<C>(component_id, std::forward<F>(constructor)));
}

}  // namespace epix::ecs
