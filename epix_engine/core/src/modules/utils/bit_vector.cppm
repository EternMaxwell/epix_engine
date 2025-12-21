module;

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <vector>

export module epix.core:utils.bit_vector;

namespace core {
export class bit_vector {
   public:
    using size_type                      = size_t;
    using word_type                      = std::uint64_t;
    static constexpr size_type word_bits = sizeof(word_type) * 8;
    static constexpr size_type npos      = static_cast<size_type>(-1);

    bit_vector() noexcept = default;
    explicit bit_vector(size_type bits, bool value = false) { resize(bits, value); }

    // capacity
    size_type size() const noexcept { return bits_; }
    bool empty() const noexcept { return bits_ == 0; }

    // resize (fills new bits with value)
    void resize(size_type bits, bool value = false) {
        const size_type old_bits = bits_;
        if (bits == old_bits) return;
        const size_type old_words = words_for(old_bits);
        const size_type new_words = words_for(bits);
        words_.resize(new_words, value ? ~word_type(0) : word_type(0));
        if (value && bits > old_bits) {
            if (old_bits % word_bits != 0 && old_words > 0) {
                const size_type start_word = old_bits / word_bits;
                const size_type start_off  = old_bits % word_bits;
                const word_type mask       = (~word_type(0)) << start_off;
                if (start_word < words_.size()) words_[start_word] |= mask;
            }
        }
        bits_ = bits;
        trim_tail();
    }
    void clear() noexcept {
        bits_ = 0;
        words_.clear();
    }

    // basic queries
    bool contains(size_type pos) const noexcept { return pos < bits_ && test(pos); }
    bool contains_all_in_range(size_type start, size_type end) const noexcept {
        if (start >= end) return true;
        if (start >= bits_) return false;
        end = std::min(end, bits_);
        // check word by word
        size_type wi     = start / word_bits;
        size_type wi_end = (end - 1) / word_bits;
        for (size_type w = wi; w <= wi_end; ++w) {
            const word_type word       = words_[w];
            const size_type word_start = w * word_bits;
            const size_type a          = (w == wi) ? (start - word_start) : 0;
            const size_type b          = (w == wi_end) ? (end - word_start) : word_bits;
            const word_type mask       = make_mask(a, b);
            if ((word & mask) != mask) return false;
        }
        return true;
    }
    bool contains_any_in_range(size_type start, size_type end) const noexcept {
        if (start >= end) return false;
        if (start >= bits_) return false;
        end              = std::min(end, bits_);
        size_type wi     = start / word_bits;
        size_type wi_end = (end - 1) / word_bits;
        for (size_type w = wi; w <= wi_end; ++w) {
            const word_type word       = words_[w];
            const size_type word_start = w * word_bits;
            const size_type a          = (w == wi) ? (start - word_start) : 0;
            const size_type b          = (w == wi_end) ? (end - word_start) : word_bits;
            const word_type mask       = make_mask(a, b);
            if ((word & mask) != 0) return true;
        }
        return false;
    }

    size_type count_ones() const noexcept {
        size_type c = 0;
        for (auto w : words_) c += static_cast<size_type>(std::popcount(w));
        return c;
    }
    size_type count_zeros() const noexcept { return bits_ - count_ones(); }

    // modifiers (auto-grow when index out of bounds)
    void set(size_type pos, bool value = true) noexcept {
        ensure_size_for_index(pos);
        const size_type wi = pos / word_bits;
        const word_type m  = word_type(1) << static_cast<unsigned>(pos % word_bits);
        if (value)
            words_[wi] |= m;
        else
            words_[wi] &= ~m;
    }

    void set_range(size_type start, size_type end) noexcept {
        if (start >= end) return;
        ensure_size_for_index(end - 1);
        end              = std::min(end, bits_);
        size_type wi     = start / word_bits;
        size_type wi_end = (end - 1) / word_bits;
        for (size_type w = wi; w <= wi_end; ++w) {
            const size_type word_start = w * word_bits;
            const size_type a          = (w == wi) ? (start - word_start) : 0;
            const size_type b          = (w == wi_end) ? (end - word_start) : word_bits;
            words_[w] |= make_mask(a, b);
        }
    }
    template <typename R>
    void set_range(R&& range, bool value = true) noexcept
        requires std::ranges::input_range<R> && std::convertible_to<std::ranges::range_value_t<R>, size_type>
    {
        for (auto i : range) set(i, value);
    }

    bool intersect(const bit_vector& o) const noexcept {
        const size_type min_words = std::min(words_.size(), o.words_.size());
        for (size_type i = 0; i < min_words; ++i)
            if ((words_[i] & o.words_[i]) != 0) return true;
        return false;
    }

