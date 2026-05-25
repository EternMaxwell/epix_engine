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

import :concepts;
import epix.utils;

namespace epix::ext::grid {

// ============================================================
// grid_category — bitmask for type-erased grid capabilities
// ============================================================

/** @brief Bitmask controlling which concepts an untyped_grid / untyped_grid_view satisfies.
 *
 * `viewable_grid` is always required and not part of the category.
 * Other capabilities are opt-in via bitwise OR.
 */
export enum class grid_category : unsigned {
    none             = 0,
    iterable         = 1 << 0,  // 0b000001 → iterable_grid
    container        = 1 << 1,  // 0b000010 → grid_container
    unsafe_viewable  = 1 << 2,  // 0b000100 → unsafe_viewable_grid
    unsafe_container = 1 << 3,  // 0b001000 → unsafe_grid_container
    constness        = 1 << 4,  // 0b010000 → const G satisfies same concepts
    copyable         = 1 << 5,  // 0b100000 → copy_constructible + copy_assignable
};

export constexpr auto operator|(grid_category a, grid_category b) -> grid_category {
    return static_cast<grid_category>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
export constexpr auto operator&(grid_category a, grid_category b) -> grid_category {
    return static_cast<grid_category>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
export constexpr auto has_category(grid_category val, grid_category flag) -> bool {
    return (static_cast<unsigned>(val & flag)) == static_cast<unsigned>(flag);
}

// ============================================================
// detail — traits for mapping GetType ↔ expected return type
// ============================================================

namespace detail {
template <typename GetType>
struct get_return_type {
    using type = std::expected<GetType, grid_error>;
};
template <typename Cell>
struct get_return_type<Cell&> {
    using type = std::expected<std::reference_wrapper<Cell>, grid_error>;
};
template <typename Cell>
struct get_return_type<const Cell&> {
    using type = std::expected<std::reference_wrapper<const Cell>, grid_error>;
};
template <typename GetType>
using get_return_t = typename get_return_type<GetType>::type;

/** @brief The return type of const get() for a given GetType.
 *  Maps cell_type& → expected<ref<const cell_type>> (downgrade mutable to const). */
template <typename GetType>
struct get_const_return_type {
    using type = std::expected<GetType, grid_error>;
};
template <typename Cell>
struct get_const_return_type<Cell&> {
    using type = std::expected<std::reference_wrapper<const Cell>, grid_error>;
};
template <typename Cell>
struct get_const_return_type<const Cell&> {
    using type = std::expected<std::reference_wrapper<const Cell>, grid_error>;
};
template <typename GetType>
using get_const_return_t = typename get_const_return_type<GetType>::type;

/** @brief Check that G satisfies all concepts required by Cat, except constness itself. */
template <typename G, grid_category Cat>
concept satisfies_category_base =
    viewable_grid<G> && (!has_category(Cat, grid_category::iterable) || iterable_grid<G>) &&
    (!has_category(Cat, grid_category::container) || grid_container<G>) &&
    (!has_category(Cat, grid_category::unsafe_viewable) || unsafe_viewable_grid<G>) &&
    (!has_category(Cat, grid_category::unsafe_container) || unsafe_grid_container<G>) &&
    (!has_category(Cat, grid_category::copyable) || std::copy_constructible<G>);

/** @brief Check that const G satisfies read-only categories (only viewable + iterable). */
template <typename G, grid_category Cat>
concept satisfies_category_const =
    viewable_grid<G> && (!has_category(Cat, grid_category::iterable) || iterable_grid<G>) &&
    (!has_category(Cat, grid_category::unsafe_viewable) || unsafe_viewable_grid<G>);

/** @brief Derive the default category from a concrete grid type G. */
template <viewable_grid G>
constexpr auto default_category_for() -> grid_category {
    constexpr grid_category it_cat = iterable_grid<G> ? grid_category::iterable : grid_category::none;
    constexpr grid_category ct_cat = grid_container<G> ? grid_category::container : grid_category::none;
    constexpr grid_category uv_cat = unsafe_viewable_grid<G> ? grid_category::unsafe_viewable : grid_category::none;
    constexpr grid_category uc_cat = unsafe_grid_container<G> ? grid_category::unsafe_container : grid_category::none;
    constexpr grid_category cp_cat = std::copy_constructible<G> ? grid_category::copyable : grid_category::none;
    constexpr grid_category cat    = it_cat | ct_cat | uv_cat | uc_cat | cp_cat;
    constexpr grid_category cn_cat =
        satisfies_category_const<const G, cat> ? grid_category::constness : grid_category::none;
    return cat | cn_cat;
}
}  // namespace detail

/** @brief Check that G satisfies all concepts required by Cat, including constness. */
export template <typename G, grid_category Cat>
concept satisfies_category =
    detail::satisfies_category_base<G, Cat> &&
    (!has_category(Cat, grid_category::constness) || detail::satisfies_category_const<const G, Cat>);

export template <std::size_t, typename, grid_category, typename, typename>
class untyped_grid;
export template <std::size_t, typename, grid_category, typename, typename>
class untyped_grid_view;

namespace detail {
template <typename T>
struct is_untyped_grid : std::false_type {};
template <std::size_t Dim, typename GetType, grid_category Cat, typename DimT, typename PosT>
struct is_untyped_grid<untyped_grid<Dim, GetType, Cat, DimT, PosT>> : std::true_type {};

template <typename T>
struct is_untyped_grid_view : std::false_type {};
template <std::size_t Dim, typename GetType, grid_category Cat, typename DimT, typename PosT>
struct is_untyped_grid_view<untyped_grid_view<Dim, GetType, Cat, DimT, PosT>> : std::true_type {};

}  // namespace detail

// ============================================================
// detail::untyped_concept — type-erased vtable base (Cat-independent)
// ============================================================

namespace detail {
template <std::size_t Dim, typename GetType, typename DimT, typename PosT>
struct untyped_concept {
    using pos_type       = std::array<PosT, Dim>;
    using cell_type      = std::remove_cvref_t<GetType>;
    using get_ret        = get_return_t<GetType>;
    using get_const_ret  = get_const_return_t<GetType>;
    using get_unsafe_ret = GetType;
    using get_unsafe_const_ret =
        std::conditional_t<std::is_reference_v<GetType>, const std::remove_reference_t<GetType>&, GetType>;

    virtual ~untyped_concept()                         = default;
    virtual auto dimensions() -> std::array<DimT, Dim> = 0;
    virtual auto contains(const pos_type& pos) -> bool = 0;
    virtual auto get(const pos_type& pos) -> get_ret   = 0;
    virtual auto clone() const -> std::unique_ptr<untyped_concept> { std::unreachable(); }
    virtual auto dimensions_c() const -> std::array<DimT, Dim> { std::unreachable(); }
    virtual auto contains_c(const pos_type& pos) const -> bool { std::unreachable(); }
    virtual auto get_c(const pos_type& pos) const -> get_const_ret { std::unreachable(); }
    virtual auto iter_pos() -> utils::input_iterable<pos_type> { std::unreachable(); }
    virtual auto iter_pos_c() const -> utils::input_iterable<pos_type> { std::unreachable(); }
    virtual auto iter_cells() -> utils::input_iterable<get_unsafe_ret> { std::unreachable(); }
    virtual auto iter_cells_c() const -> utils::input_iterable<get_unsafe_const_ret> { std::unreachable(); }
    virtual auto iter() -> utils::input_iterable<std::tuple<pos_type, get_unsafe_ret>> { std::unreachable(); }
    virtual auto iter_c() const -> utils::input_iterable<std::tuple<pos_type, get_unsafe_const_ret>> {
        std::unreachable();
    }
    virtual auto set(const pos_type& pos, cell_type val)
        -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        std::unreachable();
    }
    virtual auto remove(const pos_type& pos) -> std::expected<void, grid_error> { std::unreachable(); }
    virtual auto take(const pos_type& pos) -> std::expected<cell_type, grid_error> { std::unreachable(); }
    virtual auto clear() -> void { std::unreachable(); }
    virtual auto get_unsafe(const pos_type& pos) -> get_unsafe_ret { std::unreachable(); }
    virtual auto get_unsafe_c(const pos_type& pos) const -> get_unsafe_const_ret { std::unreachable(); }
    virtual auto set_unsafe(const pos_type& pos, cell_type val) -> get_unsafe_ret { std::unreachable(); }
    virtual auto remove_unsafe(const pos_type& pos) -> void { std::unreachable(); }
    virtual auto take_unsafe(const pos_type& pos) -> cell_type { std::unreachable(); }
};
}  // namespace detail

// ============================================================
// untyped_grid — type-erased owning grid
// ============================================================

export template <std::size_t Dim,
                 typename GetType,
                 grid_category Cat = grid_category::iterable | grid_category::container |
                                     grid_category::unsafe_viewable | grid_category::unsafe_container |
                                     grid_category::constness,
                 typename DimT     = std::uint32_t,
                 typename PosT     = std::int32_t>
class untyped_grid {
   public:
    using pos_type       = std::array<PosT, Dim>;
    using cell_type      = std::remove_cvref_t<GetType>;
    using get_ret        = detail::get_return_t<GetType>;
    using get_const_ret  = detail::get_const_return_t<GetType>;
    using get_unsafe_ret = GetType;
    using get_unsafe_const_ret =
        std::conditional_t<std::is_reference_v<GetType>, const std::remove_reference_t<GetType>&, GetType>;
    using value_type = cell_type;

    static constexpr grid_category category = Cat;

   private:
    using concept_t = detail::untyped_concept<Dim, GetType, DimT, PosT>;

    template <viewable_grid G>
        requires std::same_as<typename grid_trait<std::decay_t<G>>::cell_type, cell_type> &&
                 (std::tuple_size_v<typename detail::grid_pos_type<std::decay_t<G>>> == Dim) &&
                 std::same_as<typename detail::grid_dimensions_type<std::decay_t<G>>::value_type, DimT> &&
                 std::same_as<typename detail::grid_pos_type<std::decay_t<G>>::value_type, PosT> &&
                 satisfies_category<std::decay_t<G>, Cat>
    struct model_t final : concept_t {
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
        auto get(const pos_type& pos) -> get_ret override { return m_grid.get(pos); }
        auto dimensions_c() const -> std::array<DimT, Dim> override {
            if constexpr (has_category(Cat, grid_category::constness))
                return m_grid.dimensions();
            else
                std::unreachable();
        }
        auto contains_c(const pos_type& pos) const -> bool override {
            if constexpr (has_category(Cat, grid_category::constness))
                return m_grid.contains(pos);
            else
                std::unreachable();
        }
        auto get_c(const pos_type& pos) const -> get_const_ret override {
            if constexpr (has_category(Cat, grid_category::constness))
                return m_grid.get(pos);
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
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::constness))
                return m_grid.iter_pos();
            else
                std::unreachable();
        }
        auto iter_cells() -> utils::input_iterable<get_unsafe_ret> override {
            if constexpr (has_category(Cat, grid_category::iterable))
                return m_grid.iter_cells();
            else
                std::unreachable();
        }
        auto iter_cells_c() const -> utils::input_iterable<get_unsafe_const_ret> override {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::constness))
                return m_grid.iter_cells();
            else
                std::unreachable();
        }
        auto iter() -> utils::input_iterable<std::tuple<pos_type, get_unsafe_ret>> override {
            if constexpr (has_category(Cat, grid_category::iterable))
                return m_grid.iter();
            else
                std::unreachable();
        }
        auto iter_c() const -> utils::input_iterable<std::tuple<pos_type, get_unsafe_const_ret>> override {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::constness))
                return m_grid.iter();
            else
                std::unreachable();
        }
        auto set(const pos_type& pos, cell_type val)
            -> std::expected<std::reference_wrapper<cell_type>, grid_error> override {
            if constexpr (has_category(Cat, grid_category::container))
                return m_grid.set(pos, std::move(val));
            else
                std::unreachable();
        }
        auto remove(const pos_type& pos) -> std::expected<void, grid_error> override {
            if constexpr (has_category(Cat, grid_category::container))
                return m_grid.remove(pos);
            else
                std::unreachable();
        }
        auto take(const pos_type& pos) -> std::expected<cell_type, grid_error> override {
            if constexpr (has_category(Cat, grid_category::container))
                return m_grid.take(pos);
            else
                std::unreachable();
        }
        auto clear() -> void override {
            if constexpr (has_category(Cat, grid_category::container))
                m_grid.clear();
            else
                std::unreachable();
        }
        auto get_unsafe(const pos_type& pos) -> get_unsafe_ret override {
            if constexpr (has_category(Cat, grid_category::unsafe_viewable))
                return m_grid.get_unsafe(pos);
            else
                std::unreachable();
        }
        auto get_unsafe_c(const pos_type& pos) const -> get_unsafe_const_ret override {
            if constexpr (has_category(Cat, grid_category::unsafe_viewable | grid_category::constness))
                return m_grid.get_unsafe(pos);
            else
                std::unreachable();
        }
        auto set_unsafe(const pos_type& pos, cell_type val) -> get_unsafe_ret override {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                return m_grid.set_unsafe(pos, std::move(val));
            else
                std::unreachable();
        }
        auto remove_unsafe(const pos_type& pos) -> void override {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                m_grid.remove_unsafe(pos);
            else
                std::unreachable();
        }
        auto take_unsafe(const pos_type& pos) -> cell_type override {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                return m_grid.take_unsafe(pos);
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

    template <viewable_grid G>
        requires std::same_as<typename std::decay_t<G>::cell_type, cell_type> &&
                 (std::tuple_size_v<typename std::decay_t<G>::pos_type> == Dim) &&
                 std::same_as<typename detail::grid_dimensions_type<G>::value_type, DimT> &&
                 std::same_as<typename detail::grid_pos_type<G>::value_type, PosT> &&
                 (!detail::is_untyped_grid<std::remove_cvref_t<G>>::value) &&
                 satisfies_category<std::remove_cvref_t<G>, Cat>
    untyped_grid(G&& grid) : m_impl(std::make_unique<model_t<std::remove_cvref_t<G>>>(std::forward<G>(grid))) {}

    /** @brief Convert from another untyped_grid with a superset of this category. */
    template <grid_category OtherCat>
        requires((static_cast<unsigned>(OtherCat) & static_cast<unsigned>(Cat)) == static_cast<unsigned>(Cat))
    untyped_grid(untyped_grid<Dim, GetType, OtherCat, DimT, PosT>&& other) noexcept : m_impl(std::move(other.m_impl)) {}

    template <std::size_t, typename, grid_category, typename, typename>
    friend class untyped_grid;

    // ─── viewable_grid (always) ────────────────────────────────
    auto dimensions() -> std::array<DimT, Dim> { return m_impl->dimensions(); }
    auto contains(const pos_type& pos) -> bool { return m_impl->contains(pos); }
    auto get(const pos_type& pos) -> get_ret { return m_impl->get(pos); }

    auto dimensions() const -> std::array<DimT, Dim>
        requires(has_category(Cat, grid_category::constness))
    {
        return m_impl->dimensions_c();
    }
    auto contains(const pos_type& pos) const -> bool
        requires(has_category(Cat, grid_category::constness))
    {
        return m_impl->contains_c(pos);
    }
    auto get(const pos_type& pos) const -> get_const_ret
        requires(has_category(Cat, grid_category::constness))
    {
        return m_impl->get_c(pos);
    }

    // ─── iterable_grid ─────────────────────────────────────────
    auto iter_pos() -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl->iter_pos();
    }
    auto iter_cells() -> utils::input_iterable<cell_type&>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl->iter_cells();
    }
    auto iter() -> utils::input_iterable<std::tuple<pos_type, cell_type&>>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl->iter();
    }
    auto iter_pos() const -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_impl->iter_pos_c();
    }
    auto iter_cells() const -> utils::input_iterable<const cell_type&>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_impl->iter_cells_c();
    }
    auto iter() const -> utils::input_iterable<std::tuple<pos_type, const cell_type&>>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_impl->iter_c();
    }

    // ─── grid_container ────────────────────────────────────────
    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl->set(pos, std::move(val));
    }
    template <typename... Args>
        requires std::constructible_from<cell_type, Args...>
    auto set_new(const pos_type& pos, Args&&... args) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return set(pos, cell_type(std::forward<Args>(args)...));
    }
    auto remove(const pos_type& pos) -> std::expected<void, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl->remove(pos);
    }
    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl->take(pos);
    }
    auto clear() -> void
        requires(has_category(Cat, grid_category::container))
    {
        m_impl->clear();
    }

    // ─── unsafe_viewable_grid ──────────────────────────────────
    auto get_unsafe(const pos_type& pos) -> cell_type&
        requires(has_category(Cat, grid_category::unsafe_viewable))
    {
        return m_impl->get_unsafe(pos);
    }
    auto get_unsafe(const pos_type& pos) const -> const cell_type&
        requires(has_category(Cat, grid_category::unsafe_viewable | grid_category::constness))
    {
        return m_impl->get_unsafe_c(pos);
    }

    // ─── unsafe_grid_container ─────────────────────────────────
    auto set_unsafe(const pos_type& pos, cell_type val) -> cell_type&
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        return m_impl->set_unsafe(pos, std::move(val));
    }
    auto remove_unsafe(const pos_type& pos) -> void
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        m_impl->remove_unsafe(pos);
    }
    auto take_unsafe(const pos_type& pos) -> cell_type
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        return m_impl->take_unsafe(pos);
    }
};

