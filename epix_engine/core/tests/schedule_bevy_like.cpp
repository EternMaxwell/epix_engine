#include <cassert>
#include <iostream>
#include <print>

#include "epix/core/app/schedules.hpp"
#include "epix/core/query/fetch.hpp"
#include "epix/core/schedule/schedule.hpp"
#include "epix/core/system/commands.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::schedule;
using namespace epix::core::system;
using namespace epix::core::app;

// Example: Bevy-like schedule execution
// This demonstrates how to use the schedule system in a way similar to Bevy's ECS

// Define some components
struct Position {
    float x, y;
};

struct Velocity {
    float dx, dy;
};

struct Health {
    int value;
};

struct Player {};
struct Enemy {};

// Define schedule labels (similar to Bevy's schedule labels)
EPIX_MAKE_LABEL(UpdateSchedule);
EPIX_MAKE_LABEL(FixedUpdateSchedule);

// Define system set labels (similar to Bevy's system sets)
EPIX_MAKE_LABEL(MovementSet);
EPIX_MAKE_LABEL(PhysicsSet);
EPIX_MAKE_LABEL(InputSet);
EPIX_MAKE_LABEL(CombatSet);

// Systems
void setup_world(Commands commands) {
    std::println("Setting up world...");
    
    // Spawn player
    commands.spawn().insert(Player{}, Position{0.0f, 0.0f}, Velocity{1.0f, 0.5f}, Health{100});
    
    // Spawn enemies
    for (int i = 0; i < 3; i++) {
        commands.spawn().insert(Enemy{}, Position{float(i * 10), 0.0f}, Velocity{-0.5f, 0.0f}, Health{50});
    }
}

void apply_velocity(query::Query<query::Item<Position&, const Velocity&>> query) {
    for (auto&& [pos, vel] : query.iter()) {
        pos.x += vel.dx;
        pos.y += vel.dy;
    }
}

void print_player_position(query::Query<query::Item<Entity, const Position&, const Player&>> query) {
    for (auto&& [entity, pos, _] : query.iter()) {
        std::println("Player entity {} at position: ({}, {})", entity.index, pos.x, pos.y);
    }
}

void print_enemy_positions(query::Query<query::Item<Entity, const Position&, const Enemy&>> query) {
    for (auto&& [entity, pos, _] : query.iter()) {
        std::println("Enemy entity {} at position: ({}, {})", entity.index, pos.x, pos.y);
    }
}

void check_health(query::Query<query::Item<Entity, const Health&>> query) {
    for (auto&& [entity, health] : query.iter()) {
        if (health.value <= 0) {
            std::println("Entity {} is dead!", entity.index);
        }
    }
}

// Resource for tracking execution count
struct ExecutionCount {
    int update_count = 0;
    int fixed_update_count = 0;
};

void count_updates(Res<ExecutionCount> counter) {
    if (counter) {
        counter->update_count++;
        std::println("Update executed {} times", counter->update_count);
    }
}

void count_fixed_updates(Res<ExecutionCount> counter) {
    if (counter) {
        counter->fixed_update_count++;
        std::println("Fixed update executed {} times", counter->fixed_update_count);
    }
}

// Conditional systems
bool should_run_physics(Res<ExecutionCount> counter) {
    // Only run physics every other frame
    return counter && (counter->update_count % 2 == 0);
}

void physics_system() {
    std::println("Physics system running");
}

int main() {
    std::println("=== Bevy-like Schedule Execution Example ===\n");
    
    // Create world
    World world(WorldId(1));
    
    // Add execution counter resource
    world.insert_resource(ExecutionCount{});
    
    // Create a Schedules resource (similar to Bevy's Schedules)
    Schedules schedules;
    
    // ===== Create Update Schedule =====
    Schedule update_schedule(UpdateSchedule{});
    
    // Configure system sets with ordering
    update_schedule.configure_sets(
        sets(InputSet{}).before(MovementSet{})
    );
    update_schedule.configure_sets(
        sets(MovementSet{}).before(CombatSet{})
    );
    update_schedule.configure_sets(
        sets(PhysicsSet{}).before(MovementSet{})
    );
    
    // Add systems to the update schedule
    // Setup system (runs once)
    update_schedule.add_systems(
        into(setup_world)
            .set_name("setup_world")
    );
    
    // Movement systems
    update_schedule.add_systems(
        into(apply_velocity)
            .set_name("apply_velocity")
            .in_set(MovementSet{})
            .after(setup_world)
    );
    
    // Combat systems
    update_schedule.add_systems(
        into(check_health)
            .set_name("check_health")
            .in_set(CombatSet{})
    );
    
    // Debug print systems
    update_schedule.add_systems(
        into(print_player_position, print_enemy_positions)
            .set_names(std::array{"print_player", "print_enemies"})
            .after(apply_velocity)
            .chain()  // Chain them together so they run in sequence
    );
    
    // Counter system
    update_schedule.add_systems(
        into(count_updates)
            .set_name("count_updates")
    );
    
    // Conditional physics system
    update_schedule.add_systems(
        into(physics_system)
            .set_name("physics_system")
            .in_set(PhysicsSet{})
            .run_if(should_run_physics)
    );
    
    // ===== Create Fixed Update Schedule =====
    Schedule fixed_update_schedule(FixedUpdateSchedule{});
    
    fixed_update_schedule.add_systems(
        into(count_fixed_updates)
            .set_name("count_fixed_updates")
    );
    
    // Add schedules to the Schedules resource
    schedules.add_schedule(std::move(update_schedule));
    schedules.add_schedule(std::move(fixed_update_schedule));
    
    // Store schedules in world as resource
    world.insert_resource(std::move(schedules));
    
    // Prepare and execute schedules
    auto& world_schedules = world.resource_mut<Schedules>().value();
    
    auto& update_sched = world_schedules.schedule_mut(UpdateSchedule{});
    auto& fixed_sched = world_schedules.schedule_mut(FixedUpdateSchedule{});
    
    // Prepare schedules (validate and build execution graph)
    {
        auto result = update_sched.prepare(true);
        if (!result.has_value()) {
            std::println("Error preparing update schedule!");
            return 1;
        }
    }
    {
        auto result = fixed_sched.prepare(true);
        if (!result.has_value()) {
            std::println("Error preparing fixed update schedule!");
            return 1;
        }
    }
    
    // Create dispatcher for parallel execution
    SystemDispatcher dispatcher(world, 4);
    
    // Initialize systems
    update_sched.initialize_systems(world);
    fixed_sched.initialize_systems(world);
    
    // Run schedules multiple times (similar to Bevy's game loop)
    std::println("\n=== Running Update Schedule (Frame 1) ===");
    update_sched.execute(dispatcher);
    dispatcher.wait();
    
    std::println("\n=== Running Fixed Update Schedule ===");
    fixed_sched.execute(dispatcher);
    dispatcher.wait();
    
    std::println("\n=== Running Update Schedule (Frame 2) ===");
    update_sched.execute(dispatcher);
    dispatcher.wait();
    
    std::println("\n=== Running Update Schedule (Frame 3) ===");
    update_sched.execute(dispatcher);
    dispatcher.wait();
    
    // Verify execution counts
    auto counter = world.resource<ExecutionCount>();
    assert(counter.has_value());
    assert(counter->update_count == 3);
    assert(counter->fixed_update_count == 1);
    
    std::println("\n=== Test Passed! ===");
    std::println("Update schedule executed {} times", counter->update_count);
    std::println("Fixed update schedule executed {} times", counter->fixed_update_count);
    
    return 0;
}
