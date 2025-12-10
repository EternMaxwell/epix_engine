/**
 * @file module_compat.hpp
 * @brief Compatibility layer for C++20 modules.
 *
 * This header provides macros and utilities to support both traditional
 * header-based compilation and C++20 module-based compilation.
 *
 * Usage:
 * - Define EPIX_USE_MODULES to enable module mode
 * - Include this header before any epix headers to get proper configuration
 */
#pragma once

// Feature test macros for C++20 modules support
#if defined(__cpp_modules) && __cpp_modules >= 201907L
#define EPIX_HAS_CPP20_MODULES 1
#else
#define EPIX_HAS_CPP20_MODULES 0
#endif

// Check for import/export support
#if defined(__cpp_lib_modules) && __cpp_lib_modules >= 202207L
#define EPIX_HAS_STD_MODULES 1
#else
#define EPIX_HAS_STD_MODULES 0
#endif

// Compiler-specific module support detection
#if defined(_MSC_VER) && _MSC_VER >= 1929
#define EPIX_MSVC_MODULES_SUPPORTED 1
#elif defined(__clang__) && __clang_major__ >= 16
#define EPIX_CLANG_MODULES_SUPPORTED 1
#elif defined(__GNUC__) && __GNUC__ >= 14
#define EPIX_GCC_MODULES_SUPPORTED 1
#endif

// Master switch for module usage
#if defined(EPIX_USE_MODULES) && EPIX_HAS_CPP20_MODULES
#define EPIX_MODULES_ENABLED 1
#else
#define EPIX_MODULES_ENABLED 0
#endif

// Export macro for module interface declarations
#if EPIX_MODULES_ENABLED
#define EPIX_EXPORT export
#define EPIX_MODULE_EXPORT export
#else
#define EPIX_EXPORT
#define EPIX_MODULE_EXPORT
#endif

// Import/include macro for conditional module usage
// In module mode: import the module
// In header mode: include the header
#if EPIX_MODULES_ENABLED
#define EPIX_IMPORT(module_name) import module_name
#define EPIX_INCLUDE_OR_IMPORT(header, module_name) import module_name
#else
#define EPIX_IMPORT(module_name)
#define EPIX_INCLUDE_OR_IMPORT(header, module_name) EPIX_INCLUDE_IMPL(header)
#define EPIX_INCLUDE_IMPL(header) _Pragma("once") // placeholder, actual include done normally
#endif

// Namespace visibility macros for module-private entities
#if EPIX_MODULES_ENABLED
#define EPIX_MODULE_PRIVATE
#else
#define EPIX_MODULE_PRIVATE
#endif

// Helper for conditional compilation in module vs header mode
#define EPIX_IF_MODULES(modules_code, header_code) \
    EPIX_IF_MODULES_IMPL(EPIX_MODULES_ENABLED, modules_code, header_code)
#define EPIX_IF_MODULES_IMPL(enabled, modules_code, header_code) \
    EPIX_IF_MODULES_IMPL2(enabled, modules_code, header_code)
#define EPIX_IF_MODULES_IMPL2(enabled, modules_code, header_code) \
    EPIX_IF_MODULES_##enabled(modules_code, header_code)
#define EPIX_IF_MODULES_0(modules_code, header_code) header_code
#define EPIX_IF_MODULES_1(modules_code, header_code) modules_code

// C++23 feature availability with fallbacks
// std::expected
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#define EPIX_HAS_STD_EXPECTED 1
#else
#define EPIX_HAS_STD_EXPECTED 0
#endif

// std::ranges::to
#if defined(__cpp_lib_ranges_to_container) && __cpp_lib_ranges_to_container >= 202202L
#define EPIX_HAS_RANGES_TO 1
#else
#define EPIX_HAS_RANGES_TO 0
#endif

// std::views::enumerate
#if defined(__cpp_lib_ranges_enumerate) && __cpp_lib_ranges_enumerate >= 202302L
#define EPIX_HAS_VIEWS_ENUMERATE 1
#else
#define EPIX_HAS_VIEWS_ENUMERATE 0
#endif