export template <viewable_grid G>
untyped_grid(G&&) -> untyped_grid<std::tuple_size_v<typename std::decay_t<G>::pos_type>,
                                  detail::grid_get_type<std::remove_cvref_t<G>>,
                                  detail::default_category_for<std::remove_cvref_t<G>>(),
                                  typename detail::grid_dimensions_type<std::remove_cvref_t<G>>::value_type,
                                  typename detail::grid_pos_type<std::remove_cvref_t<G>>::value_type>;

// ============================================================
// untyped_grid_view — type-erased non-owning grid view
// ============================================================

export template <std::size_t Dim,
                 typename GetType,
                 grid_category Cat = grid_category::none,
                 typename DimT     = std::uint32_t,
                 typename PosT     = std::int32_t>
class untyped_grid_view {
   public:
    using pos_type       = std::array<PosT, Dim>;
    using cell_type      = std::remove_cvref_t<GetType>;
    using get_ret        = detail::get_return_t<GetType>;
    using get_const_ret  = detail::get_const_return_t<GetType>;
    using get_unsafe_ret = GetType;
    using get_unsafe_const_ret =
        std::conditional_t<std::is_reference_v<GetType>, const std::remove_reference_t<GetType>&, GetType>;
    using value_type = cell_type;

    static constexpr grid_category category = Cat;

