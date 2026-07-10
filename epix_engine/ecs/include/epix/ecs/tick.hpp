#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <epix/common.hpp>
#include <limits>
#include <memory>
#include <type_traits>
#endif

namespace epix::ecs {
namespace internal {
constexpr std::uint32_t CHECK_TICK_THRESHOLD = 518400000;
constexpr std::uint32_t MAX_CHANGE_AGE = std::numeric_limits<std::uint32_t>::max() - (2 * CHECK_TICK_THRESHOLD - 1);
}  // namespace internal
/** @brief Monotonic change-detection counter.
 *  Tracks when components or resources were last added or modified.
 *  Uses wrapping arithmetic: `newer_than()` compares relative ages
 *  and correctly handles wrap-around up to MAX_CHANGE_AGE ticks apart. */
EPIX_EXPORT struct Tick {
   private:
    std::uint32_t tick;

   public:
    /** @brief Construct a Tick with the given value (default 0). */
    constexpr Tick(std::uint32_t tick = 0) noexcept : tick(tick) {}

    /** @brief Return the maximum representable change age. */
    static constexpr Tick max() noexcept { return Tick(internal::MAX_CHANGE_AGE); }

    /** @brief Get the raw tick value. */
    constexpr std::uint32_t get(this Tick self) noexcept { return self.tick; }
    /** @brief Set the raw tick value. */
    constexpr void set(this Tick& self, std::uint32_t t) noexcept { self.tick = t; }
    /** @brief Check whether this tick is newer than last_run relative to this_run.
     *  Returns true if the component/resource was changed since the system
     *  last ran. */
    constexpr bool newer_than(this Tick self, Tick last_run, Tick this_run) noexcept {
        auto ticks_since_insert = std::min(this_run.relative_to(self).tick, internal::MAX_CHANGE_AGE);
        auto ticks_since_system = std::min(this_run.relative_to(last_run).tick, internal::MAX_CHANGE_AGE);
        return ticks_since_system > ticks_since_insert;
    }
    /** @brief Compute the tick difference (self - other) as a new Tick. */
    constexpr Tick relative_to(this Tick self, Tick other) noexcept {
        std::uint32_t diff = self.tick - other.tick;
        return Tick(diff);
    }
    /** @brief Clamp this tick if it is older than MAX_CHANGE_AGE relative to `tick`.
     *  @return true if the tick was clamped. */
    constexpr bool check_tick(this Tick& self, Tick tick) noexcept {
        auto age = tick.relative_to(self);
        if (age.tick > internal::MAX_CHANGE_AGE) {
            self = tick.relative_to(Tick(internal::MAX_CHANGE_AGE));
            return true;
        }
        return false;
    }
};
EPIX_EXPORT struct ComponentTicks {
    Tick added;
    Tick modified;

    ComponentTicks() noexcept : added(0), modified(0) {}
    ComponentTicks(Tick tick) noexcept : added(tick), modified(tick) {}
    ComponentTicks(Tick added, Tick modified) noexcept : added(added), modified(modified) {}
};
EPIX_EXPORT struct TickRefs {
   public:
    explicit TickRefs(Tick* added, Tick* modified) noexcept : _added(added), _modified(modified) {}
    Tick& added(this const TickRefs& self) noexcept { return *self._added; }
    Tick& modified(this const TickRefs& self) noexcept { return *self._modified; }

   private:
    Tick* _added;
    Tick* _modified;
};

EPIX_EXPORT struct Ticks {
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
EPIX_EXPORT struct TicksMut {
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
namespace internal {
template <typename T>
concept refable = !std::is_reference_v<T> && !std::is_const_v<T>;
}  // namespace internal
}  // namespace epix::ecs