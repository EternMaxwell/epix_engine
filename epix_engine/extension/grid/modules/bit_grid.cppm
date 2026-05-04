module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>
#endif

export module epix.extension.grid:bit_grid;

#ifdef EPIX_IMPORT_STD
import std;
#endif

import :basic_grid;

namespace epix::ext::grid {
/**
 * @brief A fixed-size, densely packed N-dimensional bit-grid for boolean occupancy.
 * Note that this is not a basic brid or extendible grid, this is similar to bit_vector in epix.utils but with
 * N-dimensional indexing and grid semantics.
 */
export template <std::size_t Dim>
struct bit_grid {
   private:
    std::array<std::uint32_t, Dim> m_dimensions;
    std::vector<std::uint8_t> m_words;  // each bit represents a cell; size is row_stride * dim[1] * dim[2] * ...
                                        // where row_stride = (dim[0] + 7) / 8 (bit_vector layout per row)

    std::size_t row_stride() const noexcept { return (m_dimensions[0] + 7u) / 8u; }

    // first 24 bits of the index are the position of the word, the last 8 bits is for bit-and
    std::expected<std::size_t, grid_error> offset(const std::array<std::uint32_t, Dim>& pos) const {
        for (std::size_t i = 0; i < Dim; ++i) {
            if (pos[i] >= m_dimensions[i]) return std::unexpected(grid_error::OutOfBounds);
        }
        // compute row index from higher dimensions (row-major over axes 1..Dim-1)
        std::size_t row_index     = 0;
        std::size_t row_stride_nd = 1;
        for (std::size_t i = 1; i < Dim; ++i) {
            row_index += pos[i] * row_stride_nd;
            row_stride_nd *= m_dimensions[i];
        }
        const std::size_t byte_offset = row_index * row_stride() + pos[0] / 8u;
        const std::uint8_t bit_mask   = static_cast<std::uint8_t>(1u << (pos[0] % 8u));
        return (byte_offset << 8u) | bit_mask;
    }

    std::array<std::uint32_t, Dim> index_to_pos(std::size_t index) const {
        std::array<std::uint32_t, Dim> pos{};
        for (std::size_t i = 0; i < Dim; ++i) {
            pos[i] = static_cast<std::uint32_t>(index % m_dimensions[i]);
            index /= m_dimensions[i];
        }
        return pos;
    }

    std::size_t total_cells() const noexcept {
        std::size_t n = 1;
        for (auto d : m_dimensions) n *= d;
        return n;
    }

    /** Set or clear a bit without bounds checking — pos must be within bounds. */
    void set_unsafe(const std::array<std::uint32_t, Dim>& pos, bool value = true) noexcept {
        std::size_t row_index = 0, rs = 1;
        for (std::size_t i = 1; i < Dim; ++i) {
            row_index += pos[i] * rs;
            rs *= m_dimensions[i];
        }
        const std::size_t bi = row_index * row_stride() + pos[0] / 8u;
        const std::uint8_t m = static_cast<std::uint8_t>(1u << (pos[0] % 8u));
        if (value)
            m_words[bi] |= m;
        else
            m_words[bi] &= static_cast<std::uint8_t>(~m);
    }

    /**
     * @brief Flat row index (the row's position in m_words / row_stride) given the multi-index
     *        for axes 1..Dim-1. For Dim==1 there is always exactly one row (returns 0).
     */
    static std::size_t flat_row(const std::array<std::uint32_t, Dim>& idx,
                                const std::array<std::uint32_t, Dim>& dims) noexcept {
        std::size_t flat = 0, mult = 1;
        for (std::size_t i = 1; i < Dim; ++i) {
            flat += idx[i] * mult;
            mult *= dims[i];
        }
        return flat;
    }

    /** @brief Return true if the row multi-index is within bounds for @p dims (axes 1..Dim-1). */
    static bool row_in_bounds(const std::array<std::uint32_t, Dim>& idx,
                              const std::array<std::uint32_t, Dim>& dims) noexcept {
        for (std::size_t i = 1; i < Dim; ++i)
            if (idx[i] >= dims[i]) return false;
        return true;
    }

