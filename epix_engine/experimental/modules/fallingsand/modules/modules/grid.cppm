export module epix.experimental.grid;

import std;

import epix.meta;
import epix.utils;

import epix.experimental.basic_grid;

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
export template <std::size_t Dim>
class ChunkLayer {
   public:
    virtual ~ChunkLayer() = default;

   private:
    virtual std::expected<void*, LayerError> get_mut(meta::type_index, std::array<std::uint32_t, Dim>)              = 0;
    virtual std::expected<const void*, LayerError> get(meta::type_index, std::array<std::uint32_t, Dim>) const      = 0;
    virtual std::expected<void, LayerError> set_copy(meta::type_index, std::array<std::uint32_t, Dim>, const void*) = 0;
    virtual std::expected<void, LayerError> set_move(meta::type_index, std::array<std::uint32_t, Dim>, void*)       = 0;
    /// might not actually remove the cell based on the implementation, might be reset.
    virtual std::expected<void, LayerError> remove(meta::type_index, std::array<std::uint32_t, Dim>) = 0;
    virtual std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, const void*>>, LayerError>
        try_iter_type(meta::type_index) const = 0;
    virtual std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, void*>>, LayerError>
        try_iter_type_mut(meta::type_index) = 0;

    template <std::size_t D>
    friend class Chunk;

