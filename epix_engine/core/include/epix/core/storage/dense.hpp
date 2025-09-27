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
struct DenseInterface {
   private:
    struct Functor {
        void (*clear)(void*);
        size_t (*len)(const void*);
        void (*reserve)(void*, size_t);
        void (*swap_remove)(void*, uint32_t);
        void (*push_move)(void*, ComponentTicks, void*);
        void (*push_copy)(void*, ComponentTicks, const void*);
        void (*replace_move)(void*, uint32_t, Tick, void*);
        void (*replace_copy)(void*, uint32_t, Tick, const void*);

        std::span<const Tick> (*get_added_ticks)(const void*);
        std::span<const Tick> (*get_modified_ticks)(const void*);
        std::pair<const void*, const void*> (*data)(const void*);
        std::optional<const void*> (*get_data)(const void*, uint32_t);
        std::optional<void*> (*get_data_mut)(void*, uint32_t);
        std::optional<std::reference_wrapper<const Tick>> (*get_added_tick)(const void*, uint32_t);
        std::optional<std::reference_wrapper<const Tick>> (*get_modified_tick)(const void*, uint32_t);
        std::optional<ComponentTicks> (*get_ticks)(const void*, uint32_t);
        void (*check_change_ticks)(void*, Tick);
    }* functor;
    std::unique_ptr<void, void (*)(void*)> data;

    template <typename T>
    Functor* get_functor() {
        static Functor f = {
            .clear       = [](void* ptr) { static_cast<Dense<T>*>(ptr)->clear(); },
            .len         = [](const void* ptr) { return static_cast<const Dense<T>*>(ptr)->len(); },
            .reserve     = [](void* ptr, size_t new_cap) { static_cast<Dense<T>*>(ptr)->reserve(new_cap); },
            .swap_remove = [](void* ptr, uint32_t index) { static_cast<Dense<T>*>(ptr)->swap_remove(index); },
            .push_move =
                [](void* ptr, Tick tick, void* value) {
                    static_cast<Dense<T>*>(ptr)->push(ComponentTicks{tick, tick}, std::move(*static_cast<T*>(value)));
                },
            .push_copy =
                [](void* ptr, Tick tick, const void* value) {
                    static_cast<Dense<T>*>(ptr)->push(ComponentTicks{tick, tick}, *static_cast<const T*>(value));
                },
            .replace_move =
                [](void* ptr, uint32_t index, Tick tick, void* value) {
                    static_cast<Dense<T>*>(ptr)->replace(index, tick, std::move(*static_cast<T*>(value)));
                },
            .replace_copy =
                [](void* ptr, uint32_t index, Tick tick, const void* value) {
                    static_cast<Dense<T>*>(ptr)->replace(index, tick, *static_cast<const T*>(value));
                },
            .get_added_ticks = [](const void* ptr) { return static_cast<const Dense<T>*>(ptr)->get_added_ticks(); },
            .get_modified_ticks =
                [](const void* ptr) { return static_cast<const Dense<T>*>(ptr)->get_modified_ticks(); },
            .data =
                [](const void* ptr) {
                    auto span = static_cast<const Dense<T>*>(ptr)->get_data();
                    return std::pair<const void*, const void*>(span.data(), span.data() + span.size());
                },
            .get_data = [](const void* ptr, uint32_t index) -> std::optional<const void*> {
                return static_cast<const Dense<T>*>(ptr)->get_data(index).transform(
                    [](std::reference_wrapper<const T> ref) -> const void* { return &ref.get(); });
            },
            .get_data_mut = [](void* ptr, uint32_t index) -> std::optional<void*> {
                return static_cast<Dense<T>*>(ptr)->get_data_mut(index).transform(
                    [](std::reference_wrapper<T> ref) -> void* { return &ref.get(); });
            },
            .get_added_tick = [](const void* ptr,
                                 uint32_t index) { return static_cast<const Dense<T>*>(ptr)->get_added_tick(index); },
            .get_modified_tick =
                [](const void* ptr, uint32_t index) {
                    return static_cast<const Dense<T>*>(ptr)->get_modified_tick(index);
                },
            .get_ticks          = [](const void* ptr,
                            uint32_t index) { return static_cast<const Dense<T>*>(ptr)->get_ticks(index); },
            .check_change_ticks = [](void* ptr, Tick tick) { static_cast<Dense<T>*>(ptr)->check_change_ticks(tick); },
        };
        return &f;
    }

