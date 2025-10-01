#pragma once

#include <cassert>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "../tick.hpp"
#include "epix/core/storage/untypedvec.hpp"
#include "fwd.hpp"

namespace epix::core::storage {
struct Dense {
   private:
    untyped_vector values;
    std::vector<Tick> added_ticks;
    std::vector<Tick> modified_ticks;

   public:
    explicit Dense(const epix::core::type_system::TypeInfo* desc, size_t reserve_cnt = 0) : values(desc, reserve_cnt) {
        if (reserve_cnt) {
            added_ticks.reserve(reserve_cnt);
            modified_ticks.reserve(reserve_cnt);
        }
    }

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
        self.values.swap_remove(index);
        std::swap(self.added_ticks[index], self.added_ticks.back());
        std::swap(self.modified_ticks[index], self.modified_ticks.back());
        self.added_ticks.pop_back();
        self.modified_ticks.pop_back();
    }

    template <typename T, typename... Args>
    void replace(this Dense& self, uint32_t index, Tick tick, Args&&... args) {
        assert(index < self.values.size());
        self.values.replace_emplace<T>(index, std::forward<Args>(args)...);
        self.modified_ticks[index].set(self.added_ticks[index].get());
    }
    void replace_copy(this Dense& self, uint32_t index, Tick tick, const void* src) {
        assert(index < self.values.size());
        self.values.replace_from(index, src);
        self.modified_ticks[index].set(self.added_ticks[index].get());
    }
    void replace_move(this Dense& self, uint32_t index, Tick tick, void* src) {
        assert(index < self.values.size());
        self.values.replace_from_move(index, src);
        self.modified_ticks[index].set(self.added_ticks[index].get());
    }
    template <typename T, typename... Args>
    void push(this Dense& self, ComponentTicks ticks, Args&&... args) {
        self.values.emplace_back<T>(std::forward<Args>(args)...);
        self.added_ticks.emplace_back(ticks.added);
        self.modified_ticks.emplace_back(ticks.modified);
    }
    void push_copy(this Dense& self, ComponentTicks ticks, const void* src) {
        self.values.push_back_from(src);
        self.added_ticks.emplace_back(ticks.added);
        self.modified_ticks.emplace_back(ticks.modified);
    }
    void push_move(this Dense& self, ComponentTicks ticks, void* src) {
        self.values.push_back_from_move(src);
        self.added_ticks.emplace_back(ticks.added);
        self.modified_ticks.emplace_back(ticks.modified);
    }

    std::pair<const void*, const void*> get_data(this const Dense& self) {
        return {self.values.cdata(), reinterpret_cast<const char*>(self.values.cdata()) +
                                         self.values.size() * self.values.descriptor()->size};
    }
    template <typename T>
    std::span<const T> get_data_as(this const Dense& self) {
        return self.values.cspan_as<T>();
    }
    std::span<const Tick> get_added_ticks(this const Dense& self) { return std::span(self.added_ticks); }
    std::span<const Tick> get_modified_ticks(this const Dense& self) { return std::span(self.modified_ticks); }

    std::optional<const void*> get(this const Dense& self, uint32_t index) {
        if (index < self.values.size()) {
            return self.values.get(index);
        }
        return std::nullopt;
    }
    std::optional<void*> get_mut(this Dense& self, uint32_t index) {
        if (index < self.values.size()) {
            return self.values.get(index);
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_as(this const Dense& self, uint32_t index) {
        if (index < self.values.size()) {
            return self.values.get_as<T>(index);
        }
        return std::nullopt;
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_as_mut(this Dense& self, uint32_t index) {
        if (index < self.values.size()) {
            return self.values.get_as<T>(index);
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