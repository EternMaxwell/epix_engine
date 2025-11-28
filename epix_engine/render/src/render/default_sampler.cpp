#include "epix/render.hpp"

using namespace epix;
using namespace epix::render;

void DefaultSamplerPlugin::finish(App& app) {
    app.world_mut().insert_resource(DefaultSampler{
        .desc = nvrhi::SamplerDesc().setMagFilter(false),
    });
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().insert_resource(DefaultSampler{});
        render_app->get().add_systems(
            render::ExtractSchedule,
            into([](Res<nvrhi::DeviceHandle> device,
                    ParamSet<ResMut<DefaultSampler>, Extract<Res<DefaultSampler>>> samplers) {
                auto&& [gpu_sampler, sampler] = samplers.get();
                if (!gpu_sampler->handle || sampler.is_modified()) {
                    gpu_sampler->handle = device.get()->createSampler(sampler->desc);
                }
            }).set_name("extract and update default sampler"));
    }
}