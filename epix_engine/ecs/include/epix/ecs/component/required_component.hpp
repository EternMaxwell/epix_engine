#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/type_id.hpp>

namespace epix::ecs {

struct Components;
struct ComponentsRegistrator;
struct Table;
struct SparseSets;
struct Tick;
struct TableRow;
struct Entity;

/** A type-erased constructor used to initialize a required component. */
EPIX_EXPORT struct RequiredComponentConstructor {
    using Function = std::function<void(Table&, SparseSets&, Tick, TableRow, Entity)>;

    RequiredComponentConstructor() = default;
    explicit RequiredComponentConstructor(std::shared_ptr<Function> function) : function_(std::move(function)) {}

    template <typename C, typename F>
    static RequiredComponentConstructor create(TypeId component_id, F&& constructor)
        requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>;

    void initialize(Table& table, SparseSets& sparse_sets, Tick tick, TableRow row, Entity entity) const;

    explicit operator bool() const noexcept { return static_cast<bool>(function_); }

   private:
    std::shared_ptr<Function> function_;
};

/** Metadata associated with one required component. */
EPIX_EXPORT struct RequiredComponent {
    RequiredComponentConstructor constructor;
};

/**
 * Ordered required-component metadata for a component.
 *
 * `direct` contains only explicitly registered requirements, in precedence order.
 * `all` contains the full transitive closure in depth-first order. Requirements
 * always appear before the component that requires them.
 */
EPIX_EXPORT struct RequiredComponents {
    using Entry = std::pair<TypeId, RequiredComponent>;
    using Container = std::vector<Entry>;

    Container direct;
    Container all;

    bool directly_requires(TypeId id) const noexcept;
    bool contains(TypeId id) const noexcept;
    const RequiredComponent* get(TypeId id) const noexcept;
    auto iter_ids() const noexcept { return std::views::keys(all); }

   private:
    void register_dynamic(TypeId component_id,
                          const Components& components,
                          RequiredComponentConstructor constructor);
    void rebuild_inherited_required_components(const Components& components);
    static void register_inherited_required_components(Container& all,
                                                        TypeId required_id,
                                                        RequiredComponent required_component,
                                                        const Components& components);

    friend struct Components;
    friend struct ComponentsRegistrator;
    friend struct RequiredComponentsRegistrator;
};

EPIX_EXPORT enum class RequiredComponentsErrorKind {
    DuplicateRegistration,
    CyclicRequirement,
    ArchetypeExists,
};

/** Error returned when a runtime required-component registration is rejected. */
EPIX_EXPORT struct RequiredComponentsError {
    RequiredComponentsErrorKind kind;
    TypeId requiree;
    TypeId required;

    std::string message(const Components& components) const;
};

/** Safe registration handle used by a component's static default registration hook. */
EPIX_EXPORT struct RequiredComponentsRegistrator {
    RequiredComponentsRegistrator(ComponentsRegistrator& components, RequiredComponents& required_components)
        : components_(&components), required_components_(&required_components) {}

    ComponentsRegistrator& components_registrator() noexcept { return *components_; }

    template <typename C, typename F>
    void register_required(F&& constructor)
        requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>;

    template <typename C, typename F>
    void register_required_by_id(TypeId component_id, F&& constructor)
        requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>;

    void register_required_dynamic(TypeId component_id, RequiredComponentConstructor constructor);

   private:
    ComponentsRegistrator* components_;
    RequiredComponents* required_components_;
};

}  // namespace epix::ecs
