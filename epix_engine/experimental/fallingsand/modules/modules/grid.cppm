export module epix.experimental.grid;

import std;

import epix.meta;

namespace ext::grid {
/** @brief Error codes for chunk layer operations. */
export enum LayerError {
    UnsupportedType,
    OutOfBounds,
    EmptyCell,
    InvalidValue,
};
/** @brief Abstract base for a single layer of a Dim-dimensional chunk.
 *
 * Provides type-erased get/set access to grid cells. Each layer supports
 * one or more value types identified by meta::type_index.
 * @tparam Dim Number of spatial dimensions.
 */
template <std::size_t Dim>
export class ChunkLayer {
   public:
    virtual ~ChunkLayer() = default;

   private:
    virtual std::expected<void*, LayerError> get_mut(meta::type_index, std::array<std::uint32_t, Dim>)              = 0;
    virtual std::expected<const void*, LayerError> get(meta::type_index, std::array<std::uint32_t, Dim>) const      = 0;
    virtual std::expected<void, LayerError> set_copy(meta::type_index, std::array<std::uint32_t, Dim>, const void*) = 0;
    virtual std::expected<void, LayerError> set_move(meta::type_index, std::array<std::uint32_t, Dim>, void*)       = 0;

   public:
    virtual std::vector<meta::type_index> supported_types() const = 0;
    virtual bool supports_type(meta::type_index type) const       = 0;
    virtual std::size_t width_shift() const = 0;  // for bit shifting optimization, should be equal to log2(width())
    std::size_t width() const { return static_cast<std::size_t>(1) << width_shift(); }
    bool in_bounds(std::array<std::uint32_t, Dim> pos) const {
        for (std::size_t i = 0; i < Dim; i++) {
            if (pos[i] >= width()) return false;
        }
        return true;
    }
    template <typename T>
    std::expected<void, LayerError> set(std::array<std::uint32_t, Dim> pos, T&& value) {
        using type = std::remove_cvref_t<T>;
        if constexpr (std::is_rvalue_reference_v<T>) {
            return set_move(meta::type_id<type>(), pos, static_cast<void*>(std::addressof(value)));
        } else {
            return set_copy(meta::type_id<type>(), pos, static_cast<const void*>(std::addressof(value)));
        }
    }
    template <typename T>
    std::expected<std::reference_wrapper<T>, LayerError> get_mut(std::array<std::uint32_t, Dim> pos) {
        return get_mut(meta::type_id<std::remove_cvref_t<T>>(), pos).transform([](void* ptr) {
            return std::ref(*static_cast<T*>(ptr));
        });
    }
    template <typename T>
    std::expected<std::reference_wrapper<const T>, LayerError> get(std::array<std::uint32_t, Dim> pos) const {
        return get(meta::type_id<std::remove_cvref_t<T>>(), pos).transform([](const void* ptr) {
            return std::cref(*static_cast<const T*>(ptr));
        });
    }
};
/** @brief Error codes for chunk-level layer management. */
export enum class ChunkLayerError {
    TypeAlreadyExists,
    WidthMismatch,
    LayerMissing,
};
/** @brief Composite chunk that delegates cell access to typed sub-layers.
 *
 * A Chunk owns multiple ChunkLayer instances and routes get/set calls
 * to the appropriate layer based on value type.
 * @tparam Dim Number of spatial dimensions.
 */
template <std::size_t Dim>
export class Chunk : public ChunkLayer<Dim> {
   private:
    std::size_t m_width_shift;
    std::vector<std::unique_ptr<ChunkLayer<Dim>>> m_layers;
    std::unordered_map<meta::type_index, std::size_t> m_type_to_layer;

