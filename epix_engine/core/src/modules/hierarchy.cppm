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


export module epix.core:hierarchy;

import :entities;
import :world.decl;
import :component;

namespace core {
struct Parent {
    Entity entity;
    static void on_remove(World& world, HookContext ctx);
    static void on_insert(World& world, HookContext ctx);
};
struct Children {
    std::unordered_set<Entity> entities;
    static void on_remove(World& world, HookContext ctx);
    static void on_despawn(World& world, HookContext ctx);
};
}  // namespace core