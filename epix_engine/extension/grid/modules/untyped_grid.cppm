module;

#ifndef EPIX_IMPORT_STD
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#endif

export module epix.extension.grid:untyped_grid;
#ifdef EPIX_IMPORT_STD
import std;
#endif

import :any_grid;
import :concepts;
import epix.utils;

namespace epix::ext::grid {

// ============================================================
// Forward declarations & traits
// ============================================================

export template <std::size_t, grid_category, typename, typename>
class untyped_grid;
export template <std::size_t, grid_category, typename, typename>
class untyped_grid_view;

namespace detail {
template <typename T>
struct is_untyped_grid : std::false_type {};
template <std::size_t Dim, grid_category Cat, typename DimT, typename PosT>
struct is_untyped_grid<untyped_grid<Dim, Cat, DimT, PosT>> : std::true_type {};

template <typename T>
struct is_untyped_grid_view : std::false_type {};
template <std::size_t Dim, grid_category Cat, typename DimT, typename PosT>
struct is_untyped_grid_view<untyped_grid_view<Dim, Cat, DimT, PosT>> : std::true_type {};

/** @brief Require that the underlying grid uses a reference get type. */
template <typename G>
concept reference_get_grid = viewable_grid<G> && std::is_reference_v<grid_get_type<G>> &&
                             !std::is_const_v<std::remove_reference_t<grid_get_type<G>>>;
}  // namespace detail

// ============================================================
// detail::void_grid_concept — vtable base for untyped_grid
// ============================================================

namespace detail {
template <std::size_t Dim, typename DimT, typename PosT>
struct void_grid_concept {
    using pos_type = std::array<PosT, Dim>;

    virtual ~void_grid_concept()                                              = default;
    virtual auto dimensions() -> std::array<DimT, Dim>                        = 0;
    virtual auto contains(const pos_type& pos) -> bool                        = 0;
    virtual auto get(const pos_type& pos) -> std::expected<void*, grid_error> = 0;
    virtual auto clone() const -> std::unique_ptr<void_grid_concept> { std::unreachable(); }
    virtual auto dimensions_c() const -> std::array<DimT, Dim> { std::unreachable(); }
    virtual auto contains_c(const pos_type& pos) const -> bool { std::unreachable(); }
    virtual auto get_c(const pos_type& pos) const -> std::expected<const void*, grid_error> { std::unreachable(); }
    virtual auto iter_pos() -> utils::input_iterable<pos_type> { std::unreachable(); }
    virtual auto iter_pos_c() const -> utils::input_iterable<pos_type> { std::unreachable(); }
    virtual auto iter_cells() -> utils::input_iterable<void*> { std::unreachable(); }
    virtual auto iter_cells_c() const -> utils::input_iterable<const void*> { std::unreachable(); }
    virtual auto iter() -> utils::input_iterable<std::tuple<pos_type, void*>> { std::unreachable(); }
    virtual auto iter_c() const -> utils::input_iterable<std::tuple<pos_type, const void*>> { std::unreachable(); }
    virtual auto set(const pos_type& pos, void* val) -> std::expected<void, grid_error> { std::unreachable(); }
    virtual auto set_new(const pos_type& pos, void* val) -> std::expected<void, grid_error> { std::unreachable(); }
    virtual auto remove(const pos_type& pos) -> std::expected<void, grid_error> { std::unreachable(); }
    virtual auto clear() -> void { std::unreachable(); }
    virtual auto get_unsafe(const pos_type& pos) -> void* { std::unreachable(); }
    virtual auto get_unsafe_c(const pos_type& pos) const -> const void* { std::unreachable(); }
    virtual auto set_unsafe(const pos_type& pos, void* val) -> void* { std::unreachable(); }
    virtual auto remove_unsafe(const pos_type& pos) -> void { std::unreachable(); }
};
}  // namespace detail

// ============================================================
// untyped_grid — truly type-erased owning grid (void* based)
// ============================================================

export template <std::size_t Dim,
                 grid_category Cat = grid_category::iterable | grid_category::container |
                                     grid_category::unsafe_viewable | grid_category::unsafe_container |
                                     grid_category::const_viewable | grid_category::const_iterable |
                                     grid_category::const_unsafe,
                 typename DimT     = std::uint32_t,
                 typename PosT     = std::int32_t>
class untyped_grid {
   public:
    using pos_type   = std::array<PosT, Dim>;
    using value_type = void;

