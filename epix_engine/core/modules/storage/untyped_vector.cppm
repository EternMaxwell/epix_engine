module;

#include <cassert>

export module epix.core:storage.untyped_vector;

import std;
import epix.meta;

namespace epix::core {
/** @brief Type-erased dynamic array that stores elements by their runtime type_info.
 *
 * Manages memory for elements of any type, using meta::type_info for
 * construction, destruction, move, and copy operations. Used internally
 * as the underlying storage for Dense columns, resources, and sparse sets.
 */
export class untyped_vector {
   public:
    /** @brief Construct with the given type descriptor and optional initial capacity. */
    explicit untyped_vector(const ::epix::meta::type_info& desc, std::size_t reserve_cnt = 0)
        : desc_(std::addressof(desc)), size_(0), capacity_(0), data_(nullptr) {
        if (!desc_ || desc_->size == 0) throw std::invalid_argument("element size must be > 0");
        if (reserve_cnt) reserve(reserve_cnt);
    }

    /** @brief Destructor; destroys all elements and frees memory. */
    ~untyped_vector() {
        clear();
        deallocate(data_);
    }

    [[deprecated("untyped_vector copy is not recommended; use clone() instead")]]
    untyped_vector(const untyped_vector& other)
        : desc_(other.desc_), mem_res_(other.mem_res_), size_(0), capacity_(0), data_(nullptr) {
        reserve(other.size_);
        if (other.size_ > 0) {
            // copy-construct elements
            if (desc_->trivially_copyable) {
                std::memcpy(data_, other.data_, other.size_ * desc_->size);
            } else {
                if (!desc_->copy_constructible) {
                    throw std::runtime_error("Type is not copy-constructible");
                }
                for (std::size_t i = 0; i < other.size_; ++i) {
                    desc_->copy_construct(static_cast<char*>(data_) + i * desc_->size,
                                          static_cast<const char*>(other.data_) + i * desc_->size);
                }
            }
            size_ = other.size_;
        }
    }

    [[deprecated("untyped_vector copy is not recommended; use clone() instead")]]
    untyped_vector& operator=(const untyped_vector& other) {
        if (this == &other) return *this;
        clear();
        if (type_info() != other.type_info()) {
            deallocate(data_);
            desc_     = other.desc_;
            data_     = nullptr;
            capacity_ = 0;
        }
        if (other.size_ > 0) {
            reserve(other.size_);
            // copy-construct elements
            if (desc_->trivially_copyable) {
                std::memcpy(data_, other.data_, other.size_ * desc_->size);
            } else {
                if (!desc_->copy_constructible) {
                    throw std::runtime_error("Type is not copy-constructible");
                }
                for (std::size_t i = 0; i < other.size_; ++i) {
                    desc_->copy_construct(static_cast<char*>(data_) + i * desc_->size,
                                          static_cast<const char*>(other.data_) + i * desc_->size);
                }
            }
            size_ = other.size_;
        }
        return *this;
    }

    /** @brief Move constructor; takes ownership of other's memory. */
    untyped_vector(untyped_vector&& other) noexcept
        : desc_(other.desc_),
          size_(other.size_),
          capacity_(other.capacity_),
          data_(other.data_),
          mem_res_(other.mem_res_) {
        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;
    }

