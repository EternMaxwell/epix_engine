#include "epix/core_graph/core2d.hpp"
#include "epix/render/extract.hpp"
#include "epix/render/render_phase.hpp"
#include "epix/render/schedule.hpp"
#include "epix/text/render.hpp"
#include "epix/transform.hpp"

namespace epix::text {
struct RenderText2dInstances : RenderTextInstances {};
void validate_text2d(Query<Item<Entity, Has<TextColor>, Has<transform::Transform>>, With<Text2d>> query) {
    for (auto&& [entity, has_color, has_transform] : query.iter()) {
        if (!has_color || !has_transform) {
            std::vector<std::string_view> missing_components;
            if (!has_color) missing_components.push_back("TextColor");
            if (!has_transform) missing_components.push_back("Transform");
            spdlog::warn("Entity {:#x} with Text2d is missing required components: missing {}", entity.uid,
                         std::views::all(missing_components));
        }
    }
}
void extract_text2d(Extract<Query<Item<Entity,
                                       const TextMesh&,
                                       const Text2d&,
                                       const TextColor&,
                                       const TextImage&,
                                       const transform::GlobalTransform&>>> texts,
                    ResMut<RenderText2dInstances> render_texts) {
    render_texts->clear();
    for (auto&& [entity, mesh, offset, color, image, transform] : texts.iter()) {
        glm::mat4 model = glm::translate(transform.matrix, glm::vec3(offset.offset, 0.0f));
        render_texts->emplace(entity, RenderText{
                                          .mesh       = mesh.mesh(),
                                          .font_image = image.image,
                                          .transform  = model,
                                          .color      = glm::vec4{color.r, color.g, color.b, color.a},
                                      });
    }
}
void queue_text2d(Res<RenderText2dInstances> render_meshes,
                  ResMut<TextPipeline> text_pipeline,
                  ResMut<render::PipelineServer> pipeline_server,
                  ResMut<render::render_phase::DrawFunctions<render::core_2d::Transparent2D>> draw_functions,
                  Query<Item<render::render_phase::RenderPhase<render::core_2d::Transparent2D>&>,
                        With<render::camera::ExtractedCamera, render::view::ViewTarget>> views) {
    for (auto&& [phase] : views.iter()) {
        for (auto&& [entity, render_text] : *render_meshes) {
            auto pipeline_id = text_pipeline->pipeline_id();
            phase.add(render::core_2d::Transparent2D{
                .id          = entity,
                .depth       = 0.f,
                .pipeline_id = pipeline_id,
                .draw_func   = render::render_phase::add_render_commands<
                      render::core_2d::Transparent2D, render::render_phase::SetItemPipeline,
                      render::view::BindViewUniform<0>::Command, BindFontResources<RenderText2dInstances>::Command,
                      DrawTextMesh<RenderText2dInstances>::Command>(*draw_functions),
                .batch_count = 1,
            });
        }
    }
}
void TextRenderPlugin::build(App& app) {
    app.add_plugins(TextPipelinePlugin{});
    auto& render_app = app.sub_app_mut(render::Render);
    render_app.world_mut().init_resource<RenderText2dInstances>();
#ifndef NDEBUG
    render_app.add_systems(Last, into(validate_text2d).set_name("validate_text2d"));
#endif
    render_app.add_systems(render::ExtractSchedule, into(extract_text2d).set_name("extract_text2d"));
    render_app.add_systems(render::Render,
                           into(queue_text2d).set_name("queue_text2d").in_set(render::RenderSet::Queue));
}
}  // namespace epix::text