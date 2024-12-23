#pragma once

#include <array>
#include <concepts>
#include <earcut.hpp>
#include <glm/glm.hpp>
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
    { t.size() } -> std::same_as<glm::ivec2>;
    { t.contains(0i32, 0i32) } -> std::same_as<bool>;
};
template <typename T>
concept SetBoolGrid = BoolGrid<T> && requires(T t) {
    { t.set(0i32, 0i32, true) };
};
template <typename T>
    requires std::convertible_to<T, bool> && requires(T t) {
        { !t } -> std::same_as<bool>;
    }
struct Grid2D {
    const int width;
    const int height;
    std::vector<T> data;

    Grid2D(int width, int height) : width(width), height(height) {
        data.resize(width * height);
    }
    template <typename... Args>
    Grid2D(int width, int height, Args&&... args)
        : width(width), height(height) {
        data.resize(width * height, T(std::forward<Args>(args)...));
    }
    Grid2D(const Grid2D& other) : width(other.width), height(other.height) {
        data = other.data;
    }
    Grid2D(Grid2D&& other) : width(other.width), height(other.height) {
        data = std::move(other.data);
    }
    Grid2D& operator=(const Grid2D& other) {
        if (width != other.width || height != other.height)
            throw std::runtime_error("Grid2D::operator=(): size mismatch");
        data = other.data;
        return *this;
    }
    Grid2D& operator=(Grid2D&& other) {
        if (width != other.width || height != other.height)
            throw std::runtime_error("Grid2D::operator=(): size mismatch");
        data = std::move(other.data);
        return *this;
    }
    void set(int x, int y, const T& value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        data[x + y * width] = value;
    }
    void set(int x, int y, T&& value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        data[x + y * width] = std::move(value);
    }
    template <typename... Args>
    void emplace(int x, int y, Args&&... args) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        data[x + y * width] = T(std::forward<Args>(args)...);
    }
    template <typename... Args>
    void try_emplace(int x, int y, Args&&... args) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        if (!data[x + y * width]) {
            data[x + y * width] = T(std::forward<Args>(args)...);
        }
    }
    T& operator()(int x, int y) { return data[x + y * width]; }
    const T& operator()(int x, int y) const { return data[x + y * width]; }
    bool valid(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height;
    }
    bool contains(int x, int y) const {
        return valid(x, y) && (bool)(*this)(x, y);
    }
    glm::ivec2 size() const { return {width, height}; }
};
template <>
struct Grid2D<bool> {
    const int width;
    const int height;
    const int column;
    std::vector<uint32_t> data;
    Grid2D(int width, int height)
        : width(width),
          height(height),
          column(width / 32 + (width % 32 ? 1 : 0)) {
        data.reserve(column * height);
        data.resize(column * height);
    }
    Grid2D(int width, int height, bool value)
        : width(width),
          height(height),
          column(width / 32 + (width % 32 ? 1 : 0)) {
        data.reserve(column * height);
        data.resize(column * height);
        for (int i = 0; i < column * height; i++) {
            data[i] = value ? 0xFFFFFFFF : 0;
        }
    }
    template <typename T>
    Grid2D(const Grid2D<T>& other)
        : width(other.width),
          height(other.height),
          column(width / 32 + (width % 32 ? 1 : 0)) {
        data.reserve(column * height);
        data.resize(column * height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                set(x, y, (bool)other(x, y));
            }
        }
    }
    template <BoolGrid T>
    Grid2D(const T& other)
        : width(other.size().x),
          height(other.size().y),
          column(width / 32 + (width % 32 ? 1 : 0)) {
        data.reserve(column * height);
        data.resize(column * height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                set(x, y, other.contains(x, y));
            }
        }
    }
    void set(int x, int y, bool value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        if (value)
            data[index] |= 1 << bit;
        else
            data[index] &= ~(1 << bit);
    }
    void emplace(int x, int y, bool value) { set(x, y, value); }
    void try_emplace(int x, int y, bool value) { set(x, y, value); }
    bool operator()(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        return data[index] & (1 << bit);
    }
    bool valid(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height;
    }
    bool contains(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        return data[index] & (1 << bit);
    }
    glm::ivec2 size() const { return {width, height}; }
};

