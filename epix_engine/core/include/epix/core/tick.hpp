#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "fwd.hpp"

namespace epix::core {
constexpr uint32_t CHECK_TICK_THRESHOLD = 518400000;
constexpr uint32_t MAX_CHANGE_AGE       = std::numeric_limits<uint32_t>::max() - (2 * CHECK_TICK_THRESHOLD - 1);
struct Tick {
   private:
    uint32_t tick;

   public:
    constexpr Tick(uint32_t tick = 0) : tick(tick) {}

    static constexpr Tick max() { return Tick(MAX_CHANGE_AGE); }

    constexpr uint32_t get(this Tick self) { return self.tick; }
    constexpr void set(this Tick& self, uint32_t t) { self.tick = t; }
    constexpr bool newer_than(this Tick self, Tick last_run, Tick this_run) {
        auto ticks_since_insert = std::min(this_run.relative_to(self).tick, MAX_CHANGE_AGE);
        auto ticks_since_system = std::min(this_run.relative_to(last_run).tick, MAX_CHANGE_AGE);
        return ticks_since_system > ticks_since_insert;
    }
    constexpr Tick relative_to(this Tick self, Tick other) {
        uint32_t diff = self.tick - other.tick;
        return Tick(diff);
    }
    constexpr bool check_tick(this Tick& self, Tick tick) {
        auto age = tick.relative_to(self);
        if (age.tick > MAX_CHANGE_AGE) {
            self = tick.relative_to(Tick(MAX_CHANGE_AGE));
            return true;
        }
        return false;
    }
};
struct ComponentTicks {
    Tick added;
    Tick modified;

    ComponentTicks() : added(0), modified(0) {}
    ComponentTicks(Tick tick) : added(tick), modified(tick) {}
    ComponentTicks(Tick added, Tick modified) : added(added), modified(modified) {}
};
struct TickRefs {
   public:
    explicit TickRefs(Tick* added, Tick* modified) : _added(added), _modified(modified) {}
    Tick& added(this const TickRefs& self) { return *self._added; }
    Tick& modified(this const TickRefs& self) { return *self._modified; }

   private:
    Tick* _added;
    Tick* _modified;
};
}  // namespace epix::core