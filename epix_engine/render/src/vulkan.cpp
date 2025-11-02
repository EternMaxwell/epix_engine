#include "epix/render/vulkan.hpp"

using namespace nvrhi;

class DeviceWrapper : public RefCounter<IDevice> {
   public:
    friend class CommandListWrapper;

    DeviceWrapper(IDevice* device) : m_Device(device) {}

   protected:
    DeviceHandle m_Device;
    std::mutex m_Mutex;

   public:
    // IResource implementation
    Object getNativeObject(ObjectType objectType) override { return m_Device->getNativeObject(objectType); }

    // IDevice implementation
    HeapHandle createHeap(const HeapDesc& d) override { return m_Device->createHeap(d); }
    TextureHandle createTexture(const TextureDesc& d) override { return m_Device->createTexture(d); }
    MemoryRequirements getTextureMemoryRequirements(ITexture* texture) override {
        return m_Device->getTextureMemoryRequirements(texture);
    }
    bool bindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset) override {
        return m_Device->bindTextureMemory(texture, heap, offset);
    }

    TextureHandle createHandleForNativeTexture(ObjectType objectType,
                                               Object texture,
                                               const TextureDesc& desc) override {
        return m_Device->createHandleForNativeTexture(objectType, texture, desc);
    }

    StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess) override {
        return m_Device->createStagingTexture(d, cpuAccess);
    }
    void* mapStagingTexture(IStagingTexture* tex,
                            const TextureSlice& slice,
                            CpuAccessMode cpuAccess,
                            size_t* outRowPitch) override {
        return m_Device->mapStagingTexture(tex, slice, cpuAccess, outRowPitch);
    }
    void unmapStagingTexture(IStagingTexture* tex) override { return m_Device->unmapStagingTexture(tex); }

    void getTextureTiling(ITexture* texture,
                          uint32_t* numTiles,
                          PackedMipDesc* desc,
                          TileShape* tileShape,
                          uint32_t* subresourceTilingsNum,
                          SubresourceTiling* subresourceTilings) override {
        return m_Device->getTextureTiling(texture, numTiles, desc, tileShape, subresourceTilingsNum,
                                          subresourceTilings);
    }
    void updateTextureTileMappings(ITexture* texture,
                                   const TextureTilesMapping* tileMappings,
                                   uint32_t numTileMappings,
                                   CommandQueue executionQueue = CommandQueue::Graphics) override {
        return m_Device->updateTextureTileMappings(texture, tileMappings, numTileMappings, executionQueue);
    }

    SamplerFeedbackTextureHandle createSamplerFeedbackTexture(ITexture* pairedTexture,
                                                              const SamplerFeedbackTextureDesc& desc) override {
        return m_Device->createSamplerFeedbackTexture(pairedTexture, desc);
    }
    SamplerFeedbackTextureHandle createSamplerFeedbackForNativeTexture(ObjectType objectType,
                                                                       Object texture,
                                                                       ITexture* pairedTexture) override {
        return m_Device->createSamplerFeedbackForNativeTexture(objectType, texture, pairedTexture);
    }

    BufferHandle createBuffer(const BufferDesc& d) override { return m_Device->createBuffer(d); }
    void* mapBuffer(IBuffer* b, CpuAccessMode mapFlags) override { return m_Device->mapBuffer(b, mapFlags); }
    void unmapBuffer(IBuffer* b) override { return m_Device->unmapBuffer(b); }
    MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer) override {
        return m_Device->getBufferMemoryRequirements(buffer);
    }
    bool bindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset) override {
        return m_Device->bindBufferMemory(buffer, heap, offset);
    }

    BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) override {
        return m_Device->createHandleForNativeBuffer(objectType, buffer, desc);
    }

    ShaderHandle createShader(const ShaderDesc& d, const void* binary, size_t binarySize) override {
        return m_Device->createShader(d, binary, binarySize);
    }
    ShaderHandle createShaderSpecialization(IShader* baseShader,
                                            const ShaderSpecialization* constants,
                                            uint32_t numConstants) override {
        return m_Device->createShaderSpecialization(baseShader, constants, numConstants);
    }

    ShaderLibraryHandle createShaderLibrary(const void* binary, size_t binarySize) override {
        return m_Device->createShaderLibrary(binary, binarySize);
    }

    SamplerHandle createSampler(const SamplerDesc& d) override { return m_Device->createSampler(d); }

    InputLayoutHandle createInputLayout(const VertexAttributeDesc* d,
                                        uint32_t attributeCount,
                                        IShader* vertexShader) override {
        return m_Device->createInputLayout(d, attributeCount, vertexShader);
    }

    // event queries
    EventQueryHandle createEventQuery() override { return m_Device->createEventQuery(); }
    void setEventQuery(IEventQuery* query, CommandQueue queue) override {
        return m_Device->setEventQuery(query, queue);
    }
    bool pollEventQuery(IEventQuery* query) override { return m_Device->pollEventQuery(query); }
    void waitEventQuery(IEventQuery* query) override { return m_Device->waitEventQuery(query); }
    void resetEventQuery(IEventQuery* query) override { return m_Device->resetEventQuery(query); }

    // timer queries
    TimerQueryHandle createTimerQuery() override { return m_Device->createTimerQuery(); }
    bool pollTimerQuery(ITimerQuery* query) override { return m_Device->pollTimerQuery(query); }
    float getTimerQueryTime(ITimerQuery* query) override { return m_Device->getTimerQueryTime(query); }
    void resetTimerQuery(ITimerQuery* query) override { return m_Device->resetTimerQuery(query); }

    GraphicsAPI getGraphicsAPI() override { return m_Device->getGraphicsAPI(); }

    FramebufferHandle createFramebuffer(const FramebufferDesc& desc) override {
        return m_Device->createFramebuffer(desc);
    }

    GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc,
                                                  FramebufferInfo const& fbinfo) override {
        return m_Device->createGraphicsPipeline(desc, fbinfo);
    }

    GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb) override {
        return m_Device->createGraphicsPipeline(desc, fb);
    }

    ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override {
        return m_Device->createComputePipeline(desc);
    }

    MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc,
                                                FramebufferInfo const& fbinfo) override {
        return m_Device->createMeshletPipeline(desc, fbinfo);
    }

    MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, IFramebuffer* fb) override {
        return m_Device->createMeshletPipeline(desc, fb);
    }

    rt::PipelineHandle createRayTracingPipeline(const rt::PipelineDesc& desc) override {
        return m_Device->createRayTracingPipeline(desc);
    }

    BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) override {
        return m_Device->createBindingLayout(desc);
    }
    BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc) override {
        return m_Device->createBindlessLayout(desc);
    }

    BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) override {
        return m_Device->createBindingSet(desc, layout);
    }
    DescriptorTableHandle createDescriptorTable(IBindingLayout* layout) override {
        return m_Device->createDescriptorTable(layout);
    }

    void resizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents = true) override {
        return m_Device->resizeDescriptorTable(descriptorTable, newSize, keepContents);
    }
    bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item) override {
        return m_Device->writeDescriptorTable(descriptorTable, item);
    }

    rt::OpacityMicromapHandle createOpacityMicromap(const rt::OpacityMicromapDesc& desc) override {
        return m_Device->createOpacityMicromap(desc);
    }
    rt::AccelStructHandle createAccelStruct(const rt::AccelStructDesc& desc) override {
        return m_Device->createAccelStruct(desc);
    }
    MemoryRequirements getAccelStructMemoryRequirements(rt::IAccelStruct* as) override {
        return m_Device->getAccelStructMemoryRequirements(as);
    }
    rt::cluster::OperationSizeInfo getClusterOperationSizeInfo(const rt::cluster::OperationParams& params) override {
        return m_Device->getClusterOperationSizeInfo(params);
    }
    bool bindAccelStructMemory(rt::IAccelStruct* as, IHeap* heap, uint64_t offset) override {
        return m_Device->bindAccelStructMemory(as, heap, offset);
    }

    CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) override {
        return m_Device->createCommandList(params);
    }
    uint64_t executeCommandLists(ICommandList* const* pCommandLists,
                                 size_t numCommandLists,
                                 CommandQueue executionQueue = CommandQueue::Graphics) override {
        std::lock_guard lock(m_Mutex);
        return m_Device->executeCommandLists(pCommandLists, numCommandLists, executionQueue);
    }
    void queueWaitForCommandList(CommandQueue waitQueue, CommandQueue executionQueue, uint64_t instance) override {
        return m_Device->queueWaitForCommandList(waitQueue, executionQueue, instance);
    }
    bool waitForIdle() override { return m_Device->waitForIdle(); }
    void runGarbageCollection() override { return m_Device->runGarbageCollection(); }
    bool queryFeatureSupport(Feature feature, void* pInfo = nullptr, size_t infoSize = 0) override {
        return m_Device->queryFeatureSupport(feature, pInfo, infoSize);
    }
    FormatSupport queryFormatSupport(Format format) override { return m_Device->queryFormatSupport(format); }
    coopvec::DeviceFeatures queryCoopVecFeatures() override { return m_Device->queryCoopVecFeatures(); }
    size_t getCoopVecMatrixSize(coopvec::DataType type, coopvec::MatrixLayout layout, int rows, int columns) override {
        return m_Device->getCoopVecMatrixSize(type, layout, rows, columns);
    }
    Object getNativeQueue(ObjectType objectType, CommandQueue queue) override {
        return m_Device->getNativeQueue(objectType, queue);
    }
    IMessageCallback* getMessageCallback() override { return m_Device->getMessageCallback(); }
    bool isAftermathEnabled() override { return m_Device->isAftermathEnabled(); }
    AftermathCrashDumpHelper& getAftermathCrashDumpHelper() override { return m_Device->getAftermathCrashDumpHelper(); }
};

nvrhi::DeviceHandle epix::render::create_async_device(nvrhi::DeviceHandle device) {
    return nvrhi::DeviceHandle(new DeviceWrapper(device));
}