    /**
     * @brief Increment the row multi-index (axes 1..Dim-1) within @p domain.
     * @return true if the new index is still valid, false when the domain is exhausted.
     */
    static bool increment_row(std::array<std::uint32_t, Dim>& idx,
                              const std::array<std::uint32_t, Dim>& domain) noexcept {
        for (std::size_t i = 1; i < Dim; ++i) {
            if (++idx[i] < domain[i]) return true;
            idx[i] = 0;
        }
        return false;  // all axes wrapped — exhausted
    }

   public:
    bit_grid(const std::array<std::uint32_t, Dim>& dimensions) : m_dimensions(dimensions) {
        std::size_t total_words = row_stride();
        for (std::size_t i = 1; i < Dim; ++i) total_words *= m_dimensions[i];
        m_words.resize(total_words, std::uint8_t(0));
    }

    std::array<std::uint32_t, Dim> dimensions() const { return m_dimensions; }

    /** @brief Test whether the bit at @p pos is set (returns false if out of bounds). */
    bool contains(const std::array<std::uint32_t, Dim>& pos) const {
        auto enc = offset(pos);
        if (!enc) return false;
        return (m_words[enc.value() >> 8u] & std::uint8_t(enc.value() & 0xFFu)) != 0;
    }

    /** @brief Get the bit at @p pos; returns error if out of bounds. */
    std::expected<bool, grid_error> get(const std::array<std::uint32_t, Dim>& pos) const {
        auto enc = offset(pos);
        if (!enc) return std::unexpected(enc.error());
        return (m_words[enc.value() >> 8u] & std::uint8_t(enc.value() & 0xFFu)) != 0;
    }

    /** @brief Set or clear the bit at @p pos; returns error if out of bounds. */
    std::expected<void, grid_error> set(const std::array<std::uint32_t, Dim>& pos, bool value = true) {
        auto enc = offset(pos);
        if (!enc) return std::unexpected(enc.error());
        const std::size_t wi = enc.value() >> 8u;
        const std::uint8_t m = std::uint8_t(enc.value() & 0xFFu);
        if (value)
            m_words[wi] |= m;
        else
            m_words[wi] &= static_cast<std::uint8_t>(~m);
        return {};
    }

    /** @brief Clear all bits without changing dimensions. */
    void reset() noexcept { std::fill(m_words.begin(), m_words.end(), std::uint8_t(0)); }

    /** @brief Count the number of set bits. */
    std::size_t count() const noexcept {
        std::size_t c = 0;
        for (auto w : m_words) c += static_cast<std::size_t>(std::popcount(w));
        return c;
    }

    /** @brief Return true if no bits are set. */
    bool is_clear() const noexcept {
        for (auto w : m_words)
            if (w) return false;
        return true;
    }

