module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#endif

export module epix.extension.grid:basic_grid;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :concepts;

namespace epix::ext::grid {
constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();
bool is_npos(std::size_t index) { return index == npos; }
bool not_npos(std::size_t index) { return index != npos; }
/**
 * @brief A fixed-size, densely packed N-dimensional grid where every cell holds a value.
 *
 * All cells are initialized with a default value. Every position within the
 * grid dimensions is always occupied.
 * @tparam Dim Number of dimensions.
 * @tparam T   Cell value type (must be default-constructible, movable, and copyable).
 */
export template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T> && std::copyable<T>
struct packed_grid {
    using pos_type  = std::array<std::uint32_t, Dim>;
    using cell_type = T;

   private:
    std::vector<T> m_cells;
    std::array<std::uint32_t, Dim> m_dimensions;
    T m_default_value;

    std::size_t offset_unsafe(const std::array<std::uint32_t, Dim>& pos) const;
    std::expected<std::size_t, grid_error> offset(const std::array<std::uint32_t, Dim>& pos) const;
    std::array<std::uint32_t, Dim> index_to_pos(std::size_t index) const;

   public:
    /**
     * @brief Construct a packed grid with the given dimensions.
     * @param dimensions Size along each axis.
     * @param default_value Value used to initialize every cell.
     */
    packed_grid(const std::array<std::uint32_t, Dim>& dimensions, T default_value = T{});
    /**
     * @brief Change the default value used by reset().
     * @param value New default value.
     */
    void set_default(T value) { m_default_value = std::move(value); }
    /**
     * @brief Contains function for satisfying concept.
     */
    bool contains(const std::array<std::uint32_t, Dim>& pos) const { return offset(pos).has_value(); }
    /**
     * @brief Get the dimensions of the grid.
     * @return Array of sizes along each axis.
     */
    std::array<std::uint32_t, Dim> dimensions() const { return m_dimensions; }
    /**
     * @brief Get the size along a single axis.
     * @param index Axis index.
     * @return Size along the given axis.
     */
    std::size_t dimension(std::size_t index) const { return m_dimensions[index]; }
    /**
     * @brief Get the total number of cells in the grid.
     * @return Total number of cells.
     */
    std::size_t count() const { return m_cells.size(); }
    /** @brief Clear all cells in the grid. */
    void clear() { m_cells.assign(m_cells.size(), m_default_value); }
    /** @brief Iterate over the positions of  cells. */
    auto iter_pos() const {
        return std::views::iota(std::size_t{0}, count()) |
               std::views::transform([this](std::size_t index) { return index_to_pos(index); });
    }
    /** @brief Iterate over the values of all cells (const). */
    auto iter_cells() const { return std::views::all(m_cells); }
    /** @brief Iterate over the values of all cells (mutable). */
    auto iter_cells_mut() { return std::views::all(m_cells); }
    /** @brief Iterate over (position, value) pairs for all cells (const). */
    auto iter() const { return std::views::zip(iter_pos(), m_cells); }
    /** @brief Iterate over (position, value) pairs for all cells (mutable). */
    auto iter_mut() { return std::views::zip(iter_pos(), m_cells); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(const std::array<std::uint32_t, Dim>& pos) {
        return offset(pos).transform([this](std::size_t index) { return std::ref(m_cells[index]); });
    }
    T& get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos) { return m_cells[offset_unsafe(pos)]; }
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(const std::array<std::uint32_t, Dim>& pos) const {
        return offset(pos).transform([this](std::size_t index) { return std::cref(m_cells[index]); });
    }
    const T& get_unsafe(const std::array<std::uint32_t, Dim>& pos) const { return m_cells[offset_unsafe(pos)]; }
    /**
     * @brief Set the cell at the given position by constructing a new value in-place.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set(const std::array<std::uint32_t, Dim>& pos,
                                                             Args&&... value) {
        return offset(pos).transform(
            [&](std::size_t index) { return std::ref(m_cells[index] = T(std::forward<Args>(value)...)); });
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
        return m_cells[offset_unsafe(pos)] = T(std::forward<Args>(value)...);
    }
    /** @brief set_new always returns AlreadyOccupied: every cell in a packed grid is occupied. */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set_new(const std::array<std::uint32_t, Dim>& pos, Args&&...) {
        return offset(pos).and_then([](std::size_t) -> std::expected<std::reference_wrapper<T>, grid_error> {
            return std::unexpected(grid_error::AlreadyOccupied);
        });
    }
    /** @brief Reset cell to default value without bounds check. */
    void remove_unsafe(const std::array<std::uint32_t, Dim>& pos) { m_cells[offset_unsafe(pos)] = m_default_value; }
    /** @brief Take current value, reset to default, without bounds check. */
    T take_unsafe(const std::array<std::uint32_t, Dim>& pos) {
        const std::size_t idx = offset_unsafe(pos);
        T value               = std::move(m_cells[idx]);
        m_cells[idx]          = m_default_value;
        return value;
    }
    /**
     * @brief Reset the cell at the given position to the default value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> reset(const std::array<std::uint32_t, Dim>& pos) {
        return offset(pos).transform([&](std::size_t index) { m_cells[index] = m_default_value; });
    }
    /**
     * @brief Remove the cell at given pos, here meaning reset to default value. Alias for reset().
     * @param pos Position in the grid.
     * @return void on success, or grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(const std::array<std::uint32_t, Dim>& pos) { return reset(pos); }
    /**
     * @brief Take the cell at given pos, here meaning return current value and reset to default. Not really "take"
     * since it doesn't leave an empty cell, but provided for interface compatibility with other grid types.
     * @param pos Position in the grid.
     * @return Cell value on success, or grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(const std::array<std::uint32_t, Dim>& pos) {
        return offset(pos).transform([&](std::size_t index) {
            T value        = std::move(m_cells[index]);
            m_cells[index] = m_default_value;
            return value;
        });
    }
};
/**
 * @brief A fixed-size N-dimensional grid that stores only occupied cells densely.
 *
 * Cells are stored contiguously in a vector; an index grid maps positions
 * to data indices. Iteration is efficient as it only visits occupied cells.
 * Removal uses swap-and-pop so order is not preserved.
 * @tparam Dim Number of dimensions.
 * @tparam T   Cell value type (must be movable).
 */
export template <std::size_t Dim, typename T>
    requires std::movable<T>
struct dense_grid {
    using pos_type  = std::array<std::uint32_t, Dim>;
    using cell_type = T;

   private:
    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;  // positions of each cell in m_data
    packed_grid<Dim, std::size_t> m_index_grid;               // stores indices into m_data, or npos for empty

