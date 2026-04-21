module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstdint>
#include <limits>
#endif
export module epix.core:tick;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::core {
constexpr std::uint32_t CHECK_TICK_THRESHOLD = 518400000;
constexpr std::uint32_t MAX_CHANGE_AGE = std::numeric_limits<std::uint32_t>::max() - (2 * CHECK_TICK_THRESHOLD - 1);
/** @brief Monotonic change-detection counter.
 *  Tracks when components or resources were last added or modified.
 *  Uses wrapping arithmetic: `newer_than()` compares relative ages
 *  and correctly handles wrap-around up to MAX_CHANGE_AGE ticks apart. */
export struct Tick {
   private:
    std::uint32_t tick;

   public:
    /** @brief Construct a Tick with the given value (default 0). */
    constexpr Tick(std::uint32_t tick = 0) : tick(tick) {}

    /** @brief Return the maximum representable change age. */
    static constexpr Tick max() { return Tick(MAX_CHANGE_AGE); }

    /** @brief Get the raw tick value. */
    constexpr std::uint32_t get(this Tick self) { return self.tick; }
    /** @brief Set the raw tick value. */
    constexpr void set(this Tick& self, std::uint32_t t) { self.tick = t; }
    /** @brief Check whether this tick is newer than last_run relative to this_run.
     *  Returns true if the component/resource was changed since the system
     *  last ran. */
    constexpr bool newer_than(this Tick self, Tick last_run, Tick this_run) {
        auto ticks_since_insert = std::min(this_run.relative_to(self).tick, MAX_CHANGE_AGE);
        auto ticks_since_system = std::min(this_run.relative_to(last_run).tick, MAX_CHANGE_AGE);
        return ticks_since_system > ticks_since_insert;
    }
    /** @brief Compute the tick difference (self - other) as a new Tick. */
    constexpr Tick relative_to(this Tick self, Tick other) {
        std::uint32_t diff = self.tick - other.tick;
        return Tick(diff);
    }
    /** @brief Clamp this tick if it is older than MAX_CHANGE_AGE relative to `tick`.
     *  @return true if the tick was clamped. */
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
}  // namespace core