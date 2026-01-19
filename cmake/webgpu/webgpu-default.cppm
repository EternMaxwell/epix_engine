/**
File for save setDefault implementations of WebGPU C++ types for wgpu-native.
wgpu-native currently does not provide its own init macros, so we implement them here.
*/

// Methods of StringView
void StringView::setDefault() {}

// Methods of ChainedStruct
void ChainedStruct::setDefault() {}

// Methods of ChainedStructOut
void ChainedStructOut::setDefault() {}

// Methods of BufferMapCallbackInfo
void BufferMapCallbackInfo::setDefault() {}

// Methods of CompilationInfoCallbackInfo
void CompilationInfoCallbackInfo::setDefault() {}

// Methods of CreateComputePipelineAsyncCallbackInfo
void CreateComputePipelineAsyncCallbackInfo::setDefault() {}

// Methods of CreateRenderPipelineAsyncCallbackInfo
void CreateRenderPipelineAsyncCallbackInfo::setDefault() {}

// Methods of DeviceLostCallbackInfo
void DeviceLostCallbackInfo::setDefault() {}

// Methods of PopErrorScopeCallbackInfo
void PopErrorScopeCallbackInfo::setDefault() {}

// Methods of QueueWorkDoneCallbackInfo
void QueueWorkDoneCallbackInfo::setDefault() {}

// Methods of RequestAdapterCallbackInfo
void RequestAdapterCallbackInfo::setDefault() {}

// Methods of RequestDeviceCallbackInfo
void RequestDeviceCallbackInfo::setDefault() {}

// Methods of UncapturedErrorCallbackInfo
void UncapturedErrorCallbackInfo::setDefault() {}

// Methods of AdapterInfo
void AdapterInfo::setDefault() {
    backendType = BackendType::Undefined;
    ((StringView*)&vendor)->setDefault();
    ((StringView*)&architecture)->setDefault();
    ((StringView*)&device)->setDefault();
    ((StringView*)&description)->setDefault();
}
void AdapterInfo::freeMembers() { return wgpuAdapterInfoFreeMembers(*this); }

// Methods of BindGroupEntry
void BindGroupEntry::setDefault() { offset = 0; }

// Methods of BlendComponent
void BlendComponent::setDefault() {
    operation = BlendOperation::Add;
    srcFactor = BlendFactor::One;
    dstFactor = BlendFactor::Zero;
}

// Methods of BufferBindingLayout
void BufferBindingLayout::setDefault() {
    type             = BufferBindingType::Uniform;
    hasDynamicOffset = false;
    minBindingSize   = 0;
}

// Methods of BufferDescriptor
void BufferDescriptor::setDefault() {
    mappedAtCreation = false;
    ((StringView*)&label)->setDefault();
}

// Methods of Color
void Color::setDefault() {}

// Methods of CommandBufferDescriptor
void CommandBufferDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of CommandEncoderDescriptor
void CommandEncoderDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of CompilationMessage
void CompilationMessage::setDefault() { ((StringView*)&message)->setDefault(); }

// Methods of ComputePassTimestampWrites
void ComputePassTimestampWrites::setDefault() {}

// Methods of ConstantEntry
void ConstantEntry::setDefault() { ((StringView*)&key)->setDefault(); }

// Methods of Extent3D
void Extent3D::setDefault() {
    height             = 1;
    depthOrArrayLayers = 1;
}

// Methods of Future
void Future::setDefault() {}

// Methods of InstanceCapabilities
void InstanceCapabilities::setDefault() {}

// Methods of Limits
void Limits::setDefault() {
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
}

// Methods of MultisampleState
void MultisampleState::setDefault() {
    count                  = 1;
    mask                   = 0xFFFFFFFF;
    alphaToCoverageEnabled = false;
}

// Methods of Origin3D
void Origin3D::setDefault() {
    x = 0;
    y = 0;
    z = 0;
}

