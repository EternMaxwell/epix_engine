/**
 * @file epix.core.tick.cppm
 * @brief C++20 module interface for change detection tick system.
 *
 * This module provides the Tick type used for change detection in the ECS.
 * Each component stores when it was added and last modified, allowing
 * queries to filter for recently changed components.
 */
module;

#include <algorithm>
#include <cstdint>
#include <limits>

export module epix.core.tick;

export namespace epix::core {

/// Threshold for when to check and normalize ticks to prevent overflow issues
constexpr uint32_t CHECK_TICK_THRESHOLD = 518400000;

/// Maximum age a change can be before it's considered "old"
constexpr uint32_t MAX_CHANGE_AGE = std::numeric_limits<uint32_t>::max() - (2 * CHECK_TICK_THRESHOLD - 1);

/**
 * @brief Represents a point in time for change tracking.
 *
 * Ticks are monotonically increasing counters used to track when
 * components were added or modified. The ECS uses ticks to implement
 * change detection queries.
 */
struct Tick {
   private:
    uint32_t tick;

   public:
    constexpr Tick(uint32_t tick = 0) : tick(tick) {}

    /**
     * @brief Get the maximum meaningful tick value.
     */
    static constexpr Tick max() { return Tick(MAX_CHANGE_AGE); }

    /**
     * @brief Get the raw tick value.
     */
    constexpr uint32_t get(this Tick self) { return self.tick; }

    /**
     * @brief Set the raw tick value.
     */
    constexpr void set(this Tick& self, uint32_t t) { self.tick = t; }

    /**
     * @brief Check if this tick represents a change newer than the last system run.
     * @param last_run The tick when the observing system last ran.
     * @param this_run The current tick.
     * @return True if this tick is newer than last_run.
     */
    constexpr bool newer_than(this Tick self, Tick last_run, Tick this_run) {
        auto ticks_since_insert = std::min(this_run.relative_to(self).tick, MAX_CHANGE_AGE);
        auto ticks_since_system = std::min(this_run.relative_to(last_run).tick, MAX_CHANGE_AGE);
        return ticks_since_system > ticks_since_insert;
    }

    /**
     * @brief Calculate the difference between this tick and another.
     * @param other The tick to compare against.
     * @return A new Tick representing the difference.
     */
    constexpr Tick relative_to(this Tick self, Tick other) {
        uint32_t diff = self.tick - other.tick;
        return Tick(diff);
    }

    /**
     * @brief Check and potentially normalize this tick to prevent overflow.
     * @param tick The current system tick.
     * @return True if the tick was adjusted.
     */
    constexpr bool check_tick(this Tick& self, Tick tick) {
        auto age = tick.relative_to(self);
        if (age.tick > MAX_CHANGE_AGE) {
            self = tick.relative_to(Tick(MAX_CHANGE_AGE));
            return true;
        }
        return false;
    }

    constexpr auto operator<=>(const Tick&) const = default;
};

/**
 * @brief Storage for component change ticks.
 *
 * Each component instance stores when it was added to an entity
 * and when it was last modified.
 */
struct ComponentTicks {
    Tick added;
    Tick modified;

    ComponentTicks() : added(0), modified(0) {}
    ComponentTicks(Tick tick) : added(tick), modified(tick) {}
    ComponentTicks(Tick added, Tick modified) : added(added), modified(modified) {}
};

/**
 * @brief References to component tick data.
 *
 * Provides mutable access to component tick storage without
 * copying the tick values.
 */
struct TickRefs {
   public:
    explicit TickRefs(Tick* added, Tick* modified) : _added(added), _modified(modified) {}

    Tick& added(this const TickRefs& self) { return *self._added; }
    Tick& modified(this const TickRefs& self) { return *self._modified; }

   private:
    Tick* _added;
    Tick* _modified;
};

/**
 * @brief Immutable references to component ticks.
 *
 * Used by queries that need to check change detection
 * but don't need to modify the ticks.
 */
struct Ticks {
   public:
    explicit Ticks(const Tick* added, const Tick* modified, Tick last_run, Tick this_run)
        : _added(added), _modified(modified), _last_run(last_run), _this_run(this_run) {}

    const Tick& added(this const Ticks& self) { return *self._added; }
    const Tick& modified(this const Ticks& self) { return *self._modified; }
    Tick last_run(this const Ticks& self) { return self._last_run; }
    Tick this_run(this const Ticks& self) { return self._this_run; }

    bool is_added(this const Ticks& self) { return self._added->newer_than(self._last_run, self._this_run); }
    bool is_changed(this const Ticks& self) { return self._modified->newer_than(self._last_run, self._this_run); }

   private:
    const Tick* _added;
    const Tick* _modified;
    Tick _last_run;
    Tick _this_run;
};

/**
 * @brief Mutable references to component ticks with change detection helpers.
 *
 * Used by queries that need to both check and update change detection state.
 */
struct TicksMut {
   public:
    explicit TicksMut(Tick* added, Tick* modified, Tick last_run, Tick this_run)
        : _added(added), _modified(modified), _last_run(last_run), _this_run(this_run) {}

    Tick& added(this const TicksMut& self) { return *self._added; }
    Tick& modified(this const TicksMut& self) { return *self._modified; }
    Tick last_run(this const TicksMut& self) { return self._last_run; }
    Tick this_run(this const TicksMut& self) { return self._this_run; }

    bool is_added(this const TicksMut& self) { return self._added->newer_than(self._last_run, self._this_run); }
    bool is_changed(this const TicksMut& self) { return self._modified->newer_than(self._last_run, self._this_run); }

    void set_modified(this const TicksMut& self) { *self._modified = self._this_run; }

   private:
    Tick* _added;
    Tick* _modified;
    Tick _last_run;
    Tick _this_run;
};

}  // namespace epix::core