    // lazy view of indices where both bitvectors have ones
    auto intersection(const bit_vector& o) const noexcept {
        const size_type upper = std::min(bits_, o.bits_);
        return std::views::iota((size_type)0, upper) |
               std::views::filter([this, &o](size_type i) { return test(i) && o.test(i); });
    }

    // lazy view of indices where either bitvector has ones (union)
    auto set_union(const bit_vector& o) const noexcept {
        const size_type upper = std::max(bits_, o.bits_);
        return std::views::iota((size_type)0, upper) |
               std::views::filter([this, &o](size_type i) { return this->contains(i) || o.contains(i); });
    }

    // lazy view of indices present in this but not in o (difference)
    auto difference(const bit_vector& o) const noexcept {
        const size_type upper = bits_;
        return std::views::iota((size_type)0, upper) |
               std::views::filter([this, &o](size_type i) { return this->contains(i) && !o.contains(i); });
    }

    // lazy view of indices present in exactly one of the two bitvectors (symmetric difference / XOR)
    auto symmetric_difference(const bit_vector& o) const noexcept {
        const size_type upper = std::max(bits_, o.bits_);
        return std::views::iota((size_type)0, upper) |
               std::views::filter([this, &o](size_type i) { return this->contains(i) != o.contains(i); });
    }

    size_type intersect_count(const bit_vector& o) const noexcept {
        const size_type min_words = std::min(words_.size(), o.words_.size());
        size_type c               = 0;
        for (size_type i = 0; i < min_words; ++i) c += static_cast<size_type>(std::popcount(words_[i] & o.words_[i]));
        return c;
    }

    // count of bits set in the union of this and o (up to max(bits_ , o.bits_))
    size_type union_count(const bit_vector& o) const noexcept {
        const size_type max_bits = std::max(bits_, o.bits_);
        if (max_bits == 0) return 0;
        const size_type nwords = words_for(max_bits);
        size_type c            = 0;
        for (size_type i = 0; i < nwords; ++i) {
            const word_type wa = (i < words_.size()) ? words_[i] : word_type(0);
            const word_type wb = (i < o.words_.size()) ? o.words_[i] : word_type(0);
            word_type w        = wa | wb;
            if (i + 1 == nwords) {
                const size_type rem = max_bits % word_bits;
                if (rem != 0) w &= ((word_type(1) << static_cast<unsigned>(rem)) - 1);
            }
            c += static_cast<size_type>(std::popcount(w));
        }
        return c;
    }

    // count of bits present in this but not in o (i.e. |this \ o|), limited to this->size()
    size_type difference_count(const bit_vector& o) const noexcept {
        const size_type nwords = words_for(bits_);
        if (nwords == 0) return 0;
        size_type c = 0;
        for (size_type i = 0; i < nwords; ++i) {
            const word_type wa = (i < words_.size()) ? words_[i] : word_type(0);
            const word_type wb = (i < o.words_.size()) ? o.words_[i] : word_type(0);
            word_type w        = wa & ~wb;
            if (i + 1 == nwords) {
                const size_type rem = bits_ % word_bits;
                if (rem != 0) w &= ((word_type(1) << static_cast<unsigned>(rem)) - 1);
            }
            c += static_cast<size_type>(std::popcount(w));
        }
        return c;
    }

    // count of bits present in exactly one of the two bitvectors (symmetric difference / XOR)
    size_type symmetric_difference_count(const bit_vector& o) const noexcept {
        const size_type max_bits = std::max(bits_, o.bits_);
        if (max_bits == 0) return 0;
        const size_type nwords = words_for(max_bits);
        size_type c            = 0;
        for (size_type i = 0; i < nwords; ++i) {
            const word_type wa = (i < words_.size()) ? words_[i] : word_type(0);
            const word_type wb = (i < o.words_.size()) ? o.words_[i] : word_type(0);
            word_type w        = wa ^ wb;
            if (i + 1 == nwords) {
                const size_type rem = max_bits % word_bits;
                if (rem != 0) w &= ((word_type(1) << static_cast<unsigned>(rem)) - 1);
            }
            c += static_cast<size_type>(std::popcount(w));
        }
        return c;
    }

    // relation checks
    bool is_disjoint(const bit_vector& o) const noexcept {
        const size_type min_words = std::min(words_.size(), o.words_.size());
        for (size_type i = 0; i < min_words; ++i)
            if ((words_[i] & o.words_[i]) != 0) return false;
        return true;
    }

    bool is_subset(const bit_vector& o) const noexcept {
        // all bits set in this must also be set in o
        const size_type na = words_.size();
        const size_type nb = o.words_.size();
        for (size_type i = 0; i < na; ++i) {
            const word_type wa = words_[i];
            const word_type wb = (i < nb) ? o.words_[i] : word_type(0);
            if ((wa & ~wb) != 0) return false;
        }
        // also ensure any bits beyond na in this (there are none) handled
        return true;
    }

