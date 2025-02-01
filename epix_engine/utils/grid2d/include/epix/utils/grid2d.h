#pragma once

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <earcut.hpp>
#include <format>
#include <glm/glm.hpp>
#include <numeric>
#include <stack>
#include <stdexcept>
#include <vector>

namespace mapbox {
namespace util {
template <>
struct nth<0, glm::vec2> {
    static float get(const glm::dvec2& t) { return t.x; }
};

template <>
struct nth<1, glm::vec2> {
    static float get(const glm::dvec2& t) { return t.y; }
};

template <>
struct nth<0, glm::ivec2> {
    static int get(const glm::dvec2& t) { return t.x; }
};
template <>
struct nth<1, glm::ivec2> {
    static int get(const glm::dvec2& t) { return t.y; }
};
}  // namespace util
}  // namespace mapbox

namespace epix::utils::grid2d {
template <typename T>
concept BoolGrid = requires(T t) {
    { t.size(0) } -> std::same_as<int>;
    { t.size(1) } -> std::same_as<int>;
    { t.contains(0i32, 0i32) } -> std::same_as<bool>;
};
template <typename T, typename U>
concept Boolifiable = requires(T t, U u) {
    { u.operator()(t) } -> std::same_as<bool>;
};
template <typename T>
    requires std::convertible_to<T, bool> || requires(T t) {
        { (bool)t } -> std::same_as<bool>;
        { !t } -> std::same_as<bool>;
    }
struct boolify {
    bool operator()(const T& t) const {
        if constexpr (std::convertible_to<T, bool>) {
            return static_cast<bool>(t);
        } else {
            return (bool)t;
        }
    }
};
template <>
struct boolify<bool> {
    bool operator()(bool t) const { return t; }
};
template <typename T, size_t D>
struct sparse_grid;
template <typename T, size_t D, typename Boolify = boolify<T>>
    requires Boolifiable<T, Boolify>
struct packed_grid {
   private:
    std::array<int, D> _size;
    std::vector<T> _data;
    Boolify _boolify;

    struct view_t {
        packed_grid& _grid;
        struct iterator {
            std::array<int, D> pos;
            T* data;
            packed_grid& grid;

            iterator(packed_grid& grid, const std::array<int, D>& pos)
                : grid(grid), pos(pos) {
                if (!grid.valid(pos)) {
                    data = nullptr;
                }
                int index = pos[D - 1];
                for (int i = D - 2; i >= 0; i--) {
                    index = index * grid._size[i] + pos[i];
                }
                data = &grid._data[index];
            }
            iterator& operator++() {
                for (int i = D - 1; i >= 0; i--) {
                    if (pos[i] < size[i] - 1) {
                        pos[i]++;
                        return *this;
                    } else {
                        pos[i] = 0;
                    }
                }
                data = nullptr;
                return *this;
            }
            bool operator!=(const iterator& other) const {
                return data != other.data;
            }
            bool operator==(const iterator& other) const {
                return data == other.data;
            }
            std::pair<std::array<int, D>, T&> operator*() {
                return {pos, *data};
            }
        };

        view_t(packed_grid& grid) : _grid(grid) {}
        iterator begin() { return iterator(_grid, std::array<int, D>()); }
        iterator end() { return iterator(_grid, _grid._size); }
    };
    struct const_view_t {
        const packed_grid& _grid;
        struct iterator {
            std::array<int, D> pos;
            const T* data;
            const packed_grid& grid;

            iterator(const packed_grid& grid, const std::array<int, D>& pos)
                : grid(grid), pos(pos) {
                if (!grid.valid(pos)) {
                    data = nullptr;
                }
                int index = pos[D - 1];
                for (int i = D - 2; i >= 0; i--) {
                    index = index * grid._size[i] + pos[i];
                }
                data = &grid._data[index];
            }
            iterator& operator++() {
                for (int i = D - 1; i >= 0; i--) {
                    if (pos[i] < size[i] - 1) {
                        pos[i]++;
                        return *this;
                    } else {
                        pos[i] = 0;
                    }
                }
                data = nullptr;
                return *this;
            }
            bool operator!=(const iterator& other) const {
                return data != other.data;
            }
            bool operator==(const iterator& other) const {
                return data == other.data;
            }
            std::pair<std::array<int, D>, const T&> operator*() {
                return {pos, *data};
            }
        };

        const_view_t(const packed_grid& grid) : _grid(grid) {}
        iterator begin() { return iterator(_grid, std::array<int, D>()); }
        iterator end() { return iterator(_grid, _grid._size); }
    };

   public:
    packed_grid(const std::array<int, D>& size)
        : _size(size),
          _data(std::accumulate(
              size.begin(), size.end(), 1, std::multiplies<int>()
          )) {}
    packed_grid(const std::array<int, D>& size, const T& value)
        : _size(size),
          _data(
              std::accumulate(
                  size.begin(), size.end(), 1, std::multiplies<int>()
              ),
              value
          ) {}
    packed_grid(const packed_grid& other)
        : _size(other._size), _data(other._data) {}
    packed_grid(packed_grid&& other)
        : _size(other._size), _data(std::move(other._data)) {}
    packed_grid& operator=(const packed_grid& other) {
        _size = other._size;
        _data = other._data;
        return *this;
    }
    packed_grid& operator=(packed_grid&& other) {
        _size = other._size;
        _data = std::move(other._data);
        return *this;
    }
    void reset_at(const std::array<int, D>& pos) {
        for (int i = 0; i < D; i++) {
            if (pos[i] >= _size[i] || pos[i] < 0) return;
        }
        int index = pos[D - 1];
        for (int i = D - 2; i >= 0; i--) {
            index = index * _size[i] + pos[i];
        }
        _data[index] = T();
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    void reset(Args&&... args) {
        reset_at(std::array<int, D>{args...});
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    void emplace_at(const std::array<int, D>& pos, Args&&... args) {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i >= 0; i--) {
            index = index * _size[i] + pos[i];
        }
        _data[index] = std::move(T(std::forward<Args>(args)...));
    }
    template <typename... Args, size_t... I, size_t... J>
    void
    emplace_i(std::tuple<Args...>& args, std::index_sequence<I...>, std::index_sequence<J...>) {
        emplace_at(
            std::array<int, D>{std::get<I>(args)...},
            std::forward<std::tuple_element_t<D + J, std::tuple<Args...>>>(
                std::get<D + J>(args)
            )...
        );
    }
    template <typename... Args>
        requires(sizeof...(Args) > D)
    void emplace(Args&&... args) {
        auto tuple = std::forward_as_tuple(args...);
        emplace_i(
            tuple, std::make_index_sequence<D>(),
            std::make_index_sequence<sizeof...(Args) - D>()
        );
    }
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    bool try_emplace_at(const std::array<int, D>& pos, Args&&... args) {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i >= 0; i--) {
            index = index * _size[i] + pos[i];
        }
        if (!_boolify(_data[index])) {
            _data[index] = std::move(T(std::forward<Args>(args)...));
            return true;
        }
        return false;
    }
    template <typename... Args, size_t... I, size_t... J>
    bool
    try_emplace_i(std::tuple<Args...>& args, std::index_sequence<I...>, std::index_sequence<J...>) {
        return try_emplace_at(
            std::array<int, D>{std::get<I>(args)...},
            std::forward<std::tuple_element_t<D + J, std::tuple<Args...>>>(
                std::get<D + J>(args)
            )...
        );
    }
    template <typename... Args>
        requires(sizeof...(Args) > D)
    bool try_emplace(Args&&... args) {
        auto tuple = std::forward_as_tuple(args...);
        return try_emplace_i(
            tuple, std::make_index_sequence<D>(),
            std::make_index_sequence<sizeof...(Args) - D>()
        );
    }
    bool contains_at(const std::array<int, D>& pos) const {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i >= 0; i--) {
            index = index * _size[i] + pos[i];
        }
        return _boolify(_data[index]);
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool contains(Args&&... args) const {
        return contains_at(std::array<int, D>{args...});
    }
    bool valid_at(const std::array<int, D>& pos) const {
        for (int i = 0; i < D; i++) {
            if (pos[i] >= _size[i] || pos[i] < 0) return false;
        }
        return true;
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool valid(Args&&... args) const {
        return valid_at(std::array<int, D>{args...});
    }
    T& get_at(const std::array<int, D>& pos) {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i >= 0; i--) {
            index = index * _size[i] + pos[i];
        }
        return _data[index];
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    T& get(Args&&... args) {
        return get_at(std::array<int, D>{args...});
    }
    const T& get_at(const std::array<int, D>& pos) const {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i >= 0; i--) {
            index = index * _size[i] + pos[i];
        }
        return _data[index];
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    const T& get(Args&&... args) const {
        return get_at(std::array<int, D>{args...});
    }

    const std::array<int, D>& size() const { return _size; }
    int size(int i) const { return _size[i]; }
    const std::vector<T>& data() const { return _data; }
    std::vector<T>& data() { return _data; }

    view_t view() { return view_t(*this); }
    const_view_t view() const { return const_view_t(*this); }

    template <typename T, size_t D>
    friend struct sparse_grid;
};
template <size_t D>
struct packed_grid<bool, D, boolify<bool>> {
   private:
    std::array<int, D> _size;
    int _column;
    std::vector<uint8_t> _data;

