#pragma once

/**
 * @file cpp23_compat.hpp
 * @brief C++23 compatibility layer using feature test macros
 * 
 * This header provides compatibility shims for C++23 features that may not be
 * available in all standard library implementations. It uses feature test macros
 * to detect availability and provides fallback implementations when needed.
 */

// Try to include <version> for feature test macros, fallback to specific headers
#if __has_include(<version>)
#include <version>
#endif

#include <functional>
#include <ranges>
#include <utility>

namespace epix::compat {

// ============================================================================
// std::move_only_function compatibility
// ============================================================================

#if __cpp_lib_move_only_function >= 202110L
    // C++23 feature available - use standard implementation
    template<typename Signature>
    using move_only_function = std::move_only_function<Signature>;
#else
    // Fallback implementation for std::move_only_function
    // This is a simplified version that provides move-only semantics
    template<typename>
    class move_only_function;

    template<typename R, typename... Args>
    class move_only_function<R(Args...)> {
    private:
        struct callable_base {
            virtual ~callable_base() = default;
            virtual R invoke(Args... args) = 0;
            virtual callable_base* clone_move() = 0;
        };

        template<typename F>
        struct callable : callable_base {
            F func;
            
            explicit callable(F&& f) : func(std::move(f)) {}
            
            R invoke(Args... args) override {
                return func(std::forward<Args>(args)...);
            }
            
            callable_base* clone_move() override {
                return new callable(std::move(func));
            }
        };

        callable_base* ptr_ = nullptr;

    public:
        move_only_function() = default;
        
        template<typename F>
            requires (!std::same_as<std::decay_t<F>, move_only_function>)
        move_only_function(F&& f) 
            : ptr_(new callable<std::decay_t<F>>(std::forward<F>(f))) {}
        
        ~move_only_function() { delete ptr_; }
        
        // Move-only semantics
        move_only_function(const move_only_function&) = delete;
        move_only_function& operator=(const move_only_function&) = delete;
        
        move_only_function(move_only_function&& other) noexcept 
            : ptr_(std::exchange(other.ptr_, nullptr)) {}
        
        move_only_function& operator=(move_only_function&& other) noexcept {
            if (this != &other) {
                delete ptr_;
                ptr_ = std::exchange(other.ptr_, nullptr);
            }
            return *this;
        }
        
        R operator()(Args... args) {
            if (!ptr_) {
                throw std::bad_function_call();
            }
            return ptr_->invoke(std::forward<Args>(args)...);
        }
        
        explicit operator bool() const noexcept { return ptr_ != nullptr; }
    };
#endif

// ============================================================================
// std::ranges::to compatibility
// ============================================================================

#if __cpp_lib_ranges_to_container >= 202202L
    // C++23 feature available - use standard implementation
    namespace ranges {
        using std::ranges::to;
    }
#else
    // Fallback implementation for std::ranges::to
    namespace ranges {
        namespace detail {
            template<typename C, typename R>
            C to_impl(R&& range) {
                if constexpr (std::ranges::sized_range<R>) {
                    C result;
                    if constexpr (requires { result.reserve(std::ranges::size(range)); }) {
                        result.reserve(std::ranges::size(range));
                    }
                    for (auto&& elem : range) {
                        result.push_back(std::forward<decltype(elem)>(elem));
                    }
                    return result;
                } else {
                    C result;
                    for (auto&& elem : range) {
                        result.push_back(std::forward<decltype(elem)>(elem));
                    }
                    return result;
                }
            }
        }

        template<typename C>
        struct to_fn {
            template<std::ranges::input_range R>
            auto operator()(R&& range) const {
                return detail::to_impl<C>(std::forward<R>(range));
            }
        };

