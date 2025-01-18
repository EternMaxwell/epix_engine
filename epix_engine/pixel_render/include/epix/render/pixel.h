#pragma once

#include <epix/common.h>
#include <epix/rdvk.h>

#include "pixel/components.h"
#include "pixel/systems.h"

namespace epix::render::pixel {
using namespace epix::prelude;
struct PixelRenderPlugin : Plugin {
    EPIX_API void build(App& app) override;
};
namespace vulkan2 {
using namespace epix::render::vulkan2::backend;
struct PixelVertex {
    glm::vec4 color;
    glm::vec2 pos;
    uint32_t model_index;
};
struct PixelPipeline {
    Device device;
    RenderPass render_pass;
    Pipeline pipeline;
    PipelineLayout pipeline_layout;
    DescriptorSetLayout descriptor_set_layout;
    DescriptorPool descriptor_pool;
    struct Context;
    struct mesh {
        EPIX_API mesh(size_t max_vertex_count, size_t max_model_count);

        EPIX_API void clear();
        EPIX_API void set_model(const glm::mat4& model);
        EPIX_API void draw_pixel(const glm::vec4& color, const glm::vec2& pos);

       protected:
        EPIX_API void assure_new(size_t count);
        EPIX_API void add_vertex(const glm::vec4& color, const glm::vec2& pos);
        EPIX_API void next_draw_call();

        struct PerDrawCall {
            size_t vertex_offset;
            size_t vertex_count;
            size_t model_offset;
            size_t model_count;
        };
        std::vector<PixelVertex> vertices;
        std::vector<glm::mat4> models;
        std::vector<PerDrawCall> draw_calls;

        Buffer vertex_staging_buffer   = {};
        size_t vertex_staging_capacity = 0;
        Buffer model_staging_buffer    = {};
        size_t model_staging_capacity  = 0;

        const size_t max_vertex_count;
        const size_t max_model_count;

        friend struct Context;
    };
    struct Context {
        Device device;

        RenderPass render_pass;
        Pipeline pipeline;
        PipelineLayout pipeline_layout;

        DescriptorSet descriptor_set;
        Buffer vertex_buffer;
        Buffer model_buffer;

        CommandBuffer command_buffer;
        Fence fence;

        Framebuffer framebuffer;
        vk::Extent2D extent;

        const size_t max_vertex_count;
        const size_t max_model_count;

        EPIX_API mesh generate_mesh();
        EPIX_API void destroy_mesh(mesh& mesh);

        EPIX_API void begin(
            Buffer uniform_buffer, ImageView render_target, vk::Extent2D extent
        );
        EPIX_API void draw_mesh(mesh& mesh);
        EPIX_API void end(Queue& queue);

       private:
        EPIX_API Context(
            Device device,
            RenderPass render_pass,
            Pipeline pipeline,
            PipelineLayout pipeline_layout,
            DescriptorSet descriptor_set,
            Buffer vertex_buffer,
            Buffer model_buffer,
            CommandBuffer command_buffer,
            Fence fence,
            size_t max_vertex_count,
            size_t max_model_count
        );
        EPIX_API void begin_pass();

        friend struct PixelPipeline;
    };

    EPIX_API PixelPipeline(Device device);
    EPIX_API void create();
    EPIX_API void destroy();

    EPIX_API Context create_context(
        CommandPool& command_pool,
        size_t max_vertex_count,
        size_t max_model_count
    );
    EPIX_API void destroy_context(Context& context, CommandPool& command_pool);

   protected:
    EPIX_API void create_descriptor_set_layout();
    EPIX_API void create_pipeline_layout();
    EPIX_API void create_descriptor_pool();
    EPIX_API void create_render_pass();
    EPIX_API void create_pipeline();
};
namespace systems {
EPIX_API void create_pixel_pipeline(
    Command command, ResMut<epix::render::vulkan2::RenderContext> context
);
EPIX_API void destroy_pixel_pipeline(ResMut<PixelPipeline> pipeline);
EPIX_API void extract_pixel_pipeline(
    Command cmd, ResMut<PixelPipeline> pipeline
);
}  // namespace systems
}  // namespace vulkan2
}  // namespace epix::render::pixel