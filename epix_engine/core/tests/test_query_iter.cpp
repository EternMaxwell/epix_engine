#include <cassert>
#include <iostream>
#include <type_traits>

#include "epix/core/bundle.hpp"
#include "epix/core/bundleimpl.hpp"
#include "epix/core/component.hpp"
#include "epix/core/entities.hpp"
#include "epix/core/query/fetch.hpp"
#include "epix/core/query/iter.hpp"
#include "epix/core/query/state.hpp"
#include "epix/core/storage.hpp"
#include "epix/core/world_cell.hpp"

using namespace epix::core;
using namespace epix::core::query;

struct P {
    int a;
    P(int v) : a(v) {}
};

struct World : public WorldCell {
    World() : WorldCell(WorldId(1), std::make_shared<type_system::TypeRegistry>()) {}

    template <typename T>
    EntityRefMut spawn(T&& bundle)
        requires(bundle::is_bundle<std::remove_cvref_t<T>>)
    {
        auto e       = entities_mut().alloc();
        auto spawner = BundleSpawner::create<T>(*this, change_tick());
        spawner.spawn_non_exist(e, std::forward<T>(bundle));
        return EntityRefMut(e, this);
    }
};

int main() {
    ::World wc;

    // spawn a couple entities with P
    for (int i = 0; i < 5; ++i) {
        wc.spawn(make_init_bundle<P>(std::forward_as_tuple(i)));
        wc.spawn(make_init_bundle<std::string, P>(std::forward_as_tuple("entity"), std::forward_as_tuple(i)));
        wc.spawn(make_init_bundle<int>(std::forward_as_tuple(i)));
    }
    wc.flush();

    // Create QueryState for Ref<P>
    using QD = Item<Entity, Opt<Ref<std::string>>>;
    using QF = Filter<With<P>, Without<int>>;
    auto qs  = QueryState<QD, QF>::create(wc);
    std::println(std::cout, "P type_id: {}", wc.type_registry().type_id<P>().get());

    // Iterate using QueryIter
    auto iter = QueryIter<QD, QF>::create_begin(&wc, &qs, wc.last_change_tick(), wc.change_tick());

    size_t count = 0;
    for (auto&& [entity, item] : iter) {
        if (item) {
            std::println(std::cout, "Entity {} has string '{}'", entity.index, item.value().get());
        } else {
            std::println(std::cout, "Entity {} has no string", entity.index);
        }
        ++count;
    }

    assert(count >= 2);

    std::cout << "test_query_iter passed\n";
    return 0;
}
