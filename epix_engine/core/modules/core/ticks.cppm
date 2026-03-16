module;

export module epix.core:ticks;

import std;

import :tick;

namespace core {
struct Ticks {
    static Ticks from_ticks(const Tick& added, const Tick& modified, Tick last_run, Tick this_run) {
        return Ticks{&added, &modified, last_run, this_run};
    }
    static Ticks from_refs(TickRefs refs, Tick last_run, Tick this_run) {
        return Ticks{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added() const { return added->newer_than(last_run, this_run); }
    bool is_modified() const { return modified->newer_than(last_run, this_run); }
    Tick last_modified() const { return *modified; }
    Tick added_tick() const { return *added; }

   private:
    const Tick* added;
    const Tick* modified;
    Tick last_run;
    Tick this_run;

    Ticks(const Tick* added, const Tick* modified, Tick last_run, Tick this_run)
        : added(added), modified(modified), last_run(last_run), this_run(this_run) {}
};
struct TicksMut {
    static TicksMut from_ticks(Tick& added, Tick& modified, Tick last_run, Tick this_run) {
        return TicksMut{&added, &modified, last_run, this_run};
    }
    static TicksMut from_refs(TickRefs refs, Tick last_run, Tick this_run) {
        return TicksMut{&refs.added(), &refs.modified(), last_run, this_run};
    }

    bool is_added() const { return added->newer_than(last_run, this_run); }
    bool is_modified() const { return modified->newer_than(last_run, this_run); }
    Tick last_modified() const { return *modified; }
    Tick added_tick() const { return *added; }

    void set_modified() { modified->set(this_run.get()); }
    void set_added() {
        added->set(this_run.get());
        modified->set(this_run.get());
    }

   private:
    Tick* added;
    Tick* modified;
    Tick last_run;
    Tick this_run;

    TicksMut(Tick* added, Tick* modified, Tick last_run, Tick this_run)
        : added(added), modified(modified), last_run(last_run), this_run(this_run) {}
};
/** @brief Trait to opt into copy semantics for Ref<T>.
 *
 * Specialize as `std::true_type` for types that should be copied
 * rather than referenced when wrapped in Ref.
 */
export template <typename T>
struct copy_ref : public std::false_type {};
template <typename T>
concept refable = !std::is_reference_v<T> && !std::is_const_v<T>;
export {
    /** @brief Immutable reference wrapper with change-detection tick metadata.
     *
     * Provides read-only access to a value along with tick information
     * to detect whether the value was added or modified since the last
     * system run. When copy_ref<T> is true, the value is copied instead
     * of referenced.
     * @tparam T The referenced value type (must not be a reference or const).
     */
    template <refable T>
    struct Ref;
    template <refable T>
        requires(!copy_ref<T>::value)
    struct Ref<T> {
       private:
        const T* value;
        Ticks ticks;

       public:
        Ref(const T* value, Ticks ticks) : value(value), ticks(ticks) {}

        /** @brief Get a const pointer to the value. */
        const T* ptr() const { return value; }
        /** @brief Get a const reference to the value. */
        const T& get() const { return *value; }
        /** @brief Dereference to const pointer. */
        const T* operator->() const { return value; }
        /** @brief Dereference to const reference. */
        const T& operator*() const { return *value; }
        operator const T&() const { return *value; }
        /** @brief Check whether the value was added since the last system run. */
        bool is_added() const { return ticks.is_added(); }
        /** @brief Check whether the value was modified since the last system run. */
        bool is_modified() const { return ticks.is_modified(); }
        /** @brief Get the tick when the value was last modified. */
        Tick last_modified() const { return ticks.last_modified(); }
        /** @brief Get the tick when the value was added. */
        Tick added_tick() const { return ticks.added_tick(); }
    };
    template <refable T>
        requires(copy_ref<T>::value && std::copy_constructible<T>)
    struct Ref<T> {
       private:
        T value;
        Ticks ticks;

       public:
        Ref(const T* value, Ticks ticks) : value(*value), ticks(ticks) {}

        /** @brief Get a const pointer to the copied value. */
        const T* ptr() const { return std::addressof(value); }
        /** @brief Get a mutable pointer to the copied value. */
        T* ptr_mut() { return std::addressof(value); }
        /** @brief Get a const reference to the copied value. */
        const T& get() const { return value; }
        /** @brief Get a mutable reference to the copied value. */
        T& get_mut() { return value; }
        /** @brief Dereference to const pointer. */
        const T* operator->() const { return std::addressof(value); }
        /** @brief Dereference to mutable pointer. */
        T* operator->() { return std::addressof(value); }
        /** @brief Dereference to const reference. */
        const T& operator*() const { return value; }
        /** @brief Dereference to mutable reference. */
        T& operator*() { return value; }
        operator const T&() const { return value; }
        operator T&() { return value; }
        /** @brief Check whether the value was added since the last system run. */
        bool is_added() const { return ticks.is_added(); }
        /** @brief Check whether the value was modified since the last system run. */
        bool is_modified() const { return ticks.is_modified(); }
        /** @brief Get the tick when the value was last modified. */
        Tick last_modified() const { return ticks.last_modified(); }
        /** @brief Get the tick when the value was added. */
        Tick added_tick() const { return ticks.added_tick(); }
    };
    /** @brief Mutable reference wrapper with change-detection tick metadata.
     *
     * Provides read-write access to a value. Mutating access (get_mut, operator->,
     * operator*) automatically marks the value as modified.
     * @tparam T The referenced value type (must not be a reference or const).
     */
    template <refable T>
    struct Mut {
       private:
        T* value;
        TicksMut ticks;

       public:
        Mut(T* value, TicksMut ticks) : value(value), ticks(ticks) {}

        /** @brief Get a const pointer to the value without marking as modified. */
        const T* ptr() const { return value; }
        /** @brief Get a mutable pointer, marking the value as modified. */
        T* ptr_mut() {
            ticks.set_modified();
            return value;
        }
        /** @brief Get a const reference to the value without marking as modified. */
        const T& get() const { return *value; }
        /** @brief Get a mutable reference, marking the value as modified. */
        T& get_mut() {
            ticks.set_modified();
            return *value;
        }
        /** @brief Dereference to const pointer. */
        const T* operator->() const { return value; }
        /** @brief Dereference to mutable pointer, marking the value as modified. */
        T* operator->() {
            ticks.set_modified();
            return value;
        }
        /** @brief Dereference to const reference. */
        const T& operator*() const { return *value; }
        /** @brief Dereference to mutable reference, marking the value as modified. */
        T& operator*() {
            ticks.set_modified();
            return *value;
        }
        /** @brief Implicit conversion to mutable reference, marking as modified. */
        operator T&() {
            ticks.set_modified();
            return *value;
        }
        operator const T&() const { return *value; }
        /** @brief Check whether the value was added since the last system run. */
        bool is_added() const { return ticks.is_added(); }
        /** @brief Check whether the value was modified since the last system run. */
        bool is_modified() const { return ticks.is_modified(); }
        /** @brief Get the tick when the value was last modified. */
        Tick last_modified() const { return ticks.last_modified(); }
        /** @brief Get the tick when the value was added. */
        Tick added_tick() const { return ticks.added_tick(); }
    };

    /** @brief Immutable resource reference, extending Ref<T> for use with resources.
     * @tparam T The resource type.
     */
    template <refable T>
    struct Res : public Ref<T> {
       public:
        using Ref<T>::Ref;
    };
    /** @brief Mutable resource reference, extending Mut<T> for use with resources.
     *
     * Mutating access automatically marks the resource as modified.
     * @tparam T The resource type.
     */
    template <refable T>
    struct ResMut : public Mut<T> {
       public:
        using Mut<T>::Mut;
    };
}
}  // namespace core