template <typename T, size_t width, size_t height>
    requires std::convertible_to<T, bool> && requires(T t) {
        { !t } -> std::same_as<bool>;
    }
struct Grid2DOnStack {
    std::array<T, width * height> data;
    Grid2DOnStack() = default;
    template <typename... Args>
    Grid2DOnStack(Args&&... args) {
        data.fill(T(std::forward<Args>(args)...));
    }
    void set(int x, int y, const T& value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        data[x + y * width] = value;
    }
    void set(int x, int y, T&& value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        data[x + y * width] = std::move(value);
    }
    template <typename... Args>
    void emplace(int x, int y, Args&&... args) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        data[x + y * width] = T(std::forward<Args>(args)...);
    }
    template <typename... Args>
    void try_emplace(int x, int y, Args&&... args) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        if (!data[x + y * width]) {
            data[x + y * width] = T(std::forward<Args>(args)...);
        }
    }
    bool valid(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height;
    }
    bool contains(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height &&
               (bool)(*this)(x, y);
    }
    T& operator()(int x, int y) { return data[x + y * width]; }
    const T& operator()(int x, int y) const { return data[x + y * width]; }
    glm::ivec2 size() const { return {width, height}; }
};
template <size_t width, size_t height>
struct Grid2DOnStack<bool, width, height> {
    static constexpr int column = width / 32 + (width % 32 ? 1 : 0);
    std::array<uint32_t, column * height> data;
    Grid2DOnStack() = default;
    Grid2DOnStack(bool value) { data.fill(value ? 0xFFFFFFFF : 0); }
    void set(int x, int y, bool value) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        if (value)
            data[index] |= 1 << bit;
        else
            data[index] &= ~(1 << bit);
    }
    void emplace(int x, int y, bool value) { set(x, y, value); }
    void try_emplace(int x, int y, bool value) { set(x, y, value); }
    bool operator()(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        return data[index] & (1 << bit);
    }
    bool contains(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        int index = x / 32 + y * column;
        int bit   = x % 32;
        return data[index] & (1 << bit);
    }
    glm::ivec2 size() const { return {width, height}; }
};

struct GridOpArea {
    int x1, y1, x2, y2, width, height;
    GridOpArea& setOrigin1(int x, int y) {
        x1 = x;
        y1 = y;
        return *this;
    }
    GridOpArea& setOrigin2(int x, int y) {
        x2 = x;
        y2 = y;
        return *this;
    }
    GridOpArea& setExtent(int w, int h) {
        width  = w;
        height = h;
        return *this;
    }
};

template <typename T, typename U, size_t width, size_t height>
Grid2DOnStack<T, width, height> op_and(
    const Grid2DOnStack<T, width, height>& a,
    const Grid2DOnStack<U, width, height>& b
) {
    Grid2DOnStack<T, width, height> result;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (b.contains(x, y)) {
                result.set(x, y, a(x, y));
            }
        }
    }
    return result;
}
template <typename U, size_t width, size_t height>
Grid2DOnStack<bool, width, height> operator&(
    const Grid2DOnStack<bool, width, height>& a,
    const Grid2DOnStack<U, width, height>& b
) {
    return op_and(a, b);
}
template <typename U, size_t width, size_t height>
Grid2DOnStack<bool, width, height> op_or(
    const Grid2DOnStack<bool, width, height>& a,
    const Grid2DOnStack<U, width, height>& b
) {
    Grid2DOnStack<bool, width, height> result;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            result.set(x, y, a.contains(x, y) || b.contains(x, y));
        }
    }
    return result;
}
template <typename U, size_t width, size_t height>
Grid2DOnStack<bool, width, height> operator|(
    const Grid2DOnStack<bool, width, height>& a,
    const Grid2DOnStack<U, width, height>& b
) {
    return op_or(a, b);
}
template <typename U, size_t width, size_t height>
Grid2DOnStack<bool, width, height> op_xor(
    const Grid2DOnStack<bool, width, height>& a,
    const Grid2DOnStack<U, width, height>& b
) {
    Grid2DOnStack<bool, width, height> result;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            result.set(x, y, a.contains(x, y) ^ b.contains(x, y));
        }
    }
    return result;
}
template <typename U, size_t width, size_t height>
Grid2DOnStack<bool, width, height> operator^(
    const Grid2DOnStack<bool, width, height>& a,
    const Grid2DOnStack<U, width, height>& b
) {
    return op_xor(a, b);
}
template <typename T, size_t width, size_t height>
Grid2DOnStack<bool, width, height> op_not(
    const Grid2DOnStack<T, width, height>& a
) {
    Grid2DOnStack<bool, width, height> result;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            result.set(x, y, !a.contains(x, y));
        }
    }
    return result;
}
template <size_t width, size_t height>
Grid2DOnStack<bool, width, height> operator~(
    const Grid2DOnStack<bool, width, height>& a
) {
    return op_not(a);
}

