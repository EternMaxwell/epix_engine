#pragma once

#include <spdlog/spdlog.h>

#include <BS_thread_pool.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>
#include <random>
#include <shared_mutex>

#include "epix/common.h"
#include "epix/utils/grid.h"

namespace epix::world::sand::components {
struct ElemRegistry;
struct Element {
   private:
    enum class GravType : uint8_t {
        POWDER,
        LIQUID,
        SOLID,
        GAS,
        PLACEHOLDER  // for cells that are occupied by non-elemental entities
                     // e.g. bodies
    } grav_type = GravType::SOLID;

   public:
    float density    = 0.0f;
    float bouncing   = 0.1f;
    float friction   = 0.0f;
    float awake_rate = 1.0f;
    std::string name;
    std::string description = "";

   private:
    std::function<glm::vec4()> color_gen;
    EPIX_API static Element place_holder();

    friend struct ElemRegistry;

   public:
    EPIX_API Element(const std::string& name, GravType type);
    EPIX_API static Element solid(const std::string& name);
    EPIX_API static Element liquid(const std::string& name);
    EPIX_API static Element powder(const std::string& name);
    EPIX_API static Element gas(const std::string& name);
    EPIX_API Element& set_grav_type(GravType type);
    EPIX_API Element& set_density(float density);
    EPIX_API Element& set_bouncing(float bouncing);
    EPIX_API Element& set_friction(float friction);
    EPIX_API Element& set_awake_rate(float rate);
    EPIX_API Element& set_description(const std::string& description);
    EPIX_API Element& set_color(std::function<glm::vec4()> color_gen);
    EPIX_API Element& set_color(const glm::vec4& color);
    EPIX_API bool is_complete() const;
    EPIX_API glm::vec4 gen_color() const;

    EPIX_API bool operator==(const Element& other) const;
    EPIX_API bool operator!=(const Element& other) const;
    EPIX_API bool is_solid() const;
    EPIX_API bool is_liquid() const;
    EPIX_API bool is_powder() const;
    EPIX_API bool is_gas() const;
    EPIX_API bool is_place_holder() const;
};
struct CellDef {
    enum class DefIdentifier { Name, Id, None } identifier;
    glm::vec2 default_vel;
    union {
        std::string elem_name;
        int elem_id;
    };

    EPIX_API CellDef(const CellDef& other);
    EPIX_API CellDef(CellDef&& other);
    EPIX_API CellDef& operator=(const CellDef& other);
    EPIX_API CellDef& operator=(CellDef&& other);
    EPIX_API CellDef(const std::string& name);
    EPIX_API CellDef(int id);
    EPIX_API ~CellDef();
};
struct Cell {
    int elem_id        = -1;
    glm::vec4 color    = glm::vec4(0.0f);
    glm::vec2 velocity = glm::vec2(0.0f);
    glm::vec2 inpos    = glm::vec2(0.0f);
    int not_move_count = 0;
    uint8_t bitfield   = 0;

    static constexpr uint8_t FREEFALL = 1 << 0;
    static constexpr uint8_t UPDATED  = 1 << 1;
    static constexpr uint8_t BURNING  = 1 << 2;

    EPIX_API bool freefall() const;
    EPIX_API void set_freefall(bool freefall);
    EPIX_API bool updated() const;
    EPIX_API void set_updated(bool updated);

    EPIX_API bool valid() const;
    EPIX_API operator bool() const;
    EPIX_API bool operator!() const;
};
struct ElemRegistry {
   private:
    // mutable std::shared_mutex mutex;
    entt::dense_map<std::string, uint32_t> elemId_map;
    std::vector<Element> elements;

   public:
    EPIX_API ElemRegistry();
    EPIX_API ElemRegistry(const ElemRegistry& other);
    EPIX_API ElemRegistry(ElemRegistry&& other);
    EPIX_API ElemRegistry& operator=(const ElemRegistry& other);
    EPIX_API ElemRegistry& operator=(ElemRegistry&& other);

