#pragma once

#include <cstddef>
#include <memory>

#include "../tick.hpp"
#include "sparse_set.hpp"
#include "untypedvec.hpp"

namespace epix::core::storage {
/**
 * @brief Storage for single resource data. Use untyped_vector for underlying storage.
 * Note that the data is reserved when the struct is constructed, so the reference is stable.
 */
struct ResourceData {
   public:
    ResourceData(const type_system::TypeInfo* desc) : data(desc, 1), added_tick(0), modified_tick(0) {}

    bool is_present(this const ResourceData& self) { return !self.data.empty(); }
    std::optional<const void*> get(this const ResourceData& self) {
        if (!self.data.empty()) {
            return self.data.cdata();
        }
        return std::nullopt;
    }
    std::optional<void*> get_mut(this ResourceData& self) {
        if (!self.data.empty()) {
            return self.data.data();
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_as(this const ResourceData& self) {
        return self.get().transform([&](const void* ptr) { return std::cref(*static_cast<const T*>(ptr)); });
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_as_mut(this ResourceData& self) {
        return self.get_mut().transform([&](void* ptr) { return std::ref(*static_cast<T*>(ptr)); });
    }

    std::optional<ComponentTicks> get_ticks(this const ResourceData& self) {
        if (self.is_present()) {
            return ComponentTicks{self.added_tick, self.modified_tick};
        }
        return std::nullopt;
    }
    std::optional<TickRefs> get_tick_refs(this const ResourceData& self) {
        if (self.is_present()) {
            return TickRefs{&self.added_tick, &self.modified_tick};
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<Tick>> get_added_tick(this const ResourceData& self) {
        if (self.is_present()) {
            return std::ref(self.added_tick);
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<Tick>> get_modified_tick(this const ResourceData& self) {
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
            self.data.replace_emplace<T>(0, std::forward<Args>(args)...);
            self.modified_tick.set(tick.get());
        } else {
            // no value, push
            self.data.emplace_back<T>(std::forward<Args>(args)...);
            self.added_tick.set(tick.get());
            self.modified_tick.set(tick.get());
        }
    }

    /**
     * @brief Insert an uninitialized value into the resource data.
     * This will remove the existing value if present, but won't change added tick.
     *
     * @param self
     */
    void insert_uninitialized(this ResourceData& self, Tick tick) {
        if (self.is_present()) {
            // This function is generally called before an inplace construction, so we destroy the existing value first
            self.data.type_info()->destroy(self.data.data());
        } else {
            self.data.append_uninitialized(1);
            self.added_tick.set(tick.get());
        }
        self.modified_tick.set(tick.get());
    }

    void remove(this ResourceData& self) { self.data.clear(); }

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

struct Resources {
   public:
    Resources(std::shared_ptr<type_system::TypeRegistry> registry) : registry(std::move(registry)) {}

    size_t resource_count(this const Resources& self) { return self.resources.size(); }
    bool empty(this const Resources& self) { return self.resources.empty(); }
    auto iter(this Resources& self) { return self.resources.iter(); }
    void clear(this Resources& self) { self.resources.clear(); }

    std::optional<std::reference_wrapper<const ResourceData>> get(this const Resources& self, size_t resource_id) {
        return self.resources.get(resource_id);
    }
    std::optional<std::reference_wrapper<ResourceData>> get_mut(this Resources& self, size_t resource_id) {
        return self.resources.get_mut(resource_id);
    }

    void initialize(this Resources& self, size_t resource_id) {
        if (!self.resources.contains(resource_id)) {
            const epix::core::type_system::TypeInfo* type_info = self.registry->type_info(resource_id);
            self.resources.emplace(resource_id, ResourceData(type_info));
        }
    }

    void check_change_ticks(this Resources& self, Tick tick) {
        for (auto&& [_, resource] : self.resources.iter_mut()) {
            resource.check_change_ticks(tick);
        }
    }

   private:
    std::shared_ptr<type_system::TypeRegistry> registry;
    SparseSet<size_t, ResourceData> resources;
};
}  // namespace epix::core::storage