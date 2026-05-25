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
    return (static_cast<unsigned>(val & flag)) != 0;
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

template <typename GetType>
struct cell_type_from_get {
    using type = GetType;
};
template <typename Cell>
struct cell_type_from_get<Cell&> {
    using type = Cell;
};
template <typename Cell>
struct cell_type_from_get<const Cell&> {
    using type = Cell;
};
template <typename GetType>
using cell_type_from_get_t = typename cell_type_from_get<GetType>::type;

/** @brief Derive the default category from a concrete grid type G. */
template <viewable_grid G>
constexpr auto default_category_for() -> grid_category {
    grid_category cat = grid_category::none;
    if constexpr (iterable_grid<G>) cat = cat | grid_category::iterable;
    if constexpr (grid_container<G>) cat = cat | grid_category::container;
    if constexpr (unsafe_viewable_grid<G>) cat = cat | grid_category::unsafe_viewable;
    if constexpr (unsafe_grid_container<G>) cat = cat | grid_category::unsafe_container;
    if constexpr (std::copy_constructible<G>) cat = cat | grid_category::copyable;
    if constexpr (requires(G& g) {
                      g.get(
                          std::declval<const std::array<std::int32_t, std::tuple_size_v<detail::grid_pos_type<G>>>&>());
                  })
        cat = cat | grid_category::constness;
    return cat;
}
}  // namespace detail

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
    using pos_type  = std::array<PosT, Dim>;
    using cell_type = cell_type_from_get_t<GetType>;
    using get_ret   = get_return_t<GetType>;

    virtual ~untyped_concept()                                     = default;
    virtual auto clone() const -> std::unique_ptr<untyped_concept> = 0;
    virtual auto dimensions() const -> std::array<DimT, Dim>       = 0;
    virtual auto contains(const pos_type& pos) const -> bool       = 0;
    virtual auto get(const pos_type& pos) -> get_ret               = 0;
    virtual auto get_c(const pos_type& pos) const -> get_ret {
        return std::unexpected(grid_error::NotSupportedOperation);
    }
    virtual auto iter_pos() const -> utils::input_iterable<pos_type>                             = 0;
    virtual auto iter_cells_c() const -> utils::input_iterable<const cell_type&>                 = 0;
    virtual auto iter_c() const -> utils::input_iterable<std::tuple<pos_type, const cell_type&>> = 0;
    virtual auto set(const pos_type& pos, cell_type val)
        -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
        return std::unexpected(grid_error::NotSupportedOperation);
    }
    virtual auto remove(const pos_type& pos) -> std::expected<void, grid_error> {
        return std::unexpected(grid_error::NotSupportedOperation);
    }
    virtual auto take(const pos_type& pos) -> std::expected<cell_type, grid_error> {
        return std::unexpected(grid_error::NotSupportedOperation);
    }
    virtual auto clear() -> void {}
    virtual auto get_unsafe_c(const pos_type& pos) const -> const cell_type& {
        throw std::logic_error("untyped_grid: get_unsafe not supported");
    }
    virtual auto get_unsafe(const pos_type& pos) -> cell_type& {
        throw std::logic_error("untyped_grid: get_unsafe not supported");
    }
    virtual auto set_unsafe(const pos_type& pos, cell_type val) -> cell_type& {
        throw std::logic_error("untyped_grid: set_unsafe not supported");
    }
    virtual auto remove_unsafe(const pos_type& pos) -> void {}
    virtual auto take_unsafe(const pos_type& pos) -> cell_type {
        throw std::logic_error("untyped_grid: take_unsafe not supported");
    }
    virtual auto iter_cells_mut() -> utils::input_iterable<cell_type&> { return {}; }
    virtual auto iter_mut() -> utils::input_iterable<std::tuple<pos_type, cell_type&>> { return {}; }
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
    using pos_type   = std::array<PosT, Dim>;
    using cell_type  = detail::cell_type_from_get_t<GetType>;
    using get_ret    = detail::get_return_t<GetType>;
    using value_type = cell_type;

    static constexpr grid_category category = Cat;

   private:
    using concept_t = detail::untyped_concept<Dim, GetType, DimT, PosT>;

    template <viewable_grid G>
        requires std::same_as<typename std::decay_t<G>::cell_type, cell_type> &&
                 (std::tuple_size_v<typename std::decay_t<G>::pos_type> == Dim) &&
                 (std::same_as<typename detail::grid_dimensions_type<G>::value_type, DimT> || true)
    struct model_t final : concept_t {
        G m_grid;
        model_t(G grid) : m_grid(std::move(grid)) {}
        auto clone() const -> std::unique_ptr<concept_t> override {
            if constexpr (std::copy_constructible<G>)
                return std::make_unique<model_t>(m_grid);
            else
                return nullptr;
        }
        auto dimensions() const -> std::array<DimT, Dim> override { return m_grid.dimensions(); }
        auto contains(const pos_type& pos) const -> bool override { return m_grid.contains(pos); }
        auto get(const pos_type& pos) -> get_ret override {
            if constexpr (std::same_as<GetType, cell_type>) {
                auto r = m_grid.get(pos);
                if constexpr (requires { (*r).get(); })
                    return r.transform([](const auto& v) -> cell_type { return v; });
                else
                    return r;
            } else if constexpr (std::same_as<GetType, const cell_type&>) {
                return m_grid.get(pos);
            } else {
                return m_grid.get(pos);
            }
        }
        auto get_c(const pos_type& pos) const -> get_ret override {
            if constexpr (requires { std::declval<const std::decay_t<G>&>().get(std::declval<const pos_type&>()); }) {
                auto r = m_grid.get(pos);
                if constexpr (requires { (*r).get(); })
                    return r.transform([](const auto& v) -> cell_type { return v; });
                else
                    return r;
            } else {
                return std::unexpected(grid_error::NotSupportedOperation);
            }
        }
        auto iter_pos() const -> utils::input_iterable<pos_type> override {
            if constexpr (iterable_grid<G>)
                return m_grid.iter_pos();
            else
                return {};
        }
        auto iter_cells_c() const -> utils::input_iterable<const cell_type&> override {
            if constexpr (iterable_grid<G>)
                return m_grid.iter_cells();
            else
                return {};
        }
        auto iter_c() const -> utils::input_iterable<std::tuple<pos_type, const cell_type&>> override {
            if constexpr (iterable_grid<G>)
                return m_grid.iter();
            else
                return {};
        }
        auto set(const pos_type& pos, cell_type val)
            -> std::expected<std::reference_wrapper<cell_type>, grid_error> override {
            if constexpr (grid_container<G>)
                return m_grid.set(pos, std::move(val));
            else
                return std::unexpected(grid_error::NotSupportedOperation);
        }
        auto remove(const pos_type& pos) -> std::expected<void, grid_error> override {
            if constexpr (grid_container<G>)
                return m_grid.remove(pos);
            else
                return std::unexpected(grid_error::NotSupportedOperation);
        }
        auto take(const pos_type& pos) -> std::expected<cell_type, grid_error> override {
            if constexpr (grid_container<G>)
                return m_grid.take(pos);
            else
                return std::unexpected(grid_error::NotSupportedOperation);
        }
        auto clear() -> void override {
            if constexpr (grid_container<G>) m_grid.clear();
        }
        auto get_unsafe_c(const pos_type& pos) const -> const cell_type& override {
            if constexpr (unsafe_viewable_grid<G>)
                return m_grid.get_unsafe(pos);
            else
                throw std::logic_error("untyped_grid: get_unsafe not supported");
        }
        auto get_unsafe(const pos_type& pos) -> cell_type& override {
            if constexpr (requires { std::declval<std::decay_t<G>&>().get_unsafe(std::declval<const pos_type&>()); })
                return m_grid.get_unsafe(pos);
            else
                throw std::logic_error("untyped_grid: get_unsafe not supported");
        }
        auto set_unsafe(const pos_type& pos, cell_type val) -> cell_type& override {
            if constexpr (unsafe_grid_container<G>)
                return m_grid.set_unsafe(pos, std::move(val));
            else
                throw std::logic_error("untyped_grid: set_unsafe not supported");
        }
        auto remove_unsafe(const pos_type& pos) -> void override {
            if constexpr (unsafe_grid_container<G>) m_grid.remove_unsafe(pos);
        }
        auto take_unsafe(const pos_type& pos) -> cell_type override {
            if constexpr (unsafe_grid_container<G>)
                return m_grid.take_unsafe(pos);
            else
                throw std::logic_error("untyped_grid: take_unsafe not supported");
        }
        auto iter_cells_mut() -> utils::input_iterable<cell_type&> override {
            if constexpr (requires { std::declval<std::decay_t<G>&>().iter_cells(); })
                return m_grid.iter_cells();
            else
                return {};
        }
        auto iter_mut() -> utils::input_iterable<std::tuple<pos_type, cell_type&>> override {
            if constexpr (requires { std::declval<std::decay_t<G>&>().iter(); })
                return m_grid.iter();
            else
                return {};
        }
    };

    std::unique_ptr<concept_t> m_impl;

   public:
    untyped_grid() noexcept                                  = default;
    untyped_grid(untyped_grid&&) noexcept                    = default;
    auto operator=(untyped_grid&&) noexcept -> untyped_grid& = default;

    untyped_grid(const untyped_grid& other)
        requires(has_category(Cat, grid_category::copyable))
    {
        if (other.m_impl) m_impl = other.m_impl->clone();
    }
    auto operator=(const untyped_grid& other) -> untyped_grid&
        requires(has_category(Cat, grid_category::copyable))
    {
        if (this != &other) m_impl = other.m_impl ? other.m_impl->clone() : nullptr;
        return *this;
    }

    template <viewable_grid G>
        requires std::same_as<typename std::decay_t<G>::cell_type, cell_type> &&
                 (std::tuple_size_v<typename std::decay_t<G>::pos_type> == Dim) &&
                 (!detail::is_untyped_grid<std::remove_cvref_t<G>>::value)
    untyped_grid(G&& grid) : m_impl(std::make_unique<model_t<std::remove_cvref_t<G>>>(std::forward<G>(grid))) {}

    /** @brief Convert from another untyped_grid with a superset of this category. */
    template <grid_category OtherCat>
        requires((static_cast<unsigned>(OtherCat) & static_cast<unsigned>(Cat)) == static_cast<unsigned>(Cat))
    untyped_grid(untyped_grid<Dim, GetType, OtherCat, DimT, PosT>&& other) noexcept : m_impl(std::move(other.m_impl)) {}

    template <std::size_t, typename, grid_category, typename, typename>
    friend class untyped_grid;

    // ─── viewable_grid (always) ────────────────────────────────
    auto dimensions() const -> std::array<DimT, Dim> { return m_impl ? m_impl->dimensions() : std::array<DimT, Dim>{}; }
    auto contains(const pos_type& pos) const -> bool { return m_impl && m_impl->contains(pos); }
    auto get(const pos_type& pos) -> get_ret {
        return m_impl ? m_impl->get(pos) : std::unexpected(grid_error::NotSupportedOperation);
    }

    auto get(const pos_type& pos) const -> get_ret
        requires(has_category(Cat, grid_category::constness))
    {
        return m_impl ? m_impl->get_c(pos) : std::unexpected(grid_error::NotSupportedOperation);
    }

    // ─── iterable_grid ─────────────────────────────────────────
    auto iter_pos() const -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl ? m_impl->iter_pos() : utils::input_iterable<pos_type>{};
    }
    auto iter_cells() const -> utils::input_iterable<const cell_type&>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl ? m_impl->iter_cells_c() : utils::input_iterable<const cell_type&>{};
    }
    auto iter() const -> utils::input_iterable<std::tuple<pos_type, const cell_type&>>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_impl ? m_impl->iter_c() : utils::input_iterable<std::tuple<pos_type, const cell_type&>>{};
    }
    auto iter_cells() -> utils::input_iterable<cell_type&>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_impl ? m_impl->iter_cells_mut() : utils::input_iterable<cell_type&>{};
    }
    auto iter() -> utils::input_iterable<std::tuple<pos_type, cell_type&>>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_impl ? m_impl->iter_mut() : utils::input_iterable<std::tuple<pos_type, cell_type&>>{};
    }

    // ─── grid_container ────────────────────────────────────────
    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl ? m_impl->set(pos, std::move(val)) : std::unexpected(grid_error::NotSupportedOperation);
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
        return m_impl ? m_impl->remove(pos) : std::unexpected(grid_error::NotSupportedOperation);
    }
    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_impl ? m_impl->take(pos) : std::unexpected(grid_error::NotSupportedOperation);
    }
    auto clear() -> void
        requires(has_category(Cat, grid_category::container))
    {
        if (m_impl) m_impl->clear();
    }

    // ─── unsafe_viewable_grid ──────────────────────────────────
    auto get_unsafe(const pos_type& pos) const -> const cell_type&
        requires(has_category(Cat, grid_category::unsafe_viewable))
    {
        if (!m_impl) throw std::logic_error("untyped_grid: get_unsafe on empty grid");
        return m_impl->get_unsafe_c(pos);
    }
    auto get_unsafe(const pos_type& pos) -> cell_type&
        requires(has_category(Cat, grid_category::unsafe_viewable | grid_category::constness))
    {
        if (!m_impl) throw std::logic_error("untyped_grid: get_unsafe on empty grid");
        return m_impl->get_unsafe(pos);
    }

    // ─── unsafe_grid_container ─────────────────────────────────
    auto set_unsafe(const pos_type& pos, cell_type val) -> cell_type&
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (!m_impl) throw std::logic_error("untyped_grid: set_unsafe on empty grid");
        return m_impl->set_unsafe(pos, std::move(val));
    }
    auto remove_unsafe(const pos_type& pos) -> void
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (m_impl) m_impl->remove_unsafe(pos);
    }
    auto take_unsafe(const pos_type& pos) -> cell_type
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (!m_impl) throw std::logic_error("untyped_grid: take_unsafe on empty grid");
        return m_impl->take_unsafe(pos);
    }

    explicit operator bool() const noexcept { return m_impl != nullptr; }
};

