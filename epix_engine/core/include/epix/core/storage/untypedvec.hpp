#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

#include "../type_system/type_registry.hpp"

namespace epix::core::storage {

// Use TypeInfo from the engine's type system as the runtime descriptor for elements.
// The TypeInfo contains function pointers and cached traits similar to the old
// TypeDescriptor but is provided by the centralized type registry.

// Untyped vector: stores elements of a uniform runtime-described type.
// - The user creates an instance with a TypeDescriptor (use make_type_descriptor<T>())
// - When the caller still knows the compile-time type T, templated helpers are
//   provided to push/emplace/pop safely (they check runtime type info if available).
// - When the caller does not know T, the non-templated API accepts raw pointers
//   to source objects and uses the descriptor's function pointers to manipulate them.
class untyped_vector {
   public:
    explicit untyped_vector(const epix::core::type_system::TypeInfo* desc, size_t reserve_cnt = 0)
        : desc_(desc), size_(0), capacity_(0), data_(nullptr) {
        if (!desc_ || desc_->size == 0) throw std::invalid_argument("element size must be > 0");
        if (reserve_cnt) reserve(reserve_cnt);
    }

    ~untyped_vector() {
        clear();
        deallocate(data_);
    }

    untyped_vector(const untyped_vector&)            = delete;
    untyped_vector& operator=(const untyped_vector&) = delete;

    untyped_vector(untyped_vector&& other) noexcept
        : desc_(other.desc_), size_(other.size_), capacity_(other.capacity_), data_(other.data_) {
        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;
    }

