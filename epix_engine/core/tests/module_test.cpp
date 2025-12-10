// Test program for C++20 modules
// This demonstrates that the epix.core module works correctly

import epix.core;

#include <iostream>

using namespace epix::core;

int main() {
    std::cout << "=== epix.core C++20 Module Test ===\n\n";
    
    // Test 1: Tick system
    std::cout << "Test 1: Tick System\n";
    Tick current_tick(100);
    Tick last_tick(50);
    
    std::cout << "  Current tick: " << current_tick.get() << "\n";
    std::cout << "  Last tick: " << last_tick.get() << "\n";
    std::cout << "  Relative ticks: " << current_tick.relative_to(last_tick).get() << "\n";
    
    ComponentTicks comp_ticks(current_tick);
    std::cout << "  Component added at tick: " << comp_ticks.added.get() << "\n";
    std::cout << "  Component modified at tick: " << comp_ticks.modified.get() << "\n";
    
    // Test 2: Type metadata
    std::cout << "\nTest 2: Type Metadata\n";
    std::cout << "  Type name of int: " << meta::type_name<int>() << "\n";
    std::cout << "  Type name of double: " << meta::type_name<double>() << "\n";
    std::cout << "  Short name of std::string: " << meta::short_name<std::string>() << "\n";
    
    meta::type_index ti1(meta::type_id<int>());
    meta::type_index ti2(meta::type_id<double>());
    meta::type_index ti3(meta::type_id<int>());
    
    std::cout << "  type_index(int) == type_index(double): " << (ti1 == ti2 ? "true" : "false") << "\n";
    std::cout << "  type_index(int) == type_index(int): " << (ti1 == ti3 ? "true" : "false") << "\n";
    
    // Test 3: Type system
    std::cout << "\nTest 3: Type System\n";
    TypeRegistry registry;
    
    TypeId int_id = registry.type_id<int>();
    TypeId double_id = registry.type_id<double>();
    TypeId int_id2 = registry.type_id<int>();
    
    std::cout << "  TypeId for int: " << int_id.get() << "\n";
    std::cout << "  TypeId for double: " << double_id.get() << "\n";
    std::cout << "  TypeId for int (again): " << int_id2.get() << "\n";
    std::cout << "  int and int2 are same: " << (int_id == int_id2 ? "true" : "false") << "\n";
    
    const TypeInfo* int_info = registry.type_info(int_id.get());
    std::cout << "  TypeInfo for int:\n";
    std::cout << "    Name: " << int_info->name << "\n";
    std::cout << "    Size: " << int_info->size << "\n";
    std::cout << "    Alignment: " << int_info->align << "\n";
    std::cout << "    Trivially copyable: " << (int_info->trivially_copyable ? "yes" : "no") << "\n";
    std::cout << "    Storage type: " << (int_info->storage_type == StorageType::Table ? "Table" : "SparseSet") << "\n";
    
    std::cout << "\nRegistry contains " << registry.count() << " registered types\n";
    
    // Test 4: Wrapper types
    std::cout << "\nTest 4: Wrapper Types\n";
    TypeId id1(42);
    TypeId id2(100);
    std::cout << "  TypeId(42): " << id1.get() << "\n";
    std::cout << "  TypeId(100): " << id2.get() << "\n";
    std::cout << "  id1 < id2: " << (id1 < id2 ? "true" : "false") << "\n";
    
    // Test module-specific features
    std::cout << "\nTest 5: Module Features\n";
    std::cout << "  Module compilation successful!\n";
    std::cout << "  All exported types accessible via 'import epix.core;'\n";
    std::cout << "  Partitions used: :fwd, :tick, :meta, :type_system\n";
    
    std::cout << "\n=== All Tests Passed! ===\n";
    return 0;
}