export template <viewable_grid G>
untyped_grid(G&&) -> untyped_grid<std::tuple_size_v<typename std::decay_t<G>::pos_type>,
                                  detail::grid_get_type<std::remove_cvref_t<G>>,
                                  detail::default_category_for<std::remove_cvref_t<G>>()>;

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
    using pos_type   = std::array<PosT, Dim>;
    using cell_type  = detail::cell_type_from_get_t<GetType>;
    using get_ret    = detail::get_return_t<GetType>;
    using value_type = cell_type;

    static constexpr grid_category category = Cat;

   private:
    struct vtable_t {
        auto (*dimensions)(const void* ptr) -> std::array<DimT, Dim>;
        bool (*contains)(const void* ptr, const pos_type& pos);
        auto (*get)(const void* ptr, const pos_type& pos) -> get_ret;
        auto (*get_c)(const void* ptr, const pos_type& pos) -> get_ret;
        auto (*iter_pos)(const void* ptr) -> utils::input_iterable<pos_type>;
        auto (*iter_cells_c)(const void* ptr) -> utils::input_iterable<const cell_type&>;
        auto (*iter_c)(const void* ptr) -> utils::input_iterable<std::tuple<pos_type, const cell_type&>>;
        auto (*set)(const void* ptr, const pos_type& pos, cell_type val)
            -> std::expected<std::reference_wrapper<cell_type>, grid_error>;
        auto (*remove)(const void* ptr, const pos_type& pos) -> std::expected<void, grid_error>;
        auto (*take)(const void* ptr, const pos_type& pos) -> std::expected<cell_type, grid_error>;
        auto (*get_unsafe_c)(const void* ptr, const pos_type& pos) -> const cell_type&;
        auto (*get_unsafe)(const void* ptr, const pos_type& pos) -> cell_type&;
        auto (*set_unsafe)(const void* ptr, const pos_type& pos, cell_type val) -> cell_type&;
        void (*remove_unsafe)(const void* ptr, const pos_type& pos);
        auto (*take_unsafe)(const void* ptr, const pos_type& pos) -> cell_type;
        auto (*iter_cells_mut)(const void* ptr) -> utils::input_iterable<cell_type&>;
        auto (*iter_mut)(const void* ptr) -> utils::input_iterable<std::tuple<pos_type, cell_type&>>;
    };

    template <viewable_grid G>
        requires std::same_as<detail::grid_get_type<std::remove_cvref_t<G>>, GetType> &&
                 (std::tuple_size_v<detail::grid_pos_type<std::remove_cvref_t<G>>> == Dim) &&
                 std::same_as<detail::grid_cell_type<std::remove_cvref_t<G>>, cell_type>
    static constexpr vtable_t s_vtable{
        .dimensions = [](const void* ptr) -> std::array<DimT, Dim> {
            return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->dimensions();
        },
        .contains = [](const void* ptr, const pos_type& pos) -> bool {
            return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->contains(pos);
        },
        .get = [](const void* ptr, const pos_type& pos) -> get_ret {
            if constexpr (std::same_as<GetType, cell_type&>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->get(pos);
            else if constexpr (std::same_as<GetType, const cell_type&>)
                return static_cast<const std::decay_t<G>*>(ptr)->get(pos);
            else {
                auto r = static_cast<const std::decay_t<G>*>(ptr)->get(pos);
                if constexpr (requires { (*r).get(); })
                    return r.transform([](const auto& v) -> cell_type { return v; });
                else
                    return r;
            }
        },
        .iter_pos = [](const void* ptr) -> utils::input_iterable<pos_type> {
            return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->iter_pos();
        },
        .iter_cells_c = [](const void* ptr) -> utils::input_iterable<const cell_type&> {
            return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->iter_cells();
        },
        .iter_c = [](const void* ptr) -> utils::input_iterable<std::tuple<pos_type, const cell_type&>> {
            return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->iter();
        },
        .get_c = [](const void* ptr, const pos_type& pos) -> get_ret {
            if constexpr (requires { std::declval<const std::decay_t<G>&>().get(std::declval<const pos_type&>()); }) {
                auto r = static_cast<const std::decay_t<G>*>(ptr)->get(pos);
                if constexpr (requires { (*r).get(); })
                    return r.transform([](const auto& v) -> cell_type { return v; });
                else
                    return r;
            } else {
                return std::unexpected(grid_error::NotSupportedOperation);
            }
        },
        .set = [](const void* ptr,
                  const pos_type& pos,
                  cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error> {
            if constexpr (grid_container<G>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->set(pos, std::move(val));
            else
                return std::unexpected(grid_error::NotSupportedOperation);
        },
        .remove = [](const void* ptr, const pos_type& pos) -> std::expected<void, grid_error> {
            if constexpr (grid_container<G>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->remove(pos);
            else
                return std::unexpected(grid_error::NotSupportedOperation);
        },
        .take = [](const void* ptr, const pos_type& pos) -> std::expected<cell_type, grid_error> {
            if constexpr (grid_container<G>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->take(pos);
            else
                return std::unexpected(grid_error::NotSupportedOperation);
        },
        .get_unsafe_c = [](const void* ptr, const pos_type& pos) -> const cell_type& {
            if constexpr (unsafe_viewable_grid<G>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->get_unsafe(pos);
            else
                throw std::logic_error("untyped_grid_view: get_unsafe not supported");
        },
        .get_unsafe = [](const void* ptr, const pos_type& pos) -> cell_type& {
            if constexpr (requires { std::declval<std::decay_t<G>&>().get_unsafe(std::declval<const pos_type&>()); })
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->get_unsafe(pos);
            else
                throw std::logic_error("untyped_grid_view: get_unsafe not supported");
        },
        .set_unsafe = [](const void* ptr, const pos_type& pos, cell_type val) -> cell_type& {
            if constexpr (unsafe_grid_container<G>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->set_unsafe(pos, std::move(val));
            else
                throw std::logic_error("untyped_grid_view: set_unsafe not supported");
        },
        .remove_unsafe = [](const void* ptr, const pos_type& pos) -> void {
            if constexpr (unsafe_grid_container<G>)
                static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->remove_unsafe(pos);
        },
        .take_unsafe = [](const void* ptr, const pos_type& pos) -> cell_type {
            if constexpr (unsafe_grid_container<G>)
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->take_unsafe(pos);
            else
                throw std::logic_error("untyped_grid_view: take_unsafe not supported");
        },
        .iter_cells_mut = [](const void* ptr) -> utils::input_iterable<cell_type&> {
            if constexpr (requires { std::declval<std::decay_t<G>&>().iter_cells(); })
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->iter_cells();
            else
                return {};
        },
        .iter_mut = [](const void* ptr) -> utils::input_iterable<std::tuple<pos_type, cell_type&>> {
            if constexpr (requires { std::declval<std::decay_t<G>&>().iter(); })
                return static_cast<std::decay_t<G>*>(const_cast<void*>(ptr))->iter();
            else
                return {};
        },
    };

    const void* m_ptr    = nullptr;
    const vtable_t* m_vt = nullptr;

   public:
    untyped_grid_view() noexcept = default;

    template <viewable_grid G>
        requires std::same_as<detail::grid_get_type<std::remove_cvref_t<G>>, GetType> &&
                     (std::tuple_size_v<detail::grid_pos_type<std::remove_cvref_t<G>>> == Dim) &&
                     std::same_as<detail::grid_cell_type<std::remove_cvref_t<G>>, cell_type> &&
                     (!detail::is_untyped_grid_view<std::remove_cvref_t<G>>::value)
    untyped_grid_view(G&& grid) noexcept : m_ptr(std::addressof(grid)), m_vt(&s_vtable<std::remove_cvref_t<G>>) {}

    /** @brief Convert from another untyped_grid_view with a superset of this category. */
    template <grid_category OtherCat>
        requires((static_cast<unsigned>(OtherCat) & static_cast<unsigned>(Cat)) == static_cast<unsigned>(Cat))
    untyped_grid_view(const untyped_grid_view<Dim, GetType, OtherCat, DimT, PosT>& other) noexcept
        : m_ptr(other.m_ptr), m_vt(other.m_vt) {}

    template <std::size_t, typename, grid_category, typename, typename>
    friend class untyped_grid_view;

    // ─── viewable_grid (always) ────────────────────────────────
    auto dimensions() const -> std::array<DimT, Dim> {
        return m_vt ? m_vt->dimensions(m_ptr) : std::array<DimT, Dim>{};
    }
    auto contains(const pos_type& pos) const -> bool { return m_vt && m_vt->contains(m_ptr, pos); }
    auto get(const pos_type& pos) -> get_ret {
        return m_vt ? m_vt->get(const_cast<void*>(m_ptr), pos) : std::unexpected(grid_error::NotSupportedOperation);
    }

    auto get(const pos_type& pos) const -> get_ret
        requires(has_category(Cat, grid_category::constness))
    {
        return m_vt ? m_vt->get_c(m_ptr, pos) : std::unexpected(grid_error::NotSupportedOperation);
    }

    // ─── iterable_grid ─────────────────────────────────────────
    auto iter_pos() const -> utils::input_iterable<pos_type>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_vt ? m_vt->iter_pos(m_ptr) : utils::input_iterable<pos_type>{};
    }
    auto iter_cells() const -> utils::input_iterable<const cell_type&>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_vt ? m_vt->iter_cells_c(m_ptr) : utils::input_iterable<const cell_type&>{};
    }
    auto iter() const -> utils::input_iterable<std::tuple<pos_type, const cell_type&>>
        requires(has_category(Cat, grid_category::iterable))
    {
        return m_vt ? m_vt->iter_c(m_ptr) : utils::input_iterable<std::tuple<pos_type, const cell_type&>>{};
    }
    auto iter_cells() -> utils::input_iterable<cell_type&>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_vt ? m_vt->iter_cells_mut(const_cast<void*>(m_ptr)) : utils::input_iterable<cell_type&>{};
    }
    auto iter() -> utils::input_iterable<std::tuple<pos_type, cell_type&>>
        requires(has_category(Cat, grid_category::iterable | grid_category::constness))
    {
        return m_vt ? m_vt->iter_mut(const_cast<void*>(m_ptr))
                    : utils::input_iterable<std::tuple<pos_type, cell_type&>>{};
    }

    // ─── grid_container ────────────────────────────────────────
    auto set(const pos_type& pos, cell_type val) -> std::expected<std::reference_wrapper<cell_type>, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_vt ? m_vt->set(const_cast<void*>(m_ptr), pos, std::move(val))
                    : std::unexpected(grid_error::NotSupportedOperation);
    }
    auto remove(const pos_type& pos) -> std::expected<void, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_vt ? m_vt->remove(const_cast<void*>(m_ptr), pos) : std::unexpected(grid_error::NotSupportedOperation);
    }
    auto take(const pos_type& pos) -> std::expected<cell_type, grid_error>
        requires(has_category(Cat, grid_category::container))
    {
        return m_vt ? m_vt->take(const_cast<void*>(m_ptr), pos) : std::unexpected(grid_error::NotSupportedOperation);
    }

    // ─── unsafe_viewable_grid ──────────────────────────────────
    auto get_unsafe(const pos_type& pos) const -> const cell_type&
        requires(has_category(Cat, grid_category::unsafe_viewable))
    {
        if (!m_vt) throw std::logic_error("untyped_grid_view: get_unsafe on empty view");
        return m_vt->get_unsafe_c(m_ptr, pos);
    }
    auto get_unsafe(const pos_type& pos) -> cell_type&
        requires(has_category(Cat, grid_category::unsafe_viewable | grid_category::constness))
    {
        if (!m_vt) throw std::logic_error("untyped_grid_view: get_unsafe on empty view");
        return m_vt->get_unsafe(const_cast<void*>(m_ptr), pos);
    }

    // ─── unsafe_grid_container ─────────────────────────────────
    auto set_unsafe(const pos_type& pos, cell_type val) -> cell_type&
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (!m_vt) throw std::logic_error("untyped_grid_view: set_unsafe on empty view");
        return m_vt->set_unsafe(const_cast<void*>(m_ptr), pos, std::move(val));
    }
    auto remove_unsafe(const pos_type& pos) -> void
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (m_vt) m_vt->remove_unsafe(const_cast<void*>(m_ptr), pos);
    }
    auto take_unsafe(const pos_type& pos) -> cell_type
        requires(has_category(Cat, grid_category::unsafe_container))
    {
        if (!m_vt) throw std::logic_error("untyped_grid_view: take_unsafe on empty view");
        return m_vt->take_unsafe(const_cast<void*>(m_ptr), pos);
    }

    explicit operator bool() const noexcept { return m_vt != nullptr; }
};

export template <viewable_grid G>
untyped_grid_view(G&&) -> untyped_grid_view<std::tuple_size_v<detail::grid_pos_type<std::remove_cvref_t<G>>>,
                                            detail::grid_get_type<std::remove_cvref_t<G>>,
                                            detail::default_category_for<std::remove_cvref_t<G>>()>;

}  // namespace epix::ext::grid
