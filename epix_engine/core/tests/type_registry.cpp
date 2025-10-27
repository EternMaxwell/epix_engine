#include <cassert>
#include <iostream>

#include "epix/core/type_system/type_registry.hpp"

using namespace epix::core::type_system;

int main() {
    TypeRegistry reg;
    size_t id_int = reg.type_id<int>();
    size_t id_str = reg.type_id<std::string>();
    // same type should return same id
    assert(id_int == reg.type_id<int>().get());
    assert(id_str == reg.type_id<std::string>().get());

    const TypeInfo* ti_int = reg.type_info(id_int);
    const TypeInfo* ti_str = reg.type_info(id_str);
    assert(ti_int->size == sizeof(int));
    assert(ti_str->size == sizeof(std::string));
    assert(ti_int->align == alignof(int));

    // names should match type id name
    assert(ti_int->name == epix::core::meta::type_id<int>().name());

    std::cout << "type_registry tests passed\n";
    return 0;
}
