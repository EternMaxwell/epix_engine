#pragma once

#include "world.h"

namespace epix::app {
template <typename T>
struct Res {
    using type = std::decay_t<T>;

   private:
    std::shared_ptr<T> resource;
    std::shared_ptr<std::shared_mutex> mutex;

   public:
    Res(const UntypedRes& res) {
        if (res.type != typeid(type)) {
            throw std::runtime_error(
                "Resource type mismatch: expected {" +
                std::string(typeid(type).name()) + "}, got {" +
                std::string(res.type.name()) + "}"
            );
        }
        resource = std::static_pointer_cast<T>(res.resource);
        mutex    = res.mutex;
    }

    static std::optional<Res<T>> from_world(const World& world) noexcept {
        auto res = world.get_untyped_resource(typeid(type));
        if (res) {
            return Res<T>(*res);
        }
        return std::nullopt;
    }

    Res(const Res& other) = default;
    Res(Res&& other) : Res(other) {};
    Res& operator=(const Res& other) = default;
    Res& operator=(Res&& other) {
        resource = other.resource;
        mutex    = other.mutex;
        return *this;
    }

    void lock() const noexcept {
        if (mutex) mutex->lock_shared();
    }
    void unlock() const noexcept {
        if (mutex) mutex->unlock_shared();
    }

    const T& operator*() const noexcept { return *resource; }
    const T* operator->() const noexcept { return resource.get(); }
    const T* get() const noexcept { return resource.get(); }
};
template <typename T>
struct ResMut {
    using type = std::decay_t<T>;

   private:
    std::shared_ptr<T> resource;
    std::shared_ptr<std::shared_mutex> mutex;

   public:
    ResMut(const UntypedRes& res) {
        if (res.type != typeid(type)) {
            throw std::runtime_error(
                "Resource type mismatch: expected {" +
                std::string(typeid(type).name()) + "}, got {" +
                std::string(res.type.name()) + "}"
            );
        }
        resource = std::static_pointer_cast<T>(res.resource);
        mutex    = res.mutex;
    }

    static std::optional<ResMut<T>> from_world(const World& world) noexcept {
        auto res = world.get_untyped_resource(typeid(type));
        if (res) {
            return ResMut<T>(*res);
        }
        return std::nullopt;
    }

    ResMut(const ResMut& other) = default;
    ResMut(ResMut&& other) : ResMut(other) {};
    ResMut& operator=(const ResMut& other) = default;
    ResMut& operator=(ResMut&& other) {
        resource = other.resource;
        mutex    = other.mutex;
        return *this;
    }

    void lock() const noexcept {
        if (mutex) {
            if constexpr (std::is_const_v<T>) {
                mutex->lock_shared();
            } else {
                mutex->lock();
            }
        }
    }
    void unlock() const noexcept {
        if (mutex) {
            if constexpr (std::is_const_v<T>) {
                mutex->unlock_shared();
            } else {
                mutex->unlock();
            }
        }
    }

    T& operator*() const noexcept { return *resource; }
    T* operator->() const noexcept { return resource.get(); }
    T* get() const noexcept { return resource.get(); }

    operator Res<T>() const noexcept {
        return Res<T>(UntypedRes::create(resource, mutex));
    }
};
}  // namespace epix::app