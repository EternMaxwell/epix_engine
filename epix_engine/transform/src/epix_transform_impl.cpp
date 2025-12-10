// epix_transform implementation
module epix_transform;

using namespace epix;
using namespace epix::transform;
using namespace epix::core;

// ============================================================================
// Internal systems
// ============================================================================

void insert_global_transform(Commands cmd,
                             Query<Item<Entity>, Filter<With<Transform>, Without<GlobalTransform>>> query) {
    for (auto&& [entity] : query.iter()) {
        cmd.entity(entity).insert(GlobalTransform{.matrix = glm::mat4(1.0f)});
    }
}

void calculate_global_transform(Query<Item<Entity, const Transform&, Opt<const Parent&>, Mut<GlobalTransform>>> query) {
    std::deque<std::tuple<const Transform&, const Parent&, GlobalTransform&>> toProcess;
    std::unordered_map<Entity, GlobalTransform*> globalTransforms;
    
    for (auto&& [entity, transform, parent, globalTransform] : query.iter()) {
        if (parent) {
            toProcess.emplace_back(transform, *parent, globalTransform);
        } else {
            globalTransform->matrix  = transform.to_matrix();
            globalTransforms[entity] = globalTransform.ptr_mut();
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

// ============================================================================
// TransformPlugin implementation
// ============================================================================

void TransformPlugin::build(App& app) {
    app.add_systems(
        Last,
        into(into(calculate_global_transform)
                 .in_set(TransformSets::CalculateGlobalTransform)
                 .set_name("calculate global transform"),
             into(insert_global_transform)
                 .before(calculate_global_transform)
                 .set_name("insert global transform")));
}
