// Test program for epix.core module
import epix.core;

#include <iostream>

int main() {
    std::cout << "Testing epix.core module..." << std::endl;
    
    // Test using types from the module
    epix::Entity e1(0, 0);
    epix::Entity e2(1, 0);
    
    std::cout << "Entity 1 index: " << e1.index << std::endl;
    std::cout << "Entity 2 index: " << e2.index << std::endl;
    
    if (e1 != e2) {
        std::cout << "Entities are different - PASS" << std::endl;
    }
    
    std::cout << "Module test completed successfully!" << std::endl;
    return 0;
}
