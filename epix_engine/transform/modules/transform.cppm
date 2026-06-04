module;
#include <epix/transform.hpp>

export module epix.transform;

export import glm;

export namespace epix::transform {
using epix::transform::DTransform;
using epix::transform::GlobalTransform;
using epix::transform::Transform;
using epix::transform::TransformPlugin;
using epix::transform::TransformSets;
using epix::transform::TransformT;
} // namespace epix::transform