template <typename T, typename U>
    requires std::copyable<T>
Grid2D<T> op_and(
    const Grid2D<T>& a,
    const Grid2D<U>& b,
    const GridOpArea& area =
        {0, 0, 0, 0, std::min(a.size().x, b.size().x),
         std::min(a.size().y, b.size().y)}
) {
    Grid2D<T> result(area.width, area.height);
    for (int y = 0; y < area.height; y++) {
        for (int x = 0; x < area.width; x++) {
            if (b.contains(x + area.x1, y + area.y1)) {
                result(x, y) = a(x + area.x1, y + area.y1);
            }
        }
    }
    return result;
}
template <typename U>
Grid2D<bool> op_and(
    const Grid2D<bool>& a, const Grid2D<U>& b, const GridOpArea& area
) {
    Grid2D<bool> result(area.width, area.height);
    for (int y = 0; y < area.height; y++) {
        for (int x = 0; x < area.width; x++) {
            result.set(
                x, y,
                a.contains(x + area.x1, y + area.y1) &&
                    b.contains(x + area.x1, y + area.y1)
            );
        }
    }
    return result;
}
template <typename T, typename U>
Grid2D<T> operator&(const Grid2D<T>& a, const Grid2D<U>& b) {
    return op_and(
        a, b,
        {0, 0, 0, 0, std::min(a.size().x, b.size().x),
         std::min(a.size().y, b.size().y)}
    );
}
template <typename T, typename U>
Grid2D<bool> op_or(
    const Grid2D<T>& a, const Grid2D<U>& b, const GridOpArea& area
) {
    Grid2D<bool> result(area.width, area.height);
    for (int y = 0; y < area.height; y++) {
        for (int x = 0; x < area.width; x++) {
            result.set(
                x, y,
                a.contains(x + area.x1, y + area.y1) ||
                    b.contains(x + area.x1, y + area.y1)
            );
        }
    }
    return result;
}
template <typename T, typename U>
Grid2D<bool> operator|(const Grid2D<T>& a, const Grid2D<U>& b) {
    return op_or(
        a, b,
        {0, 0, 0, 0, std::min(a.size().x, b.size().x),
         std::min(a.size().y, b.size().y)}
    );
}
template <typename T, typename U>
Grid2D<bool> op_xor(
    const Grid2D<T>& a, const Grid2D<U>& b, const GridOpArea& area
) {
    Grid2D<bool> result(area.width, area.height);
    for (int y = 0; y < area.height; y++) {
        for (int x = 0; x < area.width; x++) {
            result.set(
                x, y,
                a.contains(x + area.x1, y + area.y1) ^
                    b.contains(x + area.x1, y + area.y1)
            );
        }
    }
    return result;
}
template <typename T, typename U>
Grid2D<bool> operator^(const Grid2D<T>& a, const Grid2D<U>& b) {
    return op_xor(
        a, b,
        {0, 0, 0, 0, std::min(a.size().x, b.size().x),
         std::min(a.size().y, b.size().y)}
    );
}
template <typename T>
Grid2D<bool> op_not(const Grid2D<T>& a, const GridOpArea& area) {
    Grid2D<bool> result(area.width, area.height);
    for (int y = 0; y < area.height; y++) {
        for (int x = 0; x < area.width; x++) {
            result.set(x, y, !a.contains(x + area.x1, y + area.y1));
        }
    }
    return result;
}
template <typename T>
Grid2D<bool> operator~(const Grid2D<T>& a) {
    return op_not(a, GridOpArea{0, 0, 0, 0, a.size().x, a.size().y});
}

/**
 * @brief Get the outland of a binary grid
 *
 * @param grid The binary grid
 * @param include_diagonal Whether to include diagonal pixels connected to the
 * outland as part of the outland
 *
 * @return Grid2D<bool> The outland of the binary grid
 */