// Methods of PipelineLayoutDescriptor
void PipelineLayoutDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of PrimitiveState
void PrimitiveState::setDefault() {
    topology         = PrimitiveTopology::TriangleList;
    stripIndexFormat = IndexFormat::Undefined;
    frontFace        = FrontFace::CCW;
    cullMode         = CullMode::None;
    unclippedDepth   = false;
}

// Methods of QuerySetDescriptor
void QuerySetDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of QueueDescriptor
void QueueDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of RenderBundleDescriptor
void RenderBundleDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of RenderBundleEncoderDescriptor
void RenderBundleEncoderDescriptor::setDefault() {
    depthStencilFormat = TextureFormat::Undefined;
    depthReadOnly      = false;
    stencilReadOnly    = false;
    sampleCount        = 1;
    ((StringView*)&label)->setDefault();
}

// Methods of RenderPassDepthStencilAttachment
void RenderPassDepthStencilAttachment::setDefault() {
    depthLoadOp       = LoadOp::Undefined;
    depthStoreOp      = StoreOp::Undefined;
    depthReadOnly     = false;
    stencilLoadOp     = LoadOp::Undefined;
    stencilStoreOp    = StoreOp::Undefined;
    stencilClearValue = 0;
    stencilReadOnly   = false;
}

// Methods of RenderPassMaxDrawCount
void RenderPassMaxDrawCount::setDefault() {
    maxDrawCount = 50000000;
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::RenderPassMaxDrawCount;
    chain.next  = nullptr;
}

// Methods of RenderPassTimestampWrites
void RenderPassTimestampWrites::setDefault() {}

// Methods of RequestAdapterOptions
void RequestAdapterOptions::setDefault() {
    powerPreference      = PowerPreference::Undefined;
    forceFallbackAdapter = false;
    backendType          = BackendType::Undefined;
}

// Methods of SamplerBindingLayout
void SamplerBindingLayout::setDefault() { type = SamplerBindingType::Filtering; }

// Methods of SamplerDescriptor
void SamplerDescriptor::setDefault() {
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
}

// Methods of ShaderModuleDescriptor
void ShaderModuleDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of ShaderSourceSPIRV
void ShaderSourceSPIRV::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::ShaderSourceSPIRV;
    chain.next  = nullptr;
}

// Methods of ShaderSourceWGSL
void ShaderSourceWGSL::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&code)->setDefault();
    chain.sType = SType::ShaderSourceWGSL;
    chain.next  = nullptr;
}

// Methods of StencilFaceState
void StencilFaceState::setDefault() {
    compare     = CompareFunction::Always;
    failOp      = StencilOperation::Keep;
    depthFailOp = StencilOperation::Keep;
    passOp      = StencilOperation::Keep;
}

// Methods of StorageTextureBindingLayout
void StorageTextureBindingLayout::setDefault() {
    access        = StorageTextureAccess::WriteOnly;
    format        = TextureFormat::Undefined;
    viewDimension = TextureViewDimension::_2D;
}

// Methods of SupportedFeatures
void SupportedFeatures::setDefault() {}
void SupportedFeatures::freeMembers() { return wgpuSupportedFeaturesFreeMembers(*this); }

// Methods of SupportedWGSLLanguageFeatures
void SupportedWGSLLanguageFeatures::setDefault() {}
void SupportedWGSLLanguageFeatures::freeMembers() { return wgpuSupportedWGSLLanguageFeaturesFreeMembers(*this); }

// Methods of SurfaceCapabilities
void SurfaceCapabilities::setDefault() {}
void SurfaceCapabilities::freeMembers() { return wgpuSurfaceCapabilitiesFreeMembers(*this); }

// Methods of SurfaceConfiguration
void SurfaceConfiguration::setDefault() {
    format      = TextureFormat::Undefined;
    presentMode = PresentMode::Undefined;
}

