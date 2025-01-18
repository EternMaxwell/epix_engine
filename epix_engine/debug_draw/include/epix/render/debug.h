#pragma once

#include <epix/common.h>
#include <epix/rdvk.h>

#include "draw_dbg/components.h"
#include "draw_dbg/systems.h"

namespace epix::render::debug {
namespace vulkan {
using namespace epix::prelude;
struct DebugRenderPlugin : Plugin {
    size_t max_vertex_count = 2048 * 256;
    size_t max_model_count  = 2048 * 64;
    EPIX_API void build(App& app) override;
};
}  // namespace vulkan
namespace vulkan2 {
using namespace epix::render::vulkan2::backend;
struct DebugVertex {
    glm::vec3 pos;
    glm::vec4 color;
    uint32_t model_index;
};
struct PipelineBase {
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
        EPIX_API void draw_point(const glm::vec3& pos, const glm::vec4& color);
        EPIX_API void draw_line(
            const glm::vec3& start, const glm::vec3& end, const glm::vec4& color
        );
        EPIX_API void draw_triangle(
            const glm::vec3& v0,
            const glm::vec3& v1,
            const glm::vec3& v2,
            const glm::vec4& color
        );

       protected:
        EPIX_API void assure_new(size_t count);
        EPIX_API void add_vertex(const glm::vec3& pos, const glm::vec4& color);
        EPIX_API void next_draw_call();

        struct PerDrawCall {
            size_t vertex_offset;
            size_t vertex_count;
            size_t model_offset;
            size_t model_count;
        };
        std::vector<DebugVertex> vertices;
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

        friend struct PipelineBase;
    };
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
    EPIX_API void create_pipeline(vk::PrimitiveTopology topology);
};
struct PointPipeline : PipelineBase {
    EPIX_API void create();
    EPIX_API void destroy();
};
struct LinePipeline : PipelineBase {
    EPIX_API void create();
    EPIX_API void destroy();
};
struct TrianglePipeline : PipelineBase {
    EPIX_API void create();
    EPIX_API void destroy();
};
struct DebugPipelines {
    PointPipeline point_pipeline;
    LinePipeline line_pipeline;
    TrianglePipeline triangle_pipeline;
};
namespace systems {
EPIX_API void create_pipelines(
    Command cmd, Res<render::vulkan2::RenderContext> context
);
EPIX_API void extract_pipelines(ResMut<DebugPipelines> pipelines, Command cmd);
EPIX_API void destroy_pipelines(Command cmd, ResMut<DebugPipelines> pipelines);
}  // namespace systems
}  // namespace vulkan2
}  // namespace epix::render::debug