    untyped_vector& operator=(untyped_vector&& other) noexcept {
        if (this != &other) {
            clear();
            deallocate(data_);
            desc_           = other.desc_;
            data_           = other.data_;
            size_           = other.size_;
            capacity_       = other.capacity_;
            other.data_     = nullptr;
            other.size_     = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    // size/capacity
    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return size_ == 0; }

    const epix::core::type_system::TypeInfo* descriptor() const noexcept { return desc_; }

    // raw pointer access (void*)
    void* data() noexcept { return data_; }
    const void* data() const noexcept { return data_; }
    const void* cdata() const noexcept { return data(); }

    // index access as raw pointer. Caller must cast to appropriate type.
    void* get(size_t idx) {
        assert(idx < size_);
        return static_cast<char*>(data_) + idx * desc_->size;
    }

    const void* get(size_t idx) const {
        assert(idx < size_);
        return static_cast<const char*>(data_) + idx * desc_->size;
    }

    // explicit const getter alias: non-templated raw-pointer const getter
    const void* cget(size_t idx) const noexcept { return get(idx); }

    // Templated helpers when caller knows the static type T
    template <typename T>
    void push_back_copy(const T& v) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dest, std::addressof(v), desc_->size);
        } else {
            desc_->copy_construct(dest, &v);
        }
        ++size_;
    }

    template <typename T>
    void push_back_move(T&& v) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        // move from the provided object
        if (desc_->trivially_copyable) {
            std::memcpy(dest, std::addressof(v), desc_->size);
        } else {
            desc_->move_construct(dest, static_cast<void*>(std::addressof(v)));
        }
        ++size_;
    }

    template <typename T, typename... Args>
    void emplace_back(Args&&... args) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        // direct placement-new since we know T here
        new (dest) T(std::forward<Args>(args)...);
        ++size_;
    }

    template <typename T>
    T* data_as() noexcept {
        return reinterpret_cast<T*>(data_);
    }

    // Ranges-based iterator views over raw element pointers.
    // Non-const: yields void* to each element.
    auto iter() {
        using namespace std::views;
        return iota(size_t{0}, size()) |
               transform([this](size_t i) { return static_cast<void*>(static_cast<char*>(data_) + i * desc_->size); });
    }

    // Const: yields const void* to each element.
    auto iter() const {
        using namespace std::views;
        return iota(size_t{0}, size()) | transform([this](size_t i) {
                   return static_cast<const void*>(static_cast<const char*>(data_) + i * desc_->size);
               });
    }
    auto citer() const { return iter(); }

    // Typed span accessors when caller knows T at compile time.
    template <typename T>
    std::span<T> span_as() noexcept {
        return {reinterpret_cast<T*>(data_), size_};
    }

    template <typename T>
    std::span<const T> span_as() const noexcept {
        return {reinterpret_cast<const T*>(data_), size_};
    }
    template <typename T>
    std::span<const T> cspan_as() const noexcept {
        return span_as<T>();
    }

    template <typename T>
    const T* data_as() const noexcept {
        return reinterpret_cast<const T*>(data_);
    }
    template <typename T>
    const T* cdata_as() const noexcept {
        return data_as<T>();
    }

    // Typed element accessors -------------------------------------------------
    // Returns a reference to the element at `idx` interpreted as `T`.
    template <typename T>
    T& get_as(size_t idx) {
        assert(idx < size_);
        return *reinterpret_cast<T*>(get(idx));
    }

    template <typename T>
    const T& get_as(size_t idx) const {
        assert(idx < size_);
        return *reinterpret_cast<const T*>(get(idx));
    }

    // typed const getter
    template <typename T>
    const T& cget_as(size_t idx) const {
        return get_as<T>(idx);
    }

    // Replace element at index with a copy of the provided value (templated).
    // Basic exception guarantee: if construction throws the element may be in an
    // unspecified but valid state (consistent with other operations here).
    template <typename T>
    void replace(size_t idx, const T& src) {
        replace_from(idx, static_cast<const void*>(std::addressof(src)));
    }

    template <typename T>
    void replace_move(size_t idx, T&& src) {
        replace_from_move(idx, static_cast<void*>(std::addressof(src)));
    }

    // Replace element at index by constructing T in-place with provided args.
    // Basic exception guarantee: if construction throws, the element may be
    // in an unspecified but valid state (we destroy before constructing).
    template <typename T, typename... Args>
    void replace_emplace(size_t idx, Args&&... args) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        if (!desc_->trivially_destructible) {
            desc_->destroy(dst);
        }
        // placement-new the new object
        new (dst) T(std::forward<Args>(args)...);
    }

    // Non-templated push from raw pointer (caller supplies source address)
    void push_back_from(const void* src) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dest, src, desc_->size);
        } else {
            desc_->copy_construct(dest, src);
        }
        ++size_;
    }

    void push_back_from_move(void* src) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dest, src, desc_->size);
        } else {
            desc_->move_construct(dest, src);
        }
        ++size_;
    }

    // Replace the element at index with a copy from raw pointer `src`.
    void replace_from(size_t idx, const void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dst, src, desc_->size);
        } else {
            // destroy existing object first (basic exception guarantee)
            if (!desc_->trivially_destructible) {
                desc_->destroy(dst);
            }
            // copy-construct new object into dst
            desc_->copy_construct(dst, src);
        }
    }

    // Replace the element at index by moving from raw pointer `src`.
    void replace_from_move(size_t idx, void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dst, src, desc_->size);
        } else if (desc_->noexcept_move_constructible) {
            // destroy existing then move-construct
            if (!desc_->trivially_destructible) {
                desc_->destroy(dst);
            }
            desc_->move_construct(dst, src);
        } else {
            // fallback to copy-construct (may throw)
            if (!desc_->trivially_destructible) {
                desc_->destroy(dst);
            }
            desc_->copy_construct(dst, src);
        }
    }

    void pop_back() {
        if (size_ == 0) return;
        --size_;
        void* ptr = static_cast<char*>(data_) + size_ * desc_->size;
        if (!desc_->trivially_destructible) {
            desc_->destroy(ptr);
        }
    }

    void clear() noexcept {
        // destroy elements in reverse order
        if (!desc_->trivially_destructible) {
            for (size_t i = size_; i > 0; --i) {
                void* p = static_cast<char*>(data_) + (i - 1) * desc_->size;
                desc_->destroy(p);
            }
        }
        size_ = 0;
    }

    // Remove element at index by swapping the last element into its place.
    // Complexity: O(1).
    // Exception safety: if the type is trivially copyable or noexcept-move-constructible,
    // this operation is noexcept. Otherwise it provides the basic exception guarantee
    // (an exception may leave the vector in a valid but unspecified state).
    void swap_remove(size_t idx) {
        assert(idx < size_);
        if (size_ == 0) return;
        size_t last_idx = size_ - 1;
        if (idx == last_idx) {
            pop_back();
            return;
        }

        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        void* src = static_cast<char*>(data_) + last_idx * desc_->size;

        // If destination holds a non-trivial object, destroy it first before overwrite.
        if (!desc_->trivially_destructible) {
            desc_->destroy(dst);
        }

        if (desc_->trivially_copyable) {
            std::memcpy(dst, src, desc_->size);
        } else if (desc_->noexcept_move_constructible) {
            // move-construct into dst from src
            desc_->move_construct(dst, src);
        } else {
            // fallback: copy-construct into dst (may throw)
            desc_->copy_construct(dst, src);
        }

        // destroy the source (last element) if needed
        if (!desc_->trivially_destructible) {
            desc_->destroy(src);
        }

        --size_;
    }

    void reserve(size_t new_cap) {
        if (new_cap <= capacity_) return;
        reallocate(new_cap);
    }

    void shrink_to_fit() {
        if (capacity_ == size_) return;
        if (size_ == 0) {
            deallocate(data_);
            data_     = nullptr;
            capacity_ = 0;
            return;
        }
        reallocate(size_);
    }

    // Mark the vector to have uninitialized elements up to new_size.
    // If new_size < size(), destructs initialized elements in the tail.
    void resize_uninitialized(size_t new_size) {
        if (new_size == size_) return;
        if (new_size < size_) {
            // destroy constructed elements in tail
            for (size_t i = new_size; i < size_; ++i) {
                void* p = static_cast<char*>(data_) + i * desc_->size;
                if (!desc_->trivially_destructible) desc_->destroy(p);
            }
            size_ = new_size;
            return;
        }

        // expand
        if (new_size > capacity_) reserve(new_size);
        // note: intentionally do not construct new elements (unsafe)
        size_ = new_size;
    }
    void append_uninitialized(size_t count) {
        if (count == 0) return;
        size_t new_size = size_ + count;
        resize_uninitialized(new_size);
    }

    // Initialize a previously-uninitialized slot from a raw pointer
    void initialize_from(size_t idx, const void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dst, src, desc_->size);
        } else {
            desc_->copy_construct(dst, src);
        }
        (void)0;
    }

    // Initialize by move from raw pointer
    void initialize_from_move(size_t idx, void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        if (desc_->trivially_copyable) {
            std::memcpy(dst, src, desc_->size);
        } else if (desc_->noexcept_move_constructible) {
            desc_->move_construct(dst, src);
        } else {
            desc_->copy_construct(dst, src);
        }
        (void)0;
    }

    // Initialize templated emplace
    template <typename T, typename... Args>
    void initialize_emplace(size_t idx, Args&&... args) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        new (dst) T(std::forward<Args>(args)...);
    }

   private:
    const epix::core::type_system::TypeInfo* desc_;
    size_t size_;
    size_t capacity_;
    void* data_;

    void ensure_capacity_for_one() {
        if (size_ >= capacity_) {
            // growth factor: ~1.5x (use integer math, ensure progress for small caps)
            size_t new_cap = capacity_ ? ((capacity_ * 3 + 1) / 2) : 1;
            reallocate(new_cap);
        }
    }

    // allocate raw bytes with proper alignment
    static void* allocate(size_t bytes, size_t align) {
        if (bytes == 0) return nullptr;
        // operator new with alignment (C++17)
        return ::operator new(bytes, static_cast<std::align_val_t>(align));
    }
    void deallocate(void* p) noexcept {
        if (!p) return;
        ::operator delete(p, static_cast<std::align_val_t>(desc_->align));
    }

    void reallocate(size_t new_cap) {
        assert(new_cap > 0);
        size_t esz    = desc_->size;
        size_t needed = new_cap * esz;
        // allocate new buffer
        void* new_data = nullptr;
        try {
            new_data = ::operator new(needed, static_cast<std::align_val_t>(desc_->align));
        } catch (...) {
            throw;  // propagate
        }

        // If elements are trivially copyable, use a fast memcpy path.
        if (desc_->trivially_copyable) {
            std::memcpy(new_data, data_, size_ * esz);
        } else {
            // move-construct existing elements into new storage
            size_t i = 0;
            try {
                for (; i < size_; ++i) {
                    void* src  = static_cast<char*>(data_) + i * esz;
                    void* dest = static_cast<char*>(new_data) + i * esz;
                    // prefer move
                    desc_->move_construct(dest, src);
                }
            } catch (...) {
                // if failed during constructing new elements, destroy constructed ones
                for (size_t j = 0; j < i; ++j) {
                    void* p = static_cast<char*>(new_data) + j * esz;
                    desc_->destroy(p);
                }
                ::operator delete(new_data, static_cast<std::align_val_t>(desc_->align));
                throw;
            }
        }

        // destroy old elements and free old storage
        if (!desc_->trivially_destructible) {
            for (size_t j = 0; j < size_; ++j) {
                void* p = static_cast<char*>(data_) + j * esz;
                desc_->destroy(p);
            }
        }
        if (data_) ::operator delete(data_, static_cast<std::align_val_t>(desc_->align));

        data_     = new_data;
        capacity_ = new_cap;
    }

    template <typename T>
    void ensure_type_matches() const noexcept(false) {
        // removed: unchecked variant should not perform runtime checks for
        // performance. Use checked_untyped_vector for safety.
    }
};

}  // namespace epix::core::storage