// Methods of SurfaceDescriptor
void SurfaceDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of SurfaceSourceAndroidNativeWindow
void SurfaceSourceAndroidNativeWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceAndroidNativeWindow;
    chain.next  = nullptr;
}

// Methods of SurfaceSourceMetalLayer
void SurfaceSourceMetalLayer::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceMetalLayer;
    chain.next  = nullptr;
}

// Methods of SurfaceSourceWaylandSurface
void SurfaceSourceWaylandSurface::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceWaylandSurface;
    chain.next  = nullptr;
}

// Methods of SurfaceSourceWindowsHWND
void SurfaceSourceWindowsHWND::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceWindowsHWND;
    chain.next  = nullptr;
}

// Methods of SurfaceSourceXCBWindow
void SurfaceSourceXCBWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceXCBWindow;
    chain.next  = nullptr;
}

// Methods of SurfaceSourceXlibWindow
void SurfaceSourceXlibWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceXlibWindow;
    chain.next  = nullptr;
}

// Methods of SurfaceTexture
void SurfaceTexture::setDefault() {}

// Methods of TexelCopyBufferLayout
void TexelCopyBufferLayout::setDefault() {}

// Methods of TextureBindingLayout
void TextureBindingLayout::setDefault() {
    sampleType    = TextureSampleType::Float;
    viewDimension = TextureViewDimension::_2D;
    multisampled  = false;
}

// Methods of TextureViewDescriptor
void TextureViewDescriptor::setDefault() {
    format         = TextureFormat::Undefined;
    dimension      = TextureViewDimension::Undefined;
    baseMipLevel   = 0;
    baseArrayLayer = 0;
    aspect         = TextureAspect::All;
    ((StringView*)&label)->setDefault();
}

// Methods of VertexAttribute
void VertexAttribute::setDefault() {}

// Methods of BindGroupDescriptor
void BindGroupDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of BindGroupLayoutEntry
void BindGroupLayoutEntry::setDefault() {
    ((BufferBindingLayout*)&buffer)->setDefault();
    ((SamplerBindingLayout*)&sampler)->setDefault();
    ((TextureBindingLayout*)&texture)->setDefault();
    ((StorageTextureBindingLayout*)&storageTexture)->setDefault();
    buffer.type           = BufferBindingType::Undefined;
    sampler.type          = SamplerBindingType::Undefined;
    storageTexture.access = StorageTextureAccess::Undefined;
    texture.sampleType    = TextureSampleType::Undefined;
}

// Methods of BlendState
void BlendState::setDefault() {
    ((BlendComponent*)&color)->setDefault();
    ((BlendComponent*)&alpha)->setDefault();
}

// Methods of CompilationInfo
void CompilationInfo::setDefault() {}

// Methods of ComputePassDescriptor
void ComputePassDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of DepthStencilState
void DepthStencilState::setDefault() {
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
}

// Methods of DeviceDescriptor
void DeviceDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((QueueDescriptor*)&defaultQueue)->setDefault();
    ((DeviceLostCallbackInfo*)&deviceLostCallbackInfo)->setDefault();
    ((UncapturedErrorCallbackInfo*)&uncapturedErrorCallbackInfo)->setDefault();
}

// Methods of FutureWaitInfo
void FutureWaitInfo::setDefault() { ((Future*)&future)->setDefault(); }

// Methods of InstanceDescriptor
void InstanceDescriptor::setDefault() { ((InstanceCapabilities*)&features)->setDefault(); }

// Methods of ProgrammableStageDescriptor
void ProgrammableStageDescriptor::setDefault() { ((StringView*)&entryPoint)->setDefault(); }

// Methods of RenderPassColorAttachment
void RenderPassColorAttachment::setDefault() {
    loadOp  = LoadOp::Undefined;
    storeOp = StoreOp::Undefined;
    ((Color*)&clearValue)->setDefault();
}

// Methods of TexelCopyBufferInfo
void TexelCopyBufferInfo::setDefault() { ((TexelCopyBufferLayout*)&layout)->setDefault(); }

