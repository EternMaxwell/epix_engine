#pragma once

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

// === DECLARATION  ===//
namespace epix::world::sand {
struct Registry_T;
using Registry       = Registry_T*;
using RegistryUnique = std::unique_ptr<Registry_T>;
using RegistryShared = std::shared_ptr<Registry_T>;
struct Element;
struct Particle;  // Previously known as Cell
struct PartDef;
struct Chunk;
struct World_T;
using World       = World_T*;
using WorldUnique = std::unique_ptr<World_T>;
using WorldShared = std::shared_ptr<World_T>;
struct Simulator_T;  // This class is used to update the world. Constructing
                     // should be cheap.
using Simulator       = Simulator_T*;
using SimulatorUnique = std::unique_ptr<Simulator_T>;
using SimulatorShared = std::shared_ptr<Simulator_T>;
}  // namespace epix::world::sand

// === STRUCTS ===//
namespace epix::world::sand {
enum class ElemType : uint8_t { POWDER, LIQUID, SOLID, GAS, PLACEHOLDER };
struct Element {
   private:
    ElemType m_type = ElemType::SOLID;

   public:
    float density     = 0.0f;
    float restitution = 0.1f;
    float friction    = 0.0f;
    float awake_rate  = 1.0f;
    std::string name;
    std::string description = "";

   private:
    std::function<glm::vec4()> fn_color_gen;
    EPIX_API static Element place_holder();

    friend struct Registry_T;

   public:
    EPIX_API Element(const std::string& name, ElemType type);
    EPIX_API static Element solid(const std::string& name);
    EPIX_API static Element liquid(const std::string& name);
    EPIX_API static Element powder(const std::string& name);
    EPIX_API static Element gas(const std::string& name);
    EPIX_API Element& set_type(ElemType type);
    EPIX_API Element& set_density(float density);
    EPIX_API Element& set_restitution(float bouncing);
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

struct Particle {
   public:
    int elem_id        = -1;
    glm::vec4 color    = glm::vec4(0.0f);
    glm::vec2 velocity = glm::vec2(0.0f);
    glm::vec2 inpos    = glm::vec2(0.0f);
    int not_move_count = 0;

    uint8_t bitfield = 0;

    static constexpr uint8_t FREEFALL = 1 << 0;
    static constexpr uint8_t UPDATED  = 1 << 1;
    static constexpr uint8_t BURNING  = 1 << 2;

   public:
    EPIX_API bool freefall() const;
    EPIX_API void set_freefall(bool freefall);
    EPIX_API bool updated() const;
    EPIX_API void set_updated(bool updated);

    EPIX_API bool valid() const;
    EPIX_API operator bool() const;
    EPIX_API bool operator!() const;
};

struct PartDef {
    std::variant<std::string, int> id;

    EPIX_API PartDef(const std::string& name);
    EPIX_API PartDef(int id);
};

struct Registry_T {
   private:
    entt::dense_map<std::string, int> elemId_map;
    std::vector<Element> elements;

   public:
    EPIX_API Registry_T();
    EPIX_API Registry_T(const Registry_T& other);
    EPIX_API Registry_T(Registry_T&& other);
    EPIX_API Registry_T& operator=(const Registry_T& other);
    EPIX_API Registry_T& operator=(Registry_T&& other);
    EPIX_API static Registry_T* create();
    EPIX_API static void destroy(Registry_T* registry);
    EPIX_API static std::unique_ptr<Registry_T> create_unique();
    EPIX_API static std::shared_ptr<Registry_T> create_shared();

    EPIX_API int register_elem(const std::string& name, const Element& elem);
    EPIX_API int register_elem(const Element& elem);
    EPIX_API int id_of(const std::string& name) const;
    EPIX_API const std::string& name_of(int id) const;
    EPIX_API int count() const;
    EPIX_API const Element& get_elem(const std::string& name) const;
    EPIX_API const Element& get_elem(int id) const;
    EPIX_API const Element& operator[](int id) const;
    EPIX_API const Element& operator[](const std::string& name) const;
    EPIX_API void add_equiv(const std::string& name, const std::string& equiv);

