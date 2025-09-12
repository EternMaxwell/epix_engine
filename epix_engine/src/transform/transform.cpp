#include "epix/transform/transform.h"

void test() {
    using namespace epix::transform;
    using namespace glm;
    TransformT t;
    vec3 localX = t.local_x();
}

#include "epix/transform/plugin.h"

using namespace epix::transform;
using namespace epix;

void insert_global_transform(Commands& cmd,
                             Query<Item<Entity>, Filter<With<Transform>, Without<GlobalTransform>>>& query) {
    for (auto&& [entity] : query.iter()) {
        cmd.entity(entity).emplace(GlobalTransform{.matrix = glm::mat4(1.0f)});
    }
}
void calculate_global_transform(Query<Item<Entity, Transform, Opt<Parent>, Mut<GlobalTransform>>>& query) {
    std::deque<std::tuple<const Transform&, const Parent&, GlobalTransform&>> toProcess;
    entt::dense_map<Entity, GlobalTransform*> globalTransforms;
    for (auto&& [entity, transform, parent, globalTransform] : query.iter()) {
        if (parent) {
            toProcess.emplace_back(transform, *parent, globalTransform);
        } else {
            globalTransform.matrix   = transform.to_matrix();
            globalTransforms[entity] = &globalTransform;
        }
    }
    while (!toProcess.empty()) {
        auto [transform, parent, globalTransform] = toProcess.front();
        toProcess.pop_front();

        auto it = globalTransforms.find(parent.entity);
        if (it != globalTransforms.end()) {
            globalTransform.matrix = it->second->matrix * transform.to_matrix();
        } else {
            toProcess.emplace_back(transform, parent, globalTransform);
        }
    }
}

EPIX_API void TransformPlugin::build(epix::App& app) {
    app.add_systems(
        Last,
        into(into(calculate_global_transform)
                 .in_set(TransformSets::CalculateGlobalTransform)
                 .set_name("calculate global transform"),
             into(insert_global_transform).before(calculate_global_transform).set_name("insert global transform")));
}