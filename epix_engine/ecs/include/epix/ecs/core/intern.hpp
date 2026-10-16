#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <memory>
#include <mutex>
#include <vector>
#endif

namespace epix::ecs {

template <typename T>
class Interner;

/**
 * @brief An interned value; stays valid until the end of the program and is
 * never dropped (Bevy `bevy_ecs::intern::Interned<T>`).
 *
 * Comparisons use reference equality (the stored pointer), so two interned
 * values are equal only if they were produced by the same `Interner` - which
 * is now one interner per process, not one per module.
 * @tparam T The value type (must be copy-constructible and equality
 * comparable).
 */
EPIX_EXPORT template <typename T>
class Interned {
   public:
    /** @brief Default-construct a null interned value. */
    Interned() = default;

    /** @brief Get the interned value pointer (never null after interning). */
    const T* get() const noexcept { return m_value; }
    /** @brief Dereference to the interned value. */
    const T& operator*() const noexcept { return *m_value; }
    /** @brief Pointer access to the interned value. */
    const T* operator->() const noexcept { return m_value; }
    /** @brief Reference equality: identical pointer only. */
    bool operator==(const Interned& other) const noexcept = default;
    /** @brief The static type identity of the interned value (Bevy interning
     * keeps values of different concrete label types distinct). */
    meta::type_index type() const noexcept { return meta::type_index(meta::type_id<T>()); }

   private:
    friend class Interner<T>;
    explicit Interned(const T* value) noexcept : m_value(value) {}
    const T* m_value = nullptr;
};

/** @brief Type-erased base of the per-value-type interners. */
EPIX_EXPORT struct InternerBase {
    virtual ~InternerBase() = default;
};

/**
 * @brief The process-wide interner for a value type (Bevy's static interner per
 * `define_label!`).
 *
 * The map this hands out from lives in the engine library rather than in a function-local
 * static in this header. In a shared build the module that interns a value is not necessarily
 * the module that looks it up - a material's erased-render-asset prepare is instantiated in
 * the consumer, its queue lives in the engine - and a per-module static hands them different
 * pointers for equal values, which is a silent miss everywhere identity is compared.
 */
EPIX_EXPORT EPIX_API InternerBase* interner_slot(const meta::type_index& value_type, InternerBase* (*make)());

/**
 * @brief A thread-safe interner which creates `Interned<T>` values from `&T`
 * (Bevy `bevy_ecs::intern::Interner<T>`).
 *
 * The first call for a value leaks a copy (the interner lives for the whole
 * program); subsequent calls for equal values return the same interned
 * pointer, so `Interned<T>` equality is pointer identity.
 * @tparam T The value type (must provide `operator==`).
 */
EPIX_EXPORT template <typename T>
class Interner : public InternerBase {
   public:
    /**
     * @brief Return the `Interned<T>` corresponding to `value`, leaking a
     * copy on first encounter.
     */
    Interned<T> intern(const T& value) {
        std::lock_guard lock(m_mutex);
        for (const auto& stored : m_values) {
            if (*stored == value) {
                return Interned<T>{stored.get()};
            }
        }
        auto leaked  = std::make_unique<T>(value);
        const T* ptr = leaked.get();
        m_values.push_back(std::move(leaked));
        return Interned<T>{ptr};
    }

   private:
    std::mutex m_mutex;
    std::vector<std::unique_ptr<T>> m_values;
};

/** @brief Process-wide interner instance for type T (Bevy's static `INTERNER` per
 *  `define_label!`).
 *
 *  The instance is owned by the engine library (`interner_slot`); only the factory pointer is
 *  cached here, which is a constant and so safe to duplicate per module. */
EPIX_EXPORT template <typename T>
Interner<T>& interner() {
    static InternerBase* (*make)() = []() -> InternerBase* { return new Interner<T>(); };
    return *static_cast<Interner<T>*>(interner_slot(meta::type_index(meta::type_id<T>()), make));
}

/** @brief Intern `value`, returning a stable `Interned<T>` (Bevy
 *  `label.intern()`). */
EPIX_EXPORT template <typename T>
Interned<T> intern(const T& value) {
    return interner<T>().intern(value);
}

}  // namespace epix::ecs