    EPIX_API Particle create_particle(const PartDef& def) const;
};

struct Chunk {
    using grid_t = epix::utils::grid::sparse_grid<Particle, 2>;

    const int width;
    const int height;

   private:
    grid_t grid;
    bool _updated = false;

   public:
    EPIX_API Chunk(int width, int height);
    EPIX_API Chunk(const Chunk& other);
    EPIX_API Chunk(Chunk&& other);
    EPIX_API Chunk& operator=(const Chunk& other);
    EPIX_API Chunk& operator=(Chunk&& other);
    EPIX_API void reset_updated();
    EPIX_API Particle& get(int x, int y);
    EPIX_API const Particle& get(int x, int y) const;
    EPIX_API void insert(int x, int y, const Particle& cell);
    EPIX_API void insert(int x, int y, Particle&& cell);
    EPIX_API void remove(int x, int y);

    EPIX_API int size(int dim) const;
    EPIX_API bool contains(int x, int y) const;

    EPIX_API grid_t::view_t view();
    EPIX_API grid_t::const_view_t view() const;

    EPIX_API bool updated() const;
    EPIX_API size_t count() const;
};

struct World_T {
    using grid_t = epix::utils::grid::extendable_grid<Chunk, 2>;
    grid_t m_chunks;
    const int m_chunk_size;
    const Registry_T* m_registry;
    std::unique_ptr<BS::thread_pool<BS::tp::none>> m_thread_pool;

    struct NotMovingThresholdSetting {
        float power     = 0.3f;
        float numerator = 4000.0f;
    } not_moving_threshold_setting;

   public:
    EPIX_API World_T(const Registry_T* registry, int chunk_size);
    // === CONSTRUCTORS ===//
    EPIX_API static World_T* create(const Registry_T* registry, int chunk_size);
    EPIX_API static std::unique_ptr<World_T> create_unique(
        const Registry_T* registry, int chunk_size
    );
    EPIX_API static std::shared_ptr<World_T> create_shared(
        const Registry_T* registry, int chunk_size
    );

    // === WORLD GLOBAL ===//
    EPIX_API int chunk_size() const;
    EPIX_API const Registry_T& registry() const;

    // === HELPER FUNCTIONS ===//
    EPIX_API std::pair<int, int> to_chunk_pos(int x, int y) const;
    EPIX_API std::pair<int, int> in_chunk_pos(int x, int y) const;
    EPIX_API std::pair<std::pair<int, int>, std::pair<int, int>> decode_pos(
        int x, int y
    ) const;

    // === CHUNK OPERATIONS ===//
    EPIX_API void insert_chunk(int x, int y, Chunk&& chunk);
    EPIX_API void insert_chunk(int x, int y);
    EPIX_API void remove_chunk(int x, int y);
    EPIX_API void shrink_to_fit();

    // === OTHER DATA ===//
    EPIX_API glm::vec2 gravity_at(int x, int y) const;
    EPIX_API glm::vec2 default_velocity_at(int x, int y) const;
    EPIX_API float air_density_at(int x, int y) const;
    EPIX_API int not_moving_threshold(const glm::vec2& grav) const;

    // === PART CHECK FUNCTIONS ===//
    EPIX_API bool valid(int x, int y) const;
    EPIX_API bool contains(int x, int y) const;

    // === PART GET FUNCTIONS ===//
    EPIX_API Particle& particle_at(int x, int y);
    EPIX_API const Particle& particle_at(int x, int y) const;
    EPIX_API const Element& elem_at(int x, int y) const;
    EPIX_API std::tuple<Particle&, const Element&> get(int x, int y);
    EPIX_API std::tuple<const Particle&, const Element&> get(int x, int y)
        const;

    // === PARTICLE MOVE FUNCTIONS ===//
    EPIX_API void swap(int x, int y, int tx, int ty);
    EPIX_API void insert(int x, int y, Particle&& cell);
    EPIX_API void remove(int x, int y);