    /** @brief Move assignment operator. */
    untyped_vector& operator=(untyped_vector&& other) noexcept {
        if (this != &other) {
            clear();
            deallocate(data_);
            desc_           = other.desc_;
            mem_res_        = other.mem_res_;
            data_           = other.data_;
            size_           = other.size_;
            capacity_       = other.capacity_;
            other.data_     = nullptr;
            other.size_     = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    /** @brief Create a deep copy of this vector. */
    untyped_vector clone() const {
        untyped_vector copy(*desc_, size_);
        if (desc_->trivially_copyable) {
            std::memcpy(copy.data_, data_, size_ * desc_->size);
        } else {
            if (!desc_->copy_constructible) {
                throw std::runtime_error("Type is not copy-constructible");
            }
            for (std::size_t i = 0; i < size_; ++i) {
                desc_->copy_construct(static_cast<char*>(copy.data_) + i * desc_->size,
                                      static_cast<const char*>(data_) + i * desc_->size);
            }
        }
        copy.size_ = size_;
        return std::move(copy);
    }

    /** @brief Get the number of elements. */
    std::size_t size() const noexcept { return size_; }
    /** @brief Get the maximum possible number of elements. */
    std::size_t max_size() const noexcept { return std::numeric_limits<std::size_t>::max() / desc_->size; }
    /** @brief Get the current capacity. */
    std::size_t capacity() const noexcept { return capacity_; }
    /** @brief Check if the vector is empty. */
    bool empty() const noexcept { return size_ == 0; }

    /** @brief Get the type_info descriptor. */
    const ::epix::meta::type_info& type_info() const noexcept { return *desc_; }

    /** @brief Get a raw pointer to the element data. */
    void* data() noexcept { return data_; }
    /** @brief Get a const raw pointer to the element data. */
    const void* data() const noexcept { return data_; }
    /** @brief Get a const raw pointer (explicit const alias). */
    const void* cdata() const noexcept { return data(); }

    /** @brief Get a raw pointer to the element at the given index. */
    void* get(std::size_t idx) {
        assert(idx < size_);
        return static_cast<char*>(data_) + idx * desc_->size;
    }

    /** @brief Get a const raw pointer to the element at the given index. */
    const void* get(std::size_t idx) const {
        assert(idx < size_);
        return static_cast<const char*>(data_) + idx * desc_->size;
    }

    /** @brief Const raw-pointer getter alias. */
    const void* cget(std::size_t idx) const noexcept { return get(idx); }

    /** @brief Append a typed value by forwarding.
     *  @tparam T Element type. */
    template <typename T>
    void push_back(T&& v) {
        using type = std::decay_t<T>;
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        new (dest) type(std::forward<T>(v));
        ++size_;
    }

    /** @brief Construct a typed element in-place at the end.
     *  @tparam T Element type.
     *  @tparam Args Constructor argument types. */
    template <typename T, typename... Args>
    void emplace_back(Args&&... args) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        // direct placement-new since we know T here
        new (dest) T(std::forward<Args>(args)...);
        ++size_;
    }

    template <typename T>
    void emplace_back(T&& value) {
        using type = std::decay_t<T>;
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        // direct placement-new since we know T here
        new (dest) type(std::forward<T>(value));
        ++size_;
    }

    /** @brief Get a typed pointer to the data.
     *  @tparam T Element type. */
    template <typename T>
    T* data_as() noexcept {
        return std::launder(reinterpret_cast<T*>(data_));
    }

    /** @brief Iterate over raw element pointers (mutable). */
    auto iter() {
        return std::views::iota(std::size_t{0}, size()) | std::views::transform([this](std::size_t i) {
                   return static_cast<void*>(static_cast<char*>(data_) + i * desc_->size);
               });
    }

    /** @brief Iterate over raw element pointers (const). */
    auto iter() const {
        return std::views::iota(std::size_t{0}, size()) | std::views::transform([this](std::size_t i) {
                   return static_cast<const void*>(static_cast<const char*>(data_) + i * desc_->size);
               });
    }
    /** @brief Const iteration alias. */
    auto citer() const { return iter(); }

    /** @brief Get a typed span over the elements.
     *  @tparam T Element type. */
    template <typename T>
    std::span<T> span_as() noexcept {
        return {std::launder(reinterpret_cast<T*>(data_)), size_};
    }

    template <typename T>
    std::span<const T> span_as() const noexcept {
        return {std::launder(reinterpret_cast<const T*>(data_)), size_};
    }
    template <typename T>
    std::span<const T> cspan_as() const noexcept {
        return span_as<T>();
    }

    /** @brief Get a const typed pointer to the data.
     *  @tparam T Element type. */
    template <typename T>
    const T* data_as() const noexcept {
        return std::launder(reinterpret_cast<const T*>(data_));
    }
    /** @brief Const typed pointer alias.
     *  @tparam T Element type. */
    template <typename T>
    const T* cdata_as() const noexcept {
        return data_as<T>();
    }

    /** @brief Get a mutable typed reference to the element at idx.
     *  @tparam T Element type. */
    template <typename T>
    T& get_as(std::size_t idx) {
        assert(idx < size_);
        return *std::launder(reinterpret_cast<T*>(get(idx)));
    }

    /** @brief Get a const typed reference to the element at idx.
     *  @tparam T Element type. */
    template <typename T>
    const T& get_as(std::size_t idx) const {
        assert(idx < size_);
        return *std::launder(reinterpret_cast<const T*>(get(idx)));
    }

    /** @brief Const typed reference alias.
     *  @tparam T Element type. */
    template <typename T>
    const T& cget_as(std::size_t idx) const {
        return get_as<T>(idx);
    }

    /** @brief Replace the element at idx with a copy of src.
     *  @tparam T Element type. */
    template <typename T>
    void replace(std::size_t idx, const T& src) {
        replace_from(idx, static_cast<const void*>(std::addressof(src)));
    }

    /** @brief Replace the element at idx by moving from src.
     *  @tparam T Element type. */
    template <typename T>
    void replace_move(std::size_t idx, T&& src) {
        replace_from_move(idx, static_cast<void*>(std::addressof(src)));
    }

    /** @brief Replace the element at idx by constructing in-place.
     *  @tparam T Element type.
     *  @tparam Args Constructor argument types. */
    template <typename T, typename... Args>
    void replace_emplace(std::size_t idx, Args&&... args) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        desc_->destruct(dst);
        // placement-new the new object
        new (dst) T(std::forward<Args>(args)...);
    }

