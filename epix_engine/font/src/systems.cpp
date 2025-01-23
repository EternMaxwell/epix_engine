#include "epix/font.h"

using namespace epix;

namespace epix::font::vulkan2 {
EPIX_API void systems::create_pipeline(
    Command cmd, Res<RenderContext> context
) {
    if (!context) return;
    auto& device  = context->device;
    auto pipeline = TextPipeline(device);
    pipeline.create();
    cmd.insert_resource(std::move(pipeline));
}  // namespace epix::font::vulkan2::EPIX_API
EPIX_API void systems::insert_font_atlas(
    Command cmd,
    ResMut<RenderContext> context,
    ResMut<VulkanResources> res_manager
) {
    if (!context || !res_manager) return;
    cmd.emplace_resource<FontAtlas>(
        context->device, context->command_pool, res_manager
    );
}
EPIX_API void systems::extract_font_atlas(
    ResMut<FontAtlas> font_atlas,
    Command cmd,
    ResMut<RenderContext> context,
    ResMut<VulkanResources> res_manager
) {
    if (!font_atlas || !context || !res_manager) return;
    {
        ZoneScopedN("Update font atlas");
        font_atlas->apply_cache(context->queue, *res_manager);
    }
    ZoneScopedN("Extract FontAtlas");
    cmd.share_resource(font_atlas);
}
EPIX_API void systems::extract_pipeline(
    ResMut<TextPipeline> context, Command cmd
) {
    ZoneScopedN("Extract text pipeline");
    cmd.share_resource(context);
}
EPIX_API void systems::destroy_pipeline(
    Command cmd, ResMut<TextPipeline> pipeline
) {
    spdlog::info("destroy pipeline");
    pipeline->destroy();
}
EPIX_API void systems::destroy_font_atlas(ResMut<FontAtlas> font_atlas) {
    if (!font_atlas) return;
    font_atlas->destroy();
}
}  // namespace epix::font::vulkan2