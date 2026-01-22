// Methods of StringView
StringView& StringView::setDefault() {
	*this = WGPUStringView WGPU_STRING_VIEW_INIT;
	return *this;
}

// Methods of ChainedStruct
ChainedStruct& ChainedStruct::setDefault() {
	*this = WGPUChainedStruct {};
	return *this;
}

// Methods of ChainedStructOut
ChainedStructOut& ChainedStructOut::setDefault() {
	*this = WGPUChainedStructOut {};
	return *this;
}

// Methods of BufferMapCallbackInfo
BufferMapCallbackInfo& BufferMapCallbackInfo::setDefault() {
	*this = WGPUBufferMapCallbackInfo {};
	return *this;
}

// Methods of CompilationInfoCallbackInfo
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setDefault() {
	*this = WGPUCompilationInfoCallbackInfo {};
	return *this;
}

// Methods of CreateComputePipelineAsyncCallbackInfo
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setDefault() {
	*this = WGPUCreateComputePipelineAsyncCallbackInfo {};
	return *this;
}

// Methods of CreateRenderPipelineAsyncCallbackInfo
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setDefault() {
	*this = WGPUCreateRenderPipelineAsyncCallbackInfo {};
	return *this;
}

// Methods of DeviceLostCallbackInfo
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setDefault() {
	*this = WGPUDeviceLostCallbackInfo {};
	return *this;
}

// Methods of PopErrorScopeCallbackInfo
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setDefault() {
	*this = WGPUPopErrorScopeCallbackInfo {};
	return *this;
}

// Methods of QueueWorkDoneCallbackInfo
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setDefault() {
	*this = WGPUQueueWorkDoneCallbackInfo {};
	return *this;
}

// Methods of RequestAdapterCallbackInfo
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setDefault() {
	*this = WGPURequestAdapterCallbackInfo {};
	return *this;
}

// Methods of RequestDeviceCallbackInfo
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setDefault() {
	*this = WGPURequestDeviceCallbackInfo {};
	return *this;
}

// Methods of UncapturedErrorCallbackInfo
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setDefault() {
	*this = WGPUUncapturedErrorCallbackInfo {};
	return *this;
}

// Methods of AdapterInfo
AdapterInfo& AdapterInfo::setDefault() {
    backendType = BackendType::Undefined;
    ((StringView*)&vendor)->setDefault();
    ((StringView*)&architecture)->setDefault();
    ((StringView*)&device)->setDefault();
    ((StringView*)&description)->setDefault();
	return *this;
}

// Methods of BindGroupEntry
BindGroupEntry& BindGroupEntry::setDefault() {
	offset = 0;
	return *this;
}

// Methods of BlendComponent
BlendComponent& BlendComponent::setDefault() {
    operation = BlendOperation::Add;
    srcFactor = BlendFactor::One;
    dstFactor = BlendFactor::Zero;
	return *this;
}

// Methods of BufferBindingLayout
BufferBindingLayout& BufferBindingLayout::setDefault() {
    type             = BufferBindingType::Uniform;
    hasDynamicOffset = false;
    minBindingSize   = 0;
	return *this;
}

// Methods of BufferDescriptor
BufferDescriptor& BufferDescriptor::setDefault() {
    mappedAtCreation = false;
    ((StringView*)&label)->setDefault();
	return *this;
}

// Methods of Color
Color& Color::setDefault() {
	*this = WGPUColor {};
	return *this;
}

// Methods of CommandBufferDescriptor
CommandBufferDescriptor& CommandBufferDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of CommandEncoderDescriptor
CommandEncoderDescriptor& CommandEncoderDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of CompilationMessage
CompilationMessage& CompilationMessage::setDefault() {
	((StringView*)&message)->setDefault();
	return *this;
}

// Methods of ComputePassTimestampWrites
ComputePassTimestampWrites& ComputePassTimestampWrites::setDefault() {
	*this = WGPUComputePassTimestampWrites {};
	return *this;
}

// Methods of ConstantEntry
ConstantEntry& ConstantEntry::setDefault() {
	((StringView*)&key)->setDefault();
	return *this;
}

