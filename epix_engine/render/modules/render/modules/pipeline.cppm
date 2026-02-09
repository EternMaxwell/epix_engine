export module epix.render:pipeline;

import :shader;

namespace render {
export struct RenderPipelineId : core::int_base<std::uint32_t> {
    using int_base::int_base;
};
export struct RenderPipeline {
   public:
    RenderPipeline(RenderPipelineId id, wgpu::RenderPipeline pipeline) : _id(id), _pipeline(std::move(pipeline)) {}
    RenderPipelineId id() const { return _id; }
    const wgpu::RenderPipeline& pipeline() const { return _pipeline; }

   private:
    RenderPipelineId _id;
    wgpu::RenderPipeline _pipeline;
};
}  // namespace render