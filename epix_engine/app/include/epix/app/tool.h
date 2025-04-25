#pragma once

#include <epix/common.h>

#include <memory>
#include <string>
#include <typeindex>

namespace epix::app::tools {
template <typename T>
struct t_weak_ptr : std::weak_ptr<T> {
    t_weak_ptr(std::shared_ptr<T> ptr) noexcept : std::weak_ptr<T>(ptr) {}
    t_weak_ptr(std::weak_ptr<T> ptr) noexcept : std::weak_ptr<T>(ptr) {}

    T* get_p() noexcept { return *reinterpret_cast<T**>(this); }
};
struct Label {
   protected:
    std::type_index type;
    size_t index;

    template <typename U>
    Label(U u) noexcept : type(typeid(U)), index(0) {
        if constexpr (std::is_enum_v<U>) {
            index = static_cast<size_t>(u);
        }
    }
    EPIX_API Label(std::type_index t, size_t i) noexcept;
    EPIX_API Label() noexcept;

   public:
    Label(const Label&)            = default;
    Label(Label&&)                 = default;
    Label& operator=(const Label&) = default;
    Label& operator=(Label&&)      = default;
    EPIX_API bool operator==(const Label& other) const noexcept;
    EPIX_API bool operator!=(const Label& other) const noexcept;
    EPIX_API void set_type(std::type_index t) noexcept;
    EPIX_API void set_index(size_t i) noexcept;
    EPIX_API size_t hash_code() const noexcept;
    EPIX_API std::string name() const noexcept;
};
}  // namespace epix::app::tools

template <typename T>
struct std::hash<std::weak_ptr<T>> {
    size_t operator()(const std::weak_ptr<T>& ptr) const {
        epix::app::tools::t_weak_ptr<T> tptr(ptr);
        return std::hash<T*>()(tptr.get_p());
    }
};
template <std::derived_from<epix::app::tools::Label> T>
struct std::hash<T> {
    size_t operator()(const T& label) const
        noexcept(noexcept(label.hash_code())) {
        return label.hash_code();
    }
};

template <typename T>
struct std::equal_to<std::weak_ptr<T>> {
    bool operator()(const std::weak_ptr<T>& a, const std::weak_ptr<T>& b)
        const noexcept {
        epix::app::tools::t_weak_ptr<T> aptr(a);
        epix::app::tools::t_weak_ptr<T> bptr(b);
        return aptr.get_p() == bptr.get_p();
    }
};

namespace epix::app {
struct Entity {
   private:
    entt::entity id;

   public:
    EPIX_API Entity(entt::entity id) noexcept;
    EPIX_API Entity() noexcept;

    EPIX_API operator entt::entity() const noexcept;
    EPIX_API operator bool() const noexcept;
    EPIX_API Entity& operator=(entt::entity id) noexcept;
    EPIX_API bool operator!() const noexcept;
    EPIX_API bool operator==(const Entity& other) const noexcept;
    EPIX_API bool operator!=(const Entity& other) const noexcept;
    EPIX_API bool operator==(const entt::entity& other) const noexcept;
    EPIX_API bool operator!=(const entt::entity& other) const noexcept;
    EPIX_API size_t index() const noexcept;
    EPIX_API size_t hash_code() const noexcept;
};
}  // namespace epix::app
