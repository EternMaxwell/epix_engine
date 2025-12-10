// Example demonstrating use of epix_core module
import epix_core;

#include <iostream>

using namespace epix::core;

int main() {
    // Create a world
    World world(WorldId(1));
    
    std::cout << "World created with ID: " << world.id().get() << std::endl;
    std::cout << "Change tick: " << world.change_tick().get() << std::endl;
    
    // Test entities
    auto& entities = world.entities_mut();
    std::cout << "Entities system initialized" << std::endl;
    
    // Test components
    auto& components = world.components();
    std::cout << "Components system initialized" << std::endl;
    
    std::cout << "\n[SUCCESS] epix_core module working correctly!" << std::endl;
    
    return 0;
}