    // === CHUNK ITERATION ===//
    EPIX_API grid_t::view_t view();
    EPIX_API grid_t::const_view_t view() const;
};

struct Simulator_T {
   private:
    struct LiquidSpreadSetting {
        float spread_len;
        float prefix;
    } liquid_spread_setting;
    struct UpdateState {
        bool random_state;
        bool xorder;
        bool yorder;
        bool x_outer;
        EPIX_API void next();
    } update_state;
    std::optional<glm::ivec2> max_travel;
    struct PowderSlideSetting {
        bool always_slide;
        float prefix;
    } powder_slide_setting;

    struct SimChunkData {
        uint32_t time_threshold;
        uint32_t time_since_last_swap;
        int width;
        int height;
        int active_area[4];
        int next_active_area[4];

        struct vec2_boolify {
            bool operator()(const glm::vec2& v) const { return true; }
        };
        utils::grid::packed_grid<glm::vec2, 2, vec2_boolify> velocity;
        utils::grid::packed_grid<glm::vec2, 2, vec2_boolify> velocity_back;
        utils::grid::packed_grid<float, 2> pressure;
        utils::grid::packed_grid<float, 2> pressure_back;
        utils::grid::packed_grid<float, 2> temperature;
        utils::grid::packed_grid<float, 2> temperature_back;

        EPIX_API SimChunkData(int width, int height);
        EPIX_API SimChunkData(const SimChunkData& other);
        EPIX_API SimChunkData(SimChunkData&& other);
        EPIX_API SimChunkData& operator=(const SimChunkData& other);
        EPIX_API SimChunkData& operator=(SimChunkData&& other);

        EPIX_API void touch(int x, int y);
        EPIX_API void swap(int chunk_size);
        EPIX_API void swap_maps();
        EPIX_API void step_time(int chunk_size);
        EPIX_API bool active() const;
    };

    utils::grid::extendable_grid<SimChunkData, 2> m_chunk_data;

   public:
    EPIX_API Simulator_T();

    EPIX_API static Simulator_T* create();
    EPIX_API static std::unique_ptr<Simulator_T> create_unique();
    EPIX_API static std::shared_ptr<Simulator_T> create_shared();

    EPIX_API void assure_chunk(const World_T* world, int x, int y);
    EPIX_API const utils::grid::extendable_grid<SimChunkData, 2>& chunk_data(
    ) const;

    struct RaycastResult {
        int steps;
        int new_x;
        int new_y;
        std::optional<std::pair<int, int>> hit;
    };
    EPIX_API RaycastResult
    raycast_to(const World_T* world, int x, int y, int tx, int ty);
    EPIX_API RaycastResult
    raycast_to(const World_T* world, int x, int y, float dx, float dy);
    EPIX_API bool collide(World_T* world, int x, int y, int tx, int ty);
    EPIX_API bool collide(
        World_T* world, Particle& part1, Particle& part2, const glm::vec2& dir
    );

    EPIX_API void touch(World_T* world, int x, int y);

    // === INSERT REMOVE OPERATIONS OVERLAY ===//
    EPIX_API void insert(World_T* world, int x, int y, Particle&& cell);
    EPIX_API void remove(World_T* world, int x, int y);

    // === SIMULATION CHUNK DATA GET SET ===//
    EPIX_API void read_velocity(
        const World_T* world, int x, int y, glm::vec2* velocity
    ) const;
    EPIX_API void write_velocity(
        World_T* world, int x, int y, const glm::vec2& velocity
    );
    EPIX_API void read_pressure(
        const World_T* world, int x, int y, float* pressure
    ) const;
    EPIX_API void write_pressure(World_T* world, int x, int y, float pressure);
    EPIX_API void read_temperature(
        const World_T* world, int x, int y, float* temperature
    ) const;
    EPIX_API void write_temperature(
        World_T* world, int x, int y, float temperature
    );

    // === SIMULATION FUNCTIONS ===//
    EPIX_API void apply_viscosity(
        World_T* world, Particle& p, int x, int y, int tx, int ty
    );
    EPIX_API void step_particle(World_T* world, int x, int y, float delta);
    EPIX_API void step(World_T* world, float delta);
    EPIX_API void step_maps(
        World_T* world, float delta
    );  // velocity, pressure, temperature updates
};
}  // namespace epix::world::sand