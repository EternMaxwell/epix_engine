/**
 * @file epix.core-meta.cppm
 * @brief Type metadata partition for runtime type information
 */

export module epix.core:meta;

import :fwd;

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

// Compiler-specific macro for getting pretty function name
#if defined __clang__ || defined __GNUC__
    #define EPIX_PRETTY_FUNCTION __PRETTY_FUNCTION__
    #define EPIX_PRETTY_FUNCTION_PREFIX '='
    #define EPIX_PRETTY_FUNCTION_SUFFIX ']'
#elif defined _MSC_VER
    #define EPIX_PRETTY_FUNCTION __FUNCSIG__
    #define EPIX_PRETTY_FUNCTION_PREFIX '<'
    #define EPIX_PRETTY_FUNCTION_SUFFIX '>'
#endif

export namespace epix::core::meta {
    
    /**
     * Get the full type name as a string_view at compile time
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
     * Shorten a fully-qualified type name by removing namespace prefixes
     */
    static constexpr std::string shorten(std::string_view str) {
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
     * Get shortened type name
     */
    template <typename T>
    constexpr std::string_view short_name() {
        static std::string name = shorten(type_name<T>());
        return name;
    }

    /**
     * Compile-time type identifier
     */
    template <typename T>
    struct type_id {
       public:
        static std::string_view name() {
            static std::string_view value = type_name<T>();
            return value;
        }
        static std::string_view short_name() {
            static std::string_view value = meta::short_name<T>();
            return value;
        }
        static size_t hash_code() {
            static size_t hash = std::hash<std::string_view>()(name());
            return hash;
        }
    };
    
    /**
     * Runtime type index for type comparison and hashing
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
        template <typename T>
        type_index(type_id<T>) : inter(get_internal<T>()) {}
        type_index() : inter(nullptr) {}

        bool operator==(const type_index& other) const noexcept {
            return inter == other.inter || inter->name == other.inter->name;
        }
        bool operator!=(const type_index& other) const noexcept { return !(*this == other); }
        std::string_view name() const noexcept { return inter->name; }
        std::string_view short_name() const noexcept { return inter->short_name; }
        size_t hash_code() const noexcept { return inter->hash; }
        bool valid() const noexcept { return inter != nullptr; }
    };
}  // namespace epix::core::meta

// Export std::hash specialization for type_index
export namespace std {
    template <>
    struct hash<epix::core::meta::type_index> {
        size_t operator()(const epix::core::meta::type_index& ti) const noexcept { 
            return ti.hash_code(); 
        }
    };
}  // namespace std