// Methods of Extent3D
Extent3D& Extent3D::setDefault() {
    height             = 1;
    depthOrArrayLayers = 1;
	return *this;
}

// Methods of Future
Future& Future::setDefault() {
	*this = WGPUFuture {};
	return *this;
}

// Methods of InstanceCapabilities
InstanceCapabilities& InstanceCapabilities::setDefault() {
	*this = WGPUInstanceCapabilities {};
	return *this;
}

// Methods of Limits
Limits& Limits::setDefault() {
    maxTextureDimension1D                     = WGPU_LIMIT_U32_UNDEFINED;
    maxTextureDimension2D                     = WGPU_LIMIT_U32_UNDEFINED;
    maxTextureDimension3D                     = WGPU_LIMIT_U32_UNDEFINED;
    maxTextureArrayLayers                     = WGPU_LIMIT_U32_UNDEFINED;
    maxBindGroups                             = WGPU_LIMIT_U32_UNDEFINED;
    maxBindGroupsPlusVertexBuffers            = WGPU_LIMIT_U32_UNDEFINED;
    maxBindingsPerBindGroup                   = WGPU_LIMIT_U32_UNDEFINED;
    maxDynamicUniformBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    maxDynamicStorageBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    maxSampledTexturesPerShaderStage          = WGPU_LIMIT_U32_UNDEFINED;
    maxSamplersPerShaderStage                 = WGPU_LIMIT_U32_UNDEFINED;
    maxStorageBuffersPerShaderStage           = WGPU_LIMIT_U32_UNDEFINED;
    maxStorageTexturesPerShaderStage          = WGPU_LIMIT_U32_UNDEFINED;
    maxUniformBuffersPerShaderStage           = WGPU_LIMIT_U32_UNDEFINED;
    maxUniformBufferBindingSize               = WGPU_LIMIT_U64_UNDEFINED;
    maxStorageBufferBindingSize               = WGPU_LIMIT_U64_UNDEFINED;
    minUniformBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
    minStorageBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
    maxVertexBuffers                          = WGPU_LIMIT_U32_UNDEFINED;
    maxBufferSize                             = WGPU_LIMIT_U64_UNDEFINED;
    maxVertexAttributes                       = WGPU_LIMIT_U32_UNDEFINED;
    maxVertexBufferArrayStride                = WGPU_LIMIT_U32_UNDEFINED;
    maxInterStageShaderVariables              = WGPU_LIMIT_U32_UNDEFINED;
    maxColorAttachments                       = WGPU_LIMIT_U32_UNDEFINED;
    maxColorAttachmentBytesPerSample          = WGPU_LIMIT_U32_UNDEFINED;
    maxComputeWorkgroupStorageSize            = WGPU_LIMIT_U32_UNDEFINED;
    maxComputeInvocationsPerWorkgroup         = WGPU_LIMIT_U32_UNDEFINED;
    maxComputeWorkgroupSizeX                  = WGPU_LIMIT_U32_UNDEFINED;
    maxComputeWorkgroupSizeY                  = WGPU_LIMIT_U32_UNDEFINED;
    maxComputeWorkgroupSizeZ                  = WGPU_LIMIT_U32_UNDEFINED;
    maxComputeWorkgroupsPerDimension          = WGPU_LIMIT_U32_UNDEFINED;
	return *this;
}

// Methods of MultisampleState
MultisampleState& MultisampleState::setDefault() {
    count                  = 1;
    mask                   = 0xFFFFFFFF;
    alphaToCoverageEnabled = false;
	return *this;
}

// Methods of Origin3D
Origin3D& Origin3D::setDefault() {
    x = 0;
    y = 0;
    z = 0;
	return *this;
}

// Methods of PipelineLayoutDescriptor
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of PrimitiveState
PrimitiveState& PrimitiveState::setDefault() {
    topology         = PrimitiveTopology::TriangleList;
    stripIndexFormat = IndexFormat::Undefined;
    frontFace        = FrontFace::CCW;
    cullMode         = CullMode::None;
    unclippedDepth   = false;
	return *this;
}

