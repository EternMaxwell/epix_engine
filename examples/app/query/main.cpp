#include <epix/app.h>

#include <random>

using namespace epix;

struct Position {
    float x, y;
    Position(float x, float y) : x(x), y(y) {}
    Position() : x(0), y(0) {}
    Position(const Position&)            = delete;
    Position(Position&&)                 = default;
    Position& operator=(const Position&) = delete;
    Position& operator=(Position&&)      = default;
};
struct Health {
    int value;
    Health(int value) : value(value) {}
    Health() : value(0) {}
    Health(const Health&)            = delete;
    Health(Health&&)                 = default;
    Health& operator=(const Health&) = delete;
    Health& operator=(Health&&)      = default;
};
struct Velocity {
    float x, y;
    Velocity(float x, float y) : x(x), y(y) {}
    Velocity() : x(0), y(0) {}
    Velocity(const Velocity&)            = delete;
    Velocity(Velocity&&)                 = default;
    Velocity& operator=(const Velocity&) = delete;
    Velocity& operator=(Velocity&&)      = default;
};
struct Bundle {
    Position position;
    Health health;
    auto unpack() {
        return std::make_tuple(std::move(position), std::move(health));
    }
};

// Startup systems

// spawn entities
void spawn_entities(Command command) {
    spdlog::info("Spawning entities...");
    for (int i = 0; i < 10; i++) {
        auto entity_cmd = command.spawn(Bundle{});
        static thread_local std::mt19937 rng{std::random_device{}()};
        static thread_local std::uniform_real_distribution<float> dist(
            -1.0f, 1.0f
        );
        // randomly insert Velocity.
        if (dist(rng) > 0.5f) {
            entity_cmd.insert(std::move(Velocity(dist(rng), dist(rng))));
        }
    }
    spdlog::info("Spawned 100 entities.");
}

//

int main() { App app = App::create(); }