#pragma once

#include "tick.hpp"

namespace epix::core {
struct Ticks {
    static Ticks from_ticks(const Tick& added, const Tick& modified, Tick last_run, Tick this_run) {
        return Ticks{&added, &modified, last_run, this_run};
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
}  // namespace epix::core