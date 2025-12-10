/**
 * @file epix.core.meta.cppm
 * @brief C++20 module interface for epix core meta subsystem.
 *
 * This module provides compile-time type reflection utilities including
 * type_id for getting type names and hash codes, and type_index for
 * runtime type comparison.
 */
module;

// Standard library includes needed for module interface
#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

// Compiler-specific macros for pretty function names
#if defined __clang__ || defined __GNUC__
#define EPIX_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define EPIX_PRETTY_FUNCTION_PREFIX '='
#define EPIX_PRETTY_FUNCTION_SUFFIX ']'
#elif defined _MSC_VER
#define EPIX_PRETTY_FUNCTION __FUNCSIG__
#define EPIX_PRETTY_FUNCTION_PREFIX '<'
#define EPIX_PRETTY_FUNCTION_SUFFIX '>'
#endif

export module epix.core.meta;

export namespace epix::core::meta {

/**
 * @brief Get the full type name of T at compile time.
 * @tparam T The type to get the name for.
 * @return A string_view containing the type name.
 */
template <typename T>
constexpr std::string_view type_name() {
    static std::string pretty_function{EPIX_PRETTY_FUNCTION};
    static auto first =
        pretty_function.find_first_not_of(' ', pretty_function.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1);
    static auto last  = pretty_function.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    static auto value = pretty_function.substr(first, last - first);
    return value;
}

/**
 * @brief Shorten a fully qualified type name by removing namespace prefixes.
 * @param str The full type name string.
 * @return A shortened version of the type name.
 */
constexpr std::string shorten(std::string_view str) {
    std::string result = std::string(str);
    while (true) {
        auto last_colon = result.rfind("::");
        if (last_colon == std::string::npos) break;
        constexpr std::array left_chars = std::array{
            '<', '(', '[', ',', ' ',  // characters that can appear before a template argument
        };
        std::vector lefts = left_chars | std::views::transform([&](char c) { return result.rfind(c, last_colon); }) |
                            std::views::filter([&](size_t pos) { return pos != std::string::npos; }) |
                            std::ranges::to<std::vector>();
        auto left_elem = std::ranges::max_element(lefts);
        auto left      = (left_elem != lefts.end()) ? *left_elem + 1 : 0;
        result         = result.substr(0, left) + result.substr(last_colon + 2);
    }
    return result;
}

/**
 * @brief Get a shortened type name of T at compile time.
 * @tparam T The type to get the short name for.
 * @return A string_view containing the shortened type name.
 */
template <typename T>
constexpr std::string_view short_name() {
    static std::string name = shorten(type_name<T>());
    return name;
}

/**
 * @brief Type identity and metadata accessor.
 *
 * Provides static methods to get the full name, short name, and hash code
 * for a type T. Used for compile-time type reflection.
 *
 * @tparam T The type to get identity information for.
 */
template <typename T>
struct type_id {
   public:
    /**
     * @brief Get the full name of the type.
     * @return String view of the full type name.
     */
    static std::string_view name() {
        static std::string_view value = type_name<T>();
        return value;
    }

    /**
     * @brief Get the shortened name of the type.
     * @return String view of the shortened type name.
     */
    static std::string_view short_name() {
        static std::string_view value = meta::short_name<T>();
        return value;
    }

    /**
     * @brief Get a hash code for the type.
     * @return Hash code based on the type name.
     */
    static size_t hash_code() {
        static size_t hash = std::hash<std::string_view>()(name());
        return hash;
    }
};

/**
 * @brief Runtime type index for type comparison.
 *
 * Wraps a type_id to provide a runtime-comparable type identifier.
 * Can be stored, compared, and hashed at runtime.
 */
struct type_index {
   private:
    struct Internal {
        std::string_view name;
        std::string_view short_name;
        size_t hash;
    };

    const Internal* inter;

    template <typename T>
    const Internal* get_internal() const {
        static Internal internal = {type_id<T>::name(), type_id<T>::short_name(), type_id<T>::hash_code()};
        return &internal;
    }

   public:
    /**
     * @brief Construct from a type_id.
     * @tparam T The type whose identity to store.
     */
    template <typename T>
    type_index(type_id<T>) : inter(get_internal<T>()) {}

    /**
     * @brief Default constructor creates an invalid type_index.
     */
    type_index() : inter(nullptr) {}

    bool operator==(const type_index& other) const noexcept {
        return inter == other.inter || (inter && other.inter && inter->name == other.inter->name);
    }
    bool operator!=(const type_index& other) const noexcept { return !(*this == other); }

    /**
     * @brief Get the full name of the type.
     */
    std::string_view name() const noexcept { return inter->name; }

    /**
     * @brief Get the shortened name of the type.
     */
    std::string_view short_name() const noexcept { return inter->short_name; }

    /**
     * @brief Get the hash code of the type.
     */
    size_t hash_code() const noexcept { return inter->hash; }

    /**
     * @brief Check if this type_index is valid (was constructed from a type_id).
     */
    bool valid() const noexcept { return inter != nullptr; }
};

}  // namespace epix::core::meta

// Export hash specialization
export template <>
struct std::hash<epix::core::meta::type_index> {
    size_t operator()(const epix::core::meta::type_index& ti) const noexcept { return ti.hash_code(); }
};
