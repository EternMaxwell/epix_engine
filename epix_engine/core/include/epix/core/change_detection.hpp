#pragma once

#include "tick.hpp"

namespace epix::core {
struct Ticks {
    static Ticks from_ticks(const Tick& added, const Tick& modified, Tick last_run, Tick this_run) {
        return Ticks{&added, &modified, last_run, this_run};
    }
    static Ticks from_refs(TickRefs refs, Tick last_run, Tick this_run) {
        return Ticks{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added(this const Ticks& self) { return self.added->newer_than(self.last_run, self.this_run); }
    bool is_modified(this const Ticks& self) { return self.modified->newer_than(self.last_run, self.this_run); }
    Tick last_modified(this const Ticks& self) { return *self.modified; }
    Tick added_tick(this const Ticks& self) { return *self.added; }

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

    bool is_added(this const TicksMut& self) { return self.added->newer_than(self.last_run, self.this_run); }
    bool is_modified(this const TicksMut& self) { return self.modified->newer_than(self.last_run, self.this_run); }
    Tick last_modified(this const TicksMut& self) { return *self.modified; }
    Tick added_tick(this const TicksMut& self) { return *self.added; }

    void set_modified(this TicksMut& self) { self.modified->set(self.this_run.get()); }
    void set_added(this TicksMut& self) {
        self.added->set(self.this_run.get());
        self.modified->set(self.this_run.get());
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
struct Ref {
   private:
    const T* value;
    Ticks ticks;

   public:
    Ref(const T* value, Ticks ticks) : value(value), ticks(ticks) {}

    const T* ptr(this const Ref& self) { return self.value; }
    const T& get(this const Ref& self) { return *self.value; }
    const T* operator->(this const Ref& self) { return self.value; }
    const T& operator*(this const Ref& self) { return *self.value; }
    bool is_added(this const Ref& self) { return self.ticks.is_added(); }
    bool is_modified(this const Ref& self) { return self.ticks.is_modified(); }
    Tick last_modified(this const Ref& self) { return self.ticks.last_modified(); }
    Tick added_tick(this const Ref& self) { return self.ticks.added_tick(); }
};
template <typename T>
struct Mut {
   private:
    T* value;
    TicksMut ticks;

   public:
    Mut(T* value, TicksMut ticks) : value(value), ticks(ticks) {}

    const T* ptr(this const Mut& self) { return self.value; }
    T* ptr_mut(this Mut& self) {
        self.ticks.set_modified();
        return self.value;
    }
    const T& get(this const Mut& self) { return *self.value; }
    T& get_mut(this Mut& self) {
        self.ticks.set_modified();
        return *self.value;
    }
    const T* operator->(this const Mut& self) { return self.value; }
    T* operator->(this Mut& self) {
        self.ticks.set_modified();
        return self.value;
    }
    const T& operator*(this const Mut& self) { return *self.value; }
    T& operator*(this Mut& self) {
        self.ticks.set_modified();
        return *self.value;
    }
    bool is_added(this const Mut& self) { return self.ticks.is_added(); }
    bool is_modified(this const Mut& self) { return self.ticks.is_modified(); }
    Tick last_modified(this const Mut& self) { return self.ticks.last_modified(); }
    Tick added_tick(this const Mut& self) { return self.ticks.added_tick(); }
};
}  // namespace epix::core