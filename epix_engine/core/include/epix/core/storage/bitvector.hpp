#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

// bit_vector: runtime-sized dynamic bitset used for ECS masks and fast
// conflict detection. Implemented as a vector of 64-bit words.

namespace epix::core::storage {

// A runtime-sized bitset similar to std::bitset but with dynamic size.
// Stores bits packed into 64-bit words and supports in-place and, or, xor
// and common queries (test, set, count, any, none). Designed for use in
// the ECS backend where component-access masks are compared at runtime.
class bit_vector {
   public:
    // --- types ----------------------------------------------------------------
    using size_type                      = std::size_t;
    using word_type                      = std::uint64_t;
    static constexpr size_type word_bits = sizeof(word_type) * 8;
    static constexpr size_type npos      = static_cast<size_type>(-1);

    // --- construction ---------------------------------------------------------
    bit_vector() noexcept                        = default;
    bit_vector(const bit_vector&)                = default;
    bit_vector(bit_vector&&) noexcept            = default;
    bit_vector& operator=(const bit_vector&)     = default;
    bit_vector& operator=(bit_vector&&) noexcept = default;

    explicit bit_vector(size_type bits, bool value = false)
        : bits_(bits), data_(words_for(bits), value ? ~word_type(0) : word_type(0)) {
        if (value) trim_tail();
    }

    // --- capacity ------------------------------------------------------------
    size_type size() const noexcept { return bits_; }
    bool empty() const noexcept { return bits_ == 0; }

    // --- modifiers -----------------------------------------------------------
    // Resize preserves existing bits; newly created bits are set to 'value'.
    void resize(size_type bits, bool value = false) {
        const size_type old_bits = bits_;
        if (bits == old_bits) return;

        const size_type old_words = words_for(old_bits);
        const size_type new_words = words_for(bits);

        if (new_words > old_words) {
            data_.resize(new_words, value ? ~word_type(0) : word_type(0));
        } else if (new_words < old_words) {
            data_.resize(new_words);
        }

        if (value && bits > old_bits) {
            // set bits from old_bits .. bits-1 in the first affected partial word
            if (old_bits / word_bits < data_.size()) {
                const size_type start_word = old_bits / word_bits;
                const size_type start_off  = old_bits % word_bits;
                if (start_off != 0) {
                    const word_type mask = (~word_type(0)) << start_off;
                    data_[start_word] |= mask;
                }
            }
        }

        bits_ = bits;
        trim_tail();
    }

    void reset() noexcept { std::fill(data_.begin(), data_.end(), word_type(0)); }
    void set_all() noexcept {
        std::fill(data_.begin(), data_.end(), ~word_type(0));
        trim_tail();
    }

    // --- element access ------------------------------------------------------
    bool test(size_type pos) const noexcept {
        assert(pos < bits_);
        return (data_[pos / word_bits] >> (pos % word_bits)) & word_type(1);
    }

    bool operator[](size_type pos) const noexcept { return test(pos); }

    // --- single-bit modifiers -----------------------------------------------
    void set(size_type pos, bool value = true) noexcept {
        assert(pos < bits_);
        const size_type w = pos / word_bits;
        const word_type m = word_type(1) << (pos % word_bits);
        if (value)
            data_[w] |= m;
        else
            data_[w] &= ~m;
    }

    void reset(size_type pos) noexcept { set(pos, false); }
    void flip(size_type pos) noexcept {
        assert(pos < bits_);
        data_[pos / word_bits] ^= (word_type(1) << (pos % word_bits));
    }

    // Flip all bits
    void flip() noexcept {
        for (auto& w : data_) w = ~w;
        trim_tail();
    }

    friend bit_vector operator~(bit_vector a) noexcept {
        a.flip();
        return a;
    }

    // --- queries ------------------------------------------------------------
    size_type count() const noexcept {
        size_type c = 0;
        for (auto w : data_) c += static_cast<size_type>(std::popcount(w));
        return c;
    }

    bool any() const noexcept {
        for (auto w : data_)
            if (w) return true;
        return false;
    }
    bool none() const noexcept { return !any(); }

    // --- bitwise operators (in-place) ---------------------------------------
    bit_vector& operator&=(const bit_vector& o) noexcept {
        assert(bits_ == o.bits_);
        for (size_type i = 0; i < data_.size(); ++i) data_[i] &= o.data_[i];
        return *this;
    }

    bit_vector& operator|=(const bit_vector& o) noexcept {
        assert(bits_ == o.bits_);
        for (size_type i = 0; i < data_.size(); ++i) data_[i] |= o.data_[i];
        return *this;
    }