    bool is_superset(const bit_vector& o) const noexcept { return o.is_subset(*this); }

    bool is_clear() const noexcept {
        for (auto w : words_)
            if (w != 0) return false;
        return true;
    }

    bool is_full() const noexcept {
        if (bits_ == 0) return true;
        const size_type full_words = bits_ / word_bits;
        for (size_type i = 0; i < full_words; ++i)
            if (words_[i] != ~word_type(0)) return false;
        const size_type rem = bits_ % word_bits;
        if (rem == 0) return true;
        const word_type mask = (word_type(1) << static_cast<unsigned>(rem)) - 1;
        return !words_.empty() && words_.back() == mask;
    }

    // lazy ranges for indices of set/unset bits (views referencing *this)
    auto iter_ones() const noexcept {
        return std::views::iota((size_type)0, bits_) | std::views::filter([this](size_type i) { return test(i); });
    }

    auto iter_zeros() const noexcept {
        return std::views::iota((size_type)0, bits_) | std::views::filter([this](size_type i) { return !test(i); });
    }

    void reset(size_type pos) noexcept {
        if (pos >= bits_) return;
        const size_type wi = pos / word_bits;
        const word_type m  = word_type(1) << static_cast<unsigned>(pos % word_bits);
        words_[wi] &= ~m;
    }
    void reset_all() noexcept { std::fill(words_.begin(), words_.end(), word_type(0)); }
    void reset_range(size_type start, size_type end) noexcept {
        if (start >= end || start >= bits_) return;
        end              = std::min(end, bits_);
        size_type wi     = start / word_bits;
        size_type wi_end = (end - 1) / word_bits;
        for (size_type w = wi; w <= wi_end; ++w) {
            const size_type word_start = w * word_bits;
            const size_type a          = (w == wi) ? (start - word_start) : 0;
            const size_type b          = (w == wi_end) ? (end - word_start) : word_bits;
            words_[w] &= ~make_mask(a, b);
        }
        trim_tail();
    }

    void toggle(size_type pos) noexcept {
        ensure_size_for_index(pos);
        const size_type wi = pos / word_bits;
        const word_type m  = word_type(1) << static_cast<unsigned>(pos % word_bits);
        words_[wi] ^= m;
    }
    void toggle_all() noexcept {
        for (auto& w : words_) w = ~w;
        trim_tail();
    }
    void toggle_range(size_type start, size_type end) noexcept {
        if (start >= end) return;
        ensure_size_for_index(end - 1);
        end              = std::min(end, bits_);
        size_type wi     = start / word_bits;
        size_type wi_end = (end - 1) / word_bits;
        for (size_type w = wi; w <= wi_end; ++w) {
            const size_type word_start = w * word_bits;
            const size_type a          = (w == wi) ? (start - word_start) : 0;
            const size_type b          = (w == wi_end) ? (end - word_start) : word_bits;
            words_[w] ^= make_mask(a, b);
        }
        trim_tail();
    }

    // named bit ops returning new bitvector
    bit_vector bit_and(const bit_vector& o) const noexcept {
        bit_vector out;
        const size_type max_bits = std::max(bits_, o.bits_);
        out.resize(max_bits, false);
        const size_type min_words = std::min(words_.size(), o.words_.size());
        for (size_type i = 0; i < min_words; ++i) out.words_[i] = words_[i] & o.words_[i];
        // remaining words already zero
        out.trim_tail();
        return out;
    }
    bit_vector bit_or(const bit_vector& o) const noexcept {
        bit_vector out;
        const size_type max_bits = std::max(bits_, o.bits_);
        out.resize(max_bits, false);
        const size_type min_words = std::min(words_.size(), o.words_.size());
        for (size_type i = 0; i < min_words; ++i) out.words_[i] = words_[i] | o.words_[i];
        if (words_.size() > o.words_.size()) {
            for (size_type i = min_words; i < words_.size(); ++i) out.words_[i] = words_[i];
        } else {
            for (size_type i = min_words; i < o.words_.size(); ++i) out.words_[i] = o.words_[i];
        }
        out.trim_tail();
        return out;
    }
    bit_vector bit_xor(const bit_vector& o) const noexcept {
        bit_vector out;
        const size_type max_bits = std::max(bits_, o.bits_);
        out.resize(max_bits, false);
        const size_type min_words = std::min(words_.size(), o.words_.size());
        for (size_type i = 0; i < min_words; ++i) out.words_[i] = words_[i] ^ o.words_[i];
        if (words_.size() > o.words_.size()) {
            for (size_type i = min_words; i < words_.size(); ++i) out.words_[i] = words_[i];
        } else {
            for (size_type i = min_words; i < o.words_.size(); ++i) out.words_[i] = o.words_[i];
        }
        out.trim_tail();
        return out;
    }