template <BoolGrid T>
Grid2D<bool> get_outland(const T& grid, bool include_diagonal = false) {
    Grid2D<bool> outland(grid.width, grid.height);
    // use dimension first search
    std::stack<std::pair<int, int>> stack;
    for (int i = 0; i < grid.width; i++) {
        if (!grid.contains(i, 0)) stack.push({i, 0});
        if (!grid.contains(i, grid.height - 1))
            stack.push({i, grid.height - 1});
    }
    for (int i = 0; i < grid.height; i++) {
        if (!grid.contains(0, i)) stack.push({0, i});
        if (!grid.contains(grid.width - 1, i)) stack.push({grid.width - 1, i});
    }
    while (!stack.empty()) {
        auto [x, y] = stack.top();
        stack.pop();
        if (outland.contains(x, y)) continue;
        outland.set(x, y, true);
        if (x > 0 && !grid.contains(x - 1, y) && !outland.contains(x - 1, y))
            stack.push({x - 1, y});
        if (x < grid.width - 1 && !grid.contains(x + 1, y) &&
            !outland.contains(x + 1, y))
            stack.push({x + 1, y});
        if (y > 0 && !grid.contains(x, y - 1) && !outland.contains(x, y - 1))
            stack.push({x, y - 1});
        if (y < grid.height - 1 && !grid.contains(x, y + 1) &&
            !outland.contains(x, y + 1))
            stack.push({x, y + 1});
        if (include_diagonal) {
            if (x > 0 && y > 0 && !grid.contains(x - 1, y - 1) &&
                !outland.contains(x - 1, y - 1))
                stack.push({x - 1, y - 1});
            if (x < grid.width - 1 && y > 0 && !grid.contains(x + 1, y - 1) &&
                !outland.contains(x + 1, y - 1))
                stack.push({x + 1, y - 1});
            if (x > 0 && y < grid.height - 1 && !grid.contains(x - 1, y + 1) &&
                !outland.contains(x - 1, y + 1))
                stack.push({x - 1, y + 1});
            if (x < grid.width - 1 && y < grid.height - 1 &&
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
template <typename T>
    requires std::copyable<T>
Grid2D<T> shrink(const Grid2D<T>& grid, glm::ivec2* offset = nullptr) {
    auto size  = grid.size();
    auto width = size.x, height = size.y;
    glm::ivec2 min(width, height), max(-1, -1);
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            if (grid.contains(i, j)) {
                min.x = std::min(min.x, i);
                min.y = std::min(min.y, j);
                max.x = std::max(max.x, i);
                max.y = std::max(max.y, j);
            }
        }
    }
    if (offset) *offset = min;
    Grid2D<T> result(max.x - min.x + 1, max.y - min.y + 1);
    for (int i = min.x; i <= max.x; i++) {
        for (int j = min.y; j <= max.y; j++) {
            result.set(i - min.x, j - min.y, grid(i, j));
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
template <typename T>
    requires std::copyable<T>
std::vector<Grid2D<T>> split(
    const Grid2D<T>& grid, bool include_diagonal = false
) {
    std::vector<Grid2D<bool>> result_grid;
    Grid2D<bool> visited = get_outland(grid);
    while (true) {
        glm::ivec2 current(-1, -1);
        for (int i = 0; i < visited.size().x; i++) {
            for (int j = 0; j < visited.size().y; j++) {
                if (!visited.contains(i, j) && grid.contains(i, j)) {
                    current = {i, j};
                    break;
                }
            }
            if (current.x != -1) break;
        }
        if (current.x == -1) break;
        result_grid.push_back(Grid2D<bool>(grid.width, grid.height));
        auto& current_grid = result_grid.back();
        std::stack<std::pair<int, int>> stack;
        stack.push({current.x, current.y});
        while (!stack.empty()) {
            auto [x, y] = stack.top();
            stack.pop();
            if (current_grid.contains(x, y)) continue;
            current_grid.set(x, y, true);
            visited.set(x, y, true);
            if (x > 0 && !visited.contains(x - 1, y) && grid.contains(x - 1, y))
                stack.push({x - 1, y});
            if (x < grid.width - 1 && !visited.contains(x + 1, y) &&
                grid.contains(x + 1, y))
                stack.push({x + 1, y});
            if (y > 0 && !visited.contains(x, y - 1) && grid.contains(x, y - 1))
                stack.push({x, y - 1});
            if (y < grid.height - 1 && !visited.contains(x, y + 1) &&
                grid.contains(x, y + 1))
                stack.push({x, y + 1});
            if (include_diagonal) {
                if (x > 0 && y > 0 && !visited.contains(x - 1, y - 1) &&
                    grid.contains(x - 1, y - 1))
                    stack.push({x - 1, y - 1});
                if (x < grid.width - 1 && y > 0 &&
                    !visited.contains(x + 1, y - 1) &&
                    grid.contains(x + 1, y - 1))
                    stack.push({x + 1, y - 1});
                if (x > 0 && y < grid.height - 1 &&
                    !visited.contains(x - 1, y + 1) &&
                    grid.contains(x - 1, y + 1))
                    stack.push({x - 1, y + 1});
                if (x < grid.width - 1 && y < grid.height - 1 &&
                    !visited.contains(x + 1, y + 1) &&
                    grid.contains(x + 1, y + 1))
                    stack.push({x + 1, y + 1});
            }
        }
    }
    std::vector<Grid2D<T>> result;
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
    for (int i = 0; i < pixelbin.size().x; i++) {
        for (int j = 0; j < pixelbin.size().y; j++) {
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
    for (int i = 0; i < pixelbin.size().x; i++) {
        for (int j = 0; j < pixelbin.size().y; j++) {
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
    Grid2D<bool> grid(pixelbin);
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
    Grid2D<bool> grid(pixelbin);
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
    auto split_bin = split(Grid2D<bool>(pixelbin), include_diagonal);
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

template <typename T, bool use_list = false>
    requires std::move_constructible<T> && std::movable<T> && requires(T t) {
        { T() } -> std::same_as<T>;
        { !t } -> std::same_as<bool>;
        { (bool)t } -> std::same_as<bool>;
    }
struct ExtendableGrid2D {
   private:
    std::vector<std::vector<T>> grid_data;
    glm::ivec2 grid_origin;
    glm::ivec2 grid_size;
    size_t elem_count = 0;
    void _TestResizeX(int x) {
        if (x < grid_origin.x) {
            grid_data.insert(
                grid_data.begin(), grid_origin.x - x,
                std::vector<T>(grid_size.y)
            );
            grid_origin.x = x;
        }
        if (x >= grid_origin.x + grid_size.x) {
            grid_data.insert(
                grid_data.end(), x - grid_origin.x - grid_size.x + 1,
                std::vector<T>(grid_size.y)
            );
            grid_size.x = x - grid_origin.x + 1;
        }
    }
    void _TestResizeY(int y) {
        if (y < grid_origin.y) {
            for (auto& row : grid_data) {
                row.insert(row.begin(), grid_origin.y - y, T());
            }
            grid_origin.y = y;
        }
        if (y >= grid_origin.y + grid_size.y) {
            for (auto& row : grid_data) {
                row.insert(row.end(), y - grid_origin.y - grid_size.y + 1, T());
            }
            grid_size.y = y - grid_origin.y + 1;
        }
    }
    int _OcupiedXmin() {
        for (int i = 0; i < grid_size.x; i++) {
            for (int j = 0; j < grid_size.y; j++) {
                if ((bool)grid_data[i][j]) return i;
            }
        }
        return grid_size.x;
    }
    int _OcupiedXmax() {
        for (int i = grid_size.x - 1; i >= 0; i--) {
            for (int j = 0; j < grid_size.y; j++) {
                if ((bool)grid_data[i][j]) return i;
            }
        }
        return -1;
    }
    int _OcupiedYmin() {
        for (int j = 0; j < grid_size.y; j++) {
            for (int i = 0; i < grid_size.x; i++) {
                if ((bool)grid_data[i][j]) return j;
            }
        }
        return grid_size.y;
    }
    int _OcupiedYmax() {
        for (int j = grid_size.y - 1; j >= 0; j--) {
            for (int i = 0; i < grid_size.x; i++) {
                if ((bool)grid_data[i][j]) return j;
            }
        }
        return -1;
    }

   public:
    ExtendableGrid2D() : grid_origin(0, 0), grid_size(0, 0) {}
    ExtendableGrid2D(const ExtendableGrid2D& other)            = default;
    ExtendableGrid2D(ExtendableGrid2D&& other)                 = default;
    ExtendableGrid2D& operator=(const ExtendableGrid2D& other) = default;
    ExtendableGrid2D& operator=(ExtendableGrid2D&& other)      = default;

    bool empty() const { return grid_size.x == 0 || grid_size.y == 0; }
    size_t count() const { return elem_count; }
    glm::ivec2 origin() const { return grid_origin; }

    template <typename... Args>
    void emplace(int x, int y, Args... args) {
        if (empty()) {
            grid_data.emplace_back(1, T(args...));
            grid_origin = {x, y};
            grid_size   = {1, 1};
            return;
        }
        _TestResizeX(x);
        _TestResizeY(y);
        grid_data[x - grid_origin.x][y - grid_origin.y] = T(args...);
        elem_count++;
    }
    template <typename... Args>
    void emplace(glm::ivec2 pos, Args... args) {
        emplace(pos.x, pos.y, args...);
    }
    template <typename... Args>
    void try_emplace(int x, int y, Args... args) {
        if (contains(x, y)) {
            return;
        }
        emplace(x, y, args...);
    }
    template <typename... Args>
    void try_emplace(glm::ivec2 pos, Args... args) {
        try_emplace(pos.x, pos.y, args...);
    }
    T& get(int x, int y) {
        if (x < grid_origin.x || x >= grid_origin.x + grid_size.x ||
            y < grid_origin.y || y >= grid_origin.y + grid_size.y)
            throw std::out_of_range("Out of range");
        return grid_data[x - grid_origin.x][y - grid_origin.y];
    }
    const T& get(int x, int y) const {
        if (x < grid_origin.x || x >= grid_origin.x + grid_size.x ||
            y < grid_origin.y || y >= grid_origin.y + grid_size.y)
            throw std::out_of_range("Out of range");
        return grid_data[x - grid_origin.x][y - grid_origin.y];
    }
    T& operator()(int x, int y) { return get(x, y); }
    const T& operator()(int x, int y) const { return get(x, y); }
    glm::ivec2 size() const { return grid_size; }
    bool valid(int x, int y) const {
        return x >= grid_origin.x && x < grid_origin.x + grid_size.x &&
               y >= grid_origin.y && y < grid_origin.y + grid_size.y;
    }
    bool contains(int x, int y) const {
        return valid(x, y) && (bool)(*this)(x, y);
    }
    void shrink() {
        int xmin = _OcupiedXmin();
        int xmax = _OcupiedXmax();
        int ymin = _OcupiedYmin();
        int ymax = _OcupiedYmax();
        if (xmin > xmax || ymin > ymax) {
            grid_data.clear();
            grid_origin = {0, 0};
            grid_size   = {0, 0};
            return;
        }
        grid_data.erase(grid_data.begin(), grid_data.begin() + xmin);
        grid_data.erase(grid_data.begin() + xmax - xmin + 1, grid_data.end());
        for (auto& row : grid_data) {
            row.erase(row.begin(), row.begin() + ymin);
            row.erase(row.begin() + ymax - ymin + 1, row.end());
        }
        grid_origin.x += xmin;
        grid_origin.y += ymin;
        grid_size = {xmax - xmin + 1, ymax - ymin + 1};

        std::vector<std::vector<T>> new_data(
            grid_size.x, std::vector<T>(grid_size.y)
        );
        for (int i = 0; i < grid_size.x; i++) {
            for (int j = 0; j < grid_size.y; j++) {
                std::swap(
                    new_data[i][j],
                    grid_data[i + grid_origin.x][j + grid_origin.y]
                );
            }
        }
        grid_data = std::move(new_data);
    }
    void remove(int x, int y) {
        if (x < grid_origin.x || x >= grid_origin.x + grid_size.x ||
            y < grid_origin.y || y >= grid_origin.y + grid_size.y)
            return;
        if (contains(x, y)) {
            grid_data[x - grid_origin.x][y - grid_origin.y] = T();
            elem_count--;
        }
    }
    void remove(glm::ivec2 pos) { remove(pos.x, pos.y); }
};
}  // namespace epix::utils::grid2d