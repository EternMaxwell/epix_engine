# Transform module

`epix.transform` provides local transforms, computed world transforms, and hierarchy propagation.

```cpp
import epix.transform;
using namespace epix::transform;
```

## Types and setup

- `TransformT<T>` stores `translation`, quaternion `rotation`, and component-wise `scaler`.
- `Transform` and `DTransform` are the `float` and `double` aliases.
- `GlobalTransform` stores the propagated world-space `glm::mat4`.
- `TransformPlugin` runs propagation in `app::Last`, in
  `TransformSystems::Propagate`.

```cpp
app.add_plugins(TransformPlugin{});

commands.spawn(Transform::from_xyz(2.0f, 0.0f, 0.0f));
```

Entities with `Transform` receive or update `GlobalTransform`. Hierarchical propagation follows
ECS `Parent`/`Children`: a root uses its local matrix and each child uses
`parent_global * child_local`. Changes to a transform cause its descendant subtree to be
recalculated.

## Transform cheat sheet

Factories: `identity()`, `from_matrix()`, `from_translation()`, `from_xyz()`, `from_rotation()`, and
`from_scale()`.

Orientation and conversion: `look_at(target, up)`, `look_to(direction, up)`, `to_matrix()`, and
`local_x/y/z()`.

Mutation methods are fluent and work on lvalues or rvalues:

- parent-space: `rotate(quat)`, `rotate(axis, angle)`, and `rotate_x/y/z(angle)`;
- local-space: `rotate_local(quat)`, `rotate_local(axis, angle)`, and
  `rotate_local_x/y/z(angle)`;
- position/scale: `translate(vec3)`, `translate(x,y,z)`, `scale(vec3)`, and `scale(x,y,z)`;
- orbiting: `translate_around(point, rotation)` and `rotate_around(point, rotation)`.

`transform * point` applies scale, rotation, then translation. `transform * other` composes two
transforms using the module's composition order; `mul_vec3()` and `mul_transform()` are named
equivalents. Angles are radians because the implementation delegates to GLM.