// Checked wrapper around untyped_vector which performs runtime type/bounds
// checking and throws exceptions on errors. Use this when callers require safety.
namespace epix::core::storage {
class checked_untyped_vector {
   public:
    // Construct from an existing untyped_vector (non-owning reference wrapper).
    explicit checked_untyped_vector(untyped_vector& vec) noexcept : vec_(&vec) {}

    // Owning constructor: create and own an untyped_vector internally.
    explicit checked_untyped_vector(const epix::core::type_system::TypeInfo* desc, size_t reserve_cnt = 0)
        : owned_(std::make_unique<untyped_vector>(desc, reserve_cnt)), vec_(owned_.get()) {}

    size_t size() const noexcept { return vec_->size(); }
    size_t capacity() const noexcept { return vec_->capacity(); }
    bool empty() const noexcept { return vec_->empty(); }

    const epix::core::type_system::TypeInfo* descriptor() const noexcept { return vec_->descriptor(); }

    void* data() noexcept { return vec_->data(); }
    const void* data() const noexcept { return vec_->data(); }
    const void* cdata() const noexcept { return data(); }

    auto iter() { return vec_->iter(); }
    auto iter() const { return vec_->iter(); }
    auto citer() const { return iter(); }

    template <typename T>
    std::span<T> span_as() noexcept {
        check_type<T>();
        return vec_->span_as<T>();
    }