    EPIX_API int register_elem(const std::string& name, const Element& elem);
    EPIX_API int register_elem(const Element& elem);
    EPIX_API int elem_id(const std::string& name) const;
    EPIX_API std::string elem_name(int id) const;
    EPIX_API int count() const;
    EPIX_API const Element& get_elem(const std::string& name) const;
    EPIX_API const Element& get_elem(int id) const;
    EPIX_API const Element& operator[](int id) const;
    EPIX_API void add_equiv(const std::string& name, const std::string& equiv);
};
struct Simulation {
    struct Chunk {
        using Grid = epix::utils::grid::sparse_grid<Cell, 2>;
        Grid cells;
        const int width;
        const int height;
        int time_since_last_swap;
        int time_threshold;
        int updating_area[4];
        int updating_area_next[4];

        EPIX_API Chunk(int width, int height);
        EPIX_API Chunk(const Chunk& other);
        EPIX_API Chunk(Chunk&& other);
        EPIX_API Chunk& operator=(const Chunk& other);
        EPIX_API Chunk& operator=(Chunk&& other);
        EPIX_API void reset_updated();
        EPIX_API void count_time();
        EPIX_API Cell& get(int x, int y);
        EPIX_API const Cell& get(int x, int y) const;
        EPIX_API Cell& create(
            int x, int y, const CellDef& def, ElemRegistry& m_registry
        );
        EPIX_API void insert(int x, int y, const Cell& cell);
        EPIX_API void insert(int x, int y, Cell&& cell);
        EPIX_API void swap_area();
        EPIX_API bool in_area(int x, int y) const;
        EPIX_API void remove(int x, int y);
        EPIX_API bool is_updated(int x, int y) const;
        EPIX_API void touch(int x, int y);
        EPIX_API bool should_update() const;

        // size and contains function for find_outline algorithm in
        // physics2d::utils to work.
        /**
         * @brief Get the size of the chunk
         *
         * @return glm::ivec2
         */
        EPIX_API glm::ivec2 size() const;
        /**
         * @brief Check if the chunk contains a cell at the given coordinates
         *
         * @param x x-coordinate
         * @param y y-coordinate
         * @return true if the cell is within the chunk
         * @return false if the cell is outside the chunk
         */
        EPIX_API bool contains(int x, int y) const;
    };

    struct ChunkMap {
        const int chunk_size;
        using Grid = epix::utils::grid::extendable_grid<Chunk, 2>;
        Grid chunks;

        struct IterateSetting {
            bool xorder  = true;
            bool yorder  = true;
            bool x_outer = true;
        } iterate_setting;

        struct iterator {
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = std::pair<glm::ivec2, Chunk&>;
            using pointer           = value_type*;
            using reference         = value_type&;

            ChunkMap* chunk_map;
            int x;
            int y;

            EPIX_API iterator(ChunkMap* chunk_map, int x, int y);
            EPIX_API iterator& operator++();
            EPIX_API bool operator==(const iterator& other) const;
            EPIX_API bool operator!=(const iterator& other) const;
            EPIX_API value_type operator*();
        };

        struct const_iterator {
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = std::pair<glm::ivec2, const Chunk&>;
            using pointer           = value_type*;
            using reference         = value_type&;

            const ChunkMap* chunk_map;
            int x;
            int y;

            EPIX_API const_iterator(const ChunkMap* chunk_map, int x, int y);
            EPIX_API const_iterator& operator++();
            EPIX_API bool operator==(const const_iterator& other) const;
            EPIX_API bool operator!=(const const_iterator& other) const;
            EPIX_API value_type operator*();
        };

        EPIX_API ChunkMap(int chunk_size);
        EPIX_API void load_chunk(int x, int y, const Chunk& chunk);
        EPIX_API void load_chunk(int x, int y, Chunk&& chunk);
        EPIX_API void load_chunk(int x, int y);
        EPIX_API bool contains(int x, int y) const;
        EPIX_API Chunk& get_chunk(int x, int y);
        EPIX_API const Chunk& get_chunk(int x, int y) const;
        EPIX_API size_t chunk_count() const;

        EPIX_API void set_iterate_setting(
            bool xorder, bool yorder, bool x_outer
        );

        EPIX_API iterator begin();
        EPIX_API const_iterator begin() const;
        EPIX_API iterator end();
        EPIX_API const_iterator end() const;

