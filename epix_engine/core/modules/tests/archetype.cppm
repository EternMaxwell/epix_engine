module;

#include <gtest/gtest.h>

export module epix.core:tests.archetype;

import std;

import :archetype;

using namespace core;

TEST(core, archetype) {
    auto registry = std::make_shared<TypeRegistry>();

    // register two simple component types via ComponentInfo
    TypeId tid_int = registry->type_id<int>();
    TypeId tid_str = registry->type_id<std::string>();

    ComponentIndex index;
    Archetypes archs;

    std::vector<TypeId> table_components  = {tid_int};
    std::vector<TypeId> sparse_components = {tid_str};

    auto [id, inserted] = archs.get_id_or_insert(TableId(1), table_components, sparse_components);
    (void)inserted;
    EXPECT_GE(archs.size(), 1);

    auto opt = archs.get(id);
    EXPECT_TRUE(opt.has_value());
    const Archetype& a = opt.value().get();
    EXPECT_TRUE(a.contains(tid_int));
    EXPECT_TRUE(a.contains(tid_str));
}