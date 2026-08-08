#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#endif

#include <epix/ecs/component/components.hpp>
#include <epix/ecs/storage/sparse_set.hpp>

namespace epix::ecs {
EPIX_EXPORT struct World;

/** Marks the canonical entity that owns a movable resource component. */
EPIX_EXPORT struct IsResource {
   public:
    explicit IsResource(TypeId resource_component_id) noexcept : resource_component_id_(resource_component_id) {}

    TypeId resource_component_id() const noexcept { return resource_component_id_; }

    static void on_insert(World& world, HookContext context);
    static void on_remove(World& world, HookContext context);
    static void on_despawn(World& world, HookContext context);

   private:
    TypeId resource_component_id_;
};

/** Cache from a movable resource's component id to its canonical entity. */
EPIX_EXPORT struct ResourceEntities {
    std::size_t size() const noexcept { return entities.size(); }
    bool empty() const noexcept { return entities.empty(); }
    auto iter() const noexcept { return entities.iter(); }
    void clear() { entities.clear(); }
    bool contains(TypeId resource_id) const noexcept { return entities.contains(resource_id.get()); }
    std::optional<Entity> get(TypeId resource_id) const noexcept {
        return entities.get(resource_id.get()).transform([](const Entity& entity) { return entity; });
    }
    void insert(TypeId resource_id, Entity entity) { entities.emplace(resource_id.get(), entity); }
    bool remove(TypeId resource_id) { return entities.remove(resource_id.get()); }

   private:
    SparseSet<std::size_t, Entity> entities;
};

/**
 * @brief Explicit storage for one non-movable resource. Uses untyped_vector for underlying storage.
 * Note that
 * the data is reserved when the struct is constructed, so the reference is stable.
 */
EPIX_EXPORT struct ResourceData {
   public:
    ResourceData(const ::epix::meta::type_info& desc) : data(desc, 1), added_tick(0), modified_tick(0) {}

    bool is_present(this const ResourceData& self) noexcept { return !self.data.empty(); }
    std::optional<const void*> get(this const ResourceData& self) noexcept {
        if (!self.data.empty()) {
            return self.data.cdata();
        }
        return std::nullopt;
    }
    std::optional<void*> get_mut(this ResourceData& self) noexcept {
        if (!self.data.empty()) {
            return self.data.data();
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_as(this const ResourceData& self) noexcept {
        return self.get().transform([&](const void* ptr) { return std::cref(*static_cast<const T*>(ptr)); });
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_as_mut(this ResourceData& self) noexcept {
        return self.get_mut().transform([&](void* ptr) { return std::ref(*static_cast<T*>(ptr)); });
    }

    std::optional<ComponentTicks> get_ticks(this const ResourceData& self) noexcept {
        if (self.is_present()) {
            return ComponentTicks{self.added_tick, self.modified_tick};
        }
        return std::nullopt;
    }
    std::optional<TickRefs> get_tick_refs(this const ResourceData& self) noexcept {
        if (self.is_present()) {
            return TickRefs{&self.added_tick, &self.modified_tick};
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<Tick>> get_added_tick(this const ResourceData& self) noexcept {
        if (self.is_present()) {
            return std::ref(self.added_tick);
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<Tick>> get_modified_tick(this const ResourceData& self) noexcept {
        if (self.is_present()) {
            return std::ref(self.modified_tick);
        }
        return std::nullopt;
    }

    void insert_copy(this ResourceData& self, Tick tick, const void* src) {
        if (self.is_present()) {
            // has value, replace
            self.data.replace_from(0, src);
            self.modified_tick.set(tick.get());
        } else {
            // no value, push
            self.data.push_back_from(src);
            self.added_tick.set(tick.get());
            self.modified_tick.set(tick.get());
        }
    }
    void insert_move(this ResourceData& self, Tick tick, void* src) {
        if (self.is_present()) {
            // has value, replace
            self.data.replace_from_move(0, src);
            self.modified_tick.set(tick.get());
        } else {
            // no value, push
            self.data.push_back_from_move(src);
            self.added_tick.set(tick.get());
            self.modified_tick.set(tick.get());
        }
    }
    template <typename T, typename... Args>
    void emplace(this ResourceData& self, Tick tick, Args&&... args) {
        if (self.is_present()) {
            // has value, replace
            self.data.emplace_at<T>(0, std::forward<Args>(args)...);
            self.modified_tick.set(tick.get());
        } else {
            // no value, push
            self.data.emplace_back<T>(std::forward<Args>(args)...);
            self.added_tick.set(tick.get());
            self.modified_tick.set(tick.get());
        }
    }
    template <std::invocable<void*> F>
    void construct(this ResourceData& self, Tick tick, F&& constructor) {
        if (self.is_present()) {
            // has value, replace
            self.data.construct_at(0, std::forward<F>(constructor));
            self.modified_tick.set(tick.get());
        } else {
            // no value, push
            self.data.construct_back(std::forward<F>(constructor));
            self.added_tick.set(tick.get());
            self.modified_tick.set(tick.get());
        }
    }

    void replace(this ResourceData& self, Tick tick, untyped_vector vec) {
        bool had_value     = self.is_present();
        bool new_has_value = !vec.empty();
        self.data          = std::move(vec);
        if (had_value && new_has_value) {
            self.modified_tick.set(tick.get());
        } else if (!had_value && new_has_value) {
            self.added_tick.set(tick.get());
            self.modified_tick.set(tick.get());
        }
    }

    void remove(this ResourceData& self) { self.data.clear(); }
    template <typename T>
    std::optional<T> take(this ResourceData& self)
        requires std::movable<T>
    {
        struct ClearSpan {
            untyped_vector& vec;
            ~ClearSpan() { vec.clear(); }
        };
        ClearSpan clear_span{self.data};
        if (self.is_present()) {
            return std::move(self.get_as_mut<T>().value().get());
        }
        return std::nullopt;
    }

    void check_change_ticks(this ResourceData& self, Tick tick) {
        if (self.is_present()) {
            self.added_tick.check_tick(tick);
            self.modified_tick.check_tick(tick);
        }
    }

   private:
    untyped_vector data;
    mutable Tick added_tick;
    mutable Tick modified_tick;
};

/** Explicit storage used only for resources that cannot move through entity component storage. */
EPIX_EXPORT struct Resources {
    std::size_t resource_count() const noexcept { return resources.size(); }
    bool empty() const noexcept { return resources.empty(); }
    auto iter() { return resources.iter(); }
    void clear() { resources.clear(); }
    std::optional<std::reference_wrapper<const ResourceData>> get(TypeId resource_id) const noexcept {
        return resources.get(resource_id);
    }
    std::optional<std::reference_wrapper<ResourceData>> get_mut(TypeId resource_id) noexcept {
        return resources.get_mut(resource_id);
    }
    ResourceData& initialize(TypeId resource_id, const Components& components);
    void check_change_ticks(Tick tick);

   private:
    SparseSet<std::size_t, ResourceData> resources;
};

inline void Resources::check_change_ticks(Tick tick) {
    for (auto&& [_, resource] : resources.iter_mut()) resource.check_change_ticks(tick);
}
inline ResourceData& Resources::initialize(TypeId id, const Components& components) {
    const auto& info = components.get_info(id).value().get();
    return resources.get_mut(id)
        .or_else([&] -> std::optional<std::reference_wrapper<ResourceData>> {
            resources.emplace(id, info.type_index().type_info());
            return std::ref(resources.unsafe_get_mut(id));
        })
        .value();
}
}  // namespace epix::ecs