   public:
    template <typename T>
    DenseInterface(Dense<T> set = Dense<T>()) {
        functor = get_functor<T>();
        data    = std::unique_ptr<void, void (*)(void*)>(new Dense<T>(std::move(set)),
                                                         [](void* ptr) { delete static_cast<Dense<T>*>(ptr); });
    }
    void clear(this DenseInterface& self) { self.functor->clear(self.data.get()); }
    size_t len(this const DenseInterface& self) { return self.functor->len(self.data.get()); }
    void reserve(this DenseInterface& self, size_t new_cap) { self.functor->reserve(self.data.get(), new_cap); }
    void swap_remove(this DenseInterface& self, uint32_t index) { self.functor->swap_remove(self.data.get(), index); }

    void push_move(this DenseInterface& self, ComponentTicks tick, void* value) {
        self.functor->push_move(self.data.get(), tick, value);
    }
    void push_copy(this DenseInterface& self, ComponentTicks tick, const void* value) {
        self.functor->push_copy(self.data.get(), tick, value);
    }
    void replace_move(this DenseInterface& self, uint32_t index, Tick tick, void* value) {
        self.functor->replace_move(self.data.get(), index, tick, value);
    }
    void replace_copy(this DenseInterface& self, uint32_t index, Tick tick, const void* value) {
        self.functor->replace_copy(self.data.get(), index, tick, value);
    }

    std::pair<const void*, const void*> get_data(this const DenseInterface& self) {
        return self.functor->data(self.data.get());
    }
    template <typename T>
    std::span<const T> get_data_as(this const DenseInterface& self) {
        auto [data, end] = self.get_data();
        size_t len       = (static_cast<const char*>(end) - static_cast<const char*>(data)) / sizeof(T);
        return std::span(static_cast<const T*>(data), len);
    }
    std::span<const Tick> get_added_ticks(this const DenseInterface& self) {
        return self.functor->get_added_ticks(self.data.get());
    }
    std::span<const Tick> get_modified_ticks(this const DenseInterface& self) {
        return self.functor->get_modified_ticks(self.data.get());
    }

    std::optional<const void*> get_data(this const DenseInterface& self, uint32_t index) {
        return self.functor->get_data(self.data.get(), index);
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_data_as(this const DenseInterface& self, uint32_t index) {
        return self.get_data(index).transform(
            [](const void* ptr) -> std::reference_wrapper<const T> { return std::cref(*static_cast<const T*>(ptr)); });
    }
    std::optional<void*> get_data_mut(this DenseInterface& self, uint32_t index) {
        return self.functor->get_data_mut(self.data.get(), index);
    }
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_data_as_mut(this DenseInterface& self, uint32_t index) {
        return self.get_data_mut(index).transform(
            [](void* ptr) -> std::reference_wrapper<T> { return std::ref(*static_cast<T*>(ptr)); });
    }
    std::optional<std::reference_wrapper<const Tick>> get_added_tick(this const DenseInterface& self, uint32_t index) {
        return self.functor->get_added_tick(self.data.get(), index);
    }
    std::optional<std::reference_wrapper<const Tick>> get_modified_tick(this const DenseInterface& self,
                                                                        uint32_t index) {
        return self.functor->get_modified_tick(self.data.get(), index);
    }
    std::optional<ComponentTicks> get_ticks(this const DenseInterface& self, uint32_t index) {
        return self.functor->get_ticks(self.data.get(), index);
    }
    void check_change_ticks(this DenseInterface& self, Tick tick) {
        self.functor->check_change_ticks(self.data.get(), tick);
    }

    const void* get_raw() const { return data.get(); }
    void* get_raw_mut() { return data.get(); }
};
}  // namespace epix::core::storage