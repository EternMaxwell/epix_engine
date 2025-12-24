#include <cassert>
#include <iostream>

#include "epix/core/storage/table.hpp"
#include "epix/core/type_system/type_registry.hpp"

using namespace epix::core::storage;
using namespace epix::core::type_system;
using namespace epix::core;

int main() {
    auto registry  = std::make_shared<TypeRegistry>();
    TypeId tid_int = registry->type_id<int>();
    TypeId tid_str = registry->type_id<std::string>();

    Tables tables(registry);
    std::vector<TypeId> types = {tid_int, tid_str};
    TableId table_id          = tables.get_id_or_insert(types);
    assert(tables.table_count() == 1);

    Table& table = tables.get_or_insert(types);
    assert(table.type_count() == types.size());

    std::cout << "table tests passed\n";
    return 0;
}