   public:
    /**
     * @brief Construct a dense grid with the given dimensions (initially empty).
     * @param dimensions Size along each axis.
     */
    dense_grid(const std::array<std::uint32_t, Dim>& dimensions) : m_index_grid(dimensions, npos) {
        std::size_t total = 1;
        for (std::uint32_t dim : dimensions) total *= dim;
        m_positions.reserve(total);
        m_data.reserve(total);
    }
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(const std::array<std::uint32_t, Dim>& pos) const {
        return m_index_grid.get(pos).value_or(npos) != npos;
    }
    /**
     * @brief Get the dimensions of the grid.
     * @return Array of sizes along each axis.
     */
    std::array<std::uint32_t, Dim> dimensions() const { return m_index_grid.dimensions(); }
    /**
     * @brief Get the size along a single axis.
     * @param index Axis index.
     * @return Size along the given axis.
     */
    std::size_t dimension(std::size_t index) const { return m_index_grid.dimension(index); }
    /**
     * @brief Get the total number of occupied cells in the grid.
     * @return Number of occupied cells.
     */
    std::size_t count() const { return m_data.size(); }
    /** @brief Clear all cells in the grid. */
    void clear() {
        m_data.clear();
        m_positions.clear();
        m_index_grid.clear();
    }
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return std::views::all(m_positions); }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return std::views::all(m_data); }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return std::views::all(m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(const std::array<std::uint32_t, Dim>& pos);
    T& get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(const std::array<std::uint32_t, Dim>& pos) const;
    const T& get_unsafe(const std::array<std::uint32_t, Dim>& pos) const;
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set(const std::array<std::uint32_t, Dim>& pos,
                                                             Args&&... value);
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set_new(const std::array<std::uint32_t, Dim>& pos,
                                                                 Args&&... value);
    void remove_unsafe(const std::array<std::uint32_t, Dim>& pos);
    T take_unsafe(const std::array<std::uint32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given position, discarding its value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(const std::array<std::uint32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given position and return its value.
     * @param pos Position in the grid.
     * @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(const std::array<std::uint32_t, Dim>& pos);
};
/**
 * @brief A fixed-size N-dimensional grid with sparse storage and index recycling.
 *
 * Similar to dense_grid but recycles freed data slots instead of
 * swap-and-pop, so data indices remain stable across removals.
 * @tparam Dim Number of dimensions.
 * @tparam T   Cell value type (must be movable).
 */
export template <std::size_t Dim, typename T>
    requires std::movable<T>
struct sparse_grid {
    using pos_type  = std::array<std::uint32_t, Dim>;
    using cell_type = T;

   private:
    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;  // positions of each cell in m_data
    std::vector<std::size_t> m_recycled_indices;              // indices in m_data that are free to use
    packed_grid<Dim, std::size_t> m_index_grid;               // stores indices into m_data, or npos for empty

    auto iter_valid_indices() const;

   public:
    /**
     * @brief Construct a sparse grid with the given dimensions (initially empty).
     * @param dimensions Size along each axis.
     */
    sparse_grid(const std::array<std::uint32_t, Dim>& dimensions) : m_index_grid(dimensions, npos) {
        std::size_t total = 1;
        for (std::uint32_t dim : dimensions) total *= dim;
        m_positions.reserve(total);
        m_data.reserve(total);
    }
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(const std::array<std::uint32_t, Dim>& pos) const {
        return m_index_grid.get(pos).value_or(npos) != npos;
    }
    /**
     * @brief Get the dimensions of the grid.
     * @return Array of sizes along each axis.
     */
    std::array<std::uint32_t, Dim> dimensions() const { return m_index_grid.dimensions(); }
    /**
     * @brief Get the size along a single axis.
     * @param index Axis index.
     * @return Size along the given axis.
     */
    std::size_t dimension(std::size_t index) const { return m_index_grid.dimension(index); }
    /**
     * @brief Get the total number of active cells in the grid.
     * @return Number of active cells.
     */
    std::size_t count() const { return m_data.size() - m_recycled_indices.size(); }
    /** @brief Clear all cells in the grid. */
    void clear() {
        m_data.clear();
        m_positions.clear();
        m_recycled_indices.clear();
        m_index_grid.clear();
    }
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) -> auto& { return m_positions[index]; });
    }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) -> auto& { return m_data[index]; });
    }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) -> auto& { return m_data[index]; });
    }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) { return std::tie(m_positions[index], m_data[index]); });
    }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) { return std::tie(m_positions[index], m_data[index]); });
    }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(const std::array<std::uint32_t, Dim>& pos);
    T& get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(const std::array<std::uint32_t, Dim>& pos) const;
    const T& get_unsafe(const std::array<std::uint32_t, Dim>& pos) const;
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set(const std::array<std::uint32_t, Dim>& pos,
                                                             Args&&... value);
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set_new(const std::array<std::uint32_t, Dim>& pos,
                                                                 Args&&... value);
    void remove_unsafe(const std::array<std::uint32_t, Dim>& pos);
    T take_unsafe(const std::array<std::uint32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given position, discarding its value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(const std::array<std::uint32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given position and return its value.
     * @param pos Position in the grid.
     * @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(const std::array<std::uint32_t, Dim>& pos);
};
/**
 * @brief A resizable N-dimensional grid with signed coordinates and dense storage.
 *
 * Supports negative positions through an internal origin offset. The grid
 * can be extended to cover a larger range and shrunk to fit occupied cells.
 * @tparam Dim Number of dimensions.
 * @tparam T   Cell value type (must be movable).
 */
export template <std::size_t Dim, typename T>
    requires std::movable<T>
struct dense_extendible_grid {
    using pos_type  = std::array<std::int32_t, Dim>;
    using cell_type = T;

   private:
    std::vector<T> m_data;
    std::vector<std::array<std::int32_t, Dim>> m_positions;  // positions of each cell in m_data, can be negative
    packed_grid<Dim, std::size_t> m_index_grid;              // stores indices into m_data, or -1 for empty
    std::array<std::int32_t, Dim> m_origin;

    std::array<std::uint32_t, Dim> relative_pos_unsafe(const std::array<std::int32_t, Dim>& pos) const;
    std::expected<std::array<std::uint32_t, Dim>, grid_error> relative_pos(
        const std::array<std::int32_t, Dim>& pos) const;

   public:
    /**
     * @brief Construct with the given initial dimensions centered at origin.
     */
    dense_extendible_grid();
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Signed position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(const std::array<std::int32_t, Dim>& pos) const;
    /**
     * @brief Get the current dimensions of the grid.
     * @return Array of sizes along each axis.
     */
    std::array<std::uint32_t, Dim> dimensions() const { return m_index_grid.dimensions(); }
    /**
     * @brief Get the size along a single axis.
     * @param index Axis index.
     * @return Size along the given axis.
     */
    std::size_t dimension(std::size_t index) const { return m_index_grid.dimension(index); }
    /**
     * @brief Get the total number of cells in the grid
     * @return Total number of cells.
     */
    std::size_t count() const { return m_data.size(); }
    /** @brief Clear all cells in the grid. */
    void clear() {
        m_data.clear();
        m_positions.clear();
        m_index_grid.clear();
        m_origin.fill(0);
    }
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return std::views::all(m_positions); }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return std::views::all(m_data); }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return std::views::all(m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Signed position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(const std::array<std::int32_t, Dim>& pos);
    T& get_mut_unsafe(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Signed position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(const std::array<std::int32_t, Dim>& pos) const;
    const T& get_unsafe(const std::array<std::int32_t, Dim>& pos) const;
    /**
     * @brief Extend the grid to cover the range [new_min, new_max).
     * @param new_min Minimum corner (inclusive) of the new range.
     * @param new_max Maximum corner (exclusive) of the new range.
     */
    void extend(const std::array<std::int32_t, Dim>& new_min, const std::array<std::int32_t, Dim>& new_max);
    /** @brief Shrink the grid to the bounding box of occupied cells. */
    void shrink();
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     * @tparam Args Constructor argument types.
     * @param pos   Signed position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set(const std::array<std::int32_t, Dim>& pos, Args&&... value);
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& set_unsafe(const std::array<std::int32_t, Dim>& pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     * @tparam Args Constructor argument types.
     * @param pos   Signed position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set_new(const std::array<std::int32_t, Dim>& pos,
                                                                 Args&&... value);
    void remove_unsafe(const std::array<std::int32_t, Dim>& pos);
    T take_unsafe(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given signed position, discarding its value.
     * @param pos Signed position in the grid.
     * @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given signed position and return its value.
     * @param pos Signed position in the grid.
     * @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(const std::array<std::int32_t, Dim>& pos);
};
/**
 * @brief A tree-based dynamically extendible N-dimensional grid.
 *
 * Uses an N-ary tree (with ChildCount children per axis per node) that
 * grows in depth as needed to accommodate any unsigned coordinate.
 * Coverage per axis equals ChildCount^level.
 * @tparam Dim        Number of dimensions.
 * @tparam T          Cell value type (must be movable).
 * @tparam ChildCount Number of children per axis at each tree level (>= 2).
 */
export template <std::size_t Dim, typename T, std::size_t ChildCount = 2>
    requires std::movable<T>
struct tree_extendible_grid {
    using pos_type  = std::array<std::int32_t, Dim>;
    using cell_type = T;

   private:
    constexpr static std::size_t child_per_node = [] consteval {
        std::size_t count = 1;
        for (std::size_t i = 0; i < Dim; i++) count *= ChildCount;
        return count;
    }();

    struct Node {
        std::array<std::size_t, child_per_node> children;  // indices of child nodes or cell data, or npos for empty
        Node() { children.fill(npos); }
    };

    std::vector<T> m_data;
    std::vector<std::array<std::int32_t, Dim>> m_positions;  // positions of each cell in m_data
    std::vector<Node> m_nodes;                               // tree nodes, root is always at index 0
    std::size_t m_depth;                                     // current tree depth, determines coverage
    std::array<std::int32_t, Dim> m_origin;                  // min covered coordinate for each axis

    static std::uint32_t pow_child(std::size_t exponent);
    static std::size_t flat_child_index(const std::array<std::uint32_t, Dim>& rel_pos, std::uint32_t stride);
    std::uint32_t axis_coverage() const;
    std::array<std::uint32_t, Dim> dimensions_impl() const;

    std::expected<std::array<std::uint32_t, Dim>, grid_error> relative_pos(
        const std::array<std::int32_t, Dim>& pos) const;
    std::expected<std::size_t, grid_error> find_index(const std::array<std::int32_t, Dim>& pos) const;
    std::expected<std::reference_wrapper<std::size_t>, grid_error> find_index_slot(
        const std::array<std::int32_t, Dim>& pos);
    std::size_t find_index_unsafe(const std::array<std::int32_t, Dim>& pos) const;
    std::size_t& find_index_slot_unsafe(const std::array<std::int32_t, Dim>& pos);
    void rebuild_tree();
    void ensure_covers(const std::array<std::int32_t, Dim>& pos);

   public:
    /** @brief Default-construct an empty tree grid. */
    tree_extendible_grid();
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(const std::array<std::int32_t, Dim>& pos) const;
    /**
     * @brief Get the current spatial coverage per axis.
     * @return Coverage extent along each axis.
     */
    std::uint32_t coverage() const;
    /**
     * @brief Get the current dimensions of the grid.
     * @return Array of sizes along each axis.
     */
    std::array<std::uint32_t, Dim> dimensions() const;
    /**
     * @brief Get the size along a single axis.
     * @param index Axis index.
     * @return Size along the given axis.
     */
    std::size_t dimension(std::size_t index) const;
    /**
     * @brief Get the number of occupied cells.
     * @return Count of stored elements.
     */
    std::size_t count() const;
    /** @brief Clear all cells in the grid. */
    void clear();
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return std::views::all(m_positions); }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return std::views::all(m_data); }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return std::views::all(m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(const std::array<std::int32_t, Dim>& pos);
    T& get_mut_unsafe(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(const std::array<std::int32_t, Dim>& pos) const;
    const T& get_unsafe(const std::array<std::int32_t, Dim>& pos) const;
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     *
     * The tree is automatically extended if the position exceeds current coverage.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set(const std::array<std::int32_t, Dim>& pos, Args&&... value);
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& set_unsafe(const std::array<std::int32_t, Dim>& pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     *
     * The tree is automatically extended if the position exceeds current coverage.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set_new(const std::array<std::int32_t, Dim>& pos,
                                                                 Args&&... value);
    void remove_unsafe(const std::array<std::int32_t, Dim>& pos);
    T take_unsafe(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given position, discarding its value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Remove the cell at the given position and return its value.
     * @param pos Position in the grid.
     * @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(const std::array<std::int32_t, Dim>& pos);
    /**
     * @brief Rebuild the tree index and shrink coverage to the occupied bounding box.
     *
     * Useful after frequent remove/take operations to compact index structure.
     */
    void shrink();
};
/**
 * @brief A tree-based fixed-size N-dimensional grid.
 *
 * Similar to tree_extendible_grid but with fixed depth and coverage determined
 * at construction time. Coordinates are unsigned and positions outside the
 * configured coverage return OutOfBounds.
 */
export template <std::size_t Dim, typename T, std::size_t ChildCount = 2>
    requires std::movable<T>
struct tree_grid {
    using pos_type  = std::array<std::uint32_t, Dim>;
    using cell_type = T;

   private:
    constexpr static std::size_t child_per_node = [] consteval {
        std::size_t count = 1;
        for (std::size_t i = 0; i < Dim; i++) count *= ChildCount;
        return count;
    }();

    struct Node {
        std::array<std::size_t, child_per_node> children;
        Node() { children.fill(npos); }
    };

    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;  // positions of each cell in m_data
    std::vector<Node> m_nodes;                                // tree nodes, root at index 0
    std::size_t m_depth;                                      // fixed tree depth
    std::array<std::uint32_t, Dim> m_origin;                  // min covered coordinate for each axis

    static std::uint32_t pow_child(std::size_t exponent);
    static std::size_t flat_child_index(const std::array<std::uint32_t, Dim>& rel_pos, std::uint32_t stride);
    std::uint32_t axis_coverage() const;
    std::array<std::uint32_t, Dim> dimensions_impl() const;

    std::expected<std::array<std::uint32_t, Dim>, grid_error> relative_pos(
        const std::array<std::uint32_t, Dim>& pos) const;
    std::expected<std::size_t, grid_error> find_index(const std::array<std::uint32_t, Dim>& pos) const;
    std::expected<std::reference_wrapper<std::size_t>, grid_error> find_index_slot(
        const std::array<std::uint32_t, Dim>& pos);
    std::size_t find_index_unsafe(const std::array<std::uint32_t, Dim>& pos) const;
    std::size_t& find_index_slot_unsafe(const std::array<std::uint32_t, Dim>& pos);
    void rebuild_tree();

   public:
    /** @brief Construct with required dimensions and optional origin (defaults to zeros).
     *  The constructor computes the minimal tree depth that provides coverage
     *  equal or greater than the requested dimensions along each axis.
     *  @param dimensions Requested sizes along each axis.
     *  @param origin Minimum coordinate covered by the grid (defaults to zero).
     */
    tree_grid(
        const std::array<std::uint32_t, Dim>& dimensions, const std::array<std::uint32_t, Dim>& origin = [] {
            std::array<std::uint32_t, Dim> o;
            o.fill(0);
            return o;
        }());

    /** @brief Check whether a cell exists at the given position.
     *  @param pos Unsigned position to query.
     *  @return true if a value is stored at @p pos.
     */
    bool contains(const std::array<std::uint32_t, Dim>& pos) const;
    /** @brief Get current per-axis coverage (ChildCount^depth). */
    std::uint32_t coverage() const;
    /** @brief Get the current dimensions (coverage) of the grid. */
    std::array<std::uint32_t, Dim> dimensions() const;
    /** @brief Get the size along a single axis. */
    std::size_t dimension(std::size_t index) const;
    /** @brief Get the number of occupied cells stored. */
    std::size_t count() const;
    /** @brief Clear all stored cells and reset the internal node table. */
    void clear();

    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return std::views::all(m_positions); }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return std::views::all(m_data); }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return std::views::all(m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }

    /** @brief Get a mutable reference to the cell at the given position.
     *  @param pos Unsigned position to query.
     *  @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(const std::array<std::uint32_t, Dim>& pos);
    T& get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos);
    /** @brief Get a const reference to the cell at the given position.
     *  @param pos Unsigned position to query.
     *  @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(const std::array<std::uint32_t, Dim>& pos) const;
    const T& get_unsafe(const std::array<std::uint32_t, Dim>& pos) const;

    /** @brief Set the cell at the given position, creating or overwriting.
     *  @tparam Args Constructor argument types.
     *  @param pos Position in the grid.
     *  @param value Constructor arguments forwarded to T.
     *  @return Reference to the stored value, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set(const std::array<std::uint32_t, Dim>& pos,
                                                             Args&&... value);
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value);

    /** @brief Set the cell only if it is currently empty.
     *  @tparam Args Constructor argument types.
     *  @param pos Position in the grid.
     *  @param value Constructor arguments forwarded to T.
     *  @return Reference to the stored value, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<std::reference_wrapper<T>, grid_error> set_new(const std::array<std::uint32_t, Dim>& pos,
                                                                 Args&&... value);
    void remove_unsafe(const std::array<std::uint32_t, Dim>& pos);
    T take_unsafe(const std::array<std::uint32_t, Dim>& pos);
    /** @brief Remove the cell at the given position, discarding its value.
     *  @param pos Position in the grid.
     *  @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(const std::array<std::uint32_t, Dim>& pos);
    /** @brief Remove the cell at the given position and return its value.
     *  @param pos Position in the grid.
     *  @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(const std::array<std::uint32_t, Dim>& pos);
    /** @brief Rebuild the internal node table and compact the index structure.
     *  Depth and origin remain unchanged.
     */
    void shrink();
};
}  // namespace epix::ext::grid

namespace epix::ext::grid {
template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T> && std::copyable<T>
std::size_t packed_grid<Dim, T>::offset_unsafe(const std::array<std::uint32_t, Dim>& pos) const {
    std::size_t index = 0;
    for (std::size_t i = 0; i < Dim; i++) {
        index *= m_dimensions[i];
        index += pos[i];
    }
    return index;
}
template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T> && std::copyable<T>
std::expected<std::size_t, grid_error> packed_grid<Dim, T>::offset(const std::array<std::uint32_t, Dim>& pos) const {
    std::size_t index = 0;
    for (std::size_t i = 0; i < Dim; i++) {
        if (pos[i] >= m_dimensions[i]) [[unlikely]]
            return std::unexpected(grid_error::OutOfBounds);
    }
    for (std::size_t i = 0; i < Dim; i++) {
        index *= m_dimensions[i];
        index += pos[i];
    }
    return index;
}
template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T> && std::copyable<T>
std::array<std::uint32_t, Dim> packed_grid<Dim, T>::index_to_pos(std::size_t index) const {
    std::array<std::uint32_t, Dim> pos;
    for (std::size_t i = Dim; i-- > 0;) {
        pos[i] = index % m_dimensions[i];
        index /= m_dimensions[i];
    }
    return pos;
}
template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T> && std::copyable<T>
packed_grid<Dim, T>::packed_grid(const std::array<std::uint32_t, Dim>& dimensions, T default_value)
    : m_dimensions(dimensions), m_default_value(std::move(default_value)) {
    std::size_t total_size = 1;
    for (const auto& dim : dimensions) {
        total_size *= dim;
    }
    m_cells.resize(total_size, m_default_value);
}

template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> dense_grid<Dim, T>::get_mut(
    const std::array<std::uint32_t, Dim>& pos) {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::ref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
T& dense_grid<Dim, T>::get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    return m_data[m_index_grid.get_unsafe(pos)];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> dense_grid<Dim, T>::get(
    const std::array<std::uint32_t, Dim>& pos) const {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::cref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
const T& dense_grid<Dim, T>::get_unsafe(const std::array<std::uint32_t, Dim>& pos) const {
    return m_data[m_index_grid.get_unsafe(pos)];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> dense_grid<Dim, T>::set(const std::array<std::uint32_t, Dim>& pos,
                                                                             Args&&... value) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index_ref = index_res.value();
    if (index_ref == npos) {
        // new cell
        m_data.emplace_back(std::forward<Args>(value)...);
        m_positions.push_back(pos);
        index_ref = m_data.size() - 1;
    } else {
        // existing cell
        m_data[index_ref] = T(std::forward<Args>(value)...);
    }
    return std::ref(m_data[index_ref]);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
T& dense_grid<Dim, T>::set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    std::size_t& index_ref = m_index_grid.get_mut_unsafe(pos);
    if (index_ref == npos) {
        m_data.emplace_back(std::forward<Args>(value)...);
        m_positions.push_back(pos);
        index_ref = m_data.size() - 1;
    } else {
        m_data[index_ref] = T(std::forward<Args>(value)...);
    }
    return m_data[index_ref];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> dense_grid<Dim, T>::set_new(
    const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index_ref = index_res.value();
    if (index_ref == npos) {
        // new cell
        m_data.emplace_back(std::forward<Args>(value)...);
        m_positions.push_back(pos);
        index_ref = m_data.size() - 1;
    } else {
        return std::unexpected(grid_error::AlreadyOccupied);
    }
    return std::ref(m_data[index_ref]);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<void, grid_error> dense_grid<Dim, T>::remove(const std::array<std::uint32_t, Dim>& pos) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index = index_res.value();
    if (index == npos) return std::unexpected(grid_error::EmptyCell);
    // swap-remove from m_data and m_positions
    std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        // update index grid for moved cell
        m_index_grid.set(m_positions[index], index);
    }
    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<T, grid_error> dense_grid<Dim, T>::take(const std::array<std::uint32_t, Dim>& pos) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index = index_res.value();
    if (index == npos) return std::unexpected(grid_error::EmptyCell);
    T value = std::move(m_data[index]);
    // swap-remove from m_data and m_positions
    std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        // update index grid for moved cell
        m_index_grid.set(m_positions[index], index);
    }
    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
    return value;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
void dense_grid<Dim, T>::remove_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    std::size_t& index           = m_index_grid.get_mut_unsafe(pos);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        m_index_grid.set_unsafe(m_positions[index], index);
    }
    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
T dense_grid<Dim, T>::take_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    std::size_t& index           = m_index_grid.get_mut_unsafe(pos);
    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        m_index_grid.set_unsafe(m_positions[index], index);
    }
    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
    return value;
}

template <std::size_t Dim, typename T>
    requires std::movable<T>
auto sparse_grid<Dim, T>::iter_valid_indices() const {
    return std::views::filter(std::views::iota(0u, m_positions.size()), [this](std::size_t index) {
        return m_index_grid.get(m_positions[index]).value_or(npos) == index;
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> sparse_grid<Dim, T>::get_mut(
    const std::array<std::uint32_t, Dim>& pos) {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::ref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
T& sparse_grid<Dim, T>::get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    return m_data[m_index_grid.get_unsafe(pos)];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> sparse_grid<Dim, T>::get(
    const std::array<std::uint32_t, Dim>& pos) const {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::cref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
const T& sparse_grid<Dim, T>::get_unsafe(const std::array<std::uint32_t, Dim>& pos) const {
    return m_data[m_index_grid.get_unsafe(pos)];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> sparse_grid<Dim, T>::set(const std::array<std::uint32_t, Dim>& pos,
                                                                              Args&&... value) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index_ref = index_res.value();
    if (index_ref == npos) {
        // new cell
        std::size_t new_index;
        if (!m_recycled_indices.empty()) {
            new_index = m_recycled_indices.back();
            m_recycled_indices.pop_back();
            m_data[new_index]      = T(std::forward<Args>(value)...);
            m_positions[new_index] = pos;
        } else {
            m_data.emplace_back(std::forward<Args>(value)...);
            m_positions.push_back(pos);
            new_index = m_data.size() - 1;
        }
        index_ref = new_index;
    } else {
        // existing cell
        m_data[index_ref] = T(std::forward<Args>(value)...);
    }
    return std::ref(m_data[index_ref]);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
T& sparse_grid<Dim, T>::set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    std::size_t& index_ref = m_index_grid.get_mut_unsafe(pos);
    if (index_ref == npos) {
        std::size_t new_index;
        if (!m_recycled_indices.empty()) {
            new_index = m_recycled_indices.back();
            m_recycled_indices.pop_back();
            m_data[new_index]      = T(std::forward<Args>(value)...);
            m_positions[new_index] = pos;
        } else {
            m_data.emplace_back(std::forward<Args>(value)...);
            m_positions.push_back(pos);
            new_index = m_data.size() - 1;
        }
        index_ref = new_index;
    } else {
        m_data[index_ref] = T(std::forward<Args>(value)...);
    }
    return m_data[index_ref];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> sparse_grid<Dim, T>::set_new(
    const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index_ref = index_res.value();
    if (index_ref == npos) {
        // new cell
        std::size_t new_index;
        if (!m_recycled_indices.empty()) {
            new_index = m_recycled_indices.back();
            m_recycled_indices.pop_back();
            m_data[new_index]      = T(std::forward<Args>(value)...);
            m_positions[new_index] = pos;
        } else {
            m_data.emplace_back(std::forward<Args>(value)...);
            m_positions.push_back(pos);
            new_index = m_data.size() - 1;
        }
        index_ref = new_index;
    } else {
        return std::unexpected(grid_error::AlreadyOccupied);
    }
    return std::ref(m_data[index_ref]);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<void, grid_error> sparse_grid<Dim, T>::remove(const std::array<std::uint32_t, Dim>& pos) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index = index_res.value();
    if (index == npos) return std::unexpected(grid_error::EmptyCell);
    // mark index as recycled
    m_recycled_indices.push_back(index);
    // data, pos invalid, but don't need destruct or reset
    index = npos;
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<T, grid_error> sparse_grid<Dim, T>::take(const std::array<std::uint32_t, Dim>& pos) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index = index_res.value();
    if (index == npos) return std::unexpected(grid_error::EmptyCell);
    T value = std::move(m_data[index]);
    // mark index as recycled
    m_recycled_indices.push_back(index);
    // data, pos invalid, but don't need destruct or reset
    index = npos;
    return value;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
void sparse_grid<Dim, T>::remove_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    std::size_t& index = m_index_grid.get_mut_unsafe(pos);
    m_recycled_indices.push_back(index);
    index = npos;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
T sparse_grid<Dim, T>::take_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    std::size_t& index = m_index_grid.get_mut_unsafe(pos);
    T value            = std::move(m_data[index]);
    m_recycled_indices.push_back(index);
    index = npos;
    return value;
}

template <std::size_t Dim, typename T>
    requires std::movable<T>
std::array<std::uint32_t, Dim> dense_extendible_grid<Dim, T>::relative_pos_unsafe(
    const std::array<std::int32_t, Dim>& pos) const {
    std::array<std::uint32_t, Dim> rel_pos;
    for (std::size_t i = 0; i < Dim; i++) {
        rel_pos[i] = static_cast<std::uint32_t>(pos[i] - m_origin[i]);
    }
    return rel_pos;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::array<std::uint32_t, Dim>, grid_error> dense_extendible_grid<Dim, T>::relative_pos(
    const std::array<std::int32_t, Dim>& pos) const {
    std::array<std::uint32_t, Dim> rel_pos;
    for (std::size_t i = 0; i < Dim; i++) {
        std::int32_t rel = pos[i] - m_origin[i];
        if (rel < 0) return std::unexpected(grid_error::OutOfBounds);
        rel_pos[i] = static_cast<std::uint32_t>(rel);
    }
    return rel_pos;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
dense_extendible_grid<Dim, T>::dense_extendible_grid()
    : m_index_grid(
          [] {
              std::array<std::uint32_t, Dim> initial_dims;
              initial_dims.fill(1);
              return initial_dims;
          }(),
          npos) {
    m_origin.fill(0);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
bool dense_extendible_grid<Dim, T>::contains(const std::array<std::int32_t, Dim>& pos) const {
    return relative_pos(pos)
        .and_then([this](auto rel) { return m_index_grid.get(rel); })
        .transform(not_npos)
        .value_or(false);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> dense_extendible_grid<Dim, T>::get_mut(
    const std::array<std::int32_t, Dim>& pos) {
    return relative_pos(pos).and_then([this](const std::array<std::uint32_t, Dim>& rel_pos) {
        return m_index_grid.get(rel_pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
                if (index == npos) return std::unexpected(grid_error::EmptyCell);
                return std::ref(m_data[index]);
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
T& dense_extendible_grid<Dim, T>::get_mut_unsafe(const std::array<std::int32_t, Dim>& pos) {
    return m_data[m_index_grid.get_unsafe(relative_pos_unsafe(pos))];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> dense_extendible_grid<Dim, T>::get(
    const std::array<std::int32_t, Dim>& pos) const {
    return relative_pos(pos).and_then([this](const std::array<std::uint32_t, Dim>& rel_pos) {
        return m_index_grid.get(rel_pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
                if (index == npos) return std::unexpected(grid_error::EmptyCell);
                return std::cref(m_data[index]);
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
const T& dense_extendible_grid<Dim, T>::get_unsafe(const std::array<std::int32_t, Dim>& pos) const {
    return m_data[m_index_grid.get_unsafe(relative_pos_unsafe(pos))];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
void dense_extendible_grid<Dim, T>::extend(const std::array<std::int32_t, Dim>& new_min,
                                           const std::array<std::int32_t, Dim>& new_max) {
    // both new_min and new_max are inclusive bounds
    auto old_dims = m_index_grid.dimensions();
    std::array<std::int32_t, Dim> actual_min, actual_max;
    bool needs_extend = false;
    for (std::size_t i = 0; i < Dim; i++) {
        actual_min[i] = std::min(m_origin[i], new_min[i]);
        actual_max[i] = std::max(m_origin[i] + static_cast<std::int32_t>(old_dims[i]) - 1, new_max[i]);
        if (actual_min[i] != m_origin[i] ||
            static_cast<std::uint32_t>(actual_max[i] - actual_min[i] + 1) != old_dims[i]) {
            needs_extend = true;
        }
    }
    if (!needs_extend) return;
    std::array<std::uint32_t, Dim> new_dims;
    for (std::size_t i = 0; i < Dim; i++) {
        new_dims[i] = static_cast<std::uint32_t>(actual_max[i] - actual_min[i] + 1);
    }
    packed_grid<Dim, std::size_t> new_grid(new_dims, npos);
    for (std::size_t idx = 0; idx < m_positions.size(); idx++) {
        std::array<std::uint32_t, Dim> new_rel;
        for (std::size_t i = 0; i < Dim; i++) {
            new_rel[i] = static_cast<std::uint32_t>(m_positions[idx][i] - actual_min[i]);
        }
        new_grid.set(new_rel, idx);
    }
    m_index_grid = std::move(new_grid);
    m_origin     = actual_min;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
void dense_extendible_grid<Dim, T>::shrink() {
    if (m_data.empty()) return;
    std::array<std::int32_t, Dim> bb_min = m_positions[0], bb_max = m_positions[0];
    for (const auto& pos : m_positions) {
        for (std::size_t i = 0; i < Dim; i++) {
            bb_min[i] = std::min(bb_min[i], pos[i]);
            bb_max[i] = std::max(bb_max[i], pos[i]);
        }
    }
    auto old_dims     = m_index_grid.dimensions();
    bool needs_shrink = false;
    for (std::size_t i = 0; i < Dim; i++) {
        if (bb_min[i] != m_origin[i] || static_cast<std::uint32_t>(bb_max[i] - bb_min[i] + 1) != old_dims[i]) {
            needs_shrink = true;
            break;
        }
    }
    if (!needs_shrink) return;
    std::array<std::uint32_t, Dim> new_dims;
    for (std::size_t i = 0; i < Dim; i++) {
        new_dims[i] = static_cast<std::uint32_t>(bb_max[i] - bb_min[i] + 1);
    }
    packed_grid<Dim, std::size_t> new_grid(new_dims, npos);
    for (std::size_t idx = 0; idx < m_positions.size(); idx++) {
        std::array<std::uint32_t, Dim> new_rel;
        for (std::size_t i = 0; i < Dim; i++) {
            new_rel[i] = static_cast<std::uint32_t>(m_positions[idx][i] - bb_min[i]);
        }
        new_grid.set(new_rel, idx);
    }
    m_index_grid = std::move(new_grid);
    m_origin     = bb_min;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> dense_extendible_grid<Dim, T>::set(
    const std::array<std::int32_t, Dim>& pos, Args&&... value) {
    extend(pos, pos);  // ensure position is in bounds, extending if necessary
    return relative_pos(pos).and_then([this, &pos, &value...](const std::array<std::uint32_t, Dim>& rel_pos) {
        return m_index_grid.get_mut(rel_pos).and_then(
            [this, &pos, &value...](std::size_t& index_ref) -> std::expected<std::reference_wrapper<T>, grid_error> {
                if (index_ref == npos) {
                    // new cell
                    m_data.emplace_back(std::forward<Args>(value)...);
                    m_positions.push_back(pos);
                    index_ref = m_data.size() - 1;
                } else {
                    // existing cell
                    m_data[index_ref] = T(std::forward<Args>(value)...);
                }
                return std::ref(m_data[index_ref]);
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
T& dense_extendible_grid<Dim, T>::set_unsafe(const std::array<std::int32_t, Dim>& pos, Args&&... value) {
    extend(pos, pos);
    std::size_t& index_ref = m_index_grid.get_mut_unsafe(relative_pos_unsafe(pos));
    if (index_ref == npos) {
        m_data.emplace_back(std::forward<Args>(value)...);
        m_positions.push_back(pos);
        index_ref = m_data.size() - 1;
    } else {
        m_data[index_ref] = T(std::forward<Args>(value)...);
    }
    return m_data[index_ref];
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> dense_extendible_grid<Dim, T>::set_new(
    const std::array<std::int32_t, Dim>& pos, Args&&... value) {
    extend(pos, pos);  // ensure position is in bounds, extending if necessary
    return relative_pos(pos).and_then([this, &pos, &value...](const std::array<std::uint32_t, Dim>& rel_pos) {
        return m_index_grid.get_mut(rel_pos).and_then(
            [this, &pos, &value...](std::size_t& index_ref) -> std::expected<std::reference_wrapper<T>, grid_error> {
                if (index_ref == npos) {
                    // new cell
                    m_data.emplace_back(std::forward<Args>(value)...);
                    m_positions.push_back(pos);
                    index_ref = m_data.size() - 1;
                } else {
                    // existing cell
                    return std::unexpected(grid_error::AlreadyOccupied);
                }
                return std::ref(m_data[index_ref]);
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<void, grid_error> dense_extendible_grid<Dim, T>::remove(const std::array<std::int32_t, Dim>& pos) {
    auto rel_pos_res = relative_pos(pos);
    if (!rel_pos_res.has_value()) return std::unexpected(rel_pos_res.error());

    auto index_res = m_index_grid.get_mut(rel_pos_res.value());
    if (!index_res.has_value()) return std::unexpected(index_res.error());

    std::size_t& index = index_res.value();
    if (index == npos) return std::unexpected(grid_error::EmptyCell);

    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);

        auto moved_rel_pos_res = relative_pos(m_positions[index]);
        if (!moved_rel_pos_res.has_value()) return std::unexpected(moved_rel_pos_res.error());

        auto set_res = m_index_grid.set(moved_rel_pos_res.value(), index);
        if (!set_res.has_value()) return std::unexpected(set_res.error());
    }

    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<T, grid_error> dense_extendible_grid<Dim, T>::take(const std::array<std::int32_t, Dim>& pos) {
    auto rel_pos_res = relative_pos(pos);
    if (!rel_pos_res.has_value()) return std::unexpected(rel_pos_res.error());

    auto index_res = m_index_grid.get_mut(rel_pos_res.value());
    if (!index_res.has_value()) return std::unexpected(index_res.error());

    std::size_t& index = index_res.value();
    if (index == npos) return std::unexpected(grid_error::EmptyCell);

    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);

        auto moved_rel_pos_res = relative_pos(m_positions[index]);
        if (!moved_rel_pos_res.has_value()) return std::unexpected(moved_rel_pos_res.error());

        auto set_res = m_index_grid.set(moved_rel_pos_res.value(), index);
        if (!set_res.has_value()) return std::unexpected(set_res.error());
    }

    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
    return value;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
void dense_extendible_grid<Dim, T>::remove_unsafe(const std::array<std::int32_t, Dim>& pos) {
    std::size_t& index           = m_index_grid.get_mut_unsafe(relative_pos_unsafe(pos));
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        m_index_grid.set_unsafe(relative_pos_unsafe(m_positions[index]), index);
    }
    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
T dense_extendible_grid<Dim, T>::take_unsafe(const std::array<std::int32_t, Dim>& pos) {
    std::size_t& index           = m_index_grid.get_mut_unsafe(relative_pos_unsafe(pos));
    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        m_index_grid.set_unsafe(relative_pos_unsafe(m_positions[index]), index);
    }
    m_data.pop_back();
    m_positions.pop_back();
    index = npos;
    return value;
}

template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_extendible_grid<Dim, T, ChildCount>::pow_child(std::size_t exponent) {
    std::uint32_t result = 1;
    for (std::size_t i = 0; i < exponent; i++) result *= static_cast<std::uint32_t>(ChildCount);
    return result;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_extendible_grid<Dim, T, ChildCount>::flat_child_index(const std::array<std::uint32_t, Dim>& rel_pos,
                                                                       std::uint32_t stride) {
    std::size_t index = 0;
    for (std::size_t axis = 0; axis < Dim; axis++) {
        const std::size_t digit = static_cast<std::size_t>((rel_pos[axis] / stride) % ChildCount);
        index                   = index * ChildCount + digit;
    }
    return index;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_extendible_grid<Dim, T, ChildCount>::axis_coverage() const {
    return pow_child(m_depth);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::array<std::uint32_t, Dim> tree_extendible_grid<Dim, T, ChildCount>::dimensions_impl() const {
    std::array<std::uint32_t, Dim> dims;
    dims.fill(axis_coverage());
    return dims;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::array<std::uint32_t, Dim>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::relative_pos(
    const std::array<std::int32_t, Dim>& pos) const {
    std::array<std::uint32_t, Dim> rel;
    const std::uint32_t cov = axis_coverage();
    for (std::size_t axis = 0; axis < Dim; axis++) {
        const std::int64_t delta = static_cast<std::int64_t>(pos[axis]) - static_cast<std::int64_t>(m_origin[axis]);
        if (delta < 0 || delta >= static_cast<std::int64_t>(cov)) return std::unexpected(grid_error::OutOfBounds);
        rel[axis] = static_cast<std::uint32_t>(delta);
    }
    return rel;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::size_t, grid_error> tree_extendible_grid<Dim, T, ChildCount>::find_index(
    const std::array<std::int32_t, Dim>& pos) const {
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    const auto rel           = rel_res.value();
    std::size_t current_node = 0;

    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        const std::size_t entry    = m_nodes[current_node].children[child];
        if (entry == npos) return std::unexpected(grid_error::EmptyCell);

        if (level + 1 == m_depth) return entry;
        if (entry >= m_nodes.size()) return std::unexpected(grid_error::EmptyCell);
        current_node = entry;
    }

    return std::unexpected(grid_error::EmptyCell);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_extendible_grid<Dim, T, ChildCount>::find_index_unsafe(
    const std::array<std::int32_t, Dim>& pos) const {
    const auto rel           = relative_pos(pos).value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        const std::size_t entry    = m_nodes[current_node].children[child];
        if (level + 1 == m_depth) return entry;
        current_node = entry;
    }
    return npos;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<std::size_t>, grid_error>
tree_extendible_grid<Dim, T, ChildCount>::find_index_slot(const std::array<std::int32_t, Dim>& pos) {
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    const auto rel           = rel_res.value();
    std::size_t current_node = 0;

    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];
        if (entry == npos) return std::unexpected(grid_error::EmptyCell);

        if (level + 1 == m_depth) return std::ref(entry);
        if (entry >= m_nodes.size()) return std::unexpected(grid_error::EmptyCell);
        current_node = entry;
    }

    return std::unexpected(grid_error::EmptyCell);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t& tree_extendible_grid<Dim, T, ChildCount>::find_index_slot_unsafe(
    const std::array<std::int32_t, Dim>& pos) {
    const auto rel           = relative_pos(pos).value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];
        if (level + 1 == m_depth) return entry;
        current_node = entry;
    }
    std::unreachable();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::rebuild_tree() {
    m_nodes.clear();
    m_nodes.emplace_back();

    for (std::size_t data_index = 0; data_index < m_positions.size(); data_index++) {
        auto rel = relative_pos(m_positions[data_index]);
        if (!rel.has_value()) continue;

        std::size_t current_node = 0;
        for (std::size_t level = 0; level < m_depth; level++) {
            const std::uint32_t stride = pow_child(m_depth - level - 1);
            const std::size_t child    = flat_child_index(rel.value(), stride);
            std::size_t& entry         = m_nodes[current_node].children[child];

            if (level + 1 == m_depth) {
                entry = data_index;
                continue;
            }

            if (entry == npos) {
                const std::size_t new_node_index = m_nodes.size();
                entry                            = new_node_index;
                m_nodes.emplace_back();
                current_node = new_node_index;
                continue;
            }
            current_node = entry;
        }
    }
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::ensure_covers(const std::array<std::int32_t, Dim>& pos) {
    if (relative_pos(pos).has_value()) return;

    const std::uint32_t old_cov           = axis_coverage();
    std::array<std::int32_t, Dim> new_min = m_origin;
    std::array<std::int32_t, Dim> new_max;
    for (std::size_t axis = 0; axis < Dim; axis++) {
        new_max[axis] = m_origin[axis] + static_cast<std::int32_t>(old_cov) - 1;
        new_min[axis] = std::min(new_min[axis], pos[axis]);
        new_max[axis] = std::max(new_max[axis], pos[axis]);
    }

    std::size_t new_depth = m_depth;
    std::uint32_t new_cov = old_cov;
    while (true) {
        bool enough = true;
        for (std::size_t axis = 0; axis < Dim; axis++) {
            const std::int64_t extent =
                static_cast<std::int64_t>(new_max[axis]) - static_cast<std::int64_t>(new_min[axis]) + 1;
            if (extent > static_cast<std::int64_t>(new_cov)) {
                enough = false;
                break;
            }
        }
        if (enough) break;
        new_depth++;
        new_cov *= static_cast<std::uint32_t>(ChildCount);
    }

    m_depth  = new_depth;
    m_origin = new_min;
    rebuild_tree();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
tree_extendible_grid<Dim, T, ChildCount>::tree_extendible_grid() : m_depth(1) {
    m_origin.fill(0);
    m_nodes.emplace_back();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
bool tree_extendible_grid<Dim, T, ChildCount>::contains(const std::array<std::int32_t, Dim>& pos) const {
    return find_index(pos).has_value();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_extendible_grid<Dim, T, ChildCount>::coverage() const {
    return axis_coverage();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::array<std::uint32_t, Dim> tree_extendible_grid<Dim, T, ChildCount>::dimensions() const {
    return dimensions_impl();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_extendible_grid<Dim, T, ChildCount>::dimension(std::size_t index) const {
    return dimensions_impl()[index];
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_extendible_grid<Dim, T, ChildCount>::count() const {
    return m_data.size();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::clear() {
    m_data.clear();
    m_positions.clear();
    m_depth = 1;
    m_origin.fill(0);
    m_nodes.clear();
    m_nodes.emplace_back();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::get_mut(
    const std::array<std::int32_t, Dim>& pos) {
    return find_index(pos).transform([this](std::size_t index) { return std::ref(m_data[index]); });
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
T& tree_extendible_grid<Dim, T, ChildCount>::get_mut_unsafe(const std::array<std::int32_t, Dim>& pos) {
    return m_data[find_index_unsafe(pos)];
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::get(
    const std::array<std::int32_t, Dim>& pos) const {
    return find_index(pos).transform([this](std::size_t index) { return std::cref(m_data[index]); });
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
const T& tree_extendible_grid<Dim, T, ChildCount>::get_unsafe(const std::array<std::int32_t, Dim>& pos) const {
    return m_data[find_index_unsafe(pos)];
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::set(
    const std::array<std::int32_t, Dim>& pos, Args&&... value) {
    ensure_covers(pos);
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel_res.value(), stride);
        std::size_t& entry         = m_nodes[current_node].children[child];

        if (level + 1 == m_depth) {
            if (entry == npos) {
                m_data.emplace_back(std::forward<Args>(value)...);
                m_positions.push_back(pos);
                entry = m_data.size() - 1;
            } else {
                m_data[entry] = T(std::forward<Args>(value)...);
            }
            return std::ref(m_data[entry]);
        }

        if (entry == npos) {
            const std::size_t new_node_index = m_nodes.size();
            entry                            = new_node_index;
            m_nodes.emplace_back();
            current_node = new_node_index;
            continue;
        }
        current_node = entry;
    }

    return std::unexpected(grid_error::InvalidPos);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
T& tree_extendible_grid<Dim, T, ChildCount>::set_unsafe(const std::array<std::int32_t, Dim>& pos, Args&&... value) {
    ensure_covers(pos);
    const auto rel           = relative_pos(pos).value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];

        if (level + 1 == m_depth) {
            if (entry == npos) {
                m_data.emplace_back(std::forward<Args>(value)...);
                m_positions.push_back(pos);
                entry = m_data.size() - 1;
            } else {
                m_data[entry] = T(std::forward<Args>(value)...);
            }
            return m_data[entry];
        }

        if (entry == npos) {
            const std::size_t new_node_index = m_nodes.size();
            entry                            = new_node_index;
            m_nodes.emplace_back();
            current_node = new_node_index;
            continue;
        }
        current_node = entry;
    }
    std::unreachable();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::set_new(
    const std::array<std::int32_t, Dim>& pos, Args&&... value) {
    ensure_covers(pos);
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel_res.value(), stride);
        std::size_t& entry         = m_nodes[current_node].children[child];

        if (level + 1 == m_depth) {
            if (entry == npos) {
                m_data.emplace_back(std::forward<Args>(value)...);
                m_positions.push_back(pos);
                entry = m_data.size() - 1;
                return std::ref(m_data[entry]);
            }
            return std::unexpected(grid_error::AlreadyOccupied);
        }

        if (entry == npos) {
            const std::size_t new_node_index = m_nodes.size();
            entry                            = new_node_index;
            m_nodes.emplace_back();
            current_node = new_node_index;
            continue;
        }
        current_node = entry;
    }

    return std::unexpected(grid_error::InvalidPos);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<void, grid_error> tree_extendible_grid<Dim, T, ChildCount>::remove(
    const std::array<std::int32_t, Dim>& pos) {
    auto slot_res = find_index_slot(pos);
    if (!slot_res.has_value()) return std::unexpected(slot_res.error());

    std::size_t& removed_slot    = slot_res.value();
    std::size_t index            = removed_slot;
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);

        auto moved_slot_res = find_index_slot(m_positions[index]);
        if (!moved_slot_res.has_value()) return std::unexpected(moved_slot_res.error());
        moved_slot_res.value().get() = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
    return {};
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<T, grid_error> tree_extendible_grid<Dim, T, ChildCount>::take(const std::array<std::int32_t, Dim>& pos) {
    auto slot_res = find_index_slot(pos);
    if (!slot_res.has_value()) return std::unexpected(slot_res.error());

    std::size_t& removed_slot    = slot_res.value();
    std::size_t index            = removed_slot;
    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);

        auto moved_slot_res = find_index_slot(m_positions[index]);
        if (!moved_slot_res.has_value()) return std::unexpected(moved_slot_res.error());
        moved_slot_res.value().get() = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
    return value;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::remove_unsafe(const std::array<std::int32_t, Dim>& pos) {
    std::size_t& removed_slot    = find_index_slot_unsafe(pos);
    const std::size_t index      = removed_slot;
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        find_index_slot_unsafe(m_positions[index]) = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
T tree_extendible_grid<Dim, T, ChildCount>::take_unsafe(const std::array<std::int32_t, Dim>& pos) {
    std::size_t& removed_slot    = find_index_slot_unsafe(pos);
    const std::size_t index      = removed_slot;
    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        find_index_slot_unsafe(m_positions[index]) = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
    return value;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::shrink() {
    if (m_data.empty()) {
        m_depth = 1;
        m_origin.fill(0);
        m_nodes.clear();
        m_nodes.emplace_back();
        return;
    }

    std::array<std::int32_t, Dim> bb_min = m_positions[0];
    std::array<std::int32_t, Dim> bb_max = m_positions[0];
    for (const auto& pos : m_positions) {
        for (std::size_t axis = 0; axis < Dim; axis++) {
            bb_min[axis] = std::min(bb_min[axis], pos[axis]);
            bb_max[axis] = std::max(bb_max[axis], pos[axis]);
        }
    }

    std::uint32_t required_cov = 1;
    for (std::size_t axis = 0; axis < Dim; axis++) {
        const std::uint32_t extent = static_cast<std::uint32_t>(bb_max[axis] - bb_min[axis] + 1);
        required_cov               = std::max(required_cov, extent);
    }

    std::size_t new_depth = 1;
    std::uint32_t new_cov = pow_child(new_depth);
    while (new_cov < required_cov) {
        new_depth++;
        new_cov *= static_cast<std::uint32_t>(ChildCount);
    }

    m_depth  = new_depth;
    m_origin = bb_min;
    rebuild_tree();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_grid<Dim, T, ChildCount>::pow_child(std::size_t exponent) {
    std::uint32_t result = 1;
    for (std::size_t i = 0; i < exponent; i++) result *= static_cast<std::uint32_t>(ChildCount);
    return result;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_grid<Dim, T, ChildCount>::flat_child_index(const std::array<std::uint32_t, Dim>& rel_pos,
                                                            std::uint32_t stride) {
    std::size_t index = 0;
    for (std::size_t axis = 0; axis < Dim; axis++) {
        const std::size_t digit = static_cast<std::size_t>((rel_pos[axis] / stride) % ChildCount);
        index                   = index * ChildCount + digit;
    }
    return index;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_grid<Dim, T, ChildCount>::axis_coverage() const {
    return pow_child(m_depth);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::array<std::uint32_t, Dim> tree_grid<Dim, T, ChildCount>::dimensions_impl() const {
    std::array<std::uint32_t, Dim> dims;
    dims.fill(axis_coverage());
    return dims;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::array<std::uint32_t, Dim>, grid_error> tree_grid<Dim, T, ChildCount>::relative_pos(
    const std::array<std::uint32_t, Dim>& pos) const {
    std::array<std::uint32_t, Dim> rel;
    const std::uint32_t cov = axis_coverage();
    for (std::size_t axis = 0; axis < Dim; axis++) {
        if (pos[axis] < m_origin[axis]) return std::unexpected(grid_error::OutOfBounds);
        const std::uint32_t delta = pos[axis] - m_origin[axis];
        if (delta >= cov) return std::unexpected(grid_error::OutOfBounds);
        rel[axis] = delta;
    }
    return rel;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::size_t, grid_error> tree_grid<Dim, T, ChildCount>::find_index(
    const std::array<std::uint32_t, Dim>& pos) const {
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    const auto rel           = rel_res.value();
    std::size_t current_node = 0;

    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        const std::size_t entry    = m_nodes[current_node].children[child];
        if (entry == npos) return std::unexpected(grid_error::EmptyCell);

        if (level + 1 == m_depth) return entry;
        if (entry >= m_nodes.size()) return std::unexpected(grid_error::EmptyCell);
        current_node = entry;
    }

    return std::unexpected(grid_error::EmptyCell);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_grid<Dim, T, ChildCount>::find_index_unsafe(const std::array<std::uint32_t, Dim>& pos) const {
    const auto rel           = relative_pos(pos).value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        const std::size_t entry    = m_nodes[current_node].children[child];
        if (level + 1 == m_depth) return entry;
        current_node = entry;
    }
    return npos;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<std::size_t>, grid_error> tree_grid<Dim, T, ChildCount>::find_index_slot(
    const std::array<std::uint32_t, Dim>& pos) {
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    const auto rel           = rel_res.value();
    std::size_t current_node = 0;

    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];
        if (entry == npos) return std::unexpected(grid_error::EmptyCell);

        if (level + 1 == m_depth) return std::ref(entry);
        if (entry >= m_nodes.size()) return std::unexpected(grid_error::EmptyCell);
        current_node = entry;
    }

    return std::unexpected(grid_error::EmptyCell);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t& tree_grid<Dim, T, ChildCount>::find_index_slot_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    const auto rel           = relative_pos(pos).value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];
        if (level + 1 == m_depth) return entry;
        current_node = entry;
    }
    std::unreachable();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_grid<Dim, T, ChildCount>::rebuild_tree() {
    m_nodes.clear();
    m_nodes.emplace_back();

    for (std::size_t data_index = 0; data_index < m_positions.size(); data_index++) {
        auto rel = relative_pos(m_positions[data_index]);
        if (!rel.has_value()) continue;

        std::size_t current_node = 0;
        for (std::size_t level = 0; level < m_depth; level++) {
            const std::uint32_t stride = pow_child(m_depth - level - 1);
            const std::size_t child    = flat_child_index(rel.value(), stride);
            std::size_t& entry         = m_nodes[current_node].children[child];

            if (level + 1 == m_depth) {
                entry = data_index;
                continue;
            }

            if (entry == npos) {
                const std::size_t new_node_index = m_nodes.size();
                entry                            = new_node_index;
                m_nodes.emplace_back();
                current_node = new_node_index;
                continue;
            }
            current_node = entry;
        }
    }
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
tree_grid<Dim, T, ChildCount>::tree_grid(const std::array<std::uint32_t, Dim>& dimensions,
                                         const std::array<std::uint32_t, Dim>& origin)
    : m_origin(origin) {
    // determine required coverage from requested dimensions
    std::uint32_t required_cov = 1;
    for (std::size_t i = 0; i < Dim; ++i) required_cov = std::max(required_cov, dimensions[i]);

    m_depth           = 1;
    std::uint32_t cov = pow_child(m_depth);
    while (cov < required_cov) {
        ++m_depth;
        cov *= static_cast<std::uint32_t>(ChildCount);
    }

    m_nodes.emplace_back();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
bool tree_grid<Dim, T, ChildCount>::contains(const std::array<std::uint32_t, Dim>& pos) const {
    return find_index(pos).has_value();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_grid<Dim, T, ChildCount>::coverage() const {
    return axis_coverage();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::array<std::uint32_t, Dim> tree_grid<Dim, T, ChildCount>::dimensions() const {
    return dimensions_impl();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_grid<Dim, T, ChildCount>::dimension(std::size_t index) const {
    return dimensions_impl()[index];
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_grid<Dim, T, ChildCount>::count() const {
    return m_data.size();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_grid<Dim, T, ChildCount>::clear() {
    m_data.clear();
    m_positions.clear();
    m_nodes.clear();
    m_nodes.emplace_back();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> tree_grid<Dim, T, ChildCount>::get_mut(
    const std::array<std::uint32_t, Dim>& pos) {
    return find_index(pos).transform([this](std::size_t index) { return std::ref(m_data[index]); });
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
T& tree_grid<Dim, T, ChildCount>::get_mut_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    return m_data[find_index_unsafe(pos)];
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> tree_grid<Dim, T, ChildCount>::get(
    const std::array<std::uint32_t, Dim>& pos) const {
    return find_index(pos).transform([this](std::size_t index) { return std::cref(m_data[index]); });
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
const T& tree_grid<Dim, T, ChildCount>::get_unsafe(const std::array<std::uint32_t, Dim>& pos) const {
    return m_data[find_index_unsafe(pos)];
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> tree_grid<Dim, T, ChildCount>::set(
    const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    const auto rel           = rel_res.value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];

        if (level + 1 == m_depth) {
            if (entry == npos) {
                m_data.emplace_back(std::forward<Args>(value)...);
                m_positions.push_back(pos);
                entry = m_data.size() - 1;
            } else {
                m_data[entry] = T(std::forward<Args>(value)...);
            }
            return std::ref(m_data[entry]);
        }

        if (entry == npos) {
            const std::size_t new_node_index = m_nodes.size();
            entry                            = new_node_index;
            m_nodes.emplace_back();
            current_node = new_node_index;
            continue;
        }
        current_node = entry;
    }

    return std::unexpected(grid_error::InvalidPos);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
T& tree_grid<Dim, T, ChildCount>::set_unsafe(const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    const auto rel           = relative_pos(pos).value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];

        if (level + 1 == m_depth) {
            if (entry == npos) {
                m_data.emplace_back(std::forward<Args>(value)...);
                m_positions.push_back(pos);
                entry = m_data.size() - 1;
            } else {
                m_data[entry] = T(std::forward<Args>(value)...);
            }
            return m_data[entry];
        }

        if (entry == npos) {
            const std::size_t new_node_index = m_nodes.size();
            entry                            = new_node_index;
            m_nodes.emplace_back();
            current_node = new_node_index;
            continue;
        }
        current_node = entry;
    }
    std::unreachable();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<std::reference_wrapper<T>, grid_error> tree_grid<Dim, T, ChildCount>::set_new(
    const std::array<std::uint32_t, Dim>& pos, Args&&... value) {
    auto rel_res = relative_pos(pos);
    if (!rel_res.has_value()) return std::unexpected(rel_res.error());

    const auto rel           = rel_res.value();
    std::size_t current_node = 0;
    for (std::size_t level = 0; level < m_depth; level++) {
        const std::uint32_t stride = pow_child(m_depth - level - 1);
        const std::size_t child    = flat_child_index(rel, stride);
        std::size_t& entry         = m_nodes[current_node].children[child];

        if (level + 1 == m_depth) {
            if (entry == npos) {
                m_data.emplace_back(std::forward<Args>(value)...);
                m_positions.push_back(pos);
                entry = m_data.size() - 1;
                return std::ref(m_data[entry]);
            }
            return std::unexpected(grid_error::AlreadyOccupied);
        }

        if (entry == npos) {
            const std::size_t new_node_index = m_nodes.size();
            entry                            = new_node_index;
            m_nodes.emplace_back();
            current_node = new_node_index;
            continue;
        }
        current_node = entry;
    }

    return std::unexpected(grid_error::InvalidPos);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<void, grid_error> tree_grid<Dim, T, ChildCount>::remove(const std::array<std::uint32_t, Dim>& pos) {
    auto slot_res = find_index_slot(pos);
    if (!slot_res.has_value()) return std::unexpected(slot_res.error());

    std::size_t& removed_slot    = slot_res.value();
    std::size_t index            = removed_slot;
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);

        auto moved_slot_res = find_index_slot(m_positions[index]);
        if (!moved_slot_res.has_value()) return std::unexpected(moved_slot_res.error());
        moved_slot_res.value().get() = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
    return {};
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<T, grid_error> tree_grid<Dim, T, ChildCount>::take(const std::array<std::uint32_t, Dim>& pos) {
    auto slot_res = find_index_slot(pos);
    if (!slot_res.has_value()) return std::unexpected(slot_res.error());

    std::size_t& removed_slot    = slot_res.value();
    std::size_t index            = removed_slot;
    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);

        auto moved_slot_res = find_index_slot(m_positions[index]);
        if (!moved_slot_res.has_value()) return std::unexpected(moved_slot_res.error());
        moved_slot_res.value().get() = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
    return value;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_grid<Dim, T, ChildCount>::shrink() {
    // rebuild nodes to compact internal structure; depth and origin remain fixed
    rebuild_tree();
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_grid<Dim, T, ChildCount>::remove_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    std::size_t& removed_slot    = find_index_slot_unsafe(pos);
    const std::size_t index      = removed_slot;
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        find_index_slot_unsafe(m_positions[index]) = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
T tree_grid<Dim, T, ChildCount>::take_unsafe(const std::array<std::uint32_t, Dim>& pos) {
    std::size_t& removed_slot    = find_index_slot_unsafe(pos);
    const std::size_t index      = removed_slot;
    T value                      = std::move(m_data[index]);
    const std::size_t last_index = m_data.size() - 1;
    if (index != last_index) {
        std::swap(m_data[index], m_data[last_index]);
        std::swap(m_positions[index], m_positions[last_index]);
        find_index_slot_unsafe(m_positions[index]) = index;
    }
    m_data.pop_back();
    m_positions.pop_back();
    removed_slot = npos;
    return value;
}

}  // namespace epix::ext::grid