   private:
    struct vtable_t {
        auto (*dimensions)(void* ptr) -> std::array<DimT, Dim>;
        bool (*contains)(void* ptr, const pos_type& pos);
        auto (*get)(void* ptr, const pos_type& pos) -> get_ret;
        auto (*dimensions_c)(void* ptr) -> std::array<DimT, Dim>;
        bool (*contains_c)(void* ptr, const pos_type& pos);
        auto (*get_c)(void* ptr, const pos_type& pos) -> get_const_ret;
        auto (*iter_pos)(void* ptr) -> utils::input_iterable<pos_type>;
        auto (*iter_cells)(void* ptr) -> utils::input_iterable<get_unsafe_ret>;
        auto (*iter)(void* ptr) -> utils::input_iterable<std::tuple<pos_type, get_unsafe_ret>>;
        auto (*iter_pos_c)(void* ptr) -> utils::input_iterable<pos_type>;
        auto (*iter_cells_c)(void* ptr) -> utils::input_iterable<get_unsafe_const_ret>;
        auto (*iter_c)(void* ptr) -> utils::input_iterable<std::tuple<pos_type, get_unsafe_const_ret>>;
        auto (*set)(void* ptr, const pos_type& pos, cell_type val)
            -> std::expected<std::reference_wrapper<cell_type>, grid_error>;
        auto (*remove)(void* ptr, const pos_type& pos) -> std::expected<void, grid_error>;
        auto (*take)(void* ptr, const pos_type& pos) -> std::expected<cell_type, grid_error>;
        auto (*clear)(void* ptr) -> void;
        auto (*get_unsafe)(void* ptr, const pos_type& pos) -> get_unsafe_ret;
        auto (*get_unsafe_c)(void* ptr, const pos_type& pos) -> get_unsafe_const_ret;
        auto (*set_unsafe)(void* ptr, const pos_type& pos, cell_type val) -> get_unsafe_ret;
        void (*remove_unsafe)(void* ptr, const pos_type& pos);
        auto (*take_unsafe)(void* ptr, const pos_type& pos) -> cell_type;
    };

