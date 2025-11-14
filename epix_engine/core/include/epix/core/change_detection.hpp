#pragma once

#include <concepts>

#include "tick.hpp"

namespace epix::core {
struct Ticks {
    static Ticks from_ticks(const Tick& added, const Tick& modified, Tick last_run, Tick this_run) {
        return Ticks{&added, &modified, last_run, this_run};
    }
    static Ticks from_refs(TickRefs refs, Tick last_run, Tick this_run) {
        return Ticks{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added() const { return added->newer_than(last_run, this_run); }
    bool is_modified() const { return modified->newer_than(last_run, this_run); }
    Tick last_modified() const { return *modified; }
    Tick added_tick() const { return *added; }

   private:
    const Tick* added;
    const Tick* modified;
    Tick last_run;
    Tick this_run;

    Ticks(const Tick* added, const Tick* modified, Tick last_run, Tick this_run)
        : added(added), modified(modified), last_run(last_run), this_run(this_run) {}
};
struct TicksMut {
    static TicksMut from_ticks(Tick& added, Tick& modified, Tick last_run, Tick this_run) {
        return TicksMut{&added, &modified, last_run, this_run};
    }
    static TicksMut from_refs(TickRefs refs, Tick last_run, Tick this_run) {
        return TicksMut{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added() const { return added->newer_than(last_run, this_run); }
    bool is_modified() const { return modified->newer_than(last_run, this_run); }
    Tick last_modified() const { return *modified; }
    Tick added_tick() const { return *added; }

    void set_modified() { modified->set(this_run.get()); }
    void set_added() {
        added->set(this_run.get());
        modified->set(this_run.get());
    }

   private:
    Tick* added;
    Tick* modified;
    Tick last_run;
    Tick this_run;

    TicksMut(Tick* added, Tick* modified, Tick last_run, Tick this_run)
        : added(added), modified(modified), last_run(last_run), this_run(this_run) {}
};
template <typename T>
struct copy_ref : public std::false_type {};
template <typename T>
concept refable = !std::is_reference_v<T> && !std::is_const_v<T>;
template <refable T>
struct Ref;
template <refable T>
    requires(!copy_ref<T>::value)
struct Ref<T> {
   private:
    const T* value;
    Ticks ticks;

   public:
    Ref(const T* value, Ticks ticks) : value(value), ticks(ticks) {}

    const T* ptr() const { return value; }
    const T& get() const { return *value; }
    const T* operator->() const { return value; }
    const T& operator*() const { return *value; }
    operator const T&() const { return *value; }
    bool is_added() const { return ticks.is_added(); }
    bool is_modified() const { return ticks.is_modified(); }
    Tick last_modified() const { return ticks.last_modified(); }
    Tick added_tick() const { return ticks.added_tick(); }
};
template <refable T>
    requires(copy_ref<T>::value && std::copy_constructible<T>)
struct Ref<T> {
   private:
    T value;
    Ticks ticks;

   public:
    Ref(const T* value, Ticks ticks) : value(*value), ticks(ticks) {}

    const T* ptr() const { return std::addressof(value); }
    T* ptr_mut() { return std::addressof(value); }
    const T& get() const { return value; }
    T& get_mut() { return value; }
    const T* operator->() const { return std::addressof(value); }
    T* operator->() { return std::addressof(value); }
    const T& operator*() const { return value; }
    T& operator*() { return value; }
    operator const T&() const { return value; }
    operator T&() { return value; }
    bool is_added() const { return ticks.is_added(); }
    bool is_modified() const { return ticks.is_modified(); }
    Tick last_modified() const { return ticks.last_modified(); }
    Tick added_tick() const { return ticks.added_tick(); }
};
template <refable T>
struct Mut {
   private:
    T* value;
    TicksMut ticks;

   public:
    Mut(T* value, TicksMut ticks) : value(value), ticks(ticks) {}

    const T* ptr() const { return value; }
    T* ptr_mut() {
        ticks.set_modified();
        return value;
    }
    const T& get() const { return *value; }
    T& get_mut() {
        ticks.set_modified();
        return *value;
    }
    const T* operator->() const { return value; }
    T* operator->() {
        ticks.set_modified();
        return value;
    }
    const T& operator*() const { return *value; }
    T& operator*() {
        ticks.set_modified();
        return *value;
    }
    operator T&() {
        ticks.set_modified();
        return *value;
    }
    operator const T&() const { return *value; }
    bool is_added() const { return ticks.is_added(); }
    bool is_modified() const { return ticks.is_modified(); }
    Tick last_modified() const { return ticks.last_modified(); }
    Tick added_tick() const { return ticks.added_tick(); }
};

template <refable T>
struct Res : public Ref<T> {
   public:
    using Ref<T>::Ref;
};
template <refable T>
struct ResMut : public Mut<T> {
   public:
    using Mut<T>::Mut;
};
}  // namespace epix::core