    bit_vector& operator^=(const bit_vector& o) noexcept {
        assert(bits_ == o.bits_);
        for (size_type i = 0; i < data_.size(); ++i) data_[i] ^= o.data_[i];
        trim_tail();
        return *this;
    }

    friend bit_vector operator&(bit_vector a, const bit_vector& b) {
        a &= b;
        return a;
    }
    friend bit_vector operator|(bit_vector a, const bit_vector& b) {
        a |= b;
        return a;
    }
    friend bit_vector operator^(bit_vector a, const bit_vector& b) {
        a ^= b;
        return a;
    }

    bool operator==(const bit_vector& o) const noexcept { return bits_ == o.bits_ && data_ == o.data_; }
    bool operator!=(const bit_vector& o) const noexcept { return !(*this == o); }

    // --- set-like operations & fast checks ----------------------------------
    // Fast intersection test: return true if this and o share any set bit.
    // Only the overlapping range is checked (works for different sizes).
    bool intersects(const bit_vector& o) const noexcept {
        const size_type min_words = std::min(data_.size(), o.data_.size());
        for (size_type i = 0; i < min_words; ++i)
            if ((data_[i] & o.data_[i]) != 0) return true;
        return false;
    }

    // Find first bit position where both have a 1. Returns npos if none.
    size_type find_first_intersection(const bit_vector& o) const noexcept {
        const size_type min_bits  = std::min(bits_, o.bits_);
        const size_type min_words = words_for(min_bits);
        for (size_type wi = 0; wi < min_words; ++wi) {
            const word_type w = data_[wi] & o.data_[wi];
            if (w != 0) {
                const unsigned int tz = std::countr_zero(w);
                const size_type pos   = wi * word_bits + tz;
                return pos < min_bits ? pos : npos;
            }
        }
        return npos;
    }

    // Check if all set bits of this are contained in 'o' (this subset of o)
    bool is_subset_of(const bit_vector& o) const noexcept {
        const size_type max_words = std::max(data_.size(), o.data_.size());
        for (size_type i = 0; i < max_words; ++i) {
            const word_type a = (i < data_.size()) ? data_[i] : word_type(0);
            const word_type b = (i < o.data_.size()) ? o.data_[i] : word_type(0);
            if ((a & ~b) != 0) return false;
        }
        return true;
    }

    bool is_disjoint(const bit_vector& o) const noexcept { return !intersects(o); }

    // --- finders ------------------------------------------------------------
    // Find first set bit in this bit_vector. Returns npos if none.
    size_type find_first() const noexcept {
        for (size_type wi = 0; wi < data_.size(); ++wi) {
            const word_type w = data_[wi];
            if (w != 0) {
                const unsigned int tz = std::countr_zero(w);
                const size_type pos   = wi * word_bits + tz;
                return pos < bits_ ? pos : npos;
            }
        }
        return npos;
    }

    // Find next set bit after 'pos', return npos if none
    size_type find_next(size_type pos) const noexcept {
        if (pos >= bits_) return npos;
        ++pos;
        if (pos >= bits_) return npos;
        size_type wi           = pos / word_bits;
        const unsigned int off = pos % word_bits;
        if (off != 0) {
            word_type w = data_[wi] & (~word_type(0) << off);
            if (w != 0) {
                const unsigned int tz = std::countr_zero(w);
                const size_type p     = wi * word_bits + tz;
                return p < bits_ ? p : npos;
            }
            ++wi;
        }
        for (; wi < data_.size(); ++wi) {
            const word_type w = data_[wi];
            if (w != 0) {
                const unsigned int tz = std::countr_zero(w);
                const size_type p     = wi * word_bits + tz;
                return p < bits_ ? p : npos;
            }
        }
        return npos;
    }

    // --- utilities ----------------------------------------------------------
    const std::vector<word_type>& words() const noexcept { return data_; }
    std::vector<word_type>& words() noexcept { return data_; }

    void swap(bit_vector& o) noexcept {
        using std::swap;
        swap(bits_, o.bits_);
        swap(data_, o.data_);
    }

   private:
    size_type bits_ = 0;
    std::vector<word_type> data_;

    static constexpr size_type words_for(size_type bits) noexcept {
        return bits == 0 ? 0 : ((bits + word_bits - 1) / word_bits);
    }

    // Clear bits past 'bits_' in the last word
    void trim_tail() noexcept {
        if (bits_ == 0) {
            data_.clear();
            return;
        }
        const size_type rem = bits_ % word_bits;
        if (rem == 0) return;
        const word_type mask = (word_type(1) << rem) - word_type(1);
        if (!data_.empty()) data_.back() &= mask;
    }
};

}  // namespace epix::core::storage