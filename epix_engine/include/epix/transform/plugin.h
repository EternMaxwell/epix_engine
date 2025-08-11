#pragma once

#include "epix/app.h"
#include "transform.h"

namespace epix::transform {
enum class TransformSets {
    CalculateGlobalTransform = 0,
};
struct TransformPlugin {
    EPIX_API void build(epix::App& app);
};
}  // namespace epix::transform