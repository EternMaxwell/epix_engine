module;

#include <spdlog/spdlog.h>

module epix.transform;

using namespace epix::transform;
using namespace epix::core;

void calculate_global_transform(
    Commands cmd,
    Query<Item<Entity, Ref<Transform>, Opt<const Children&>, Opt<const Parent&>, Opt<Mut<GlobalTransform>>>> query) {
    std::unordered_map<Entity, Entity> change_root;
    for (auto&& [entity, transform, children, parent, globalTransform] : query.iter()) {
        if (transform.is_modified() || transform.is_added() || !globalTransform.has_value()) {
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
                auto current_item = query.get(current);
                if (!current_item.has_value()) continue;

                auto [_, current_transform, current_children, current_parent, current_global] = *current_item;
                if (current_children.has_value()) {
                    for (const auto& child : current_children->get().entities()) {
                        child_stack.push(child);
                        change_root[child] = root;
                    }
                }
            }
        }
    }
    std::unordered_map<Entity, GlobalTransform> not_added_globals;
    for (auto entity : std::views::keys(std::views::filter(change_root, [&](const auto& pair) {
             return pair.first == pair.second;
         }))) {
        auto [_, transform, children, parent, globalTransform] = query.get(entity).value();

        auto calculate_global = [&](this auto&& self, Entity ent, const GlobalTransform& parent_matrix) -> void {
            auto [_, transform, children, parent, globalTransform] = query.get(ent).value();
            glm::mat4 global_matrix                                = parent_matrix.matrix * transform.get().to_matrix();
            const auto& global_ref =
                [&] {
                    if (globalTransform.has_value()) {
                        globalTransform->get_mut() = GlobalTransform{.matrix = global_matrix};
                        return std::ref(globalTransform->get());
                    } else {
                        return std::cref(
                            not_added_globals.emplace(ent, GlobalTransform{.matrix = global_matrix}).first->second);
                    }
                }()
                    .get();
            if (children.has_value()) {
                for (const auto& child : children->get().entities()) {
                    self(child, global_ref);
                }
            }
        };

        const auto& global_ref =
            [&] {
                if (globalTransform.has_value()) {
                    globalTransform->get_mut() = GlobalTransform{
                        .matrix = transform.get().to_matrix(),
                    };
                    return std::ref(globalTransform->get());
                } else {
                    return std::cref(
                        not_added_globals.emplace(entity, GlobalTransform{.matrix = transform.get().to_matrix()})
                            .first->second);
                }
            }()
                .get();
        if (children.has_value()) {
            for (const auto& child : children->get().entities()) {
                calculate_global(child, global_ref);
            }
        }
    }
    for (auto&& [entity, global_transform] : not_added_globals) {
        cmd.entity(entity).insert(global_transform);
    }
}

void TransformPlugin::build(App& app) {
    spdlog::debug("[transform] Building TransformPlugin.");
    app.configure_sets(sets(TransformSets::CalculateGlobalTransform));
    app.add_systems(Last, into(calculate_global_transform)
                              .in_set(TransformSets::CalculateGlobalTransform)
                              .set_name("calculate global transform"));
}