    template <typename G>
        requires viewable_grid<G&> && std::same_as<detail::grid_get_type<std::remove_cvref_t<G>>, GetType> &&
                 (std::tuple_size_v<detail::grid_pos_type<std::remove_cvref_t<G>>> == Dim) &&
                 std::same_as<detail::grid_cell_type<std::remove_cvref_t<G>>, cell_type> &&
                 std::same_as<typename detail::grid_dimensions_type<std::remove_cvref_t<G>>::value_type, DimT> &&
                 std::same_as<typename detail::grid_pos_type<std::remove_cvref_t<G>>::value_type, PosT> &&
                 (!detail::is_untyped_grid_view<std::remove_cvref_t<G>>::value) &&
                 satisfies_category<std::remove_cvref_t<G>, Cat>
    static constexpr vtable_t s_vtable{
        .dimensions   = [](void* ptr) -> std::array<DimT, Dim> { return static_cast<G*>(ptr)->dimensions(); },
        .contains     = [](void* ptr, const pos_type& pos) -> bool { return static_cast<G*>(ptr)->contains(pos); },
        .get          = [](void* ptr, const pos_type& pos) -> get_ret { return static_cast<G*>(ptr)->get(pos); },
        .dimensions_c = [](void* ptr) -> std::array<DimT, Dim> {
            if constexpr (has_category(Cat, grid_category::constness))
                return static_cast<const G*>(ptr)->dimensions();
            else
                std::unreachable();
        },
        .contains_c = [](void* ptr, const pos_type& pos) -> bool {
            if constexpr (has_category(Cat, grid_category::constness))
                return static_cast<const G*>(ptr)->contains(pos);
            else
                std::unreachable();
        },
        .get_c = [](void* ptr, const pos_type& pos) -> get_const_ret {
            if constexpr (has_category(Cat, grid_category::constness))
                return static_cast<const G*>(ptr)->get(pos);
            else
                std::unreachable();
        },
        .iter_pos = [](void* ptr) -> utils::input_iterable<pos_type> {
            if constexpr (has_category(Cat, grid_category::iterable))
                return static_cast<G*>(ptr)->iter_pos();
            else
                std::unreachable();
        },
        .iter_cells = [](void* ptr) -> utils::input_iterable<get_unsafe_ret> {
            if constexpr (has_category(Cat, grid_category::iterable))
                return static_cast<G*>(ptr)->iter_cells();
            else
                std::unreachable();
        },
        .iter = [](void* ptr) -> utils::input_iterable<std::tuple<pos_type, get_unsafe_ret>> {
            if constexpr (has_category(Cat, grid_category::iterable))
                return static_cast<G*>(ptr)->iter();
            else
                std::unreachable();
        },
        .iter_pos_c = [](void* ptr) -> utils::input_iterable<pos_type> {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::constness))
                return static_cast<const G*>(ptr)->iter_pos();
            else
                std::unreachable();
        },
        .iter_cells_c = [](void* ptr) -> utils::input_iterable<get_unsafe_const_ret> {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::constness))
                return static_cast<const G*>(ptr)->iter_cells();
            else
                std::unreachable();
        },
        .iter_c = [](void* ptr) -> utils::input_iterable<std::tuple<pos_type, get_unsafe_const_ret>> {
            if constexpr (has_category(Cat, grid_category::iterable | grid_category::constness))
                return static_cast<const G*>(ptr)->iter();
            else
                std::unreachable();
        },
        .set = [](void* ptr,
                  const pos_type& pos,
                  cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
            if constexpr (has_category(Cat, grid_category::container))
                return static_cast<G*>(ptr)->set(pos, std::move(val));
            else
                std::unreachable();
        },
        .remove = [](void* ptr, const pos_type& pos) -> std::expected<void, grid_error> {
            if constexpr (has_category(Cat, grid_category::container))
                return static_cast<G*>(ptr)->remove(pos);
            else
                std::unreachable();
        },
        .take = [](void* ptr, const pos_type& pos) -> std::expected<cell_type, grid_error> {
            if constexpr (has_category(Cat, grid_category::container))
                return static_cast<G*>(ptr)->take(pos);
            else
                std::unreachable();
        },
        .clear =
            [](void* ptr) {
                if constexpr (has_category(Cat, grid_category::container))
                    static_cast<G*>(ptr)->clear();
                else
                    std::unreachable();
            },
        .get_unsafe = [](void* ptr, const pos_type& pos) -> get_unsafe_ret {
            if constexpr (has_category(Cat, grid_category::unsafe_viewable))
                return static_cast<G*>(ptr)->get_unsafe(pos);
            else
                std::unreachable();
        },
        .get_unsafe_c = [](void* ptr, const pos_type& pos) -> get_unsafe_const_ret {
            if constexpr (has_category(Cat, grid_category::unsafe_viewable | grid_category::constness))
                return static_cast<const G*>(ptr)->get_unsafe(pos);
            else
                std::unreachable();
        },
        .set_unsafe = [](void* ptr, const pos_type& pos, cell_type val) -> get_unsafe_ret {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                return static_cast<G*>(ptr)->set_unsafe(pos, std::move(val));
            else
                std::unreachable();
        },
        .remove_unsafe =
            [](void* ptr, const pos_type& pos) {
                if constexpr (has_category(Cat, grid_category::unsafe_container))
                    static_cast<G*>(ptr)->remove_unsafe(pos);
                else
                    std::unreachable();
            },
        .take_unsafe = [](void* ptr, const pos_type& pos) -> cell_type {
            if constexpr (has_category(Cat, grid_category::unsafe_container))
                return static_cast<G*>(ptr)->take_unsafe(pos);
            else
                std::unreachable();
        },
    };

    void* m_ptr          = nullptr;
    const vtable_t* m_vt = nullptr;

   public:
    // untyped_grid_view() noexcept = default;

    template <viewable_grid G>
        requires std::same_as<detail::grid_get_type<G>, GetType> &&
                 (std::tuple_size_v<detail::grid_pos_type<G>> == Dim) &&
                 std::same_as<detail::grid_cell_type<G>, cell_type> &&
                 std::same_as<typename detail::grid_dimensions_type<G>::value_type, DimT> &&
                 std::same_as<typename detail::grid_pos_type<G>::value_type, PosT> &&
                 (!detail::is_untyped_grid_view<std::remove_cvref_t<G>>::value) && satisfies_category<G, Cat>
    untyped_grid_view(G& grid) noexcept : m_vt(&s_vtable<G>) {
        if constexpr (std::is_const_v<G>)
            m_ptr = const_cast<void*>(static_cast<const void*>(std::addressof(grid)));
        else
            m_ptr = static_cast<void*>(std::addressof(grid));
    }

    /** @brief Convert from another untyped_grid_view with a superset of this category. */
    template <grid_category OtherCat>
        requires((static_cast<unsigned>(OtherCat) & static_cast<unsigned>(Cat)) == static_cast<unsigned>(Cat))
    untyped_grid_view(const untyped_grid_view<Dim, GetType, OtherCat, DimT, PosT>& other) noexcept
        : m_ptr(other.m_ptr), m_vt(other.m_vt) {}

    template <std::size_t, typename, grid_category, typename, typename>
    friend class untyped_grid_view;

    // ─── viewable_grid (always) ────────────────────────────────
    auto dimensions() -> std::array<DimT, Dim> { return m_vt->dimensions(m_ptr); }
    auto contains(const pos_type& pos) -> bool { return m_vt && m_vt->contains(m_ptr, pos); }
    auto get(const pos_type& pos) -> get_ret { return m_vt->get(m_ptr, pos); }

    auto dimensions() const -> std::array<DimT, Dim>
        requires(has_category(Cat, grid_category::constness))
    {
        return m_vt->dimensions_c(m_ptr);
    }
    auto contains(const pos_type& pos) const -> bool
        requires(has_category(Cat, grid_category::constness))
    {
        return m_vt->contains_c(m_ptr, pos);
    }

    auto get(const pos_type& pos) const -> get_const_ret
        requires(has_category(Cat, grid_category::constness))
    {
        return m_vt->get_c(m_ptr, pos);
    }

    // ─── iterable_grid ─────────────────────────────────────────
    auto iter_pos() -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_vt->iter_pos(m_ptr);
    }
    auto iter_cells() -> utils::input_iterable<get_unsafe_ret>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_vt->iter_cells(m_ptr);
    }
    auto iter() -> utils::input_iterable<std::tuple<pos_type, get_unsafe_ret>>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_vt->iter(m_ptr);
    }
    auto iter_pos() const -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_vt->iter_pos_c(m_ptr);
    }
    auto iter_cells() const -> utils::input_iterable<get_unsafe_const_ret>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_vt->iter_cells_c(m_ptr);
    }
    auto iter() const -> utils::input_iterable<std::tuple<pos_type, get_unsafe_const_ret>>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_vt->iter_c(m_ptr);
    }

    // ─── grid_container ────────────────────────────────────────
    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_vt->set(m_ptr, pos, std::move(val));
    }
    auto remove(const pos_type& pos) -> std::expected<void, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_vt->remove(m_ptr, pos);
    }
    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_vt->take(m_ptr, pos);
    }
    auto clear() -> void
        requires(has_category(Cat, grid_category::container))
    {
        m_vt->clear(m_ptr);
    }

    // ─── unsafe_viewable_grid ──────────────────────────────────
    auto get_unsafe(const pos_type& pos) -> get_unsafe_ret
        requires(has_category(Cat, grid_category::unsafe_viewable))
    {
        return m_vt->get_unsafe(m_ptr, pos);
    }
    auto get_unsafe(const pos_type& pos) const -> get_unsafe_const_ret
        requires(has_category(Cat, grid_category::unsafe_viewable | grid_category::constness))
    {
        return m_vt->get_unsafe_c(m_ptr, pos);
    }

    // ─── unsafe_grid_container ─────────────────────────────────
    auto set_unsafe(const pos_type& pos, cell_type val) -> get_unsafe_ret
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        return m_vt->set_unsafe(m_ptr, pos, std::move(val));
    }
    auto remove_unsafe(const pos_type& pos) -> void
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (m_vt) m_vt->remove_unsafe(m_ptr, pos);
    }
    auto take_unsafe(const pos_type& pos) -> cell_type
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        return m_vt->take_unsafe(m_ptr, pos);
    }
};

export template <viewable_grid G>
untyped_grid_view(G&&) -> untyped_grid_view<std::tuple_size_v<detail::grid_pos_type<std::remove_cvref_t<G>>>,
                                            detail::grid_get_type<std::remove_cvref_t<G>>,
                                            detail::default_category_for<std::remove_cvref_t<G>>(),
                                            typename detail::grid_dimensions_type<std::remove_cvref_t<G>>::value_type,
                                            typename detail::grid_pos_type<std::remove_cvref_t<G>>::value_type>;

}  // namespace epix::ext::grid
