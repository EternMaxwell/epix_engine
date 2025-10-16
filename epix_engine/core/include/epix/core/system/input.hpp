#pragma once

#include <concepts>
#include <tuple>

namespace epix::core::system {
template <typename T>
concept valid_system_input = requires {
    // The parameter type that is used as input in system.(particularly first argument of function systems)
    typename T::Param;
    typename T::Input;
    { T::wrap_input(std::declval<typename T::Input>()) } -> std::same_as<typename T::Param>;
};
template <typename T>
struct SystemInput;

template <typename T>
    requires std::movable<T> || std::copyable<T>
struct In {
   public:
    In() = default;
    In(const T& value) : value(value) {}
    In(T&& value) : value(std::move(value)) {}

    const T& get() const { return value; }
    T& get() { return value; }  // since this does not consider change detection, no explicit `mut` is needed.
    const T* operator->() const { return std::addressof(value); }
    T* operator->() { return std::addressof(value); }
    const T& operator*() const { return value; }
    T& operator*() { return value; }
    operator const T&() const { return value; }
    operator T&() { return value; }

   private:
    T value;
};
template <typename T>
    requires std::copyable<T>
struct InCopy : public In<T> {
   public:
    using In<T>::In;
};
template <typename T>
    requires std::movable<T>
struct InMove : public In<T> {
   public:
    using In<T>::In;
};
template <typename T>
struct InRef {
   public:
    InRef(const T& value) : value(std::addressof(value)) {}

    const T& get() const { return *value; }
    const T* operator->() const { return value; }
    const T& operator*() const { return *value; }
    operator const T&() const { return *value; }

   private:
    const T* value;
};
template <typename T>
struct InMut {
   public:
    InMut(T& value) : value(std::addressof(value)) {}

    T& get() { return *value; }
    T* operator->() { return value; }
    T& operator*() { return *value; }
    operator T&() { return *value; }
    const T& get() const { return *value; }
    const T* operator->() const { return value; }
    const T& operator*() const { return *value; }
    operator const T&() const { return *value; }

   private:
    T* value;
};

template <typename T>
    requires std::movable<T> || std::copyable<T>
struct SystemInput<In<T>> {
    using Param = In<T>;
    using Input = T;
    static Param wrap_input(Input input) { return Param(std::move(input)); }
};
template <typename T>
    requires std::copyable<T>
struct SystemInput<InCopy<T>> {
    using Param = InCopy<T>;
    using Input = const T&;
    static Param wrap_input(Input input) { return Param(input); }
};
template <typename T>
    requires std::movable<T>
struct SystemInput<InMove<T>> {
    using Param = InMove<T>;
    using Input = T&&;
    static Param wrap_input(Input input) { return Param(std::move(input)); }
};
template <typename T>
struct SystemInput<InRef<T>> {
    using Param = InRef<T>;
    using Input = const T&;
    static Param wrap_input(Input input) { return Param(input); }
};
template <typename T>
struct SystemInput<InMut<T>> {
    using Param = InMut<T>;
    using Input = T&;
    static Param wrap_input(Input input) { return Param(input); }
};

template <typename... Ts>
    requires(valid_system_input<SystemInput<Ts>> && ...)
struct SystemInput<std::tuple<Ts...>> {
    using Param = std::tuple<typename SystemInput<Ts>::Param...>;
    using Input = std::tuple<typename SystemInput<Ts>::Input...>;
    static Param wrap_input(Input input) {
        return []<size_t... Is>(std::index_sequence<Is...>, Input input) {
            return Param(SystemInput<Ts>::wrap_input(std::get<Is>(input))...);
        }(std::index_sequence_for<Ts...>{}, std::move(input));
    }
};
}  // namespace epix::core::system