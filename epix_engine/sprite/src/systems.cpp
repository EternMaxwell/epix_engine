#include "epix/sprite.h"

namespace epix::sprite::vulkan2 {

EPIX_API void systems::create_pipeline(
    Command cmd, Res<epix::render::vulkan2::RenderContext> context
){
    SpritePipeline pipeline(context->device);
    pipeline.create();
    cmd.insert_resource(std::move(pipeline));
}
EPIX_API void systems::destroy_pipeline(
    Command cmd, ResMut<SpritePipeline> pipeline
) {
    pipeline->destroy();
    cmd.remove_resource<SpritePipeline>();
}
EPIX_API void systems::extract_pipeline(ResMut<SpritePipeline> pipeline, Command cmd) {
    cmd.share_resource(pipeline);
}
}  // namespace epix::sprite::vulkan2