    static constexpr grid_category category = Cat;

   private:
    using concept_t = detail::void_grid_concept<Dim, DimT, PosT>;

    template <detail::reference_get_grid G>
        requires(std::tuple_size_v<typename detail::grid_pos_type<G>> == Dim) &&
                std::same_as<typename detail::grid_dimensions_type<G>::value_type, DimT> &&
                std::same_as<typename detail::grid_pos_type<G>::value_type, PosT> && satisfies_category<G, Cat>
    struct model_t final : concept_t {
        using Cell = detail::grid_cell_type<G>;
        G m_grid;
        model_t(G grid) : m_grid(std::move(grid)) {}
        auto clone() const -> std::unique_ptr<concept_t> override {
            if constexpr (has_category(Cat, grid_category::copyable))
                return std::make_unique<model_t>(m_grid);
            else
                std::unreachable();
        }
        auto dimensions() -> std::array<DimT, Dim> override { return m_grid.dimensions(); }
        auto contains(const pos_type& pos) -> bool override { return m_grid.contains(pos); }
        auto get(const pos_type& pos) -> std::expected<void*, grid_error> override {
            return m_grid.get(pos).transform(
                [](auto&& ref) -> void* { return static_cast<void*>(std::addressof(ref.get())); });
        }
        auto dimensions_c() const -> std::array<DimT, Dim> override {
            if constexpr (has_category(Cat, grid_category::const_viewable))
                return m_grid.dimensions();
            else
                std::unreachable();
        }
        auto contains_c(const pos_type& pos) const -> bool override {
            if constexpr (has_category(Cat, grid_category::const_viewable))
                return m_grid.contains(pos);
            else
                std::unreachable();
        }
        auto get_c(const pos_type& pos) const -> std::expected<const void*, grid_error> override {
            if constexpr (has_category(Cat, grid_category::const_viewable))
                return m_grid.get(pos).transform(
                    [](auto&& ref) -> const void* { return static_cast<const void*>(std::addressof(ref.get())); });
            else
                std::unreachable();
        }
        auto iter_pos() -> utils::input_iterable<pos_type> override {
            if constexpr (has_category(Cat, grid_category::iterable))
                return m_grid.iter_pos();
            else
                std::unreachable();
        }
        auto iter_pos_c() const -> utils::input_iterable<pos_type> override {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::const_iterable))
                return m_grid.iter_pos();
            else
                std::unreachable();
        }
        auto iter_cells() -> utils::input_iterable<void*> override {
            if constexpr (has_category(Cat, grid_category::iterable))
                return utils::input_iterable<void*>(m_grid.iter_cells() |
                                                    std::views::transform([](Cell& cell) -> void* {
                                                        return static_cast<void*>(std::addressof(cell));
                                                    }));
            else
                std::unreachable();
        }
        auto iter_cells_c() const -> utils::input_iterable<const void*> override {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::const_iterable))
                return utils::input_iterable<const void*>(m_grid.iter_cells() |
                                                          std::views::transform([](const Cell& cell) -> const void* {
                                                              return static_cast<const void*>(std::addressof(cell));
                                                          }));
            else
                std::unreachable();
        }
        auto iter() -> utils::input_iterable<std::tuple<pos_type, void*>> override {
            if constexpr (has_category(Cat, grid_category::iterable))
                return utils::input_iterable<std::tuple<pos_type, void*>>(
                    m_grid.iter() | std::views::transform([](auto&& pair) -> std::tuple<pos_type, void*> {
                        return {std::get<0>(pair), static_cast<void*>(std::addressof(std::get<1>(pair)))};
                    }));
            else
                std::unreachable();
        }
        auto iter_c() const -> utils::input_iterable<std::tuple<pos_type, const void*>> override {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::const_iterable))
                return utils::input_iterable<std::tuple<pos_type, const void*>>(
                    m_grid.iter() | std::views::transform([](auto&& pair) -> std::tuple<pos_type, const void*> {
                        return {std::get<0>(pair), static_cast<const void*>(std::addressof(std::get<1>(pair)))};
                    }));
            else
                std::unreachable();
        }
        auto set(const pos_type& pos, void* val) -> std::expected<void, grid_error> override {
            if constexpr (has_category(Cat, grid_category::container))
                return m_grid.set(pos, std::move(*static_cast<Cell*>(val))).transform([](auto&&) -> void {});
            else
                std::unreachable();
        }
        auto set_new(const pos_type& pos, void* val) -> std::expected<void, grid_error> override {
            if constexpr (has_category(Cat, grid_category::container))
                return m_grid.set_new(pos, std::move(*static_cast<Cell*>(val))).transform([](auto&&) -> void {});
            else
                std::unreachable();
        }
        auto remove(const pos_type& pos) -> std::expected<void, grid_error> override {
            if constexpr (has_category(Cat, grid_category::container))
                return m_grid.remove(pos);
            else
                std::unreachable();
        }
        auto clear() -> void override {
            if constexpr (has_category(Cat, grid_category::container))
                m_grid.clear();
            else
                std::unreachable();
        }
        auto get_unsafe(const pos_type& pos) -> void* override {
            if constexpr (has_category(Cat, grid_category::unsafe_viewable))
                return static_cast<void*>(std::addressof(m_grid.get_unsafe(pos)));
            else
                std::unreachable();
        }
        auto get_unsafe_c(const pos_type& pos) const -> const void* override {
            if constexpr (has_category(Cat, grid_category::unsafe_viewable | grid_category::const_unsafe))
                return static_cast<const void*>(std::addressof(m_grid.get_unsafe(pos)));
            else
                std::unreachable();
        }
        auto set_unsafe(const pos_type& pos, void* val) -> void* override {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                return static_cast<void*>(std::addressof(m_grid.set_unsafe(pos, std::move(*static_cast<Cell*>(val)))));
            else
                std::unreachable();
        }
        auto remove_unsafe(const pos_type& pos) -> void override {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                m_grid.remove_unsafe(pos);
            else
                std::unreachable();
        }
    };

    std::unique_ptr<concept_t> m_impl;

   public:
    untyped_grid(untyped_grid&&) noexcept                    = default;
    auto operator=(untyped_grid&&) noexcept -> untyped_grid& = default;

    untyped_grid(const untyped_grid& other)
        requires(has_category(Cat, grid_category::copyable))
    {
        m_impl = other.m_impl->clone();
    }
    auto operator=(const untyped_grid& other) -> untyped_grid&
        requires(has_category(Cat, grid_category::copyable))
    {
        if (this != &other) m_impl = other.m_impl->clone();
        return *this;
    }

    template <typename G>
        requires(detail::reference_get_grid<std::decay_t<G>> &&
                 std::tuple_size_v<typename detail::grid_pos_type<std::decay_t<G>>> == Dim) &&
                std::same_as<typename detail::grid_dimensions_type<std::decay_t<G>>::value_type, DimT> &&
                std::same_as<typename detail::grid_pos_type<std::decay_t<G>>::value_type, PosT> &&
                (!detail::is_untyped_grid<std::remove_cvref_t<G>>::value) &&
                satisfies_category<std::remove_cvref_t<G>, Cat>
    untyped_grid(G&& grid) : m_impl(std::make_unique<model_t<std::remove_cvref_t<G>>>(std::forward<G>(grid))) {}

    /** @brief Convert from another untyped_grid with a superset of this category. */
    template <grid_category OtherCat>
        requires((static_cast<unsigned>(OtherCat) & static_cast<unsigned>(Cat)) == static_cast<unsigned>(Cat))
    untyped_grid(untyped_grid<Dim, OtherCat, DimT, PosT>&& other) noexcept : m_impl(std::move(other.m_impl)) {}

    template <std::size_t, grid_category, typename, typename>
    friend class untyped_grid;

    // ─── viewable_grid (always) ────────────────────────────────
    auto dimensions() -> std::array<DimT, Dim> { return m_impl->dimensions(); }
    auto contains(const pos_type& pos) -> bool { return m_impl->contains(pos); }
    auto get(const pos_type& pos) -> std::expected<void*, grid_error> { return m_impl->get(pos); }

    auto dimensions() const -> std::array<DimT, Dim>
        requires(has_category(Cat, grid_category::const_viewable))
    {
        return m_impl->dimensions_c();
    }
    auto contains(const pos_type& pos) const -> bool
        requires(has_category(Cat, grid_category::const_viewable))
    {
        return m_impl->contains_c(pos);
    }
    auto get(const pos_type& pos) const -> std::expected<const void*, grid_error>
        requires(has_category(Cat, grid_category::const_viewable))
    {
        return m_impl->get_c(pos);
    }

    // ─── iterable_grid ─────────────────────────────────────────
    auto iter_pos() -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl->iter_pos();
    }
    auto iter_cells() -> utils::input_iterable<void*>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl->iter_cells();
    }
    auto iter() -> utils::input_iterable<std::tuple<pos_type, void*>>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl->iter();
    }
    auto iter_pos() const -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable | grid_category::const_iterable))
    {
        return m_impl->iter_pos_c();
    }
    auto iter_cells() const -> utils::input_iterable<const void*>
        requires(has_category(Cat, grid_category::iterable | grid_category::const_iterable))
    {
        return m_impl->iter_cells_c();
    }
    auto iter() const -> utils::input_iterable<std::tuple<pos_type, const void*>>
        requires(has_category(Cat, grid_category::iterable | grid_category::const_iterable))
    {
        return m_impl->iter_c();
    }

    // ─── grid_container ────────────────────────────────────────
    auto set(const pos_type& pos, void* val) -> std::expected<void, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl->set(pos, val);
    }
    auto set_new(const pos_type& pos, void* val) -> std::expected<void, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl->set_new(pos, val);
    }
    auto remove(const pos_type& pos) -> std::expected<void, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl->remove(pos);
    }
    auto clear() -> void
        requires(has_category(Cat, grid_category::container))
    {
        m_impl->clear();
    }

    // ─── unsafe_viewable_grid ──────────────────────────────────
    auto get_unsafe(const pos_type& pos) -> void*
        requires(has_category(Cat, grid_category::unsafe_viewable))
    {
        return m_impl->get_unsafe(pos);
    }
    auto get_unsafe(const pos_type& pos) const -> const void*
        requires(has_category(Cat, grid_category::unsafe_viewable | grid_category::const_unsafe))
    {
        return m_impl->get_unsafe_c(pos);
    }

    // ─── unsafe_grid_container ─────────────────────────────────
    auto set_unsafe(const pos_type& pos, void* val) -> void*
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        return m_impl->set_unsafe(pos, val);
    }
    auto remove_unsafe(const pos_type& pos) -> void
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        m_impl->remove_unsafe(pos);
    }
};

export template <typename G>
    requires detail::reference_get_grid<std::decay_t<G>>
untyped_grid(G&&) -> untyped_grid<std::tuple_size_v<typename detail::grid_pos_type<std::decay_t<G>>>,
                                  detail::default_category_for<std::remove_cvref_t<G>>(),
                                  typename detail::grid_dimensions_type<std::remove_cvref_t<G>>::value_type,
                                  typename detail::grid_pos_type<std::remove_cvref_t<G>>::value_type>;

}  // namespace epix::ext::grid