    std::expected<void*, LayerError> get_mut(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->get_mut(type, pos);
    }
    std::expected<const void*, LayerError> get(meta::type_index type,
                                               std::array<std::uint32_t, Dim> pos) const override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->get(type, pos);
    }
    std::expected<void, LayerError> set_copy(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             const void* value) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->set_copy(type, pos, value);
    }
    std::expected<void, LayerError> set_move(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             void* value) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->set_move(type, pos, value);
    }

   public:
    Chunk(std::size_t width_shift) : m_width_shift(width_shift) {}

    std::size_t width_shift() const override { return m_width_shift; }
    bool supports_type(meta::type_index type) const override { return m_type_to_layer.contains(type); }
    std::vector<meta::type_index> supported_types() const override {
        return m_type_to_layer | std::views::keys | std::ranges::to<std::vector>();
    }

    std::expected<void, ChunkLayerError> add_layer(std::unique_ptr<ChunkLayer<Dim>> layer) {
        if (layer->width_shift() != m_width_shift) return std::unexpected(ChunkLayerError::WidthMismatch);
        for (const auto& type : layer->supported_types()) {
            if (m_type_to_layer.contains(type)) return std::unexpected(ChunkLayerError::TypeAlreadyExists);
        }
        for (const auto& type : layer->supported_types()) {
            m_type_to_layer[type] = m_layers.size();
        }
        m_layers.push_back(std::move(layer));
        return {};
    }
    std::expected<std::unique_ptr<ChunkLayer<Dim>>, ChunkLayerError> remove_layer(std::size_t index) {
        if (index >= m_layers.size()) return std::unexpected(ChunkLayerError::LayerMissing);
        auto layer = std::move(m_layers[index]);
        for (const auto& type : layer->supported_types()) {
            m_type_to_layer.erase(type);
        }
        m_layers.erase(m_layers.begin() + index);
        return std::move(layer);
    }
    std::expected<std::unique_ptr<ChunkLayer<Dim>>, ChunkLayerError> remove_layer_by_type(meta::type_index type) {
        if (!m_type_to_layer.contains(type)) return std::unexpected(ChunkLayerError::LayerMissing);
        return remove_layer(m_type_to_layer.at(type));
    }
    std::expected<std::reference_wrapper<ChunkLayer<Dim>>, ChunkLayerError> get_layer_mut_by_type(
        meta::type_index type) {
        if (!m_type_to_layer.contains(type)) return std::unexpected(ChunkLayerError::LayerMissing);
        return std::ref(*m_layers[m_type_to_layer.at(type)]);
    }
    std::expected<std::reference_wrapper<const ChunkLayer<Dim>>, ChunkLayerError> get_layer_by_type(
        meta::type_index type) const {
        if (!m_type_to_layer.contains(type)) return std::unexpected(ChunkLayerError::LayerMissing);
        return std::cref(*m_layers.at(m_type_to_layer.at(type)));
    }
};
export enum class grid_error {
    OutOfBounds,
    InvalidPos,
    EmptyCell,
    AlreadyOccupied,
};
export template <std::size_t Dim, typename T>
    requires std::constructible_from<T> && std::movable<T>
struct packed_grid {
   private:
    std::vector<T> m_cells;
    std::array<std::uint32_t, Dim> m_dimensions;
    T m_default_value;

    std::expected<std::size_t, grid_error> offset(std::array<std::uint32_t, Dim> pos) const {
        std::size_t index = 0;
        for (std::size_t i = 0; i < Dim; i++) {
            if (pos[i] >= m_dimensions[i]) return std::unexpected(grid_error::OutOfBounds);
            index *= m_dimensions[i];
            index += pos[i];
        }
        return index;
    }

   public:
    packed_grid(std::array<std::uint32_t, Dim> dimensions, T default_value = T{})
        : m_dimensions(dimensions), m_default_value(std::move(default_value)) {
        std::size_t total_size = 1;
        for (const auto& dim : dimensions) {
            total_size *= dim;
        }
        m_cells.resize(total_size, m_default_value);
    }
    void set_default(T value) { m_default_value = std::move(value); }
    bool in_bounds(std::array<std::uint32_t, Dim> pos) const {
        for (std::size_t i = 0; i < Dim; i++) {
            if (pos[i] >= m_dimensions[i]) return false;
        }
        return true;
    }
    std::array<std::uint32_t, Dim> dimensions() const { return m_dimensions; }
    std::size_t dimension(std::size_t index) const { return m_dimensions[index]; }
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos) {
        return offset(pos).transform([this](std::size_t index) { return std::ref(m_cells[index]); });
    }
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const {
        return offset(pos).transform([this](std::size_t index) { return std::cref(m_cells[index]); });
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        return offset(pos).transform([&](std::size_t index) { m_cells[index] = T(std::forward<Args>(value)...); });
    }
    std::expected<void, grid_error> reset(std::array<std::uint32_t, Dim> pos) {
        return offset(pos).transform([&](std::size_t index) { m_cells[index] = m_default_value; });
    }
};
export template <std::size_t Dim, typename T>
    requires std::movable<T>
