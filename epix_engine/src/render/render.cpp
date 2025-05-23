#include "epix/render.h"

EPIX_API void epix::render::RenderPlugin::build(epix::App& app) {
    // create webgpu render resources
    // instance
    wgpu::Instance instance = wgpu::createInstance(WGPUInstanceDescriptor{});
    // create primary window surface for temporary use
    wgpu::Surface primary_surface =
        app.run_system(
               [&](Query<Get<GLFWwindow*>, With<epix::window::PrimaryWindow>>
                       primary_window_query) {
                   return epix::webgpu::utils::create_surface(
                       instance, std::get<0>(primary_window_query.single())
                   );
               }
        ).value();
}