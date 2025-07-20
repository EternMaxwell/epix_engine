#pragma once

#include <epix/app.h>
#include <epix/vulkan.h>

namespace epix::render {
struct SpecializedPipelinesBase {
   public:
    EPIX_API SpecializedPipelinesBase(nvrhi::DeviceHandle device,
                                      const nvrhi::GraphicsPipelineDesc& desc);

    EPIX_API nvrhi::GraphicsPipelineHandle specialize(
        nvrhi::FramebufferHandle framebuffer);
    EPIX_API void garbage_collect();

   private:
    nvrhi::DeviceHandle m_device;
    nvrhi::GraphicsPipelineDesc m_desc;

    std::vector<nvrhi::GraphicsPipelineHandle> m_specializedPipelines;
    std::deque<size_t> m_freeIndices;
    entt::dense_map<nvrhi::FramebufferHandle, size_t> m_framebufferToIndex;
};
template <typename T>
concept SpecializablePipeline = requires(T const t) {
    { t.descriptor() } -> std::same_as<nvrhi::GraphicsPipelineDesc>;
    { t.key() } -> std::equality_comparable;
    { std::hash<decltype(t.key())>()(t.key()) } -> std::same_as<std::size_t>;
};
template <SpecializablePipeline T>
struct SpecializedPipelines {
   public:
    using KeyType = decltype(std::declval<T>().key());

    SpecializedPipelines(nvrhi::DeviceHandle device) : m_device(device) {}

    static std::optional<SpecializedPipelines<T>> from_world(
        app::World& world) {
        if (auto device = world.get_resource<nvrhi::DeviceHandle>()) {
            return SpecializedPipelines<T>(*device);
        }
        return std::nullopt;
    }

    nvrhi::GraphicsPipelineHandle specialize(
        const T& pipeline, nvrhi::FramebufferHandle framebuffer) {
        auto it = m_pipelines.find(pipeline.key());
        if (it == m_pipelines.end()) {
            // current entry not created
            it = m_pipelines
                     .try_emplace(pipeline.key(), m_device,
                                  pipeline.descriptor())
                     .first;
        }
        return it->second.specialize(framebuffer);
    }

    void garbage_collect() {
        for (auto&& [key, specializedPipeline] : m_pipelines) {
            specializedPipeline.garbage_collect();
        }
    }

   private:
    nvrhi::DeviceHandle m_device;
    entt::dense_map<KeyType, SpecializedPipelinesBase> m_pipelines;
};
}  // namespace epix::render