// Methods of TexelCopyTextureInfo
void TexelCopyTextureInfo::setDefault() {
    mipLevel = 0;
    aspect   = TextureAspect::All;
    ((Origin3D*)&origin)->setDefault();
}

// Methods of TextureDescriptor
void TextureDescriptor::setDefault() {
    dimension     = TextureDimension::_2D;
    format        = TextureFormat::Undefined;
    mipLevelCount = 1;
    sampleCount   = 1;
    ((StringView*)&label)->setDefault();
    ((Extent3D*)&size)->setDefault();
}

// Methods of VertexBufferLayout
void VertexBufferLayout::setDefault() { stepMode = VertexStepMode::Vertex; }

// Methods of BindGroupLayoutDescriptor
void BindGroupLayoutDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of ColorTargetState
void ColorTargetState::setDefault() { format = TextureFormat::Undefined; }

// Methods of ComputePipelineDescriptor
void ComputePipelineDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((ProgrammableStageDescriptor*)&compute)->setDefault();
}

// Methods of RenderPassDescriptor
void RenderPassDescriptor::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of VertexState
void VertexState::setDefault() { ((StringView*)&entryPoint)->setDefault(); }

// Methods of FragmentState
void FragmentState::setDefault() { ((StringView*)&entryPoint)->setDefault(); }

// Methods of RenderPipelineDescriptor
void RenderPipelineDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((VertexState*)&vertex)->setDefault();
    ((PrimitiveState*)&primitive)->setDefault();
    ((MultisampleState*)&multisample)->setDefault();
}

// Methods of InstanceExtras
void InstanceExtras::setDefault() {
    dx12ShaderCompiler = Dx12Compiler::Undefined;
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&dxcPath)->setDefault();
    chain.sType = (WGPUSType)NativeSType::InstanceExtras;
    chain.next  = nullptr;
}

// Methods of DeviceExtras
void DeviceExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&tracePath)->setDefault();
    chain.sType = (WGPUSType)NativeSType::DeviceExtras;
    chain.next  = nullptr;
}

// Methods of NativeLimits
void NativeLimits::setDefault() {
    ((ChainedStructOut*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::NativeLimits;
    chain.next  = nullptr;
}

// Methods of PushConstantRange
void PushConstantRange::setDefault() {}

// Methods of PipelineLayoutExtras
void PipelineLayoutExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::PipelineLayoutExtras;
    chain.next  = nullptr;
}

// Methods of ShaderDefine
void ShaderDefine::setDefault() {
    ((StringView*)&name)->setDefault();
    ((StringView*)&value)->setDefault();
}

// Methods of ShaderModuleDescriptorSpirV
void ShaderModuleDescriptorSpirV::setDefault() { ((StringView*)&label)->setDefault(); }

// Methods of RegistryReport
void RegistryReport::setDefault() {}

// Methods of HubReport
void HubReport::setDefault() {
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
}

// Methods of GlobalReport
void GlobalReport::setDefault() {
    ((RegistryReport*)&surfaces)->setDefault();
    ((HubReport*)&hub)->setDefault();
}

// Methods of InstanceEnumerateAdapterOptions
void InstanceEnumerateAdapterOptions::setDefault() {}

// Methods of BindGroupEntryExtras
void BindGroupEntryExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::BindGroupEntryExtras;
    chain.next  = nullptr;
}

// Methods of BindGroupLayoutEntryExtras
void BindGroupLayoutEntryExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::BindGroupLayoutEntryExtras;
    chain.next  = nullptr;
}

// Methods of QuerySetDescriptorExtras
void QuerySetDescriptorExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::QuerySetDescriptorExtras;
    chain.next  = nullptr;
}

// Methods of SurfaceConfigurationExtras
void SurfaceConfigurationExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::SurfaceConfigurationExtras;
    chain.next  = nullptr;
}