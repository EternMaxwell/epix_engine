#include <cassert>
#include <iostream>
#include <vector>

#include "epix/core/archetype.hpp"
#include "epix/core/component.hpp"
#include "epix/core/type_system/type_registry.hpp"

using namespace epix::core;
using namespace epix::core::archetype;

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();

    Components comps(registry);
    // register two simple component types via ComponentInfo
    TypeId tid_int = registry->type_id<int>();
    TypeId tid_str = registry->type_id<std::string>();
    comps.emplace(tid_int, ComponentInfo(tid_int, ComponentDesc::from_type<int>()));
    comps.emplace(tid_str, ComponentInfo(tid_str, ComponentDesc::from_type<std::string>()));

    ComponentIndex index;
    Archetypes archs;

    std::vector<TypeId> table_components  = {tid_int};
    std::vector<TypeId> sparse_components = {tid_str};

    auto [id, inserted] = archs.get_id_or_insert(comps, TableId(1), table_components, sparse_components);
    (void)inserted;
    assert(archs.size() >= 1);

    auto opt = archs.get(id);
    assert(opt.has_value());
    const Archetype& a = opt.value().get();
    assert(a.contains(tid_int));
    assert(a.contains(tid_str));

    std::cout << "test_archetype passed\n";
    return 0;
}