// std::move_only_function
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
#define EPIX_HAS_MOVE_ONLY_FUNCTION 1
#else
#define EPIX_HAS_MOVE_ONLY_FUNCTION 0
#endif

// Deducing this
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
#define EPIX_HAS_DEDUCING_THIS 1
#else
#define EPIX_HAS_DEDUCING_THIS 0
#endif

// Helper macros for deducing this compatibility
// When EPIX_HAS_DEDUCING_THIS is available, use the modern syntax
// Otherwise, these can be defined differently or code can be conditionally compiled
#if EPIX_HAS_DEDUCING_THIS
#define EPIX_THIS_PARAM(type) this type
#define EPIX_SELF self
#define EPIX_SELF_DOT self.
#else
// For non-deducing-this compilers, functions need traditional declarations
// This requires manual adjustment of each function signature
#define EPIX_THIS_PARAM(type) // Empty, requires traditional declaration
#define EPIX_SELF (*this)
#define EPIX_SELF_DOT this->
#endif

// Fallback implementations for missing C++23 features
namespace epix::compat {

// Fallback for ranges::to when not available
#if !EPIX_HAS_RANGES_TO
template <template <typename...> class Container>
struct to_container_adaptor {
    template <typename Range>
    auto operator()(Range&& range) const {
        using value_type = std::ranges::range_value_t<Range>;
        Container<value_type> result;
        if constexpr (std::ranges::sized_range<Range> && requires { result.reserve(size_t{}); }) {
            result.reserve(std::ranges::size(range));
        }
        for (auto&& elem : range) {
            result.push_back(std::forward<decltype(elem)>(elem));
        }
        return result;
    }
};

template <template <typename...> class Container>
inline constexpr to_container_adaptor<Container> to{};

// Pipe operator support
template <typename Range, template <typename...> class Container>
auto operator|(Range&& range, to_container_adaptor<Container> adaptor) {
    return adaptor(std::forward<Range>(range));
}
#endif

// Fallback for views::enumerate when not available
#if !EPIX_HAS_VIEWS_ENUMERATE
namespace detail {
template <typename Range>
class enumerate_view : public std::ranges::view_interface<enumerate_view<Range>> {
    Range base_;
    
public:
    explicit enumerate_view(Range base) : base_(std::move(base)) {}
    
    class iterator {
        std::ranges::iterator_t<Range> current_;
        std::size_t index_ = 0;
        
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<std::size_t, std::ranges::range_reference_t<Range>>;
        using reference = value_type;
        using pointer = void;
        
        iterator() = default;
        explicit iterator(std::ranges::iterator_t<Range> current, std::size_t index = 0)
            : current_(std::move(current)), index_(index) {}
        
        value_type operator*() const { return {index_, *current_}; }
        
        iterator& operator++() {
            ++current_;
            ++index_;
            return *this;
        }
        
        iterator operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }
        
        bool operator==(const iterator& other) const { return current_ == other.current_; }
        bool operator!=(const iterator& other) const { return !(*this == other); }
        bool operator==(std::ranges::sentinel_t<Range> sent) const { return current_ == sent; }
    };
    
    auto begin() { return iterator(std::ranges::begin(base_)); }
    auto end() { return std::ranges::end(base_); }
};

struct enumerate_fn {
    template <typename Range>
    auto operator()(Range&& range) const {
        return enumerate_view(std::forward<Range>(range));
    }
};
}  // namespace detail

inline constexpr detail::enumerate_fn enumerate{};

template <typename Range>
auto operator|(Range&& range, detail::enumerate_fn fn) {
    return fn(std::forward<Range>(range));
}
#endif

}  // namespace epix::compat

// Note: Adding to std namespace is technically undefined behavior per the C++ standard.
// These aliases are provided for convenience but users should prefer using epix::compat
// directly or waiting for proper compiler support.
// For maximum safety, use epix::compat::to and epix::compat::enumerate directly.