struct dense_grid {
   private:
    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;  // positions of each cell in m_data
    packed_grid<Dim, std::size_t> m_index_grid;               // stores indices into m_data, or -1 for empty

   public:
    dense_grid(std::array<std::uint32_t, Dim> dimensions) : m_index_grid(dimensions, static_cast<std::size_t>(-1)) {}
    bool in_bounds(std::array<std::uint32_t, Dim> pos) const { return m_index_grid.in_bounds(pos); }
    bool contains(std::array<std::uint32_t, Dim> pos) const {
        return m_index_grid.get(pos)
            .transform([](std::size_t i) { return i != static_cast<std::size_t>(-1); })
            .value_or(false);
    }
    std::array<std::uint32_t, Dim> dimensions() const { return m_index_grid.dimensions(); }
    std::size_t dimension(std::size_t index) const { return m_index_grid.dimension(index); }
    auto iter_pos() const { return m_positions | std::views::all; }
    auto iter_cells() const { return m_data | std::views::all; }
    auto iter_cells_mut() { return m_data | std::views::all; }
    auto iter() const { return std::views::zip(m_positions, m_data); }
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos) {
        return m_index_grid.get(pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
                if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
                return std::ref(m_data[index]);
            });
    }
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const {
        return m_index_grid.get(pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
                if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
                return std::cref(m_data[index]);
            });
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index_ref = index_res.value();
        if (index_ref == static_cast<std::size_t>(-1)) {
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
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index_ref = index_res.value();
        if (index_ref == static_cast<std::size_t>(-1)) {
            // new cell
            m_data.emplace_back(std::forward<Args>(value)...);
            m_positions.push_back(pos);
            index_ref = m_data.size() - 1;
        } else {
            return std::unexpected(grid_error::AlreadyOccupied);
        }
        return {};
    }
    std::expected<void, grid_error> remove(std::array<std::uint32_t, Dim> pos) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index = index_res.value();
        if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
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
        index = static_cast<std::size_t>(-1);
        return {};
    }
    std::expected<T, grid_error> take(std::array<std::uint32_t, Dim> pos) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index = index_res.value();
        if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
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
        index = static_cast<std::size_t>(-1);
        return value;
    }
};
export template <std::size_t Dim>
struct sparse_grid {
   private:
    std::vector<T> m_data;
    std::vector<std::array<std::uint32_t, Dim>> m_positions;  // positions of each cell in m_data
    std::vector<std::size_t> m_recycled_indices;              // indices in m_data that are free to use
    packed_grid<Dim, std::size_t> m_index_grid;               // stores indices into m_data, or -1 for empty

    auto iter_valid_indices() const {
        return std::views::iota(0u, m_positions.size()) | std::views::filter([this](std::size_t index) {
                   return m_index_grid.get(m_positions[index]).value_or(static_cast<std::size_t>(-1)) == index;
               });
    }