   public:
    virtual std::vector<meta::type_index> supported_types() const = 0;
    virtual bool supports_type(meta::type_index type) const       = 0;
    virtual void clear()                                          = 0;
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
    template <typename... Args>
    std::expected<void, LayerError> set_multi(std::array<std::uint32_t, Dim> pos, Args&&... value) {
        std::expected<void, LayerError> result = std::unexpected(LayerError::UnsupportedType);
        ((result = set(pos, std::forward<Args>(value))), ...);
        return result;
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
    template <typename T>
    std::expected<void, LayerError> remove(std::array<std::uint32_t, Dim> pos) {
        return remove(meta::type_id<std::remove_cvref_t<T>>(), pos);
    }
    std::expected<void, LayerError> remove_all(std::array<std::uint32_t, Dim> pos) {
        std::expected<void, LayerError> result = std::unexpected(LayerError::UnsupportedType);
        for (const auto& type : supported_types()) {
            auto res = remove(type, pos);
            if (res.has_value()) result = res;
        }
        return result;
    }

    template <typename T>
    auto try_iter() const {
        return try_iter_type(meta::type_id<std::remove_cvref_t<T>>()).transform([](auto iterable) {
            return iterable | std::views::transform([](auto&& pair) {
                       return std::pair<std::array<std::uint32_t, Dim>, const T&>(std::move(pair.first),
                                                                                  *static_cast<const T*>(pair.second));
                   });
        });
    }
    template <typename T>
    auto iter() const {
        return try_iter<T>().value();
    }
    template <typename T>
    auto try_iter_mut() {
        return try_iter_type_mut(meta::type_id<std::remove_cvref_t<T>>()).transform([](auto iterable) {
            return iterable | std::views::transform([](auto&& pair) {
                       return std::pair<std::array<std::uint32_t, Dim>, T&>(std::move(pair.first),
                                                                            *static_cast<T*>(pair.second));
                   });
        });
    }
    template <typename T>
    auto iter_mut() {
        return try_iter_mut<T>().value();
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
export template <std::size_t Dim>
class Chunk : public ChunkLayer<Dim> {
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
    std::expected<void, LayerError> remove(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->remove(type, pos);
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, const void*>>, LayerError>
    try_iter_type(meta::type_index type) const override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->try_iter_type(type);
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, void*>>, LayerError>
    try_iter_type_mut(meta::type_index type) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->try_iter_type_mut(type);
    }

   public:
    Chunk(std::size_t width_shift) : m_width_shift(width_shift) {}
    Chunk(const Chunk&)            = delete;
    Chunk(Chunk&&)                 = default;
    Chunk& operator=(const Chunk&) = delete;
    Chunk& operator=(Chunk&&)      = default;

    std::size_t width_shift() const override { return m_width_shift; }
    bool supports_type(meta::type_index type) const override { return m_type_to_layer.contains(type); }
    std::vector<meta::type_index> supported_types() const override {
        return m_type_to_layer | std::views::keys | std::ranges::to<std::vector>();
    }
    void clear() override {
        for (auto& layer : m_layers) layer->clear();
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

export using ChunkGridError = std::variant<ChunkLayerError, LayerError, grid_error>;

// this won't work if using function instead of lambda, don't know why
constexpr auto map_err = [](auto&& error) -> ChunkGridError {
    return ChunkGridError{std::forward<decltype(error)>(error)};
};

export template <std::size_t Dim>
struct ExtendibleChunkGrid {
   private:
    tree_extendible_grid<Dim, Chunk<Dim>> m_chunk_grid;
    std::size_t m_chunk_width_shift;

    auto chunk_coords(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::tuple<std::array<std::int32_t, Dim>, std::array<std::uint32_t, Dim>>, ChunkGridError> {
        std::tuple<std::array<std::int32_t, Dim>, std::array<std::uint32_t, Dim>> result;
        // if coords don't fit in int32 with width shift, return error, negative allowd
        for (std::size_t i = 0; i < Dim; i++) {
            std::int64_t chunk_coord = pos[i] >> m_chunk_width_shift;
            if (chunk_coord < std::numeric_limits<std::int32_t>::min() ||
                chunk_coord > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected(grid_error::OutOfBounds);
            }
            std::get<0>(result)[i] = static_cast<std::int32_t>(chunk_coord);
            std::get<1>(result)[i] = static_cast<std::uint32_t>(pos[i] & ((1 << m_chunk_width_shift) - 1));
        }
        return result;
    }

   public:
    ExtendibleChunkGrid(std::size_t chunk_width_shift) : m_chunk_width_shift(chunk_width_shift) {}
    ExtendibleChunkGrid(const ExtendibleChunkGrid&)            = delete;
    ExtendibleChunkGrid(ExtendibleChunkGrid&&)                 = default;
    ExtendibleChunkGrid& operator=(const ExtendibleChunkGrid&) = delete;
    ExtendibleChunkGrid& operator=(ExtendibleChunkGrid&&)      = default;

    std::size_t chunk_width_shift() const { return m_chunk_width_shift; }
    std::size_t chunk_width() const { return static_cast<std::size_t>(1) << m_chunk_width_shift; }

    auto get_chunk(std::array<std::int32_t, Dim> pos) const { return m_chunk_grid.get(pos); }
    auto get_chunk_mut(std::array<std::int32_t, Dim> pos) { return m_chunk_grid.get_mut(pos); }

    void shrink_grid() { m_chunk_grid.shrink(); }

    auto add_chunk(std::array<std::int32_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        auto result = m_chunk_grid.set_new(pos, chunk_width_shift());
        if (!result.has_value()) return std::unexpected(ChunkGridError{result.error()});
        return {};
    }
    auto insert_chunk(std::array<std::int32_t, Dim> pos, Chunk<Dim>&& chunk) -> std::expected<void, ChunkGridError> {
        if (chunk.width_shift() != m_chunk_width_shift) return std::unexpected(ChunkLayerError::WidthMismatch);
        auto result = m_chunk_grid.set_new(pos, std::move(chunk));
        if (!result.has_value()) return std::unexpected(ChunkGridError{result.error()});
        return {};
    }

    auto remove_chunk(std::array<std::int32_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        return m_chunk_grid.remove(pos);
    }
    auto take_chunk(std::array<std::int32_t, Dim> pos) -> std::expected<Chunk<Dim>, ChunkGridError> {
        return m_chunk_grid.take(pos);
    }
    auto iter_chunks() const { return m_chunk_grid.iter_cells(); }
    auto iter_chunks_mut() { return m_chunk_grid.iter_cells_mut(); }
    auto iter_chunk_pos() const { return m_chunk_grid.iter_pos(); }
    auto iter_chunk_with_pos() const { return m_chunk_grid.iter(); }
    auto iter_chunk_with_pos_mut() { return m_chunk_grid.iter_mut(); }

    template <typename T>
    auto insert_cell(std::array<std::int64_t, Dim> pos, T&& value) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        return m_chunk_grid.get_mut(chunk_pos).transform_error(map_err).and_then(
            [&cell_pos, &value](Chunk<Dim>& chunk) -> std::expected<void, ChunkGridError> {
                return chunk.set(cell_pos, std::forward<T>(value));
            });
    }
    template <typename... Args>
    auto insert_cell_multi(std::array<std::int64_t, Dim> pos, Args&&... value) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        return m_chunk_grid.get_mut(chunk_pos).transform_error(map_err).and_then(
            [&cell_pos, &value...](Chunk<Dim>& chunk) -> std::expected<void, ChunkGridError> {
                return chunk.set_multi(cell_pos, std::forward<Args>(value)...);
            });
    }
    template <typename T>
    auto get_cell(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::reference_wrapper<const T>, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = m_chunk_grid.get(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        const ChunkLayer<Dim>& layer = chunk_result.value().get();
        return layer.template get<T>(cell_pos).transform_error(map_err);
    }
    template <typename T>
    auto get_cell_mut(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::reference_wrapper<const T>, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = m_chunk_grid.get_mut(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        const ChunkLayer<Dim>& layer = chunk_result.value().get();
        return layer.template get_mut<T>(cell_pos).transform_error(map_err);
    }
    template <typename T>
    auto remove_cell(std::array<std::int64_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = m_chunk_grid.get_mut(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        ChunkLayer<Dim>& layer = chunk_result.value().get();
        return layer.template remove<T>(cell_pos).transform_error(map_err);
    }
    auto remove_cell_all(std::array<std::int64_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = m_chunk_grid.get_mut(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        return chunk_result.value().get().remove_all(cell_pos).transform_error(map_err);
    }
};

export template <std::size_t Dim>
struct ExtendibleChunkRefGrid {
   private:
    struct ChunkRef {
        std::reference_wrapper<Chunk<Dim>> value;
    };

    tree_extendible_grid<Dim, ChunkRef> m_chunk_grid;
    std::size_t m_chunk_width_shift;

    auto chunk_coords(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::tuple<std::array<std::int32_t, Dim>, std::array<std::uint32_t, Dim>>, ChunkGridError> {
        std::tuple<std::array<std::int32_t, Dim>, std::array<std::uint32_t, Dim>> result;
        // if coords don't fit in int32 with width shift, return error, negative allowd
        for (std::size_t i = 0; i < Dim; i++) {
            std::int64_t chunk_coord = pos[i] >> m_chunk_width_shift;
            if (chunk_coord < std::numeric_limits<std::int32_t>::min() ||
                chunk_coord > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected(grid_error::OutOfBounds);
            }
            std::get<0>(result)[i] = static_cast<std::int32_t>(chunk_coord);
            std::get<1>(result)[i] = static_cast<std::uint32_t>(pos[i] & ((1 << m_chunk_width_shift) - 1));
        }
        return result;
    }
    constexpr static auto deref =
        [](std::reference_wrapper<const ChunkRef> ref) -> std::reference_wrapper<const Chunk<Dim>> {
        return std::cref(ref.get().value.get());
    };
    constexpr static auto deref_mut = [](std::reference_wrapper<ChunkRef> ref) -> std::reference_wrapper<Chunk<Dim>> {
        return std::ref(ref.get().value.get());
    };

   public:
    ExtendibleChunkRefGrid(std::size_t chunk_width_shift) : m_chunk_width_shift(chunk_width_shift) {}
    ExtendibleChunkRefGrid(const ExtendibleChunkRefGrid&)            = delete;
    ExtendibleChunkRefGrid(ExtendibleChunkRefGrid&&)                 = default;
    ExtendibleChunkRefGrid& operator=(const ExtendibleChunkRefGrid&) = delete;
    ExtendibleChunkRefGrid& operator=(ExtendibleChunkRefGrid&&)      = default;

    std::size_t chunk_width_shift() const { return m_chunk_width_shift; }
    std::size_t chunk_width() const { return static_cast<std::size_t>(1) << m_chunk_width_shift; }

    auto get_chunk(std::array<std::int32_t, Dim> pos) const { return m_chunk_grid.get(pos).transform(deref); }
    auto get_chunk_mut(std::array<std::int32_t, Dim> pos) { return m_chunk_grid.get_mut(pos).transform(deref_mut); }

    void shrink_grid() { m_chunk_grid.shrink(); }
    void clear_grid() { m_chunk_grid.clear(); }

    auto insert_chunk(std::array<std::int32_t, Dim> pos, Chunk<Dim>& chunk) -> std::expected<void, ChunkGridError> {
        if (chunk.width_shift() != m_chunk_width_shift) return std::unexpected(ChunkLayerError::WidthMismatch);
        auto result = m_chunk_grid.set_new(pos, ChunkRef{std::ref(chunk)});
        if (!result.has_value()) return std::unexpected(ChunkGridError{result.error()});
        return {};
    }

    auto remove_chunk(std::array<std::int32_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        return m_chunk_grid.remove(pos);
    }
    auto take_chunk(std::array<std::int32_t, Dim> pos) -> std::expected<Chunk<Dim>, ChunkGridError> {
        (void)pos;
        return std::unexpected(ChunkGridError{grid_error::InvalidPos});
    }
    auto iter_chunks() const { return m_chunk_grid.iter_cells() | std::views::transform(deref); }
    auto iter_chunks_mut() { return m_chunk_grid.iter_cells_mut() | std::views::transform(deref_mut); }
    auto iter_chunk_pos() const { return m_chunk_grid.iter_pos(); }
    auto iter_chunk_with_pos() const { return std::views::zip(iter_chunk_pos(), iter_chunks()); }
    auto iter_chunk_with_pos_mut() { return std::views::zip(iter_chunk_pos(), iter_chunks_mut()); }

    template <typename T>
    auto insert_cell(std::array<std::int64_t, Dim> pos, T&& value) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        return get_chunk_mut(chunk_pos).transform_error(map_err).and_then(
            [&cell_pos, &value](Chunk<Dim>& chunk) -> std::expected<void, ChunkGridError> {
                return chunk.set(cell_pos, std::forward<T>(value));
            });
    }
    template <typename... Args>
    auto insert_cell_multi(std::array<std::int64_t, Dim> pos, Args&&... value) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        return get_chunk_mut(chunk_pos).transform_error(map_err).and_then(
            [&cell_pos, &value...](Chunk<Dim>& chunk) -> std::expected<void, ChunkGridError> {
                return chunk.set_multi(cell_pos, std::forward<Args>(value)...);
            });
    }
    template <typename T>
    auto get_cell(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::reference_wrapper<const T>, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = get_chunk(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        const ChunkLayer<Dim>& layer = chunk_result.value().get();
        return layer.template get<T>(cell_pos).transform_error(map_err);
    }
    template <typename T>
    auto get_cell_mut(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::reference_wrapper<const T>, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = get_chunk_mut(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        const ChunkLayer<Dim>& layer = chunk_result.value().get();
        return layer.template get_mut<T>(cell_pos).transform_error(map_err);
    }
    template <typename T>
    auto remove_cell(std::array<std::int64_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = get_chunk_mut(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        ChunkLayer<Dim>& layer = chunk_result.value().get();
        return layer.template remove<T>(cell_pos).transform_error(map_err);
    }
    auto remove_cell_all(std::array<std::int64_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        auto coords_result = chunk_coords(pos);
        if (!coords_result.has_value()) return std::unexpected(coords_result.error());

        auto [chunk_pos, cell_pos] = coords_result.value();
        auto chunk_result          = get_chunk_mut(chunk_pos).transform_error(map_err);
        if (!chunk_result.has_value()) return std::unexpected(chunk_result.error());

        return chunk_result.value().get().remove_all(cell_pos).transform_error(map_err);
    }
};

}  // namespace ext::grid

namespace ext::grid::layers {
constexpr static auto map_err = [](grid_error err) -> LayerError {
    switch (err) {
        case grid_error::OutOfBounds:
            return LayerError::OutOfBounds;
        case grid_error::EmptyCell:
            return LayerError::EmptyCell;
        case grid_error::AlreadyOccupied:
            return LayerError::InvalidValue;  // treat already occupied as invalid value for simplicity
        default:
            return LayerError::InvalidValue;  // treat all other errors as invalid value for simplicity
    }
};
export template <std::size_t Dim, typename T>
    requires std::movable<T>
class PackedLayer : public ChunkLayer<Dim> {
   private:
    packed_grid<Dim, T> m_grid;
    std::size_t m_width_shift;

   public:
    PackedLayer(std::size_t width_shift, T default_value = T{})
        : m_grid(
              []() {
                  std::array<std::uint32_t, Dim> dimensions;
                  dimensions.fill(static_cast<std::uint32_t>(1) << width_shift);
                  return dimensions;
              }(),
              default_value),
          m_width_shift(width_shift) {}
    std::size_t width_shift() const override { return m_width_shift; }
    bool supports_type(meta::type_index type) const override { return type == meta::type_id<T>(); }
    std::vector<meta::type_index> supported_types() const override { return {meta::type_id<T>()}; }
    void clear() override { m_grid.clear(); }

   private:
    std::expected<void*, LayerError> get_mut(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.get_mut(pos).transform_error(map_err).transform(
            [](std::reference_wrapper<T> ref) { return static_cast<void*>(std::addressof(ref.get())); });
    }
    std::expected<const void*, LayerError> get(meta::type_index type,
                                               std::array<std::uint32_t, Dim> pos) const override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.get(pos).transform_error(map_err).transform(
            [](std::reference_wrapper<const T> ref) { return static_cast<const void*>(std::addressof(ref.get())); });
    }
    std::expected<void, LayerError> set_copy(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             const void* value) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.set(pos, *static_cast<const T*>(value)).transform_error(map_err).transform([](auto&&) {});
    }
    std::expected<void, LayerError> set_move(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             void* value) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.set(pos, std::move(*static_cast<T*>(value))).transform_error(map_err).transform([](auto&&) {});
    }
    std::expected<void, LayerError> remove(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.reset(pos).transform_error(map_err);
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, const void*>>, LayerError>
    try_iter_type(meta::type_index type) const override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.iter() | std::views::transform([](auto&& pair) {
                   auto&& [pos, value] = pair;
                   return std::make_pair(std::move(pos), static_cast<const void*>(std::addressof(value)));
               });
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, void*>>, LayerError>
    try_iter_type_mut(meta::type_index type) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.iter_mut() | std::views::transform([](auto&& pair) {
                   auto&& [pos, value] = pair;
                   return std::make_pair(std::move(pos), static_cast<void*>(std::addressof(value)));
               });
    }
};
export template <std::size_t Dim, typename T>
    requires std::movable<T>
class DenseLayer : public ChunkLayer<Dim> {
   private:
    dense_grid<Dim, T> m_grid;
    std::size_t m_width_shift;

   public:
    DenseLayer(std::size_t width_shift)
        : m_grid([width_shift]() {
              std::array<std::uint32_t, Dim> dimensions;
              dimensions.fill(static_cast<std::uint32_t>(1) << width_shift);
              return dimensions;
          }()),
          m_width_shift(width_shift) {}
    std::size_t width_shift() const override { return m_width_shift; }
    bool supports_type(meta::type_index type) const override { return type == meta::type_id<T>(); }
    std::vector<meta::type_index> supported_types() const override { return {meta::type_id<T>()}; }
    void clear() override { m_grid.clear(); }

   private:
    std::expected<void*, LayerError> get_mut(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.get_mut(pos).transform_error(map_err).transform(
            [](std::reference_wrapper<T> ref) { return static_cast<void*>(std::addressof(ref.get())); });
    }
    std::expected<const void*, LayerError> get(meta::type_index type,
                                               std::array<std::uint32_t, Dim> pos) const override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.get(pos).transform_error(map_err).transform(
            [](std::reference_wrapper<const T> ref) { return static_cast<const void*>(std::addressof(ref.get())); });
    }
    std::expected<void, LayerError> set_copy(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             const void* value) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.set(pos, *static_cast<const T*>(value)).transform_error(map_err).transform([](auto&&) {});
    }
    std::expected<void, LayerError> set_move(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             void* value) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.set(pos, std::move(*static_cast<T*>(value))).transform_error(map_err).transform([](auto&&) {});
    }
    std::expected<void, LayerError> remove(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.remove(pos).transform_error(map_err);
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, const void*>>, LayerError>
    try_iter_type(meta::type_index type) const override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.iter() | std::views::transform([](auto&& pair) {
                   auto&& [pos, value] = pair;
                   return std::make_pair(std::move(pos), static_cast<const void*>(std::addressof(value)));
               });
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, void*>>, LayerError>
    try_iter_type_mut(meta::type_index type) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.iter_mut() | std::views::transform([](auto&& pair) {
                   auto&& [pos, value] = pair;
                   return std::make_pair(std::move(pos), static_cast<void*>(std::addressof(value)));
               });
    }
};
export template <std::size_t Dim, typename T>
    requires std::movable<T>
class SparseLayer : public ChunkLayer<Dim> {
   private:
    sparse_grid<Dim, T> m_grid;
    std::size_t m_width_shift;

   public:
    SparseLayer(std::size_t width_shift)
        : m_grid([width_shift]() {
              std::array<std::uint32_t, Dim> dimensions;
              dimensions.fill(static_cast<std::uint32_t>(1) << width_shift);
              return dimensions;
          }()),
          m_width_shift(width_shift) {}
    std::size_t width_shift() const override { return m_width_shift; }
    bool supports_type(meta::type_index type) const override { return type == meta::type_id<T>(); }
    std::vector<meta::type_index> supported_types() const override { return {meta::type_id<T>()}; }
    void clear() override { m_grid.clear(); }

   private:
    std::expected<void*, LayerError> get_mut(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.get_mut(pos).transform_error(map_err).transform(
            [](std::reference_wrapper<T> ref) { return static_cast<void*>(std::addressof(ref.get())); });
    }
    std::expected<const void*, LayerError> get(meta::type_index type,
                                               std::array<std::uint32_t, Dim> pos) const override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.get(pos).transform_error(map_err).transform(
            [](std::reference_wrapper<const T> ref) { return static_cast<const void*>(std::addressof(ref.get())); });
    }
    std::expected<void, LayerError> set_copy(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             const void* value) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.set(pos, *static_cast<const T*>(value)).transform_error(map_err).transform([](auto&&) {});
    }
    std::expected<void, LayerError> set_move(meta::type_index type,
                                             std::array<std::uint32_t, Dim> pos,
                                             void* value) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.set(pos, std::move(*static_cast<T*>(value))).transform_error(map_err).transform([](auto&&) {});
    }
    std::expected<void, LayerError> remove(meta::type_index type, std::array<std::uint32_t, Dim> pos) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.remove(pos).transform_error(map_err);
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, const void*>>, LayerError>
    try_iter_type(meta::type_index type) const override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.iter() | std::views::transform([](auto&& pair) {
                   auto&& [pos, value] = pair;
                   return std::make_pair(std::move(pos), static_cast<const void*>(std::addressof(value)));
               });
    }
    std::expected<utils::input_iterable<std::pair<std::array<std::uint32_t, Dim>, void*>>, LayerError>
    try_iter_type_mut(meta::type_index type) override {
        if (type != meta::type_id<T>()) return std::unexpected(LayerError::UnsupportedType);
        return m_grid.iter_mut() | std::views::transform([](auto&& pair) {
                   auto&& [pos, value] = pair;
                   return std::make_pair(std::move(pos), static_cast<void*>(std::addressof(value)));
               });
    }
};
}  // namespace ext::grid::layers