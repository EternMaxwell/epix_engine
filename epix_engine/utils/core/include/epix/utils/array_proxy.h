#pragma once

#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <type_traits>

namespace epix::util {

template <typename T>
class ArrayProxy : public std::ranges::view_interface<ArrayProxy<T>> {
   public:
    constexpr ArrayProxy() noexcept : m_count(0), m_ptr(nullptr) {}

    constexpr ArrayProxy(std::nullptr_t) noexcept
        : m_count(0), m_ptr(nullptr) {}

    ArrayProxy(T const &value) noexcept : m_count(1), m_ptr(&value) {}

    ArrayProxy(uint32_t count, T const *ptr) noexcept
        : m_count(count), m_ptr(ptr) {}

    template <std::size_t C>
    ArrayProxy(T const (&ptr)[C]) noexcept : m_count(C), m_ptr(ptr) {}

    ArrayProxy(std::initializer_list<T> const &list) noexcept
        : m_count(static_cast<uint32_t>(list.size())), m_ptr(list.begin()) {}

    template <
        typename B                                                  = T,
        typename std::enable_if<std::is_const<B>::value, int>::type = 0>
    ArrayProxy(
        std::initializer_list<typename std::remove_const<T>::type> const &list
    ) noexcept
        : m_count(static_cast<uint32_t>(list.size())), m_ptr(list.begin()) {}

    // Any type with a .data() return type implicitly convertible to T*, and a
    // .size() return type implicitly convertible to size_t. The const version
    // can capture temporaries, with lifetime ending at end of statement.
    template <
        typename V,
        typename std::enable_if<
            std::is_convertible<decltype(std::declval<V>().data()), T *>::
                value &&
            std::is_convertible<
                decltype(std::declval<V>().size()),
                std::size_t>::value>::type * = nullptr>
    ArrayProxy(V const &v) noexcept
        : m_count(static_cast<uint32_t>(v.size())), m_ptr(v.data()) {}

    const T *begin() const noexcept { return m_ptr; }

    const T *end() const noexcept { return m_ptr + m_count; }

    const T &front() const noexcept {
        assert(m_count && m_ptr);
        return *m_ptr;
    }

    const T &back() const noexcept {
        assert(m_count && m_ptr);
        return *(m_ptr + m_count - 1);
    }

    bool empty() const noexcept { return (m_count == 0); }

    uint32_t size() const noexcept { return m_count; }

    T const *data() const noexcept { return m_ptr; }

   private:
    uint32_t m_count;
    T const *m_ptr;
};
}  // namespace epix::util