   public:
    packed_grid(const std::array<int, D>& size)
        : _size(size),
          _column((size[0] + 7) / 8),
          _data(
              std::accumulate(
                  size.begin(), size.end(), 1, std::multiplies<int>()
              ) *
              _column
          ) {}
    packed_grid(const std::array<int, D>& size, bool value)
        : _size(size),
          _column((size[0] + 7) / 8),
          _data(
              std::accumulate(
                  size.begin(), size.end(), 1, std::multiplies<int>()
              ) * _column,
              value ? 0xff : 0
          ) {}
    packed_grid(const packed_grid& other)
        : _size(other._size), _column(other._column), _data(other._data) {}
    template <typename T, typename TB>
    packed_grid(const packed_grid<T, D, TB>& other)
        : _size(other.size()),
          _column((other.size()[0] + 7) / 8),
          _data(
              std::accumulate(
                  other.size().begin(),
                  other.size().end(),
                  1,
                  std::multiplies<int>()
              ) *
              _column
          ) {
        for (auto&& [pos, value] : other.view()) {
            insert(pos, other._boolify(value));
        }
    }
    template <BoolGrid G>
        requires(D == 2)
    packed_grid(const G& other)
        : _size({other.size(0), other.size(1)}),
          _column((other.size(0) + 7) / 8),
          _data(other.size(1) * _column) {
        for (int y = 0; y < other.size(1); y++) {
            for (int x = 0; x < other.size(0); x++) {
                insert({x, y}, other.contains(x, y));
            }
        }
    }
    packed_grid(packed_grid&& other)
        : _size(other._size),
          _column(other._column),
          _data(std::move(other._data)) {}
    packed_grid& operator=(const packed_grid& other) {
        _size   = other._size;
        _column = other._column;
        _data   = other._data;
        return *this;
    }
    packed_grid& operator=(packed_grid&& other) {
        _size   = other._size;
        _column = other._column;
        _data   = std::move(other._data);
        return *this;
    }
    bool valid_at(const std::array<int, D>& pos) const {
        for (int i = 0; i < D; i++) {
            if (pos[i] >= _size[i] || pos[i] < 0) return false;
        }
        return true;
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool valid(Args&&... args) const {
        return valid_at(std::array<int, D>{args...});
    }
    bool contains_at(const std::array<int, D>& pos) const {
        for (int i = 0; i < D; i++) {
            if (pos[i] >= _size[i] || pos[i] < 0) return false;
        }
        int index = pos[D - 1];
        for (int i = D - 2; i > 0; i--) {
            index = index * _size[i] + pos[i];
        }
        return (_data[index * _column + pos[0] / 8] & (1 << (pos[0] % 8))) != 0;
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool contains(Args&&... args) const {
        return contains_at(std::array<int, D>{args...});
    }
    void insert(const std::array<int, D>& pos, bool value) {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i > 0; i--) {
            index = index * _size[i] + pos[i];
        }
        if (value) {
            _data[index * _column + pos[0] / 8] |= 1 << (pos[0] % 8);
        } else {
            _data[index * _column + pos[0] / 8] &= ~(1 << (pos[0] % 8));
        }
    }
    void emplace_at(const std::array<int, D>& pos, bool value) {
        insert(pos, value);
    }
    template <typename... Args, size_t... I>
    void emplace_i(std::tuple<Args...>& args, std::index_sequence<I...>) {
        emplace_at(std::array<int, D>{std::get<I>(args)...}, std::get<D>(args));
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D + 1 &&
            (std::same_as<std::tuple_element_t<D, std::tuple<Args...>>, int> ||
             std::convertible_to<
                 std::tuple_element_t<D, std::tuple<Args...>>,
                 int>)
        )
    void emplace(Args&&... args) {
        auto tuple = std::forward_as_tuple(args...);
        emplace_i(tuple, std::make_index_sequence<D>());
    }
    bool try_emplace_at(const std::array<int, D>& pos, bool value) {
        assert(valid_at(pos));
        if (contains(pos)) return false;
        insert(pos, value);
        return true;
    }
    template <typename... Args, size_t... I>
    bool try_emplace_i(std::tuple<Args...>& args, std::index_sequence<I...>) {
        return try_emplace_at(
            std::array<int, D>{std::get<I>(args)...}, std::get<D>(args)
        );
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D + 1 &&
            (std::same_as<std::tuple_element_t<D, std::tuple<Args...>>, int> ||
             std::convertible_to<
                 std::tuple_element_t<D, std::tuple<Args...>>,
                 int>)
        )
    bool try_emplace(Args&&... args) {
        auto tuple = std::forward_as_tuple(args...);
        return try_emplace_i(tuple, std::make_index_sequence<D>());
    }
    void reset_at(const std::array<int, D>& pos) {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i > 0; i--) {
            index = index * _size[i] + pos[i];
        }
        _data[index * _column + pos[0] / 8] &= ~(1 << (pos[0] % 8));
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    void reset(Args&&... args) {
        reset_at(std::array<int, D>{args...});
    }
    bool get_at(const std::array<int, D>& pos) const {
        assert(valid_at(pos));
        int index = pos[D - 1];
        for (int i = D - 2; i > 0; i--) {
            index = index * _size[i] + pos[i];
        }
        return (_data[index * _column + pos[0] / 8] & (1 << (pos[0] % 8))) != 0;
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool get(Args&&... args) const {
        return get_at(std::array<int, D>{args...});
    }
    const std::array<int, D>& size() const { return _size; }
    int size(int i) const { return _size[i]; }
    const std::vector<uint8_t>& data() const { return _data; }
};
template <size_t D>
using binary_grid = packed_grid<bool, D, boolify<bool>>;
template <typename T, size_t D>
struct sparse_grid {
   private:
    struct index_boolify {
        bool operator()(int index) const { return index != -1; }
    };

    packed_grid<int, D, index_boolify> _index_grid;
    std::vector<T> _data;
    std::vector<std::array<int, D>> _pos;

    struct view_t {
        sparse_grid& _grid;
        struct iterator {
            sparse_grid& grid;
            int index;
            iterator(sparse_grid& grid, int index) : grid(grid), index(index) {}
            iterator& operator++() {
                if (index < grid._data.size()) index++;
                return *this;
            }
            bool operator!=(const iterator& other) const {
                return index != other.index || &grid != &other.grid;
            }
            bool operator==(const iterator& other) const {
                return index == other.index && &grid == &other.grid;
            }
            std::pair<std::array<int, D>, T&> operator*() {
                return {grid._pos[index], grid._data[index]};
            }
        };
        view_t(sparse_grid& grid) : _grid(grid) {}
        iterator begin() { return iterator(_grid, 0); }
        iterator end() { return iterator(_grid, _grid._data.size()); }
    };
    struct const_view_t {
        const sparse_grid& _grid;
        struct iterator {
            const sparse_grid& grid;
            int index;
            iterator(const sparse_grid& grid, int index)
                : grid(grid), index(index) {}
            iterator& operator++() {
                if (index < grid._data.size()) index++;
                return *this;
            }
            bool operator!=(const iterator& other) const {
                return index != other.index || &grid != &other.grid;
            }
            bool operator==(const iterator& other) const {
                return index == other.index && &grid == &other.grid;
            }
            std::pair<std::array<int, D>, const T&> operator*() {
                return {grid._pos[index], grid._data[index]};
            }
        };
        const_view_t(const sparse_grid& grid) : _grid(grid) {}
        iterator begin() { return iterator(_grid, 0); }
        iterator end() { return iterator(_grid, _grid._data.size()); }
    };

   public:
    sparse_grid(const std::array<int, D>& size)
        : _index_grid(size, -1), _data(), _pos() {}
    sparse_grid(const sparse_grid& other)
        : _index_grid(other._index_grid),
          _data(other._data),
          _pos(other._pos) {}
    sparse_grid(sparse_grid&& other)
        : _index_grid(std::move(other._index_grid)),
          _data(std::move(other._data)),
          _pos(std::move(other._pos)) {}
    sparse_grid& operator=(const sparse_grid& other) {
        _index_grid = other._index_grid;
        _data       = other._data;
        _pos        = other._pos;
        return *this;
    }
    sparse_grid& operator=(sparse_grid&& other) {
        _index_grid = std::move(other._index_grid);
        _data       = std::move(other._data);
        _pos        = std::move(other._pos);
        return *this;
    }
    template <typename... Args>
    void emplace_at(const std::array<int, D>& pos, Args&&... args) {
        assert(valid_at(pos));
        if (_index_grid.contains_at(pos)) {
            _data[_index_grid.get_at(pos)] = T(std::forward<Args>(args)...);
        } else {
            _index_grid.emplace_at(pos, _data.size());
            _pos.push_back(pos);
            _data.emplace_back(std::forward<Args>(args)...);
        }
    }
    template <typename... Args, size_t... I, size_t... J>
    void
    emplace_i(std::tuple<Args...>& args, std::index_sequence<I...>, std::index_sequence<J...>) {
        emplace_at(
            std::array<int, D>{std::get<I>(args)...}, std::get<J + D>(args)...
        );
    }
    template <typename... Args>
    void emplace(Args&&... args) {
        auto tuple = std::forward_as_tuple(args...);
        emplace_i(
            tuple, std::make_index_sequence<D>(),
            std::make_index_sequence<sizeof...(Args) - D>()
        );
    }
    template <typename... Args>
    bool try_emplace_at(const std::array<int, D>& pos, Args&&... args) {
        assert(valid_at(pos));
        if (_index_grid.contains_at(pos)) return false;
        _index_grid.emplace_at(pos, _data.size());
        _pos.push_back(pos);
        _data.emplace_back(std::forward<Args>(args)...);
        return true;
    }
    template <typename... Args, size_t... I, size_t... J>
    bool
    try_emplace_i(std::tuple<Args...>& args, std::index_sequence<I...>, std::index_sequence<J...>) {
        return try_emplace_at(
            std::array<int, D>{std::get<I>(args)...}, std::get<J + D>(args)...
        );
    }
    template <typename... Args>
    bool try_emplace(Args&&... args) {
        auto tuple = std::forward_as_tuple(args...);
        return try_emplace_i(
            tuple, std::make_index_sequence<D>(),
            std::make_index_sequence<sizeof...(Args) - D>()
        );
    }

    bool remove_at(const std::array<int, D>& pos) {
        if (!_index_grid.contains_at(pos)) return false;
        int index = _index_grid.get_at(pos);
        std::swap(_data[index], _data.back());
        std::swap(_pos[index], _pos.back());
        _index_grid.emplace_at(_pos[index], index);
        _data.pop_back();
        _pos.pop_back();
        _index_grid.emplace_at(pos, -1u);
        return true;
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool remove(Args&&... args) {
        return remove_at(std::array<int, D>{args...});
    }

    bool contains_at(const std::array<int, D>& pos) const {
        return _index_grid.contains_at(pos);
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool contains(Args&&... args) const {
        return _index_grid.contains_at(std::array<int, D>{args...});
    }
    bool valid_at(const std::array<int, D>& pos) const {
        return _index_grid.valid_at(pos);
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    bool valid(Args&&... args) const {
        return _index_grid.valid_at(std::array<int, D>{args...});
    }

    T& get_at(const std::array<int, D>& pos) {
        if (!_index_grid.contains_at(pos))
            throw std::out_of_range("Position empty");
        return _data[_index_grid.get_at(pos)];
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    T& get(Args&&... args) {
        return get_at(std::array<int, D>{args...});
    }
    const T& get_at(const std::array<int, D>& pos) const {
        if (!_index_grid.contains_at(pos))
            throw std::out_of_range("Position empty");
        return _data[_index_grid.get_at(pos)];
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    const T& get(Args&&... args) const {
        return get_at(std::array<int, D>{args...});
    }

    const std::array<int, D>& size() const { return _index_grid.size(); }
    int size(int i) const { return _index_grid.size(i); }
    const std::vector<T>& data() const { return _data; }
    std::vector<T>& data() { return _data; }

    void swap_at(
        const std::array<int, D>& pos1, const std::array<int, D>& pos2
    ) {
        if (!_index_grid.valid_at(pos1) || !_index_grid.valid_at(pos2)) return;
        auto index1 = _index_grid.get_at(pos1);
        auto index2 = _index_grid.get_at(pos2);
        if (index1 >= 0 && index1 < _pos.size()) {
            _pos[index1] = pos2;
        }
        if (index2 >= 0 && index2 < _pos.size()) {
            _pos[index2] = pos1;
        }
        _index_grid.emplace_at(pos1, index2);
        _index_grid.emplace_at(pos2, index1);
    }

    view_t view() { return view_t(*this); }
    const_view_t view() const { return const_view_t(*this); }
};
template <typename T, size_t D>
struct sparse_grid_with_default : public sparse_grid<T, D> {
    T _default_value;
    sparse_grid_with_default(const std::array<int, D>& size)
        : sparse_grid<T, D>(size), _default_value() {}
    sparse_grid_with_default(
        const std::array<int, D>& size, const T& default_value
    )
        : sparse_grid<T, D>(size), _default_value(default_value) {}
    sparse_grid_with_default(const sparse_grid_with_default& other)
        : sparse_grid<T, D>(other), _default_value(other._default_value) {}
    sparse_grid_with_default(sparse_grid_with_default&& other)
        : sparse_grid<T, D>(std::move(other)),
          _default_value(other._default_value) {}
    sparse_grid_with_default& operator=(const sparse_grid_with_default& other) {
        sparse_grid<T, D>::operator=(other);
        _default_value = other._default_value;
        return *this;
    }
    sparse_grid_with_default& operator=(sparse_grid_with_default&& other) {
        sparse_grid<T, D>::operator=(std::move(other));
        _default_value = other._default_value;
        return *this;
    }
    T& get_at(const std::array<int, D>& pos) {
        if (!sparse_grid<T, D>::valid_at(pos)) return _default_value;
        if (!sparse_grid<T, D>::contains_at(pos)) return _default_value;
        return sparse_grid<T, D>::get_at(pos);
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    T& get(Args&&... args) {
        return get_at(std::array<int, D>{args...});
    }
    const T& get_at(const std::array<int, D>& pos) const {
        if (!sparse_grid<T, D>::valid_at(pos)) return _default_value;
        if (!sparse_grid<T, D>::contains_at(pos)) return _default_value;
        return sparse_grid<T, D>::get_at(pos);
    }
    template <typename... Args>
        requires(
            sizeof...(Args) == D &&
            ((std::same_as<Args, int> || std::convertible_to<Args, int>) && ...)
        )
    const T& get(Args&&... args) const {
        return get_at(std::array<int, D>{args...});
    }
};

template <size_t D>
struct OpArea {
    std::array<int, D> origin1;
    std::array<int, D> origin2;
    std::array<int, D> extent;
    OpArea& setOrigin1(const std::array<int, D>& origin) {
        origin1 = origin;
        return *this;
    }
    OpArea& setOrigin2(const std::array<int, D>& origin) {
        origin2 = origin;
        return *this;
    }
    OpArea& setExtent(const std::array<int, D>& extent) {
        this->extent = extent;
        return *this;
    }
};

template <size_t D>
std::array<int, D> operator+(
    const std::array<int, D>& a, const std::array<int, D>& b
) {
    std::array<int, D> result;
    for (int i = 0; i < D; i++) {
        result[i] = a[i] + b[i];
    }
    return result;
}
template <size_t D>
std::array<int, D> operator-(
    const std::array<int, D>& a, const std::array<int, D>& b
) {
    std::array<int, D> result;
    for (int i = 0; i < D; i++) {
        result[i] = a[i] - b[i];
    }
    return result;
}
template <size_t D>
std::array<int, D> operator*(
    const std::array<int, D>& a, const std::array<int, D>& b
) {
    std::array<int, D> result;
    for (int i = 0; i < D; i++) {
        result[i] = a[i] * b[i];
    }
    return result;
}
template <size_t D>
std::array<int, D> min(
    const std::array<int, D>& a, const std::array<int, D>& b
) {
    std::array<int, D> result;
    for (int i = 0; i < D; i++) {
        result[i] = std::min(a[i], b[i]);
    }
    return result;
}
template <size_t D>
std::array<int, D> max(
    const std::array<int, D>& a, const std::array<int, D>& b
) {
    std::array<int, D> result;
    for (int i = 0; i < D; i++) {
        result[i] = std::max(a[i], b[i]);
    }
    return result;
}

template <size_t D>
std::array<int, D> array_fill(int value) {
    std::array<int, D> result;
    result.fill(value);
    return result;
}

template <typename T, typename U, typename TB, typename UB, size_t D>
    requires std::copyable<T>
packed_grid<T, D, TB> op_and(
    const packed_grid<T, D, TB>& a,
    const packed_grid<U, D, UB>& b,
    const OpArea<D>& area =
        {array_fill<D>(0), array_fill<D>(0), min(a.size(), b.size())}
) {
    packed_grid<T, TB> result(area.extent);
    std::array<int, D> pos;
    pos.fill(0);
    bool should_break = false;
    while (!should_break) {
        if (b.contains(pos + area.origin2)) {
            result.insert(pos, a.get(pos + area.origin1));
        }
        for (int i = 0; i < D; i++) {
            if (pos[i] < area.extent[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    return result;
}
template <typename U, typename UB, size_t D>
binary_grid<D> op_and(
    const binary_grid<D>& a,
    const packed_grid<U, D, UB>& b,
    const OpArea<D>& area
) {
    binary_grid<D> result(area.extent);
    std::array<int, D> pos;
    pos.fill(0);
    bool should_break = false;
    while (!should_break) {
        if (b.contains_at(pos + area.origin2)) {
            result.insert(pos, a.get_at(pos + area.origin1));
        }
        for (int i = 0; i < D; i++) {
            if (pos[i] < area.extent[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    return result;
}
template <typename T, typename U, typename TB, typename UB, size_t D>
packed_grid<T, D, TB> operator&(
    const packed_grid<T, D, TB>& a, const packed_grid<U, D, UB>& b
) {
    return op_and(
        a, b, {array_fill<D>(0), array_fill<D>(0), min(a.size(), b.size())}
    );
}
template <typename T, typename U, typename TB, typename UB, size_t D>
binary_grid<D> op_or(
    const packed_grid<T, D, TB>& a,
    const packed_grid<U, D, UB>& b,
    const OpArea<D>& area
) {
    binary_grid<D> result(area.extent);
    std::array<int, D> pos;
    pos.fill(0);
    bool should_break = false;
    while (!should_break) {
        result.insert(
            pos, a.contains_at(pos + area.origin1) ||
                     b.contains_at(pos + area.origin2)
        );
        for (int i = 0; i < D; i++) {
            if (pos[i] < area.extent[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    return result;
}
template <typename T, typename U, typename TB, typename UB, size_t D>
binary_grid<D> operator|(
    const packed_grid<T, D, TB>& a, const packed_grid<U, D, UB>& b
) {
    return op_or(
        a, b, {array_fill<D>(0), array_fill<D>(0), min(a.size(), b.size())}
    );
}
template <typename T, typename U, typename TB, typename UB, size_t D>
binary_grid<D> op_xor(
    const binary_grid<D>& a,
    const packed_grid<U, D, UB>& b,
    const OpArea<D>& area
) {
    binary_grid<D> result(area.width, area.height);
    std::array<int, D> pos;
    pos.fill(0);
    bool should_break = false;
    while (!should_break) {
        result.insert(
            pos, a.contains_at(pos + area.origin1) ^
                     b.contains_at(pos + area.origin2)
        );
        for (int i = 0; i < D; i++) {
            if (pos[i] < area.extent[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    return result;
}
template <typename T, typename U, typename TB, typename UB, size_t D>
binary_grid<D> operator^(
    const packed_grid<T, D, TB>& a, const packed_grid<U, D, UB>& b
) {
    return op_xor(
        a, b, {array_fill<D>(0), array_fill<D>(0), min(a.size(), b.size())}
    );
}
template <typename T, typename TB, size_t D>
binary_grid<D> op_not(const packed_grid<T, D, TB>& a, const OpArea<D>& area) {
    binary_grid<D> result(area.extent);
    std::array<int, D> pos;
    pos.fill(0);
    bool should_break = false;
    while (!should_break) {
        result.insert(pos, !a.contains_at(pos + area.origin1));
        for (int i = 0; i < D; i++) {
            if (pos[i] < area.extent[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    return result;
}
template <typename T, typename TB, size_t D>
binary_grid<D> operator~(const packed_grid<T, D, TB>& a) {
    return op_not(a, {array_fill<D>(0), array_fill<D>(0), a.size()});
}

using binary_grid2d = binary_grid<2>;
template <typename T, typename TB = boolify<T>>
using packed_grid2d = packed_grid<T, 2, TB>;
template <typename T>
using sparse_grid2d = sparse_grid<T, 2>;

/**
 * @brief Get the outland of a binary grid
 *
 * @param grid The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected to the
 * outland as part of the outland
 *
 * @return Grid2D<bool> The outland of the binary grid
 */
template <typename T, typename TB>
binary_grid2d get_outland(
    const packed_grid<T, 2, TB>& grid, bool include_diagonal = false
) {
    auto size  = grid.size();
    auto width = size[0], height = size[1];
    binary_grid2d outland(size);
    // use dimension first search
    std::stack<std::pair<int, int>> stack;
    for (int i = 0; i < width; i++) {
        if (!grid.contains(i, 0)) stack.push({i, 0});
        if (!grid.contains(i, height - 1)) stack.push({i, height - 1});
    }
    for (int i = 0; i < height; i++) {
        if (!grid.contains(0, i)) stack.push({0, i});
        if (!grid.contains(width - 1, i)) stack.push({width - 1, i});
    }
    while (!stack.empty()) {
        auto [x, y] = stack.top();
        stack.pop();
        if (outland.contains(x, y)) continue;
        outland.insert({x, y}, true);
        if (x > 0 && !grid.contains(x - 1, y) && !outland.contains(x - 1, y))
            stack.push({x - 1, y});
        if (x < width - 1 && !grid.contains(x + 1, y) &&
            !outland.contains(x + 1, y))
            stack.push({x + 1, y});
        if (y > 0 && !grid.contains(x, y - 1) && !outland.contains(x, y - 1))
            stack.push({x, y - 1});
        if (y < height - 1 && !grid.contains(x, y + 1) &&
            !outland.contains(x, y + 1))
            stack.push({x, y + 1});
        if (include_diagonal) {
            if (x > 0 && y > 0 && !grid.contains(x - 1, y - 1) &&
                !outland.contains(x - 1, y - 1))
                stack.push({x - 1, y - 1});
            if (x < width - 1 && y > 0 && !grid.contains(x + 1, y - 1) &&
                !outland.contains(x + 1, y - 1))
                stack.push({x + 1, y - 1});
            if (x > 0 && y < height - 1 && !grid.contains(x - 1, y + 1) &&
                !outland.contains(x - 1, y + 1))
                stack.push({x - 1, y + 1});
            if (x < width - 1 && y < height - 1 &&
                !grid.contains(x + 1, y + 1) && !outland.contains(x + 1, y + 1))
                stack.push({x + 1, y + 1});
        }
    }
    return outland;
}

/**
 * @brief Shrink a binary grid to the smallest size that contains all the
 * pixels
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * @param grid The binary grid
 * @param offset The offset of the new grid from the original grid
 *
 * @return Grid2D<bool> The shrunken grid
 */
template <typename T, typename TB, size_t D>
    requires std::copyable<T>
packed_grid<T, D, TB> shrink(
    const packed_grid<T, D, TB>& grid, std::array<int, D>* offset = nullptr
) {
    auto size              = grid.size();
    std::array<int, D> min = size;
    std::array<int, D> max = {0};
    std::array<int, D> pos;
    pos.fill(0);
    bool should_break = false;
    while (!should_break) {
        if (grid.contains(pos)) {
            for (int i = 0; i < D; i++) {
                min[i] = std::min(min[i], pos[i]);
                max[i] = std::max(max[i], pos[i]);
            }
        }
        for (int i = 0; i < D; i++) {
            if (pos[i] < size[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    for (int i = 0; i < D; i++) {
        if (min[i] > max[i]) {
            return packed_grid<T, D, TB>(array_fill<D>(0));
        }
    }
    if (offset) *offset = min;
    packed_grid<T, D, TB> result(max - min + array_fill<D>(1));
    pos.fill(0);
    should_break = false;
    while (!should_break) {
        if (grid.contains(pos + min)) {
            result.insert(pos, grid.get(pos + min));
        }
        for (int i = 0; i < D; i++) {
            if (pos[i] < result.size()[i] - 1) {
                pos[i]++;
                break;
            } else if (i < D - 1) {
                pos[i] = 0;
            } else {
                should_break = true;
            }
        }
    }
    return result;
}

/**
 * @brief Split a binary grid into multiple binary grids if there are multiple
 *
 * @param grid The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected to a
 * subset as part of the subset
 *
 * @return `std::vector<Grid2D<bool>>` The splitted binary grids
 */
template <typename T, typename TB>
    requires std::copyable<T>
std::vector<packed_grid<T, 2, TB>> split(
    const packed_grid<T, 2, TB>& grid, bool include_diagonal = false
) {
    std::vector<binary_grid2d> result_grid;
    binary_grid2d visited = get_outland(grid);
    auto size             = grid.size();
    auto width = size[0], height = size[1];
    while (true) {
        glm::ivec2 current(-1, -1);
        for (int i = 0; i < visited.size()[0]; i++) {
            for (int j = 0; j < visited.size()[1]; j++) {
                if (!visited.contains(i, j) && grid.contains(i, j)) {
                    current = {i, j};
                    break;
                }
            }
            if (current.x != -1) break;
        }
        if (current.x == -1) break;
        result_grid.emplace_back(binary_grid2d({width, height}));
        auto& current_grid = result_grid.back();
        std::stack<std::pair<int, int>> stack;
        stack.push({current.x, current.y});
        while (!stack.empty()) {
            auto [x, y] = stack.top();
            stack.pop();
            if (current_grid.contains(x, y)) continue;
            current_grid.emplace(x, y, true);
            visited.emplace(x, y, true);
            if (x > 0 && !visited.contains(x - 1, y) && grid.contains(x - 1, y))
                stack.push({x - 1, y});
            if (x < width - 1 && !visited.contains(x + 1, y) &&
                grid.contains(x + 1, y))
                stack.push({x + 1, y});
            if (y > 0 && !visited.contains(x, y - 1) && grid.contains(x, y - 1))
                stack.push({x, y - 1});
            if (y < height - 1 && !visited.contains(x, y + 1) &&
                grid.contains(x, y + 1))
                stack.push({x, y + 1});
            if (include_diagonal) {
                if (x > 0 && y > 0 && !visited.contains(x - 1, y - 1) &&
                    grid.contains(x - 1, y - 1))
                    stack.push({x - 1, y - 1});
                if (x < width - 1 && y > 0 && !visited.contains(x + 1, y - 1) &&
                    grid.contains(x + 1, y - 1))
                    stack.push({x + 1, y - 1});
                if (x > 0 && y < height - 1 &&
                    !visited.contains(x - 1, y + 1) &&
                    grid.contains(x - 1, y + 1))
                    stack.push({x - 1, y + 1});
                if (x < width - 1 && y < height - 1 &&
                    !visited.contains(x + 1, y + 1) &&
                    grid.contains(x + 1, y + 1))
                    stack.push({x + 1, y + 1});
            }
        }
    }
    std::vector<packed_grid2d<T, TB>> result;
    for (auto& grid_i : result_grid) {
        result.emplace_back(grid & grid_i);
    }
    return result;
}

/**
 * @brief Find the outline of a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The outline of the binary grid, in cw order
 */
template <BoolGrid T>
std::vector<glm::ivec2> find_outline(
    const T& pixelbin, bool include_diagonal = false
) {
    std::vector<glm::ivec2> outline;
    static constexpr std::array<glm::ivec2, 4> move = {
        glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(1, 0), glm::ivec2(0, -1)
    };
    static constexpr std::array<glm::ivec2, 4> offsets = {
        glm::ivec2{-1, -1}, glm::ivec2{-1, 0}, glm::ivec2{0, 0},
        glm::ivec2{0, -1}
    };  // if dir is i then in ccw order, i+1 is the right pixel coord, i is
        // the left pixel coord, i = 0 means left.
    glm::ivec2 start(-1, -1);
    for (int j = 0; j < pixelbin.size().y; j++) {
        for (int i = 0; i < pixelbin.size().x; i++) {
            if (pixelbin.contains(i, j)) {
                start = {i, j};
                break;
            }
        }
        if (start.x != -1) break;
    }
    if (start.x == -1) return outline;
    glm::ivec2 current = start;
    int dir            = 0;
    do {
        outline.push_back(current);
        for (int ndir = (include_diagonal ? dir + 3 : dir + 1) % 4;
             ndir != (dir + 2) % 4;
             ndir = (include_diagonal ? ndir + 1 : ndir + 3) % 4) {
            auto outside = current + offsets[ndir];
            auto inside  = current + offsets[(ndir + 1) % 4];
            if (!pixelbin.contains(outside.x, outside.y) &&
                pixelbin.contains(inside.x, inside.y)) {
                current = current + move[ndir];
                if (dir == ndir) outline.pop_back();
                dir = ndir;
                break;
            }
        }
    } while (current != start);
    return outline;
}
/**
 * @brief Find the outline of a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 * @param outline The outline of the binary grid, in cw order, this wont clear
 * the vector
 */
template <BoolGrid T>
void find_outline(
    const T& pixelbin,
    std::vector<glm::ivec2>& outline,
    bool include_diagonal = false
) {
    outline.clear();
    static constexpr std::array<glm::ivec2, 4> move = {
        glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(1, 0), glm::ivec2(0, -1)
    };
    static constexpr std::array<glm::ivec2, 4> offsets = {
        glm::ivec2{-1, -1}, glm::ivec2{-1, 0}, glm::ivec2{0, 0},
        glm::ivec2{0, -1}
    };  // if dir is i then in ccw order, i+1 is the right pixel coord, i is
        // the left pixel coord, i = 0 means left.
    glm::ivec2 start(-1, -1);
    for (int j = 0; j < pixelbin.size(1); j++) {
        for (int i = 0; i < pixelbin.size(0); i++) {
            if (pixelbin.contains(i, j)) {
                start = {i, j};
                break;
            }
        }
        if (start.x != -1) break;
    }
    if (start.x == -1) return;
    glm::ivec2 current = start;
    int dir            = 0;
    do {
        outline.push_back(current);
        for (int ndir = (include_diagonal ? dir + 3 : dir + 1) % 4;
             ndir != (dir + 2) % 4;
             ndir = (include_diagonal ? ndir + 1 : ndir + 3) % 4) {
            auto outside = current + offsets[ndir];
            auto inside  = current + offsets[(ndir + 1) % 4];
            if (!pixelbin.contains(outside.x, outside.y) &&
                pixelbin.contains(inside.x, inside.y)) {
                current = current + move[ndir];
                if (dir == ndir) outline.pop_back();
                dir = ndir;
                break;
            }
        }
    } while (current != start);
}

/**
 * @brief Find holes in a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The holes in the binary grid, in cw order
 */
template <BoolGrid T>
std::vector<std::vector<glm::ivec2>> find_holes(
    const T& pixelbin, bool include_diagonal = false
) {
    binary_grid2d grid(pixelbin);
    auto outland     = get_outland(grid, !include_diagonal);
    auto holes_solid = split(~(outland | grid), !include_diagonal);
    std::vector<std::vector<glm::ivec2>> holes;
    for (auto& hole : holes_solid) {
        holes.emplace_back(find_outline(hole, !include_diagonal));
    }
    return holes;
}
/**
 * @brief Find holes in a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 * @param holes The holes in the binary grid, in cw order, this wont clear the
 * vector
 */
template <BoolGrid T>
void find_holes(
    const T& pixelbin,
    std::vector<std::vector<glm::ivec2>>& holes,
    int start_size        = 1,
    bool include_diagonal = false
) {
    binary_grid2d grid(pixelbin);
    auto outland     = get_outland(grid, !include_diagonal);
    auto holes_solid = split(~(outland | grid), !include_diagonal);
    holes.resize(holes_solid.size() + start_size);
    for (int i = 0; i < holes_solid.size(); i++) {
        find_outline(holes_solid[i], holes[start_size + i], !include_diagonal);
    }
}

/**
 * @brief Douglas-Peucker algorithm for simplifying a line
 */
template <typename T>
    requires std::same_as<T, glm::ivec2> || std::same_as<T, glm::vec2>
std::vector<T> douglas_peucker(const std::vector<T>& points, float epsilon) {
    if (points.size() < 3) return points;
    float dmax              = 0.0f;
    int index               = 0;
    int end                 = points.size() - 1;
    auto distance_from_line = [](T l1, T l2, T p) {
        if (l1 == l2) return glm::distance(glm::vec2(l1), glm::vec2(p));
        float x1 = l1.x, y1 = l1.y, x2 = l2.x, y2 = l2.y, x = p.x, y = p.y;
        return std::abs((y2 - y1) * x - (x2 - x1) * y + x2 * y1 - y2 * x1) /
               glm::distance(glm::vec2(x1, y1), glm::vec2(x2, y2));
    };
    for (int i = 1; i < end; i++) {
        float d = distance_from_line(points[0], points[end], points[i]);
        if (d > dmax) {
            index = i;
            dmax  = d;
        }
    }
    std::vector<glm::ivec2> results;
    if (dmax > epsilon) {
        auto rec_results1 = douglas_peucker(
            std::vector<glm::ivec2>(points.begin(), points.begin() + index + 1),
            epsilon
        );
        auto rec_results2 = douglas_peucker(
            std::vector<glm::ivec2>(points.begin() + index, points.end()),
            epsilon
        );
        results.insert(
            results.end(), rec_results1.begin(), rec_results1.end() - 1
        );
        results.insert(results.end(), rec_results2.begin(), rec_results2.end());
    } else {
        results.push_back(points[0]);
        results.push_back(points[end]);
    }
    return results;
}

/**
 * @brief Get the polygon of a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The polygon of the binary grid, all in cw order
 */
template <BoolGrid T>
std::vector<std::vector<glm::ivec2>> get_polygon(
    const T& pixelbin, bool include_diagonal = false
) {
    auto outline        = find_outline(pixelbin, include_diagonal);
    auto holes          = find_holes(pixelbin, include_diagonal);
    auto earcut_polygon = std::vector<std::vector<glm::ivec2>>();
    earcut_polygon.emplace_back(std::move(outline));
    for (auto& hole : holes) {
        earcut_polygon.emplace_back(std::move(hole));
    }
    return std::move(earcut_polygon);
}
/**
 * @brief Get the polygon of a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 */
template <BoolGrid T>
bool get_polygon(
    const T& pixelbin,
    std::vector<std::vector<glm::ivec2>>& polygon,
    bool include_diagonal = false
) {
    polygon.resize(std::max(1, (int)polygon.size()));
    find_outline(pixelbin, polygon[0], include_diagonal);
    if (polygon[0].empty()) return false;
    find_holes(pixelbin, polygon, 1, include_diagonal);
    return true;
}

/**
 * @brief Get the polygons of a binary grid with multiple objects
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The polygons of the binary grid, all in cw order
 */
template <typename T>
    requires BoolGrid<T>
std::vector<std::vector<std::vector<glm::ivec2>>> get_polygon_multi(
    const T& pixelbin, bool include_diagonal = false
) {
    auto split_bin = split(Grid2D<bool>(pixelbin), include_diagonal);
    std::vector<std::vector<std::vector<glm::ivec2>>> result;
    for (auto& bin : split_bin) {
        result.emplace_back(std::move(get_polygon(bin, include_diagonal)));
    }
    return result;
}
/**
 * @brief Get the polygons of a binary grid with multiple objects
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The polygons of the binary grid, all in cw order
 */
template <typename T>
    requires BoolGrid<T>
bool get_polygon_multi(
    const T& pixelbin,
    std::vector<std::vector<std::vector<glm::ivec2>>>& polygons,
    bool include_diagonal = false
) {
    auto split_bin = split(binary_grid2d(pixelbin), include_diagonal);
    if (split_bin.empty()) return false;
    polygons.resize(split_bin.size());
    for (int i = 0; i < split_bin.size(); i++) {
        get_polygon(split_bin[i], polygons[i], include_diagonal);
    }
    return true;
}

/**
 * @brief Get the polygon of a binary grid
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param epsilon The epsilon value for the Douglas-Peucker algorithm
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The polygon of the binary grid, all in cw order
 */
template <BoolGrid T>
std::vector<std::vector<glm::ivec2>> get_polygon_simplified(
    const T& pixelbin, float epsilon = 0.5f, bool include_diagonal = false
) {
    auto outline        = find_outline(pixelbin, include_diagonal);
    auto holes          = find_holes(pixelbin, include_diagonal);
    auto earcut_polygon = std::vector<std::vector<glm::ivec2>>();
    earcut_polygon.emplace_back(std::move(douglas_peucker(outline, epsilon)));
    for (auto& hole : holes) {
        hole.push_back(hole[0]);
        earcut_polygon.emplace_back(std::move(douglas_peucker(hole, epsilon)));
    }
    return std::move(earcut_polygon);
}

/**
 * @brief Get the polygons of a binary grid with multiple objects
 *
 * @tparam T The type of the binary grid, must have a `size() -> glm::ivec2`
 * method and a `contains(int, int) -> bool` method
 *
 * @param pixelbin The binary grid
 * @param epsilon The epsilon value for the Douglas-Peucker algorithm
 * @param include_diagonal Whether to include diagonal pixels connected
 * as part of the target obj
 *
 * @return The polygons of the binary grid, all in cw order
 */
template <typename T>
    requires BoolGrid<T>
std::vector<std::vector<std::vector<glm::ivec2>>> get_polygon_simplified_multi(
    const T& pixelbin, float epsilon = 0.5f, bool include_diagonal = false
) {
    auto split_bin = split(Grid2D<bool>(pixelbin), include_diagonal);
    std::vector<std::vector<std::vector<glm::ivec2>>> result;
    for (auto& bin : split_bin) {
        result.emplace_back(
            std::move(get_polygon_simplified(bin, epsilon, include_diagonal))
        );
    }
    return result;
}

template <typename T>
    requires std::movable<T>
struct ExtendableGrid2D {
   private:
    std::vector<std::pair<glm::ivec2, T>> grid_data;
    packed_grid2d<int64_t> grid_indices;
    glm::ivec2 grid_origin;
    int _OcupiedXmin() {
        for (int i = 0; i < grid_indices.size(0); i++) {
            for (int j = 0; j < grid_indices.size(1); j++) {
                if (grid_indices.get(i, j) >= 0) return i;
            }
        }
        return grid_indices.size(0);
    }
    int _OcupiedXmax() {
        for (int i = grid_indices.size(0) - 1; i >= 0; i--) {
            for (int j = 0; j < grid_indices.size(1); j++) {
                if (grid_indices.get(i, j) >= 0) return i;
            }
        }
        return -1;
    }
    int _OcupiedYmin() {
        for (int j = 0; j < grid_indices.size(1); j++) {
            for (int i = 0; i < grid_indices.size(0); i++) {
                if (grid_indices.get(i, j) >= 0) return j;
            }
        }
        return grid_indices.size(1);
    }
    int _OcupiedYmax() {
        for (int j = grid_indices.size(1) - 1; j >= 0; j--) {
            for (int i = 0; i < grid_indices.size(0); i++) {
                if (grid_indices.get(i, j) >= 0) return j;
            }
        }
        return -1;
    }

   public:
    ExtendableGrid2D() : grid_origin(0, 0), grid_indices({0, 0}, -1) {}
    ExtendableGrid2D(const ExtendableGrid2D& other)            = default;
    ExtendableGrid2D(ExtendableGrid2D&& other)                 = default;
    ExtendableGrid2D& operator=(const ExtendableGrid2D& other) = default;
    ExtendableGrid2D& operator=(ExtendableGrid2D&& other)      = default;

    bool empty() const { return grid_data.empty(); }
    size_t count() const { return grid_data.size(); }
    glm::ivec2 origin() const { return grid_origin; }

    template <typename... Args>
    void emplace(int x, int y, Args&&... args) {
        if (contains(x, y)) {
            grid_data[grid_indices.get(x - grid_origin.x, y - grid_origin.y)] =
                std::make_pair(
                    glm::ivec2(x, y), T(std::forward<Args>(args)...)
                );
            return;
        }
        grid_data.emplace_back(
            glm::ivec2(x, y), T(std::forward<Args>(args)...)
        );
        if (empty()) {
            grid_origin  = {x, y};
            grid_indices = packed_grid2d<int64_t>({1, 1}, -1);
            return;
        }
        auto new_width = std::max(
            grid_indices.size()[0],
            std::max(
                x - grid_origin.x + 1,
                grid_indices.size()[0] - x + grid_origin.x
            )
        );
        auto new_height = std::max(
            grid_indices.size()[1],
            std::max(
                y - grid_origin.y + 1,
                grid_indices.size()[1] - y + grid_origin.y
            )
        );
        auto new_origin =
            glm::ivec2(std::min(grid_origin.x, x), std::min(grid_origin.y, y));
        auto diff = grid_origin - new_origin;
        if (new_width != grid_indices.size()[0] ||
            new_height != grid_indices.size()[1]) {
            packed_grid2d<int64_t> new_indices({new_width, new_height}, -1);
            for (int i = 0; i < grid_indices.size()[0]; i++) {
                for (int j = 0; j < grid_indices.size()[1]; j++) {
                    new_indices.emplace(
                        i + diff.x, j + diff.y, grid_indices.get(i, j)
                    );
                }
            }
            grid_indices = std::move(new_indices);
        }
        grid_origin = new_origin;
        grid_indices.emplace(
            x - grid_origin.x, y - grid_origin.y, grid_data.size() - 1
        );
    }
    template <typename... Args>
    void emplace(glm::ivec2 pos, Args... args) {
        emplace(pos.x, pos.y, args...);
    }
    template <typename... Args>
    void try_emplace(int x, int y, Args... args) {
        if (contains(x, y)) return;
        emplace(x, y, args...);
    }
    template <typename... Args>
    void try_emplace(glm::ivec2 pos, Args... args) {
        try_emplace(pos.x, pos.y, args...);
    }
    T& get(int x, int y) {
        if (!valid(x, y))
            throw std::out_of_range(std::format(
                "(x={}, y={}) out of range ({}, {})", x, y, grid_origin.x,
                grid_origin.y
            ));
        if (!contains(x, y)) {
            std::string msg = std::format("(x={}, y={}) not in grid", x, y);
            throw std::exception(msg.c_str());
        }
        return grid_data[grid_indices.get(x - grid_origin.x, y - grid_origin.y)]
            .second;
    }
    const T& get(int x, int y) const {
        if (!valid(x, y))
            throw std::out_of_range(std::format(
                "(x={}, y={}) out of range ({}, {})", x, y, grid_origin.x,
                grid_origin.y
            ));
        if (!contains(x, y)) {
            std::string msg = std::format("(x={}, y={}) not in grid", x, y);
            throw std::exception(msg.c_str());
        }
        return grid_data[grid_indices.get(x - grid_origin.x, y - grid_origin.y)]
            .second;
    }
    T& operator()(int x, int y) { return get(x, y); }
    const T& operator()(int x, int y) const { return get(x, y); }
    glm::ivec2 size() const {
        return glm::ivec2(grid_indices.size()[0], grid_indices.size()[1]);
    }
    bool valid(int x, int y) const {
        return grid_indices.valid(x - grid_origin.x, y - grid_origin.y);
    }
    bool contains(int x, int y) const {
        return valid(x, y) &&
               grid_indices.get(x - grid_origin.x, y - grid_origin.y) >= 0;
    }
    void shrink() {
        int xmin = _OcupiedXmin();
        int xmax = _OcupiedXmax();
        int ymin = _OcupiedYmin();
        int ymax = _OcupiedYmax();
        if (xmin > xmax || ymin > ymax) {
            grid_indices = packed_grid2d<int64_t>({0, 0}, -1);
            grid_data.clear();
            return;
        }
        auto new_size = std::array{xmax - xmin + 1, ymax - ymin + 1};
        if (new_size[0] == grid_indices.size()[0] &&
            new_size[1] == grid_indices.size()[1])
            return;
        packed_grid2d<int64_t> new_indices({new_size[0], new_size[1]}, -1);
        for (int i = xmin; i <= xmax; i++) {
            for (int j = ymin; j <= ymax; j++) {
                new_indices.emplace(i - xmin, j - ymin, grid_indices.get(i, j));
            }
        }
        grid_origin += glm::ivec2(xmin, ymin);
        grid_indices = std::move(new_indices);
    }
    void remove(int x, int y) {
        if (!contains(x, y)) return;
        auto index = grid_indices.get(x - grid_origin.x, y - grid_origin.y);
        auto pos   = grid_data.back().first;
        grid_indices.get(pos.x - grid_origin.x, pos.y - grid_origin.y) = index;
        grid_indices.get(x - grid_origin.x, y - grid_origin.y)         = -1;
        grid_data[index] = std::move(grid_data.back());
        grid_data.pop_back();
    }
    void remove(glm::ivec2 pos) { remove(pos.x, pos.y); }
    const std::vector<std::pair<glm::ivec2, T>>& data() const {
        return grid_data;
    }
};
}  // namespace epix::utils::grid2d