#pragma once

#include <cassert>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "../tick.hpp"
#include "fwd.hpp"

namespace epix::core::storage {
// struct UntypedVec {
//    private:
//     struct Accessors {
//         std::unique_ptr<void, void (*)(void*)> (*clone)(const void*);
//         void (*clear)(void*);
//         void* (*data)(const void*, void**);
//         size_t (*size)(const void*);
//         void (*reserve)(void*, size_t);
//         void (*swap_remove)(void*, size_t);
//         void (*push_move)(void*, void*);
//         void (*push_copy)(void*, const void*);
//         void (*replace_move)(void*, size_t, void*);
//         void (*replace_copy)(void*, size_t, const void*);
//         void* (*get_mut)(void*, size_t);
//         const void* (*get)(const void*, size_t);

//         bool static_accessors;
//     }* accessors = nullptr;

//     std::unique_ptr<void, void (*)(void*)> data;

//    public:
//     ~UntypedVec() {
//         // data is given the deleter. No need to do anything here.
//         if (accessors && !accessors->static_accessors) {
//             delete accessors;
//         }
//     }
//     UntypedVec(size_t elem_size, size_t elem_align) {
//         // in this case we use std::vector<std::byte> as the underlying storage
//         data = std::unique_ptr<void, void (*)(void*)>(
//             new std::vector<std::byte>(), [](void* ptr) { delete static_cast<std::vector<std::byte>*>(ptr); });
//     }

//     void clear(this UntypedVec& self) {
//         assert(self.accessors != nullptr);
//         self.accessors->clear(self.data.get());
//     }
// };
template <typename T>
struct Dense {
   private:
    std::vector<T> values;
    std::vector<Tick> added_ticks;
    std::vector<Tick> modified_ticks;

   public:
    void reserve(this Dense& self, size_t new_cap) {
        self.values.reserve(new_cap);
        self.added_ticks.reserve(new_cap);
        self.modified_ticks.reserve(new_cap);
    }
    size_t len(this const Dense& self) { return self.values.size(); }
    void clear(this Dense& self) {
        self.values.clear();
        self.added_ticks.clear();
        self.modified_ticks.clear();
    }

    void swap_remove(this Dense& self, uint32_t index) {
        assert(index < self.values.size());
        std::swap(self.values[index], self.values.back());
        std::swap(self.added_ticks[index], self.added_ticks.back());
        std::swap(self.modified_ticks[index], self.modified_ticks.back());
        self.values.pop_back();
        self.added_ticks.pop_back();
        self.modified_ticks.pop_back();
    }

    template <typename... Args>
    void replace(this Dense& self, uint32_t index, Tick tick, Args&&... args) {
        assert(index < self.values.size());
        self.values[index] = T(std::forward<Args>(args)...);
        self.modified_ticks[index].set(self.added_ticks[index].get());
    }
    template <typename... Args>
    void push(this Dense& self, ComponentTicks ticks, Args&&... args) {
        self.values.emplace_back(std::forward<Args>(args)...);
        self.added_ticks.emplace_back(ticks.added);
        self.modified_ticks.emplace_back(ticks.modified);
    }

    std::span<const T> get_data(this const Dense& self) { return std::span(self.values); }
    std::span<const Tick> get_added_ticks(this const Dense& self) { return std::span(self.added_ticks); }
    std::span<const Tick> get_modified_ticks(this const Dense& self) { return std::span(self.modified_ticks); }

    std::optional<std::reference_wrapper<const T>> get_data(this const Dense& self, uint32_t index) {
        if (index < self.values.size()) {
            return self.values[index];
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<T>> get_data_mut(this Dense& self, uint32_t index) {
        if (index < self.values.size()) {
            return self.values[index];
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const Tick>> get_added_tick(this const Dense& self, uint32_t index) {
        if (index < self.added_ticks.size()) {
            return self.added_ticks[index];
        }
        return std::nullopt;
    }
    std::optional<std::reference_wrapper<const Tick>> get_modified_tick(this const Dense& self, uint32_t index) {
        if (index < self.modified_ticks.size()) {
            return self.modified_ticks[index];
        }
        return std::nullopt;
    }
    std::optional<ComponentTicks> get_ticks(this const Dense& self, uint32_t index) {
        if (index < self.modified_ticks.size()) {
            return ComponentTicks{self.added_ticks[index], self.modified_ticks[index]};
        }
        return std::nullopt;
    }
    void check_change_ticks(this Dense& self, Tick tick) {
        for (auto&& [added, modified] : std::views::zip(self.added_ticks, self.modified_ticks)) {
            added.check_tick(tick);
            modified.check_tick(tick);
        }
    }
};
}  // namespace epix::core::storage