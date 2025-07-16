#pragma once

#include "systemparam.h"
#include "world.h"

namespace epix::app {
template <typename T>
constexpr bool enable_mutable_res = false;
template <typename T>
struct Res {
    using type = std::conditional_t<enable_mutable_res<T>,
                                    std::decay_t<T>,
                                    std::add_const_t<std::decay_t<T>>>;

   private:
    type* resource;

    Res(type* res) : resource(res) {}

   public:
    static std::optional<Res<T>> from_world(World& world) noexcept {
        auto res = world.get_resource<T>();
        if (res) {
            return Res<T>(res);
        }
        return std::nullopt;
    }

    Res(const Res& other)            = default;
    Res(Res&& other)                 = default;
    Res& operator=(const Res& other) = default;
    Res& operator=(Res&& other)      = default;

    type& operator*() const noexcept { return *resource; }
    type* operator->() const noexcept { return resource; }
    type& get() const noexcept { return *resource; }
    operator type*() const noexcept { return resource; }
};
template <typename T>
struct ResMut {
    using type = std::decay_t<T>;

   private:
    T* resource;

   public:
    ResMut(T* res) : resource(res) {}

    static std::optional<ResMut<T>> from_world(World& world) noexcept {
        auto res = world.get_resource<T>();
        if (res) {
            return ResMut<T>(res);
        }
        return std::nullopt;
    }

    ResMut(const ResMut& other)            = default;
    ResMut(ResMut&& other)                 = default;
    ResMut& operator=(const ResMut& other) = default;
    ResMut& operator=(ResMut&& other)      = default;

    T& operator*() const noexcept { return *resource; }
    T* operator->() const noexcept { return resource; }
    T& get() const noexcept { return *resource; }
    operator T*() const noexcept { return resource; }

    operator Res<T>() const noexcept { return Res<T>(resource); }
};

template <typename T>
struct SystemParam<Res<T>> {
    using State = std::optional<Res<T>>;
    State init(SystemMeta& meta) {
        meta.access.resource_reads.emplace(meta::type_id<std::decay_t<T>>{});
        return std::nullopt;
    }
    bool update(State& state, World& world, const SystemMeta&) {
        state = Res<T>::from_world(world);
        return state.has_value();
    }
    Res<T>& get(State& state) {
        if (!state.has_value()) {
            throw std::runtime_error("Resource not found");
        }
        return *state;
    }
};
template <typename T>
struct SystemParam<std::optional<Res<T>>> {
    using State = std::optional<Res<T>>;
    State init(SystemMeta& meta) {
        meta.access.resource_reads.emplace(meta::type_id<std::decay_t<T>>{});
        return std::nullopt;
    }
    bool update(State& state, World& world, const SystemMeta&) {
        state = Res<T>::from_world(world);
        return true;
    }
    std::optional<Res<T>>& get(State& state) { return state; }
};
template <typename T>
struct SystemParam<ResMut<T>> {
    using State = std::optional<ResMut<T>>;
    State init(SystemMeta& meta) {
        meta.access.resource_writes.emplace(meta::type_id<std::decay_t<T>>{});
        return std::nullopt;
    }
    bool update(State& state, World& world, const SystemMeta&) {
        state = ResMut<T>::from_world(world);
        return state.has_value();
    }
    ResMut<T>& get(State& state) {
        if (!state.has_value()) {
            throw std::runtime_error("Resource not found");
        }
        return *state;
    }
};
template <typename T>
struct SystemParam<std::optional<ResMut<T>>> {
    using State = std::optional<ResMut<T>>;
    State init(SystemMeta& meta) {
        meta.access.resource_writes.emplace(meta::type_id<std::decay_t<T>>{});
        return std::nullopt;
    }
    bool update(State& state, World& world, const SystemMeta&) {
        state = ResMut<T>::from_world(world);
        return true;
    }
    std::optional<ResMut<T>>& get(State& state) { return state; }
};
static_assert(ValidParam<Res<int>> && ValidParam<std::optional<Res<int>>> &&
                  ValidParam<ResMut<int>> &&
                  ValidParam<std::optional<ResMut<int>>>,
              "Res and ResMut should be valid parameters for systems.");
}  // namespace epix::app