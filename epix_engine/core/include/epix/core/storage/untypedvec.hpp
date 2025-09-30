#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>
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

    // index access as raw pointer. Caller must cast to appropriate type.
    void* at(size_t idx) {
        if (idx >= size_) throw std::out_of_range("index out of range");
        return static_cast<char*>(data_) + idx * desc_->size;
    }

    const void* at(size_t idx) const {
        if (idx >= size_) throw std::out_of_range("index out of range");
        return static_cast<const char*>(data_) + idx * desc_->size;
    }

    // Templated helpers when caller knows the static type T
    template <typename T>
    void push_back_copy(const T& v) {
        ensure_type_matches<T>();
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
        ensure_type_matches<T>();
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
        ensure_type_matches<T>();
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        // direct placement-new since we know T here
        new (dest) T(std::forward<Args>(args)...);
        ++size_;
    }

    template <typename T>
    T* data_as() noexcept {
        ensure_type_matches<T>();
        return reinterpret_cast<T*>(data_);
    }

    template <typename T>
    const T* data_as() const noexcept {
        ensure_type_matches<T>();
        return reinterpret_cast<const T*>(data_);
    }

    template <typename T>
    T& operator[](size_t idx) {
        ensure_type_matches<T>();
        return *reinterpret_cast<T*>(at(idx));
    }

    template <typename T>
    const T& operator[](size_t idx) const {
        ensure_type_matches<T>();
        return *reinterpret_cast<const T*>(at(idx));
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
        if (idx >= size_) throw std::out_of_range("index out of range");
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

   private:
    const epix::core::type_system::TypeInfo* desc_;
    size_t size_;
    size_t capacity_;
    void* data_;

    void ensure_capacity_for_one() {
        if (size_ >= capacity_) {
            size_t new_cap = capacity_ ? capacity_ * 2 : 1;
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
        if (!desc_) throw std::logic_error("descriptor is null");
        // If the TypeInfo name is present, use it to verify identity across translation units.
        if (!desc_->name.empty() && desc_->name != epix::core::meta::type_id<T>().name()) {
            throw std::logic_error("type mismatch between descriptor and requested T");
        }
        if (desc_->size != sizeof(T) || desc_->align != alignof(T)) {
            throw std::logic_error("type size or alignment mismatch");
        }
    }
};

}  // namespace epix::core::storage