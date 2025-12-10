// Example usage of epix.core module (once migration is complete)
// This demonstrates the modern C++20 module-based API

// Import the core module instead of including headers
import epix.core;

// Or import specific partitions if you only need certain features
// import epix.core:tick;
// import epix.core:world;
// import epix.core:query;

#include <iostream>

using namespace epix::core;

int main() {
    // Example 1: Using the Tick system (already converted to module)
    Tick current_tick(100);
    Tick last_tick(50);
    
    std::cout << "Current tick: " << current_tick.get() << "\n";
    std::cout << "Relative ticks: " << current_tick.relative_to(last_tick).get() << "\n";
    
    // Example 2: Using ComponentTicks
    ComponentTicks comp_ticks(current_tick);
    std::cout << "Component added at tick: " << comp_ticks.added.get() << "\n";
    
    // Example 3: Once World is converted to module
    // World world(WorldId(1));
    // auto entity = world.spawn();
    // world.insert_resource<SomeResource>();
    
    // Example 4: Query system (once converted)
    // Query<Item<Entity, const Transform&, Mut<GlobalTransform>>> query;
    // for (auto&& [entity, transform, global] : query.iter()) {
    //     // Process entities
    // }
    
    // Example 5: System definition (once converted)
    // auto my_system = make_system([](Query<Item<const Transform&>> query) {
    //     for (auto&& [transform] : query.iter()) {
    //         // System logic
    //     }
    // });
    
    // Example 6: App building (once converted)
    // App app;
    // app.add_plugins(TransformPlugin{})
    //    .add_systems(Update, my_system)
    //    .run();
    
    std::cout << "Module example completed successfully!\n";
    return 0;
}
