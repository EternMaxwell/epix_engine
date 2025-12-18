// Module interface unit for epix.transform
// This file is only compiled when EPIX_ENABLE_MODULES=ON

module;

// Import third-party headers in global module fragment
#include <epix/core.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

export module epix.transform;

// Re-export the transform namespace from the header
export namespace epix::transform {
    using ::epix::transform::TransformT;
    using ::epix::transform::Transform;
    using ::epix::transform::DTransform;
    using ::epix::transform::GlobalTransform;
    using ::epix::transform::TransformSets;
    using ::epix::transform::TransformPlugin;
}

// Make transform available in epix namespace
export namespace epix {
    using namespace transform;
}