// Methods of QuerySetDescriptor
QuerySetDescriptor& QuerySetDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of QueueDescriptor
QueueDescriptor& QueueDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of RenderBundleDescriptor
RenderBundleDescriptor& RenderBundleDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of RenderBundleEncoderDescriptor
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setDefault() {
    depthStencilFormat = TextureFormat::Undefined;
    depthReadOnly      = false;
    stencilReadOnly    = false;
    sampleCount        = 1;
    ((StringView*)&label)->setDefault();
	return *this;
}

// Methods of RenderPassDepthStencilAttachment
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDefault() {
    depthLoadOp       = LoadOp::Undefined;
    depthStoreOp      = StoreOp::Undefined;
    depthReadOnly     = false;
    stencilLoadOp     = LoadOp::Undefined;
    stencilStoreOp    = StoreOp::Undefined;
    stencilClearValue = 0;
    stencilReadOnly   = false;
	return *this;
}

// Methods of RenderPassMaxDrawCount
RenderPassMaxDrawCount& RenderPassMaxDrawCount::setDefault() {
    maxDrawCount = 50000000;
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::RenderPassMaxDrawCount;
    chain.next  = nullptr;
	return *this;
}

// Methods of RenderPassTimestampWrites
RenderPassTimestampWrites& RenderPassTimestampWrites::setDefault() {
	*this = WGPURenderPassTimestampWrites {};
	return *this;
}

// Methods of RequestAdapterOptions
RequestAdapterOptions& RequestAdapterOptions::setDefault() {
    powerPreference      = PowerPreference::Undefined;
    forceFallbackAdapter = false;
    backendType          = BackendType::Undefined;
	return *this;
}

// Methods of SamplerBindingLayout
SamplerBindingLayout& SamplerBindingLayout::setDefault() {
	type = SamplerBindingType::Filtering;
	return *this;
}

// Methods of SamplerDescriptor
SamplerDescriptor& SamplerDescriptor::setDefault() {
    addressModeU = AddressMode::ClampToEdge;
    addressModeV = AddressMode::ClampToEdge;
    addressModeW = AddressMode::ClampToEdge;
    magFilter    = FilterMode::Nearest;
    minFilter    = FilterMode::Nearest;
    mipmapFilter = MipmapFilterMode::Nearest;
    lodMinClamp  = 0;
    lodMaxClamp  = 32;
    compare      = CompareFunction::Undefined;
    ((StringView*)&label)->setDefault();
	return *this;
}

// Methods of ShaderModuleDescriptor
ShaderModuleDescriptor& ShaderModuleDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of ShaderSourceSPIRV
ShaderSourceSPIRV& ShaderSourceSPIRV::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::ShaderSourceSPIRV;
    chain.next  = nullptr;
	return *this;
}

// Methods of ShaderSourceWGSL
ShaderSourceWGSL& ShaderSourceWGSL::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&code)->setDefault();
    chain.sType = SType::ShaderSourceWGSL;
    chain.next  = nullptr;
	return *this;
}

// Methods of StencilFaceState
StencilFaceState& StencilFaceState::setDefault() {
    compare     = CompareFunction::Always;
    failOp      = StencilOperation::Keep;
    depthFailOp = StencilOperation::Keep;
    passOp      = StencilOperation::Keep;
	return *this;
}

// Methods of StorageTextureBindingLayout
StorageTextureBindingLayout& StorageTextureBindingLayout::setDefault() {
    access        = StorageTextureAccess::WriteOnly;
    format        = TextureFormat::Undefined;
    viewDimension = TextureViewDimension::_2D;
	return *this;
}

// Methods of SupportedFeatures
SupportedFeatures& SupportedFeatures::setDefault() {
	*this = WGPUSupportedFeatures {};
	return *this;
}

// Methods of SupportedWGSLLanguageFeatures
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setDefault() {
	*this = WGPUSupportedWGSLLanguageFeatures {};
	return *this;
}

