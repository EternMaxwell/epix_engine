#include <epix/app.h>

#include <chrono>
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
void spawn_entities(Commands command) {
    spdlog::info("Spawning entities...");
    for (int i = 0; i < 10; i++) {
        auto entity_cmd = command.spawn(
            Bundle{.position = Position(0, 0), .health = Health(100)}
        );
        static thread_local std::mt19937 rng{std::random_device{}()};
        static thread_local std::uniform_real_distribution<float> dist(
            -1.0f, 1.0f
        );
        // randomly insert Velocity.
        if (dist(rng) > 0.5f) {
            entity_cmd.emplace(
                std::move(Velocity(dist(rng) * 100, dist(rng) * 100))
            );
        }
    }
    spdlog::info("Spawned 10 entities.");
}

// print the current state of entities
void check_entities(
    Commands command,
    Query<Get<Entity, const Position, const Health, Opt<const Velocity>>> query
) {
    spdlog::info("Checking entities...");
    for (auto [entity, position, health, velocity] : query.iter()) {
        spdlog::info(
            "Entity {}: Position ({}, {}), Health {}, Velocity ({}, {})",
            entity.index(), position.x, position.y, health.value,
            velocity ? std::to_string(velocity->x) : "N/A",
            velocity ? std::to_string(velocity->y) : "N/A"
        );
    }
    spdlog::info("Checked entities.");
}

// Update systems
void update_positions(
    Query<Get<Mut<Position>, const Velocity>> query,
    Local<std::optional<std::chrono::steady_clock::time_point>> timer
) {
    spdlog::info("Updating positions...");
    if (!timer->has_value()) {
        *timer = std::chrono::steady_clock::now();
    }
    auto now = std::chrono::steady_clock::now();
    double delta_time =
        std::chrono::duration<double, std::chrono::seconds::period>(
            now - timer->value()
        )
            .count();
    for (auto [position, velocity] : query.iter()) {
        position.x += velocity.x * delta_time;
        position.y += velocity.y * delta_time;
    }
    timer->value() = now;
    spdlog::info("Updated positions.");
}

void despawn_to_far(
    Commands command, Query<Get<Entity, const Position>> query
) {
    for (auto [entity, position] : query.iter()) {
        if (std::fabsf(
                std::sqrtf(position.x * position.x + position.y * position.y)
            ) > 1.0f) {
            command.entity(entity).despawn();
            spdlog::info(
                "Enqueue despawn command for entity {}: Position ({}, {})",
                entity.index(), position.x, position.y
            );
        }
    }
}

void random_assign_vel(
    Commands command,
    Query<Get<Entity, const Position, Opt<const Velocity>>> query
) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto [entity, position, velocity] : query.iter()) {
        if (!velocity) {
            if (dist(rng) > 0.5f) {
                float x = dist(rng) * 100;
                float y = dist(rng) * 100;
                command.entity(entity).emplace(Velocity(x, y));
                spdlog::info(
                    "Insert Velocity to entity {}: ({}, {})", entity.index(), x,
                    y
                );
            }
        } else if (dist(rng) > 0.5f) {
            command.entity(entity).erase<Velocity>();
            spdlog::info(
                "Remove Velocity from entity {}: ({}, {})", entity.index(),
                velocity->x, velocity->y
            );
        }
    }
}

void random_decrease_health(
    Commands command, Query<Get<Entity, Mut<Health>>> query
) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<int> dist(1, 100);
    for (auto [entity, health] : query.iter()) {
        if (dist(rng) > 50) {
            int prev_health = health.value;
            health.value -= dist(rng);
            spdlog::info(
                "Decrease Health of entity {}: {} to {}", entity.index(),
                prev_health, health.value
            );
        }
        if (health.value <= 0) {
            command.entity(entity).erase<Health>();
            spdlog::info(
                "Enqueue remove Health from entity {}: {}", entity.index(),
                health.value
            );
        }
    }
}

void assign_health(
    Commands command, Query<Get<Entity, const Position, Has<Health>>> query
) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<int> dist(1, 100);
    for (auto [entity, position, has_health] : query.iter()) {
        if (!has_health) {
            int health_value = dist(rng);
            command.entity(entity).emplace(Health(health_value));
            spdlog::info(
                "Insert Health to entity {}: {}", entity.index(), health_value
            );
        }
    }
}

struct FrameCounter {
    int count = 0;
};

void quit(EventWriter<AppExit> exit_event, Local<FrameCounter> frame_counter) {
    frame_counter->count++;
    if (frame_counter->count >= 10) {
        exit_event.write(AppExit{});
    }
}

int main() {
    App app = App::create();
    app.add_plugin(LoopPlugin{})
        .add_systems(Startup, into(spawn_entities, check_entities).chain())
        .add_systems(
            Update, into(
                        into(
                            update_positions, random_assign_vel, despawn_to_far,
                            check_entities
                        )
                            .chain(),
                        into(assign_health, random_decrease_health)
                    )
        )
        .add_systems(Update, into(quit));
    app.run();
}