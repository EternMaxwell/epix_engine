/**
 * @file epix.core-query.cppm
 * @brief Query partition for entity queries
 */

export module epix.core:query;

import :fwd;
import :entities;
import :type_system;
import :component;
import :archetype;
import :tick;
import :change_detection;

#include <concepts>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export namespace epix::core::query {
    // Query access types
    enum class Access {
        Read,
        Write,
    };
    
    // Query fetch traits
    template <typename T>
    struct Fetch;
    
    template <typename T>
    struct QueryData;
    
    template <typename T>
    struct QueryFilter;
    
    // Filter types
    template <typename... Ts>
    struct With {};
    
    template <typename... Ts>
    struct Without {};
    
    template <typename... Ts>
    struct Or {};
    
    template <typename... Ts>
    struct Filter {};
    
    template <typename T>
    struct Added {};
    
    template <typename T>
    struct Modified {};
    
    template <typename T>
    struct Has {};
    
    // Optional wrapper
    template <typename T>
    struct Opt {};
    
    // Query iteration
    template <typename D, typename F>
    struct QueryIter;
    
    template <typename D, typename F>
    struct QueryState;
    
    template <typename D, typename F = Filter<>>
    struct Query;
    
    template <typename D>
    struct Single;
    
    template <typename T>
    struct Item;
    
    // Validation
    template <typename T>
    concept valid_query_data = requires { typename QueryData<T>; };
    
    template <typename T>
    concept valid_query_filter = requires { typename QueryFilter<T>; };
}  // namespace epix::core::query
