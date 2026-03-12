export module epix.experimental.grid;

import std;

import epix.meta;

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

export template <std::size_t Dim>
struct ExtendibleGrid {
   private:
    std::size_t m_chunk_width_shift;
    std::vector<Chunk<Dim>> m_chunks;
};
}  // namespace ext::grid