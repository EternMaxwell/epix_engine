export module epix.experimental.basic_grid;

import std;

namespace ext::grid {
constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();
bool is_npos(std::size_t index) { return index == npos; }
bool not_npos(std::size_t index) { return index != npos; }
/** @brief Error codes returned by grid operations. */
export enum class grid_error {
    OutOfBounds,     /**< Position is outside the grid bounds. */
    InvalidPos,      /**< Position is invalid. */
    EmptyCell,       /**< The cell at the given position is empty. */
    AlreadyOccupied, /**< The cell at the given position is already occupied. */
};
/**
 * @brief A fixed-size, densely packed N-dimensional grid where every cell holds a value.
 *
 * All cells are initialized with a default value. Every position within the
 * grid dimensions is always occupied.
 * @tparam Dim Number of dimensions.
 * @tparam T   Cell value type (must be default-constructible and movable).
 */
export template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T>
struct packed_grid {
   private:
    std::vector<T> m_cells;
    std::array<std::uint32_t, Dim> m_dimensions;
    T m_default_value;

    std::expected<std::size_t, grid_error> offset(std::array<std::uint32_t, Dim> pos) const;

   public:
    /**
     * @brief Construct a packed grid with the given dimensions.
     * @param dimensions Size along each axis.
     * @param default_value Value used to initialize every cell.
     */
    packed_grid(std::array<std::uint32_t, Dim> dimensions, T default_value = T{});
    /**
     * @brief Change the default value used by reset().
     * @param value New default value.
     */
    void set_default(T value) { m_default_value = std::move(value); }
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
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos) {
        return offset(pos).transform([this](std::size_t index) { return std::ref(m_cells[index]); });
    }
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const {
        return offset(pos).transform([this](std::size_t index) { return std::cref(m_cells[index]); });
    }
    /**
     * @brief Set the cell at the given position by constructing a new value in-place.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        return offset(pos).transform([&](std::size_t index) { m_cells[index] = T(std::forward<Args>(value)...); });
    }
    /**
     * @brief Reset the cell at the given position to the default value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> reset(std::array<std::uint32_t, Dim> pos) {
        return offset(pos).transform([&](std::size_t index) { m_cells[index] = m_default_value; });
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
   private:
    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;  // positions of each cell in m_data
    packed_grid<Dim, std::size_t> m_index_grid;               // stores indices into m_data, or npos for empty

   public:
    /**
     * @brief Construct a dense grid with the given dimensions (initially empty).
     * @param dimensions Size along each axis.
     */
    dense_grid(std::array<std::uint32_t, Dim> dimensions) : m_index_grid(dimensions, npos) {}
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(std::array<std::uint32_t, Dim> pos) const { return m_index_grid.get(pos).value_or(npos) != npos; }
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
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return m_positions | std::views::all; }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return m_data | std::views::all; }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return m_data | std::views::all; }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const;
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::uint32_t, Dim> pos, Args&&... value);
    /**
     * @brief Remove the cell at the given position, discarding its value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(std::array<std::uint32_t, Dim> pos);
    /**
     * @brief Remove the cell at the given position and return its value.
     * @param pos Position in the grid.
     * @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(std::array<std::uint32_t, Dim> pos);
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
    sparse_grid(std::array<std::uint32_t, Dim> dimensions) : m_index_grid(dimensions, npos) {}
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(std::array<std::uint32_t, Dim> pos) const { return m_index_grid.get(pos).value_or(npos) != npos; }
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
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const;
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::uint32_t, Dim> pos, Args&&... value);
    /**
     * @brief Remove the cell at the given position, discarding its value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<void, grid_error> remove(std::array<std::uint32_t, Dim> pos);
    /**
     * @brief Remove the cell at the given position and return its value.
     * @param pos Position in the grid.
     * @return The taken value, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<T, grid_error> take(std::array<std::uint32_t, Dim> pos);
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
   private:
    std::vector<T> m_data;
    std::vector<std::array<std::int32_t, Dim>> m_positions;  // positions of each cell in m_data, can be negative
    packed_grid<Dim, std::size_t> m_index_grid;              // stores indices into m_data, or -1 for empty
    std::array<std::int32_t, Dim> m_origin;

    std::expected<std::array<std::uint32_t, Dim>, grid_error> relative_pos(std::array<std::int32_t, Dim> pos) const;

   public:
    /**
     * @brief Construct with the given initial dimensions centered at origin.
     * @param dimensions Initial size along each axis.
     */
    dense_extendible_grid(std::array<std::uint32_t, Dim> dimensions);
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Signed position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(std::array<std::int32_t, Dim> pos) const;
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
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return m_positions | std::views::all; }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return m_data | std::views::all; }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return m_data | std::views::all; }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Signed position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::int32_t, Dim> pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Signed position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell / grid_error::OutOfBounds.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::int32_t, Dim> pos) const;
    /**
     * @brief Extend the grid to cover the range [new_min, new_max).
     * @param new_min Minimum corner (inclusive) of the new range.
     * @param new_max Maximum corner (exclusive) of the new range.
     */
    void extend(std::array<std::int32_t, Dim> new_min, std::array<std::int32_t, Dim> new_max);
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
    std::expected<void, grid_error> set(std::array<std::int32_t, Dim> pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     * @tparam Args Constructor argument types.
     * @param pos   Signed position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied / grid_error::OutOfBounds.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::int32_t, Dim> pos, Args&&... value);
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
    static_assert(ChildCount >= 2, "ChildCount must be at least 2");

   private:
    static constexpr std::size_t children_per_node = [] {
        std::size_t r = 1;
        for (std::size_t i = 0; i < Dim; i++) r *= ChildCount;
        return r;
    }();
    struct node {
        std::array<std::size_t, children_per_node> children;
        node() { children.fill(npos); }
    };

    std::vector<node> m_nodes;
    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;
    std::size_t m_root    = npos;
    std::uint32_t m_level = 0;  // tree depth; coverage per axis = ChildCount^m_level

    static std::size_t flat_child_index(std::array<std::uint32_t, Dim> pos, std::uint32_t stride);
    std::size_t alloc_node();
    std::uint32_t compute_coverage() const;
    std::uint32_t required_level(std::array<std::uint32_t, Dim> pos) const;
    void ensure_level(std::uint32_t needed);
    void update_data_index(std::array<std::uint32_t, Dim> pos, std::size_t new_data_index);

   public:
    /** @brief Default-construct an empty tree grid. */
    tree_extendible_grid() = default;
    /**
     * @brief Check whether a cell exists at the given position.
     * @param pos Position to query.
     * @return true if a value is stored at @p pos.
     */
    bool contains(std::array<std::uint32_t, Dim> pos) const;
    /**
     * @brief Get the current spatial coverage per axis (ChildCount^level).
     * @return Coverage extent along each axis.
     */
    std::uint32_t coverage() const { return compute_coverage(); }
    /**
     * @brief Get the current dimensions of the grid, which is a cube with side length equal to coverage().
     * @return Array of sizes along each axis.
     */
    std::array<std::uint32_t, Dim> dimensions() const {
        std::uint32_t cov = compute_coverage();
        std::array<std::uint32_t, Dim> dims;
        dims.fill(cov);
        return dims;
    }
    /**
     * @brief Get the size along a single axis.
     * @param index Axis index.
     * @return Size along the given axis.
     */
    std::size_t dimension(std::size_t index) const { return compute_coverage(); }
    /**
     * @brief Get the number of occupied cells.
     * @return Count of stored elements.
     */
    std::size_t count() const { return m_data.size(); }
    /** @brief Iterate over the positions of all occupied cells. */
    auto iter_pos() const { return m_positions | std::views::all; }
    /** @brief Iterate over the values of all occupied cells (const). */
    auto iter_cells() const { return m_data | std::views::all; }
    /** @brief Iterate over the values of all occupied cells (mutable). */
    auto iter_cells_mut() { return m_data | std::views::all; }
    /** @brief Iterate over (position, value) pairs for all occupied cells (const). */
    auto iter() const { return std::views::zip(m_positions, m_data); }
    /** @brief Iterate over (position, value) pairs for all occupied cells (mutable). */
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    /**
     * @brief Get a mutable reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Reference to the cell, or grid_error::EmptyCell.
     */
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos);
    /**
     * @brief Get a const reference to the cell at the given position.
     * @param pos Position in the grid.
     * @return Const reference to the cell, or grid_error::EmptyCell.
     */
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const;
    /**
     * @brief Set the cell at the given position, creating or overwriting.
     *
     * The tree is automatically extended if the position exceeds the current coverage.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or an error.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value);
    /**
     * @brief Set the cell only if it is currently empty.
     *
     * The tree is automatically extended if the position exceeds the current coverage.
     * @tparam Args Constructor argument types.
     * @param pos   Position in the grid.
     * @param value Constructor arguments forwarded to T.
     * @return void on success, or grid_error::AlreadyOccupied.
     */
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::uint32_t, Dim> pos, Args&&... value);
    /**
     * @brief Remove the cell at the given position, discarding its value.
     * @param pos Position in the grid.
     * @return void on success, or grid_error::EmptyCell.
     */
    std::expected<void, grid_error> remove(std::array<std::uint32_t, Dim> pos);
    /**
     * @brief Remove the cell at the given position and return its value.
     * @param pos Position in the grid.
     * @return The taken value, or grid_error::EmptyCell.
     */
    std::expected<T, grid_error> take(std::array<std::uint32_t, Dim> pos);
};