        EPIX_API void reset_updated();
        EPIX_API void count_time();
    };

   private:
    ElemRegistry m_registry;
    const int m_chunk_size;
    ChunkMap m_chunk_map;
    std::unique_ptr<BS::thread_pool<BS::tp::none>> m_thread_pool;
    struct NotMovingThresholdSetting {
        float power     = 0.3f;
        float numerator = 4000.0f;
    } not_moving_threshold_setting;
    struct LiquidSpreadSetting {
        float spread_len = 3.0f;
        float prefix     = 0.01f;
    } liquid_spread_setting;
    struct UpdateState {
        bool random_state = true;
        bool xorder       = true;
        bool yorder       = true;
        bool x_outer      = true;
        EPIX_API void next();
    } update_state;
    std::optional<glm::ivec2> max_travel;
    struct UpdatingState {
        bool is_updating = false;
        std::vector<glm::ivec2> updating_chunks;
        std::pair<std::tuple<int, int, int>, std::tuple<int, int, int>> bounds;
        int updating_index = 0;
        std::optional<glm::ivec2> in_chunk_pos;

        EPIX_API std::optional<glm::ivec2> current_chunk() const;
        EPIX_API std::optional<glm::ivec2> current_cell() const;
    } m_updating_state;
    struct PowderSlideSetting {
        bool always_slide = true;
        float prefix      = 0.05f;
    } powder_slide_setting;
    // settings

    EPIX_API Cell& create_def(int x, int y, const CellDef& def);

    friend void update_cell(
        Simulation& sim, const int x_, const int y_, float delta
    );

   public:
    EPIX_API Simulation(const ElemRegistry& registry, int chunk_size = 64);
    EPIX_API Simulation(ElemRegistry&& registry, int chunk_size = 64);

    EPIX_API UpdatingState& updating_state();
    EPIX_API const UpdatingState& updating_state() const;

    /**
     * @brief Get the size of a chunk
     *
     * @return `int` the size of a chunk
     */
    EPIX_API int chunk_size() const;
    /**
     * @brief Get the element registry
     *
     * @return `ElemRegistry&` the element registry reference
     */
    EPIX_API ElemRegistry& registry();
    /**
     * @brief Get the element registry
     *
     * @return `const ElemRegistry&` the element registry reference
     */
    EPIX_API const ElemRegistry& registry() const;
    /**
     * @brief Get the chunk map
     *
     * @return `ChunkMap&` the chunk map reference
     */
    EPIX_API ChunkMap& chunk_map();
    /**
     * @brief Get the chunk map
     *
     * @return `const ChunkMap&` the chunk map reference
     */
    EPIX_API const ChunkMap& chunk_map() const;
    /**
     * @brief Reset the updated status of all cells. This is used to avoid
     * updating the same cell multiple times in a single frame.
     */
    EPIX_API void reset_updated();
    /**
     * @brief Check if a cell has been updated in the current frame
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     * @return `bool` true if the cell has been updated, false otherwise
     */
    EPIX_API bool is_updated(int x, int y) const;
    /**
     * @brief Load a chunk into the chunk map using copy assignment
     *
     * @param x x-coordinate of the chunk
     * @param y y-coordinate of the chunk
     * @param chunk the chunk to load
     */
    EPIX_API void load_chunk(int x, int y, const Chunk& chunk);
    /**
     * @brief Load a chunk into the chunk map using move assignment
     *
     * @param x x-coordinate of the chunk
     * @param y y-coordinate of the chunk
     * @param chunk the chunk to load
     */
    EPIX_API void load_chunk(int x, int y, Chunk&& chunk);
    /**
     * @brief Load an empty chunk into the chunk map
     *
     * @param x x-coordinate of the chunk
     * @param y y-coordinate of the chunk
     */
    EPIX_API void load_chunk(int x, int y);
    /**
     * @brief Get the position of the chunk in which cell (x, y) is located
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     *
     * @return `std::pair<int, int>` the position of the chunk
     */
    EPIX_API std::pair<int, int> to_chunk_pos(int x, int y) const;
    /**
     * @brief Get the position of the cell in the chunk
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     *
     * @return `std::pair<int, int>` the position of the cell
     */
    EPIX_API std::pair<int, int> to_in_chunk_pos(int x, int y) const;
    /**
     * @brief Check a cell is present at location (x, y)
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     *
     * @return `bool` true if there is a cell at the location, false otherwise
     */
    EPIX_API bool contain_cell(int x, int y) const;
    /**
     * @brief Check if a location is valid or within the bounds of the
     * simulation
     *
     * @param x x-coordinate of the location
     * @param y y-coordinate of the location
     *
     * @return `bool` true if the location is valid, false otherwise
     */
    EPIX_API bool valid(int x, int y) const;
    /**
     * @brief Get the cell and its element at location (x, y)
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     *
     * @return `std::tuple<Cell&, const Element&>` the reference to the cell and
     * the element at the location
     */
    EPIX_API std::tuple<Cell&, const Element&> get(int x, int y);
    EPIX_API std::tuple<const Cell&, const Element&> get(int x, int y) const;
    /**
     * @brief Get the cell at location (x, y)
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     *
     * @return `Cell&` the reference to the cell
     */
    EPIX_API Cell& get_cell(int x, int y);
    EPIX_API const Cell& get_cell(int x, int y) const;