   public:
    sparse_grid(std::array<std::uint32_t, Dim> dimensions) : m_index_grid(dimensions, static_cast<std::size_t>(-1)) {}
    bool in_bounds(std::array<std::uint32_t, Dim> pos) const { return m_index_grid.in_bounds(pos); }
    bool contains(std::array<std::uint32_t, Dim> pos) const {
        return m_index_grid.get(pos)
            .transform([](std::size_t i) { return i != static_cast<std::size_t>(-1); })
            .value_or(false);
    }
    std::array<std::uint32_t, Dim> dimensions() const { return m_index_grid.dimensions(); }
    std::size_t dimension(std::size_t index) const { return m_index_grid.dimension(index); }
    auto iter_pos() const {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) -> auto& { return m_positions[index]; });
    }
    auto iter_cells() const {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) -> auto& { return m_data[index]; });
    }
    auto iter_cells_mut() {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) -> auto& { return m_data[index]; });
    }
    auto iter() const {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) { return std::tie(m_positions[index], m_data[index]); });
    }
    auto iter_mut() {
        return iter_valid_indices() |
               std::views::transform([this](std::size_t index) { return std::tie(m_positions[index], m_data[index]); });
    }
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::uint32_t, Dim> pos) {
        return m_index_grid.get(pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
                if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
                return std::ref(m_data[index]);
            });
    }
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::uint32_t, Dim> pos) const {
        return m_index_grid.get(pos).and_then(
            [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
                if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
                return std::cref(m_data[index]);
            });
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index_ref = index_res.value();
        if (index_ref == static_cast<std::size_t>(-1)) {
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
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index_ref = index_res.value();
        if (index_ref == static_cast<std::size_t>(-1)) {
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
    std::expected<void, grid_error> remove(std::array<std::uint32_t, Dim> pos) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index = index_res.value();
        if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
        // mark index as recycled
        m_recycled_indices.push_back(index);
        // data, pos invalid, but don't need destruct or reset
        index = static_cast<std::size_t>(-1);
        return {};
    }
    std::expected<T, grid_error> take(std::array<std::uint32_t, Dim> pos) {
        auto index_res = m_index_grid.get_mut(pos);
        if (!index_res.has_value()) return std::unexpected(index_res.error());
        std::size_t& index = index_res.value();
        if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
        T value = std::move(m_data[index]);
        // mark index as recycled
        m_recycled_indices.push_back(index);
        // data, pos invalid, but don't need destruct or reset
        index = static_cast<std::size_t>(-1);
        return value;
    }
};
export template <std::size_t Dim, typename T>
    requires std::movable<T>
struct dense_extendible_grid {
   private:
    std::vector<T> m_data;
    std::vector<std::array<std::int32_t, Dim>> m_positions;  // positions of each cell in m_data, can be negative
    packed_grid<Dim, std::size_t> m_index_grid;              // stores indices into m_data, or -1 for empty
    std::array<std::int32_t, Dim> m_origin;

    std::expected<std::array<std::uint32_t, Dim>, grid_error> relative_pos(std::array<std::int32_t, Dim> pos) const {
        std::array<std::uint32_t, Dim> rel_pos;
        for (std::size_t i = 0; i < Dim; i++) {
            std::int32_t rel = pos[i] - m_origin[i];
            if (rel < 0) return std::unexpected(grid_error::OutOfBounds);
            rel_pos[i] = static_cast<std::uint32_t>(rel);
        }
        return rel_pos;
    }

   public:
    dense_extendible_grid(std::array<std::uint32_t, Dim> dimensions)
        : m_index_grid(dimensions, static_cast<std::size_t>(-1)) {
        m_origin.fill(0);
    }
    bool in_bounds(std::array<std::int32_t, Dim> pos) const {
        auto rel_pos_res = relative_pos(pos);
        return rel_pos_res.has_value() && m_index_grid.in_bounds(rel_pos_res.value());
    }
    bool contains(std::array<std::int32_t, Dim> pos) const {
        return relative_pos(pos)
            .and_then([this](auto rel) { return m_index_grid.get(rel); })
            .transform([](std::size_t i) { return i != static_cast<std::size_t>(-1); })
            .value_or(false);
    }
    std::array<std::uint32_t, Dim> dimensions() const { return m_index_grid.dimensions(); }
    std::size_t dimension(std::size_t index) const { return m_index_grid.dimension(index); }
    auto iter_pos() const { return m_positions | std::views::all; }
    auto iter_cells() const { return m_data | std::views::all; }
    auto iter_cells_mut() { return m_data | std::views::all; }
    auto iter() const { return std::views::zip(m_positions, m_data); }
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::int32_t, Dim> pos) {
        return relative_pos(pos).and_then([this](std::array<std::uint32_t, Dim> rel_pos) {
            return m_index_grid.get(rel_pos).and_then(
                [this](std::size_t index) -> std::expected<std::reference_wrapper<T>, grid_error> {
                    if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
                    return std::ref(m_data[index]);
                });
        });
    }
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::int32_t, Dim> pos) const {
        return relative_pos(pos).and_then([this](std::array<std::uint32_t, Dim> rel_pos) {
            return m_index_grid.get(rel_pos).and_then(
                [this](std::size_t index) -> std::expected<std::reference_wrapper<const T>, grid_error> {
                    if (index == static_cast<std::size_t>(-1)) return std::unexpected(grid_error::EmptyCell);
                    return std::cref(m_data[index]);
                });
        });
    }
    void extend(std::array<std::int32_t, Dim> new_min, std::array<std::int32_t, Dim> new_max) {
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
        packed_grid<Dim, std::size_t> new_grid(new_dims, static_cast<std::size_t>(-1));
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
    void shrink() {
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
        packed_grid<Dim, std::size_t> new_grid(new_dims, static_cast<std::size_t>(-1));
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
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::int32_t, Dim> pos, Args&&... value) {
        extend(pos, pos);  // ensure position is in bounds, extending if necessary
        return relative_pos(pos).and_then([this, &value...](std::array<std::uint32_t, Dim> rel_pos) {
            return m_index_grid.get_mut(rel_pos).and_then(
                [this, &value...](std::size_t& index_ref) -> std::expected<void, grid_error> {
                    if (index_ref == static_cast<std::size_t>(-1)) {
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
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::int32_t, Dim> pos, Args&&... value) {
        extend(pos, pos);  // ensure position is in bounds, extending if necessary
        return relative_pos(pos).and_then([this, &value...](std::array<std::uint32_t, Dim> rel_pos) {
            return m_index_grid.get_mut(rel_pos).and_then(
                [this, &value...](std::size_t& index_ref) -> std::expected<void, grid_error> {
                    if (index_ref == static_cast<std::size_t>(-1)) {
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
};
// dim, type, child count each axis
export template <std::size_t Dim, typename T, std::size_t ChildCount = 2>
    requires std::movable<T>
struct tree_extendible_grid {
   private:
    static constexpr std::size_t children_per_node = []() consteval {
        std::size_t result = 1;
        for (std::size_t i = 0; i < Dim; i++) result *= ChildCount;
        return result;
    }();
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    struct Node {
        std::array<std::size_t, children_per_node> children;
        Node() { children.fill(npos); }
    };

    std::vector<T> m_data;
    std::vector<std::array<std::int32_t, Dim>> m_positions;
    std::vector<Node> m_nodes;
    std::size_t m_root  = npos;
    std::size_t m_depth = 0;
    std::array<std::int32_t, Dim> m_root_origin{};

    static std::int32_t level_width(std::size_t level) {
        std::int32_t w = 1;
        for (std::size_t i = 0; i < level; i++) w *= static_cast<std::int32_t>(ChildCount);
        return w;
    }

    std::int32_t root_width() const { return level_width(m_depth); }

    bool contains(std::array<std::int32_t, Dim> pos) const {
        if (m_root == npos) return false;
        std::int32_t w = root_width();
        for (std::size_t i = 0; i < Dim; i++) {
            if (pos[i] < m_root_origin[i] || pos[i] >= m_root_origin[i] + w) return false;
        }
        return true;
    }

    void grow_to_contain(std::array<std::int32_t, Dim> pos) {
        if (m_root == npos) {
            m_root_origin = pos;
            m_depth       = 1;
            m_nodes.emplace_back();
            m_root = m_nodes.size() - 1;
            return;
        }
        while (!contains(pos)) {
            std::int32_t old_width = root_width();
            std::array<std::size_t, Dim> old_child_idx;
            std::array<std::int32_t, Dim> new_origin;
            for (std::size_t i = 0; i < Dim; i++) {
                old_child_idx[i] = (pos[i] < m_root_origin[i]) ? ChildCount - 1 : 0;
                new_origin[i]    = m_root_origin[i] - static_cast<std::int32_t>(old_child_idx[i]) * old_width;
            }
            std::size_t flat = 0;
            for (std::size_t i = 0; i < Dim; i++) {
                flat = flat * ChildCount + old_child_idx[i];
            }
            m_nodes.emplace_back();
            std::size_t new_root             = m_nodes.size() - 1;
            m_nodes[new_root].children[flat] = m_root;
            m_root                           = new_root;
            m_root_origin                    = new_origin;
            m_depth++;
        }
    }

    std::size_t find_data_index(std::array<std::int32_t, Dim> pos) const {
        if (!contains(pos)) return npos;
        std::size_t node                     = m_root;
        std::array<std::int32_t, Dim> origin = m_root_origin;
        for (std::size_t level = m_depth; level > 1; level--) {
            std::int32_t child_w = level_width(level - 1);
            std::size_t flat     = 0;
            for (std::size_t i = 0; i < Dim; i++) {
                std::size_t ci = static_cast<std::size_t>((pos[i] - origin[i]) / child_w);
                flat           = flat * ChildCount + ci;
                origin[i] += static_cast<std::int32_t>(ci) * child_w;
            }
            node = m_nodes[node].children[flat];
            if (node == npos) return npos;
        }
        std::size_t flat = 0;
        for (std::size_t i = 0; i < Dim; i++) {
            std::size_t ci = static_cast<std::size_t>(pos[i] - origin[i]);
            flat           = flat * ChildCount + ci;
        }
        return m_nodes[node].children[flat];
    }

    std::size_t* find_leaf_slot(std::array<std::int32_t, Dim> pos) {
        if (!contains(pos)) return nullptr;
        std::size_t node                     = m_root;
        std::array<std::int32_t, Dim> origin = m_root_origin;
        for (std::size_t level = m_depth; level > 1; level--) {
            std::int32_t child_w = level_width(level - 1);
            std::size_t flat     = 0;
            for (std::size_t i = 0; i < Dim; i++) {
                std::size_t ci = static_cast<std::size_t>((pos[i] - origin[i]) / child_w);
                flat           = flat * ChildCount + ci;
                origin[i] += static_cast<std::int32_t>(ci) * child_w;
            }
            if (m_nodes[node].children[flat] == npos) return nullptr;
            node = m_nodes[node].children[flat];
        }
        std::size_t flat = 0;
        for (std::size_t i = 0; i < Dim; i++) {
            std::size_t ci = static_cast<std::size_t>(pos[i] - origin[i]);
            flat           = flat * ChildCount + ci;
        }
        return &m_nodes[node].children[flat];
    }

    std::size_t& ensure_leaf_slot(std::array<std::int32_t, Dim> pos) {
        std::size_t node                     = m_root;
        std::array<std::int32_t, Dim> origin = m_root_origin;
        for (std::size_t level = m_depth; level > 1; level--) {
            std::int32_t child_w = level_width(level - 1);
            std::size_t flat     = 0;
            for (std::size_t i = 0; i < Dim; i++) {
                std::size_t ci = static_cast<std::size_t>((pos[i] - origin[i]) / child_w);
                flat           = flat * ChildCount + ci;
                origin[i] += static_cast<std::int32_t>(ci) * child_w;
            }
            if (m_nodes[node].children[flat] == npos) {
                m_nodes.emplace_back();
                m_nodes[node].children[flat] = m_nodes.size() - 1;
            }
            node = m_nodes[node].children[flat];
        }
        std::size_t flat = 0;
        for (std::size_t i = 0; i < Dim; i++) {
            std::size_t ci = static_cast<std::size_t>(pos[i] - origin[i]);
            flat           = flat * ChildCount + ci;
        }
        return m_nodes[node].children[flat];
    }

   public:
    tree_extendible_grid() = default;
    bool in_bounds(std::array<std::int32_t, Dim> pos) const { return contains(pos); }
    bool contains(std::array<std::int32_t, Dim> pos) const { return find_data_index(pos) != npos; }
    auto iter_pos() const { return m_positions | std::views::all; }
    auto iter_cells() const { return m_data | std::views::all; }
    auto iter_cells_mut() { return m_data | std::views::all; }
    auto iter() const { return std::views::zip(m_positions, m_data); }
    auto iter_mut() { return std::views::zip(m_positions, m_data); }
    std::expected<std::reference_wrapper<T>, grid_error> get_mut(std::array<std::int32_t, Dim> pos) {
        std::size_t idx = find_data_index(pos);
        if (idx == npos) return std::unexpected(grid_error::EmptyCell);
        return std::ref(m_data[idx]);
    }
    std::expected<std::reference_wrapper<const T>, grid_error> get(std::array<std::int32_t, Dim> pos) const {
        std::size_t idx = find_data_index(pos);
        if (idx == npos) return std::unexpected(grid_error::EmptyCell);
        return std::cref(m_data[idx]);
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set(std::array<std::int32_t, Dim> pos, Args&&... value) {
        grow_to_contain(pos);
        std::size_t& slot = ensure_leaf_slot(pos);
        if (slot == npos) {
            m_data.emplace_back(std::forward<Args>(value)...);
            m_positions.push_back(pos);
            slot = m_data.size() - 1;
        } else {
            m_data[slot] = T(std::forward<Args>(value)...);
        }
        return {};
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    std::expected<void, grid_error> set_new(std::array<std::int32_t, Dim> pos, Args&&... value) {
        grow_to_contain(pos);
        std::size_t& slot = ensure_leaf_slot(pos);
        if (slot == npos) {
            m_data.emplace_back(std::forward<Args>(value)...);
            m_positions.push_back(pos);
            slot = m_data.size() - 1;
        } else {
            return std::unexpected(grid_error::AlreadyOccupied);
        }
        return {};
    }
    std::expected<void, grid_error> remove(std::array<std::int32_t, Dim> pos) {
        std::size_t* slot_ptr = find_leaf_slot(pos);
        if (!slot_ptr || *slot_ptr == npos) return std::unexpected(grid_error::EmptyCell);
        std::size_t index      = *slot_ptr;
        *slot_ptr              = npos;
        std::size_t last_index = m_data.size() - 1;
        if (index != last_index) {
            m_data[index]           = std::move(m_data[last_index]);
            m_positions[index]      = m_positions[last_index];
            std::size_t* moved_slot = find_leaf_slot(m_positions[index]);
            *moved_slot             = index;
        }
        m_data.pop_back();
        m_positions.pop_back();
        return {};
    }
    std::expected<T, grid_error> take(std::array<std::int32_t, Dim> pos) {
        std::size_t* slot_ptr = find_leaf_slot(pos);
        if (!slot_ptr || *slot_ptr == npos) return std::unexpected(grid_error::EmptyCell);
        std::size_t index      = *slot_ptr;
        *slot_ptr              = npos;
        T value                = std::move(m_data[index]);
        std::size_t last_index = m_data.size() - 1;
        if (index != last_index) {
            m_data[index]           = std::move(m_data[last_index]);
            m_positions[index]      = m_positions[last_index];
            std::size_t* moved_slot = find_leaf_slot(m_positions[index]);
            *moved_slot             = index;
        }
        m_data.pop_back();
        m_positions.pop_back();
        return value;
    }
    void shrink() {
        if (m_data.empty()) {
            m_nodes.clear();
            m_root        = npos;
            m_depth       = 0;
            m_root_origin = {};
            return;
        }
        // compute bounding box of all occupied positions
        std::array<std::int32_t, Dim> bb_min = m_positions[0], bb_max = m_positions[0];
        for (const auto& pos : m_positions) {
            for (std::size_t i = 0; i < Dim; i++) {
                bb_min[i] = std::min(bb_min[i], pos[i]);
                bb_max[i] = std::max(bb_max[i], pos[i]);
            }
        }
        // peel off root levels while all data fits in a single child
        while (m_depth > 1) {
            std::int32_t child_w = level_width(m_depth - 1);
            bool single_child    = true;
            std::size_t flat     = 0;
            std::array<std::int32_t, Dim> new_origin;
            for (std::size_t i = 0; i < Dim; i++) {
                std::size_t ci_min = static_cast<std::size_t>((bb_min[i] - m_root_origin[i]) / child_w);
                std::size_t ci_max = static_cast<std::size_t>((bb_max[i] - m_root_origin[i]) / child_w);
                if (ci_min != ci_max) {
                    single_child = false;
                    break;
                }
                flat          = flat * ChildCount + ci_min;
                new_origin[i] = m_root_origin[i] + static_cast<std::int32_t>(ci_min) * child_w;
            }
            if (!single_child) break;
            std::size_t child = m_nodes[m_root].children[flat];
            if (child == npos) break;
            m_root        = child;
            m_root_origin = new_origin;
            m_depth--;
        }
    }
};
export template <std::size_t Dim>
struct ExtendibleGrid {
   private:
    std::size_t m_chunk_width_shift;
    std::vector<Chunk<Dim>> m_chunks;
};
}  // namespace ext::grid