    /** @brief Append an element by copying from a raw pointer. */
    void push_back_from(const void* src) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        desc_->copy_construct(dest, src);
        ++size_;
    }

    /** @brief Append an element by moving from a raw pointer. */
    void push_back_from_move(void* src) {
        ensure_capacity_for_one();
        void* dest = static_cast<char*>(data_) + size_ * desc_->size;
        desc_->move_construct(dest, src);
        ++size_;
    }

    /** @brief Replace the element at idx by copying from a raw pointer. */
    void replace_from(std::size_t idx, const void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        desc_->destruct(dst);
        desc_->copy_construct(dst, src);
    }

    /** @brief Replace the element at idx by moving from a raw pointer. */
    void replace_from_move(std::size_t idx, void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        desc_->destruct(dst);
        desc_->move_construct(dst, src);
    }

    /** @brief Remove the last element. */
    void pop_back() {
        if (size_ == 0) return;
        --size_;
        void* ptr = static_cast<char*>(data_) + size_ * desc_->size;
        desc_->destruct(ptr);
    }

    /** @brief Destroy all elements without releasing memory. */
    void clear() noexcept {
        // destroy elements in reverse order
        if (!desc_->trivially_destructible) {
            for (std::size_t i = size_; i > 0; --i) {
                void* p = static_cast<char*>(data_) + (i - 1) * desc_->size;
                desc_->destruct(p);
            }
        }
        size_ = 0;
    }

    /** @brief Remove element at idx by swapping the last element into its place.
     *  O(1) complexity. */
    void swap_remove(std::size_t idx) {
        assert(idx < size_);
        if (size_ == 0) return;
        std::size_t last_idx = size_ - 1;
        if (idx == last_idx) {
            pop_back();
            return;
        }

        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        void* src = static_cast<char*>(data_) + last_idx * desc_->size;

        // If destination holds a non-trivial object, destroy it first before overwrite.
        desc_->destruct(dst);

        if (desc_->trivially_copyable) {
            std::memcpy(dst, src, desc_->size);
        } else {
            // move-construct into dst from src
            desc_->move_construct(dst, src);
        }

        // destroy the source (last element)
        desc_->destruct(src);

        --size_;
    }

