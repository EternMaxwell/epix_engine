#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <memory>
#include <type_traits>
#endif

#include <epix/core/tick.hpp>

namespace epix::core {
struct Ticks {
    static Ticks from_ticks(const Tick& added, const Tick& modified, Tick last_run, Tick this_run) noexcept {
        return Ticks{&added, &modified, last_run, this_run};
    }
    static Ticks from_refs(TickRefs refs, Tick last_run, Tick this_run) noexcept {
        return Ticks{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added() const noexcept { return added->newer_than(last_run, this_run); }
    bool is_modified() const noexcept { return modified->newer_than(last_run, this_run); }
    Tick last_modified() const noexcept { return *modified; }
    Tick added_tick() const noexcept { return *added; }

   private:
    const Tick* added;
    const Tick* modified;
    Tick last_run;
    Tick this_run;

    Ticks(const Tick* added, const Tick* modified, Tick last_run, Tick this_run) noexcept
        : added(added), modified(modified), last_run(last_run), this_run(this_run) {}

    friend struct TicksMut;
};
struct TicksMut {
    static TicksMut from_ticks(Tick& added, Tick& modified, Tick last_run, Tick this_run) noexcept {
        return TicksMut{&added, &modified, last_run, this_run};
    }
    static TicksMut from_refs(TickRefs refs, Tick last_run, Tick this_run) noexcept {
        return TicksMut{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added() const noexcept { return added->newer_than(last_run, this_run); }
    bool is_modified() const noexcept { return modified->newer_than(last_run, this_run); }
    Tick last_modified() const noexcept { return *modified; }
    Tick added_tick() const noexcept { return *added; }

    void set_modified() noexcept { modified->set(this_run.get()); }
    void set_added() noexcept {
        added->set(this_run.get());
        modified->set(this_run.get());
    }
    operator Ticks() const noexcept { return Ticks(added, modified, last_run, this_run); }

   private:
    Tick* added;
    Tick* modified;
    Tick last_run;
    Tick this_run;

    TicksMut(Tick* added, Tick* modified, Tick last_run, Tick this_run) noexcept
        : added(added), modified(modified), last_run(last_run), this_run(this_run) {}
};
/** @brief Trait to opt into copy semantics for Ref<T>.
 *
 * Specialize as `std::true_type` for types that should be copied
 * rather than referenced when wrapped in Ref.
 */
EPIX_EXPORT template <typename T>
struct copy_ref : public std::false_type {};
template <typename T>
concept refable = !std::is_reference_v<T> && !std::is_const_v<T>;
/** @brief Immutable reference wrapper with change-detection tick metadata. */
EPIX_EXPORT template <refable T>
struct Ref;
template <refable T>
    requires(!copy_ref<T>::value)
struct Ref<T> {
   private:
    const T* value;
    Ticks ticks;

   public:
    Ref(const T* value, Ticks ticks) noexcept : value(value), ticks(ticks) {}

    const T* ptr() const noexcept { return value; }
    const T& get() const noexcept { return *value; }
    const T* operator->() const noexcept { return value; }
    const T& operator*() const noexcept { return *value; }
    operator const T&() const noexcept { return *value; }
    bool is_added() const noexcept { return ticks.is_added(); }
    bool is_modified() const noexcept { return ticks.is_modified(); }
    Tick last_modified() const noexcept { return ticks.last_modified(); }
    Tick added_tick() const noexcept { return ticks.added_tick(); }
};
template <refable T>
    requires(copy_ref<T>::value && std::copy_constructible<T>)
struct Ref<T> {
   private:
    T value;
    Ticks ticks;

   public:
    Ref(const T* value, Ticks ticks) : value(*value), ticks(ticks) {}

    const T* ptr() const noexcept { return std::addressof(value); }
    T* ptr_mut() noexcept { return std::addressof(value); }
    const T& get() const noexcept { return value; }
    T& get_mut() noexcept { return value; }
    const T* operator->() const noexcept { return std::addressof(value); }
    T* operator->() noexcept { return std::addressof(value); }
    const T& operator*() const noexcept { return value; }
    T& operator*() noexcept { return value; }
    operator const T&() const noexcept { return value; }
    operator T&() noexcept { return value; }
    bool is_added() const noexcept { return ticks.is_added(); }
    bool is_modified() const noexcept { return ticks.is_modified(); }
    Tick last_modified() const noexcept { return ticks.last_modified(); }
    Tick added_tick() const noexcept { return ticks.added_tick(); }
};
/** @brief Mutable reference wrapper with change-detection tick metadata. */
EPIX_EXPORT template <refable T>
struct Mut {
   private:
    T* value;
    TicksMut ticks;

   public:
    Mut(T* value, TicksMut ticks) noexcept : value(value), ticks(ticks) {}

    const T* ptr() const noexcept { return value; }
    T* ptr_mut() noexcept {
        ticks.set_modified();
        return value;
    }
    const T& get() const noexcept { return *value; }
    T& get_mut() noexcept {
        ticks.set_modified();
        return *value;
    }
    const T* operator->() const noexcept { return value; }
    T* operator->() noexcept {
        ticks.set_modified();
        return value;
    }
    const T& operator*() const noexcept { return *value; }
    T& operator*() noexcept {
        ticks.set_modified();
        return *value;
    }
    operator T&() noexcept {
        ticks.set_modified();
        return *value;
    }
    operator const T&() const noexcept { return *value; }
    bool is_added() const noexcept { return ticks.is_added(); }
    bool is_modified() const noexcept { return ticks.is_modified(); }
    Tick last_modified() const noexcept { return ticks.last_modified(); }
    Tick added_tick() const noexcept { return ticks.added_tick(); }
    operator Ref<T>() const { return Ref<T>(value, ticks); }
};

/** @brief Immutable resource reference, extending Ref<T>. */
EPIX_EXPORT template <refable T>
struct Res : public Ref<T> {
   public:
    using Ref<T>::Ref;
};
/** @brief Mutable resource reference, extending Mut<T>. */
EPIX_EXPORT template <refable T>
struct ResMut : public Mut<T> {
   public:
    using Mut<T>::Mut;
};
}  // namespace epix::core