        template<typename C>
        inline constexpr to_fn<C> to{};
    }
#endif

// ============================================================================
// std::ranges::views::enumerate compatibility
// ============================================================================

#if __cpp_lib_ranges_enumerate >= 202302L
    // C++23 feature available - use standard implementation
    namespace views {
        using std::ranges::views::enumerate;
    }
#else
    // Fallback implementation for std::ranges::views::enumerate
    namespace views {
        namespace detail {
            template<std::ranges::view V>
            class enumerate_view : public std::ranges::view_interface<enumerate_view<V>> {
            private:
                V base_;
                
                template<bool Const>
                class iterator {
                private:
                    using Base = std::conditional_t<Const, const V, V>;
                    using BaseIter = std::ranges::iterator_t<Base>;
                    
                    BaseIter current_{};
                    std::size_t index_ = 0;
                    
                public:
                    using difference_type = std::ranges::range_difference_t<Base>;
                    using value_type = std::pair<std::size_t, std::ranges::range_reference_t<Base>>;
                    
                    iterator() = default;
                    
                    iterator(BaseIter current, std::size_t index)
                        : current_(current), index_(index) {}
                    
                    auto operator*() const {
                        return std::pair<std::size_t, std::ranges::range_reference_t<Base>>{
                            index_, *current_
                        };
                    }
                    
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
                    
                    bool operator==(const iterator& other) const {
                        return current_ == other.current_;
                    }
                    
                    bool operator!=(const iterator& other) const {
                        return !(*this == other);
                    }
                };
                
            public:
                enumerate_view() = default;
                
                explicit enumerate_view(V base) : base_(std::move(base)) {}
                
                auto begin() { return iterator<false>(std::ranges::begin(base_), 0); }
                auto begin() const { return iterator<true>(std::ranges::begin(base_), 0); }
                
                auto end() { 
                    auto end_iter = std::ranges::end(base_);
                    auto size = std::ranges::distance(std::ranges::begin(base_), end_iter);
                    return iterator<false>(end_iter, static_cast<std::size_t>(size)); 
                }
                auto end() const { 
                    auto end_iter = std::ranges::end(base_);
                    auto size = std::ranges::distance(std::ranges::begin(base_), end_iter);
                    return iterator<true>(end_iter, static_cast<std::size_t>(size)); 
                }
            };
        }
        
        struct enumerate_fn {
            template<std::ranges::viewable_range R>
            auto operator()(R&& range) const {
                return detail::enumerate_view(std::views::all(std::forward<R>(range)));
            }
        };
        
        inline constexpr enumerate_fn enumerate{};
    }
#endif

// ============================================================================
// Container insert_range compatibility
// ============================================================================

#if __cpp_lib_containers_ranges >= 202202L
    // C++23 feature available - no shim needed
#else
    // Provide insert_range as a free function for containers that don't have it
    template<typename Container, typename Range>
    auto insert_range(Container& container, typename Container::iterator pos, Range&& range)
        -> typename Container::iterator
    {
        // For containers with insert(iter, first, last), use that for efficiency
        if constexpr (requires { container.insert(pos, std::ranges::begin(range), std::ranges::end(range)); }) {
            return container.insert(pos, std::ranges::begin(range), std::ranges::end(range));
        } else {
            // Fallback for containers that don't support range insert
            auto result = pos;
            for (auto&& elem : range) {
                result = container.insert(result, std::forward<decltype(elem)>(elem));
                ++result;
            }
            return result;
        }
    }
#endif

} // namespace epix::compat

// Convenience macros for using compatibility layer
#define EPIX_MOVE_ONLY_FUNCTION epix::compat::move_only_function
#define EPIX_RANGES_TO epix::compat::ranges::to
#define EPIX_VIEWS_ENUMERATE epix::compat::views::enumerate

// For insert_range, use the member function if available, otherwise use free function
#if __cpp_lib_containers_ranges >= 202202L
    #define EPIX_INSERT_RANGE(container, pos, range) (container).insert_range((pos), (range))
#else
    #define EPIX_INSERT_RANGE(container, pos, range) epix::compat::insert_range((container), (pos), (range))
#endif
