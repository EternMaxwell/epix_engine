module;

export module epix.core:system.input;

import std;

namespace epix::core {
template <typename T>
struct SystemInput;

template <typename T>
concept system_input = requires {
    // The parameter type that is used as input in system.(particularly first argument of function systems)
    typename SystemInput<T>::Param;
    typename SystemInput<T>::Input;
    {
        SystemInput<T>::wrap_input(std::declval<typename SystemInput<T>::Input>())
    } -> std::same_as<typename SystemInput<T>::Param>;
};

/** @brief System input wrapper for movable/copyable values.
 *  Wraps a value passed as input to a system function.
 *  @tparam T Value type. */
export template <typename T>
    requires std::movable<T> || std::copyable<T>
struct In {
   public:
    /** @brief Default-construct with a value-initialized T. */
    In() = default;
    /** @brief Copy-construct from a const reference. */
    In(const T& value)
        requires std::copyable<T>
        : value(value) {}
    /** @brief Move-construct from an rvalue. */
    In(T&& value)
        requires std::movable<T>
        : value(std::move(value)) {}

    /** @brief Get a const reference to the wrapped value. */
    const T& get() const { return value; }
    /** @brief Get a mutable reference to the wrapped value. */
    T& get() { return value; }
    /** @brief Const pointer access to the wrapped value. */
    const T* operator->() const { return std::addressof(value); }
    /** @brief Mutable pointer access to the wrapped value. */
    T* operator->() { return std::addressof(value); }
    /** @brief Const dereference. */
    const T& operator*() const { return value; }
    /** @brief Mutable dereference. */
    T& operator*() { return value; }
    operator const T&() const { return value; }
    operator T&() { return value; }

   private:
    T value;
};
/** @brief System input wrapper for copy-only values.
 *  @tparam T Copyable value type. */
export template <std::copyable T>
struct InCopy : public In<T> {
   public:
    using In<T>::In;
};
/** @brief System input wrapper for move-only values.
 *  @tparam T Movable value type. */
export template <std::movable T>
struct InMove : public In<T> {
   public:
    using In<T>::In;
};
/** @brief System input wrapper providing const reference access.
 *  @tparam T Value type. */
export template <typename T>
struct InRef {
   public:
    /** @brief Construct from a const reference. */
    InRef(const T& value) : value(std::addressof(value)) {}

    /** @brief Get the referenced value. */
    const T& get() const { return *value; }
    /** @brief Const pointer access. */
    const T* operator->() const { return value; }
    /** @brief Const dereference. */
    const T& operator*() const { return *value; }
    operator const T&() const { return *value; }

   private:
    const T* value;
};
/** @brief System input wrapper providing mutable reference access.
 *  @tparam T Value type. */
export template <typename T>
struct InMut {
   public:
    /** @brief Construct from a mutable reference. */
    InMut(T& value) : value(std::addressof(value)) {}

    /** @brief Get a mutable reference. */
    T& get() { return *value; }
    /** @brief Mutable pointer access. */
    T* operator->() { return value; }
    /** @brief Mutable dereference. */
    T& operator*() { return *value; }
    /** @brief Implicit conversion to mutable reference. */
    operator T&() { return *value; }
    /** @brief Get a const reference. */
    const T& get() const { return *value; }
    /** @brief Const pointer access. */
    const T* operator->() const { return value; }
    /** @brief Const dereference. */
    const T& operator*() const { return *value; }
    /** @brief Implicit conversion to const reference. */
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

template <system_input... Ts>
struct SystemInput<std::tuple<Ts...>> {
    using Param = std::tuple<typename SystemInput<Ts>::Param...>;
    using Input = std::tuple<typename SystemInput<Ts>::Input...>;
    static Param wrap_input(Input input) {
        return []<std::size_t... Is>(std::index_sequence<Is...>, Input input) {
            return Param(SystemInput<Ts>::wrap_input(std::get<Is>(input))...);
        }(std::index_sequence_for<Ts...>{}, std::move(input));
    }
};
}  // namespace core