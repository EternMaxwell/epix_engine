#include <epix/render.hpp>

namespace epix::render::batching {

void BatchingPlugin::attach(app::App& app) const noexcept {
    if (auto render_app = app.get_sub_app_mut(Render)) {
        auto device = render_app->get().world().get_resource<wgpu::Device>();
        std::optional<wgpu::BackendType> backend_type;
        if (auto adapter = render_app->get().world().get_resource<wgpu::Adapter>()) {
            wgpu::AdapterInfo info;
            adapter->get().getInfo(&info);
            backend_type = info.backendType;
        }
        auto& world = render_app->get().world_mut();
        world.insert_resource(IndirectParametersBuffers{debug_flags.allow_copies_from_indirect_parameters()});
        world.insert_resource(device ? GpuPreprocessingSupport::from_device(device->get(), backend_type)
                                     : GpuPreprocessingSupport{});
        render_app->get().add_systems(Render, ecs::into(clear_indirect_parameters_buffers)
                                                  .in_set(RenderSystems::ManageViews)
                                                  .set_name("clear indirect parameter buffers"));
        render_app->get().add_systems(Render, ecs::into(write_indirect_parameters_buffers)
                                                  .in_set(RenderSystems::PrepareResourcesFlush)
                                                  .set_name("write indirect parameter buffers"));
    }
}

}  // namespace epix::render::batching
