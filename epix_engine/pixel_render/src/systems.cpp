#include "epix/render/pixel.h"

using namespace epix::render::pixel::vulkan2;

EPIX_API void systems::create_pixel_pipeline(
    Command command, ResMut<epix::render::vulkan2::RenderContext> context
) {
    PixelPipeline pipeline(context->device);
    pipeline.create();
    command.insert_resource(std::move(pipeline));
}
EPIX_API void systems::destroy_pixel_pipeline(ResMut<PixelPipeline> pipeline) {
    pipeline->destroy();
}
EPIX_API void systems::extract_pixel_pipeline(
    Command cmd, ResMut<PixelPipeline> pipeline
) {
    cmd.share_resource(pipeline);
}