    /** @brief Check whether this and @p other share any set bits. */
    bool intersect(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i)
                if ((m_words[i] & other.m_words[i]) != 0) return true;
            return false;
        }
        std::array<std::uint32_t, Dim> min_dims;
        for (std::size_t i = 0; i < Dim; ++i) min_dims[i] = std::min(m_dimensions[i], other.m_dimensions[i]);
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::array<std::uint32_t, Dim> idx{};
        do {
            const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
            const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
            for (std::size_t j = 0; j < min_rs; ++j)
                if (m_words[a_row + j] & other.m_words[b_row + j]) return true;
        } while (increment_row(idx, min_dims));
        return false;
    }

    /** @brief Count bits set in the intersection of this and @p other. */
    std::size_t intersect_count(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            std::size_t c = 0;
            for (std::size_t i = 0; i < m_words.size(); ++i)
                c += static_cast<std::size_t>(std::popcount(static_cast<std::uint8_t>(m_words[i] & other.m_words[i])));
            return c;
        }
        std::array<std::uint32_t, Dim> min_dims;
        for (std::size_t i = 0; i < Dim; ++i) min_dims[i] = std::min(m_dimensions[i], other.m_dimensions[i]);
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::size_t c            = 0;
        std::array<std::uint32_t, Dim> idx{};
        do {
            const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
            const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
            for (std::size_t j = 0; j < min_rs; ++j)
                c += static_cast<std::size_t>(
                    std::popcount(static_cast<std::uint8_t>(m_words[a_row + j] & other.m_words[b_row + j])));
        } while (increment_row(idx, min_dims));
        return c;
    }

    /** @brief Count bits set in the union of this and @p other. */
    std::size_t union_count(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            std::size_t c = 0;
            for (std::size_t i = 0; i < m_words.size(); ++i)
                c += static_cast<std::size_t>(std::popcount(static_cast<std::uint8_t>(m_words[i] | other.m_words[i])));
            return c;
        }
        return count() + other.count() - intersect_count(other);
    }

    /** @brief Count bits set in this but not in @p other. */
    std::size_t difference_count(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            std::size_t c = 0;
            for (std::size_t i = 0; i < m_words.size(); ++i)
                c += static_cast<std::size_t>(std::popcount(static_cast<std::uint8_t>(m_words[i] & ~other.m_words[i])));
            return c;
        }
        return count() - intersect_count(other);
    }

    /** @brief Count bits set in exactly one of the two grids. */
    std::size_t symmetric_difference_count(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            std::size_t c = 0;
            for (std::size_t i = 0; i < m_words.size(); ++i)
                c += static_cast<std::size_t>(std::popcount(static_cast<std::uint8_t>(m_words[i] ^ other.m_words[i])));
            return c;
        }
        return count() + other.count() - 2u * intersect_count(other);
    }

    /** @brief Return true if this and @p other have no set bits in common. */
    bool is_disjoint(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i)
                if ((m_words[i] & other.m_words[i]) != 0) return false;
            return true;
        }
        return !intersect(other);
    }

    /** @brief Return true if every set bit in this is also set in @p other. */
    bool is_subset(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i)
                if ((m_words[i] & ~other.m_words[i]) != 0) return false;
            return true;
        }
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::array<std::uint32_t, Dim> idx{};
        do {
            const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
            if (!row_in_bounds(idx, other.m_dimensions)) {
                // row has no counterpart in other — any set bit means not a subset
                for (std::size_t j = 0; j < a_rs; ++j)
                    if (m_words[a_row + j]) return false;
            } else {
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j)
                    if (m_words[a_row + j] & ~other.m_words[b_row + j]) return false;
                // bits in this beyond other's axis-0 stride have no match in other
                for (std::size_t j = min_rs; j < a_rs; ++j)
                    if (m_words[a_row + j]) return false;
            }
        } while (increment_row(idx, m_dimensions));
        return true;
    }

    /** @brief Return true if every set bit in @p other is also set in this. */
    bool is_superset(const bit_grid& other) const noexcept { return other.is_subset(*this); }

    /** @brief Return a lazy view of positions where bits are set. */
    auto iter_set() const noexcept {
        std::size_t total = 1;
        for (auto d : m_dimensions) total *= d;
        return std::views::iota(std::size_t(0), total) |
               std::views::filter([this](std::size_t idx) { return contains(index_to_pos(idx)); }) |
               std::views::transform([this](std::size_t idx) { return index_to_pos(idx); });
    }

    /** @brief Return a lazy view of positions where bits are unset. */
    auto iter_unset() const noexcept {
        std::size_t total = 1;
        for (auto d : m_dimensions) total *= d;
        return std::views::iota(std::size_t(0), total) |
               std::views::filter([this](std::size_t idx) { return !contains(index_to_pos(idx)); }) |
               std::views::transform([this](std::size_t idx) { return index_to_pos(idx); });
    }

    /** @brief Return a lazy view of positions set in both this and @p other. */
    auto intersection(const bit_grid& other) const noexcept {
        std::size_t total = 1;
        for (auto d : m_dimensions) total *= d;
        return std::views::iota(std::size_t(0), total) | std::views::filter([this, &other](std::size_t idx) {
                   auto pos = index_to_pos(idx);
                   return contains(pos) && other.contains(pos);
               }) |
               std::views::transform([this](std::size_t idx) { return index_to_pos(idx); });
    }

    /** @brief Return a lazy view of positions set in either this or @p other.
     *  Iterates over the element-wise maximum of both grids' dimensions. */
    auto set_union(const bit_grid& other) const noexcept {
        std::array<std::uint32_t, Dim> max_dims;
        for (std::size_t i = 0; i < Dim; ++i) max_dims[i] = std::max(m_dimensions[i], other.m_dimensions[i]);
        std::size_t total = 1;
        for (auto d : max_dims) total *= d;
        return std::views::iota(std::size_t(0), total) | std::views::filter([this, &other, max_dims](std::size_t idx) {
                   std::array<std::uint32_t, Dim> pos{};
                   std::size_t tmp = idx;
                   for (std::size_t i = 0; i < Dim; ++i) {
                       pos[i] = static_cast<std::uint32_t>(tmp % max_dims[i]);
                       tmp /= max_dims[i];
                   }
                   return contains(pos) || other.contains(pos);
               }) |
               std::views::transform([max_dims](std::size_t idx) {
                   std::array<std::uint32_t, Dim> pos{};
                   std::size_t tmp = idx;
                   for (std::size_t i = 0; i < Dim; ++i) {
                       pos[i] = static_cast<std::uint32_t>(tmp % max_dims[i]);
                       tmp /= max_dims[i];
                   }
                   return pos;
               });
    }

    /** @brief Return a lazy view of positions set in this but not in @p other. */
    auto difference(const bit_grid& other) const noexcept {
        std::size_t total = 1;
        for (auto d : m_dimensions) total *= d;
        return std::views::iota(std::size_t(0), total) | std::views::filter([this, &other](std::size_t idx) {
                   auto pos = index_to_pos(idx);
                   return contains(pos) && !other.contains(pos);
               }) |
               std::views::transform([this](std::size_t idx) { return index_to_pos(idx); });
    }

    /** @brief Return a lazy view of positions set in exactly one of the two grids.
     *  Iterates over the element-wise maximum of both grids' dimensions. */
    auto symmetric_difference(const bit_grid& other) const noexcept {
        std::array<std::uint32_t, Dim> max_dims;
        for (std::size_t i = 0; i < Dim; ++i) max_dims[i] = std::max(m_dimensions[i], other.m_dimensions[i]);
        std::size_t total = 1;
        for (auto d : max_dims) total *= d;
        return std::views::iota(std::size_t(0), total) | std::views::filter([this, &other, max_dims](std::size_t idx) {
                   std::array<std::uint32_t, Dim> pos{};
                   std::size_t tmp = idx;
                   for (std::size_t i = 0; i < Dim; ++i) {
                       pos[i] = static_cast<std::uint32_t>(tmp % max_dims[i]);
                       tmp /= max_dims[i];
                   }
                   return contains(pos) != other.contains(pos);
               }) |
               std::views::transform([max_dims](std::size_t idx) {
                   std::array<std::uint32_t, Dim> pos{};
                   std::size_t tmp = idx;
                   for (std::size_t i = 0; i < Dim; ++i) {
                       pos[i] = static_cast<std::uint32_t>(tmp % max_dims[i]);
                       tmp /= max_dims[i];
                   }
                   return pos;
               });
    }

    /** @brief Return a new grid that is the bitwise AND of this and @p other.
     *  The result dimensions are the element-wise minimum of both grids. */
    bit_grid bit_and(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            bit_grid out(m_dimensions);
            for (std::size_t i = 0; i < m_words.size(); ++i) out.m_words[i] = m_words[i] & other.m_words[i];
            return out;
        }
        std::array<std::uint32_t, Dim> out_dims;
        for (std::size_t i = 0; i < Dim; ++i) out_dims[i] = std::min(m_dimensions[i], other.m_dimensions[i]);
        bit_grid out(out_dims);
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride(), o_rs = out.row_stride();
        // o_rs == min(a_rs, b_rs) because out_dims[0] == min(A[0], B[0])
        std::array<std::uint32_t, Dim> idx{};
        do {
            const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
            const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
            const std::size_t o_row = flat_row(idx, out_dims) * o_rs;
            for (std::size_t j = 0; j < o_rs; ++j)
                out.m_words[o_row + j] = m_words[a_row + j] & other.m_words[b_row + j];
        } while (increment_row(idx, out_dims));
        return out;
    }

    /** @brief Return a new grid that is the bitwise OR of this and @p other.
     *  The result dimensions are the element-wise maximum of both grids. */
    bit_grid bit_or(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            bit_grid out(m_dimensions);
            for (std::size_t i = 0; i < m_words.size(); ++i) out.m_words[i] = m_words[i] | other.m_words[i];
            return out;
        }
        std::array<std::uint32_t, Dim> out_dims;
        for (std::size_t i = 0; i < Dim; ++i) out_dims[i] = std::max(m_dimensions[i], other.m_dimensions[i]);
        bit_grid out(out_dims);  // zero-initialized
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride(), o_rs = out.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::array<std::uint32_t, Dim> idx{};
        do {
            const bool in_a         = row_in_bounds(idx, m_dimensions);
            const bool in_b         = row_in_bounds(idx, other.m_dimensions);
            const std::size_t o_row = flat_row(idx, out_dims) * o_rs;
            if (in_a && in_b) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j)
                    out.m_words[o_row + j] = m_words[a_row + j] | other.m_words[b_row + j];
                // copy remaining bytes from the wider grid (OR with 0 = identity)
                for (std::size_t j = min_rs; j < a_rs; ++j) out.m_words[o_row + j] = m_words[a_row + j];
                for (std::size_t j = min_rs; j < b_rs; ++j) out.m_words[o_row + j] = other.m_words[b_row + j];
            } else if (in_a) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                for (std::size_t j = 0; j < a_rs; ++j) out.m_words[o_row + j] = m_words[a_row + j];
            } else if (in_b) {
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < b_rs; ++j) out.m_words[o_row + j] = other.m_words[b_row + j];
            }
        } while (increment_row(idx, out_dims));
        return out;
    }

    /** @brief Return a new grid that is the bitwise XOR of this and @p other.
     *  The result dimensions are the element-wise maximum of both grids. */
    bit_grid bit_xor(const bit_grid& other) const noexcept {
        if (m_dimensions == other.m_dimensions) {
            bit_grid out(m_dimensions);
            for (std::size_t i = 0; i < m_words.size(); ++i) out.m_words[i] = m_words[i] ^ other.m_words[i];
            return out;
        }
        std::array<std::uint32_t, Dim> out_dims;
        for (std::size_t i = 0; i < Dim; ++i) out_dims[i] = std::max(m_dimensions[i], other.m_dimensions[i]);
        bit_grid out(out_dims);  // zero-initialized
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride(), o_rs = out.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::array<std::uint32_t, Dim> idx{};
        do {
            const bool in_a         = row_in_bounds(idx, m_dimensions);
            const bool in_b         = row_in_bounds(idx, other.m_dimensions);
            const std::size_t o_row = flat_row(idx, out_dims) * o_rs;
            if (in_a && in_b) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j)
                    out.m_words[o_row + j] = m_words[a_row + j] ^ other.m_words[b_row + j];
                // XOR with 0 = identity — copy remaining bytes from the wider side
                for (std::size_t j = min_rs; j < a_rs; ++j) out.m_words[o_row + j] = m_words[a_row + j];
                for (std::size_t j = min_rs; j < b_rs; ++j) out.m_words[o_row + j] = other.m_words[b_row + j];
            } else if (in_a) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                for (std::size_t j = 0; j < a_rs; ++j) out.m_words[o_row + j] = m_words[a_row + j];
            } else if (in_b) {
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < b_rs; ++j) out.m_words[o_row + j] = other.m_words[b_row + j];
            }
        } while (increment_row(idx, out_dims));
        return out;
    }

    /** @brief Return a new grid with all bits flipped (including tail padding bits). */
    bit_grid bit_not() const noexcept {
        bit_grid out(m_dimensions);
        for (std::size_t i = 0; i < m_words.size(); ++i) out.m_words[i] = static_cast<std::uint8_t>(~m_words[i]);
        // clear tail padding bits in each row so logical bits only
        if (m_dimensions[0] % 8u != 0) {
            const std::uint8_t tail_mask = static_cast<std::uint8_t>((1u << (m_dimensions[0] % 8u)) - 1u);
            const std::size_t stride     = row_stride();
            for (std::size_t row = 0; row < out.m_words.size() / stride; ++row)
                out.m_words[row * stride + stride - 1] &= tail_mask;
        }
        return out;
    }

    /** @brief In-place bitwise AND with @p other.
     *  Bits in this that are outside other's domain are cleared. */
    bit_grid& bit_and_assign(const bit_grid& other) noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i) m_words[i] &= other.m_words[i];
            return *this;
        }
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::array<std::uint32_t, Dim> idx{};
        do {
            const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
            if (!row_in_bounds(idx, other.m_dimensions)) {
                std::fill(m_words.begin() + a_row, m_words.begin() + a_row + a_rs, std::uint8_t(0));
            } else {
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j) m_words[a_row + j] &= other.m_words[b_row + j];
                // bits in this beyond other's axis-0 stride have no match — clear them
                for (std::size_t j = min_rs; j < a_rs; ++j) m_words[a_row + j] = std::uint8_t(0);
            }
        } while (increment_row(idx, m_dimensions));
        return *this;
    }

    /** @brief In-place bitwise OR with @p other.
     *  Bits from other that fall within this's domain are set. */
    bit_grid& bit_or_assign(const bit_grid& other) noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i) m_words[i] |= other.m_words[i];
            return *this;
        }
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        // Tail mask for last byte of this's row — prevents bits from other beyond axis-0 domain
        const std::size_t tail_bits = m_dimensions[0] % 8u;
        const std::uint8_t a_tail = tail_bits ? static_cast<std::uint8_t>((1u << tail_bits) - 1u) : std::uint8_t(0xFF);
        std::array<std::uint32_t, Dim> idx{};
        do {
            if (row_in_bounds(idx, other.m_dimensions)) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j) {
                    const std::uint8_t mask = (j == a_rs - 1) ? a_tail : std::uint8_t(0xFF);
                    m_words[a_row + j] |= other.m_words[b_row + j] & mask;
                }
            }
        } while (increment_row(idx, m_dimensions));
        return *this;
    }

    /** @brief In-place bitwise XOR with @p other.
     *  Bits from other that fall within this's domain are toggled. */
    bit_grid& bit_xor_assign(const bit_grid& other) noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i) m_words[i] ^= other.m_words[i];
            return *this;
        }
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs    = std::min(a_rs, b_rs);
        const std::size_t tail_bits = m_dimensions[0] % 8u;
        const std::uint8_t a_tail = tail_bits ? static_cast<std::uint8_t>((1u << tail_bits) - 1u) : std::uint8_t(0xFF);
        std::array<std::uint32_t, Dim> idx{};
        do {
            if (row_in_bounds(idx, other.m_dimensions)) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j) {
                    const std::uint8_t mask = (j == a_rs - 1) ? a_tail : std::uint8_t(0xFF);
                    m_words[a_row + j] ^= other.m_words[b_row + j] & mask;
                }
            }
        } while (increment_row(idx, m_dimensions));
        return *this;
    }

    /** @brief Remove bits set in @p other from this (this = this \ other). */
    bit_grid& difference_with(const bit_grid& other) noexcept {
        if (m_dimensions == other.m_dimensions) {
            for (std::size_t i = 0; i < m_words.size(); ++i) m_words[i] &= static_cast<std::uint8_t>(~other.m_words[i]);
            return *this;
        }
        const std::size_t a_rs = row_stride(), b_rs = other.row_stride();
        const std::size_t min_rs = std::min(a_rs, b_rs);
        std::array<std::uint32_t, Dim> idx{};
        do {
            if (row_in_bounds(idx, other.m_dimensions)) {
                const std::size_t a_row = flat_row(idx, m_dimensions) * a_rs;
                const std::size_t b_row = flat_row(idx, other.m_dimensions) * b_rs;
                for (std::size_t j = 0; j < min_rs; ++j)
                    m_words[a_row + j] &= static_cast<std::uint8_t>(~other.m_words[b_row + j]);
                // bits in this beyond other's axis-0 stride are not in other — leave them unchanged
            }
        } while (increment_row(idx, m_dimensions));
        return *this;
    }

    friend bool operator==(const bit_grid& a, const bit_grid& b) noexcept {
        return a.m_dimensions == b.m_dimensions && a.m_words == b.m_words;
    }
    friend bool operator!=(const bit_grid& a, const bit_grid& b) noexcept { return !(a == b); }

    friend bit_grid operator&(const bit_grid& a, const bit_grid& b) noexcept { return a.bit_and(b); }
    friend bit_grid operator|(const bit_grid& a, const bit_grid& b) noexcept { return a.bit_or(b); }
    friend bit_grid operator^(const bit_grid& a, const bit_grid& b) noexcept { return a.bit_xor(b); }
    friend bit_grid operator~(const bit_grid& a) noexcept { return a.bit_not(); }

    bit_grid& operator&=(const bit_grid& other) noexcept { return bit_and_assign(other); }
    bit_grid& operator|=(const bit_grid& other) noexcept { return bit_or_assign(other); }
    bit_grid& operator^=(const bit_grid& other) noexcept { return bit_xor_assign(other); }
};
}  // namespace epix::ext::grid