// Methods of SurfaceCapabilities
SurfaceCapabilities& SurfaceCapabilities::setDefault() {
	*this = WGPUSurfaceCapabilities {};
	return *this;
}

// Methods of SurfaceConfiguration
SurfaceConfiguration& SurfaceConfiguration::setDefault() {
    format      = TextureFormat::Undefined;
    presentMode = PresentMode::Undefined;
	return *this;
}

// Methods of SurfaceDescriptor
SurfaceDescriptor& SurfaceDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of SurfaceSourceAndroidNativeWindow
SurfaceSourceAndroidNativeWindow& SurfaceSourceAndroidNativeWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceAndroidNativeWindow;
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceSourceMetalLayer
SurfaceSourceMetalLayer& SurfaceSourceMetalLayer::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceMetalLayer;
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceSourceWaylandSurface
SurfaceSourceWaylandSurface& SurfaceSourceWaylandSurface::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceWaylandSurface;
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceSourceWindowsHWND
SurfaceSourceWindowsHWND& SurfaceSourceWindowsHWND::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceWindowsHWND;
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceSourceXCBWindow
SurfaceSourceXCBWindow& SurfaceSourceXCBWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceXCBWindow;
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceSourceXlibWindow
SurfaceSourceXlibWindow& SurfaceSourceXlibWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceXlibWindow;
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceTexture
SurfaceTexture& SurfaceTexture::setDefault() {
	*this = WGPUSurfaceTexture {};
	return *this;
}

// Methods of TexelCopyBufferLayout
TexelCopyBufferLayout& TexelCopyBufferLayout::setDefault() {
	*this = WGPUTexelCopyBufferLayout {};
	return *this;
}

// Methods of TextureBindingLayout
TextureBindingLayout& TextureBindingLayout::setDefault() {
    sampleType    = TextureSampleType::Float;
    viewDimension = TextureViewDimension::_2D;
    multisampled  = false;
	return *this;
}

// Methods of TextureViewDescriptor
TextureViewDescriptor& TextureViewDescriptor::setDefault() {
    format         = TextureFormat::Undefined;
    dimension      = TextureViewDimension::Undefined;
    baseMipLevel   = 0;
    baseArrayLayer = 0;
    aspect         = TextureAspect::All;
    ((StringView*)&label)->setDefault();
	return *this;
}

// Methods of VertexAttribute
VertexAttribute& VertexAttribute::setDefault() {
	*this = WGPUVertexAttribute {};
	return *this;
}

// Methods of BindGroupDescriptor
BindGroupDescriptor& BindGroupDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of BindGroupLayoutEntry
BindGroupLayoutEntry& BindGroupLayoutEntry::setDefault() {
    ((BufferBindingLayout*)&buffer)->setDefault();
    ((SamplerBindingLayout*)&sampler)->setDefault();
    ((TextureBindingLayout*)&texture)->setDefault();
    ((StorageTextureBindingLayout*)&storageTexture)->setDefault();
    buffer.type           = BufferBindingType::Undefined;
    sampler.type          = SamplerBindingType::Undefined;
    storageTexture.access = StorageTextureAccess::Undefined;
    texture.sampleType    = TextureSampleType::Undefined;
	return *this;
}

// Methods of BlendState
BlendState& BlendState::setDefault() {
    ((BlendComponent*)&color)->setDefault();
    ((BlendComponent*)&alpha)->setDefault();
	return *this;
}

// Methods of CompilationInfo
CompilationInfo& CompilationInfo::setDefault() {
	*this = WGPUCompilationInfo {};
	return *this;
}

// Methods of ComputePassDescriptor
ComputePassDescriptor& ComputePassDescriptor::setDefault() {
	*this = WGPUComputePassDescriptor {};
	return *this;
}