    void reserve(std::size_t new_cap) {
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

    /** @brief Resize to new_size without initializing new elements.
     *  Destructs tail elements if shrinking. */
    void resize_uninitialized(std::size_t new_size) {
        if (new_size == size_) return;
        if (new_size < size_) {
            // destroy constructed elements in tail
            if (!desc_->trivially_destructible) {
                for (std::size_t i = new_size; i < size_; ++i) {
                    void* p = static_cast<char*>(data_) + i * desc_->size;
                    desc_->destruct(p);
                }
            }
            size_ = new_size;
            return;
        }

        // expand
        if (new_size > capacity_) ensure_capacity_for(new_size - size_);
        // note: intentionally do not construct new elements (unsafe)
        size_ = new_size;
    }
    /** @brief Append count uninitialized slots. */
    void append_uninitialized(std::size_t count) {
        if (count == 0) return;
        std::size_t new_size = size_ + count;
        resize_uninitialized(new_size);
    }

    /** @brief Copy-construct into a previously-uninitialized slot from a raw pointer. */
    void initialize_from(std::size_t idx, const void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        desc_->copy_construct(dst, src);
    }

    /** @brief Move-construct into a previously-uninitialized slot from a raw pointer. */
    void initialize_from_move(std::size_t idx, void* src) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        desc_->move_construct(dst, src);
    }

    /** @brief Construct in-place into a previously-uninitialized slot.
     *  @tparam T Element type.
     *  @tparam Args Constructor argument types. */
    template <typename T, typename... Args>
    void initialize_emplace(std::size_t idx, Args&&... args) {
        assert(idx < size_);
        void* dst = static_cast<char*>(data_) + idx * desc_->size;
        new (dst) T(std::forward<Args>(args)...);
    }

   private:
    const ::epix::meta::type_info* desc_;
    std::pmr::memory_resource* mem_res_ = std::pmr::get_default_resource();
    std::size_t size_;
    std::size_t capacity_;
    void* data_;

    void ensure_capacity_for_one() {
        if (size_ >= capacity_) {
            // growth factor: ~1.5x (use integer math, ensure progress for small caps)
            std::size_t new_cap = capacity_ ? ((capacity_ * 3 + 1) / 2) : 1;
            reallocate(new_cap);
        }
    }
    void ensure_capacity_for(std::size_t n) {
        if (n == 0) return;
        if (size_ + n > capacity_) {
            std::size_t new_cap = capacity_;
            while (new_cap < size_ + n) {
                new_cap = new_cap ? ((new_cap * 3 + 1) / 2) : 1;
            }
            reallocate(new_cap);
        }
    }

    // allocate raw bytes with proper alignment
    void* allocate(std::size_t bytes) {
        if (bytes == 0) return nullptr;
        return mem_res_->allocate(bytes, desc_->align);
    }
    void deallocate(void* p) noexcept {
        if (!p) return;
        mem_res_->deallocate(p, capacity_ * desc_->size, desc_->align);
    }

    void reallocate(std::size_t new_cap) {
        assert(new_cap > 0);
        std::size_t esz    = desc_->size;
        std::size_t needed = new_cap * esz;
        // allocate new buffer
        void* new_data = nullptr;
        try {
            new_data = allocate(needed);
        } catch (...) {
            throw;  // propagate
        }

        // If elements are trivially copyable, use a fast memcpy path.
        if (desc_->trivially_copyable) {
            std::memcpy(new_data, data_, size_ * esz);
        } else {
            // move-construct existing elements into new storage
            std::size_t i = 0;
            for (; i < size_; ++i) {
                void* src  = static_cast<char*>(data_) + i * esz;
                void* dest = static_cast<char*>(new_data) + i * esz;
                // prefer move
                desc_->move_construct(dest, src);
            }
        }

        // destroy old elements and free old storage
        if (!desc_->trivially_destructible) {
            for (std::size_t j = 0; j < size_; ++j) {
                void* p = static_cast<char*>(data_) + j * esz;
                desc_->destruct(p);
            }
        }
        if (data_) deallocate(data_);

        data_     = new_data;
        capacity_ = new_cap;
    }
};
}  // namespace core