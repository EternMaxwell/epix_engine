#include <epix/pbr/mesh_view_key.hpp>

namespace epix::pbr {

void check_views_need_specialization(ecs::ResMut<ViewKeyCache> view_key_cache,
                                     ecs::ResMut<ViewSpecializationTicks> view_specialization_ticks,
                                     ecs::Query<ViewSpecializationItem> views,
                                     ecs::SystemChangeTick ticks) {
    for (auto&& [view, msaa, tonemapping, dither, shadow_filter_method, normal_prepass, depth_prepass,
                 motion_vector_prepass, deferred_prepass, camera_3d, temporal_jitter, perspective,
                 orthographic, distance_fog, has_oit, has_atmosphere] : views.iter()) {
        MeshViewKeyInputs inputs;
        inputs.msaa_samples          = ::epix::render::view::samples(msaa);
        inputs.hdr                   = view.hdr;
        inputs.normal_prepass        = normal_prepass;
        inputs.depth_prepass         = depth_prepass;
        inputs.motion_vector_prepass = motion_vector_prepass;
        inputs.deferred_prepass      = deferred_prepass;
        inputs.temporal_jitter       = temporal_jitter;
        inputs.oit_enabled           = has_oit;
        inputs.atmosphere            = has_atmosphere;
        inputs.invert_culling        = view.invert_culling;
        inputs.distance_fog          = distance_fog;
        // The light-probe pair is omitted with its query items: this port has no
        // `RenderViewLightProbes`, so both bits stay clear, as they would for a view with
        // neither probe. Tonemapping and dither are read off the camera, as Bevy does.
        if (camera_3d) {
            inputs.tonemap_in_shader = tonemapping.has_value();
            inputs.deband_dither     = dither.has_value() && dither->get() == ::epix::core_graph::DebandDither::Enabled;
            inputs.screen_space_specular_transmission_quality =
                camera_3d->get().screen_space_specular_transmission_quality;
        }
        if (shadow_filter_method) inputs.shadow_filtering_method = shadow_filter_method->get();
        inputs.projection = perspective ? MeshViewKeyInputs::ProjectionKind::Perspective
                                        : (orthographic ? MeshViewKeyInputs::ProjectionKind::Orthographic
                                                        : MeshViewKeyInputs::ProjectionKind::Nonstandard);

        const MeshPipelineKey view_key = make_mesh_view_key(inputs);
        const auto cached              = view_key_cache->keys.find(view.retained_view_entity);
        if (cached == view_key_cache->keys.end() || cached->second != view_key) {
            view_key_cache->keys.insert_or_assign(view.retained_view_entity, view_key);
            view_specialization_ticks->ticks.insert_or_assign(view.retained_view_entity, ticks.this_run());
        }
    }
}

}  // namespace epix::pbr
