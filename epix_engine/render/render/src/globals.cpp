#include <spdlog/spdlog.h>

#include <epix/assets.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <span>
#include <string_view>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

namespace {
struct GlobalsShaderLibraryHandle {
    epix::assets::Handle<epix::shader::Shader> shader;
};

/** @brief Slang counterpart of Bevy's `bevy_render::globals` shader library
 * (`globals.wgsl`), which Bevy loads with `load_shader_library!` in
 * `GlobalsPlugin`.
 *
 * The struct matches Bevy's member for member. Bevy's `_webgl2_padding` exists
 * only behind `SIXTEEN_BYTE_ALIGNMENT` for WebGL2, where the uniform must be 16
 * bytes; native Vulkan uses the 12-byte struct, which is what
 * `GlobalsUniform` and `ShaderTypeInfo` describe here. */
constexpr std::string_view kGlobalsShaderSlang = R"(
module epix.globals;

namespace epix::globals {
public struct Globals {
    public float time;
    public float delta_time;
    public uint frame_count;
#ifdef SIXTEEN_BYTE_ALIGNMENT
    // WebGL2 structs must be 16 byte aligned.
    public float _webgl2_padding;
#endif
};
}
)";

/** @brief Register the globals shader library as an embedded asset and keep its
 * handle alive (Bevy `load_shader_library!(app, "globals.wgsl")`). */
void register_globals_shader_library(App& app) {
    auto& world   = app.world_mut();
    auto registry = world.get_resource_mut<epix::assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<epix::assets::AssetServer>();
    if (!registry || !server) {
        spdlog::warn(
            "[render.globals] EmbeddedAssetRegistry or AssetServer not available. Globals shader library not "
            "registered.");
        return;
    }
    auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(kGlobalsShaderSlang.data()),
                                            kGlobalsShaderSlang.size());
    registry->get().insert_asset_static("epix/shaders/globals.slang", bytes);
    world.insert_resource(GlobalsShaderLibraryHandle{
        server->get().load<epix::shader::Shader>("embedded://epix/shaders/globals.slang")});
}
}  // namespace

void FrameCountPlugin::attach(App& app) {
    spdlog::debug("[render.globals] Attaching FrameCountPlugin.");
    app.world_mut().init_resource<FrameCount>();
    // Bevy increments FrameCount in the Last schedule (0 during the first update,
    // 1 during the next), see bevy_diagnostic::FrameCountPlugin.
    app.add_systems(Last, into(detail::increment_frame_count).set_name("increment frame count"));
}

void GlobalsPlugin::attach(App& app) {
    spdlog::debug("[render.globals] Attaching GlobalsPlugin.");
    // Bevy loads the globals shader library in `GlobalsPlugin::build`, before
    // and independently of the render sub-app, so this happens first.
    register_globals_shader_library(app);
    auto render_app = app.get_sub_app_mut(epix::render::Render);
    if (!render_app) return;
    // Render-world copies of Time and FrameCount, populated by the extract
    // systems below (Bevy: init_resource::<GlobalsBuffer>().init_resource::<Time>()).
    auto& render_world = render_app->get();
    render_world.world_mut().init_resource<GlobalsBuffer>();
    render_world.world_mut().init_resource<epix::time::Time<>>();
    render_world.add_systems(ExtractSchedule, into(detail::extract_frame_count, detail::extract_time)
                                                  .set_names(std::array{"extract frame count", "extract time"}));
    render_world.add_systems(epix::render::Render, into(detail::prepare_globals_buffer)
                                                       .in_set(epix::render::RenderSystems::PrepareResources)
                                                       .set_name("prepare globals buffer"));
}
