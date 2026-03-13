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
    virtual std::expected<void, LayerError> remove(meta::type_index, std::array<std::uint32_t, Dim>)                = 0;
    virtual std::expected<void, LayerError> for_each_cell(
        meta::type_index, utils::function_ref<void(std::array<std::uint32_t, Dim>, const void*)>) = 0;
    virtual std::expected<void, LayerError> for_each_cell_mut(
        meta::type_index, utils::function_ref<void(std::array<std::uint32_t, Dim>, void*)>) = 0;

    template <std::size_t D>
    friend class Chunk;

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
    template <typename T>
    std::expected<void, LayerError> remove(std::array<std::uint32_t, Dim> pos) {
        return remove(meta::type_id<std::remove_cvref_t<T>>(), pos);
    }
    std::expected<void, LayerError> remove_all(std::array<std::uint32_t, Dim> pos) {
        std::expected<void, LayerError> result = std::unexpected(LayerError::UnsupportedType);
        for (const auto& type : supported_types()) {
            auto res = remove(type, pos);
            if (res.has_value()) {
                result = res;
            }
        }
        return result;
    }

    template <typename T>
    std::expected<void, LayerError> for_each_cell(
        utils::function_ref<void(std::array<std::uint32_t, Dim>, const T&)> func) const {
        auto type = meta::type_id<std::remove_cvref_t<T>>();
        return for_each_cell(type, [&](std::array<std::uint32_t, Dim> pos, const void* value) {
            func(pos, *static_cast<const T*>(value));
        });
    }
    template <typename T>
    std::expected<void, LayerError> for_each_cell_mut(
        utils::function_ref<void(std::array<std::uint32_t, Dim>, T&)> func) {
        auto type = meta::type_id<std::remove_cvref_t<T>>();
        return for_each_cell_mut(
            type, [&](std::array<std::uint32_t, Dim> pos, void* value) { func(pos, *static_cast<T*>(value)); });
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
    std::expected<void, LayerError> for_each_cell(
        meta::type_index type, utils::function_ref<void(std::array<std::uint32_t, Dim>, const void*)> func) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->for_each_cell(type, func);
    }
    std::expected<void, LayerError> for_each_cell_mut(
        meta::type_index type, utils::function_ref<void(std::array<std::uint32_t, Dim>, void*)> func) override {
        if (!m_type_to_layer.contains(type)) return std::unexpected(LayerError::UnsupportedType);
        return m_layers.at(m_type_to_layer.at(type))->for_each_cell_mut(type, func);
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
        return chunk_coords(pos).and_then([this, &value](auto&& coords) {
            auto&& [chunk_pos, cell_pos] = coords;
            return m_chunk_grid.get_mut(chunk_pos).and_then(
                [&cell_pos, &value](Chunk<Dim>& chunk) { return chunk.set(cell_pos, std::forward<T>(value)); });
        });
    }
    template <typename T>
    auto get_cell(std::array<std::int64_t, Dim> pos) const
        -> std::expected<std::reference_wrapper<const T>, ChunkGridError> {
        return chunk_coords(pos).and_then([this](auto&& coords) {
            auto&& [chunk_pos, cell_pos] = coords;
            return m_chunk_grid.get(chunk_pos).and_then(
                [&cell_pos](const Chunk<Dim>& chunk) { return chunk.get<T>(cell_pos); });
        });
    }
    template <typename T>
    auto remove(std::array<std::int64_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        return chunk_coords(pos).and_then([this](auto&& coords) {
            auto&& [chunk_pos, cell_pos] = coords;
            return m_chunk_grid.get_mut(chunk_pos).and_then(
                [&cell_pos](Chunk<Dim>& chunk) { return chunk.remove<T>(cell_pos); });
        });
    }
    auto remove_all(std::array<std::int64_t, Dim> pos) -> std::expected<void, ChunkGridError> {
        return chunk_coords(pos).and_then([this](auto&& coords) {
            auto&& [chunk_pos, cell_pos] = coords;
            return m_chunk_grid.get_mut(chunk_pos).and_then(
                [&cell_pos](Chunk<Dim>& chunk) { return chunk.remove_all(cell_pos); });
        });
    }
};

// instantiations for testing
template class Chunk<2>;
template class ExtendibleChunkGrid<2>;
}  // namespace ext::grid