    // assign variants (modify caller)
    bit_vector& bit_and_assign(const bit_vector& o) noexcept {
        const size_type min_words = std::min(words_.size(), o.words_.size());
        for (size_type i = 0; i < min_words; ++i) words_[i] &= o.words_[i];
        for (size_type i = min_words; i < words_.size(); ++i) words_[i] = 0;
        trim_tail();
        return *this;
    }
    bit_vector& bit_or_assign(const bit_vector& o) noexcept {
        const size_type max_bits = std::max(bits_, o.bits_);
        if (max_bits != bits_) resize(max_bits, false);
        for (size_type i = 0; i < o.words_.size(); ++i) words_[i] |= o.words_[i];
        trim_tail();
        return *this;
    }
    bit_vector& bit_xor_assign(const bit_vector& o) noexcept {
        const size_type max_bits = std::max(bits_, o.bits_);
        if (max_bits != bits_) resize(max_bits, false);
        for (size_type i = 0; i < o.words_.size(); ++i) words_[i] ^= o.words_[i];
        trim_tail();
        return *this;
    }

    // friendlier in-place names
    bit_vector& intersect_with(const bit_vector& o) noexcept { return bit_and_assign(o); }
    bit_vector& union_with(const bit_vector& o) noexcept { return bit_or_assign(o); }
    bit_vector& symmetric_difference_with(const bit_vector& o) noexcept { return bit_xor_assign(o); }

    // remove bits present in o from this (this = this \ o)
    bit_vector& difference_with(const bit_vector& o) noexcept {
        const size_type n = words_.size();
        for (size_type i = 0; i < n; ++i) {
            const word_type wb = (i < o.words_.size()) ? o.words_[i] : word_type(0);
            words_[i] &= ~wb;
        }
        trim_tail();
        return *this;
    }

    // utilities
    const std::vector<word_type>& words() const noexcept { return words_; }
    std::vector<word_type>& words() noexcept { return words_; }

    friend bool operator==(const bit_vector& a, const bit_vector& b) noexcept {
        if (a.bits_ != b.bits_) return false;
        // words_ may have extra capacity cleared by trim_tail; compare relevant word content
        const size_type na = a.words_.size();
        const size_type nb = b.words_.size();
        if (na != nb) {
            // normalize by padding shorter with zeros for comparison
            const size_type n = std::max(na, nb);
            for (size_type i = 0; i < n; ++i) {
                const word_type wa = (i < na) ? a.words_[i] : word_type(0);
                const word_type wb = (i < nb) ? b.words_[i] : word_type(0);
                if (wa != wb) return false;
            }
            return true;
        }
        return std::ranges::equal(a.words_, b.words_);
    }
    friend bool operator!=(const bit_vector& a, const bit_vector& b) noexcept { return !(a == b); }

    void swap(bit_vector& o) noexcept {
        using std::swap;
        swap(bits_, o.bits_);
        swap(words_, o.words_);
    }

    // compatibility helpers
    bool test(size_type pos) const noexcept {
        return ((words_[pos / word_bits] >> static_cast<unsigned>(pos % word_bits)) & word_type(1)) != 0;
    }

   private:
    size_type bits_ = 0;
    std::vector<word_type> words_;

    static constexpr size_type words_for(size_type bits) noexcept {
        return bits == 0 ? 0 : ((bits + word_bits - 1) / word_bits);
    }

    static constexpr word_type make_mask(size_type a, size_type b) noexcept {
        // mask bits [a, b) within a single word, 0 <= a <= b <= word_bits
        if (a == 0 && b == word_bits) return ~word_type(0);
        return (((word_type(1) << static_cast<unsigned>(b - a)) - 1) << static_cast<unsigned>(a));
    }

    void trim_tail() noexcept {
        if (bits_ == 0) {
            words_.clear();
            return;
        }
        const size_type rem = bits_ % word_bits;
        if (rem == 0) return;
        const word_type mask = (word_type(1) << static_cast<unsigned>(rem)) - 1;
        if (!words_.empty()) words_.back() &= mask;
    }

    void ensure_size_for_index(size_type pos) noexcept {
        if (pos < bits_) return;
        // grow to contain pos
        resize(pos + 1, false);
    }
};
}  // namespace core

export template <>
struct ::std::hash<::core::bit_vector> {
    size_t operator()(::core::bit_vector const& bv) const noexcept {
        using bv_t        = ::core::bit_vector;
        const auto& words = bv.words();
        size_t h          = std::ranges::fold_left(words, bv.size(), [](size_t acc, auto w) noexcept {
            return (acc * 0x9e3779b97f4a7c15ULL) ^
                   static_cast<size_t>(w + 0x9e3779b97f4a7c15ULL + (acc << 6) + (acc >> 2));
        });
        return h;
    }
};