// Methods of DepthStencilState
DepthStencilState& DepthStencilState::setDefault() {
    format              = TextureFormat::Undefined;
    depthWriteEnabled   = OptionalBool::Undefined;
    depthCompare        = CompareFunction::Undefined;
    stencilReadMask     = 0xFFFFFFFF;
    stencilWriteMask    = 0xFFFFFFFF;
    depthBias           = 0;
    depthBiasSlopeScale = 0;
    depthBiasClamp      = 0;
    ((StencilFaceState*)&stencilFront)->setDefault();
    ((StencilFaceState*)&stencilBack)->setDefault();
	return *this;
}

// Methods of DeviceDescriptor
DeviceDescriptor& DeviceDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((QueueDescriptor*)&defaultQueue)->setDefault();
    ((DeviceLostCallbackInfo*)&deviceLostCallbackInfo)->setDefault();
    ((UncapturedErrorCallbackInfo*)&uncapturedErrorCallbackInfo)->setDefault();
	return *this;
}

// Methods of FutureWaitInfo
FutureWaitInfo& FutureWaitInfo::setDefault() {
	((Future*)&future)->setDefault();
	return *this;
}

// Methods of InstanceDescriptor
InstanceDescriptor& InstanceDescriptor::setDefault() {
	((InstanceCapabilities*)&features)->setDefault();
	return *this;
}

// Methods of ProgrammableStageDescriptor
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setDefault() {
	((StringView*)&entryPoint)->setDefault();
	return *this;
}

// Methods of RenderPassColorAttachment
RenderPassColorAttachment& RenderPassColorAttachment::setDefault() {
    loadOp  = LoadOp::Undefined;
    storeOp = StoreOp::Undefined;
    ((Color*)&clearValue)->setDefault();
	return *this;
}

// Methods of TexelCopyBufferInfo
TexelCopyBufferInfo& TexelCopyBufferInfo::setDefault() {
	((TexelCopyBufferLayout*)&layout)->setDefault();
	return *this;
}

// Methods of TexelCopyTextureInfo
TexelCopyTextureInfo& TexelCopyTextureInfo::setDefault() {
    mipLevel = 0;
    aspect   = TextureAspect::All;
    ((Origin3D*)&origin)->setDefault();
	return *this;
}

// Methods of TextureDescriptor
TextureDescriptor& TextureDescriptor::setDefault() {
    dimension     = TextureDimension::_2D;
    format        = TextureFormat::Undefined;
    mipLevelCount = 1;
    sampleCount   = 1;
    ((StringView*)&label)->setDefault();
    ((Extent3D*)&size)->setDefault();
	return *this;
}

// Methods of VertexBufferLayout
VertexBufferLayout& VertexBufferLayout::setDefault() {
	stepMode = VertexStepMode::Vertex;
	return *this;
}

// Methods of BindGroupLayoutDescriptor
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of ColorTargetState
ColorTargetState& ColorTargetState::setDefault() {
	format = TextureFormat::Undefined;
	return *this;
}

// Methods of ComputePipelineDescriptor
ComputePipelineDescriptor& ComputePipelineDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((ProgrammableStageDescriptor*)&compute)->setDefault();
	return *this;
}

// Methods of RenderPassDescriptor
RenderPassDescriptor& RenderPassDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of VertexState
VertexState& VertexState::setDefault() {
	((StringView*)&entryPoint)->setDefault();
	return *this;
}

// Methods of FragmentState
FragmentState& FragmentState::setDefault() {
	((StringView*)&entryPoint)->setDefault();
	return *this;
}

// Methods of RenderPipelineDescriptor
RenderPipelineDescriptor& RenderPipelineDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((VertexState*)&vertex)->setDefault();
    ((PrimitiveState*)&primitive)->setDefault();
    ((MultisampleState*)&multisample)->setDefault();
	return *this;
}

// Methods of InstanceExtras
InstanceExtras& InstanceExtras::setDefault() {
    dx12ShaderCompiler = Dx12Compiler::Undefined;
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&dxcPath)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::InstanceExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of DeviceExtras
DeviceExtras& DeviceExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&tracePath)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::DeviceExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of NativeLimits
NativeLimits& NativeLimits::setDefault() {
    ((ChainedStructOut*)&chain)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::NativeLimits);
    chain.next  = nullptr;
	return *this;
}

