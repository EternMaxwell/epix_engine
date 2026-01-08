module;

#include <gtest/gtest.h>

export module epix.core:tests.table;

import std;
import :storage;
import :type_registry;

TEST(core, table) {
    using namespace core;

    auto registry  = std::make_shared<TypeRegistry>();
    TypeId tid_int = registry->type_id<int>();
    TypeId tid_str = registry->type_id<std::string>();

    Tables tables(registry);
    std::vector<TypeId> types = {tid_int, tid_str};
    TableId table_id          = tables.get_id_or_insert(types);
    EXPECT_EQ(tables.table_count(), 2);  // one for added table, one for empty table

    Table& table = tables.get_or_insert(types);
    EXPECT_EQ(table.type_count(), types.size());
}