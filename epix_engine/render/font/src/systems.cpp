#include "epix/font.h"

using namespace epix;

namespace epix::font::vulkan2 {
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
    Extract<ResMut<FontAtlas>> font_atlas,
    Command cmd,
    Extract<ResMut<RenderContext>> context,
    Extract<ResMut<VulkanResources>> res_manager
) {
    if (!font_atlas || !context || !res_manager) return;
    {
        ZoneScopedN("Update font atlas");
        font_atlas->apply_cache(context->queue, *res_manager);
    }
    ZoneScopedN("Extract FontAtlas");
    cmd.share_resource(font_atlas);
}
EPIX_API void systems::destroy_font_atlas(ResMut<FontAtlas> font_atlas) {
    if (!font_atlas) return;
    font_atlas->destroy();
}
}  // namespace epix::font::vulkan2