// Methods of PushConstantRange
PushConstantRange& PushConstantRange::setDefault() {
	*this = WGPUPushConstantRange {};
	return *this;
}

// Methods of PipelineLayoutExtras
PipelineLayoutExtras& PipelineLayoutExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::PipelineLayoutExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of ShaderDefine
ShaderDefine& ShaderDefine::setDefault() {
    ((StringView*)&name)->setDefault();
    ((StringView*)&value)->setDefault();
	return *this;
}

// Methods of ShaderSourceGLSL
ShaderSourceGLSL& ShaderSourceGLSL::setDefault() {
	*this = WGPUShaderSourceGLSL {};
	chain.sType = static_cast<SType>(NativeSType::ShaderSourceGLSL);
	chain.next = nullptr;
	return *this;
}

// Methods of ShaderModuleDescriptorSpirV
ShaderModuleDescriptorSpirV& ShaderModuleDescriptorSpirV::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}

// Methods of RegistryReport
RegistryReport& RegistryReport::setDefault() {
	*this = WGPURegistryReport {};
	return *this;
}

// Methods of HubReport
HubReport& HubReport::setDefault() {
    ((RegistryReport*)&adapters)->setDefault();
    ((RegistryReport*)&devices)->setDefault();
    ((RegistryReport*)&queues)->setDefault();
    ((RegistryReport*)&pipelineLayouts)->setDefault();
    ((RegistryReport*)&shaderModules)->setDefault();
    ((RegistryReport*)&bindGroupLayouts)->setDefault();
    ((RegistryReport*)&bindGroups)->setDefault();
    ((RegistryReport*)&commandBuffers)->setDefault();
    ((RegistryReport*)&renderBundles)->setDefault();
    ((RegistryReport*)&renderPipelines)->setDefault();
    ((RegistryReport*)&computePipelines)->setDefault();
    ((RegistryReport*)&pipelineCaches)->setDefault();
    ((RegistryReport*)&querySets)->setDefault();
    ((RegistryReport*)&buffers)->setDefault();
    ((RegistryReport*)&textures)->setDefault();
    ((RegistryReport*)&textureViews)->setDefault();
    ((RegistryReport*)&samplers)->setDefault();
	return *this;
}

// Methods of GlobalReport
GlobalReport& GlobalReport::setDefault() {
    ((RegistryReport*)&surfaces)->setDefault();
    ((HubReport*)&hub)->setDefault();
	return *this;
}

// Methods of InstanceEnumerateAdapterOptions
InstanceEnumerateAdapterOptions& InstanceEnumerateAdapterOptions::setDefault() {
	*this = WGPUInstanceEnumerateAdapterOptions {};
	return *this;
}

// Methods of BindGroupEntryExtras
BindGroupEntryExtras& BindGroupEntryExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::BindGroupEntryExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of BindGroupLayoutEntryExtras
BindGroupLayoutEntryExtras& BindGroupLayoutEntryExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::BindGroupLayoutEntryExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of QuerySetDescriptorExtras
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::QuerySetDescriptorExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceConfigurationExtras
SurfaceConfigurationExtras& SurfaceConfigurationExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = static_cast<SType>(NativeSType::SurfaceConfigurationExtras);
    chain.next  = nullptr;
	return *this;
}

// Methods of SurfaceSourceSwapChainPanel
SurfaceSourceSwapChainPanel& SurfaceSourceSwapChainPanel::setDefault() {
	*this = WGPUSurfaceSourceSwapChainPanel {};
	chain.sType = static_cast<SType>(NativeSType::SurfaceSourceSwapChainPanel);
	chain.next = nullptr;
	return *this;
}

// Methods of PrimitiveStateExtras
PrimitiveStateExtras& PrimitiveStateExtras::setDefault() {
	*this = WGPUPrimitiveStateExtras {};
	chain.sType = static_cast<SType>(NativeSType::PrimitiveStateExtras);
	chain.next = nullptr;
	return *this;
}