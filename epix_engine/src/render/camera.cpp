#include "epix/render/camera.h"

using namespace epix::render::camera;

EPIX_API std::optional<RenderTarget> RenderTarget::normalize(std::optional<Entity> primary) const {
    return std::visit(
        epix::util::visitor{[&](const nvrhi::TextureHandle& tex) -> std::optional<RenderTarget> { return *this; },
                            [&](const WindowRef& win_ref) -> std::optional<RenderTarget> {
                                if (win_ref.primary) {
                                    if (primary.has_value()) {
                                        return RenderTarget(WindowRef{false, primary.value()});
                                    } else {
                                        return std::nullopt;
                                    }
                                } else {
                                    return *this;
                                }
                            }},
        *this);
}