#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace epix::utils {

/** @brief Insertion-ordered map with constant-time lookup and swap removal.
 *
 * This mirrors the `IndexMap` operations used by Bevy render code. Iteration
 * preserves insertion order until `swap_remove` or `swap_remove_index` moves
 * the final entry into a removed position. */
EPIX_EXPORT template <typename K, typename V>
class IndexMap {
   public:
    using value_type = std::pair<K, V>;

    V& operator[](const K& key) {
        if (auto it = m_indices.find(key); it != m_indices.end()) return m_entries[it->second].second;
        const std::size_t index = m_entries.size();
        m_indices.emplace(key, index);
        m_entries.emplace_back(key, V{});
        return m_entries.back().second;
    }

    V* get(const K& key) {
        if (auto it = m_indices.find(key); it != m_indices.end()) return &m_entries[it->second].second;
        return nullptr;
    }
    const V* get(const K& key) const {
        if (auto it = m_indices.find(key); it != m_indices.end()) return &m_entries[it->second].second;
        return nullptr;
    }
    bool contains(const K& key) const { return m_indices.contains(key); }
    bool empty() const noexcept { return m_entries.empty(); }
    std::size_t size() const noexcept { return m_entries.size(); }
    void clear() noexcept {
        m_entries.clear();
        m_indices.clear();
    }

    /** @brief Remove an entry in O(1), moving the final entry into its position. */
    bool swap_remove(const K& key) {
        auto it = m_indices.find(key);
        return it != m_indices.end() && swap_remove_index(it->second).has_value();
    }

    /** @brief Remove an entry at an insertion position in O(1).
     * The moved entry's lookup index is updated before this returns. */
    std::optional<value_type> swap_remove_index(std::size_t index) {
        if (index >= m_entries.size()) return std::nullopt;
        value_type removed = std::move(m_entries[index]);
        const std::size_t last = m_entries.size() - 1;
        m_indices.erase(removed.first);
        if (index != last) {
            m_entries[index] = std::move(m_entries[last]);
            m_indices.at(m_entries[index].first) = index;
        }
        m_entries.pop_back();
        return std::optional<value_type>{std::move(removed)};
    }

    /** @brief Position of a key in insertion order, if present. */
    std::optional<std::size_t> index_of(const K& key) const {
        if (auto it = m_indices.find(key); it != m_indices.end()) return it->second;
        return std::nullopt;
    }

    /** @brief Sort entries by key, rebuilding lookup indices. */
    void sort_unstable_keys()
        requires std::totally_ordered<K>
    {
        std::sort(m_entries.begin(), m_entries.end(),
                  [](const value_type& lhs, const value_type& rhs) { return lhs.first < rhs.first; });
        m_indices.clear();
        for (std::size_t index = 0; index < m_entries.size(); ++index) m_indices.emplace(m_entries[index].first, index);
    }

    auto begin() { return m_entries.begin(); }
    auto end() { return m_entries.end(); }
    auto begin() const { return m_entries.begin(); }
    auto end() const { return m_entries.end(); }
    auto iter() { return std::views::all(m_entries); }
    auto iter() const { return std::views::all(m_entries); }

   private:
    std::vector<value_type> m_entries;
    std::unordered_map<K, std::size_t> m_indices;
};

}  // namespace epix::utils
