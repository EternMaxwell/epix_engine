module;

export module epix.traits:templates;

import std;

template <typename T, template <typename...> typename Templated>
struct is_specialization_of : std::false_type {};
template <template <typename...> typename Templated, typename... Ts>
struct is_specialization_of<Templated<Ts...>, Templated> : std::true_type {};
template <typename V, typename T>
struct is_constructible_from_tuple : std::false_type {};
template <typename T, typename... Args>
struct is_constructible_from_tuple<T, std::tuple<Args...>> : std::bool_constant<std::is_constructible_v<T, Args...>> {};

export {
    /** @brief Concept that checks whether T is a specialization of a given class template. */
    template <typename T, template <typename...> typename Templated>
    concept specialization_of = is_specialization_of<T, Templated>::value;
    /** @brief Concept that checks whether V is constructible from the elements of tuple T. */
    template <typename V, typename T>
    concept constructible_from_tuple = is_constructible_from_tuple<V, T>::value;
    /** @brief Concept that checks whether T is a viewable range whose value type is V. */
    template <typename T, typename V>
    concept view_of_value = std::ranges::viewable_range<T> && std::same_as<std::ranges::range_value_t<T>, V>;
}  // namespace traits
