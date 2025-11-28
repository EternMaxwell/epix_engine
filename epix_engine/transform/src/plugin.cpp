#include <ranges>
#include <stack>

#include "epix/core.hpp"
#include "epix/transform.hpp"

using namespace epix;
using namespace epix::transform;

void insert_global_transform(Commands cmd,
                             Query<Item<Entity>, Filter<With<Transform>, Without<GlobalTransform>>> query) {
    for (auto&& [entity] : query.iter()) {
        cmd.entity(entity).insert(GlobalTransform{.matrix = glm::mat4(1.0f)});
    }
}
void calculate_global_transform(
    Query<Item<Entity, Ref<Transform>, Opt<const Children&>, Opt<const Parent&>, Mut<GlobalTransform>>> query) {
    std::unordered_map<Entity, Entity> change_root;
    for (auto&& [entity, transform, children, parent, globalTransform] : query.iter()) {
        if (transform.is_modified() || transform.is_added()) {
            Entity root;
            if (auto root_it = change_root.find(entity); root_it != change_root.end()) {
                root = root_it->second;
            } else {
                root                = entity;
                change_root[entity] = entity;
            }
            std::stack<Entity> child_stack;
            child_stack.push(entity);
            while (!child_stack.empty()) {
                Entity current = child_stack.top();
                child_stack.pop();
                if (children.has_value()) {
                    for (const auto& child : children->get().entities) {
                        child_stack.push(child);
                        change_root[child] = root;
                    }
                }
            }
        }
    }
    for (auto entity : change_root | std::views::filter([&](const auto& pair) { return pair.first == pair.second; }) |
                           std::views::keys) {
        auto [_, transform, children, parent, globalTransform] = query.get(entity).value();

        globalTransform.get_mut() = GlobalTransform{
            .matrix = transform.get().to_matrix(),
        };
        if (children.has_value()) {
            std::stack<Entity> child_stack;
            for (const auto& child : children->get().entities) {
                auto [_, child_transform, child_children, child_parent, child_globalTransform] =
                    query.get(child).value();
                child_globalTransform.get_mut() = GlobalTransform{
                    .matrix = globalTransform.get_mut().matrix * child_transform.get().to_matrix(),
                };
                if (child_children.has_value()) child_stack.push(child);
            }
            while (!child_stack.empty()) {
                Entity current = child_stack.top();
                child_stack.pop();
                auto [_, current_transform, current_children, current_parent, current_globalTransform] =
                    query.get(current).value();
                if (current_children.has_value()) {
                    for (const auto& child : current_children->get().entities) {
                        auto [_, child_transform, child_children, child_parent, child_globalTransform] =
                            query.get(child).value();
                        child_globalTransform.get_mut() = GlobalTransform{
                            .matrix = current_globalTransform.get_mut().matrix * child_transform.get().to_matrix(),
                        };
                        if (child_children.has_value()) child_stack.push(child);
                    }
                }
            }
        }
    }
}

void TransformPlugin::build(epix::App& app) {
    app.configure_sets(sets(TransformSets::CalculateGlobalTransform));
    app.add_systems(
        Last,
        into(into(calculate_global_transform)
                 .in_set(TransformSets::CalculateGlobalTransform)
                 .set_name("calculate global transform"),
             into(insert_global_transform).before(calculate_global_transform).set_name("insert global transform")));
}