    template <typename T>
    std::span<const T> span_as() const noexcept {
        check_type<T>();
        return vec_->span_as<T>();
    }
    template <typename T>
    std::span<const T> cspan_as() const noexcept {
        return span_as<T>();
    }

    void* get(size_t idx) {
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        return vec_->get(idx);
    }

    const void* get(size_t idx) const {
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        return vec_->get(idx);
    }

    // raw const pointer accessor
    const void* cget(size_t idx) const {
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        return vec_->get(idx);
    }

    template <typename T>
    void push_back_copy(const T& v) {
        check_type<T>();
        vec_->push_back_copy<T>(v);
    }

    template <typename T>
    void push_back_move(T&& v) {
        check_type<T>();
        vec_->push_back_move<T>(std::forward<T>(v));
    }

    template <typename T, typename... Args>
    void emplace_back(Args&&... args) {
        check_type<T>();
        vec_->emplace_back<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    T* data_as() noexcept {
        check_type<T>();
        return vec_->data_as<T>();
    }

    template <typename T>
    const T* data_as() const noexcept {
        check_type<T>();
        return vec_->data_as<T>();
    }
    template <typename T>
    const T* cdata_as() const noexcept {
        return data_as<T>();
    }

    // Typed access with runtime checks
    template <typename T>
    T& get_as(size_t idx) {
        check_type<T>();
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        return *reinterpret_cast<T*>(vec_->get(idx));
    }

    template <typename T>
    const T& get_as(size_t idx) const {
        check_type<T>();
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        return *reinterpret_cast<const T*>(vec_->get(idx));
    }

    // typed const getter with checks
    template <typename T>
    const T& cget_as(size_t idx) const {
        check_type<T>();
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        return *reinterpret_cast<const T*>(vec_->get(idx));
    }

    template <typename T>
    void replace(size_t idx, const T& src) {
        check_type<T>();
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        vec_->replace<T>(idx, src);
    }

    template <typename T>
    void replace_move(size_t idx, T&& src) {
        check_type<T>();
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        vec_->replace_move<T>(idx, std::forward<T>(src));
    }

    template <typename T, typename... Args>
    void replace_emplace(size_t idx, Args&&... args) {
        check_type<T>();
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        vec_->replace_emplace<T>(idx, std::forward<Args>(args)...);
    }

    void push_back_from(const void* src) { vec_->push_back_from(src); }
    void push_back_from_move(void* src) { vec_->push_back_from_move(src); }

    void replace_from(size_t idx, const void* src) {
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        vec_->replace_from(idx, src);
    }

    void replace_from_move(size_t idx, void* src) {
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        vec_->replace_from_move(idx, src);
    }

    void pop_back() { vec_->pop_back(); }
    void clear() noexcept { vec_->clear(); }
    void swap_remove(size_t idx) {
        if (idx >= vec_->size()) throw std::out_of_range("index out of range");
        vec_->swap_remove(idx);
    }

    void reserve(size_t new_cap) { vec_->reserve(new_cap); }
    void shrink_to_fit() { vec_->shrink_to_fit(); }

   private:
    untyped_vector* vec_;
    std::unique_ptr<untyped_vector> owned_;

    template <typename T>
    void check_type() const {
        const auto* d = vec_->descriptor();
        if (!d) throw std::logic_error("descriptor is null");
        if (!d->name.empty() && d->name != epix::core::meta::type_id<T>().name())
            throw std::logic_error("type mismatch between descriptor and requested T");
        if (d->size != sizeof(T) || d->align != alignof(T)) throw std::logic_error("type size or alignment mismatch");
    }
};
}  // namespace epix::core::storage