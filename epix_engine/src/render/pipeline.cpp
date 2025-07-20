#include "epix/render/pipeline.h"

using namespace epix::render;

EPIX_API SpecializedPipelinesBase::SpecializedPipelinesBase(
    nvrhi::DeviceHandle device, const nvrhi::GraphicsPipelineDesc& desc)
    : m_device(device), m_desc(desc) {}

EPIX_API nvrhi::GraphicsPipelineHandle SpecializedPipelinesBase::specialize(
    nvrhi::FramebufferHandle framebuffer) {
    auto it = m_framebufferToIndex.find(framebuffer);
    if (it != m_framebufferToIndex.end()) {
        return m_specializedPipelines[it->second];
    }

    // look for all specialized pipelines, if anyone compatible with the
    // framebuffer, store it and return, otherwise create a new one
    for (size_t i = 0; i < m_specializedPipelines.size(); ++i) {
        if (m_specializedPipelines[i]->getFramebufferInfo() ==
            framebuffer->getFramebufferInfo()) {
            m_framebufferToIndex[framebuffer] = i;
            return m_specializedPipelines[i];
        }
    }

    // create a new specialized pipeline
    auto specializedPipeline =
        m_device->createGraphicsPipeline(m_desc, framebuffer);
    if (!m_freeIndices.empty()) {
        size_t index                      = m_freeIndices.front();
        m_framebufferToIndex[framebuffer] = index;
        m_freeIndices.pop_front();
        m_specializedPipelines[index] = specializedPipeline;
    } else {
        m_framebufferToIndex[framebuffer] = m_specializedPipelines.size();
        m_specializedPipelines.push_back(specializedPipeline);
    }
    return specializedPipeline;
}
EPIX_API void SpecializedPipelinesBase::garbage_collect() {
    // remove all pipelines that are not used anymore

    // cache pipeline reference counts and remove map entries that the
    // framebuffer is not used anywhere else.
    std::vector<size_t> refcounts(m_specializedPipelines.size(), 0);
    for (const auto&& [framebuffer, index] : m_framebufferToIndex) {
        if (framebuffer->AddRef() == 2) {
            framebuffer->Release();
            m_framebufferToIndex.erase(framebuffer);
        } else {
            refcounts[index]++;
        }
    }

    for (auto&& [index, refcount] : std::views::enumerate(refcounts)) {
        if (refcount == 0) {
            m_specializedPipelines[index] = nullptr;
            m_freeIndices.push_back(index);
        }
    }
}

// build test for the specialized pipelines
struct TestPipeline {
    nvrhi::GraphicsPipelineDesc descriptor() const {
        return nvrhi::GraphicsPipelineDesc()
            .setPrimType(nvrhi::PrimitiveType::TriangleList)
            .setVertexShader(nullptr)
            .setPixelShader(nullptr);
    }
    std::string_view key() const { return "TestPipeline"; }
};
using TestSpecializedPipelines = SpecializedPipelines<TestPipeline>;

void test() {
    nvrhi::DeviceHandle device; // Assume this is initialized
    TestSpecializedPipelines pipelines(device);
    nvrhi::FramebufferHandle framebuffer; // Assume this is initialized
    auto specializedPipeline = pipelines.specialize(TestPipeline{}, framebuffer);
    pipelines.garbage_collect();
}