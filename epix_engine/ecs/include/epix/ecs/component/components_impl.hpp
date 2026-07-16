#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cassert>
#include <ranges>
#endif

#include <epix/ecs/component/components.hpp>
#include <epix/ecs/component/required_component.hpp>
#include <epix/ecs/storage/dense.hpp>
#include <epix/ecs/storage/sparse_set.hpp>
#include <epix/ecs/storage/table.hpp>

namespace epix::ecs {

// template <typename Requiree, typename F>
// inline void Components::register_required(F&& constructor)
//     requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
// {
//     auto requiree = register_info<Requiree>();
//     auto required = register_info<std::invoke_result_t<F>>();
//     register_required_by_id(requiree, required, std::forward<F>(constructor));
// }

// template <typename F>
// inline void Components::register_required(TypeId requiree, F&& constructor)
//     requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
// {
//     auto required = register_info<std::invoke_result_t<F>>();
//     register_required_by_id(requiree, required, std::forward<F>(constructor));
// }

// template <typename F>
// inline void Components::register_required_by_id(TypeId requiree, TypeId required, F&& constructor)
//     requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
// {
//     using C = std::invoke_result_t<F>;
//     assert(required == registry().type_id<C>() && "required type must match the constructor return type");
//     auto& rc = get_mut(requiree).value().get()._required_components;
//     auto existing = rc.components.find(required);
//     if (existing != rc.components.end() && existing->second.inheritance_depth == 0) return;
//     rc.register_id<C>(required, 0, std::forward<F>(constructor));
//     auto& required_by = get_mut(required).value().get()._required_by;
//     required_by.insert(requiree);
//     internal::RequiredComponents rct;
//     auto inherited = register_inherited_required_components(requiree, required, rct);
//     rc.merge(rct);
//     required_by.insert_range(get(required).value().get()._required_by);
//     for (auto&& rbi : get(requiree).value().get()._required_by) {
//         auto& rc2 = get_mut(rbi).value().get()._required_components;
//         auto&& depth = get_mut(rbi).value().get()._required_components.components.at(requiree).inheritance_depth;
//         rc2.register_id<C>(requiree, depth + 1, std::forward<F>(constructor));
//         for (auto&& [tid, reqc] : inherited) {
//             rc2.register_dynamic(tid, reqc.inheritance_depth + depth + 1, reqc.constructor);
//             get_mut(tid).value().get()._required_by.insert(rbi);
//         }
//     }
// }

// inline void Components::register_required_dyn(TypeId requiree, TypeId required,
// internal::RequiredComponentConstructor constructor) {
//     auto& rc = get_mut(requiree).value().get()._required_components;
//     auto existing = rc.components.find(required);
//     if (existing != rc.components.end() && existing->second.inheritance_depth == 0) return;
//     rc.register_dynamic(required, 0, std::move(constructor));
//     auto& required_by = get_mut(required).value().get()._required_by;
//     required_by.insert(requiree);
//     internal::RequiredComponents rct;
//     auto inherited = register_inherited_required_components(requiree, required, rct);
//     rc.merge(rct);
//     required_by.insert_range(get(required).value().get()._required_by);
//     for (auto&& rbi : get(requiree).value().get()._required_by) {
//         auto& rc2 = get_mut(rbi).value().get()._required_components;
//         auto&& depth = get_mut(rbi).value().get()._required_components.components.at(requiree).inheritance_depth;
//         rc2.register_dynamic(requiree, depth + 1, constructor);
//         for (auto&& [tid, reqc] : inherited) {
//             rc2.register_dynamic(tid, reqc.inheritance_depth + depth + 1, reqc.constructor);
//             get_mut(tid).value().get()._required_by.insert(rbi);
//         }
//     }
// }

}  // namespace epix::ecs
