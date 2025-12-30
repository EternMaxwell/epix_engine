module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


export module epix.core:world.spec;

import :world.interface;
import :world.entity_ref;

export namespace core {
template <typename... Args>
EntityWorldMut World::spawn(Args&&... args)
    requires((std::constructible_from<std::decay_t<Args>, Args> || is_bundle<Args>) && ...)
{
    auto spawn_bundle = [&]<typename T>(T&& bundle) {
        flush();  // needed for Entities::alloc.
        auto e       = _entities.alloc();
        auto spawner = BundleSpawner::create<T&&>(*this, change_tick());
        spawner.spawn_non_exist(e, std::forward<T>(bundle));
        flush();  // flush to ensure no delayed operations.
        return EntityWorldMut(e, this);
    };
    if constexpr (sizeof...(Args) == 1 && (is_bundle<Args> && ...)) {
        return spawn_bundle(std::forward<Args>(args)...);
    }
    return spawn_bundle(make_bundle<std::decay_t<Args>...>(std::forward_as_tuple(std::forward<Args>(args))...));
}
}  // namespace core