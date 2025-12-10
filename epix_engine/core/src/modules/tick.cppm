// Module partition for tick system
// Provides change detection tick types

module;

#include <algorithm>
#include <cstdint>
#include <limits>

export module epix.core:tick;

// Import forward declarations partition
import :fwd;

export namespace epix::core {

constexpr uint32_t CHECK_TICK_THRESHOLD = 518400000;
constexpr uint32_t MAX_CHANGE_AGE = std::numeric_limits<uint32_t>::max() - (2 * CHECK_TICK_THRESHOLD - 1);

struct Tick {
private:
    uint32_t tick;

public:
    constexpr Tick(uint32_t tick = 0) : tick(tick) {}

    static constexpr Tick max() { return Tick(MAX_CHANGE_AGE); }

#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
    // C++23 deducing this version
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
#else
    // C++20 fallback version
    constexpr uint32_t get() const { return tick; }
    
    constexpr void set(uint32_t t) { tick = t; }
    
    constexpr bool newer_than(Tick last_run, Tick this_run) const {
        auto ticks_since_insert = std::min(this_run.relative_to(*this).tick, MAX_CHANGE_AGE);
        auto ticks_since_system = std::min(this_run.relative_to(last_run).tick, MAX_CHANGE_AGE);
        return ticks_since_system > ticks_since_insert;
    }
    
    constexpr Tick relative_to(Tick other) const {
        uint32_t diff = tick - other.tick;
        return Tick(diff);
    }
    
    constexpr bool check_tick(Tick tick) {
        auto age = tick.relative_to(*this);
        if (age.tick > MAX_CHANGE_AGE) {
            *this = tick.relative_to(Tick(MAX_CHANGE_AGE));
            return true;
        }
        return false;
    }
#endif
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
    
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
    Tick& added(this const TickRefs& self) { return *self._added; }
    Tick& modified(this const TickRefs& self) { return *self._modified; }
#else
    Tick& added() const { return *_added; }
    Tick& modified() const { return *_modified; }
#endif

private:
    Tick* _added;
    Tick* _modified;
};

}  // export namespace epix::core