// instantiate for compile test
template class packed_grid<3, int>;
template class dense_grid<3, int>;
template class sparse_grid<3, int>;
template class dense_extendible_grid<3, int>;
template class tree_extendible_grid<3, int>;
}  // namespace ext::grid

namespace ext::grid {
template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T>
std::expected<std::size_t, grid_error> packed_grid<Dim, T>::offset(std::array<std::uint32_t, Dim> pos) const {
    std::size_t index = 0;
    for (std::size_t i = 0; i < Dim; i++) {
        if (pos[i] >= m_dimensions[i]) return std::unexpected(grid_error::OutOfBounds);
        index *= m_dimensions[i];
        index += pos[i];
    }
    return index;
}
template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T>
packed_grid<Dim, T>::packed_grid(std::array<std::uint32_t, Dim> dimensions, T default_value)
    : m_dimensions(dimensions), m_default_value(std::move(default_value)) {
    std::size_t total_size = 1;
    for (const auto& dim : dimensions) {
        total_size *= dim;
    }
    m_cells.resize(total_size, m_default_value);
}

template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> dense_grid<Dim, T>::get_mut(std::array<std::uint32_t, Dim> pos) {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::ref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> dense_grid<Dim, T>::get(
    std::array<std::uint32_t, Dim> pos) const {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::cref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> dense_grid<Dim, T>::set(std::array<std::uint32_t, Dim> pos, Args&&... value) {
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
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> dense_grid<Dim, T>::set_new(std::array<std::uint32_t, Dim> pos, Args&&... value) {
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
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<void, grid_error> dense_grid<Dim, T>::remove(std::array<std::uint32_t, Dim> pos) {
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
std::expected<T, grid_error> dense_grid<Dim, T>::take(std::array<std::uint32_t, Dim> pos) {
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
auto sparse_grid<Dim, T>::iter_valid_indices() const {
    return std::views::iota(0u, m_positions.size()) | std::views::filter([this](std::size_t index) {
               return m_index_grid.get(m_positions[index]).value_or(npos) == index;
           });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> sparse_grid<Dim, T>::get_mut(std::array<std::uint32_t, Dim> pos) {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::ref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> sparse_grid<Dim, T>::get(
    std::array<std::uint32_t, Dim> pos) const {
    return m_index_grid.get(pos).and_then(
        [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
            if (index == npos) return std::unexpected(grid_error::EmptyCell);
            return std::cref(m_data[index]);
        });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> sparse_grid<Dim, T>::set(std::array<std::uint32_t, Dim> pos, Args&&... value) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index_ref = index_res.value();
    if (index_ref == npos) {
        // new cell
        std::size_t new_index;
        if (!m_recycled_indices.empty()) {
            new_index = m_recycled_indices.back();
            m_recycled_indices.pop_back();
            m_data[new_index] = T(std::forward<Args>(value)...);
        } else {
            m_data.emplace_back(std::forward<Args>(value)...);
            new_index = m_data.size() - 1;
        }
        m_positions.push_back(pos);
        index_ref = new_index;
    } else {
        // existing cell
        m_data[index_ref] = T(std::forward<Args>(value)...);
    }
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> sparse_grid<Dim, T>::set_new(std::array<std::uint32_t, Dim> pos, Args&&... value) {
    auto index_res = m_index_grid.get_mut(pos);
    if (!index_res.has_value()) return std::unexpected(index_res.error());
    std::size_t& index_ref = index_res.value();
    if (index_ref == npos) {
        // new cell
        std::size_t new_index;
        if (!m_recycled_indices.empty()) {
            new_index = m_recycled_indices.back();
            m_recycled_indices.pop_back();
            m_data[new_index] = T(std::forward<Args>(value)...);
        } else {
            m_data.emplace_back(std::forward<Args>(value)...);
            new_index = m_data.size() - 1;
        }
        m_positions.push_back(pos);
        index_ref = new_index;
    } else {
        return std::unexpected(grid_error::AlreadyOccupied);
    }
    return {};
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<void, grid_error> sparse_grid<Dim, T>::remove(std::array<std::uint32_t, Dim> pos) {
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
std::expected<T, grid_error> sparse_grid<Dim, T>::take(std::array<std::uint32_t, Dim> pos) {
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
std::expected<std::array<std::uint32_t, Dim>, grid_error> dense_extendible_grid<Dim, T>::relative_pos(
    std::array<std::int32_t, Dim> pos) const {
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
dense_extendible_grid<Dim, T>::dense_extendible_grid(std::array<std::uint32_t, Dim> dimensions)
    : m_index_grid(dimensions, npos) {
    m_origin.fill(0);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
bool dense_extendible_grid<Dim, T>::contains(std::array<std::int32_t, Dim> pos) const {
    return relative_pos(pos)
        .and_then([this](auto rel) { return m_index_grid.get(rel); })
        .transform(not_npos)
        .value_or(false);
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> dense_extendible_grid<Dim, T>::get_mut(
    std::array<std::int32_t, Dim> pos) {
    return relative_pos(pos).and_then([this](std::array<std::uint32_t, Dim> rel_pos) {
        return m_index_grid.get(rel_pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
                if (index == npos) return std::unexpected(grid_error::EmptyCell);
                return std::ref(m_data[index]);
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> dense_extendible_grid<Dim, T>::get(
    std::array<std::int32_t, Dim> pos) const {
    return relative_pos(pos).and_then([this](std::array<std::uint32_t, Dim> rel_pos) {
        return m_index_grid.get(rel_pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
                if (index == npos) return std::unexpected(grid_error::EmptyCell);
                return std::cref(m_data[index]);
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
void dense_extendible_grid<Dim, T>::extend(std::array<std::int32_t, Dim> new_min,
                                           std::array<std::int32_t, Dim> new_max) {
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
std::expected<void, grid_error> dense_extendible_grid<Dim, T>::set(std::array<std::int32_t, Dim> pos, Args&&... value) {
    extend(pos, pos);  // ensure position is in bounds, extending if necessary
    return relative_pos(pos).and_then([this, &pos, &value...](std::array<std::uint32_t, Dim> rel_pos) {
        return m_index_grid.get_mut(rel_pos).and_then(
            [this, &pos, &value...](std::size_t& index_ref) -> std::expected<void, grid_error> {
                if (index_ref == npos) {
                    // new cell
                    m_data.emplace_back(std::forward<Args>(value)...);
                    m_positions.push_back(pos);
                    index_ref = m_data.size() - 1;
                } else {
                    // existing cell
                    m_data[index_ref] = T(std::forward<Args>(value)...);
                }
                return {};
            });
    });
}
template <std::size_t Dim, typename T>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> dense_extendible_grid<Dim, T>::set_new(std::array<std::int32_t, Dim> pos,
                                                                       Args&&... value) {
    extend(pos, pos);  // ensure position is in bounds, extending if necessary
    return relative_pos(pos).and_then([this, &pos, &value...](std::array<std::uint32_t, Dim> rel_pos) {
        return m_index_grid.get_mut(rel_pos).and_then(
            [this, &pos, &value...](std::size_t& index_ref) -> std::expected<void, grid_error> {
                if (index_ref == npos) {
                    // new cell
                    m_data.emplace_back(std::forward<Args>(value)...);
                    m_positions.push_back(pos);
                    index_ref = m_data.size() - 1;
                } else {
                    // existing cell
                    return std::unexpected(grid_error::AlreadyOccupied);
                }
                return {};
            });
    });
}

template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_extendible_grid<Dim, T, ChildCount>::flat_child_index(std::array<std::uint32_t, Dim> pos,
                                                                       std::uint32_t stride) {
    std::size_t idx = 0;
    for (std::size_t i = 0; i < Dim; i++) {
        idx = idx * ChildCount + static_cast<std::size_t>((pos[i] / stride) % ChildCount);
    }
    return idx;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::size_t tree_extendible_grid<Dim, T, ChildCount>::alloc_node() {
    m_nodes.emplace_back();
    return m_nodes.size() - 1;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_extendible_grid<Dim, T, ChildCount>::compute_coverage() const {
    std::uint32_t c = 1;
    for (std::uint32_t i = 0; i < m_level; i++) c *= static_cast<std::uint32_t>(ChildCount);
    return c;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::uint32_t tree_extendible_grid<Dim, T, ChildCount>::required_level(std::array<std::uint32_t, Dim> pos) const {
    std::uint32_t max_coord = 0;
    for (auto p : pos) max_coord = std::max(max_coord, p);
    std::uint32_t level    = 0;
    std::uint32_t coverage = 1;
    while (coverage <= max_coord) {
        coverage *= static_cast<std::uint32_t>(ChildCount);
        level++;
    }
    return std::max(level, static_cast<std::uint32_t>(1));
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::ensure_level(std::uint32_t needed) {
    if (m_level == 0 && needed > 0) {
        m_root  = alloc_node();
        m_level = 1;
    }
    while (m_level < needed) {
        std::size_t new_root          = alloc_node();
        m_nodes[new_root].children[0] = m_root;
        m_root                        = new_root;
        m_level++;
    }
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
void tree_extendible_grid<Dim, T, ChildCount>::update_data_index(std::array<std::uint32_t, Dim> pos,
                                                                 std::size_t new_data_index) {
    std::size_t current  = m_root;
    std::uint32_t stride = compute_coverage() / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        current = m_nodes[current].children[flat_child_index(pos, stride)];
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    m_nodes[current].children[flat_child_index(pos, 1)] = new_data_index;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
bool tree_extendible_grid<Dim, T, ChildCount>::contains(std::array<std::uint32_t, Dim> pos) const {
    if (m_level == 0) return false;
    std::uint32_t cov = compute_coverage();
    for (auto p : pos)
        if (p >= cov) return false;
    std::size_t current  = m_root;
    std::uint32_t stride = cov / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        current = m_nodes[current].children[flat_child_index(pos, stride)];
        if (current == npos) return false;
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    return m_nodes[current].children[flat_child_index(pos, 1)] != npos;
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<T>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::get_mut(
    std::array<std::uint32_t, Dim> pos) {
    if (m_level == 0) return std::unexpected(grid_error::OutOfBounds);
    std::uint32_t cov = compute_coverage();
    for (auto p : pos)
        if (p >= cov) return std::unexpected(grid_error::OutOfBounds);
    std::size_t current  = m_root;
    std::uint32_t stride = cov / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        current = m_nodes[current].children[flat_child_index(pos, stride)];
        if (current == npos) return std::unexpected(grid_error::EmptyCell);
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    std::size_t data_idx = m_nodes[current].children[flat_child_index(pos, 1)];
    if (data_idx == npos) return std::unexpected(grid_error::EmptyCell);
    return std::ref(m_data[data_idx]);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<std::reference_wrapper<const T>, grid_error> tree_extendible_grid<Dim, T, ChildCount>::get(
    std::array<std::uint32_t, Dim> pos) const {
    if (m_level == 0) return std::unexpected(grid_error::OutOfBounds);
    std::uint32_t cov = compute_coverage();
    for (auto p : pos)
        if (p >= cov) return std::unexpected(grid_error::OutOfBounds);
    std::size_t current  = m_root;
    std::uint32_t stride = cov / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        current = m_nodes[current].children[flat_child_index(pos, stride)];
        if (current == npos) return std::unexpected(grid_error::EmptyCell);
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    std::size_t data_idx = m_nodes[current].children[flat_child_index(pos, 1)];
    if (data_idx == npos) return std::unexpected(grid_error::EmptyCell);
    return std::cref(m_data[data_idx]);
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> tree_extendible_grid<Dim, T, ChildCount>::set(std::array<std::uint32_t, Dim> pos,
                                                                              Args&&... value) {
    ensure_level(required_level(pos));
    std::size_t current  = m_root;
    std::uint32_t stride = compute_coverage() / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        std::size_t ci = flat_child_index(pos, stride);
        if (m_nodes[current].children[ci] == npos) {
            std::size_t new_node          = alloc_node();
            m_nodes[current].children[ci] = new_node;
        }
        current = m_nodes[current].children[ci];
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    std::size_t ci        = flat_child_index(pos, 1);
    std::size_t& data_idx = m_nodes[current].children[ci];
    if (data_idx == npos) {
        m_data.emplace_back(std::forward<Args>(value)...);
        m_positions.push_back(pos);
        data_idx = m_data.size() - 1;
    } else {
        m_data[data_idx] = T(std::forward<Args>(value)...);
    }
    return {};
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
template <typename... Args>
    requires std::constructible_from<T, Args...>
std::expected<void, grid_error> tree_extendible_grid<Dim, T, ChildCount>::set_new(std::array<std::uint32_t, Dim> pos,
                                                                                  Args&&... value) {
    ensure_level(required_level(pos));
    std::size_t current  = m_root;
    std::uint32_t stride = compute_coverage() / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        std::size_t ci = flat_child_index(pos, stride);
        if (m_nodes[current].children[ci] == npos) {
            std::size_t new_node          = alloc_node();
            m_nodes[current].children[ci] = new_node;
        }
        current = m_nodes[current].children[ci];
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    std::size_t ci        = flat_child_index(pos, 1);
    std::size_t& data_idx = m_nodes[current].children[ci];
    if (data_idx == npos) {
        m_data.emplace_back(std::forward<Args>(value)...);
        m_positions.push_back(pos);
        data_idx = m_data.size() - 1;
    } else {
        return std::unexpected(grid_error::AlreadyOccupied);
    }
    return {};
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<void, grid_error> tree_extendible_grid<Dim, T, ChildCount>::remove(std::array<std::uint32_t, Dim> pos) {
    if (m_level == 0) return std::unexpected(grid_error::OutOfBounds);
    std::uint32_t cov = compute_coverage();
    for (auto p : pos)
        if (p >= cov) return std::unexpected(grid_error::OutOfBounds);
    std::size_t current  = m_root;
    std::uint32_t stride = cov / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        current = m_nodes[current].children[flat_child_index(pos, stride)];
        if (current == npos) return std::unexpected(grid_error::EmptyCell);
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    std::size_t ci        = flat_child_index(pos, 1);
    std::size_t& data_idx = m_nodes[current].children[ci];
    if (data_idx == npos) return std::unexpected(grid_error::EmptyCell);
    std::size_t idx  = data_idx;
    std::size_t last = m_data.size() - 1;
    if (idx != last) {
        std::swap(m_data[idx], m_data[last]);
        std::swap(m_positions[idx], m_positions[last]);
        update_data_index(m_positions[idx], idx);
    }
    m_data.pop_back();
    m_positions.pop_back();
    data_idx = npos;
    return {};
}
template <std::size_t Dim, typename T, std::size_t ChildCount>
    requires std::movable<T>
std::expected<T, grid_error> tree_extendible_grid<Dim, T, ChildCount>::take(std::array<std::uint32_t, Dim> pos) {
    if (m_level == 0) return std::unexpected(grid_error::OutOfBounds);
    std::uint32_t cov = compute_coverage();
    for (auto p : pos)
        if (p >= cov) return std::unexpected(grid_error::OutOfBounds);
    std::size_t current  = m_root;
    std::uint32_t stride = cov / static_cast<std::uint32_t>(ChildCount);
    for (std::uint32_t l = 0; l + 1 < m_level; l++) {
        current = m_nodes[current].children[flat_child_index(pos, stride)];
        if (current == npos) return std::unexpected(grid_error::EmptyCell);
        stride /= static_cast<std::uint32_t>(ChildCount);
    }
    std::size_t ci        = flat_child_index(pos, 1);
    std::size_t& data_idx = m_nodes[current].children[ci];
    if (data_idx == npos) return std::unexpected(grid_error::EmptyCell);
    std::size_t idx  = data_idx;
    T value          = std::move(m_data[idx]);
    std::size_t last = m_data.size() - 1;
    if (idx != last) {
        std::swap(m_data[idx], m_data[last]);
        std::swap(m_positions[idx], m_positions[last]);
        update_data_index(m_positions[idx], idx);
    }
    m_data.pop_back();
    m_positions.pop_back();
    data_idx = npos;
    return value;
}
}  // namespace ext::grid