    EPIX_API void swap(int x, int y, int tx, int ty);
    /**
     * @brief Get the element at location (x, y)
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     *
     * @return `const Element&` the reference to the element
     */
    EPIX_API const Element& get_elem(int x, int y) const;
    /**
     * @brief Create a cell at location (x, y) with the given element
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     * @param args arguments to pass to the `CellDef` constructor
     *
     * @return `Cell&` the reference to the created cell
     */
    template <typename... Args>
    Cell& create(int x, int y, Args&&... args) {
        CellDef def(std::forward<Args>(args)...);
        def.default_vel = get_default_vel(x, y);
        return create_def(x, y, def);
    }
    EPIX_API void extrusion(int x, int y, float intense);
    /**
     * @brief Remove a cell at location (x, y)
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     */
    EPIX_API void remove(int x, int y);
    /**
     * @brief Get the gravity vector at location (x, y). The gravity vector
     * can be varied based on the location to simulate different effects.
     *
     * @param x x-coordinate of the cell
     * @param y y-coordinate of the cell
     * @return `glm::vec2` the gravity vector
     */
    EPIX_API glm::vec2 get_grav(int x, int y);
    EPIX_API glm::vec2 get_default_vel(int x, int y);
    EPIX_API float air_density(int x, int y);
    EPIX_API int not_moving_threshold(glm::vec2 grav);
    /**
     * @brief Update the simulation by one frame
     *
     * @param delta time elapsed since the last frame
     */
    EPIX_API void update(float delta);
    EPIX_API void update_multithread(float delta);
    EPIX_API bool init_update_state();
    EPIX_API bool deinit_update_state();
    EPIX_API bool next_chunk();
    EPIX_API bool next_cell();
    EPIX_API void update_cell(float delta);
    EPIX_API void update_chunk(float delta);
    struct RaycastResult {
        int steps;
        int new_x;
        int new_y;
        std::optional<std::pair<int, int>> hit;
    };
    /**
     * @brief Perform a raycast from (x, y) to (tx, ty)
     *
     * @param x x-coordinate of the start point
     * @param y y-coordinate of the start point
     * @param tx x-coordinate of the end point
     * @param ty y-coordinate of the end point
     *
     * @return `RaycastResult` the result of the raycast, in which `moved` is
     * whether the ray is blocked at the beginning, `new_x` and `new_y` are the
     * coordinates of the last cell the ray passed through, and `hit` is the
     * coordinates of the cell the ray hit, if any.
     */
    EPIX_API RaycastResult raycast_to(int x, int y, int tx, int ty);
    /**
     * @brief Perform a collision check between two cells at (x, y) and (tx, ty)
     *
     * @param x x-coordinate of the first cell
     * @param y y-coordinate of the first cell
     * @param tx x-coordinate of the second cell
     * @param ty y-coordinate of the second cell
     * @return `bool` true if the two cells collide, false otherwise
     */
    EPIX_API bool collide(int x, int y, int tx, int ty);
    EPIX_API void touch(int x, int y);
};
}  // namespace epix::world::sand::components