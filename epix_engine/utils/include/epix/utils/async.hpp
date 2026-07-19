#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#endif

namespace epix::utils {
/** @brief Simple mutex wrapper that pairs a std::mutex with the data it protects.
 *
 * Access the protected data via lock() which returns a Guard, or via
 * with_lock() which takes a callable.
 * @tparam T The protected value type.
 */
EPIX_EXPORT template <typename T>
struct Mutex {
    /** @brief RAII guard that locks the mutex and provides access to the
     * protected value. */
    struct Guard {
       private:
        std::unique_lock<std::mutex> const lock;

       public:
        /** @brief Reference to the mutex-protected value. */
        T& ref;

        /** @brief Construct a guard, locking the given mutex. */
        Guard(std::mutex& mtx, T& val) : lock(mtx), ref(val) {}
        Guard(const Guard&)            = delete;
        Guard(Guard&&)                 = default;
        Guard& operator=(const Guard&) = delete;
        Guard& operator=(Guard&&)      = delete;

        /** @brief Access the protected value by pointer. */
        T* operator->() { return &ref; }
        /** @brief Dereference the guard to get the protected value. */
        T& operator*() { return ref; }
        /** @brief Access the protected value by const pointer. */
        const T* operator->() const { return &ref; }
        /** @brief Dereference the guard to get a const reference. */
        const T& operator*() const { return ref; }
        /** @brief Implicit conversion to a mutable reference. */
        operator T&() { return ref; }
        /** @brief Implicit conversion to a const reference. */
        operator const T&() const { return ref; }
    };

   private:
    mutable std::mutex m_mutex;
    union {
        mutable T m_value;
    };

   public:
    /** @brief Construct the protected value in-place.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     */
    template <typename... Args>
    Mutex(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        new (&m_value) T(std::forward<Args>(args)...);
    }
    Mutex(const Mutex&) = delete;
    /** @brief Move-construct by locking both mutexes and moving the value. */
    Mutex(Mutex&& other)
        requires std::move_constructible<T>
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        new (&m_value) T(std::move(other.m_value));
    }
    Mutex& operator=(const Mutex&) = delete;
    /** @brief Move-assign by locking both mutexes and moving the value. */
    Mutex& operator=(Mutex&& other)
        requires std::is_move_assignable<T>::value
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_value = std::move(other.m_value);
        return *this;
    }
    ~Mutex() {
        std::lock_guard lock(m_mutex);
        m_value.~T();
    }
    /** @brief Create a copy of the Mutex by cloning the protected value. */
    Mutex clone() const
        requires std::copy_constructible<T>
    {
        std::lock_guard lock(m_mutex);
        return Mutex(m_value);
    }

    /** @brief Execute a callable while holding the lock.
     * @tparam Func A callable accepting T&.
     * @param func The callable to invoke with the protected value.
     * @return The result of invoking func.
     */
    template <std::invocable<T&> Func>
    auto with_lock(Func&& func) const -> std::invoke_result_t<Func, T&> {
        std::lock_guard lock(m_mutex);
        return func(m_value);
    }

    /** @brief Lock the mutex and return a Guard that provides access to the
     * value. */
    Guard lock() const { return Guard(m_mutex, m_value); }
};
EPIX_EXPORT template <typename T>
struct RwLock {
   public:
    struct ReadGuard {
       private:
        std::shared_lock<std::shared_mutex> const lock;

       public:
        const T& ref;

        ReadGuard(std::shared_mutex& mtx, const T& val) : lock(mtx), ref(val) {}
        ReadGuard(const ReadGuard&)            = delete;
        ReadGuard(ReadGuard&&)                 = default;
        ReadGuard& operator=(const ReadGuard&) = delete;
        ReadGuard& operator=(ReadGuard&&)      = delete;

        const T* operator->() const { return &ref; }
        const T& operator*() const { return ref; }
        operator const T&() const { return ref; }
    };
    struct WriteGuard {
       private:
        std::unique_lock<std::shared_mutex> const lock;

       public:
        T& ref;

        WriteGuard(std::shared_mutex& mtx, T& val) : lock(mtx), ref(val) {}
        WriteGuard(const WriteGuard&)            = delete;
        WriteGuard(WriteGuard&&)                 = default;
        WriteGuard& operator=(const WriteGuard&) = delete;
        WriteGuard& operator=(WriteGuard&&)      = delete;

        T* operator->() { return &ref; }
        T& operator*() { return ref; }
        const T* operator->() const { return &ref; }
        const T& operator*() const { return ref; }
        operator T&() { return ref; }
        operator const T&() const { return ref; }
    };

   private:
    mutable std::shared_mutex m_mutex;
    union {
        mutable T m_value;
    };

   public:
    template <typename... Args>
    RwLock(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        new (&m_value) T(std::forward<Args>(args)...);
    }
    RwLock(const RwLock&) = delete;
    RwLock(RwLock&& other)
        requires std::move_constructible<T>
    {
        std::unique_lock lock(m_mutex, std::defer_lock);
        std::unique_lock other_lock(other.m_mutex, std::defer_lock);
        std::lock(lock, other_lock);
        new (&m_value) T(std::move(other.m_value));
    }
    RwLock& operator=(const RwLock&) = delete;
    RwLock& operator=(RwLock&& other)
        requires std::is_move_assignable<T>::value
    {
        std::unique_lock lock(m_mutex, std::defer_lock);
        std::unique_lock other_lock(other.m_mutex, std::defer_lock);
        std::lock(lock, other_lock);
        m_value = std::move(other.m_value);
        return *this;
    }
    RwLock clone() const
        requires std::copy_constructible<T>
    {
        std::shared_lock lock(m_mutex);
        return RwLock(m_value);
    }
    ~RwLock() {
        std::unique_lock lock(m_mutex);
        m_value.~T();
    }

    ReadGuard read() const { return ReadGuard(m_mutex, m_value); }
    WriteGuard write() const { return WriteGuard(m_mutex, m_value); }
};
}  // namespace epix::utils