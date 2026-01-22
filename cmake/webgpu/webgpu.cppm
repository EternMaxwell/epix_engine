module;

#define WGPUBufferUsage_None WGPUBufferUsage_None_Internal
#define WGPUBufferUsage_MapRead WGPUBufferUsage_MapRead_Internal
#define WGPUBufferUsage_MapWrite WGPUBufferUsage_MapWrite_Internal
#define WGPUBufferUsage_CopySrc WGPUBufferUsage_CopySrc_Internal
#define WGPUBufferUsage_CopyDst WGPUBufferUsage_CopyDst_Internal
#define WGPUBufferUsage_Index WGPUBufferUsage_Index_Internal
#define WGPUBufferUsage_Vertex WGPUBufferUsage_Vertex_Internal
#define WGPUBufferUsage_Uniform WGPUBufferUsage_Uniform_Internal
#define WGPUBufferUsage_Storage WGPUBufferUsage_Storage_Internal
#define WGPUBufferUsage_Indirect WGPUBufferUsage_Indirect_Internal
#define WGPUBufferUsage_QueryResolve WGPUBufferUsage_QueryResolve_Internal
#define WGPUColorWriteMask_None WGPUColorWriteMask_None_Internal
#define WGPUColorWriteMask_Red WGPUColorWriteMask_Red_Internal
#define WGPUColorWriteMask_Green WGPUColorWriteMask_Green_Internal
#define WGPUColorWriteMask_Blue WGPUColorWriteMask_Blue_Internal
#define WGPUColorWriteMask_Alpha WGPUColorWriteMask_Alpha_Internal
#define WGPUColorWriteMask_All WGPUColorWriteMask_All_Internal
#define WGPUMapMode_None WGPUMapMode_None_Internal
#define WGPUMapMode_Read WGPUMapMode_Read_Internal
#define WGPUMapMode_Write WGPUMapMode_Write_Internal
#define WGPUShaderStage_None WGPUShaderStage_None_Internal
#define WGPUShaderStage_Vertex WGPUShaderStage_Vertex_Internal
#define WGPUShaderStage_Fragment WGPUShaderStage_Fragment_Internal
#define WGPUShaderStage_Compute WGPUShaderStage_Compute_Internal
#define WGPUTextureUsage_None WGPUTextureUsage_None_Internal
#define WGPUTextureUsage_CopySrc WGPUTextureUsage_CopySrc_Internal
#define WGPUTextureUsage_CopyDst WGPUTextureUsage_CopyDst_Internal
#define WGPUTextureUsage_TextureBinding WGPUTextureUsage_TextureBinding_Internal
#define WGPUTextureUsage_StorageBinding WGPUTextureUsage_StorageBinding_Internal
#define WGPUTextureUsage_RenderAttachment WGPUTextureUsage_RenderAttachment_Internal
#define WGPUInstanceBackend_All WGPUInstanceBackend_All_Internal
#define WGPUInstanceBackend_Force32 WGPUInstanceBackend_Force32_Internal
#define WGPUInstanceFlag_Default WGPUInstanceFlag_Default_Internal
#define WGPUInstanceFlag_Force32 WGPUInstanceFlag_Force32_Internal
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#undef WGPUBufferUsage_None
#undef WGPUBufferUsage_MapRead
#undef WGPUBufferUsage_MapWrite
#undef WGPUBufferUsage_CopySrc
#undef WGPUBufferUsage_CopyDst
#undef WGPUBufferUsage_Index
#undef WGPUBufferUsage_Vertex
#undef WGPUBufferUsage_Uniform
#undef WGPUBufferUsage_Storage
#undef WGPUBufferUsage_Indirect
#undef WGPUBufferUsage_QueryResolve
#undef WGPUColorWriteMask_None
#undef WGPUColorWriteMask_Red
#undef WGPUColorWriteMask_Green
#undef WGPUColorWriteMask_Blue
#undef WGPUColorWriteMask_Alpha
#undef WGPUColorWriteMask_All
#undef WGPUMapMode_None
#undef WGPUMapMode_Read
#undef WGPUMapMode_Write
#undef WGPUShaderStage_None
#undef WGPUShaderStage_Vertex
#undef WGPUShaderStage_Fragment
#undef WGPUShaderStage_Compute
#undef WGPUTextureUsage_None
#undef WGPUTextureUsage_CopySrc
#undef WGPUTextureUsage_CopyDst
#undef WGPUTextureUsage_TextureBinding
#undef WGPUTextureUsage_StorageBinding
#undef WGPUTextureUsage_RenderAttachment
#undef WGPUInstanceBackend_All
#undef WGPUInstanceBackend_Force32
#undef WGPUInstanceFlag_Default
#undef WGPUInstanceFlag_Force32

#define WEBGPU_CPP_USE_RAW_NAMESPACE

#define WEBGPU_CPP_NAMESPACE wgpu


#include <iostream>
#include <vector>
#include <functional>
#include <cassert>
#include <concepts>
#include <cmath>
#include <memory>
#include <string_view>
#include <span>

#if __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _MSVC_LANG
#  if _MSVC_LANG >= 202002L
#   define NO_DISCARD [[nodiscard("You should keep this handle alive for as long as the callback may get invoked.")]]
#  elif _MSVC_LANG >= 201703L
#   define NO_DISCARD [[nodiscard]]
#  else
#   define NO_DISCARD
#  endif
#else
#  if __cplusplus >= 202002L
#    define NO_DISCARD [[nodiscard("You should keep this handle alive for as long as the callback may get invoked.")]]
#  elif __cplusplus >= 201703L
#    define NO_DISCARD [[nodiscard]]
#  else
#    define NO_DISCARD
#  endif
#endif

export module webgpu;

export using ::WGPUAdapter;
export using ::WGPUBindGroup;
export using ::WGPUBindGroupLayout;
export using ::WGPUBuffer;
export using ::WGPUCommandBuffer;
export using ::WGPUCommandEncoder;
export using ::WGPUComputePassEncoder;
export using ::WGPUComputePipeline;
export using ::WGPUDevice;
export using ::WGPUInstance;
export using ::WGPUPipelineLayout;
export using ::WGPUQuerySet;
export using ::WGPUQueue;
export using ::WGPURenderBundle;
export using ::WGPURenderBundleEncoder;
export using ::WGPURenderPassEncoder;
export using ::WGPURenderPipeline;
export using ::WGPUSampler;
export using ::WGPUShaderModule;
export using ::WGPUSurface;
export using ::WGPUTexture;
export using ::WGPUTextureView;
export using ::WGPUStringView;
export using ::WGPUChainedStruct;
export using ::WGPUChainedStructOut;
export using ::WGPUBufferMapCallbackInfo;
export using ::WGPUCompilationInfoCallbackInfo;
export using ::WGPUCreateComputePipelineAsyncCallbackInfo;
export using ::WGPUCreateRenderPipelineAsyncCallbackInfo;
export using ::WGPUDeviceLostCallbackInfo;
export using ::WGPUPopErrorScopeCallbackInfo;
export using ::WGPUQueueWorkDoneCallbackInfo;
export using ::WGPURequestAdapterCallbackInfo;
export using ::WGPURequestDeviceCallbackInfo;
export using ::WGPUUncapturedErrorCallbackInfo;
export using ::WGPUAdapterInfo;
export using ::WGPUBindGroupEntry;
export using ::WGPUBlendComponent;
export using ::WGPUBufferBindingLayout;
export using ::WGPUBufferDescriptor;
export using ::WGPUColor;
export using ::WGPUCommandBufferDescriptor;
export using ::WGPUCommandEncoderDescriptor;
export using ::WGPUCompilationMessage;
export using ::WGPUComputePassTimestampWrites;
export using ::WGPUConstantEntry;
export using ::WGPUExtent3D;
export using ::WGPUFuture;
export using ::WGPUInstanceCapabilities;
export using ::WGPULimits;
export using ::WGPUMultisampleState;
export using ::WGPUOrigin3D;
export using ::WGPUPipelineLayoutDescriptor;
export using ::WGPUPrimitiveState;
export using ::WGPUQuerySetDescriptor;
export using ::WGPUQueueDescriptor;
export using ::WGPURenderBundleDescriptor;
export using ::WGPURenderBundleEncoderDescriptor;
export using ::WGPURenderPassDepthStencilAttachment;
export using ::WGPURenderPassMaxDrawCount;
export using ::WGPURenderPassTimestampWrites;
export using ::WGPURequestAdapterOptions;
export using ::WGPUSamplerBindingLayout;
export using ::WGPUSamplerDescriptor;
export using ::WGPUShaderModuleDescriptor;
export using ::WGPUShaderSourceSPIRV;
export using ::WGPUShaderSourceWGSL;
export using ::WGPUStencilFaceState;
export using ::WGPUStorageTextureBindingLayout;
export using ::WGPUSupportedFeatures;
export using ::WGPUSupportedWGSLLanguageFeatures;
export using ::WGPUSurfaceCapabilities;
export using ::WGPUSurfaceConfiguration;
export using ::WGPUSurfaceDescriptor;
export using ::WGPUSurfaceSourceAndroidNativeWindow;
export using ::WGPUSurfaceSourceMetalLayer;
export using ::WGPUSurfaceSourceWaylandSurface;
export using ::WGPUSurfaceSourceWindowsHWND;
export using ::WGPUSurfaceSourceXCBWindow;
export using ::WGPUSurfaceSourceXlibWindow;
export using ::WGPUSurfaceTexture;
export using ::WGPUTexelCopyBufferLayout;
export using ::WGPUTextureBindingLayout;
export using ::WGPUTextureViewDescriptor;
export using ::WGPUVertexAttribute;
export using ::WGPUBindGroupDescriptor;
export using ::WGPUBindGroupLayoutEntry;
export using ::WGPUBlendState;
export using ::WGPUCompilationInfo;
export using ::WGPUComputePassDescriptor;
export using ::WGPUDepthStencilState;
export using ::WGPUDeviceDescriptor;
export using ::WGPUFutureWaitInfo;
export using ::WGPUInstanceDescriptor;
export using ::WGPUProgrammableStageDescriptor;
export using ::WGPURenderPassColorAttachment;
export using ::WGPUTexelCopyBufferInfo;
export using ::WGPUTexelCopyTextureInfo;
export using ::WGPUTextureDescriptor;
export using ::WGPUVertexBufferLayout;
export using ::WGPUBindGroupLayoutDescriptor;
export using ::WGPUColorTargetState;
export using ::WGPUComputePipelineDescriptor;
export using ::WGPURenderPassDescriptor;
export using ::WGPUVertexState;
export using ::WGPUFragmentState;
export using ::WGPURenderPipelineDescriptor;
export using ::WGPUInstanceExtras;
export using ::WGPUDeviceExtras;
export using ::WGPUNativeLimits;
export using ::WGPUPushConstantRange;
export using ::WGPUPipelineLayoutExtras;
export using ::WGPUShaderDefine;
export using ::WGPUShaderSourceGLSL;
export using ::WGPUShaderModuleDescriptorSpirV;
export using ::WGPURegistryReport;
export using ::WGPUHubReport;
export using ::WGPUGlobalReport;
export using ::WGPUInstanceEnumerateAdapterOptions;
export using ::WGPUBindGroupEntryExtras;
export using ::WGPUBindGroupLayoutEntryExtras;
export using ::WGPUQuerySetDescriptorExtras;
export using ::WGPUSurfaceConfigurationExtras;
export using ::WGPUSurfaceSourceSwapChainPanel;
export using ::WGPUPrimitiveStateExtras;
export using ::WGPUAdapterType;
export using ::WGPUAdapterType_DiscreteGPU;
export using ::WGPUAdapterType_IntegratedGPU;
export using ::WGPUAdapterType_CPU;
export using ::WGPUAdapterType_Unknown;
export using ::WGPUAdapterType_Force32;
export using ::WGPUAddressMode;
export using ::WGPUAddressMode_Undefined;
export using ::WGPUAddressMode_ClampToEdge;
export using ::WGPUAddressMode_Repeat;
export using ::WGPUAddressMode_MirrorRepeat;
export using ::WGPUAddressMode_Force32;
export using ::WGPUBackendType;
export using ::WGPUBackendType_Undefined;
export using ::WGPUBackendType_Null;
export using ::WGPUBackendType_WebGPU;
export using ::WGPUBackendType_D3D11;
export using ::WGPUBackendType_D3D12;
export using ::WGPUBackendType_Metal;
export using ::WGPUBackendType_Vulkan;
export using ::WGPUBackendType_OpenGL;
export using ::WGPUBackendType_OpenGLES;
export using ::WGPUBackendType_Force32;
export using ::WGPUBlendFactor;
export using ::WGPUBlendFactor_Undefined;
export using ::WGPUBlendFactor_Zero;
export using ::WGPUBlendFactor_One;
export using ::WGPUBlendFactor_Src;
export using ::WGPUBlendFactor_OneMinusSrc;
export using ::WGPUBlendFactor_SrcAlpha;
export using ::WGPUBlendFactor_OneMinusSrcAlpha;
export using ::WGPUBlendFactor_Dst;
export using ::WGPUBlendFactor_OneMinusDst;
export using ::WGPUBlendFactor_DstAlpha;
export using ::WGPUBlendFactor_OneMinusDstAlpha;
export using ::WGPUBlendFactor_SrcAlphaSaturated;
export using ::WGPUBlendFactor_Constant;
export using ::WGPUBlendFactor_OneMinusConstant;
export using ::WGPUBlendFactor_Src1;
export using ::WGPUBlendFactor_OneMinusSrc1;
export using ::WGPUBlendFactor_Src1Alpha;
export using ::WGPUBlendFactor_OneMinusSrc1Alpha;
export using ::WGPUBlendFactor_Force32;
export using ::WGPUBlendOperation;
export using ::WGPUBlendOperation_Undefined;
export using ::WGPUBlendOperation_Add;
export using ::WGPUBlendOperation_Subtract;
export using ::WGPUBlendOperation_ReverseSubtract;
export using ::WGPUBlendOperation_Min;
export using ::WGPUBlendOperation_Max;
export using ::WGPUBlendOperation_Force32;
export using ::WGPUBufferBindingType;
export using ::WGPUBufferBindingType_BindingNotUsed;
export using ::WGPUBufferBindingType_Undefined;
export using ::WGPUBufferBindingType_Uniform;
export using ::WGPUBufferBindingType_Storage;
export using ::WGPUBufferBindingType_ReadOnlyStorage;
export using ::WGPUBufferBindingType_Force32;
export using ::WGPUBufferMapState;
export using ::WGPUBufferMapState_Unmapped;
export using ::WGPUBufferMapState_Pending;
export using ::WGPUBufferMapState_Mapped;
export using ::WGPUBufferMapState_Force32;
export using ::WGPUCallbackMode;
export using ::WGPUCallbackMode_WaitAnyOnly;
export using ::WGPUCallbackMode_AllowProcessEvents;
export using ::WGPUCallbackMode_AllowSpontaneous;
export using ::WGPUCallbackMode_Force32;
export using ::WGPUCompareFunction;
export using ::WGPUCompareFunction_Undefined;
export using ::WGPUCompareFunction_Never;
export using ::WGPUCompareFunction_Less;
export using ::WGPUCompareFunction_Equal;
export using ::WGPUCompareFunction_LessEqual;
export using ::WGPUCompareFunction_Greater;
export using ::WGPUCompareFunction_NotEqual;
export using ::WGPUCompareFunction_GreaterEqual;
export using ::WGPUCompareFunction_Always;
export using ::WGPUCompareFunction_Force32;
export using ::WGPUCompilationInfoRequestStatus;
export using ::WGPUCompilationInfoRequestStatus_Success;
export using ::WGPUCompilationInfoRequestStatus_InstanceDropped;
export using ::WGPUCompilationInfoRequestStatus_Error;
export using ::WGPUCompilationInfoRequestStatus_Unknown;
export using ::WGPUCompilationInfoRequestStatus_Force32;
export using ::WGPUCompilationMessageType;
export using ::WGPUCompilationMessageType_Error;
export using ::WGPUCompilationMessageType_Warning;
export using ::WGPUCompilationMessageType_Info;
export using ::WGPUCompilationMessageType_Force32;
export using ::WGPUCompositeAlphaMode;
export using ::WGPUCompositeAlphaMode_Auto;
export using ::WGPUCompositeAlphaMode_Opaque;
export using ::WGPUCompositeAlphaMode_Premultiplied;
export using ::WGPUCompositeAlphaMode_Unpremultiplied;
export using ::WGPUCompositeAlphaMode_Inherit;
export using ::WGPUCompositeAlphaMode_Force32;
export using ::WGPUCreatePipelineAsyncStatus;
export using ::WGPUCreatePipelineAsyncStatus_Success;
export using ::WGPUCreatePipelineAsyncStatus_InstanceDropped;
export using ::WGPUCreatePipelineAsyncStatus_ValidationError;
export using ::WGPUCreatePipelineAsyncStatus_InternalError;
export using ::WGPUCreatePipelineAsyncStatus_Unknown;
export using ::WGPUCreatePipelineAsyncStatus_Force32;
export using ::WGPUCullMode;
export using ::WGPUCullMode_Undefined;
export using ::WGPUCullMode_None;
export using ::WGPUCullMode_Front;
export using ::WGPUCullMode_Back;
export using ::WGPUCullMode_Force32;
export using ::WGPUDeviceLostReason;
export using ::WGPUDeviceLostReason_Unknown;
export using ::WGPUDeviceLostReason_Destroyed;
export using ::WGPUDeviceLostReason_InstanceDropped;
export using ::WGPUDeviceLostReason_FailedCreation;
export using ::WGPUDeviceLostReason_Force32;
export using ::WGPUErrorFilter;
export using ::WGPUErrorFilter_Validation;
export using ::WGPUErrorFilter_OutOfMemory;
export using ::WGPUErrorFilter_Internal;
export using ::WGPUErrorFilter_Force32;
export using ::WGPUErrorType;
export using ::WGPUErrorType_NoError;
export using ::WGPUErrorType_Validation;
export using ::WGPUErrorType_OutOfMemory;
export using ::WGPUErrorType_Internal;
export using ::WGPUErrorType_Unknown;
export using ::WGPUErrorType_Force32;
export using ::WGPUFeatureLevel;
export using ::WGPUFeatureLevel_Compatibility;
export using ::WGPUFeatureLevel_Core;
export using ::WGPUFeatureLevel_Force32;
export using ::WGPUFeatureName;
export using ::WGPUFeatureName_Undefined;
export using ::WGPUFeatureName_DepthClipControl;
export using ::WGPUFeatureName_Depth32FloatStencil8;
export using ::WGPUFeatureName_TimestampQuery;
export using ::WGPUFeatureName_TextureCompressionBC;
export using ::WGPUFeatureName_TextureCompressionBCSliced3D;
export using ::WGPUFeatureName_TextureCompressionETC2;
export using ::WGPUFeatureName_TextureCompressionASTC;
export using ::WGPUFeatureName_TextureCompressionASTCSliced3D;
export using ::WGPUFeatureName_IndirectFirstInstance;
export using ::WGPUFeatureName_ShaderF16;
export using ::WGPUFeatureName_RG11B10UfloatRenderable;
export using ::WGPUFeatureName_BGRA8UnormStorage;
export using ::WGPUFeatureName_Float32Filterable;
export using ::WGPUFeatureName_Float32Blendable;
export using ::WGPUFeatureName_ClipDistances;
export using ::WGPUFeatureName_DualSourceBlending;
export using ::WGPUFeatureName_Force32;
export using ::WGPUFilterMode;
export using ::WGPUFilterMode_Undefined;
export using ::WGPUFilterMode_Nearest;
export using ::WGPUFilterMode_Linear;
export using ::WGPUFilterMode_Force32;
export using ::WGPUFrontFace;
export using ::WGPUFrontFace_Undefined;
export using ::WGPUFrontFace_CCW;
export using ::WGPUFrontFace_CW;
export using ::WGPUFrontFace_Force32;
export using ::WGPUIndexFormat;
export using ::WGPUIndexFormat_Undefined;
export using ::WGPUIndexFormat_Uint16;
export using ::WGPUIndexFormat_Uint32;
export using ::WGPUIndexFormat_Force32;
export using ::WGPULoadOp;
export using ::WGPULoadOp_Undefined;
export using ::WGPULoadOp_Load;
export using ::WGPULoadOp_Clear;
export using ::WGPULoadOp_Force32;
export using ::WGPUMapAsyncStatus;
export using ::WGPUMapAsyncStatus_Success;
export using ::WGPUMapAsyncStatus_InstanceDropped;
export using ::WGPUMapAsyncStatus_Error;
export using ::WGPUMapAsyncStatus_Aborted;
export using ::WGPUMapAsyncStatus_Unknown;
export using ::WGPUMapAsyncStatus_Force32;
export using ::WGPUMipmapFilterMode;
export using ::WGPUMipmapFilterMode_Undefined;
export using ::WGPUMipmapFilterMode_Nearest;
export using ::WGPUMipmapFilterMode_Linear;
export using ::WGPUMipmapFilterMode_Force32;
export using ::WGPUOptionalBool;
export using ::WGPUOptionalBool_False;
export using ::WGPUOptionalBool_True;
export using ::WGPUOptionalBool_Undefined;
export using ::WGPUOptionalBool_Force32;
export using ::WGPUPopErrorScopeStatus;
export using ::WGPUPopErrorScopeStatus_Success;
export using ::WGPUPopErrorScopeStatus_InstanceDropped;
export using ::WGPUPopErrorScopeStatus_EmptyStack;
export using ::WGPUPopErrorScopeStatus_Force32;
export using ::WGPUPowerPreference;
export using ::WGPUPowerPreference_Undefined;
export using ::WGPUPowerPreference_LowPower;
export using ::WGPUPowerPreference_HighPerformance;
export using ::WGPUPowerPreference_Force32;
export using ::WGPUPresentMode;
export using ::WGPUPresentMode_Undefined;
export using ::WGPUPresentMode_Fifo;
export using ::WGPUPresentMode_FifoRelaxed;
export using ::WGPUPresentMode_Immediate;
export using ::WGPUPresentMode_Mailbox;
export using ::WGPUPresentMode_Force32;
export using ::WGPUPrimitiveTopology;
export using ::WGPUPrimitiveTopology_Undefined;
export using ::WGPUPrimitiveTopology_PointList;
export using ::WGPUPrimitiveTopology_LineList;
export using ::WGPUPrimitiveTopology_LineStrip;
export using ::WGPUPrimitiveTopology_TriangleList;
export using ::WGPUPrimitiveTopology_TriangleStrip;
export using ::WGPUPrimitiveTopology_Force32;
export using ::WGPUQueryType;
export using ::WGPUQueryType_Occlusion;
export using ::WGPUQueryType_Timestamp;
export using ::WGPUQueryType_Force32;
export using ::WGPUQueueWorkDoneStatus;
export using ::WGPUQueueWorkDoneStatus_Success;
export using ::WGPUQueueWorkDoneStatus_InstanceDropped;
export using ::WGPUQueueWorkDoneStatus_Error;
export using ::WGPUQueueWorkDoneStatus_Unknown;
export using ::WGPUQueueWorkDoneStatus_Force32;
export using ::WGPURequestAdapterStatus;
export using ::WGPURequestAdapterStatus_Success;
export using ::WGPURequestAdapterStatus_InstanceDropped;
export using ::WGPURequestAdapterStatus_Unavailable;
export using ::WGPURequestAdapterStatus_Error;
export using ::WGPURequestAdapterStatus_Unknown;
export using ::WGPURequestAdapterStatus_Force32;
export using ::WGPURequestDeviceStatus;
export using ::WGPURequestDeviceStatus_Success;
export using ::WGPURequestDeviceStatus_InstanceDropped;
export using ::WGPURequestDeviceStatus_Error;
export using ::WGPURequestDeviceStatus_Unknown;
export using ::WGPURequestDeviceStatus_Force32;
export using ::WGPUSType;
export using ::WGPUSType_ShaderSourceSPIRV;
export using ::WGPUSType_ShaderSourceWGSL;
export using ::WGPUSType_RenderPassMaxDrawCount;
export using ::WGPUSType_SurfaceSourceMetalLayer;
export using ::WGPUSType_SurfaceSourceWindowsHWND;
export using ::WGPUSType_SurfaceSourceXlibWindow;
export using ::WGPUSType_SurfaceSourceWaylandSurface;
export using ::WGPUSType_SurfaceSourceAndroidNativeWindow;
export using ::WGPUSType_SurfaceSourceXCBWindow;
export using ::WGPUSType_Force32;
export using ::WGPUSamplerBindingType;
export using ::WGPUSamplerBindingType_BindingNotUsed;
export using ::WGPUSamplerBindingType_Undefined;
export using ::WGPUSamplerBindingType_Filtering;
export using ::WGPUSamplerBindingType_NonFiltering;
export using ::WGPUSamplerBindingType_Comparison;
export using ::WGPUSamplerBindingType_Force32;
export using ::WGPUStatus;
export using ::WGPUStatus_Success;
export using ::WGPUStatus_Error;
export using ::WGPUStatus_Force32;
export using ::WGPUStencilOperation;
export using ::WGPUStencilOperation_Undefined;
export using ::WGPUStencilOperation_Keep;
export using ::WGPUStencilOperation_Zero;
export using ::WGPUStencilOperation_Replace;
export using ::WGPUStencilOperation_Invert;
export using ::WGPUStencilOperation_IncrementClamp;
export using ::WGPUStencilOperation_DecrementClamp;
export using ::WGPUStencilOperation_IncrementWrap;
export using ::WGPUStencilOperation_DecrementWrap;
export using ::WGPUStencilOperation_Force32;
export using ::WGPUStorageTextureAccess;
export using ::WGPUStorageTextureAccess_BindingNotUsed;
export using ::WGPUStorageTextureAccess_Undefined;
export using ::WGPUStorageTextureAccess_WriteOnly;
export using ::WGPUStorageTextureAccess_ReadOnly;
export using ::WGPUStorageTextureAccess_ReadWrite;
export using ::WGPUStorageTextureAccess_Force32;
export using ::WGPUStoreOp;
export using ::WGPUStoreOp_Undefined;
export using ::WGPUStoreOp_Store;
export using ::WGPUStoreOp_Discard;
export using ::WGPUStoreOp_Force32;
export using ::WGPUSurfaceGetCurrentTextureStatus;
export using ::WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
export using ::WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
export using ::WGPUSurfaceGetCurrentTextureStatus_Timeout;
export using ::WGPUSurfaceGetCurrentTextureStatus_Outdated;
export using ::WGPUSurfaceGetCurrentTextureStatus_Lost;
export using ::WGPUSurfaceGetCurrentTextureStatus_OutOfMemory;
export using ::WGPUSurfaceGetCurrentTextureStatus_DeviceLost;
export using ::WGPUSurfaceGetCurrentTextureStatus_Error;
export using ::WGPUSurfaceGetCurrentTextureStatus_Force32;
export using ::WGPUTextureAspect;
export using ::WGPUTextureAspect_Undefined;
export using ::WGPUTextureAspect_All;
export using ::WGPUTextureAspect_StencilOnly;
export using ::WGPUTextureAspect_DepthOnly;
export using ::WGPUTextureAspect_Force32;
export using ::WGPUTextureDimension;
export using ::WGPUTextureDimension_Undefined;
export using ::WGPUTextureDimension_1D;
export using ::WGPUTextureDimension_2D;
export using ::WGPUTextureDimension_3D;
export using ::WGPUTextureDimension_Force32;
export using ::WGPUTextureFormat;
export using ::WGPUTextureFormat_Undefined;
export using ::WGPUTextureFormat_R8Unorm;
export using ::WGPUTextureFormat_R8Snorm;
export using ::WGPUTextureFormat_R8Uint;
export using ::WGPUTextureFormat_R8Sint;
export using ::WGPUTextureFormat_R16Uint;
export using ::WGPUTextureFormat_R16Sint;
export using ::WGPUTextureFormat_R16Float;
export using ::WGPUTextureFormat_RG8Unorm;
export using ::WGPUTextureFormat_RG8Snorm;
export using ::WGPUTextureFormat_RG8Uint;
export using ::WGPUTextureFormat_RG8Sint;
export using ::WGPUTextureFormat_R32Float;
export using ::WGPUTextureFormat_R32Uint;
export using ::WGPUTextureFormat_R32Sint;
export using ::WGPUTextureFormat_RG16Uint;
export using ::WGPUTextureFormat_RG16Sint;
export using ::WGPUTextureFormat_RG16Float;
export using ::WGPUTextureFormat_RGBA8Unorm;
export using ::WGPUTextureFormat_RGBA8UnormSrgb;
export using ::WGPUTextureFormat_RGBA8Snorm;
export using ::WGPUTextureFormat_RGBA8Uint;
export using ::WGPUTextureFormat_RGBA8Sint;
export using ::WGPUTextureFormat_BGRA8Unorm;
export using ::WGPUTextureFormat_BGRA8UnormSrgb;
export using ::WGPUTextureFormat_RGB10A2Uint;
export using ::WGPUTextureFormat_RGB10A2Unorm;
export using ::WGPUTextureFormat_RG11B10Ufloat;
export using ::WGPUTextureFormat_RGB9E5Ufloat;
export using ::WGPUTextureFormat_RG32Float;
export using ::WGPUTextureFormat_RG32Uint;
export using ::WGPUTextureFormat_RG32Sint;
export using ::WGPUTextureFormat_RGBA16Uint;
export using ::WGPUTextureFormat_RGBA16Sint;
export using ::WGPUTextureFormat_RGBA16Float;
export using ::WGPUTextureFormat_RGBA32Float;
export using ::WGPUTextureFormat_RGBA32Uint;
export using ::WGPUTextureFormat_RGBA32Sint;
export using ::WGPUTextureFormat_Stencil8;
export using ::WGPUTextureFormat_Depth16Unorm;
export using ::WGPUTextureFormat_Depth24Plus;
export using ::WGPUTextureFormat_Depth24PlusStencil8;
export using ::WGPUTextureFormat_Depth32Float;
export using ::WGPUTextureFormat_Depth32FloatStencil8;
export using ::WGPUTextureFormat_BC1RGBAUnorm;
export using ::WGPUTextureFormat_BC1RGBAUnormSrgb;
export using ::WGPUTextureFormat_BC2RGBAUnorm;
export using ::WGPUTextureFormat_BC2RGBAUnormSrgb;
export using ::WGPUTextureFormat_BC3RGBAUnorm;
export using ::WGPUTextureFormat_BC3RGBAUnormSrgb;
export using ::WGPUTextureFormat_BC4RUnorm;
export using ::WGPUTextureFormat_BC4RSnorm;
export using ::WGPUTextureFormat_BC5RGUnorm;
export using ::WGPUTextureFormat_BC5RGSnorm;
export using ::WGPUTextureFormat_BC6HRGBUfloat;
export using ::WGPUTextureFormat_BC6HRGBFloat;
export using ::WGPUTextureFormat_BC7RGBAUnorm;
export using ::WGPUTextureFormat_BC7RGBAUnormSrgb;
export using ::WGPUTextureFormat_ETC2RGB8Unorm;
export using ::WGPUTextureFormat_ETC2RGB8UnormSrgb;
export using ::WGPUTextureFormat_ETC2RGB8A1Unorm;
export using ::WGPUTextureFormat_ETC2RGB8A1UnormSrgb;
export using ::WGPUTextureFormat_ETC2RGBA8Unorm;
export using ::WGPUTextureFormat_ETC2RGBA8UnormSrgb;
export using ::WGPUTextureFormat_EACR11Unorm;
export using ::WGPUTextureFormat_EACR11Snorm;
export using ::WGPUTextureFormat_EACRG11Unorm;
export using ::WGPUTextureFormat_EACRG11Snorm;
export using ::WGPUTextureFormat_ASTC4x4Unorm;
export using ::WGPUTextureFormat_ASTC4x4UnormSrgb;
export using ::WGPUTextureFormat_ASTC5x4Unorm;
export using ::WGPUTextureFormat_ASTC5x4UnormSrgb;
export using ::WGPUTextureFormat_ASTC5x5Unorm;
export using ::WGPUTextureFormat_ASTC5x5UnormSrgb;
export using ::WGPUTextureFormat_ASTC6x5Unorm;
export using ::WGPUTextureFormat_ASTC6x5UnormSrgb;
export using ::WGPUTextureFormat_ASTC6x6Unorm;
export using ::WGPUTextureFormat_ASTC6x6UnormSrgb;
export using ::WGPUTextureFormat_ASTC8x5Unorm;
export using ::WGPUTextureFormat_ASTC8x5UnormSrgb;
export using ::WGPUTextureFormat_ASTC8x6Unorm;
export using ::WGPUTextureFormat_ASTC8x6UnormSrgb;
export using ::WGPUTextureFormat_ASTC8x8Unorm;
export using ::WGPUTextureFormat_ASTC8x8UnormSrgb;
export using ::WGPUTextureFormat_ASTC10x5Unorm;
export using ::WGPUTextureFormat_ASTC10x5UnormSrgb;
export using ::WGPUTextureFormat_ASTC10x6Unorm;
export using ::WGPUTextureFormat_ASTC10x6UnormSrgb;
export using ::WGPUTextureFormat_ASTC10x8Unorm;
export using ::WGPUTextureFormat_ASTC10x8UnormSrgb;
export using ::WGPUTextureFormat_ASTC10x10Unorm;
export using ::WGPUTextureFormat_ASTC10x10UnormSrgb;
export using ::WGPUTextureFormat_ASTC12x10Unorm;
export using ::WGPUTextureFormat_ASTC12x10UnormSrgb;
export using ::WGPUTextureFormat_ASTC12x12Unorm;
export using ::WGPUTextureFormat_ASTC12x12UnormSrgb;
export using ::WGPUTextureFormat_Force32;
export using ::WGPUTextureSampleType;
export using ::WGPUTextureSampleType_BindingNotUsed;
export using ::WGPUTextureSampleType_Undefined;
export using ::WGPUTextureSampleType_Float;
export using ::WGPUTextureSampleType_UnfilterableFloat;
export using ::WGPUTextureSampleType_Depth;
export using ::WGPUTextureSampleType_Sint;
export using ::WGPUTextureSampleType_Uint;
export using ::WGPUTextureSampleType_Force32;
export using ::WGPUTextureViewDimension;
export using ::WGPUTextureViewDimension_Undefined;
export using ::WGPUTextureViewDimension_1D;
export using ::WGPUTextureViewDimension_2D;
export using ::WGPUTextureViewDimension_2DArray;
export using ::WGPUTextureViewDimension_Cube;
export using ::WGPUTextureViewDimension_CubeArray;
export using ::WGPUTextureViewDimension_3D;
export using ::WGPUTextureViewDimension_Force32;
export using ::WGPUVertexFormat;
export using ::WGPUVertexFormat_Uint8;
export using ::WGPUVertexFormat_Uint8x2;
export using ::WGPUVertexFormat_Uint8x4;
export using ::WGPUVertexFormat_Sint8;
export using ::WGPUVertexFormat_Sint8x2;
export using ::WGPUVertexFormat_Sint8x4;
export using ::WGPUVertexFormat_Unorm8;
export using ::WGPUVertexFormat_Unorm8x2;
export using ::WGPUVertexFormat_Unorm8x4;
export using ::WGPUVertexFormat_Snorm8;
export using ::WGPUVertexFormat_Snorm8x2;
export using ::WGPUVertexFormat_Snorm8x4;
export using ::WGPUVertexFormat_Uint16;
export using ::WGPUVertexFormat_Uint16x2;
export using ::WGPUVertexFormat_Uint16x4;
export using ::WGPUVertexFormat_Sint16;
export using ::WGPUVertexFormat_Sint16x2;
export using ::WGPUVertexFormat_Sint16x4;
export using ::WGPUVertexFormat_Unorm16;
export using ::WGPUVertexFormat_Unorm16x2;
export using ::WGPUVertexFormat_Unorm16x4;
export using ::WGPUVertexFormat_Snorm16;
export using ::WGPUVertexFormat_Snorm16x2;
export using ::WGPUVertexFormat_Snorm16x4;
export using ::WGPUVertexFormat_Float16;
export using ::WGPUVertexFormat_Float16x2;
export using ::WGPUVertexFormat_Float16x4;
export using ::WGPUVertexFormat_Float32;
export using ::WGPUVertexFormat_Float32x2;
export using ::WGPUVertexFormat_Float32x3;
export using ::WGPUVertexFormat_Float32x4;
export using ::WGPUVertexFormat_Uint32;
export using ::WGPUVertexFormat_Uint32x2;
export using ::WGPUVertexFormat_Uint32x3;
export using ::WGPUVertexFormat_Uint32x4;
export using ::WGPUVertexFormat_Sint32;
export using ::WGPUVertexFormat_Sint32x2;
export using ::WGPUVertexFormat_Sint32x3;
export using ::WGPUVertexFormat_Sint32x4;
export using ::WGPUVertexFormat_Unorm10_10_10_2;
export using ::WGPUVertexFormat_Unorm8x4BGRA;
export using ::WGPUVertexFormat_Force32;
export using ::WGPUVertexStepMode;
export using ::WGPUVertexStepMode_VertexBufferNotUsed;
export using ::WGPUVertexStepMode_Undefined;
export using ::WGPUVertexStepMode_Vertex;
export using ::WGPUVertexStepMode_Instance;
export using ::WGPUVertexStepMode_Force32;
export using ::WGPUWGSLLanguageFeatureName;
export using ::WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures;
export using ::WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct;
export using ::WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters;
export using ::WGPUWGSLLanguageFeatureName_PointerCompositeAccess;
export using ::WGPUWGSLLanguageFeatureName_Force32;
export using ::WGPUWaitStatus;
export using ::WGPUWaitStatus_Success;
export using ::WGPUWaitStatus_TimedOut;
export using ::WGPUWaitStatus_UnsupportedTimeout;
export using ::WGPUWaitStatus_UnsupportedCount;
export using ::WGPUWaitStatus_UnsupportedMixedSources;
export using ::WGPUWaitStatus_Force32;
export using ::WGPUBufferUsage;
export constexpr auto WGPUBufferUsage_None = ::WGPUBufferUsage_None_Internal;
export constexpr auto WGPUBufferUsage_MapRead = ::WGPUBufferUsage_MapRead_Internal;
export constexpr auto WGPUBufferUsage_MapWrite = ::WGPUBufferUsage_MapWrite_Internal;
export constexpr auto WGPUBufferUsage_CopySrc = ::WGPUBufferUsage_CopySrc_Internal;
export constexpr auto WGPUBufferUsage_CopyDst = ::WGPUBufferUsage_CopyDst_Internal;
export constexpr auto WGPUBufferUsage_Index = ::WGPUBufferUsage_Index_Internal;
export constexpr auto WGPUBufferUsage_Vertex = ::WGPUBufferUsage_Vertex_Internal;
export constexpr auto WGPUBufferUsage_Uniform = ::WGPUBufferUsage_Uniform_Internal;
export constexpr auto WGPUBufferUsage_Storage = ::WGPUBufferUsage_Storage_Internal;
export constexpr auto WGPUBufferUsage_Indirect = ::WGPUBufferUsage_Indirect_Internal;
export constexpr auto WGPUBufferUsage_QueryResolve = ::WGPUBufferUsage_QueryResolve_Internal;
export using ::WGPUColorWriteMask;
export constexpr auto WGPUColorWriteMask_None = ::WGPUColorWriteMask_None_Internal;
export constexpr auto WGPUColorWriteMask_Red = ::WGPUColorWriteMask_Red_Internal;
export constexpr auto WGPUColorWriteMask_Green = ::WGPUColorWriteMask_Green_Internal;
export constexpr auto WGPUColorWriteMask_Blue = ::WGPUColorWriteMask_Blue_Internal;
export constexpr auto WGPUColorWriteMask_Alpha = ::WGPUColorWriteMask_Alpha_Internal;
export constexpr auto WGPUColorWriteMask_All = ::WGPUColorWriteMask_All_Internal;
export using ::WGPUMapMode;
export constexpr auto WGPUMapMode_None = ::WGPUMapMode_None_Internal;
export constexpr auto WGPUMapMode_Read = ::WGPUMapMode_Read_Internal;
export constexpr auto WGPUMapMode_Write = ::WGPUMapMode_Write_Internal;
export using ::WGPUShaderStage;
export constexpr auto WGPUShaderStage_None = ::WGPUShaderStage_None_Internal;
export constexpr auto WGPUShaderStage_Vertex = ::WGPUShaderStage_Vertex_Internal;
export constexpr auto WGPUShaderStage_Fragment = ::WGPUShaderStage_Fragment_Internal;
export constexpr auto WGPUShaderStage_Compute = ::WGPUShaderStage_Compute_Internal;
export using ::WGPUTextureUsage;
export constexpr auto WGPUTextureUsage_None = ::WGPUTextureUsage_None_Internal;
export constexpr auto WGPUTextureUsage_CopySrc = ::WGPUTextureUsage_CopySrc_Internal;
export constexpr auto WGPUTextureUsage_CopyDst = ::WGPUTextureUsage_CopyDst_Internal;
export constexpr auto WGPUTextureUsage_TextureBinding = ::WGPUTextureUsage_TextureBinding_Internal;
export constexpr auto WGPUTextureUsage_StorageBinding = ::WGPUTextureUsage_StorageBinding_Internal;
export constexpr auto WGPUTextureUsage_RenderAttachment = ::WGPUTextureUsage_RenderAttachment_Internal;
export using ::WGPUNativeSType;
export using ::WGPUSType_DeviceExtras;
export using ::WGPUSType_NativeLimits;
export using ::WGPUSType_PipelineLayoutExtras;
export using ::WGPUSType_ShaderSourceGLSL;
export using ::WGPUSType_InstanceExtras;
export using ::WGPUSType_BindGroupEntryExtras;
export using ::WGPUSType_BindGroupLayoutEntryExtras;
export using ::WGPUSType_QuerySetDescriptorExtras;
export using ::WGPUSType_SurfaceConfigurationExtras;
export using ::WGPUSType_SurfaceSourceSwapChainPanel;
export using ::WGPUSType_PrimitiveStateExtras;
export using ::WGPUNativeSType_Force32;
export using ::WGPUNativeFeature;
export using ::WGPUNativeFeature_PushConstants;
export using ::WGPUNativeFeature_TextureAdapterSpecificFormatFeatures;
export using ::WGPUNativeFeature_MultiDrawIndirectCount;
export using ::WGPUNativeFeature_VertexWritableStorage;
export using ::WGPUNativeFeature_TextureBindingArray;
export using ::WGPUNativeFeature_SampledTextureAndStorageBufferArrayNonUniformIndexing;
export using ::WGPUNativeFeature_PipelineStatisticsQuery;
export using ::WGPUNativeFeature_StorageResourceBindingArray;
export using ::WGPUNativeFeature_PartiallyBoundBindingArray;
export using ::WGPUNativeFeature_TextureFormat16bitNorm;
export using ::WGPUNativeFeature_TextureCompressionAstcHdr;
export using ::WGPUNativeFeature_MappablePrimaryBuffers;
export using ::WGPUNativeFeature_BufferBindingArray;
export using ::WGPUNativeFeature_UniformBufferAndStorageTextureArrayNonUniformIndexing;
export using ::WGPUNativeFeature_PolygonModeLine;
export using ::WGPUNativeFeature_PolygonModePoint;
export using ::WGPUNativeFeature_ConservativeRasterization;
export using ::WGPUNativeFeature_SpirvShaderPassthrough;
export using ::WGPUNativeFeature_VertexAttribute64bit;
export using ::WGPUNativeFeature_TextureFormatNv12;
export using ::WGPUNativeFeature_RayQuery;
export using ::WGPUNativeFeature_ShaderF64;
export using ::WGPUNativeFeature_ShaderI16;
export using ::WGPUNativeFeature_ShaderPrimitiveIndex;
export using ::WGPUNativeFeature_ShaderEarlyDepthTest;
export using ::WGPUNativeFeature_Subgroup;
export using ::WGPUNativeFeature_SubgroupVertex;
export using ::WGPUNativeFeature_SubgroupBarrier;
export using ::WGPUNativeFeature_TimestampQueryInsideEncoders;
export using ::WGPUNativeFeature_TimestampQueryInsidePasses;
export using ::WGPUNativeFeature_ShaderInt64;
export using ::WGPUNativeFeature_Force32;
export using ::WGPULogLevel;
export using ::WGPULogLevel_Off;
export using ::WGPULogLevel_Error;
export using ::WGPULogLevel_Warn;
export using ::WGPULogLevel_Info;
export using ::WGPULogLevel_Debug;
export using ::WGPULogLevel_Trace;
export using ::WGPULogLevel_Force32;
export using ::WGPUInstanceBackend;
export constexpr auto WGPUInstanceBackend_All = ::WGPUInstanceBackend_All_Internal;
export constexpr auto WGPUInstanceBackend_Force32 = ::WGPUInstanceBackend_Force32_Internal;
export using ::WGPUInstanceFlag;
export constexpr auto WGPUInstanceFlag_Default = ::WGPUInstanceFlag_Default_Internal;
export constexpr auto WGPUInstanceFlag_Force32 = ::WGPUInstanceFlag_Force32_Internal;
export using ::WGPUDx12Compiler;
export using ::WGPUDx12Compiler_Undefined;
export using ::WGPUDx12Compiler_Fxc;
export using ::WGPUDx12Compiler_Dxc;
export using ::WGPUDx12Compiler_Force32;
export using ::WGPUGles3MinorVersion;
export using ::WGPUGles3MinorVersion_Automatic;
export using ::WGPUGles3MinorVersion_Version0;
export using ::WGPUGles3MinorVersion_Version1;
export using ::WGPUGles3MinorVersion_Version2;
export using ::WGPUGles3MinorVersion_Force32;
export using ::WGPUPipelineStatisticName;
export using ::WGPUPipelineStatisticName_VertexShaderInvocations;
export using ::WGPUPipelineStatisticName_ClipperInvocations;
export using ::WGPUPipelineStatisticName_ClipperPrimitivesOut;
export using ::WGPUPipelineStatisticName_FragmentShaderInvocations;
export using ::WGPUPipelineStatisticName_ComputeShaderInvocations;
export using ::WGPUPipelineStatisticName_Force32;
export using ::WGPUNativeQueryType;
export using ::WGPUNativeQueryType_PipelineStatistics;
export using ::WGPUNativeQueryType_Force32;
export using ::WGPUDxcMaxShaderModel;
export using ::WGPUDxcMaxShaderModel_V6_0;
export using ::WGPUDxcMaxShaderModel_V6_1;
export using ::WGPUDxcMaxShaderModel_V6_2;
export using ::WGPUDxcMaxShaderModel_V6_3;
export using ::WGPUDxcMaxShaderModel_V6_4;
export using ::WGPUDxcMaxShaderModel_V6_5;
export using ::WGPUDxcMaxShaderModel_V6_6;
export using ::WGPUDxcMaxShaderModel_V6_7;
export using ::WGPUDxcMaxShaderModel_Force32;
export using ::WGPUGLFenceBehaviour;
export using ::WGPUGLFenceBehaviour_Normal;
export using ::WGPUGLFenceBehaviour_AutoFinish;
export using ::WGPUGLFenceBehaviour_Force32;
export using ::WGPUDx12SwapchainKind;
export using ::WGPUDx12SwapchainKind_Undefined;
export using ::WGPUDx12SwapchainKind_DxgiFromHwnd;
export using ::WGPUDx12SwapchainKind_DxgiFromVisual;
export using ::WGPUDx12SwapchainKind_Force32;
export using ::WGPUPolygonMode;
export using ::WGPUPolygonMode_Fill;
export using ::WGPUPolygonMode_Line;
export using ::WGPUPolygonMode_Point;
export using ::WGPUNativeTextureFormat;
export using ::WGPUNativeTextureFormat_R16Unorm;
export using ::WGPUNativeTextureFormat_R16Snorm;
export using ::WGPUNativeTextureFormat_Rg16Unorm;
export using ::WGPUNativeTextureFormat_Rg16Snorm;
export using ::WGPUNativeTextureFormat_Rgba16Unorm;
export using ::WGPUNativeTextureFormat_Rgba16Snorm;
export using ::WGPUNativeTextureFormat_NV12;
export using ::WGPUNativeTextureFormat_P010;
export using ::wgpuCreateInstance;
export using ::wgpuGetInstanceCapabilities;
export using ::wgpuGetProcAddress;
export using ::wgpuAdapterGetFeatures;
export using ::wgpuAdapterGetInfo;
export using ::wgpuAdapterGetLimits;
export using ::wgpuAdapterHasFeature;
export using ::wgpuAdapterRequestDevice;
export using ::wgpuAdapterAddRef;
export using ::wgpuAdapterRelease;
export using ::wgpuAdapterInfoFreeMembers;
export using ::wgpuBindGroupSetLabel;
export using ::wgpuBindGroupAddRef;
export using ::wgpuBindGroupRelease;
export using ::wgpuBindGroupLayoutSetLabel;
export using ::wgpuBindGroupLayoutAddRef;
export using ::wgpuBindGroupLayoutRelease;
export using ::wgpuBufferDestroy;
export using ::wgpuBufferGetConstMappedRange;
export using ::wgpuBufferGetMapState;
export using ::wgpuBufferGetMappedRange;
export using ::wgpuBufferGetSize;
export using ::wgpuBufferGetUsage;
export using ::wgpuBufferMapAsync;
export using ::wgpuBufferSetLabel;
export using ::wgpuBufferUnmap;
export using ::wgpuBufferAddRef;
export using ::wgpuBufferRelease;
export using ::wgpuCommandBufferSetLabel;
export using ::wgpuCommandBufferAddRef;
export using ::wgpuCommandBufferRelease;
export using ::wgpuCommandEncoderBeginComputePass;
export using ::wgpuCommandEncoderBeginRenderPass;
export using ::wgpuCommandEncoderClearBuffer;
export using ::wgpuCommandEncoderCopyBufferToBuffer;
export using ::wgpuCommandEncoderCopyBufferToTexture;
export using ::wgpuCommandEncoderCopyTextureToBuffer;
export using ::wgpuCommandEncoderCopyTextureToTexture;
export using ::wgpuCommandEncoderFinish;
export using ::wgpuCommandEncoderInsertDebugMarker;
export using ::wgpuCommandEncoderPopDebugGroup;
export using ::wgpuCommandEncoderPushDebugGroup;
export using ::wgpuCommandEncoderResolveQuerySet;
export using ::wgpuCommandEncoderSetLabel;
export using ::wgpuCommandEncoderWriteTimestamp;
export using ::wgpuCommandEncoderAddRef;
export using ::wgpuCommandEncoderRelease;
export using ::wgpuComputePassEncoderDispatchWorkgroups;
export using ::wgpuComputePassEncoderDispatchWorkgroupsIndirect;
export using ::wgpuComputePassEncoderEnd;
export using ::wgpuComputePassEncoderInsertDebugMarker;
export using ::wgpuComputePassEncoderPopDebugGroup;
export using ::wgpuComputePassEncoderPushDebugGroup;
export using ::wgpuComputePassEncoderSetBindGroup;
export using ::wgpuComputePassEncoderSetLabel;
export using ::wgpuComputePassEncoderSetPipeline;
export using ::wgpuComputePassEncoderAddRef;
export using ::wgpuComputePassEncoderRelease;
export using ::wgpuComputePipelineGetBindGroupLayout;
export using ::wgpuComputePipelineSetLabel;
export using ::wgpuComputePipelineAddRef;
export using ::wgpuComputePipelineRelease;
export using ::wgpuDeviceCreateBindGroup;
export using ::wgpuDeviceCreateBindGroupLayout;
export using ::wgpuDeviceCreateBuffer;
export using ::wgpuDeviceCreateCommandEncoder;
export using ::wgpuDeviceCreateComputePipeline;
export using ::wgpuDeviceCreateComputePipelineAsync;
export using ::wgpuDeviceCreatePipelineLayout;
export using ::wgpuDeviceCreateQuerySet;
export using ::wgpuDeviceCreateRenderBundleEncoder;
export using ::wgpuDeviceCreateRenderPipeline;
export using ::wgpuDeviceCreateRenderPipelineAsync;
export using ::wgpuDeviceCreateSampler;
export using ::wgpuDeviceCreateShaderModule;
export using ::wgpuDeviceCreateTexture;
export using ::wgpuDeviceDestroy;
export using ::wgpuDeviceGetAdapterInfo;
export using ::wgpuDeviceGetFeatures;
export using ::wgpuDeviceGetLimits;
export using ::wgpuDeviceGetLostFuture;
export using ::wgpuDeviceGetQueue;
export using ::wgpuDeviceHasFeature;
export using ::wgpuDevicePopErrorScope;
export using ::wgpuDevicePushErrorScope;
export using ::wgpuDeviceSetLabel;
export using ::wgpuDeviceAddRef;
export using ::wgpuDeviceRelease;
export using ::wgpuInstanceCreateSurface;
export using ::wgpuInstanceGetWGSLLanguageFeatures;
export using ::wgpuInstanceHasWGSLLanguageFeature;
export using ::wgpuInstanceProcessEvents;
export using ::wgpuInstanceRequestAdapter;
export using ::wgpuInstanceWaitAny;
export using ::wgpuInstanceAddRef;
export using ::wgpuInstanceRelease;
export using ::wgpuPipelineLayoutSetLabel;
export using ::wgpuPipelineLayoutAddRef;
export using ::wgpuPipelineLayoutRelease;
export using ::wgpuQuerySetDestroy;
export using ::wgpuQuerySetGetCount;
export using ::wgpuQuerySetGetType;
export using ::wgpuQuerySetSetLabel;
export using ::wgpuQuerySetAddRef;
export using ::wgpuQuerySetRelease;
export using ::wgpuQueueOnSubmittedWorkDone;
export using ::wgpuQueueSetLabel;
export using ::wgpuQueueSubmit;
export using ::wgpuQueueWriteBuffer;
export using ::wgpuQueueWriteTexture;
export using ::wgpuQueueAddRef;
export using ::wgpuQueueRelease;
export using ::wgpuRenderBundleSetLabel;
export using ::wgpuRenderBundleAddRef;
export using ::wgpuRenderBundleRelease;
export using ::wgpuRenderBundleEncoderDraw;
export using ::wgpuRenderBundleEncoderDrawIndexed;
export using ::wgpuRenderBundleEncoderDrawIndexedIndirect;
export using ::wgpuRenderBundleEncoderDrawIndirect;
export using ::wgpuRenderBundleEncoderFinish;
export using ::wgpuRenderBundleEncoderInsertDebugMarker;
export using ::wgpuRenderBundleEncoderPopDebugGroup;
export using ::wgpuRenderBundleEncoderPushDebugGroup;
export using ::wgpuRenderBundleEncoderSetBindGroup;
export using ::wgpuRenderBundleEncoderSetIndexBuffer;
export using ::wgpuRenderBundleEncoderSetLabel;
export using ::wgpuRenderBundleEncoderSetPipeline;
export using ::wgpuRenderBundleEncoderSetVertexBuffer;
export using ::wgpuRenderBundleEncoderAddRef;
export using ::wgpuRenderBundleEncoderRelease;
export using ::wgpuRenderPassEncoderBeginOcclusionQuery;
export using ::wgpuRenderPassEncoderDraw;
export using ::wgpuRenderPassEncoderDrawIndexed;
export using ::wgpuRenderPassEncoderDrawIndexedIndirect;
export using ::wgpuRenderPassEncoderDrawIndirect;
export using ::wgpuRenderPassEncoderEnd;
export using ::wgpuRenderPassEncoderEndOcclusionQuery;
export using ::wgpuRenderPassEncoderExecuteBundles;
export using ::wgpuRenderPassEncoderInsertDebugMarker;
export using ::wgpuRenderPassEncoderPopDebugGroup;
export using ::wgpuRenderPassEncoderPushDebugGroup;
export using ::wgpuRenderPassEncoderSetBindGroup;
export using ::wgpuRenderPassEncoderSetBlendConstant;
export using ::wgpuRenderPassEncoderSetIndexBuffer;
export using ::wgpuRenderPassEncoderSetLabel;
export using ::wgpuRenderPassEncoderSetPipeline;
export using ::wgpuRenderPassEncoderSetScissorRect;
export using ::wgpuRenderPassEncoderSetStencilReference;
export using ::wgpuRenderPassEncoderSetVertexBuffer;
export using ::wgpuRenderPassEncoderSetViewport;
export using ::wgpuRenderPassEncoderAddRef;
export using ::wgpuRenderPassEncoderRelease;
export using ::wgpuRenderPipelineGetBindGroupLayout;
export using ::wgpuRenderPipelineSetLabel;
export using ::wgpuRenderPipelineAddRef;
export using ::wgpuRenderPipelineRelease;
export using ::wgpuSamplerSetLabel;
export using ::wgpuSamplerAddRef;
export using ::wgpuSamplerRelease;
export using ::wgpuShaderModuleGetCompilationInfo;
export using ::wgpuShaderModuleSetLabel;
export using ::wgpuShaderModuleAddRef;
export using ::wgpuShaderModuleRelease;
export using ::wgpuSupportedFeaturesFreeMembers;
export using ::wgpuSupportedWGSLLanguageFeaturesFreeMembers;
export using ::wgpuSurfaceConfigure;
export using ::wgpuSurfaceGetCapabilities;
export using ::wgpuSurfaceGetCurrentTexture;
export using ::wgpuSurfacePresent;
export using ::wgpuSurfaceSetLabel;
export using ::wgpuSurfaceUnconfigure;
export using ::wgpuSurfaceAddRef;
export using ::wgpuSurfaceRelease;
export using ::wgpuSurfaceCapabilitiesFreeMembers;
export using ::wgpuTextureCreateView;
export using ::wgpuTextureDestroy;
export using ::wgpuTextureGetDepthOrArrayLayers;
export using ::wgpuTextureGetDimension;
export using ::wgpuTextureGetFormat;
export using ::wgpuTextureGetHeight;
export using ::wgpuTextureGetMipLevelCount;
export using ::wgpuTextureGetSampleCount;
export using ::wgpuTextureGetUsage;
export using ::wgpuTextureGetWidth;
export using ::wgpuTextureSetLabel;
export using ::wgpuTextureAddRef;
export using ::wgpuTextureRelease;
export using ::wgpuTextureViewSetLabel;
export using ::wgpuTextureViewAddRef;
export using ::wgpuTextureViewRelease;
export using ::wgpuGenerateReport;
export using ::wgpuInstanceEnumerateAdapters;
export using ::wgpuQueueSubmitForIndex;
export using ::wgpuQueueGetTimestampPeriod;
export using ::wgpuDevicePoll;
export using ::wgpuDeviceCreateShaderModuleSpirV;
export using ::wgpuSetLogCallback;
export using ::wgpuSetLogLevel;
export using ::wgpuGetVersion;
export using ::wgpuRenderPassEncoderSetPushConstants;
export using ::wgpuComputePassEncoderSetPushConstants;
export using ::wgpuRenderBundleEncoderSetPushConstants;
export using ::wgpuRenderPassEncoderMultiDrawIndirect;
export using ::wgpuRenderPassEncoderMultiDrawIndexedIndirect;
export using ::wgpuRenderPassEncoderMultiDrawIndirectCount;
export using ::wgpuRenderPassEncoderMultiDrawIndexedIndirectCount;
export using ::wgpuComputePassEncoderBeginPipelineStatisticsQuery;
export using ::wgpuComputePassEncoderEndPipelineStatisticsQuery;
export using ::wgpuRenderPassEncoderBeginPipelineStatisticsQuery;
export using ::wgpuRenderPassEncoderEndPipelineStatisticsQuery;
export using ::wgpuComputePassEncoderWriteTimestamp;
export using ::wgpuRenderPassEncoderWriteTimestamp;
export using ::WGPUBufferMapCallback;
export using ::WGPUCompilationInfoCallback;
export using ::WGPUCreateComputePipelineAsyncCallback;
export using ::WGPUCreateRenderPipelineAsyncCallback;
export using ::WGPUDeviceLostCallback;
export using ::WGPUPopErrorScopeCallback;
export using ::WGPUQueueWorkDoneCallback;
export using ::WGPURequestAdapterCallback;
export using ::WGPURequestDeviceCallback;
export using ::WGPUUncapturedErrorCallback;
export using ::WGPULogCallback;
export using ::WGPUFlags;
export using ::WGPUBool;
export using ::WGPUSubmissionIndex;

export
#ifdef WEBGPU_CPP_NAMESPACE
namespace WEBGPU_CPP_NAMESPACE
#endif
{

struct DefaultFlag {};
constexpr DefaultFlag Default;

}

#define HANDLE(Type) \
class Type { \
public: \
	typedef Type S; \
	typedef WGPU ## Type W; \
	constexpr Type() : m_raw(nullptr) {} \
	constexpr Type(const W& w) : m_raw(w) {} \
	operator W&() { return m_raw; } \
	operator const W&() const { return m_raw; } \
	operator bool() const { return m_raw != nullptr; } \
	bool operator==(const Type& other) const { return m_raw == other.m_raw; } \
	bool operator!=(const Type& other) const { return m_raw != other.m_raw; } \
	bool operator==(const W& other) const { return m_raw == other; } \
	bool operator!=(const W& other) const { return m_raw != other; } \
	friend auto operator<<(std::ostream &stream, const S& self) -> std::ostream & { \
		return stream << "<wgpu::" << #Type << " " << self.m_raw << ">"; \
	} \
private: \
	W m_raw; \
public:

#define HANDLE_RAII(Type, Template) \
class Type : public Template { \
public: \
	typedef Template H; \
    using H::H; \
	Type(const H& handle) : H(handle) {} \
	Type& operator=(const H& handle) { \
		H& h = *this; \
		if (h) h.release(); \
		h = handle; \
		return *this; \
	} \
	Type& operator=(std::nullptr_t) { \
		H& h = *this; \
		if (h) h.release(); \
		h = nullptr; \
		return *this; \
	} \
	Type(const Type& other) { \
		H& h = *this; \
		if (h) h.release(); \
		h = other; \
		h.addRef(); \
	} \
	Type& operator=(const Type& other) { \
		H& h = *this; \
		if (h) h.release(); \
		h = other; \
		h.addRef(); \
		return *this; \
	} \
	Type(Type&& other) noexcept { \
		H& h = *this; \
		if (h) h.release(); \
		h = other; \
		other = nullptr; \
	} \
	Type& operator=(Type&& other) noexcept { \
		H& h = *this; \
		if (h) h.release(); \
		h = other; \
		other = nullptr; \
		return *this; \
	} \
	~Type() { \
		H& h = *this; \
		if (h) h.release(); \
		h = nullptr; \
	} \
	operator typename H::W() const { return (H&)(*this); } \
	operator bool() const { return (H&)(*this); } \
	bool operator==(const Type& other) const { return (H&)(*this) == (H&)(other); } \
	bool operator!=(const Type& other) const { return (H&)(*this) != (H&)(other); } \
	bool operator==(const typename H::W& other) const { return (H&)(*this) == other; } \
	bool operator!=(const typename H::W& other) const { return (H&)(*this) != other; } \
};

#define DESCRIPTOR(Type) \
struct Type { \
public: \
	typedef Type S; /* S == Self */ \
	typedef WGPU ## Type W; /* W == WGPU Type */ \
	Type() { setDefault(); nextInChain = nullptr; } \
	Type(const W &other) { reinterpret_cast<W&>(*this) = other; nextInChain = nullptr; } \
	Type(const DefaultFlag &) { setDefault(); } \
	Type& operator=(const DefaultFlag &) { setDefault(); return *this; } \
	operator W&() { return reinterpret_cast<W&>(*this); } \
	operator const W&() const { return reinterpret_cast<const W&>(*this); } \
	friend auto operator<<(std::ostream &stream, const S&) -> std::ostream & { \
		return stream << "<wgpu::" << #Type << ">"; \
	} \
public:

#define STRUCT_NO_OSTREAM(Type) \
struct Type { \
public: \
	typedef Type S; /* S == Self */ \
	typedef WGPU ## Type W; /* W == WGPU Type */ \
	Type() { setDefault(); } \
	Type(const W &other) { reinterpret_cast<W&>(*this) = other; } \
	Type(const DefaultFlag &) { setDefault(); } \
	Type& operator=(const DefaultFlag &) { setDefault(); return *this; } \
	operator W&() { return reinterpret_cast<W&>(*this); } \
	operator const W&() const { return reinterpret_cast<const W&>(*this); } \
public:

#define STRUCT(Type) \
STRUCT_NO_OSTREAM(Type) \
	friend auto operator<<(std::ostream &stream, const S&) -> std::ostream & { \
		return stream << "<wgpu::" << #Type << ">"; \
	} \
public:

#define ENUM(Type) \
class Type { \
public: \
	typedef Type S; /* S == Self */ \
	typedef WGPU ## Type W; /* W == WGPU Type */ \
	constexpr Type() : m_raw(W{}) {} /* Using default value-initialization */ \
	constexpr Type(const W& w) : m_raw(w) {} \
	constexpr operator W() const { return m_raw; } \
	W m_raw; /* Ideally, this would be private, but then types generated with this macro would not be structural. */

#define ENUM_ENTRY(Name, Value) \
	static constexpr W Name = (W)(Value);

#define END };



export {

// Other type aliases
namespace wgpu {
using Flags = uint64_t;
using Bool = uint32_t;
using SubmissionIndex = uint64_t;
} // namespace wgpu


// Enumerations
namespace wgpu {
enum class AdapterType: int {
	DiscreteGPU = WGPUAdapterType_DiscreteGPU,
	IntegratedGPU = WGPUAdapterType_IntegratedGPU,
	CPU = WGPUAdapterType_CPU,
	Unknown = WGPUAdapterType_Unknown,
	Force32 = WGPUAdapterType_Force32,
};

inline constexpr bool operator==(AdapterType a, WGPUAdapterType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUAdapterType a, AdapterType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(AdapterType a, WGPUAdapterType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUAdapterType a, AdapterType b) { return !(a == b); }

inline constexpr AdapterType operator|(AdapterType a, AdapterType b) { return static_cast<AdapterType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr AdapterType operator&(AdapterType a, AdapterType b) { return static_cast<AdapterType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class AddressMode: int {
	Undefined = WGPUAddressMode_Undefined,
	ClampToEdge = WGPUAddressMode_ClampToEdge,
	Repeat = WGPUAddressMode_Repeat,
	MirrorRepeat = WGPUAddressMode_MirrorRepeat,
	Force32 = WGPUAddressMode_Force32,
};

inline constexpr bool operator==(AddressMode a, WGPUAddressMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUAddressMode a, AddressMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(AddressMode a, WGPUAddressMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUAddressMode a, AddressMode b) { return !(a == b); }

inline constexpr AddressMode operator|(AddressMode a, AddressMode b) { return static_cast<AddressMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr AddressMode operator&(AddressMode a, AddressMode b) { return static_cast<AddressMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class BackendType: int {
	Undefined = WGPUBackendType_Undefined,
	Null = WGPUBackendType_Null,
	WebGPU = WGPUBackendType_WebGPU,
	D3D11 = WGPUBackendType_D3D11,
	D3D12 = WGPUBackendType_D3D12,
	Metal = WGPUBackendType_Metal,
	Vulkan = WGPUBackendType_Vulkan,
	OpenGL = WGPUBackendType_OpenGL,
	OpenGLES = WGPUBackendType_OpenGLES,
	Force32 = WGPUBackendType_Force32,
};

inline constexpr bool operator==(BackendType a, WGPUBackendType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUBackendType a, BackendType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(BackendType a, WGPUBackendType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUBackendType a, BackendType b) { return !(a == b); }

inline constexpr BackendType operator|(BackendType a, BackendType b) { return static_cast<BackendType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr BackendType operator&(BackendType a, BackendType b) { return static_cast<BackendType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class BlendFactor: int {
	Undefined = WGPUBlendFactor_Undefined,
	Zero = WGPUBlendFactor_Zero,
	One = WGPUBlendFactor_One,
	Src = WGPUBlendFactor_Src,
	OneMinusSrc = WGPUBlendFactor_OneMinusSrc,
	SrcAlpha = WGPUBlendFactor_SrcAlpha,
	OneMinusSrcAlpha = WGPUBlendFactor_OneMinusSrcAlpha,
	Dst = WGPUBlendFactor_Dst,
	OneMinusDst = WGPUBlendFactor_OneMinusDst,
	DstAlpha = WGPUBlendFactor_DstAlpha,
	OneMinusDstAlpha = WGPUBlendFactor_OneMinusDstAlpha,
	SrcAlphaSaturated = WGPUBlendFactor_SrcAlphaSaturated,
	Constant = WGPUBlendFactor_Constant,
	OneMinusConstant = WGPUBlendFactor_OneMinusConstant,
	Src1 = WGPUBlendFactor_Src1,
	OneMinusSrc1 = WGPUBlendFactor_OneMinusSrc1,
	Src1Alpha = WGPUBlendFactor_Src1Alpha,
	OneMinusSrc1Alpha = WGPUBlendFactor_OneMinusSrc1Alpha,
	Force32 = WGPUBlendFactor_Force32,
};

inline constexpr bool operator==(BlendFactor a, WGPUBlendFactor b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUBlendFactor a, BlendFactor b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(BlendFactor a, WGPUBlendFactor b) { return !(a == b); }
inline constexpr bool operator!=(WGPUBlendFactor a, BlendFactor b) { return !(a == b); }

inline constexpr BlendFactor operator|(BlendFactor a, BlendFactor b) { return static_cast<BlendFactor>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr BlendFactor operator&(BlendFactor a, BlendFactor b) { return static_cast<BlendFactor>(static_cast<int>(a) & static_cast<int>(b)); }

enum class BlendOperation: int {
	Undefined = WGPUBlendOperation_Undefined,
	Add = WGPUBlendOperation_Add,
	Subtract = WGPUBlendOperation_Subtract,
	ReverseSubtract = WGPUBlendOperation_ReverseSubtract,
	Min = WGPUBlendOperation_Min,
	Max = WGPUBlendOperation_Max,
	Force32 = WGPUBlendOperation_Force32,
};

inline constexpr bool operator==(BlendOperation a, WGPUBlendOperation b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUBlendOperation a, BlendOperation b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(BlendOperation a, WGPUBlendOperation b) { return !(a == b); }
inline constexpr bool operator!=(WGPUBlendOperation a, BlendOperation b) { return !(a == b); }

inline constexpr BlendOperation operator|(BlendOperation a, BlendOperation b) { return static_cast<BlendOperation>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr BlendOperation operator&(BlendOperation a, BlendOperation b) { return static_cast<BlendOperation>(static_cast<int>(a) & static_cast<int>(b)); }

enum class BufferBindingType: int {
	BindingNotUsed = WGPUBufferBindingType_BindingNotUsed,
	Undefined = WGPUBufferBindingType_Undefined,
	Uniform = WGPUBufferBindingType_Uniform,
	Storage = WGPUBufferBindingType_Storage,
	ReadOnlyStorage = WGPUBufferBindingType_ReadOnlyStorage,
	Force32 = WGPUBufferBindingType_Force32,
};

inline constexpr bool operator==(BufferBindingType a, WGPUBufferBindingType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUBufferBindingType a, BufferBindingType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(BufferBindingType a, WGPUBufferBindingType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUBufferBindingType a, BufferBindingType b) { return !(a == b); }

inline constexpr BufferBindingType operator|(BufferBindingType a, BufferBindingType b) { return static_cast<BufferBindingType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr BufferBindingType operator&(BufferBindingType a, BufferBindingType b) { return static_cast<BufferBindingType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class BufferMapState: int {
	Unmapped = WGPUBufferMapState_Unmapped,
	Pending = WGPUBufferMapState_Pending,
	Mapped = WGPUBufferMapState_Mapped,
	Force32 = WGPUBufferMapState_Force32,
};

inline constexpr bool operator==(BufferMapState a, WGPUBufferMapState b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUBufferMapState a, BufferMapState b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(BufferMapState a, WGPUBufferMapState b) { return !(a == b); }
inline constexpr bool operator!=(WGPUBufferMapState a, BufferMapState b) { return !(a == b); }

inline constexpr BufferMapState operator|(BufferMapState a, BufferMapState b) { return static_cast<BufferMapState>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr BufferMapState operator&(BufferMapState a, BufferMapState b) { return static_cast<BufferMapState>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CallbackMode: int {
	WaitAnyOnly = WGPUCallbackMode_WaitAnyOnly,
	AllowProcessEvents = WGPUCallbackMode_AllowProcessEvents,
	AllowSpontaneous = WGPUCallbackMode_AllowSpontaneous,
	Force32 = WGPUCallbackMode_Force32,
};

inline constexpr bool operator==(CallbackMode a, WGPUCallbackMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCallbackMode a, CallbackMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CallbackMode a, WGPUCallbackMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCallbackMode a, CallbackMode b) { return !(a == b); }

inline constexpr CallbackMode operator|(CallbackMode a, CallbackMode b) { return static_cast<CallbackMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CallbackMode operator&(CallbackMode a, CallbackMode b) { return static_cast<CallbackMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CompareFunction: int {
	Undefined = WGPUCompareFunction_Undefined,
	Never = WGPUCompareFunction_Never,
	Less = WGPUCompareFunction_Less,
	Equal = WGPUCompareFunction_Equal,
	LessEqual = WGPUCompareFunction_LessEqual,
	Greater = WGPUCompareFunction_Greater,
	NotEqual = WGPUCompareFunction_NotEqual,
	GreaterEqual = WGPUCompareFunction_GreaterEqual,
	Always = WGPUCompareFunction_Always,
	Force32 = WGPUCompareFunction_Force32,
};

inline constexpr bool operator==(CompareFunction a, WGPUCompareFunction b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCompareFunction a, CompareFunction b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CompareFunction a, WGPUCompareFunction b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCompareFunction a, CompareFunction b) { return !(a == b); }

inline constexpr CompareFunction operator|(CompareFunction a, CompareFunction b) { return static_cast<CompareFunction>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CompareFunction operator&(CompareFunction a, CompareFunction b) { return static_cast<CompareFunction>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CompilationInfoRequestStatus: int {
	Success = WGPUCompilationInfoRequestStatus_Success,
	InstanceDropped = WGPUCompilationInfoRequestStatus_InstanceDropped,
	Error = WGPUCompilationInfoRequestStatus_Error,
	Unknown = WGPUCompilationInfoRequestStatus_Unknown,
	Force32 = WGPUCompilationInfoRequestStatus_Force32,
};

inline constexpr bool operator==(CompilationInfoRequestStatus a, WGPUCompilationInfoRequestStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCompilationInfoRequestStatus a, CompilationInfoRequestStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CompilationInfoRequestStatus a, WGPUCompilationInfoRequestStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCompilationInfoRequestStatus a, CompilationInfoRequestStatus b) { return !(a == b); }

inline constexpr CompilationInfoRequestStatus operator|(CompilationInfoRequestStatus a, CompilationInfoRequestStatus b) { return static_cast<CompilationInfoRequestStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CompilationInfoRequestStatus operator&(CompilationInfoRequestStatus a, CompilationInfoRequestStatus b) { return static_cast<CompilationInfoRequestStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CompilationMessageType: int {
	Error = WGPUCompilationMessageType_Error,
	Warning = WGPUCompilationMessageType_Warning,
	Info = WGPUCompilationMessageType_Info,
	Force32 = WGPUCompilationMessageType_Force32,
};

inline constexpr bool operator==(CompilationMessageType a, WGPUCompilationMessageType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCompilationMessageType a, CompilationMessageType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CompilationMessageType a, WGPUCompilationMessageType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCompilationMessageType a, CompilationMessageType b) { return !(a == b); }

inline constexpr CompilationMessageType operator|(CompilationMessageType a, CompilationMessageType b) { return static_cast<CompilationMessageType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CompilationMessageType operator&(CompilationMessageType a, CompilationMessageType b) { return static_cast<CompilationMessageType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CompositeAlphaMode: int {
	Auto = WGPUCompositeAlphaMode_Auto,
	Opaque = WGPUCompositeAlphaMode_Opaque,
	Premultiplied = WGPUCompositeAlphaMode_Premultiplied,
	Unpremultiplied = WGPUCompositeAlphaMode_Unpremultiplied,
	Inherit = WGPUCompositeAlphaMode_Inherit,
	Force32 = WGPUCompositeAlphaMode_Force32,
};

inline constexpr bool operator==(CompositeAlphaMode a, WGPUCompositeAlphaMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCompositeAlphaMode a, CompositeAlphaMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CompositeAlphaMode a, WGPUCompositeAlphaMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCompositeAlphaMode a, CompositeAlphaMode b) { return !(a == b); }

inline constexpr CompositeAlphaMode operator|(CompositeAlphaMode a, CompositeAlphaMode b) { return static_cast<CompositeAlphaMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CompositeAlphaMode operator&(CompositeAlphaMode a, CompositeAlphaMode b) { return static_cast<CompositeAlphaMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CreatePipelineAsyncStatus: int {
	Success = WGPUCreatePipelineAsyncStatus_Success,
	InstanceDropped = WGPUCreatePipelineAsyncStatus_InstanceDropped,
	ValidationError = WGPUCreatePipelineAsyncStatus_ValidationError,
	InternalError = WGPUCreatePipelineAsyncStatus_InternalError,
	Unknown = WGPUCreatePipelineAsyncStatus_Unknown,
	Force32 = WGPUCreatePipelineAsyncStatus_Force32,
};

inline constexpr bool operator==(CreatePipelineAsyncStatus a, WGPUCreatePipelineAsyncStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCreatePipelineAsyncStatus a, CreatePipelineAsyncStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CreatePipelineAsyncStatus a, WGPUCreatePipelineAsyncStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCreatePipelineAsyncStatus a, CreatePipelineAsyncStatus b) { return !(a == b); }

inline constexpr CreatePipelineAsyncStatus operator|(CreatePipelineAsyncStatus a, CreatePipelineAsyncStatus b) { return static_cast<CreatePipelineAsyncStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CreatePipelineAsyncStatus operator&(CreatePipelineAsyncStatus a, CreatePipelineAsyncStatus b) { return static_cast<CreatePipelineAsyncStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class CullMode: int {
	Undefined = WGPUCullMode_Undefined,
	None = WGPUCullMode_None,
	Front = WGPUCullMode_Front,
	Back = WGPUCullMode_Back,
	Force32 = WGPUCullMode_Force32,
};

inline constexpr bool operator==(CullMode a, WGPUCullMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUCullMode a, CullMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(CullMode a, WGPUCullMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUCullMode a, CullMode b) { return !(a == b); }

inline constexpr CullMode operator|(CullMode a, CullMode b) { return static_cast<CullMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr CullMode operator&(CullMode a, CullMode b) { return static_cast<CullMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class DeviceLostReason: int {
	Unknown = WGPUDeviceLostReason_Unknown,
	Destroyed = WGPUDeviceLostReason_Destroyed,
	InstanceDropped = WGPUDeviceLostReason_InstanceDropped,
	FailedCreation = WGPUDeviceLostReason_FailedCreation,
	Force32 = WGPUDeviceLostReason_Force32,
};

inline constexpr bool operator==(DeviceLostReason a, WGPUDeviceLostReason b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUDeviceLostReason a, DeviceLostReason b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(DeviceLostReason a, WGPUDeviceLostReason b) { return !(a == b); }
inline constexpr bool operator!=(WGPUDeviceLostReason a, DeviceLostReason b) { return !(a == b); }

inline constexpr DeviceLostReason operator|(DeviceLostReason a, DeviceLostReason b) { return static_cast<DeviceLostReason>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr DeviceLostReason operator&(DeviceLostReason a, DeviceLostReason b) { return static_cast<DeviceLostReason>(static_cast<int>(a) & static_cast<int>(b)); }

enum class ErrorFilter: int {
	Validation = WGPUErrorFilter_Validation,
	OutOfMemory = WGPUErrorFilter_OutOfMemory,
	Internal = WGPUErrorFilter_Internal,
	Force32 = WGPUErrorFilter_Force32,
};

inline constexpr bool operator==(ErrorFilter a, WGPUErrorFilter b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUErrorFilter a, ErrorFilter b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(ErrorFilter a, WGPUErrorFilter b) { return !(a == b); }
inline constexpr bool operator!=(WGPUErrorFilter a, ErrorFilter b) { return !(a == b); }

inline constexpr ErrorFilter operator|(ErrorFilter a, ErrorFilter b) { return static_cast<ErrorFilter>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr ErrorFilter operator&(ErrorFilter a, ErrorFilter b) { return static_cast<ErrorFilter>(static_cast<int>(a) & static_cast<int>(b)); }

enum class ErrorType: int {
	NoError = WGPUErrorType_NoError,
	Validation = WGPUErrorType_Validation,
	OutOfMemory = WGPUErrorType_OutOfMemory,
	Internal = WGPUErrorType_Internal,
	Unknown = WGPUErrorType_Unknown,
	Force32 = WGPUErrorType_Force32,
};

inline constexpr bool operator==(ErrorType a, WGPUErrorType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUErrorType a, ErrorType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(ErrorType a, WGPUErrorType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUErrorType a, ErrorType b) { return !(a == b); }

inline constexpr ErrorType operator|(ErrorType a, ErrorType b) { return static_cast<ErrorType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr ErrorType operator&(ErrorType a, ErrorType b) { return static_cast<ErrorType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class FeatureLevel: int {
	Compatibility = WGPUFeatureLevel_Compatibility,
	Core = WGPUFeatureLevel_Core,
	Force32 = WGPUFeatureLevel_Force32,
};

inline constexpr bool operator==(FeatureLevel a, WGPUFeatureLevel b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUFeatureLevel a, FeatureLevel b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(FeatureLevel a, WGPUFeatureLevel b) { return !(a == b); }
inline constexpr bool operator!=(WGPUFeatureLevel a, FeatureLevel b) { return !(a == b); }

inline constexpr FeatureLevel operator|(FeatureLevel a, FeatureLevel b) { return static_cast<FeatureLevel>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr FeatureLevel operator&(FeatureLevel a, FeatureLevel b) { return static_cast<FeatureLevel>(static_cast<int>(a) & static_cast<int>(b)); }

enum class FeatureName: int {
	Undefined = WGPUFeatureName_Undefined,
	DepthClipControl = WGPUFeatureName_DepthClipControl,
	Depth32FloatStencil8 = WGPUFeatureName_Depth32FloatStencil8,
	TimestampQuery = WGPUFeatureName_TimestampQuery,
	TextureCompressionBC = WGPUFeatureName_TextureCompressionBC,
	TextureCompressionBCSliced3D = WGPUFeatureName_TextureCompressionBCSliced3D,
	TextureCompressionETC2 = WGPUFeatureName_TextureCompressionETC2,
	TextureCompressionASTC = WGPUFeatureName_TextureCompressionASTC,
	TextureCompressionASTCSliced3D = WGPUFeatureName_TextureCompressionASTCSliced3D,
	IndirectFirstInstance = WGPUFeatureName_IndirectFirstInstance,
	ShaderF16 = WGPUFeatureName_ShaderF16,
	RG11B10UfloatRenderable = WGPUFeatureName_RG11B10UfloatRenderable,
	BGRA8UnormStorage = WGPUFeatureName_BGRA8UnormStorage,
	Float32Filterable = WGPUFeatureName_Float32Filterable,
	Float32Blendable = WGPUFeatureName_Float32Blendable,
	ClipDistances = WGPUFeatureName_ClipDistances,
	DualSourceBlending = WGPUFeatureName_DualSourceBlending,
	Force32 = WGPUFeatureName_Force32,
};

inline constexpr bool operator==(FeatureName a, WGPUFeatureName b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUFeatureName a, FeatureName b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(FeatureName a, WGPUFeatureName b) { return !(a == b); }
inline constexpr bool operator!=(WGPUFeatureName a, FeatureName b) { return !(a == b); }

inline constexpr FeatureName operator|(FeatureName a, FeatureName b) { return static_cast<FeatureName>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr FeatureName operator&(FeatureName a, FeatureName b) { return static_cast<FeatureName>(static_cast<int>(a) & static_cast<int>(b)); }

enum class FilterMode: int {
	Undefined = WGPUFilterMode_Undefined,
	Nearest = WGPUFilterMode_Nearest,
	Linear = WGPUFilterMode_Linear,
	Force32 = WGPUFilterMode_Force32,
};

inline constexpr bool operator==(FilterMode a, WGPUFilterMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUFilterMode a, FilterMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(FilterMode a, WGPUFilterMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUFilterMode a, FilterMode b) { return !(a == b); }

inline constexpr FilterMode operator|(FilterMode a, FilterMode b) { return static_cast<FilterMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr FilterMode operator&(FilterMode a, FilterMode b) { return static_cast<FilterMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class FrontFace: int {
	Undefined = WGPUFrontFace_Undefined,
	CCW = WGPUFrontFace_CCW,
	CW = WGPUFrontFace_CW,
	Force32 = WGPUFrontFace_Force32,
};

inline constexpr bool operator==(FrontFace a, WGPUFrontFace b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUFrontFace a, FrontFace b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(FrontFace a, WGPUFrontFace b) { return !(a == b); }
inline constexpr bool operator!=(WGPUFrontFace a, FrontFace b) { return !(a == b); }

inline constexpr FrontFace operator|(FrontFace a, FrontFace b) { return static_cast<FrontFace>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr FrontFace operator&(FrontFace a, FrontFace b) { return static_cast<FrontFace>(static_cast<int>(a) & static_cast<int>(b)); }

enum class IndexFormat: int {
	Undefined = WGPUIndexFormat_Undefined,
	Uint16 = WGPUIndexFormat_Uint16,
	Uint32 = WGPUIndexFormat_Uint32,
	Force32 = WGPUIndexFormat_Force32,
};

inline constexpr bool operator==(IndexFormat a, WGPUIndexFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUIndexFormat a, IndexFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(IndexFormat a, WGPUIndexFormat b) { return !(a == b); }
inline constexpr bool operator!=(WGPUIndexFormat a, IndexFormat b) { return !(a == b); }

inline constexpr IndexFormat operator|(IndexFormat a, IndexFormat b) { return static_cast<IndexFormat>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr IndexFormat operator&(IndexFormat a, IndexFormat b) { return static_cast<IndexFormat>(static_cast<int>(a) & static_cast<int>(b)); }

enum class LoadOp: int {
	Undefined = WGPULoadOp_Undefined,
	Load = WGPULoadOp_Load,
	Clear = WGPULoadOp_Clear,
	Force32 = WGPULoadOp_Force32,
};

inline constexpr bool operator==(LoadOp a, WGPULoadOp b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPULoadOp a, LoadOp b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(LoadOp a, WGPULoadOp b) { return !(a == b); }
inline constexpr bool operator!=(WGPULoadOp a, LoadOp b) { return !(a == b); }

inline constexpr LoadOp operator|(LoadOp a, LoadOp b) { return static_cast<LoadOp>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr LoadOp operator&(LoadOp a, LoadOp b) { return static_cast<LoadOp>(static_cast<int>(a) & static_cast<int>(b)); }

enum class MapAsyncStatus: int {
	Success = WGPUMapAsyncStatus_Success,
	InstanceDropped = WGPUMapAsyncStatus_InstanceDropped,
	Error = WGPUMapAsyncStatus_Error,
	Aborted = WGPUMapAsyncStatus_Aborted,
	Unknown = WGPUMapAsyncStatus_Unknown,
	Force32 = WGPUMapAsyncStatus_Force32,
};

inline constexpr bool operator==(MapAsyncStatus a, WGPUMapAsyncStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUMapAsyncStatus a, MapAsyncStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(MapAsyncStatus a, WGPUMapAsyncStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUMapAsyncStatus a, MapAsyncStatus b) { return !(a == b); }

inline constexpr MapAsyncStatus operator|(MapAsyncStatus a, MapAsyncStatus b) { return static_cast<MapAsyncStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr MapAsyncStatus operator&(MapAsyncStatus a, MapAsyncStatus b) { return static_cast<MapAsyncStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class MipmapFilterMode: int {
	Undefined = WGPUMipmapFilterMode_Undefined,
	Nearest = WGPUMipmapFilterMode_Nearest,
	Linear = WGPUMipmapFilterMode_Linear,
	Force32 = WGPUMipmapFilterMode_Force32,
};

inline constexpr bool operator==(MipmapFilterMode a, WGPUMipmapFilterMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUMipmapFilterMode a, MipmapFilterMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(MipmapFilterMode a, WGPUMipmapFilterMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUMipmapFilterMode a, MipmapFilterMode b) { return !(a == b); }

inline constexpr MipmapFilterMode operator|(MipmapFilterMode a, MipmapFilterMode b) { return static_cast<MipmapFilterMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr MipmapFilterMode operator&(MipmapFilterMode a, MipmapFilterMode b) { return static_cast<MipmapFilterMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class OptionalBool: int {
	False = WGPUOptionalBool_False,
	True = WGPUOptionalBool_True,
	Undefined = WGPUOptionalBool_Undefined,
	Force32 = WGPUOptionalBool_Force32,
};

inline constexpr bool operator==(OptionalBool a, WGPUOptionalBool b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUOptionalBool a, OptionalBool b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(OptionalBool a, WGPUOptionalBool b) { return !(a == b); }
inline constexpr bool operator!=(WGPUOptionalBool a, OptionalBool b) { return !(a == b); }

inline constexpr OptionalBool operator|(OptionalBool a, OptionalBool b) { return static_cast<OptionalBool>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr OptionalBool operator&(OptionalBool a, OptionalBool b) { return static_cast<OptionalBool>(static_cast<int>(a) & static_cast<int>(b)); }

enum class PopErrorScopeStatus: int {
	Success = WGPUPopErrorScopeStatus_Success,
	InstanceDropped = WGPUPopErrorScopeStatus_InstanceDropped,
	EmptyStack = WGPUPopErrorScopeStatus_EmptyStack,
	Force32 = WGPUPopErrorScopeStatus_Force32,
};

inline constexpr bool operator==(PopErrorScopeStatus a, WGPUPopErrorScopeStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUPopErrorScopeStatus a, PopErrorScopeStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(PopErrorScopeStatus a, WGPUPopErrorScopeStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUPopErrorScopeStatus a, PopErrorScopeStatus b) { return !(a == b); }

inline constexpr PopErrorScopeStatus operator|(PopErrorScopeStatus a, PopErrorScopeStatus b) { return static_cast<PopErrorScopeStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr PopErrorScopeStatus operator&(PopErrorScopeStatus a, PopErrorScopeStatus b) { return static_cast<PopErrorScopeStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class PowerPreference: int {
	Undefined = WGPUPowerPreference_Undefined,
	LowPower = WGPUPowerPreference_LowPower,
	HighPerformance = WGPUPowerPreference_HighPerformance,
	Force32 = WGPUPowerPreference_Force32,
};

inline constexpr bool operator==(PowerPreference a, WGPUPowerPreference b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUPowerPreference a, PowerPreference b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(PowerPreference a, WGPUPowerPreference b) { return !(a == b); }
inline constexpr bool operator!=(WGPUPowerPreference a, PowerPreference b) { return !(a == b); }

inline constexpr PowerPreference operator|(PowerPreference a, PowerPreference b) { return static_cast<PowerPreference>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr PowerPreference operator&(PowerPreference a, PowerPreference b) { return static_cast<PowerPreference>(static_cast<int>(a) & static_cast<int>(b)); }

enum class PresentMode: int {
	Undefined = WGPUPresentMode_Undefined,
	Fifo = WGPUPresentMode_Fifo,
	FifoRelaxed = WGPUPresentMode_FifoRelaxed,
	Immediate = WGPUPresentMode_Immediate,
	Mailbox = WGPUPresentMode_Mailbox,
	Force32 = WGPUPresentMode_Force32,
};

inline constexpr bool operator==(PresentMode a, WGPUPresentMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUPresentMode a, PresentMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(PresentMode a, WGPUPresentMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUPresentMode a, PresentMode b) { return !(a == b); }

inline constexpr PresentMode operator|(PresentMode a, PresentMode b) { return static_cast<PresentMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr PresentMode operator&(PresentMode a, PresentMode b) { return static_cast<PresentMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class PrimitiveTopology: int {
	Undefined = WGPUPrimitiveTopology_Undefined,
	PointList = WGPUPrimitiveTopology_PointList,
	LineList = WGPUPrimitiveTopology_LineList,
	LineStrip = WGPUPrimitiveTopology_LineStrip,
	TriangleList = WGPUPrimitiveTopology_TriangleList,
	TriangleStrip = WGPUPrimitiveTopology_TriangleStrip,
	Force32 = WGPUPrimitiveTopology_Force32,
};

inline constexpr bool operator==(PrimitiveTopology a, WGPUPrimitiveTopology b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUPrimitiveTopology a, PrimitiveTopology b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(PrimitiveTopology a, WGPUPrimitiveTopology b) { return !(a == b); }
inline constexpr bool operator!=(WGPUPrimitiveTopology a, PrimitiveTopology b) { return !(a == b); }

inline constexpr PrimitiveTopology operator|(PrimitiveTopology a, PrimitiveTopology b) { return static_cast<PrimitiveTopology>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr PrimitiveTopology operator&(PrimitiveTopology a, PrimitiveTopology b) { return static_cast<PrimitiveTopology>(static_cast<int>(a) & static_cast<int>(b)); }

enum class QueryType: int {
	Occlusion = WGPUQueryType_Occlusion,
	Timestamp = WGPUQueryType_Timestamp,
	Force32 = WGPUQueryType_Force32,
};

inline constexpr bool operator==(QueryType a, WGPUQueryType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUQueryType a, QueryType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(QueryType a, WGPUQueryType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUQueryType a, QueryType b) { return !(a == b); }

inline constexpr QueryType operator|(QueryType a, QueryType b) { return static_cast<QueryType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr QueryType operator&(QueryType a, QueryType b) { return static_cast<QueryType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class QueueWorkDoneStatus: int {
	Success = WGPUQueueWorkDoneStatus_Success,
	InstanceDropped = WGPUQueueWorkDoneStatus_InstanceDropped,
	Error = WGPUQueueWorkDoneStatus_Error,
	Unknown = WGPUQueueWorkDoneStatus_Unknown,
	Force32 = WGPUQueueWorkDoneStatus_Force32,
};

inline constexpr bool operator==(QueueWorkDoneStatus a, WGPUQueueWorkDoneStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUQueueWorkDoneStatus a, QueueWorkDoneStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(QueueWorkDoneStatus a, WGPUQueueWorkDoneStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUQueueWorkDoneStatus a, QueueWorkDoneStatus b) { return !(a == b); }

inline constexpr QueueWorkDoneStatus operator|(QueueWorkDoneStatus a, QueueWorkDoneStatus b) { return static_cast<QueueWorkDoneStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr QueueWorkDoneStatus operator&(QueueWorkDoneStatus a, QueueWorkDoneStatus b) { return static_cast<QueueWorkDoneStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class RequestAdapterStatus: int {
	Success = WGPURequestAdapterStatus_Success,
	InstanceDropped = WGPURequestAdapterStatus_InstanceDropped,
	Unavailable = WGPURequestAdapterStatus_Unavailable,
	Error = WGPURequestAdapterStatus_Error,
	Unknown = WGPURequestAdapterStatus_Unknown,
	Force32 = WGPURequestAdapterStatus_Force32,
};

inline constexpr bool operator==(RequestAdapterStatus a, WGPURequestAdapterStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPURequestAdapterStatus a, RequestAdapterStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(RequestAdapterStatus a, WGPURequestAdapterStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPURequestAdapterStatus a, RequestAdapterStatus b) { return !(a == b); }

inline constexpr RequestAdapterStatus operator|(RequestAdapterStatus a, RequestAdapterStatus b) { return static_cast<RequestAdapterStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr RequestAdapterStatus operator&(RequestAdapterStatus a, RequestAdapterStatus b) { return static_cast<RequestAdapterStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class RequestDeviceStatus: int {
	Success = WGPURequestDeviceStatus_Success,
	InstanceDropped = WGPURequestDeviceStatus_InstanceDropped,
	Error = WGPURequestDeviceStatus_Error,
	Unknown = WGPURequestDeviceStatus_Unknown,
	Force32 = WGPURequestDeviceStatus_Force32,
};

inline constexpr bool operator==(RequestDeviceStatus a, WGPURequestDeviceStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPURequestDeviceStatus a, RequestDeviceStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(RequestDeviceStatus a, WGPURequestDeviceStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPURequestDeviceStatus a, RequestDeviceStatus b) { return !(a == b); }

inline constexpr RequestDeviceStatus operator|(RequestDeviceStatus a, RequestDeviceStatus b) { return static_cast<RequestDeviceStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr RequestDeviceStatus operator&(RequestDeviceStatus a, RequestDeviceStatus b) { return static_cast<RequestDeviceStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class SType: int {
	ShaderSourceSPIRV = WGPUSType_ShaderSourceSPIRV,
	ShaderSourceWGSL = WGPUSType_ShaderSourceWGSL,
	RenderPassMaxDrawCount = WGPUSType_RenderPassMaxDrawCount,
	SurfaceSourceMetalLayer = WGPUSType_SurfaceSourceMetalLayer,
	SurfaceSourceWindowsHWND = WGPUSType_SurfaceSourceWindowsHWND,
	SurfaceSourceXlibWindow = WGPUSType_SurfaceSourceXlibWindow,
	SurfaceSourceWaylandSurface = WGPUSType_SurfaceSourceWaylandSurface,
	SurfaceSourceAndroidNativeWindow = WGPUSType_SurfaceSourceAndroidNativeWindow,
	SurfaceSourceXCBWindow = WGPUSType_SurfaceSourceXCBWindow,
	Force32 = WGPUSType_Force32,
};

inline constexpr bool operator==(SType a, WGPUSType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUSType a, SType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(SType a, WGPUSType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUSType a, SType b) { return !(a == b); }

inline constexpr SType operator|(SType a, SType b) { return static_cast<SType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr SType operator&(SType a, SType b) { return static_cast<SType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class SamplerBindingType: int {
	BindingNotUsed = WGPUSamplerBindingType_BindingNotUsed,
	Undefined = WGPUSamplerBindingType_Undefined,
	Filtering = WGPUSamplerBindingType_Filtering,
	NonFiltering = WGPUSamplerBindingType_NonFiltering,
	Comparison = WGPUSamplerBindingType_Comparison,
	Force32 = WGPUSamplerBindingType_Force32,
};

inline constexpr bool operator==(SamplerBindingType a, WGPUSamplerBindingType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUSamplerBindingType a, SamplerBindingType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(SamplerBindingType a, WGPUSamplerBindingType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUSamplerBindingType a, SamplerBindingType b) { return !(a == b); }

inline constexpr SamplerBindingType operator|(SamplerBindingType a, SamplerBindingType b) { return static_cast<SamplerBindingType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr SamplerBindingType operator&(SamplerBindingType a, SamplerBindingType b) { return static_cast<SamplerBindingType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class Status: int {
	Success = WGPUStatus_Success,
	Error = WGPUStatus_Error,
	Force32 = WGPUStatus_Force32,
};

inline constexpr bool operator==(Status a, WGPUStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUStatus a, Status b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(Status a, WGPUStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUStatus a, Status b) { return !(a == b); }

inline constexpr Status operator|(Status a, Status b) { return static_cast<Status>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr Status operator&(Status a, Status b) { return static_cast<Status>(static_cast<int>(a) & static_cast<int>(b)); }

enum class StencilOperation: int {
	Undefined = WGPUStencilOperation_Undefined,
	Keep = WGPUStencilOperation_Keep,
	Zero = WGPUStencilOperation_Zero,
	Replace = WGPUStencilOperation_Replace,
	Invert = WGPUStencilOperation_Invert,
	IncrementClamp = WGPUStencilOperation_IncrementClamp,
	DecrementClamp = WGPUStencilOperation_DecrementClamp,
	IncrementWrap = WGPUStencilOperation_IncrementWrap,
	DecrementWrap = WGPUStencilOperation_DecrementWrap,
	Force32 = WGPUStencilOperation_Force32,
};

inline constexpr bool operator==(StencilOperation a, WGPUStencilOperation b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUStencilOperation a, StencilOperation b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(StencilOperation a, WGPUStencilOperation b) { return !(a == b); }
inline constexpr bool operator!=(WGPUStencilOperation a, StencilOperation b) { return !(a == b); }

inline constexpr StencilOperation operator|(StencilOperation a, StencilOperation b) { return static_cast<StencilOperation>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr StencilOperation operator&(StencilOperation a, StencilOperation b) { return static_cast<StencilOperation>(static_cast<int>(a) & static_cast<int>(b)); }

enum class StorageTextureAccess: int {
	BindingNotUsed = WGPUStorageTextureAccess_BindingNotUsed,
	Undefined = WGPUStorageTextureAccess_Undefined,
	WriteOnly = WGPUStorageTextureAccess_WriteOnly,
	ReadOnly = WGPUStorageTextureAccess_ReadOnly,
	ReadWrite = WGPUStorageTextureAccess_ReadWrite,
	Force32 = WGPUStorageTextureAccess_Force32,
};

inline constexpr bool operator==(StorageTextureAccess a, WGPUStorageTextureAccess b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUStorageTextureAccess a, StorageTextureAccess b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(StorageTextureAccess a, WGPUStorageTextureAccess b) { return !(a == b); }
inline constexpr bool operator!=(WGPUStorageTextureAccess a, StorageTextureAccess b) { return !(a == b); }

inline constexpr StorageTextureAccess operator|(StorageTextureAccess a, StorageTextureAccess b) { return static_cast<StorageTextureAccess>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr StorageTextureAccess operator&(StorageTextureAccess a, StorageTextureAccess b) { return static_cast<StorageTextureAccess>(static_cast<int>(a) & static_cast<int>(b)); }

enum class StoreOp: int {
	Undefined = WGPUStoreOp_Undefined,
	Store = WGPUStoreOp_Store,
	Discard = WGPUStoreOp_Discard,
	Force32 = WGPUStoreOp_Force32,
};

inline constexpr bool operator==(StoreOp a, WGPUStoreOp b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUStoreOp a, StoreOp b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(StoreOp a, WGPUStoreOp b) { return !(a == b); }
inline constexpr bool operator!=(WGPUStoreOp a, StoreOp b) { return !(a == b); }

inline constexpr StoreOp operator|(StoreOp a, StoreOp b) { return static_cast<StoreOp>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr StoreOp operator&(StoreOp a, StoreOp b) { return static_cast<StoreOp>(static_cast<int>(a) & static_cast<int>(b)); }

enum class SurfaceGetCurrentTextureStatus: int {
	SuccessOptimal = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal,
	SuccessSuboptimal = WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal,
	Timeout = WGPUSurfaceGetCurrentTextureStatus_Timeout,
	Outdated = WGPUSurfaceGetCurrentTextureStatus_Outdated,
	Lost = WGPUSurfaceGetCurrentTextureStatus_Lost,
	OutOfMemory = WGPUSurfaceGetCurrentTextureStatus_OutOfMemory,
	DeviceLost = WGPUSurfaceGetCurrentTextureStatus_DeviceLost,
	Error = WGPUSurfaceGetCurrentTextureStatus_Error,
	Force32 = WGPUSurfaceGetCurrentTextureStatus_Force32,
};

inline constexpr bool operator==(SurfaceGetCurrentTextureStatus a, WGPUSurfaceGetCurrentTextureStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUSurfaceGetCurrentTextureStatus a, SurfaceGetCurrentTextureStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(SurfaceGetCurrentTextureStatus a, WGPUSurfaceGetCurrentTextureStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUSurfaceGetCurrentTextureStatus a, SurfaceGetCurrentTextureStatus b) { return !(a == b); }

inline constexpr SurfaceGetCurrentTextureStatus operator|(SurfaceGetCurrentTextureStatus a, SurfaceGetCurrentTextureStatus b) { return static_cast<SurfaceGetCurrentTextureStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr SurfaceGetCurrentTextureStatus operator&(SurfaceGetCurrentTextureStatus a, SurfaceGetCurrentTextureStatus b) { return static_cast<SurfaceGetCurrentTextureStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class TextureAspect: int {
	Undefined = WGPUTextureAspect_Undefined,
	All = WGPUTextureAspect_All,
	StencilOnly = WGPUTextureAspect_StencilOnly,
	DepthOnly = WGPUTextureAspect_DepthOnly,
	Force32 = WGPUTextureAspect_Force32,
};

inline constexpr bool operator==(TextureAspect a, WGPUTextureAspect b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUTextureAspect a, TextureAspect b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(TextureAspect a, WGPUTextureAspect b) { return !(a == b); }
inline constexpr bool operator!=(WGPUTextureAspect a, TextureAspect b) { return !(a == b); }

inline constexpr TextureAspect operator|(TextureAspect a, TextureAspect b) { return static_cast<TextureAspect>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr TextureAspect operator&(TextureAspect a, TextureAspect b) { return static_cast<TextureAspect>(static_cast<int>(a) & static_cast<int>(b)); }

enum class TextureDimension: int {
	Undefined = WGPUTextureDimension_Undefined,
	_1D = WGPUTextureDimension_1D,
	_2D = WGPUTextureDimension_2D,
	_3D = WGPUTextureDimension_3D,
	Force32 = WGPUTextureDimension_Force32,
};

inline constexpr bool operator==(TextureDimension a, WGPUTextureDimension b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUTextureDimension a, TextureDimension b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(TextureDimension a, WGPUTextureDimension b) { return !(a == b); }
inline constexpr bool operator!=(WGPUTextureDimension a, TextureDimension b) { return !(a == b); }

inline constexpr TextureDimension operator|(TextureDimension a, TextureDimension b) { return static_cast<TextureDimension>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr TextureDimension operator&(TextureDimension a, TextureDimension b) { return static_cast<TextureDimension>(static_cast<int>(a) & static_cast<int>(b)); }

enum class TextureFormat: int {
	Undefined = WGPUTextureFormat_Undefined,
	R8Unorm = WGPUTextureFormat_R8Unorm,
	R8Snorm = WGPUTextureFormat_R8Snorm,
	R8Uint = WGPUTextureFormat_R8Uint,
	R8Sint = WGPUTextureFormat_R8Sint,
	R16Uint = WGPUTextureFormat_R16Uint,
	R16Sint = WGPUTextureFormat_R16Sint,
	R16Float = WGPUTextureFormat_R16Float,
	RG8Unorm = WGPUTextureFormat_RG8Unorm,
	RG8Snorm = WGPUTextureFormat_RG8Snorm,
	RG8Uint = WGPUTextureFormat_RG8Uint,
	RG8Sint = WGPUTextureFormat_RG8Sint,
	R32Float = WGPUTextureFormat_R32Float,
	R32Uint = WGPUTextureFormat_R32Uint,
	R32Sint = WGPUTextureFormat_R32Sint,
	RG16Uint = WGPUTextureFormat_RG16Uint,
	RG16Sint = WGPUTextureFormat_RG16Sint,
	RG16Float = WGPUTextureFormat_RG16Float,
	RGBA8Unorm = WGPUTextureFormat_RGBA8Unorm,
	RGBA8UnormSrgb = WGPUTextureFormat_RGBA8UnormSrgb,
	RGBA8Snorm = WGPUTextureFormat_RGBA8Snorm,
	RGBA8Uint = WGPUTextureFormat_RGBA8Uint,
	RGBA8Sint = WGPUTextureFormat_RGBA8Sint,
	BGRA8Unorm = WGPUTextureFormat_BGRA8Unorm,
	BGRA8UnormSrgb = WGPUTextureFormat_BGRA8UnormSrgb,
	RGB10A2Uint = WGPUTextureFormat_RGB10A2Uint,
	RGB10A2Unorm = WGPUTextureFormat_RGB10A2Unorm,
	RG11B10Ufloat = WGPUTextureFormat_RG11B10Ufloat,
	RGB9E5Ufloat = WGPUTextureFormat_RGB9E5Ufloat,
	RG32Float = WGPUTextureFormat_RG32Float,
	RG32Uint = WGPUTextureFormat_RG32Uint,
	RG32Sint = WGPUTextureFormat_RG32Sint,
	RGBA16Uint = WGPUTextureFormat_RGBA16Uint,
	RGBA16Sint = WGPUTextureFormat_RGBA16Sint,
	RGBA16Float = WGPUTextureFormat_RGBA16Float,
	RGBA32Float = WGPUTextureFormat_RGBA32Float,
	RGBA32Uint = WGPUTextureFormat_RGBA32Uint,
	RGBA32Sint = WGPUTextureFormat_RGBA32Sint,
	Stencil8 = WGPUTextureFormat_Stencil8,
	Depth16Unorm = WGPUTextureFormat_Depth16Unorm,
	Depth24Plus = WGPUTextureFormat_Depth24Plus,
	Depth24PlusStencil8 = WGPUTextureFormat_Depth24PlusStencil8,
	Depth32Float = WGPUTextureFormat_Depth32Float,
	Depth32FloatStencil8 = WGPUTextureFormat_Depth32FloatStencil8,
	BC1RGBAUnorm = WGPUTextureFormat_BC1RGBAUnorm,
	BC1RGBAUnormSrgb = WGPUTextureFormat_BC1RGBAUnormSrgb,
	BC2RGBAUnorm = WGPUTextureFormat_BC2RGBAUnorm,
	BC2RGBAUnormSrgb = WGPUTextureFormat_BC2RGBAUnormSrgb,
	BC3RGBAUnorm = WGPUTextureFormat_BC3RGBAUnorm,
	BC3RGBAUnormSrgb = WGPUTextureFormat_BC3RGBAUnormSrgb,
	BC4RUnorm = WGPUTextureFormat_BC4RUnorm,
	BC4RSnorm = WGPUTextureFormat_BC4RSnorm,
	BC5RGUnorm = WGPUTextureFormat_BC5RGUnorm,
	BC5RGSnorm = WGPUTextureFormat_BC5RGSnorm,
	BC6HRGBUfloat = WGPUTextureFormat_BC6HRGBUfloat,
	BC6HRGBFloat = WGPUTextureFormat_BC6HRGBFloat,
	BC7RGBAUnorm = WGPUTextureFormat_BC7RGBAUnorm,
	BC7RGBAUnormSrgb = WGPUTextureFormat_BC7RGBAUnormSrgb,
	ETC2RGB8Unorm = WGPUTextureFormat_ETC2RGB8Unorm,
	ETC2RGB8UnormSrgb = WGPUTextureFormat_ETC2RGB8UnormSrgb,
	ETC2RGB8A1Unorm = WGPUTextureFormat_ETC2RGB8A1Unorm,
	ETC2RGB8A1UnormSrgb = WGPUTextureFormat_ETC2RGB8A1UnormSrgb,
	ETC2RGBA8Unorm = WGPUTextureFormat_ETC2RGBA8Unorm,
	ETC2RGBA8UnormSrgb = WGPUTextureFormat_ETC2RGBA8UnormSrgb,
	EACR11Unorm = WGPUTextureFormat_EACR11Unorm,
	EACR11Snorm = WGPUTextureFormat_EACR11Snorm,
	EACRG11Unorm = WGPUTextureFormat_EACRG11Unorm,
	EACRG11Snorm = WGPUTextureFormat_EACRG11Snorm,
	ASTC4x4Unorm = WGPUTextureFormat_ASTC4x4Unorm,
	ASTC4x4UnormSrgb = WGPUTextureFormat_ASTC4x4UnormSrgb,
	ASTC5x4Unorm = WGPUTextureFormat_ASTC5x4Unorm,
	ASTC5x4UnormSrgb = WGPUTextureFormat_ASTC5x4UnormSrgb,
	ASTC5x5Unorm = WGPUTextureFormat_ASTC5x5Unorm,
	ASTC5x5UnormSrgb = WGPUTextureFormat_ASTC5x5UnormSrgb,
	ASTC6x5Unorm = WGPUTextureFormat_ASTC6x5Unorm,
	ASTC6x5UnormSrgb = WGPUTextureFormat_ASTC6x5UnormSrgb,
	ASTC6x6Unorm = WGPUTextureFormat_ASTC6x6Unorm,
	ASTC6x6UnormSrgb = WGPUTextureFormat_ASTC6x6UnormSrgb,
	ASTC8x5Unorm = WGPUTextureFormat_ASTC8x5Unorm,
	ASTC8x5UnormSrgb = WGPUTextureFormat_ASTC8x5UnormSrgb,
	ASTC8x6Unorm = WGPUTextureFormat_ASTC8x6Unorm,
	ASTC8x6UnormSrgb = WGPUTextureFormat_ASTC8x6UnormSrgb,
	ASTC8x8Unorm = WGPUTextureFormat_ASTC8x8Unorm,
	ASTC8x8UnormSrgb = WGPUTextureFormat_ASTC8x8UnormSrgb,
	ASTC10x5Unorm = WGPUTextureFormat_ASTC10x5Unorm,
	ASTC10x5UnormSrgb = WGPUTextureFormat_ASTC10x5UnormSrgb,
	ASTC10x6Unorm = WGPUTextureFormat_ASTC10x6Unorm,
	ASTC10x6UnormSrgb = WGPUTextureFormat_ASTC10x6UnormSrgb,
	ASTC10x8Unorm = WGPUTextureFormat_ASTC10x8Unorm,
	ASTC10x8UnormSrgb = WGPUTextureFormat_ASTC10x8UnormSrgb,
	ASTC10x10Unorm = WGPUTextureFormat_ASTC10x10Unorm,
	ASTC10x10UnormSrgb = WGPUTextureFormat_ASTC10x10UnormSrgb,
	ASTC12x10Unorm = WGPUTextureFormat_ASTC12x10Unorm,
	ASTC12x10UnormSrgb = WGPUTextureFormat_ASTC12x10UnormSrgb,
	ASTC12x12Unorm = WGPUTextureFormat_ASTC12x12Unorm,
	ASTC12x12UnormSrgb = WGPUTextureFormat_ASTC12x12UnormSrgb,
	Force32 = WGPUTextureFormat_Force32,
};

inline constexpr bool operator==(TextureFormat a, WGPUTextureFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUTextureFormat a, TextureFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(TextureFormat a, WGPUTextureFormat b) { return !(a == b); }
inline constexpr bool operator!=(WGPUTextureFormat a, TextureFormat b) { return !(a == b); }

inline constexpr TextureFormat operator|(TextureFormat a, TextureFormat b) { return static_cast<TextureFormat>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr TextureFormat operator&(TextureFormat a, TextureFormat b) { return static_cast<TextureFormat>(static_cast<int>(a) & static_cast<int>(b)); }

enum class TextureSampleType: int {
	BindingNotUsed = WGPUTextureSampleType_BindingNotUsed,
	Undefined = WGPUTextureSampleType_Undefined,
	Float = WGPUTextureSampleType_Float,
	UnfilterableFloat = WGPUTextureSampleType_UnfilterableFloat,
	Depth = WGPUTextureSampleType_Depth,
	Sint = WGPUTextureSampleType_Sint,
	Uint = WGPUTextureSampleType_Uint,
	Force32 = WGPUTextureSampleType_Force32,
};

inline constexpr bool operator==(TextureSampleType a, WGPUTextureSampleType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUTextureSampleType a, TextureSampleType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(TextureSampleType a, WGPUTextureSampleType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUTextureSampleType a, TextureSampleType b) { return !(a == b); }

inline constexpr TextureSampleType operator|(TextureSampleType a, TextureSampleType b) { return static_cast<TextureSampleType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr TextureSampleType operator&(TextureSampleType a, TextureSampleType b) { return static_cast<TextureSampleType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class TextureViewDimension: int {
	Undefined = WGPUTextureViewDimension_Undefined,
	_1D = WGPUTextureViewDimension_1D,
	_2D = WGPUTextureViewDimension_2D,
	_2DArray = WGPUTextureViewDimension_2DArray,
	Cube = WGPUTextureViewDimension_Cube,
	CubeArray = WGPUTextureViewDimension_CubeArray,
	_3D = WGPUTextureViewDimension_3D,
	Force32 = WGPUTextureViewDimension_Force32,
};

inline constexpr bool operator==(TextureViewDimension a, WGPUTextureViewDimension b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUTextureViewDimension a, TextureViewDimension b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(TextureViewDimension a, WGPUTextureViewDimension b) { return !(a == b); }
inline constexpr bool operator!=(WGPUTextureViewDimension a, TextureViewDimension b) { return !(a == b); }

inline constexpr TextureViewDimension operator|(TextureViewDimension a, TextureViewDimension b) { return static_cast<TextureViewDimension>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr TextureViewDimension operator&(TextureViewDimension a, TextureViewDimension b) { return static_cast<TextureViewDimension>(static_cast<int>(a) & static_cast<int>(b)); }

enum class VertexFormat: int {
	Uint8 = WGPUVertexFormat_Uint8,
	Uint8x2 = WGPUVertexFormat_Uint8x2,
	Uint8x4 = WGPUVertexFormat_Uint8x4,
	Sint8 = WGPUVertexFormat_Sint8,
	Sint8x2 = WGPUVertexFormat_Sint8x2,
	Sint8x4 = WGPUVertexFormat_Sint8x4,
	Unorm8 = WGPUVertexFormat_Unorm8,
	Unorm8x2 = WGPUVertexFormat_Unorm8x2,
	Unorm8x4 = WGPUVertexFormat_Unorm8x4,
	Snorm8 = WGPUVertexFormat_Snorm8,
	Snorm8x2 = WGPUVertexFormat_Snorm8x2,
	Snorm8x4 = WGPUVertexFormat_Snorm8x4,
	Uint16 = WGPUVertexFormat_Uint16,
	Uint16x2 = WGPUVertexFormat_Uint16x2,
	Uint16x4 = WGPUVertexFormat_Uint16x4,
	Sint16 = WGPUVertexFormat_Sint16,
	Sint16x2 = WGPUVertexFormat_Sint16x2,
	Sint16x4 = WGPUVertexFormat_Sint16x4,
	Unorm16 = WGPUVertexFormat_Unorm16,
	Unorm16x2 = WGPUVertexFormat_Unorm16x2,
	Unorm16x4 = WGPUVertexFormat_Unorm16x4,
	Snorm16 = WGPUVertexFormat_Snorm16,
	Snorm16x2 = WGPUVertexFormat_Snorm16x2,
	Snorm16x4 = WGPUVertexFormat_Snorm16x4,
	Float16 = WGPUVertexFormat_Float16,
	Float16x2 = WGPUVertexFormat_Float16x2,
	Float16x4 = WGPUVertexFormat_Float16x4,
	Float32 = WGPUVertexFormat_Float32,
	Float32x2 = WGPUVertexFormat_Float32x2,
	Float32x3 = WGPUVertexFormat_Float32x3,
	Float32x4 = WGPUVertexFormat_Float32x4,
	Uint32 = WGPUVertexFormat_Uint32,
	Uint32x2 = WGPUVertexFormat_Uint32x2,
	Uint32x3 = WGPUVertexFormat_Uint32x3,
	Uint32x4 = WGPUVertexFormat_Uint32x4,
	Sint32 = WGPUVertexFormat_Sint32,
	Sint32x2 = WGPUVertexFormat_Sint32x2,
	Sint32x3 = WGPUVertexFormat_Sint32x3,
	Sint32x4 = WGPUVertexFormat_Sint32x4,
	Unorm10_10_10_2 = WGPUVertexFormat_Unorm10_10_10_2,
	Unorm8x4BGRA = WGPUVertexFormat_Unorm8x4BGRA,
	Force32 = WGPUVertexFormat_Force32,
};

inline constexpr bool operator==(VertexFormat a, WGPUVertexFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUVertexFormat a, VertexFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(VertexFormat a, WGPUVertexFormat b) { return !(a == b); }
inline constexpr bool operator!=(WGPUVertexFormat a, VertexFormat b) { return !(a == b); }

inline constexpr VertexFormat operator|(VertexFormat a, VertexFormat b) { return static_cast<VertexFormat>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr VertexFormat operator&(VertexFormat a, VertexFormat b) { return static_cast<VertexFormat>(static_cast<int>(a) & static_cast<int>(b)); }

enum class VertexStepMode: int {
	VertexBufferNotUsed = WGPUVertexStepMode_VertexBufferNotUsed,
	Undefined = WGPUVertexStepMode_Undefined,
	Vertex = WGPUVertexStepMode_Vertex,
	Instance = WGPUVertexStepMode_Instance,
	Force32 = WGPUVertexStepMode_Force32,
};

inline constexpr bool operator==(VertexStepMode a, WGPUVertexStepMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUVertexStepMode a, VertexStepMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(VertexStepMode a, WGPUVertexStepMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUVertexStepMode a, VertexStepMode b) { return !(a == b); }

inline constexpr VertexStepMode operator|(VertexStepMode a, VertexStepMode b) { return static_cast<VertexStepMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr VertexStepMode operator&(VertexStepMode a, VertexStepMode b) { return static_cast<VertexStepMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class WGSLLanguageFeatureName: int {
	ReadonlyAndReadwriteStorageTextures = WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures,
	Packed4x8IntegerDotProduct = WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct,
	UnrestrictedPointerParameters = WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters,
	PointerCompositeAccess = WGPUWGSLLanguageFeatureName_PointerCompositeAccess,
	Force32 = WGPUWGSLLanguageFeatureName_Force32,
};

inline constexpr bool operator==(WGSLLanguageFeatureName a, WGPUWGSLLanguageFeatureName b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUWGSLLanguageFeatureName a, WGSLLanguageFeatureName b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(WGSLLanguageFeatureName a, WGPUWGSLLanguageFeatureName b) { return !(a == b); }
inline constexpr bool operator!=(WGPUWGSLLanguageFeatureName a, WGSLLanguageFeatureName b) { return !(a == b); }

inline constexpr WGSLLanguageFeatureName operator|(WGSLLanguageFeatureName a, WGSLLanguageFeatureName b) { return static_cast<WGSLLanguageFeatureName>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr WGSLLanguageFeatureName operator&(WGSLLanguageFeatureName a, WGSLLanguageFeatureName b) { return static_cast<WGSLLanguageFeatureName>(static_cast<int>(a) & static_cast<int>(b)); }

enum class WaitStatus: int {
	Success = WGPUWaitStatus_Success,
	TimedOut = WGPUWaitStatus_TimedOut,
	UnsupportedTimeout = WGPUWaitStatus_UnsupportedTimeout,
	UnsupportedCount = WGPUWaitStatus_UnsupportedCount,
	UnsupportedMixedSources = WGPUWaitStatus_UnsupportedMixedSources,
	Force32 = WGPUWaitStatus_Force32,
};

inline constexpr bool operator==(WaitStatus a, WGPUWaitStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUWaitStatus a, WaitStatus b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(WaitStatus a, WGPUWaitStatus b) { return !(a == b); }
inline constexpr bool operator!=(WGPUWaitStatus a, WaitStatus b) { return !(a == b); }

inline constexpr WaitStatus operator|(WaitStatus a, WaitStatus b) { return static_cast<WaitStatus>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr WaitStatus operator&(WaitStatus a, WaitStatus b) { return static_cast<WaitStatus>(static_cast<int>(a) & static_cast<int>(b)); }

enum class BufferUsage: int {
	None = WGPUBufferUsage_None,
	MapRead = WGPUBufferUsage_MapRead,
	MapWrite = WGPUBufferUsage_MapWrite,
	CopySrc = WGPUBufferUsage_CopySrc,
	CopyDst = WGPUBufferUsage_CopyDst,
	Index = WGPUBufferUsage_Index,
	Vertex = WGPUBufferUsage_Vertex,
	Uniform = WGPUBufferUsage_Uniform,
	Storage = WGPUBufferUsage_Storage,
	Indirect = WGPUBufferUsage_Indirect,
	QueryResolve = WGPUBufferUsage_QueryResolve,
};

inline constexpr bool operator==(BufferUsage a, WGPUBufferUsage b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUBufferUsage a, BufferUsage b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(BufferUsage a, WGPUBufferUsage b) { return !(a == b); }
inline constexpr bool operator!=(WGPUBufferUsage a, BufferUsage b) { return !(a == b); }

inline constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr BufferUsage operator&(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(static_cast<int>(a) & static_cast<int>(b)); }

enum class ColorWriteMask: int {
	None = WGPUColorWriteMask_None,
	Red = WGPUColorWriteMask_Red,
	Green = WGPUColorWriteMask_Green,
	Blue = WGPUColorWriteMask_Blue,
	Alpha = WGPUColorWriteMask_Alpha,
	All = WGPUColorWriteMask_All,
};

inline constexpr bool operator==(ColorWriteMask a, WGPUColorWriteMask b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUColorWriteMask a, ColorWriteMask b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(ColorWriteMask a, WGPUColorWriteMask b) { return !(a == b); }
inline constexpr bool operator!=(WGPUColorWriteMask a, ColorWriteMask b) { return !(a == b); }

inline constexpr ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b) { return static_cast<ColorWriteMask>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b) { return static_cast<ColorWriteMask>(static_cast<int>(a) & static_cast<int>(b)); }

enum class MapMode: int {
	None = WGPUMapMode_None,
	Read = WGPUMapMode_Read,
	Write = WGPUMapMode_Write,
};

inline constexpr bool operator==(MapMode a, WGPUMapMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUMapMode a, MapMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(MapMode a, WGPUMapMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUMapMode a, MapMode b) { return !(a == b); }

inline constexpr MapMode operator|(MapMode a, MapMode b) { return static_cast<MapMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr MapMode operator&(MapMode a, MapMode b) { return static_cast<MapMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class ShaderStage: int {
	None = WGPUShaderStage_None,
	Vertex = WGPUShaderStage_Vertex,
	Fragment = WGPUShaderStage_Fragment,
	Compute = WGPUShaderStage_Compute,
};

inline constexpr bool operator==(ShaderStage a, WGPUShaderStage b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUShaderStage a, ShaderStage b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(ShaderStage a, WGPUShaderStage b) { return !(a == b); }
inline constexpr bool operator!=(WGPUShaderStage a, ShaderStage b) { return !(a == b); }

inline constexpr ShaderStage operator|(ShaderStage a, ShaderStage b) { return static_cast<ShaderStage>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr ShaderStage operator&(ShaderStage a, ShaderStage b) { return static_cast<ShaderStage>(static_cast<int>(a) & static_cast<int>(b)); }

enum class TextureUsage: int {
	None = WGPUTextureUsage_None,
	CopySrc = WGPUTextureUsage_CopySrc,
	CopyDst = WGPUTextureUsage_CopyDst,
	TextureBinding = WGPUTextureUsage_TextureBinding,
	StorageBinding = WGPUTextureUsage_StorageBinding,
	RenderAttachment = WGPUTextureUsage_RenderAttachment,
};

inline constexpr bool operator==(TextureUsage a, WGPUTextureUsage b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUTextureUsage a, TextureUsage b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(TextureUsage a, WGPUTextureUsage b) { return !(a == b); }
inline constexpr bool operator!=(WGPUTextureUsage a, TextureUsage b) { return !(a == b); }

inline constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) { return static_cast<TextureUsage>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr TextureUsage operator&(TextureUsage a, TextureUsage b) { return static_cast<TextureUsage>(static_cast<int>(a) & static_cast<int>(b)); }

enum class NativeSType: int {
	DeviceExtras = WGPUSType_DeviceExtras,
	NativeLimits = WGPUSType_NativeLimits,
	PipelineLayoutExtras = WGPUSType_PipelineLayoutExtras,
	ShaderSourceGLSL = WGPUSType_ShaderSourceGLSL,
	InstanceExtras = WGPUSType_InstanceExtras,
	BindGroupEntryExtras = WGPUSType_BindGroupEntryExtras,
	BindGroupLayoutEntryExtras = WGPUSType_BindGroupLayoutEntryExtras,
	QuerySetDescriptorExtras = WGPUSType_QuerySetDescriptorExtras,
	SurfaceConfigurationExtras = WGPUSType_SurfaceConfigurationExtras,
	SurfaceSourceSwapChainPanel = WGPUSType_SurfaceSourceSwapChainPanel,
	PrimitiveStateExtras = WGPUSType_PrimitiveStateExtras,
	Force32 = WGPUNativeSType_Force32,
};

inline constexpr bool operator==(NativeSType a, WGPUNativeSType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUNativeSType a, NativeSType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(NativeSType a, WGPUNativeSType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUNativeSType a, NativeSType b) { return !(a == b); }

inline constexpr NativeSType operator|(NativeSType a, NativeSType b) { return static_cast<NativeSType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr NativeSType operator&(NativeSType a, NativeSType b) { return static_cast<NativeSType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class NativeFeature: int {
	PushConstants = WGPUNativeFeature_PushConstants,
	TextureAdapterSpecificFormatFeatures = WGPUNativeFeature_TextureAdapterSpecificFormatFeatures,
	MultiDrawIndirectCount = WGPUNativeFeature_MultiDrawIndirectCount,
	VertexWritableStorage = WGPUNativeFeature_VertexWritableStorage,
	TextureBindingArray = WGPUNativeFeature_TextureBindingArray,
	SampledTextureAndStorageBufferArrayNonUniformIndexing = WGPUNativeFeature_SampledTextureAndStorageBufferArrayNonUniformIndexing,
	PipelineStatisticsQuery = WGPUNativeFeature_PipelineStatisticsQuery,
	StorageResourceBindingArray = WGPUNativeFeature_StorageResourceBindingArray,
	PartiallyBoundBindingArray = WGPUNativeFeature_PartiallyBoundBindingArray,
	TextureFormat16bitNorm = WGPUNativeFeature_TextureFormat16bitNorm,
	TextureCompressionAstcHdr = WGPUNativeFeature_TextureCompressionAstcHdr,
	MappablePrimaryBuffers = WGPUNativeFeature_MappablePrimaryBuffers,
	BufferBindingArray = WGPUNativeFeature_BufferBindingArray,
	UniformBufferAndStorageTextureArrayNonUniformIndexing = WGPUNativeFeature_UniformBufferAndStorageTextureArrayNonUniformIndexing,
	PolygonModeLine = WGPUNativeFeature_PolygonModeLine,
	PolygonModePoint = WGPUNativeFeature_PolygonModePoint,
	ConservativeRasterization = WGPUNativeFeature_ConservativeRasterization,
	SpirvShaderPassthrough = WGPUNativeFeature_SpirvShaderPassthrough,
	VertexAttribute64bit = WGPUNativeFeature_VertexAttribute64bit,
	TextureFormatNv12 = WGPUNativeFeature_TextureFormatNv12,
	RayQuery = WGPUNativeFeature_RayQuery,
	ShaderF64 = WGPUNativeFeature_ShaderF64,
	ShaderI16 = WGPUNativeFeature_ShaderI16,
	ShaderPrimitiveIndex = WGPUNativeFeature_ShaderPrimitiveIndex,
	ShaderEarlyDepthTest = WGPUNativeFeature_ShaderEarlyDepthTest,
	Subgroup = WGPUNativeFeature_Subgroup,
	SubgroupVertex = WGPUNativeFeature_SubgroupVertex,
	SubgroupBarrier = WGPUNativeFeature_SubgroupBarrier,
	TimestampQueryInsideEncoders = WGPUNativeFeature_TimestampQueryInsideEncoders,
	TimestampQueryInsidePasses = WGPUNativeFeature_TimestampQueryInsidePasses,
	ShaderInt64 = WGPUNativeFeature_ShaderInt64,
	Force32 = WGPUNativeFeature_Force32,
};

inline constexpr bool operator==(NativeFeature a, WGPUNativeFeature b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUNativeFeature a, NativeFeature b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(NativeFeature a, WGPUNativeFeature b) { return !(a == b); }
inline constexpr bool operator!=(WGPUNativeFeature a, NativeFeature b) { return !(a == b); }

inline constexpr NativeFeature operator|(NativeFeature a, NativeFeature b) { return static_cast<NativeFeature>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr NativeFeature operator&(NativeFeature a, NativeFeature b) { return static_cast<NativeFeature>(static_cast<int>(a) & static_cast<int>(b)); }

enum class LogLevel: int {
	Off = WGPULogLevel_Off,
	Error = WGPULogLevel_Error,
	Warn = WGPULogLevel_Warn,
	Info = WGPULogLevel_Info,
	Debug = WGPULogLevel_Debug,
	Trace = WGPULogLevel_Trace,
	Force32 = WGPULogLevel_Force32,
};

inline constexpr bool operator==(LogLevel a, WGPULogLevel b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPULogLevel a, LogLevel b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(LogLevel a, WGPULogLevel b) { return !(a == b); }
inline constexpr bool operator!=(WGPULogLevel a, LogLevel b) { return !(a == b); }

inline constexpr LogLevel operator|(LogLevel a, LogLevel b) { return static_cast<LogLevel>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr LogLevel operator&(LogLevel a, LogLevel b) { return static_cast<LogLevel>(static_cast<int>(a) & static_cast<int>(b)); }

enum class InstanceBackend: int {
	All = WGPUInstanceBackend_All,
	Force32 = WGPUInstanceBackend_Force32,
};

inline constexpr bool operator==(InstanceBackend a, WGPUInstanceBackend b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUInstanceBackend a, InstanceBackend b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(InstanceBackend a, WGPUInstanceBackend b) { return !(a == b); }
inline constexpr bool operator!=(WGPUInstanceBackend a, InstanceBackend b) { return !(a == b); }

inline constexpr InstanceBackend operator|(InstanceBackend a, InstanceBackend b) { return static_cast<InstanceBackend>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr InstanceBackend operator&(InstanceBackend a, InstanceBackend b) { return static_cast<InstanceBackend>(static_cast<int>(a) & static_cast<int>(b)); }

enum class InstanceFlag: int {
	Default = WGPUInstanceFlag_Default,
	Force32 = WGPUInstanceFlag_Force32,
};

inline constexpr bool operator==(InstanceFlag a, WGPUInstanceFlag b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUInstanceFlag a, InstanceFlag b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(InstanceFlag a, WGPUInstanceFlag b) { return !(a == b); }
inline constexpr bool operator!=(WGPUInstanceFlag a, InstanceFlag b) { return !(a == b); }

inline constexpr InstanceFlag operator|(InstanceFlag a, InstanceFlag b) { return static_cast<InstanceFlag>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr InstanceFlag operator&(InstanceFlag a, InstanceFlag b) { return static_cast<InstanceFlag>(static_cast<int>(a) & static_cast<int>(b)); }

enum class Dx12Compiler: int {
	Undefined = WGPUDx12Compiler_Undefined,
	Fxc = WGPUDx12Compiler_Fxc,
	Dxc = WGPUDx12Compiler_Dxc,
	Force32 = WGPUDx12Compiler_Force32,
};

inline constexpr bool operator==(Dx12Compiler a, WGPUDx12Compiler b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUDx12Compiler a, Dx12Compiler b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(Dx12Compiler a, WGPUDx12Compiler b) { return !(a == b); }
inline constexpr bool operator!=(WGPUDx12Compiler a, Dx12Compiler b) { return !(a == b); }

inline constexpr Dx12Compiler operator|(Dx12Compiler a, Dx12Compiler b) { return static_cast<Dx12Compiler>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr Dx12Compiler operator&(Dx12Compiler a, Dx12Compiler b) { return static_cast<Dx12Compiler>(static_cast<int>(a) & static_cast<int>(b)); }

enum class Gles3MinorVersion: int {
	Automatic = WGPUGles3MinorVersion_Automatic,
	Version0 = WGPUGles3MinorVersion_Version0,
	Version1 = WGPUGles3MinorVersion_Version1,
	Version2 = WGPUGles3MinorVersion_Version2,
	Force32 = WGPUGles3MinorVersion_Force32,
};

inline constexpr bool operator==(Gles3MinorVersion a, WGPUGles3MinorVersion b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUGles3MinorVersion a, Gles3MinorVersion b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(Gles3MinorVersion a, WGPUGles3MinorVersion b) { return !(a == b); }
inline constexpr bool operator!=(WGPUGles3MinorVersion a, Gles3MinorVersion b) { return !(a == b); }

inline constexpr Gles3MinorVersion operator|(Gles3MinorVersion a, Gles3MinorVersion b) { return static_cast<Gles3MinorVersion>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr Gles3MinorVersion operator&(Gles3MinorVersion a, Gles3MinorVersion b) { return static_cast<Gles3MinorVersion>(static_cast<int>(a) & static_cast<int>(b)); }

enum class PipelineStatisticName: int {
	VertexShaderInvocations = WGPUPipelineStatisticName_VertexShaderInvocations,
	ClipperInvocations = WGPUPipelineStatisticName_ClipperInvocations,
	ClipperPrimitivesOut = WGPUPipelineStatisticName_ClipperPrimitivesOut,
	FragmentShaderInvocations = WGPUPipelineStatisticName_FragmentShaderInvocations,
	ComputeShaderInvocations = WGPUPipelineStatisticName_ComputeShaderInvocations,
	Force32 = WGPUPipelineStatisticName_Force32,
};

inline constexpr bool operator==(PipelineStatisticName a, WGPUPipelineStatisticName b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUPipelineStatisticName a, PipelineStatisticName b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(PipelineStatisticName a, WGPUPipelineStatisticName b) { return !(a == b); }
inline constexpr bool operator!=(WGPUPipelineStatisticName a, PipelineStatisticName b) { return !(a == b); }

inline constexpr PipelineStatisticName operator|(PipelineStatisticName a, PipelineStatisticName b) { return static_cast<PipelineStatisticName>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr PipelineStatisticName operator&(PipelineStatisticName a, PipelineStatisticName b) { return static_cast<PipelineStatisticName>(static_cast<int>(a) & static_cast<int>(b)); }

enum class NativeQueryType: int {
	PipelineStatistics = WGPUNativeQueryType_PipelineStatistics,
	Force32 = WGPUNativeQueryType_Force32,
};

inline constexpr bool operator==(NativeQueryType a, WGPUNativeQueryType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUNativeQueryType a, NativeQueryType b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(NativeQueryType a, WGPUNativeQueryType b) { return !(a == b); }
inline constexpr bool operator!=(WGPUNativeQueryType a, NativeQueryType b) { return !(a == b); }

inline constexpr NativeQueryType operator|(NativeQueryType a, NativeQueryType b) { return static_cast<NativeQueryType>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr NativeQueryType operator&(NativeQueryType a, NativeQueryType b) { return static_cast<NativeQueryType>(static_cast<int>(a) & static_cast<int>(b)); }

enum class DxcMaxShaderModel: int {
	V6_0 = WGPUDxcMaxShaderModel_V6_0,
	V6_1 = WGPUDxcMaxShaderModel_V6_1,
	V6_2 = WGPUDxcMaxShaderModel_V6_2,
	V6_3 = WGPUDxcMaxShaderModel_V6_3,
	V6_4 = WGPUDxcMaxShaderModel_V6_4,
	V6_5 = WGPUDxcMaxShaderModel_V6_5,
	V6_6 = WGPUDxcMaxShaderModel_V6_6,
	V6_7 = WGPUDxcMaxShaderModel_V6_7,
	Force32 = WGPUDxcMaxShaderModel_Force32,
};

inline constexpr bool operator==(DxcMaxShaderModel a, WGPUDxcMaxShaderModel b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUDxcMaxShaderModel a, DxcMaxShaderModel b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(DxcMaxShaderModel a, WGPUDxcMaxShaderModel b) { return !(a == b); }
inline constexpr bool operator!=(WGPUDxcMaxShaderModel a, DxcMaxShaderModel b) { return !(a == b); }

inline constexpr DxcMaxShaderModel operator|(DxcMaxShaderModel a, DxcMaxShaderModel b) { return static_cast<DxcMaxShaderModel>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr DxcMaxShaderModel operator&(DxcMaxShaderModel a, DxcMaxShaderModel b) { return static_cast<DxcMaxShaderModel>(static_cast<int>(a) & static_cast<int>(b)); }

enum class GLFenceBehaviour: int {
	Normal = WGPUGLFenceBehaviour_Normal,
	AutoFinish = WGPUGLFenceBehaviour_AutoFinish,
	Force32 = WGPUGLFenceBehaviour_Force32,
};

inline constexpr bool operator==(GLFenceBehaviour a, WGPUGLFenceBehaviour b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUGLFenceBehaviour a, GLFenceBehaviour b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(GLFenceBehaviour a, WGPUGLFenceBehaviour b) { return !(a == b); }
inline constexpr bool operator!=(WGPUGLFenceBehaviour a, GLFenceBehaviour b) { return !(a == b); }

inline constexpr GLFenceBehaviour operator|(GLFenceBehaviour a, GLFenceBehaviour b) { return static_cast<GLFenceBehaviour>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr GLFenceBehaviour operator&(GLFenceBehaviour a, GLFenceBehaviour b) { return static_cast<GLFenceBehaviour>(static_cast<int>(a) & static_cast<int>(b)); }

enum class Dx12SwapchainKind: int {
	Undefined = WGPUDx12SwapchainKind_Undefined,
	DxgiFromHwnd = WGPUDx12SwapchainKind_DxgiFromHwnd,
	DxgiFromVisual = WGPUDx12SwapchainKind_DxgiFromVisual,
	Force32 = WGPUDx12SwapchainKind_Force32,
};

inline constexpr bool operator==(Dx12SwapchainKind a, WGPUDx12SwapchainKind b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUDx12SwapchainKind a, Dx12SwapchainKind b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(Dx12SwapchainKind a, WGPUDx12SwapchainKind b) { return !(a == b); }
inline constexpr bool operator!=(WGPUDx12SwapchainKind a, Dx12SwapchainKind b) { return !(a == b); }

inline constexpr Dx12SwapchainKind operator|(Dx12SwapchainKind a, Dx12SwapchainKind b) { return static_cast<Dx12SwapchainKind>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr Dx12SwapchainKind operator&(Dx12SwapchainKind a, Dx12SwapchainKind b) { return static_cast<Dx12SwapchainKind>(static_cast<int>(a) & static_cast<int>(b)); }

enum class PolygonMode: int {
	Fill = WGPUPolygonMode_Fill,
	Line = WGPUPolygonMode_Line,
	Point = WGPUPolygonMode_Point,
};

inline constexpr bool operator==(PolygonMode a, WGPUPolygonMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUPolygonMode a, PolygonMode b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(PolygonMode a, WGPUPolygonMode b) { return !(a == b); }
inline constexpr bool operator!=(WGPUPolygonMode a, PolygonMode b) { return !(a == b); }

inline constexpr PolygonMode operator|(PolygonMode a, PolygonMode b) { return static_cast<PolygonMode>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr PolygonMode operator&(PolygonMode a, PolygonMode b) { return static_cast<PolygonMode>(static_cast<int>(a) & static_cast<int>(b)); }

enum class NativeTextureFormat: int {
	R16Unorm = WGPUNativeTextureFormat_R16Unorm,
	R16Snorm = WGPUNativeTextureFormat_R16Snorm,
	Rg16Unorm = WGPUNativeTextureFormat_Rg16Unorm,
	Rg16Snorm = WGPUNativeTextureFormat_Rg16Snorm,
	Rgba16Unorm = WGPUNativeTextureFormat_Rgba16Unorm,
	Rgba16Snorm = WGPUNativeTextureFormat_Rgba16Snorm,
	NV12 = WGPUNativeTextureFormat_NV12,
	P010 = WGPUNativeTextureFormat_P010,
};

inline constexpr bool operator==(NativeTextureFormat a, WGPUNativeTextureFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator==(WGPUNativeTextureFormat a, NativeTextureFormat b) { return static_cast<int>(a) == static_cast<int>(b); }
inline constexpr bool operator!=(NativeTextureFormat a, WGPUNativeTextureFormat b) { return !(a == b); }
inline constexpr bool operator!=(WGPUNativeTextureFormat a, NativeTextureFormat b) { return !(a == b); }

inline constexpr NativeTextureFormat operator|(NativeTextureFormat a, NativeTextureFormat b) { return static_cast<NativeTextureFormat>(static_cast<int>(a) | static_cast<int>(b)); }
inline constexpr NativeTextureFormat operator&(NativeTextureFormat a, NativeTextureFormat b) { return static_cast<NativeTextureFormat>(static_cast<int>(a) & static_cast<int>(b)); }

} // namespace wgpu

namespace wgpu {
std::string_view to_string(AdapterType v);
std::string_view to_string(AddressMode v);
std::string_view to_string(BackendType v);
std::string_view to_string(BlendFactor v);
std::string_view to_string(BlendOperation v);
std::string_view to_string(BufferBindingType v);
std::string_view to_string(BufferMapState v);
std::string_view to_string(CallbackMode v);
std::string_view to_string(CompareFunction v);
std::string_view to_string(CompilationInfoRequestStatus v);
std::string_view to_string(CompilationMessageType v);
std::string_view to_string(CompositeAlphaMode v);
std::string_view to_string(CreatePipelineAsyncStatus v);
std::string_view to_string(CullMode v);
std::string_view to_string(DeviceLostReason v);
std::string_view to_string(ErrorFilter v);
std::string_view to_string(ErrorType v);
std::string_view to_string(FeatureLevel v);
std::string_view to_string(FeatureName v);
std::string_view to_string(FilterMode v);
std::string_view to_string(FrontFace v);
std::string_view to_string(IndexFormat v);
std::string_view to_string(LoadOp v);
std::string_view to_string(MapAsyncStatus v);
std::string_view to_string(MipmapFilterMode v);
std::string_view to_string(OptionalBool v);
std::string_view to_string(PopErrorScopeStatus v);
std::string_view to_string(PowerPreference v);
std::string_view to_string(PresentMode v);
std::string_view to_string(PrimitiveTopology v);
std::string_view to_string(QueryType v);
std::string_view to_string(QueueWorkDoneStatus v);
std::string_view to_string(RequestAdapterStatus v);
std::string_view to_string(RequestDeviceStatus v);
std::string_view to_string(SType v);
std::string_view to_string(SamplerBindingType v);
std::string_view to_string(Status v);
std::string_view to_string(StencilOperation v);
std::string_view to_string(StorageTextureAccess v);
std::string_view to_string(StoreOp v);
std::string_view to_string(SurfaceGetCurrentTextureStatus v);
std::string_view to_string(TextureAspect v);
std::string_view to_string(TextureDimension v);
std::string_view to_string(TextureFormat v);
std::string_view to_string(TextureSampleType v);
std::string_view to_string(TextureViewDimension v);
std::string_view to_string(VertexFormat v);
std::string_view to_string(VertexStepMode v);
std::string_view to_string(WGSLLanguageFeatureName v);
std::string_view to_string(WaitStatus v);
std::string_view to_string(BufferUsage v);
std::string_view to_string(ColorWriteMask v);
std::string_view to_string(MapMode v);
std::string_view to_string(ShaderStage v);
std::string_view to_string(TextureUsage v);
std::string_view to_string(NativeSType v);
std::string_view to_string(NativeFeature v);
std::string_view to_string(LogLevel v);
std::string_view to_string(InstanceBackend v);
std::string_view to_string(InstanceFlag v);
std::string_view to_string(Dx12Compiler v);
std::string_view to_string(Gles3MinorVersion v);
std::string_view to_string(PipelineStatisticName v);
std::string_view to_string(NativeQueryType v);
std::string_view to_string(DxcMaxShaderModel v);
std::string_view to_string(GLFenceBehaviour v);
std::string_view to_string(Dx12SwapchainKind v);
std::string_view to_string(PolygonMode v);
std::string_view to_string(NativeTextureFormat v);
} // namespace wgpu


// Forward declarations
namespace wgpu {
struct StringView;
struct ChainedStruct;
struct ChainedStructOut;
struct BlendComponent;
struct Color;
struct ComputePassTimestampWrites;
struct Extent3D;
struct Future;
struct Origin3D;
struct RenderPassDepthStencilAttachment;
struct RenderPassMaxDrawCount;
struct RenderPassTimestampWrites;
struct ShaderSourceSPIRV;
struct ShaderSourceWGSL;
struct StencilFaceState;
struct SupportedFeatures;
struct SupportedWGSLLanguageFeatures;
struct SurfaceSourceAndroidNativeWindow;
struct SurfaceSourceMetalLayer;
struct SurfaceSourceWaylandSurface;
struct SurfaceSourceWindowsHWND;
struct SurfaceSourceXCBWindow;
struct SurfaceSourceXlibWindow;
struct TexelCopyBufferLayout;
struct VertexAttribute;
struct BlendState;
struct FutureWaitInfo;
struct TexelCopyBufferInfo;
struct TexelCopyTextureInfo;
struct VertexBufferLayout;
struct InstanceExtras;
struct DeviceExtras;
struct NativeLimits;
struct PushConstantRange;
struct PipelineLayoutExtras;
struct ShaderDefine;
struct ShaderSourceGLSL;
struct ShaderModuleDescriptorSpirV;
struct RegistryReport;
struct HubReport;
struct GlobalReport;
struct BindGroupEntryExtras;
struct BindGroupLayoutEntryExtras;
struct QuerySetDescriptorExtras;
struct SurfaceConfigurationExtras;
struct SurfaceSourceSwapChainPanel;
struct PrimitiveStateExtras;
} // namespace wgpu

namespace wgpu {
struct BufferMapCallbackInfo;
struct CompilationInfoCallbackInfo;
struct CreateComputePipelineAsyncCallbackInfo;
struct CreateRenderPipelineAsyncCallbackInfo;
struct DeviceLostCallbackInfo;
struct PopErrorScopeCallbackInfo;
struct QueueWorkDoneCallbackInfo;
struct RequestAdapterCallbackInfo;
struct RequestDeviceCallbackInfo;
struct UncapturedErrorCallbackInfo;
struct AdapterInfo;
struct BindGroupEntry;
struct BufferBindingLayout;
struct BufferDescriptor;
struct CommandBufferDescriptor;
struct CommandEncoderDescriptor;
struct CompilationMessage;
struct ConstantEntry;
struct InstanceCapabilities;
struct Limits;
struct MultisampleState;
struct PipelineLayoutDescriptor;
struct PrimitiveState;
struct QuerySetDescriptor;
struct QueueDescriptor;
struct RenderBundleDescriptor;
struct RenderBundleEncoderDescriptor;
struct RequestAdapterOptions;
struct SamplerBindingLayout;
struct SamplerDescriptor;
struct ShaderModuleDescriptor;
struct StorageTextureBindingLayout;
struct SurfaceCapabilities;
struct SurfaceConfiguration;
struct SurfaceDescriptor;
struct SurfaceTexture;
struct TextureBindingLayout;
struct TextureViewDescriptor;
struct BindGroupDescriptor;
struct BindGroupLayoutEntry;
struct CompilationInfo;
struct ComputePassDescriptor;
struct DepthStencilState;
struct DeviceDescriptor;
struct InstanceDescriptor;
struct ProgrammableStageDescriptor;
struct RenderPassColorAttachment;
struct TextureDescriptor;
struct BindGroupLayoutDescriptor;
struct ColorTargetState;
struct ComputePipelineDescriptor;
struct RenderPassDescriptor;
struct VertexState;
struct FragmentState;
struct RenderPipelineDescriptor;
struct InstanceEnumerateAdapterOptions;
} // namespace wgpu

namespace wgpu {
class Adapter;
class BindGroup;
class BindGroupLayout;
class Buffer;
class CommandBuffer;
class CommandEncoder;
class ComputePassEncoder;
class ComputePipeline;
class Device;
class Instance;
class PipelineLayout;
class QuerySet;
class Queue;
class RenderBundle;
class RenderBundleEncoder;
class RenderPassEncoder;
class RenderPipeline;
class Sampler;
class ShaderModule;
class Surface;
class Texture;
class TextureView;
namespace raw {
class Adapter;
class BindGroup;
class BindGroupLayout;
class Buffer;
class CommandBuffer;
class CommandEncoder;
class ComputePassEncoder;
class ComputePipeline;
class Device;
class Instance;
class PipelineLayout;
class QuerySet;
class Queue;
class RenderBundle;
class RenderBundleEncoder;
class RenderPassEncoder;
class RenderPipeline;
class Sampler;
class ShaderModule;
class Surface;
class Texture;
class TextureView;
}

} // namespace wgpu


// Handles
namespace wgpu {
namespace raw {
HANDLE(Adapter)
	void getFeatures(SupportedFeatures& features) const;
	Status getInfo(AdapterInfo& info) const;
	Status getLimits(Limits& limits) const;
	Bool hasFeature(FeatureName feature) const;
	template<std::invocable<RequestDeviceStatus, raw::Device, StringView> Lambda>
	Future requestDevice(const DeviceDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const;
	void addRef() const;
	void release() const;
	Device requestDevice(const DeviceDescriptor& descriptor);
END

HANDLE(BindGroup)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(BindGroupLayout)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Buffer)
	void destroy() const;
	void const * getConstMappedRange(size_t offset, size_t size) const;
	BufferMapState getMapState() const;
	void * getMappedRange(size_t offset, size_t size) const;
	uint64_t getSize() const;
	BufferUsage getUsage() const;
	template<std::invocable<MapAsyncStatus, StringView> Lambda>
	Future mapAsync(MapMode mode, size_t offset, size_t size, CallbackMode callbackMode, const Lambda& callback) const;
	void setLabel(StringView label) const;
	void unmap() const;
	void addRef() const;
	void release() const;
END

HANDLE(CommandBuffer)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(CommandEncoder)
	raw::ComputePassEncoder beginComputePass(const ComputePassDescriptor& descriptor) const;
	raw::RenderPassEncoder beginRenderPass(const RenderPassDescriptor& descriptor) const;
	void clearBuffer(raw::Buffer buffer, uint64_t offset, uint64_t size) const;
	void copyBufferToBuffer(raw::Buffer source, uint64_t sourceOffset, raw::Buffer destination, uint64_t destinationOffset, uint64_t size) const;
	void copyBufferToTexture(const TexelCopyBufferInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const;
	void copyTextureToBuffer(const TexelCopyTextureInfo& source, const TexelCopyBufferInfo& destination, const Extent3D& copySize) const;
	void copyTextureToTexture(const TexelCopyTextureInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const;
	raw::CommandBuffer finish(const CommandBufferDescriptor& descriptor) const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void resolveQuerySet(raw::QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, raw::Buffer destination, uint64_t destinationOffset) const;
	void setLabel(StringView label) const;
	void writeTimestamp(raw::QuerySet querySet, uint32_t queryIndex) const;
	void addRef() const;
	void release() const;
END

HANDLE(ComputePassEncoder)
	void dispatchWorkgroups(uint32_t workgroupCountX, uint32_t workgroupCountY, uint32_t workgroupCountZ) const;
	void dispatchWorkgroupsIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const;
	void end() const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::span<const uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const uint32_t& dynamicOffsets) const;
	void setLabel(StringView label) const;
	void setPipeline(raw::ComputePipeline pipeline) const;
	void addRef() const;
	void release() const;
	void setPushConstants(uint32_t offset, uint32_t sizeBytes, void const * data) const;
	void beginPipelineStatisticsQuery(raw::QuerySet querySet, uint32_t queryIndex) const;
	void endPipelineStatisticsQuery() const;
	void writeTimestamp(raw::QuerySet querySet, uint32_t queryIndex) const;
END

HANDLE(ComputePipeline)
	raw::BindGroupLayout getBindGroupLayout(uint32_t groupIndex) const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Device)
	raw::BindGroup createBindGroup(const BindGroupDescriptor& descriptor) const;
	raw::BindGroupLayout createBindGroupLayout(const BindGroupLayoutDescriptor& descriptor) const;
	raw::Buffer createBuffer(const BufferDescriptor& descriptor) const;
	raw::CommandEncoder createCommandEncoder(const CommandEncoderDescriptor& descriptor) const;
	raw::ComputePipeline createComputePipeline(const ComputePipelineDescriptor& descriptor) const;
	template<std::invocable<CreatePipelineAsyncStatus, raw::ComputePipeline, StringView> Lambda>
	Future createComputePipelineAsync(const ComputePipelineDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const;
	raw::PipelineLayout createPipelineLayout(const PipelineLayoutDescriptor& descriptor) const;
	raw::QuerySet createQuerySet(const QuerySetDescriptor& descriptor) const;
	raw::RenderBundleEncoder createRenderBundleEncoder(const RenderBundleEncoderDescriptor& descriptor) const;
	raw::RenderPipeline createRenderPipeline(const RenderPipelineDescriptor& descriptor) const;
	template<std::invocable<CreatePipelineAsyncStatus, raw::RenderPipeline, StringView> Lambda>
	Future createRenderPipelineAsync(const RenderPipelineDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const;
	raw::Sampler createSampler(const SamplerDescriptor& descriptor) const;
	raw::ShaderModule createShaderModule(const ShaderModuleDescriptor& descriptor) const;
	raw::Texture createTexture(const TextureDescriptor& descriptor) const;
	void destroy() const;
	AdapterInfo getAdapterInfo() const;
	void getFeatures(SupportedFeatures& features) const;
	Status getLimits(Limits& limits) const;
	raw::Queue getQueue() const;
	Bool hasFeature(FeatureName feature) const;
	template<std::invocable<PopErrorScopeStatus, ErrorType, StringView> Lambda>
	Future popErrorScope(CallbackMode callbackMode, const Lambda& callback) const;
	void pushErrorScope(ErrorFilter filter) const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
	Bool poll(Bool wait, SubmissionIndex const * submissionIndex) const;
	raw::ShaderModule createShaderModuleSpirV(const ShaderModuleDescriptorSpirV& descriptor) const;
END

HANDLE(Instance)
	raw::Surface createSurface(const SurfaceDescriptor& descriptor) const;
	Status getWGSLLanguageFeatures(SupportedWGSLLanguageFeatures& features) const;
	Bool hasWGSLLanguageFeature(WGSLLanguageFeatureName feature) const;
	void processEvents() const;
	template<std::invocable<RequestAdapterStatus, raw::Adapter, StringView> Lambda>
	Future requestAdapter(const RequestAdapterOptions& options, CallbackMode callbackMode, const Lambda& callback) const;
	WaitStatus waitAny(size_t futureCount, FutureWaitInfo& futures, uint64_t timeoutNS) const;
	void addRef() const;
	void release() const;
	size_t enumerateAdapters(const InstanceEnumerateAdapterOptions& options, Adapter * adapters) const;
	Adapter requestAdapter(const RequestAdapterOptions& options);
END

HANDLE(PipelineLayout)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(QuerySet)
	void destroy() const;
	uint32_t getCount() const;
	QueryType getType() const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Queue)
	template<std::invocable<QueueWorkDoneStatus> Lambda>
	Future onSubmittedWorkDone(CallbackMode callbackMode, const Lambda& callback) const;
	void setLabel(StringView label) const;
	void submit(size_t commandCount, CommandBuffer const * commands) const;
	void submit(const std::vector<CommandBuffer>& commands) const;
	void submit(const std::span<const CommandBuffer>& commands) const;
	void submit(const CommandBuffer& commands) const;
	void submit(const std::vector<wgpu::CommandBuffer>& commands) const;
	void submit(const std::span<const wgpu::CommandBuffer>& commands) const;
	void submit(const wgpu::CommandBuffer& commands) const;
	void writeBuffer(raw::Buffer buffer, uint64_t bufferOffset, void const * data, size_t size) const;
	void writeTexture(const TexelCopyTextureInfo& destination, void const * data, size_t dataSize, const TexelCopyBufferLayout& dataLayout, const Extent3D& writeSize) const;
	void addRef() const;
	void release() const;
	SubmissionIndex submitForIndex(size_t commandCount, CommandBuffer const * commands) const;
	SubmissionIndex submitForIndex(const std::vector<CommandBuffer>& commands) const;
	SubmissionIndex submitForIndex(const std::span<const CommandBuffer>& commands) const;
	SubmissionIndex submitForIndex(const CommandBuffer& commands) const;
	SubmissionIndex submitForIndex(const std::vector<wgpu::CommandBuffer>& commands) const;
	SubmissionIndex submitForIndex(const std::span<const wgpu::CommandBuffer>& commands) const;
	SubmissionIndex submitForIndex(const wgpu::CommandBuffer& commands) const;
	float getTimestampPeriod() const;
END

HANDLE(RenderBundle)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(RenderBundleEncoder)
	void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const;
	void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) const;
	void drawIndexedIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const;
	void drawIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const;
	raw::RenderBundle finish(const RenderBundleDescriptor& descriptor) const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::span<const uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const uint32_t& dynamicOffsets) const;
	void setIndexBuffer(raw::Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const;
	void setLabel(StringView label) const;
	void setPipeline(raw::RenderPipeline pipeline) const;
	void setVertexBuffer(uint32_t slot, raw::Buffer buffer, uint64_t offset, uint64_t size) const;
	void addRef() const;
	void release() const;
	void setPushConstants(ShaderStage stages, uint32_t offset, uint32_t sizeBytes, void const * data) const;
END

HANDLE(RenderPassEncoder)
	void beginOcclusionQuery(uint32_t queryIndex) const;
	void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const;
	void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) const;
	void drawIndexedIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const;
	void drawIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const;
	void end() const;
	void endOcclusionQuery() const;
	void executeBundles(size_t bundleCount, RenderBundle const * bundles) const;
	void executeBundles(const std::vector<RenderBundle>& bundles) const;
	void executeBundles(const std::span<const RenderBundle>& bundles) const;
	void executeBundles(const RenderBundle& bundles) const;
	void executeBundles(const std::vector<wgpu::RenderBundle>& bundles) const;
	void executeBundles(const std::span<const wgpu::RenderBundle>& bundles) const;
	void executeBundles(const wgpu::RenderBundle& bundles) const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::span<const uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, raw::BindGroup group, const uint32_t& dynamicOffsets) const;
	void setBlendConstant(const Color& color) const;
	void setIndexBuffer(raw::Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const;
	void setLabel(StringView label) const;
	void setPipeline(raw::RenderPipeline pipeline) const;
	void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const;
	void setStencilReference(uint32_t reference) const;
	void setVertexBuffer(uint32_t slot, raw::Buffer buffer, uint64_t offset, uint64_t size) const;
	void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) const;
	void addRef() const;
	void release() const;
	void setPushConstants(ShaderStage stages, uint32_t offset, uint32_t sizeBytes, void const * data) const;
	void multiDrawIndirect(raw::Buffer buffer, uint64_t offset, uint32_t count) const;
	void multiDrawIndexedIndirect(raw::Buffer buffer, uint64_t offset, uint32_t count) const;
	void multiDrawIndirectCount(raw::Buffer buffer, uint64_t offset, raw::Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const;
	void multiDrawIndexedIndirectCount(raw::Buffer buffer, uint64_t offset, raw::Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const;
	void beginPipelineStatisticsQuery(raw::QuerySet querySet, uint32_t queryIndex) const;
	void endPipelineStatisticsQuery() const;
	void writeTimestamp(raw::QuerySet querySet, uint32_t queryIndex) const;
END

HANDLE(RenderPipeline)
	raw::BindGroupLayout getBindGroupLayout(uint32_t groupIndex) const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Sampler)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(ShaderModule)
	template<std::invocable<CompilationInfoRequestStatus, const CompilationInfo&> Lambda>
	Future getCompilationInfo(CallbackMode callbackMode, const Lambda& callback) const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Surface)
	void configure(const SurfaceConfiguration& config) const;
	Status getCapabilities(raw::Adapter adapter, SurfaceCapabilities& capabilities) const;
	void getCurrentTexture(SurfaceTexture& surfaceTexture) const;
	Status present() const;
	void setLabel(StringView label) const;
	void unconfigure() const;
	void addRef() const;
	void release() const;
END

HANDLE(Texture)
	raw::TextureView createView(const TextureViewDescriptor& descriptor) const;
	void destroy() const;
	uint32_t getDepthOrArrayLayers() const;
	TextureDimension getDimension() const;
	TextureFormat getFormat() const;
	uint32_t getHeight() const;
	uint32_t getMipLevelCount() const;
	uint32_t getSampleCount() const;
	TextureUsage getUsage() const;
	uint32_t getWidth() const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(TextureView)
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

}

} // namespace wgpu


// RAII handles
namespace wgpu {
HANDLE_RAII(Adapter, raw::Adapter);
HANDLE_RAII(BindGroup, raw::BindGroup);
HANDLE_RAII(BindGroupLayout, raw::BindGroupLayout);
HANDLE_RAII(Buffer, raw::Buffer);
HANDLE_RAII(CommandBuffer, raw::CommandBuffer);
HANDLE_RAII(CommandEncoder, raw::CommandEncoder);
HANDLE_RAII(ComputePassEncoder, raw::ComputePassEncoder);
HANDLE_RAII(ComputePipeline, raw::ComputePipeline);
HANDLE_RAII(Device, raw::Device);
HANDLE_RAII(Instance, raw::Instance);
HANDLE_RAII(PipelineLayout, raw::PipelineLayout);
HANDLE_RAII(QuerySet, raw::QuerySet);
HANDLE_RAII(Queue, raw::Queue);
HANDLE_RAII(RenderBundle, raw::RenderBundle);
HANDLE_RAII(RenderBundleEncoder, raw::RenderBundleEncoder);
HANDLE_RAII(RenderPassEncoder, raw::RenderPassEncoder);
HANDLE_RAII(RenderPipeline, raw::RenderPipeline);
HANDLE_RAII(Sampler, raw::Sampler);
HANDLE_RAII(ShaderModule, raw::ShaderModule);
HANDLE_RAII(Surface, raw::Surface);
HANDLE_RAII(Texture, raw::Texture);
HANDLE_RAII(TextureView, raw::TextureView);
} // namespace wgpu


// Structs
namespace wgpu {
STRUCT_NO_OSTREAM(StringView)
	const char * data;
	size_t length;
	StringView& setDefault();
	StringView& setData(const char * data);
	StringView& setLength(size_t length);
	StringView(const std::string_view& cpp) : data(cpp.data()), length(cpp.length()) {}
	StringView(const char* cstr) : data(cstr), length(WGPU_STRLEN) {}
	operator std::string_view() const;
	friend auto operator<<(std::ostream& stream, const S& self) -> std::ostream& {
		return stream << std::string_view(self);
	}
END

STRUCT(ChainedStruct)
	const struct WGPUChainedStruct * next;
	SType sType;
	ChainedStruct& setDefault();
	ChainedStruct& setNext(const struct WGPUChainedStruct * next);
	ChainedStruct& setSType(SType sType);
END

STRUCT(ChainedStructOut)
	struct WGPUChainedStructOut * next;
	SType sType;
	ChainedStructOut& setDefault();
	ChainedStructOut& setNext(struct WGPUChainedStructOut * next);
	ChainedStructOut& setSType(SType sType);
END

STRUCT(BlendComponent)
	BlendOperation operation;
	BlendFactor srcFactor;
	BlendFactor dstFactor;
	BlendComponent& setDefault();
	BlendComponent& setOperation(BlendOperation operation);
	BlendComponent& setSrcFactor(BlendFactor srcFactor);
	BlendComponent& setDstFactor(BlendFactor dstFactor);
END

STRUCT(Color)
	double r;
	double g;
	double b;
	double a;
	Color& setDefault();
	Color& setR(double r);
	Color& setG(double g);
	Color& setB(double b);
	Color& setA(double a);
	Color(double r, double g, double b, double a) : r(r), g(g), b(b), a(a) {}
END

STRUCT(ComputePassTimestampWrites)
	raw::QuerySet querySet;
	uint32_t beginningOfPassWriteIndex;
	uint32_t endOfPassWriteIndex;
	ComputePassTimestampWrites& setDefault();
	ComputePassTimestampWrites& setQuerySet(raw::QuerySet querySet);
	ComputePassTimestampWrites& setBeginningOfPassWriteIndex(uint32_t beginningOfPassWriteIndex);
	ComputePassTimestampWrites& setEndOfPassWriteIndex(uint32_t endOfPassWriteIndex);
END

STRUCT(Extent3D)
	uint32_t width;
	uint32_t height;
	uint32_t depthOrArrayLayers;
	Extent3D& setDefault();
	Extent3D& setWidth(uint32_t width);
	Extent3D& setHeight(uint32_t height);
	Extent3D& setDepthOrArrayLayers(uint32_t depthOrArrayLayers);
	Extent3D(uint32_t width, uint32_t height, uint32_t depthOrArrayLayers) : width(width), height(height), depthOrArrayLayers(depthOrArrayLayers) {}
END

STRUCT(Future)
	uint64_t id;
	Future& setDefault();
	Future& setId(uint64_t id);
END

STRUCT(Origin3D)
	uint32_t x;
	uint32_t y;
	uint32_t z;
	Origin3D& setDefault();
	Origin3D& setX(uint32_t x);
	Origin3D& setY(uint32_t y);
	Origin3D& setZ(uint32_t z);
	Origin3D(uint32_t x, uint32_t y, uint32_t z) : x(x), y(y), z(z) {}
END

STRUCT(RenderPassDepthStencilAttachment)
	raw::TextureView view;
	LoadOp depthLoadOp;
	StoreOp depthStoreOp;
	float depthClearValue;
	WGPUBool depthReadOnly;
	LoadOp stencilLoadOp;
	StoreOp stencilStoreOp;
	uint32_t stencilClearValue;
	WGPUBool stencilReadOnly;
	RenderPassDepthStencilAttachment& setDefault();
	RenderPassDepthStencilAttachment& setView(raw::TextureView view);
	RenderPassDepthStencilAttachment& setDepthLoadOp(LoadOp depthLoadOp);
	RenderPassDepthStencilAttachment& setDepthStoreOp(StoreOp depthStoreOp);
	RenderPassDepthStencilAttachment& setDepthClearValue(float depthClearValue);
	RenderPassDepthStencilAttachment& setDepthReadOnly(WGPUBool depthReadOnly);
	RenderPassDepthStencilAttachment& setStencilLoadOp(LoadOp stencilLoadOp);
	RenderPassDepthStencilAttachment& setStencilStoreOp(StoreOp stencilStoreOp);
	RenderPassDepthStencilAttachment& setStencilClearValue(uint32_t stencilClearValue);
	RenderPassDepthStencilAttachment& setStencilReadOnly(WGPUBool stencilReadOnly);
END

STRUCT(RenderPassMaxDrawCount)
	ChainedStruct chain;
	uint64_t maxDrawCount;
	RenderPassMaxDrawCount& setDefault();
	RenderPassMaxDrawCount& setMaxDrawCount(uint64_t maxDrawCount);
END

STRUCT(RenderPassTimestampWrites)
	raw::QuerySet querySet;
	uint32_t beginningOfPassWriteIndex;
	uint32_t endOfPassWriteIndex;
	RenderPassTimestampWrites& setDefault();
	RenderPassTimestampWrites& setQuerySet(raw::QuerySet querySet);
	RenderPassTimestampWrites& setBeginningOfPassWriteIndex(uint32_t beginningOfPassWriteIndex);
	RenderPassTimestampWrites& setEndOfPassWriteIndex(uint32_t endOfPassWriteIndex);
END

STRUCT(ShaderSourceSPIRV)
	ChainedStruct chain;
	uint32_t codeSize;
	const uint32_t * code;
	ShaderSourceSPIRV& setDefault();
	ShaderSourceSPIRV& setCodeSize(uint32_t codeSize);
	ShaderSourceSPIRV& setCode(const uint32_t * code);
END

STRUCT(ShaderSourceWGSL)
	ChainedStruct chain;
	StringView code;
	ShaderSourceWGSL& setDefault();
	ShaderSourceWGSL& setCode(StringView code);
END

STRUCT(StencilFaceState)
	CompareFunction compare;
	StencilOperation failOp;
	StencilOperation depthFailOp;
	StencilOperation passOp;
	StencilFaceState& setDefault();
	StencilFaceState& setCompare(CompareFunction compare);
	StencilFaceState& setFailOp(StencilOperation failOp);
	StencilFaceState& setDepthFailOp(StencilOperation depthFailOp);
	StencilFaceState& setPassOp(StencilOperation passOp);
END

STRUCT(SupportedFeatures)
	size_t featureCount;
	const FeatureName * features;
	SupportedFeatures& setDefault();
	SupportedFeatures& setFeatures(size_t featureCount, const FeatureName * features);
	SupportedFeatures& setFeatures(const std::vector<FeatureName>& features);
	SupportedFeatures& setFeatures(const std::span<const FeatureName>& features);
	void freeMembers();
END

STRUCT(SupportedWGSLLanguageFeatures)
	size_t featureCount;
	const WGSLLanguageFeatureName * features;
	SupportedWGSLLanguageFeatures& setDefault();
	SupportedWGSLLanguageFeatures& setFeatures(size_t featureCount, const WGSLLanguageFeatureName * features);
	SupportedWGSLLanguageFeatures& setFeatures(const std::vector<WGSLLanguageFeatureName>& features);
	SupportedWGSLLanguageFeatures& setFeatures(const std::span<const WGSLLanguageFeatureName>& features);
	void freeMembers();
END

STRUCT(SurfaceSourceAndroidNativeWindow)
	ChainedStruct chain;
	void * window;
	SurfaceSourceAndroidNativeWindow& setDefault();
	SurfaceSourceAndroidNativeWindow& setWindow(void * window);
END

STRUCT(SurfaceSourceMetalLayer)
	ChainedStruct chain;
	void * layer;
	SurfaceSourceMetalLayer& setDefault();
	SurfaceSourceMetalLayer& setLayer(void * layer);
END

STRUCT(SurfaceSourceWaylandSurface)
	ChainedStruct chain;
	void * display;
	void * surface;
	SurfaceSourceWaylandSurface& setDefault();
	SurfaceSourceWaylandSurface& setDisplay(void * display);
	SurfaceSourceWaylandSurface& setSurface(void * surface);
END

STRUCT(SurfaceSourceWindowsHWND)
	ChainedStruct chain;
	void * hinstance;
	void * hwnd;
	SurfaceSourceWindowsHWND& setDefault();
	SurfaceSourceWindowsHWND& setHinstance(void * hinstance);
	SurfaceSourceWindowsHWND& setHwnd(void * hwnd);
END

STRUCT(SurfaceSourceXCBWindow)
	ChainedStruct chain;
	void * connection;
	uint32_t window;
	SurfaceSourceXCBWindow& setDefault();
	SurfaceSourceXCBWindow& setConnection(void * connection);
	SurfaceSourceXCBWindow& setWindow(uint32_t window);
END

STRUCT(SurfaceSourceXlibWindow)
	ChainedStruct chain;
	void * display;
	uint64_t window;
	SurfaceSourceXlibWindow& setDefault();
	SurfaceSourceXlibWindow& setDisplay(void * display);
	SurfaceSourceXlibWindow& setWindow(uint64_t window);
END

STRUCT(TexelCopyBufferLayout)
	uint64_t offset;
	uint32_t bytesPerRow;
	uint32_t rowsPerImage;
	TexelCopyBufferLayout& setDefault();
	TexelCopyBufferLayout& setOffset(uint64_t offset);
	TexelCopyBufferLayout& setBytesPerRow(uint32_t bytesPerRow);
	TexelCopyBufferLayout& setRowsPerImage(uint32_t rowsPerImage);
END

STRUCT(VertexAttribute)
	VertexFormat format;
	uint64_t offset;
	uint32_t shaderLocation;
	VertexAttribute& setDefault();
	VertexAttribute& setFormat(VertexFormat format);
	VertexAttribute& setOffset(uint64_t offset);
	VertexAttribute& setShaderLocation(uint32_t shaderLocation);
END

STRUCT(BlendState)
	BlendComponent color;
	BlendComponent alpha;
	BlendState& setDefault();
	BlendState& setColor(BlendComponent color);
	BlendState& setAlpha(BlendComponent alpha);
END

STRUCT(FutureWaitInfo)
	Future future;
	WGPUBool completed;
	FutureWaitInfo& setDefault();
	FutureWaitInfo& setFuture(Future future);
	FutureWaitInfo& setCompleted(WGPUBool completed);
END

STRUCT(TexelCopyBufferInfo)
	TexelCopyBufferLayout layout;
	raw::Buffer buffer;
	TexelCopyBufferInfo& setDefault();
	TexelCopyBufferInfo& setLayout(TexelCopyBufferLayout layout);
	TexelCopyBufferInfo& setBuffer(raw::Buffer buffer);
END

STRUCT(TexelCopyTextureInfo)
	raw::Texture texture;
	uint32_t mipLevel;
	Origin3D origin;
	TextureAspect aspect;
	TexelCopyTextureInfo& setDefault();
	TexelCopyTextureInfo& setTexture(raw::Texture texture);
	TexelCopyTextureInfo& setMipLevel(uint32_t mipLevel);
	TexelCopyTextureInfo& setOrigin(Origin3D origin);
	TexelCopyTextureInfo& setAspect(TextureAspect aspect);
END

STRUCT(VertexBufferLayout)
	VertexStepMode stepMode;
	uint64_t arrayStride;
	size_t attributeCount;
	const VertexAttribute * attributes;
	VertexBufferLayout& setDefault();
	VertexBufferLayout& setStepMode(VertexStepMode stepMode);
	VertexBufferLayout& setArrayStride(uint64_t arrayStride);
	VertexBufferLayout& setAttributes(size_t attributeCount, const VertexAttribute * attributes);
	VertexBufferLayout& setAttributes(const std::vector<VertexAttribute>& attributes);
	VertexBufferLayout& setAttributes(const std::span<const VertexAttribute>& attributes);
END

STRUCT(InstanceExtras)
	ChainedStruct chain;
	InstanceBackend backends;
	InstanceFlag flags;
	Dx12Compiler dx12ShaderCompiler;
	Gles3MinorVersion gles3MinorVersion;
	GLFenceBehaviour glFenceBehaviour;
	StringView dxcPath;
	DxcMaxShaderModel dxcMaxShaderModel;
	Dx12SwapchainKind dx12PresentationSystem;
	const uint8_t * budgetForDeviceCreation;
	const uint8_t * budgetForDeviceLoss;
	InstanceExtras& setDefault();
	InstanceExtras& setBackends(InstanceBackend backends);
	InstanceExtras& setFlags(InstanceFlag flags);
	InstanceExtras& setDx12ShaderCompiler(Dx12Compiler dx12ShaderCompiler);
	InstanceExtras& setGles3MinorVersion(Gles3MinorVersion gles3MinorVersion);
	InstanceExtras& setGlFenceBehaviour(GLFenceBehaviour glFenceBehaviour);
	InstanceExtras& setDxcPath(StringView dxcPath);
	InstanceExtras& setDxcMaxShaderModel(DxcMaxShaderModel dxcMaxShaderModel);
	InstanceExtras& setDx12PresentationSystem(Dx12SwapchainKind dx12PresentationSystem);
	InstanceExtras& setBudgetForDeviceCreation(const uint8_t * budgetForDeviceCreation);
	InstanceExtras& setBudgetForDeviceLoss(const uint8_t * budgetForDeviceLoss);
END

STRUCT(DeviceExtras)
	ChainedStruct chain;
	StringView tracePath;
	DeviceExtras& setDefault();
	DeviceExtras& setTracePath(StringView tracePath);
END

STRUCT(NativeLimits)
	ChainedStructOut chain;
	uint32_t maxPushConstantSize;
	uint32_t maxNonSamplerBindings;
	NativeLimits& setDefault();
	NativeLimits& setMaxPushConstantSize(uint32_t maxPushConstantSize);
	NativeLimits& setMaxNonSamplerBindings(uint32_t maxNonSamplerBindings);
END

STRUCT(PushConstantRange)
	ShaderStage stages;
	uint32_t start;
	uint32_t end;
	PushConstantRange& setDefault();
	PushConstantRange& setStages(ShaderStage stages);
	PushConstantRange& setStart(uint32_t start);
	PushConstantRange& setEnd(uint32_t end);
END

STRUCT(PipelineLayoutExtras)
	ChainedStruct chain;
	size_t pushConstantRangeCount;
	const PushConstantRange * pushConstantRanges;
	PipelineLayoutExtras& setDefault();
	PipelineLayoutExtras& setPushConstantRanges(size_t pushConstantRangeCount, const PushConstantRange * pushConstantRanges);
	PipelineLayoutExtras& setPushConstantRanges(const std::vector<PushConstantRange>& pushConstantRanges);
	PipelineLayoutExtras& setPushConstantRanges(const std::span<const PushConstantRange>& pushConstantRanges);
END

STRUCT(ShaderDefine)
	StringView name;
	StringView value;
	ShaderDefine& setDefault();
	ShaderDefine& setName(StringView name);
	ShaderDefine& setValue(StringView value);
END

STRUCT(ShaderSourceGLSL)
	ChainedStruct chain;
	ShaderStage stage;
	StringView code;
	uint32_t defineCount;
	ShaderDefine * defines;
	ShaderSourceGLSL& setDefault();
	ShaderSourceGLSL& setStage(ShaderStage stage);
	ShaderSourceGLSL& setCode(StringView code);
	ShaderSourceGLSL& setDefines(uint32_t defineCount, ShaderDefine * defines);
	ShaderSourceGLSL& setDefines(std::vector<ShaderDefine>& defines);
	ShaderSourceGLSL& setDefines(const std::span<ShaderDefine>& defines);
END

STRUCT(ShaderModuleDescriptorSpirV)
	StringView label;
	uint32_t sourceSize;
	const uint32_t * source;
	ShaderModuleDescriptorSpirV& setDefault();
	ShaderModuleDescriptorSpirV& setLabel(StringView label);
	ShaderModuleDescriptorSpirV& setSourceSize(uint32_t sourceSize);
	ShaderModuleDescriptorSpirV& setSource(const uint32_t * source);
END

STRUCT(RegistryReport)
	size_t numAllocated;
	size_t numKeptFromUser;
	size_t numReleasedFromUser;
	size_t elementSize;
	RegistryReport& setDefault();
	RegistryReport& setNumAllocated(size_t numAllocated);
	RegistryReport& setNumKeptFromUser(size_t numKeptFromUser);
	RegistryReport& setNumReleasedFromUser(size_t numReleasedFromUser);
	RegistryReport& setElementSize(size_t elementSize);
END

STRUCT(HubReport)
	RegistryReport adapters;
	RegistryReport devices;
	RegistryReport queues;
	RegistryReport pipelineLayouts;
	RegistryReport shaderModules;
	RegistryReport bindGroupLayouts;
	RegistryReport bindGroups;
	RegistryReport commandBuffers;
	RegistryReport renderBundles;
	RegistryReport renderPipelines;
	RegistryReport computePipelines;
	RegistryReport pipelineCaches;
	RegistryReport querySets;
	RegistryReport buffers;
	RegistryReport textures;
	RegistryReport textureViews;
	RegistryReport samplers;
	HubReport& setDefault();
	HubReport& setAdapters(RegistryReport adapters);
	HubReport& setDevices(RegistryReport devices);
	HubReport& setQueues(RegistryReport queues);
	HubReport& setPipelineLayouts(RegistryReport pipelineLayouts);
	HubReport& setShaderModules(RegistryReport shaderModules);
	HubReport& setBindGroupLayouts(RegistryReport bindGroupLayouts);
	HubReport& setBindGroups(RegistryReport bindGroups);
	HubReport& setCommandBuffers(RegistryReport commandBuffers);
	HubReport& setRenderBundles(RegistryReport renderBundles);
	HubReport& setRenderPipelines(RegistryReport renderPipelines);
	HubReport& setComputePipelines(RegistryReport computePipelines);
	HubReport& setPipelineCaches(RegistryReport pipelineCaches);
	HubReport& setQuerySets(RegistryReport querySets);
	HubReport& setBuffers(RegistryReport buffers);
	HubReport& setTextures(RegistryReport textures);
	HubReport& setTextureViews(RegistryReport textureViews);
	HubReport& setSamplers(RegistryReport samplers);
END

STRUCT(GlobalReport)
	RegistryReport surfaces;
	HubReport hub;
	GlobalReport& setDefault();
	GlobalReport& setSurfaces(RegistryReport surfaces);
	GlobalReport& setHub(HubReport hub);
END

STRUCT(BindGroupEntryExtras)
	ChainedStruct chain;
	const raw::Buffer * buffers;
	size_t bufferCount;
	const raw::Sampler * samplers;
	size_t samplerCount;
	const raw::TextureView * textureViews;
	size_t textureViewCount;
	BindGroupEntryExtras& setDefault();
	BindGroupEntryExtras& setBuffers(size_t bufferCount, const raw::Buffer * buffers);
	BindGroupEntryExtras& setBuffers(const std::vector<raw::Buffer>& buffers);
	BindGroupEntryExtras& setBuffers(const std::span<const raw::Buffer>& buffers);
	BindGroupEntryExtras& setBuffers(const std::vector<Buffer>& buffers);
	BindGroupEntryExtras& setBuffers(const std::span<const Buffer>& buffers);
	BindGroupEntryExtras& setSamplers(size_t samplerCount, const raw::Sampler * samplers);
	BindGroupEntryExtras& setSamplers(const std::vector<raw::Sampler>& samplers);
	BindGroupEntryExtras& setSamplers(const std::span<const raw::Sampler>& samplers);
	BindGroupEntryExtras& setSamplers(const std::vector<Sampler>& samplers);
	BindGroupEntryExtras& setSamplers(const std::span<const Sampler>& samplers);
	BindGroupEntryExtras& setTextureViews(size_t textureViewCount, const raw::TextureView * textureViews);
	BindGroupEntryExtras& setTextureViews(const std::vector<raw::TextureView>& textureViews);
	BindGroupEntryExtras& setTextureViews(const std::span<const raw::TextureView>& textureViews);
	BindGroupEntryExtras& setTextureViews(const std::vector<TextureView>& textureViews);
	BindGroupEntryExtras& setTextureViews(const std::span<const TextureView>& textureViews);
END

STRUCT(BindGroupLayoutEntryExtras)
	ChainedStruct chain;
	uint32_t count;
	BindGroupLayoutEntryExtras& setDefault();
	BindGroupLayoutEntryExtras& setCount(uint32_t count);
END

STRUCT(QuerySetDescriptorExtras)
	ChainedStruct chain;
	const PipelineStatisticName * pipelineStatistics;
	size_t pipelineStatisticCount;
	QuerySetDescriptorExtras& setDefault();
	QuerySetDescriptorExtras& setPipelineStatistics(size_t pipelineStatisticCount, const PipelineStatisticName * pipelineStatistics);
	QuerySetDescriptorExtras& setPipelineStatistics(const std::vector<PipelineStatisticName>& pipelineStatistics);
	QuerySetDescriptorExtras& setPipelineStatistics(const std::span<const PipelineStatisticName>& pipelineStatistics);
END

STRUCT(SurfaceConfigurationExtras)
	ChainedStruct chain;
	uint32_t desiredMaximumFrameLatency;
	SurfaceConfigurationExtras& setDefault();
	SurfaceConfigurationExtras& setDesiredMaximumFrameLatency(uint32_t desiredMaximumFrameLatency);
END

STRUCT(SurfaceSourceSwapChainPanel)
	ChainedStruct chain;
	void * panelNative;
	SurfaceSourceSwapChainPanel& setDefault();
	SurfaceSourceSwapChainPanel& setPanelNative(void * panelNative);
END

STRUCT(PrimitiveStateExtras)
	ChainedStruct chain;
	PolygonMode polygonMode;
	WGPUBool conservative;
	PrimitiveStateExtras& setDefault();
	PrimitiveStateExtras& setPolygonMode(PolygonMode polygonMode);
	PrimitiveStateExtras& setConservative(WGPUBool conservative);
END

} // namespace wgpu


// Descriptors
namespace wgpu {
DESCRIPTOR(BufferMapCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUBufferMapCallback callback;
	void * userdata1;
	void * userdata2;
	BufferMapCallbackInfo& setDefault();
	BufferMapCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	BufferMapCallbackInfo& setMode(CallbackMode mode);
	BufferMapCallbackInfo& setCallback(WGPUBufferMapCallback callback);
	BufferMapCallbackInfo& setUserdata1(void * userdata1);
	BufferMapCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(CompilationInfoCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUCompilationInfoCallback callback;
	void * userdata1;
	void * userdata2;
	CompilationInfoCallbackInfo& setDefault();
	CompilationInfoCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	CompilationInfoCallbackInfo& setMode(CallbackMode mode);
	CompilationInfoCallbackInfo& setCallback(WGPUCompilationInfoCallback callback);
	CompilationInfoCallbackInfo& setUserdata1(void * userdata1);
	CompilationInfoCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(CreateComputePipelineAsyncCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUCreateComputePipelineAsyncCallback callback;
	void * userdata1;
	void * userdata2;
	CreateComputePipelineAsyncCallbackInfo& setDefault();
	CreateComputePipelineAsyncCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	CreateComputePipelineAsyncCallbackInfo& setMode(CallbackMode mode);
	CreateComputePipelineAsyncCallbackInfo& setCallback(WGPUCreateComputePipelineAsyncCallback callback);
	CreateComputePipelineAsyncCallbackInfo& setUserdata1(void * userdata1);
	CreateComputePipelineAsyncCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(CreateRenderPipelineAsyncCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUCreateRenderPipelineAsyncCallback callback;
	void * userdata1;
	void * userdata2;
	CreateRenderPipelineAsyncCallbackInfo& setDefault();
	CreateRenderPipelineAsyncCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	CreateRenderPipelineAsyncCallbackInfo& setMode(CallbackMode mode);
	CreateRenderPipelineAsyncCallbackInfo& setCallback(WGPUCreateRenderPipelineAsyncCallback callback);
	CreateRenderPipelineAsyncCallbackInfo& setUserdata1(void * userdata1);
	CreateRenderPipelineAsyncCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(DeviceLostCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUDeviceLostCallback callback;
	void * userdata1;
	void * userdata2;
	DeviceLostCallbackInfo& setDefault();
	DeviceLostCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	DeviceLostCallbackInfo& setMode(CallbackMode mode);
	DeviceLostCallbackInfo& setCallback(WGPUDeviceLostCallback callback);
	DeviceLostCallbackInfo& setUserdata1(void * userdata1);
	DeviceLostCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(PopErrorScopeCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUPopErrorScopeCallback callback;
	void * userdata1;
	void * userdata2;
	PopErrorScopeCallbackInfo& setDefault();
	PopErrorScopeCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	PopErrorScopeCallbackInfo& setMode(CallbackMode mode);
	PopErrorScopeCallbackInfo& setCallback(WGPUPopErrorScopeCallback callback);
	PopErrorScopeCallbackInfo& setUserdata1(void * userdata1);
	PopErrorScopeCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(QueueWorkDoneCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPUQueueWorkDoneCallback callback;
	void * userdata1;
	void * userdata2;
	QueueWorkDoneCallbackInfo& setDefault();
	QueueWorkDoneCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	QueueWorkDoneCallbackInfo& setMode(CallbackMode mode);
	QueueWorkDoneCallbackInfo& setCallback(WGPUQueueWorkDoneCallback callback);
	QueueWorkDoneCallbackInfo& setUserdata1(void * userdata1);
	QueueWorkDoneCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(RequestAdapterCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPURequestAdapterCallback callback;
	void * userdata1;
	void * userdata2;
	RequestAdapterCallbackInfo& setDefault();
	RequestAdapterCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	RequestAdapterCallbackInfo& setMode(CallbackMode mode);
	RequestAdapterCallbackInfo& setCallback(WGPURequestAdapterCallback callback);
	RequestAdapterCallbackInfo& setUserdata1(void * userdata1);
	RequestAdapterCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(RequestDeviceCallbackInfo)
	const ChainedStruct * nextInChain;
	CallbackMode mode;
	WGPURequestDeviceCallback callback;
	void * userdata1;
	void * userdata2;
	RequestDeviceCallbackInfo& setDefault();
	RequestDeviceCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	RequestDeviceCallbackInfo& setMode(CallbackMode mode);
	RequestDeviceCallbackInfo& setCallback(WGPURequestDeviceCallback callback);
	RequestDeviceCallbackInfo& setUserdata1(void * userdata1);
	RequestDeviceCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(UncapturedErrorCallbackInfo)
	const ChainedStruct * nextInChain;
	WGPUUncapturedErrorCallback callback;
	void * userdata1;
	void * userdata2;
	UncapturedErrorCallbackInfo& setDefault();
	UncapturedErrorCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	UncapturedErrorCallbackInfo& setCallback(WGPUUncapturedErrorCallback callback);
	UncapturedErrorCallbackInfo& setUserdata1(void * userdata1);
	UncapturedErrorCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(AdapterInfo)
	ChainedStructOut * nextInChain;
	StringView vendor;
	StringView architecture;
	StringView device;
	StringView description;
	BackendType backendType;
	AdapterType adapterType;
	uint32_t vendorID;
	uint32_t deviceID;
	AdapterInfo& setDefault();
	AdapterInfo& setNextInChain(ChainedStructOut * nextInChain);
	AdapterInfo& setVendor(StringView vendor);
	AdapterInfo& setArchitecture(StringView architecture);
	AdapterInfo& setDevice(StringView device);
	AdapterInfo& setDescription(StringView description);
	AdapterInfo& setBackendType(BackendType backendType);
	AdapterInfo& setAdapterType(AdapterType adapterType);
	AdapterInfo& setVendorID(uint32_t vendorID);
	AdapterInfo& setDeviceID(uint32_t deviceID);
	void freeMembers();
END

DESCRIPTOR(BindGroupEntry)
	const ChainedStruct * nextInChain;
	uint32_t binding;
	raw::Buffer buffer;
	uint64_t offset;
	uint64_t size;
	raw::Sampler sampler;
	raw::TextureView textureView;
	BindGroupEntry& setDefault();
	BindGroupEntry& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupEntry& setBinding(uint32_t binding);
	BindGroupEntry& setBuffer(raw::Buffer buffer);
	BindGroupEntry& setOffset(uint64_t offset);
	BindGroupEntry& setSize(uint64_t size);
	BindGroupEntry& setSampler(raw::Sampler sampler);
	BindGroupEntry& setTextureView(raw::TextureView textureView);
END

DESCRIPTOR(BufferBindingLayout)
	const ChainedStruct * nextInChain;
	BufferBindingType type;
	WGPUBool hasDynamicOffset;
	uint64_t minBindingSize;
	BufferBindingLayout& setDefault();
	BufferBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	BufferBindingLayout& setType(BufferBindingType type);
	BufferBindingLayout& setHasDynamicOffset(WGPUBool hasDynamicOffset);
	BufferBindingLayout& setMinBindingSize(uint64_t minBindingSize);
END

DESCRIPTOR(BufferDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	BufferUsage usage;
	uint64_t size;
	WGPUBool mappedAtCreation;
	BufferDescriptor& setDefault();
	BufferDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	BufferDescriptor& setLabel(StringView label);
	BufferDescriptor& setUsage(BufferUsage usage);
	BufferDescriptor& setSize(uint64_t size);
	BufferDescriptor& setMappedAtCreation(WGPUBool mappedAtCreation);
END

DESCRIPTOR(CommandBufferDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	CommandBufferDescriptor& setDefault();
	CommandBufferDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	CommandBufferDescriptor& setLabel(StringView label);
END

DESCRIPTOR(CommandEncoderDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	CommandEncoderDescriptor& setDefault();
	CommandEncoderDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	CommandEncoderDescriptor& setLabel(StringView label);
END

DESCRIPTOR(CompilationMessage)
	const ChainedStruct * nextInChain;
	StringView message;
	CompilationMessageType type;
	uint64_t lineNum;
	uint64_t linePos;
	uint64_t offset;
	uint64_t length;
	CompilationMessage& setDefault();
	CompilationMessage& setNextInChain(const ChainedStruct * nextInChain);
	CompilationMessage& setMessage(StringView message);
	CompilationMessage& setType(CompilationMessageType type);
	CompilationMessage& setLineNum(uint64_t lineNum);
	CompilationMessage& setLinePos(uint64_t linePos);
	CompilationMessage& setOffset(uint64_t offset);
	CompilationMessage& setLength(uint64_t length);
END

DESCRIPTOR(ConstantEntry)
	const ChainedStruct * nextInChain;
	StringView key;
	double value;
	ConstantEntry& setDefault();
	ConstantEntry& setNextInChain(const ChainedStruct * nextInChain);
	ConstantEntry& setKey(StringView key);
	ConstantEntry& setValue(double value);
END

DESCRIPTOR(InstanceCapabilities)
	ChainedStructOut * nextInChain;
	WGPUBool timedWaitAnyEnable;
	size_t timedWaitAnyMaxCount;
	InstanceCapabilities& setDefault();
	InstanceCapabilities& setNextInChain(ChainedStructOut * nextInChain);
	InstanceCapabilities& setTimedWaitAnyEnable(WGPUBool timedWaitAnyEnable);
	InstanceCapabilities& setTimedWaitAnyMaxCount(size_t timedWaitAnyMaxCount);
END

DESCRIPTOR(Limits)
	ChainedStructOut * nextInChain;
	uint32_t maxTextureDimension1D;
	uint32_t maxTextureDimension2D;
	uint32_t maxTextureDimension3D;
	uint32_t maxTextureArrayLayers;
	uint32_t maxBindGroups;
	uint32_t maxBindGroupsPlusVertexBuffers;
	uint32_t maxBindingsPerBindGroup;
	uint32_t maxDynamicUniformBuffersPerPipelineLayout;
	uint32_t maxDynamicStorageBuffersPerPipelineLayout;
	uint32_t maxSampledTexturesPerShaderStage;
	uint32_t maxSamplersPerShaderStage;
	uint32_t maxStorageBuffersPerShaderStage;
	uint32_t maxStorageTexturesPerShaderStage;
	uint32_t maxUniformBuffersPerShaderStage;
	uint64_t maxUniformBufferBindingSize;
	uint64_t maxStorageBufferBindingSize;
	uint32_t minUniformBufferOffsetAlignment;
	uint32_t minStorageBufferOffsetAlignment;
	uint32_t maxVertexBuffers;
	uint64_t maxBufferSize;
	uint32_t maxVertexAttributes;
	uint32_t maxVertexBufferArrayStride;
	uint32_t maxInterStageShaderVariables;
	uint32_t maxColorAttachments;
	uint32_t maxColorAttachmentBytesPerSample;
	uint32_t maxComputeWorkgroupStorageSize;
	uint32_t maxComputeInvocationsPerWorkgroup;
	uint32_t maxComputeWorkgroupSizeX;
	uint32_t maxComputeWorkgroupSizeY;
	uint32_t maxComputeWorkgroupSizeZ;
	uint32_t maxComputeWorkgroupsPerDimension;
	Limits& setDefault();
	Limits& setNextInChain(ChainedStructOut * nextInChain);
	Limits& setMaxTextureDimension1D(uint32_t maxTextureDimension1D);
	Limits& setMaxTextureDimension2D(uint32_t maxTextureDimension2D);
	Limits& setMaxTextureDimension3D(uint32_t maxTextureDimension3D);
	Limits& setMaxTextureArrayLayers(uint32_t maxTextureArrayLayers);
	Limits& setMaxBindGroups(uint32_t maxBindGroups);
	Limits& setMaxBindGroupsPlusVertexBuffers(uint32_t maxBindGroupsPlusVertexBuffers);
	Limits& setMaxBindingsPerBindGroup(uint32_t maxBindingsPerBindGroup);
	Limits& setMaxDynamicUniformBuffersPerPipelineLayout(uint32_t maxDynamicUniformBuffersPerPipelineLayout);
	Limits& setMaxDynamicStorageBuffersPerPipelineLayout(uint32_t maxDynamicStorageBuffersPerPipelineLayout);
	Limits& setMaxSampledTexturesPerShaderStage(uint32_t maxSampledTexturesPerShaderStage);
	Limits& setMaxSamplersPerShaderStage(uint32_t maxSamplersPerShaderStage);
	Limits& setMaxStorageBuffersPerShaderStage(uint32_t maxStorageBuffersPerShaderStage);
	Limits& setMaxStorageTexturesPerShaderStage(uint32_t maxStorageTexturesPerShaderStage);
	Limits& setMaxUniformBuffersPerShaderStage(uint32_t maxUniformBuffersPerShaderStage);
	Limits& setMaxUniformBufferBindingSize(uint64_t maxUniformBufferBindingSize);
	Limits& setMaxStorageBufferBindingSize(uint64_t maxStorageBufferBindingSize);
	Limits& setMinUniformBufferOffsetAlignment(uint32_t minUniformBufferOffsetAlignment);
	Limits& setMinStorageBufferOffsetAlignment(uint32_t minStorageBufferOffsetAlignment);
	Limits& setMaxVertexBuffers(uint32_t maxVertexBuffers);
	Limits& setMaxBufferSize(uint64_t maxBufferSize);
	Limits& setMaxVertexAttributes(uint32_t maxVertexAttributes);
	Limits& setMaxVertexBufferArrayStride(uint32_t maxVertexBufferArrayStride);
	Limits& setMaxInterStageShaderVariables(uint32_t maxInterStageShaderVariables);
	Limits& setMaxColorAttachments(uint32_t maxColorAttachments);
	Limits& setMaxColorAttachmentBytesPerSample(uint32_t maxColorAttachmentBytesPerSample);
	Limits& setMaxComputeWorkgroupStorageSize(uint32_t maxComputeWorkgroupStorageSize);
	Limits& setMaxComputeInvocationsPerWorkgroup(uint32_t maxComputeInvocationsPerWorkgroup);
	Limits& setMaxComputeWorkgroupSizeX(uint32_t maxComputeWorkgroupSizeX);
	Limits& setMaxComputeWorkgroupSizeY(uint32_t maxComputeWorkgroupSizeY);
	Limits& setMaxComputeWorkgroupSizeZ(uint32_t maxComputeWorkgroupSizeZ);
	Limits& setMaxComputeWorkgroupsPerDimension(uint32_t maxComputeWorkgroupsPerDimension);
END

DESCRIPTOR(MultisampleState)
	const ChainedStruct * nextInChain;
	uint32_t count;
	uint32_t mask;
	WGPUBool alphaToCoverageEnabled;
	MultisampleState& setDefault();
	MultisampleState& setNextInChain(const ChainedStruct * nextInChain);
	MultisampleState& setCount(uint32_t count);
	MultisampleState& setMask(uint32_t mask);
	MultisampleState& setAlphaToCoverageEnabled(WGPUBool alphaToCoverageEnabled);
END

DESCRIPTOR(PipelineLayoutDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	size_t bindGroupLayoutCount;
	const raw::BindGroupLayout * bindGroupLayouts;
	PipelineLayoutDescriptor& setDefault();
	PipelineLayoutDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	PipelineLayoutDescriptor& setLabel(StringView label);
	PipelineLayoutDescriptor& setBindGroupLayouts(size_t bindGroupLayoutCount, const raw::BindGroupLayout * bindGroupLayouts);
	PipelineLayoutDescriptor& setBindGroupLayouts(const std::vector<raw::BindGroupLayout>& bindGroupLayouts);
	PipelineLayoutDescriptor& setBindGroupLayouts(const std::span<const raw::BindGroupLayout>& bindGroupLayouts);
	PipelineLayoutDescriptor& setBindGroupLayouts(const std::vector<BindGroupLayout>& bindGroupLayouts);
	PipelineLayoutDescriptor& setBindGroupLayouts(const std::span<const BindGroupLayout>& bindGroupLayouts);
END

DESCRIPTOR(PrimitiveState)
	const ChainedStruct * nextInChain;
	PrimitiveTopology topology;
	IndexFormat stripIndexFormat;
	FrontFace frontFace;
	CullMode cullMode;
	WGPUBool unclippedDepth;
	PrimitiveState& setDefault();
	PrimitiveState& setNextInChain(const ChainedStruct * nextInChain);
	PrimitiveState& setTopology(PrimitiveTopology topology);
	PrimitiveState& setStripIndexFormat(IndexFormat stripIndexFormat);
	PrimitiveState& setFrontFace(FrontFace frontFace);
	PrimitiveState& setCullMode(CullMode cullMode);
	PrimitiveState& setUnclippedDepth(WGPUBool unclippedDepth);
END

DESCRIPTOR(QuerySetDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	QueryType type;
	uint32_t count;
	QuerySetDescriptor& setDefault();
	QuerySetDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	QuerySetDescriptor& setLabel(StringView label);
	QuerySetDescriptor& setType(QueryType type);
	QuerySetDescriptor& setCount(uint32_t count);
END

DESCRIPTOR(QueueDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	QueueDescriptor& setDefault();
	QueueDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	QueueDescriptor& setLabel(StringView label);
END

DESCRIPTOR(RenderBundleDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	RenderBundleDescriptor& setDefault();
	RenderBundleDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderBundleDescriptor& setLabel(StringView label);
END

DESCRIPTOR(RenderBundleEncoderDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	size_t colorFormatCount;
	const TextureFormat * colorFormats;
	TextureFormat depthStencilFormat;
	uint32_t sampleCount;
	WGPUBool depthReadOnly;
	WGPUBool stencilReadOnly;
	RenderBundleEncoderDescriptor& setDefault();
	RenderBundleEncoderDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderBundleEncoderDescriptor& setLabel(StringView label);
	RenderBundleEncoderDescriptor& setColorFormats(size_t colorFormatCount, const TextureFormat * colorFormats);
	RenderBundleEncoderDescriptor& setColorFormats(const std::vector<TextureFormat>& colorFormats);
	RenderBundleEncoderDescriptor& setColorFormats(const std::span<const TextureFormat>& colorFormats);
	RenderBundleEncoderDescriptor& setDepthStencilFormat(TextureFormat depthStencilFormat);
	RenderBundleEncoderDescriptor& setSampleCount(uint32_t sampleCount);
	RenderBundleEncoderDescriptor& setDepthReadOnly(WGPUBool depthReadOnly);
	RenderBundleEncoderDescriptor& setStencilReadOnly(WGPUBool stencilReadOnly);
END

DESCRIPTOR(RequestAdapterOptions)
	const ChainedStruct * nextInChain;
	FeatureLevel featureLevel;
	PowerPreference powerPreference;
	WGPUBool forceFallbackAdapter;
	BackendType backendType;
	raw::Surface compatibleSurface;
	RequestAdapterOptions& setDefault();
	RequestAdapterOptions& setNextInChain(const ChainedStruct * nextInChain);
	RequestAdapterOptions& setFeatureLevel(FeatureLevel featureLevel);
	RequestAdapterOptions& setPowerPreference(PowerPreference powerPreference);
	RequestAdapterOptions& setForceFallbackAdapter(WGPUBool forceFallbackAdapter);
	RequestAdapterOptions& setBackendType(BackendType backendType);
	RequestAdapterOptions& setCompatibleSurface(raw::Surface compatibleSurface);
END

DESCRIPTOR(SamplerBindingLayout)
	const ChainedStruct * nextInChain;
	SamplerBindingType type;
	SamplerBindingLayout& setDefault();
	SamplerBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	SamplerBindingLayout& setType(SamplerBindingType type);
END

DESCRIPTOR(SamplerDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	AddressMode addressModeU;
	AddressMode addressModeV;
	AddressMode addressModeW;
	FilterMode magFilter;
	FilterMode minFilter;
	MipmapFilterMode mipmapFilter;
	float lodMinClamp;
	float lodMaxClamp;
	CompareFunction compare;
	uint16_t maxAnisotropy;
	SamplerDescriptor& setDefault();
	SamplerDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	SamplerDescriptor& setLabel(StringView label);
	SamplerDescriptor& setAddressModeU(AddressMode addressModeU);
	SamplerDescriptor& setAddressModeV(AddressMode addressModeV);
	SamplerDescriptor& setAddressModeW(AddressMode addressModeW);
	SamplerDescriptor& setMagFilter(FilterMode magFilter);
	SamplerDescriptor& setMinFilter(FilterMode minFilter);
	SamplerDescriptor& setMipmapFilter(MipmapFilterMode mipmapFilter);
	SamplerDescriptor& setLodMinClamp(float lodMinClamp);
	SamplerDescriptor& setLodMaxClamp(float lodMaxClamp);
	SamplerDescriptor& setCompare(CompareFunction compare);
	SamplerDescriptor& setMaxAnisotropy(uint16_t maxAnisotropy);
END

DESCRIPTOR(ShaderModuleDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	ShaderModuleDescriptor& setDefault();
	ShaderModuleDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ShaderModuleDescriptor& setLabel(StringView label);
END

DESCRIPTOR(StorageTextureBindingLayout)
	const ChainedStruct * nextInChain;
	StorageTextureAccess access;
	TextureFormat format;
	TextureViewDimension viewDimension;
	StorageTextureBindingLayout& setDefault();
	StorageTextureBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	StorageTextureBindingLayout& setAccess(StorageTextureAccess access);
	StorageTextureBindingLayout& setFormat(TextureFormat format);
	StorageTextureBindingLayout& setViewDimension(TextureViewDimension viewDimension);
END

DESCRIPTOR(SurfaceCapabilities)
	ChainedStructOut * nextInChain;
	TextureUsage usages;
	size_t formatCount;
	const TextureFormat * formats;
	size_t presentModeCount;
	const PresentMode * presentModes;
	size_t alphaModeCount;
	const CompositeAlphaMode * alphaModes;
	SurfaceCapabilities& setDefault();
	SurfaceCapabilities& setNextInChain(ChainedStructOut * nextInChain);
	SurfaceCapabilities& setUsages(TextureUsage usages);
	SurfaceCapabilities& setFormats(size_t formatCount, const TextureFormat * formats);
	SurfaceCapabilities& setFormats(const std::vector<TextureFormat>& formats);
	SurfaceCapabilities& setFormats(const std::span<const TextureFormat>& formats);
	SurfaceCapabilities& setPresentModes(size_t presentModeCount, const PresentMode * presentModes);
	SurfaceCapabilities& setPresentModes(const std::vector<PresentMode>& presentModes);
	SurfaceCapabilities& setPresentModes(const std::span<const PresentMode>& presentModes);
	SurfaceCapabilities& setAlphaModes(size_t alphaModeCount, const CompositeAlphaMode * alphaModes);
	SurfaceCapabilities& setAlphaModes(const std::vector<CompositeAlphaMode>& alphaModes);
	SurfaceCapabilities& setAlphaModes(const std::span<const CompositeAlphaMode>& alphaModes);
	void freeMembers();
END

DESCRIPTOR(SurfaceConfiguration)
	const ChainedStruct * nextInChain;
	raw::Device device;
	TextureFormat format;
	TextureUsage usage;
	uint32_t width;
	uint32_t height;
	size_t viewFormatCount;
	const TextureFormat * viewFormats;
	CompositeAlphaMode alphaMode;
	PresentMode presentMode;
	SurfaceConfiguration& setDefault();
	SurfaceConfiguration& setNextInChain(const ChainedStruct * nextInChain);
	SurfaceConfiguration& setDevice(raw::Device device);
	SurfaceConfiguration& setFormat(TextureFormat format);
	SurfaceConfiguration& setUsage(TextureUsage usage);
	SurfaceConfiguration& setWidth(uint32_t width);
	SurfaceConfiguration& setHeight(uint32_t height);
	SurfaceConfiguration& setViewFormats(size_t viewFormatCount, const TextureFormat * viewFormats);
	SurfaceConfiguration& setViewFormats(const std::vector<TextureFormat>& viewFormats);
	SurfaceConfiguration& setViewFormats(const std::span<const TextureFormat>& viewFormats);
	SurfaceConfiguration& setAlphaMode(CompositeAlphaMode alphaMode);
	SurfaceConfiguration& setPresentMode(PresentMode presentMode);
END

DESCRIPTOR(SurfaceDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	SurfaceDescriptor& setDefault();
	SurfaceDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	SurfaceDescriptor& setLabel(StringView label);
END

DESCRIPTOR(SurfaceTexture)
	ChainedStructOut * nextInChain;
	raw::Texture texture;
	SurfaceGetCurrentTextureStatus status;
	SurfaceTexture& setDefault();
	SurfaceTexture& setNextInChain(ChainedStructOut * nextInChain);
	SurfaceTexture& setTexture(raw::Texture texture);
	SurfaceTexture& setStatus(SurfaceGetCurrentTextureStatus status);
END

DESCRIPTOR(TextureBindingLayout)
	const ChainedStruct * nextInChain;
	TextureSampleType sampleType;
	TextureViewDimension viewDimension;
	WGPUBool multisampled;
	TextureBindingLayout& setDefault();
	TextureBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	TextureBindingLayout& setSampleType(TextureSampleType sampleType);
	TextureBindingLayout& setViewDimension(TextureViewDimension viewDimension);
	TextureBindingLayout& setMultisampled(WGPUBool multisampled);
END

DESCRIPTOR(TextureViewDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	TextureFormat format;
	TextureViewDimension dimension;
	uint32_t baseMipLevel;
	uint32_t mipLevelCount;
	uint32_t baseArrayLayer;
	uint32_t arrayLayerCount;
	TextureAspect aspect;
	TextureUsage usage;
	TextureViewDescriptor& setDefault();
	TextureViewDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	TextureViewDescriptor& setLabel(StringView label);
	TextureViewDescriptor& setFormat(TextureFormat format);
	TextureViewDescriptor& setDimension(TextureViewDimension dimension);
	TextureViewDescriptor& setBaseMipLevel(uint32_t baseMipLevel);
	TextureViewDescriptor& setMipLevelCount(uint32_t mipLevelCount);
	TextureViewDescriptor& setBaseArrayLayer(uint32_t baseArrayLayer);
	TextureViewDescriptor& setArrayLayerCount(uint32_t arrayLayerCount);
	TextureViewDescriptor& setAspect(TextureAspect aspect);
	TextureViewDescriptor& setUsage(TextureUsage usage);
END

DESCRIPTOR(BindGroupDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	raw::BindGroupLayout layout;
	size_t entryCount;
	const BindGroupEntry * entries;
	BindGroupDescriptor& setDefault();
	BindGroupDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupDescriptor& setLabel(StringView label);
	BindGroupDescriptor& setLayout(raw::BindGroupLayout layout);
	BindGroupDescriptor& setEntries(size_t entryCount, const BindGroupEntry * entries);
	BindGroupDescriptor& setEntries(const std::vector<BindGroupEntry>& entries);
	BindGroupDescriptor& setEntries(const std::span<const BindGroupEntry>& entries);
END

DESCRIPTOR(BindGroupLayoutEntry)
	const ChainedStruct * nextInChain;
	uint32_t binding;
	ShaderStage visibility;
	BufferBindingLayout buffer;
	SamplerBindingLayout sampler;
	TextureBindingLayout texture;
	StorageTextureBindingLayout storageTexture;
	BindGroupLayoutEntry& setDefault();
	BindGroupLayoutEntry& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupLayoutEntry& setBinding(uint32_t binding);
	BindGroupLayoutEntry& setVisibility(ShaderStage visibility);
	BindGroupLayoutEntry& setBuffer(BufferBindingLayout buffer);
	BindGroupLayoutEntry& setSampler(SamplerBindingLayout sampler);
	BindGroupLayoutEntry& setTexture(TextureBindingLayout texture);
	BindGroupLayoutEntry& setStorageTexture(StorageTextureBindingLayout storageTexture);
END

DESCRIPTOR(CompilationInfo)
	const ChainedStruct * nextInChain;
	size_t messageCount;
	const CompilationMessage * messages;
	CompilationInfo& setDefault();
	CompilationInfo& setNextInChain(const ChainedStruct * nextInChain);
	CompilationInfo& setMessages(size_t messageCount, const CompilationMessage * messages);
	CompilationInfo& setMessages(const std::vector<CompilationMessage>& messages);
	CompilationInfo& setMessages(const std::span<const CompilationMessage>& messages);
END

DESCRIPTOR(ComputePassDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	const ComputePassTimestampWrites * timestampWrites;
	ComputePassDescriptor& setDefault();
	ComputePassDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ComputePassDescriptor& setLabel(StringView label);
	ComputePassDescriptor& setTimestampWrites(const ComputePassTimestampWrites * timestampWrites);
END

DESCRIPTOR(DepthStencilState)
	const ChainedStruct * nextInChain;
	TextureFormat format;
	OptionalBool depthWriteEnabled;
	CompareFunction depthCompare;
	StencilFaceState stencilFront;
	StencilFaceState stencilBack;
	uint32_t stencilReadMask;
	uint32_t stencilWriteMask;
	int32_t depthBias;
	float depthBiasSlopeScale;
	float depthBiasClamp;
	DepthStencilState& setDefault();
	DepthStencilState& setNextInChain(const ChainedStruct * nextInChain);
	DepthStencilState& setFormat(TextureFormat format);
	DepthStencilState& setDepthWriteEnabled(OptionalBool depthWriteEnabled);
	DepthStencilState& setDepthCompare(CompareFunction depthCompare);
	DepthStencilState& setStencilFront(StencilFaceState stencilFront);
	DepthStencilState& setStencilBack(StencilFaceState stencilBack);
	DepthStencilState& setStencilReadMask(uint32_t stencilReadMask);
	DepthStencilState& setStencilWriteMask(uint32_t stencilWriteMask);
	DepthStencilState& setDepthBias(int32_t depthBias);
	DepthStencilState& setDepthBiasSlopeScale(float depthBiasSlopeScale);
	DepthStencilState& setDepthBiasClamp(float depthBiasClamp);
END

DESCRIPTOR(DeviceDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	size_t requiredFeatureCount;
	const FeatureName * requiredFeatures;
	const Limits * requiredLimits;
	QueueDescriptor defaultQueue;
	DeviceLostCallbackInfo deviceLostCallbackInfo;
	UncapturedErrorCallbackInfo uncapturedErrorCallbackInfo;
	DeviceDescriptor& setDefault();
	DeviceDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	DeviceDescriptor& setLabel(StringView label);
	DeviceDescriptor& setRequiredFeatures(size_t requiredFeatureCount, const FeatureName * requiredFeatures);
	DeviceDescriptor& setRequiredFeatures(const std::vector<FeatureName>& requiredFeatures);
	DeviceDescriptor& setRequiredFeatures(const std::span<const FeatureName>& requiredFeatures);
	DeviceDescriptor& setRequiredLimits(const Limits * requiredLimits);
	DeviceDescriptor& setDefaultQueue(QueueDescriptor defaultQueue);
	DeviceDescriptor& setDeviceLostCallbackInfo(DeviceLostCallbackInfo deviceLostCallbackInfo);
	DeviceDescriptor& setUncapturedErrorCallbackInfo(UncapturedErrorCallbackInfo uncapturedErrorCallbackInfo);
END

DESCRIPTOR(InstanceDescriptor)
	const ChainedStruct * nextInChain;
	InstanceCapabilities features;
	InstanceDescriptor& setDefault();
	InstanceDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	InstanceDescriptor& setFeatures(InstanceCapabilities features);
END

DESCRIPTOR(ProgrammableStageDescriptor)
	const ChainedStruct * nextInChain;
	raw::ShaderModule module;
	StringView entryPoint;
	size_t constantCount;
	const ConstantEntry * constants;
	ProgrammableStageDescriptor& setDefault();
	ProgrammableStageDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ProgrammableStageDescriptor& setModule(raw::ShaderModule module);
	ProgrammableStageDescriptor& setEntryPoint(StringView entryPoint);
	ProgrammableStageDescriptor& setConstants(size_t constantCount, const ConstantEntry * constants);
	ProgrammableStageDescriptor& setConstants(const std::vector<ConstantEntry>& constants);
	ProgrammableStageDescriptor& setConstants(const std::span<const ConstantEntry>& constants);
END

DESCRIPTOR(RenderPassColorAttachment)
	const ChainedStruct * nextInChain;
	raw::TextureView view;
	uint32_t depthSlice;
	raw::TextureView resolveTarget;
	LoadOp loadOp;
	StoreOp storeOp;
	Color clearValue;
	RenderPassColorAttachment& setDefault();
	RenderPassColorAttachment& setNextInChain(const ChainedStruct * nextInChain);
	RenderPassColorAttachment& setView(raw::TextureView view);
	RenderPassColorAttachment& setDepthSlice(uint32_t depthSlice);
	RenderPassColorAttachment& setResolveTarget(raw::TextureView resolveTarget);
	RenderPassColorAttachment& setLoadOp(LoadOp loadOp);
	RenderPassColorAttachment& setStoreOp(StoreOp storeOp);
	RenderPassColorAttachment& setClearValue(Color clearValue);
END

DESCRIPTOR(TextureDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	TextureUsage usage;
	TextureDimension dimension;
	Extent3D size;
	TextureFormat format;
	uint32_t mipLevelCount;
	uint32_t sampleCount;
	size_t viewFormatCount;
	const TextureFormat * viewFormats;
	TextureDescriptor& setDefault();
	TextureDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	TextureDescriptor& setLabel(StringView label);
	TextureDescriptor& setUsage(TextureUsage usage);
	TextureDescriptor& setDimension(TextureDimension dimension);
	TextureDescriptor& setSize(Extent3D size);
	TextureDescriptor& setFormat(TextureFormat format);
	TextureDescriptor& setMipLevelCount(uint32_t mipLevelCount);
	TextureDescriptor& setSampleCount(uint32_t sampleCount);
	TextureDescriptor& setViewFormats(size_t viewFormatCount, const TextureFormat * viewFormats);
	TextureDescriptor& setViewFormats(const std::vector<TextureFormat>& viewFormats);
	TextureDescriptor& setViewFormats(const std::span<const TextureFormat>& viewFormats);
END

DESCRIPTOR(BindGroupLayoutDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	size_t entryCount;
	const BindGroupLayoutEntry * entries;
	BindGroupLayoutDescriptor& setDefault();
	BindGroupLayoutDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupLayoutDescriptor& setLabel(StringView label);
	BindGroupLayoutDescriptor& setEntries(size_t entryCount, const BindGroupLayoutEntry * entries);
	BindGroupLayoutDescriptor& setEntries(const std::vector<BindGroupLayoutEntry>& entries);
	BindGroupLayoutDescriptor& setEntries(const std::span<const BindGroupLayoutEntry>& entries);
END

DESCRIPTOR(ColorTargetState)
	const ChainedStruct * nextInChain;
	TextureFormat format;
	const BlendState * blend;
	ColorWriteMask writeMask;
	ColorTargetState& setDefault();
	ColorTargetState& setNextInChain(const ChainedStruct * nextInChain);
	ColorTargetState& setFormat(TextureFormat format);
	ColorTargetState& setBlend(const BlendState * blend);
	ColorTargetState& setWriteMask(ColorWriteMask writeMask);
END

DESCRIPTOR(ComputePipelineDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	raw::PipelineLayout layout;
	ProgrammableStageDescriptor compute;
	ComputePipelineDescriptor& setDefault();
	ComputePipelineDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ComputePipelineDescriptor& setLabel(StringView label);
	ComputePipelineDescriptor& setLayout(raw::PipelineLayout layout);
	ComputePipelineDescriptor& setCompute(ProgrammableStageDescriptor compute);
END

DESCRIPTOR(RenderPassDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	size_t colorAttachmentCount;
	const RenderPassColorAttachment * colorAttachments;
	const RenderPassDepthStencilAttachment * depthStencilAttachment;
	raw::QuerySet occlusionQuerySet;
	const RenderPassTimestampWrites * timestampWrites;
	RenderPassDescriptor& setDefault();
	RenderPassDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderPassDescriptor& setLabel(StringView label);
	RenderPassDescriptor& setColorAttachments(size_t colorAttachmentCount, const RenderPassColorAttachment * colorAttachments);
	RenderPassDescriptor& setColorAttachments(const std::vector<RenderPassColorAttachment>& colorAttachments);
	RenderPassDescriptor& setColorAttachments(const std::span<const RenderPassColorAttachment>& colorAttachments);
	RenderPassDescriptor& setDepthStencilAttachment(const RenderPassDepthStencilAttachment * depthStencilAttachment);
	RenderPassDescriptor& setOcclusionQuerySet(raw::QuerySet occlusionQuerySet);
	RenderPassDescriptor& setTimestampWrites(const RenderPassTimestampWrites * timestampWrites);
END

DESCRIPTOR(VertexState)
	const ChainedStruct * nextInChain;
	raw::ShaderModule module;
	StringView entryPoint;
	size_t constantCount;
	const ConstantEntry * constants;
	size_t bufferCount;
	const VertexBufferLayout * buffers;
	VertexState& setDefault();
	VertexState& setNextInChain(const ChainedStruct * nextInChain);
	VertexState& setModule(raw::ShaderModule module);
	VertexState& setEntryPoint(StringView entryPoint);
	VertexState& setConstants(size_t constantCount, const ConstantEntry * constants);
	VertexState& setConstants(const std::vector<ConstantEntry>& constants);
	VertexState& setConstants(const std::span<const ConstantEntry>& constants);
	VertexState& setBuffers(size_t bufferCount, const VertexBufferLayout * buffers);
	VertexState& setBuffers(const std::vector<VertexBufferLayout>& buffers);
	VertexState& setBuffers(const std::span<const VertexBufferLayout>& buffers);
END

DESCRIPTOR(FragmentState)
	const ChainedStruct * nextInChain;
	raw::ShaderModule module;
	StringView entryPoint;
	size_t constantCount;
	const ConstantEntry * constants;
	size_t targetCount;
	const ColorTargetState * targets;
	FragmentState& setDefault();
	FragmentState& setNextInChain(const ChainedStruct * nextInChain);
	FragmentState& setModule(raw::ShaderModule module);
	FragmentState& setEntryPoint(StringView entryPoint);
	FragmentState& setConstants(size_t constantCount, const ConstantEntry * constants);
	FragmentState& setConstants(const std::vector<ConstantEntry>& constants);
	FragmentState& setConstants(const std::span<const ConstantEntry>& constants);
	FragmentState& setTargets(size_t targetCount, const ColorTargetState * targets);
	FragmentState& setTargets(const std::vector<ColorTargetState>& targets);
	FragmentState& setTargets(const std::span<const ColorTargetState>& targets);
END

DESCRIPTOR(RenderPipelineDescriptor)
	const ChainedStruct * nextInChain;
	StringView label;
	raw::PipelineLayout layout;
	VertexState vertex;
	PrimitiveState primitive;
	const DepthStencilState * depthStencil;
	MultisampleState multisample;
	const FragmentState * fragment;
	RenderPipelineDescriptor& setDefault();
	RenderPipelineDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderPipelineDescriptor& setLabel(StringView label);
	RenderPipelineDescriptor& setLayout(raw::PipelineLayout layout);
	RenderPipelineDescriptor& setVertex(VertexState vertex);
	RenderPipelineDescriptor& setPrimitive(PrimitiveState primitive);
	RenderPipelineDescriptor& setDepthStencil(const DepthStencilState * depthStencil);
	RenderPipelineDescriptor& setMultisample(MultisampleState multisample);
	RenderPipelineDescriptor& setFragment(const FragmentState * fragment);
END

DESCRIPTOR(InstanceEnumerateAdapterOptions)
	const ChainedStruct * nextInChain;
	InstanceBackend backends;
	InstanceEnumerateAdapterOptions& setDefault();
	InstanceEnumerateAdapterOptions& setNextInChain(const ChainedStruct * nextInChain);
	InstanceEnumerateAdapterOptions& setBackends(InstanceBackend backends);
END

} // namespace wgpu


// Callback types
namespace wgpu {
using UncapturedErrorCallback = WGPUUncapturedErrorCallback;
using CreateRenderPipelineAsyncCallback = WGPUCreateRenderPipelineAsyncCallback;
using LogCallback = WGPULogCallback;
using CreateComputePipelineAsyncCallback = WGPUCreateComputePipelineAsyncCallback;
using PopErrorScopeCallback = WGPUPopErrorScopeCallback;
using DeviceLostCallback = WGPUDeviceLostCallback;
using RequestAdapterCallback = WGPURequestAdapterCallback;
using CompilationInfoCallback = WGPUCompilationInfoCallback;
using RequestDeviceCallback = WGPURequestDeviceCallback;
using QueueWorkDoneCallback = WGPUQueueWorkDoneCallback;
using BufferMapCallback = WGPUBufferMapCallback;
} // namespace wgpu


// Non-member procedures
namespace wgpu {

} // namespace wgpu


#ifdef WEBGPU_CPP_NAMESPACE
namespace WEBGPU_CPP_NAMESPACE
#endif
{
using InstanceHandle = 
#ifdef WEBGPU_CPP_USE_RAW_NAMESPACE
raw::Instance;
#else
Instance;
#endif

InstanceHandle createInstance();
InstanceHandle createInstance(const InstanceDescriptor& descriptor);
}

}


// Implementations
namespace wgpu {
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

} // namespace wgpu

namespace wgpu {
// Methods of StringView
StringView& StringView::setData(const char * data) {
	this->data = data;
	return *this;
}
StringView& StringView::setLength(size_t length) {
	this->length = length;
	return *this;
}


// Methods of ChainedStruct
ChainedStruct& ChainedStruct::setNext(const struct WGPUChainedStruct * next) {
	this->next = next;
	return *this;
}
ChainedStruct& ChainedStruct::setSType(SType sType) {
	this->sType = sType;
	return *this;
}


// Methods of ChainedStructOut
ChainedStructOut& ChainedStructOut::setNext(struct WGPUChainedStructOut * next) {
	this->next = next;
	return *this;
}
ChainedStructOut& ChainedStructOut::setSType(SType sType) {
	this->sType = sType;
	return *this;
}


// Methods of BufferMapCallbackInfo
BufferMapCallbackInfo& BufferMapCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BufferMapCallbackInfo& BufferMapCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
BufferMapCallbackInfo& BufferMapCallbackInfo::setCallback(WGPUBufferMapCallback callback) {
	this->callback = callback;
	return *this;
}
BufferMapCallbackInfo& BufferMapCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
BufferMapCallbackInfo& BufferMapCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of CompilationInfoCallbackInfo
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setCallback(WGPUCompilationInfoCallback callback) {
	this->callback = callback;
	return *this;
}
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of CreateComputePipelineAsyncCallbackInfo
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setCallback(WGPUCreateComputePipelineAsyncCallback callback) {
	this->callback = callback;
	return *this;
}
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of CreateRenderPipelineAsyncCallbackInfo
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setCallback(WGPUCreateRenderPipelineAsyncCallback callback) {
	this->callback = callback;
	return *this;
}
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of DeviceLostCallbackInfo
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setCallback(WGPUDeviceLostCallback callback) {
	this->callback = callback;
	return *this;
}
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of PopErrorScopeCallbackInfo
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setCallback(WGPUPopErrorScopeCallback callback) {
	this->callback = callback;
	return *this;
}
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of QueueWorkDoneCallbackInfo
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setCallback(WGPUQueueWorkDoneCallback callback) {
	this->callback = callback;
	return *this;
}
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of RequestAdapterCallbackInfo
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setCallback(WGPURequestAdapterCallback callback) {
	this->callback = callback;
	return *this;
}
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of RequestDeviceCallbackInfo
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setMode(CallbackMode mode) {
	this->mode = mode;
	return *this;
}
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setCallback(WGPURequestDeviceCallback callback) {
	this->callback = callback;
	return *this;
}
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of UncapturedErrorCallbackInfo
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setCallback(WGPUUncapturedErrorCallback callback) {
	this->callback = callback;
	return *this;
}
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setUserdata1(void * userdata1) {
	this->userdata1 = userdata1;
	return *this;
}
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setUserdata2(void * userdata2) {
	this->userdata2 = userdata2;
	return *this;
}


// Methods of AdapterInfo
AdapterInfo& AdapterInfo::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
AdapterInfo& AdapterInfo::setVendor(StringView vendor) {
	this->vendor = vendor;
	return *this;
}
AdapterInfo& AdapterInfo::setArchitecture(StringView architecture) {
	this->architecture = architecture;
	return *this;
}
AdapterInfo& AdapterInfo::setDevice(StringView device) {
	this->device = device;
	return *this;
}
AdapterInfo& AdapterInfo::setDescription(StringView description) {
	this->description = description;
	return *this;
}
AdapterInfo& AdapterInfo::setBackendType(BackendType backendType) {
	this->backendType = backendType;
	return *this;
}
AdapterInfo& AdapterInfo::setAdapterType(AdapterType adapterType) {
	this->adapterType = adapterType;
	return *this;
}
AdapterInfo& AdapterInfo::setVendorID(uint32_t vendorID) {
	this->vendorID = vendorID;
	return *this;
}
AdapterInfo& AdapterInfo::setDeviceID(uint32_t deviceID) {
	this->deviceID = deviceID;
	return *this;
}
void AdapterInfo::freeMembers() {
	return wgpuAdapterInfoFreeMembers(*this);
}


// Methods of BindGroupEntry
BindGroupEntry& BindGroupEntry::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BindGroupEntry& BindGroupEntry::setBinding(uint32_t binding) {
	this->binding = binding;
	return *this;
}
BindGroupEntry& BindGroupEntry::setBuffer(raw::Buffer buffer) {
	this->buffer = buffer;
	return *this;
}
BindGroupEntry& BindGroupEntry::setOffset(uint64_t offset) {
	this->offset = offset;
	return *this;
}
BindGroupEntry& BindGroupEntry::setSize(uint64_t size) {
	this->size = size;
	return *this;
}
BindGroupEntry& BindGroupEntry::setSampler(raw::Sampler sampler) {
	this->sampler = sampler;
	return *this;
}
BindGroupEntry& BindGroupEntry::setTextureView(raw::TextureView textureView) {
	this->textureView = textureView;
	return *this;
}


// Methods of BlendComponent
BlendComponent& BlendComponent::setOperation(BlendOperation operation) {
	this->operation = operation;
	return *this;
}
BlendComponent& BlendComponent::setSrcFactor(BlendFactor srcFactor) {
	this->srcFactor = srcFactor;
	return *this;
}
BlendComponent& BlendComponent::setDstFactor(BlendFactor dstFactor) {
	this->dstFactor = dstFactor;
	return *this;
}


// Methods of BufferBindingLayout
BufferBindingLayout& BufferBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BufferBindingLayout& BufferBindingLayout::setType(BufferBindingType type) {
	this->type = type;
	return *this;
}
BufferBindingLayout& BufferBindingLayout::setHasDynamicOffset(WGPUBool hasDynamicOffset) {
	this->hasDynamicOffset = hasDynamicOffset;
	return *this;
}
BufferBindingLayout& BufferBindingLayout::setMinBindingSize(uint64_t minBindingSize) {
	this->minBindingSize = minBindingSize;
	return *this;
}


// Methods of BufferDescriptor
BufferDescriptor& BufferDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BufferDescriptor& BufferDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
BufferDescriptor& BufferDescriptor::setUsage(BufferUsage usage) {
	this->usage = usage;
	return *this;
}
BufferDescriptor& BufferDescriptor::setSize(uint64_t size) {
	this->size = size;
	return *this;
}
BufferDescriptor& BufferDescriptor::setMappedAtCreation(WGPUBool mappedAtCreation) {
	this->mappedAtCreation = mappedAtCreation;
	return *this;
}


// Methods of Color
Color& Color::setR(double r) {
	this->r = r;
	return *this;
}
Color& Color::setG(double g) {
	this->g = g;
	return *this;
}
Color& Color::setB(double b) {
	this->b = b;
	return *this;
}
Color& Color::setA(double a) {
	this->a = a;
	return *this;
}


// Methods of CommandBufferDescriptor
CommandBufferDescriptor& CommandBufferDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CommandBufferDescriptor& CommandBufferDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of CommandEncoderDescriptor
CommandEncoderDescriptor& CommandEncoderDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CommandEncoderDescriptor& CommandEncoderDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of CompilationMessage
CompilationMessage& CompilationMessage::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CompilationMessage& CompilationMessage::setMessage(StringView message) {
	this->message = message;
	return *this;
}
CompilationMessage& CompilationMessage::setType(CompilationMessageType type) {
	this->type = type;
	return *this;
}
CompilationMessage& CompilationMessage::setLineNum(uint64_t lineNum) {
	this->lineNum = lineNum;
	return *this;
}
CompilationMessage& CompilationMessage::setLinePos(uint64_t linePos) {
	this->linePos = linePos;
	return *this;
}
CompilationMessage& CompilationMessage::setOffset(uint64_t offset) {
	this->offset = offset;
	return *this;
}
CompilationMessage& CompilationMessage::setLength(uint64_t length) {
	this->length = length;
	return *this;
}


// Methods of ComputePassTimestampWrites
ComputePassTimestampWrites& ComputePassTimestampWrites::setQuerySet(raw::QuerySet querySet) {
	this->querySet = querySet;
	return *this;
}
ComputePassTimestampWrites& ComputePassTimestampWrites::setBeginningOfPassWriteIndex(uint32_t beginningOfPassWriteIndex) {
	this->beginningOfPassWriteIndex = beginningOfPassWriteIndex;
	return *this;
}
ComputePassTimestampWrites& ComputePassTimestampWrites::setEndOfPassWriteIndex(uint32_t endOfPassWriteIndex) {
	this->endOfPassWriteIndex = endOfPassWriteIndex;
	return *this;
}


// Methods of ConstantEntry
ConstantEntry& ConstantEntry::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
ConstantEntry& ConstantEntry::setKey(StringView key) {
	this->key = key;
	return *this;
}
ConstantEntry& ConstantEntry::setValue(double value) {
	this->value = value;
	return *this;
}


// Methods of Extent3D
Extent3D& Extent3D::setWidth(uint32_t width) {
	this->width = width;
	return *this;
}
Extent3D& Extent3D::setHeight(uint32_t height) {
	this->height = height;
	return *this;
}
Extent3D& Extent3D::setDepthOrArrayLayers(uint32_t depthOrArrayLayers) {
	this->depthOrArrayLayers = depthOrArrayLayers;
	return *this;
}


// Methods of Future
Future& Future::setId(uint64_t id) {
	this->id = id;
	return *this;
}


// Methods of InstanceCapabilities
InstanceCapabilities& InstanceCapabilities::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
InstanceCapabilities& InstanceCapabilities::setTimedWaitAnyEnable(WGPUBool timedWaitAnyEnable) {
	this->timedWaitAnyEnable = timedWaitAnyEnable;
	return *this;
}
InstanceCapabilities& InstanceCapabilities::setTimedWaitAnyMaxCount(size_t timedWaitAnyMaxCount) {
	this->timedWaitAnyMaxCount = timedWaitAnyMaxCount;
	return *this;
}


// Methods of Limits
Limits& Limits::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
Limits& Limits::setMaxTextureDimension1D(uint32_t maxTextureDimension1D) {
	this->maxTextureDimension1D = maxTextureDimension1D;
	return *this;
}
Limits& Limits::setMaxTextureDimension2D(uint32_t maxTextureDimension2D) {
	this->maxTextureDimension2D = maxTextureDimension2D;
	return *this;
}
Limits& Limits::setMaxTextureDimension3D(uint32_t maxTextureDimension3D) {
	this->maxTextureDimension3D = maxTextureDimension3D;
	return *this;
}
Limits& Limits::setMaxTextureArrayLayers(uint32_t maxTextureArrayLayers) {
	this->maxTextureArrayLayers = maxTextureArrayLayers;
	return *this;
}
Limits& Limits::setMaxBindGroups(uint32_t maxBindGroups) {
	this->maxBindGroups = maxBindGroups;
	return *this;
}
Limits& Limits::setMaxBindGroupsPlusVertexBuffers(uint32_t maxBindGroupsPlusVertexBuffers) {
	this->maxBindGroupsPlusVertexBuffers = maxBindGroupsPlusVertexBuffers;
	return *this;
}
Limits& Limits::setMaxBindingsPerBindGroup(uint32_t maxBindingsPerBindGroup) {
	this->maxBindingsPerBindGroup = maxBindingsPerBindGroup;
	return *this;
}
Limits& Limits::setMaxDynamicUniformBuffersPerPipelineLayout(uint32_t maxDynamicUniformBuffersPerPipelineLayout) {
	this->maxDynamicUniformBuffersPerPipelineLayout = maxDynamicUniformBuffersPerPipelineLayout;
	return *this;
}
Limits& Limits::setMaxDynamicStorageBuffersPerPipelineLayout(uint32_t maxDynamicStorageBuffersPerPipelineLayout) {
	this->maxDynamicStorageBuffersPerPipelineLayout = maxDynamicStorageBuffersPerPipelineLayout;
	return *this;
}
Limits& Limits::setMaxSampledTexturesPerShaderStage(uint32_t maxSampledTexturesPerShaderStage) {
	this->maxSampledTexturesPerShaderStage = maxSampledTexturesPerShaderStage;
	return *this;
}
Limits& Limits::setMaxSamplersPerShaderStage(uint32_t maxSamplersPerShaderStage) {
	this->maxSamplersPerShaderStage = maxSamplersPerShaderStage;
	return *this;
}
Limits& Limits::setMaxStorageBuffersPerShaderStage(uint32_t maxStorageBuffersPerShaderStage) {
	this->maxStorageBuffersPerShaderStage = maxStorageBuffersPerShaderStage;
	return *this;
}
Limits& Limits::setMaxStorageTexturesPerShaderStage(uint32_t maxStorageTexturesPerShaderStage) {
	this->maxStorageTexturesPerShaderStage = maxStorageTexturesPerShaderStage;
	return *this;
}
Limits& Limits::setMaxUniformBuffersPerShaderStage(uint32_t maxUniformBuffersPerShaderStage) {
	this->maxUniformBuffersPerShaderStage = maxUniformBuffersPerShaderStage;
	return *this;
}
Limits& Limits::setMaxUniformBufferBindingSize(uint64_t maxUniformBufferBindingSize) {
	this->maxUniformBufferBindingSize = maxUniformBufferBindingSize;
	return *this;
}
Limits& Limits::setMaxStorageBufferBindingSize(uint64_t maxStorageBufferBindingSize) {
	this->maxStorageBufferBindingSize = maxStorageBufferBindingSize;
	return *this;
}
Limits& Limits::setMinUniformBufferOffsetAlignment(uint32_t minUniformBufferOffsetAlignment) {
	this->minUniformBufferOffsetAlignment = minUniformBufferOffsetAlignment;
	return *this;
}
Limits& Limits::setMinStorageBufferOffsetAlignment(uint32_t minStorageBufferOffsetAlignment) {
	this->minStorageBufferOffsetAlignment = minStorageBufferOffsetAlignment;
	return *this;
}
Limits& Limits::setMaxVertexBuffers(uint32_t maxVertexBuffers) {
	this->maxVertexBuffers = maxVertexBuffers;
	return *this;
}
Limits& Limits::setMaxBufferSize(uint64_t maxBufferSize) {
	this->maxBufferSize = maxBufferSize;
	return *this;
}
Limits& Limits::setMaxVertexAttributes(uint32_t maxVertexAttributes) {
	this->maxVertexAttributes = maxVertexAttributes;
	return *this;
}
Limits& Limits::setMaxVertexBufferArrayStride(uint32_t maxVertexBufferArrayStride) {
	this->maxVertexBufferArrayStride = maxVertexBufferArrayStride;
	return *this;
}
Limits& Limits::setMaxInterStageShaderVariables(uint32_t maxInterStageShaderVariables) {
	this->maxInterStageShaderVariables = maxInterStageShaderVariables;
	return *this;
}
Limits& Limits::setMaxColorAttachments(uint32_t maxColorAttachments) {
	this->maxColorAttachments = maxColorAttachments;
	return *this;
}
Limits& Limits::setMaxColorAttachmentBytesPerSample(uint32_t maxColorAttachmentBytesPerSample) {
	this->maxColorAttachmentBytesPerSample = maxColorAttachmentBytesPerSample;
	return *this;
}
Limits& Limits::setMaxComputeWorkgroupStorageSize(uint32_t maxComputeWorkgroupStorageSize) {
	this->maxComputeWorkgroupStorageSize = maxComputeWorkgroupStorageSize;
	return *this;
}
Limits& Limits::setMaxComputeInvocationsPerWorkgroup(uint32_t maxComputeInvocationsPerWorkgroup) {
	this->maxComputeInvocationsPerWorkgroup = maxComputeInvocationsPerWorkgroup;
	return *this;
}
Limits& Limits::setMaxComputeWorkgroupSizeX(uint32_t maxComputeWorkgroupSizeX) {
	this->maxComputeWorkgroupSizeX = maxComputeWorkgroupSizeX;
	return *this;
}
Limits& Limits::setMaxComputeWorkgroupSizeY(uint32_t maxComputeWorkgroupSizeY) {
	this->maxComputeWorkgroupSizeY = maxComputeWorkgroupSizeY;
	return *this;
}
Limits& Limits::setMaxComputeWorkgroupSizeZ(uint32_t maxComputeWorkgroupSizeZ) {
	this->maxComputeWorkgroupSizeZ = maxComputeWorkgroupSizeZ;
	return *this;
}
Limits& Limits::setMaxComputeWorkgroupsPerDimension(uint32_t maxComputeWorkgroupsPerDimension) {
	this->maxComputeWorkgroupsPerDimension = maxComputeWorkgroupsPerDimension;
	return *this;
}


// Methods of MultisampleState
MultisampleState& MultisampleState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
MultisampleState& MultisampleState::setCount(uint32_t count) {
	this->count = count;
	return *this;
}
MultisampleState& MultisampleState::setMask(uint32_t mask) {
	this->mask = mask;
	return *this;
}
MultisampleState& MultisampleState::setAlphaToCoverageEnabled(WGPUBool alphaToCoverageEnabled) {
	this->alphaToCoverageEnabled = alphaToCoverageEnabled;
	return *this;
}


// Methods of Origin3D
Origin3D& Origin3D::setX(uint32_t x) {
	this->x = x;
	return *this;
}
Origin3D& Origin3D::setY(uint32_t y) {
	this->y = y;
	return *this;
}
Origin3D& Origin3D::setZ(uint32_t z) {
	this->z = z;
	return *this;
}


// Methods of PipelineLayoutDescriptor
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(size_t bindGroupLayoutCount, const raw::BindGroupLayout * bindGroupLayouts) {
	this->bindGroupLayoutCount = bindGroupLayoutCount;
	this->bindGroupLayouts = bindGroupLayouts;
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(const std::vector<raw::BindGroupLayout>& bindGroupLayouts) {
	this->bindGroupLayoutCount = static_cast<size_t>(bindGroupLayouts.size());
	this->bindGroupLayouts = reinterpret_cast<const raw::BindGroupLayout *>(bindGroupLayouts.data());
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(const std::span<const raw::BindGroupLayout>& bindGroupLayouts) {
	this->bindGroupLayoutCount = static_cast<size_t>(bindGroupLayouts.size());
	this->bindGroupLayouts = reinterpret_cast<const raw::BindGroupLayout *>(bindGroupLayouts.data());
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(const std::vector<BindGroupLayout>& bindGroupLayouts) {
	this->bindGroupLayoutCount = static_cast<size_t>(bindGroupLayouts.size());
	this->bindGroupLayouts = reinterpret_cast<const raw::BindGroupLayout *>(bindGroupLayouts.data());
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(const std::span<const BindGroupLayout>& bindGroupLayouts) {
	this->bindGroupLayoutCount = static_cast<size_t>(bindGroupLayouts.size());
	this->bindGroupLayouts = reinterpret_cast<const raw::BindGroupLayout *>(bindGroupLayouts.data());
	return *this;
}


// Methods of PrimitiveState
PrimitiveState& PrimitiveState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
PrimitiveState& PrimitiveState::setTopology(PrimitiveTopology topology) {
	this->topology = topology;
	return *this;
}
PrimitiveState& PrimitiveState::setStripIndexFormat(IndexFormat stripIndexFormat) {
	this->stripIndexFormat = stripIndexFormat;
	return *this;
}
PrimitiveState& PrimitiveState::setFrontFace(FrontFace frontFace) {
	this->frontFace = frontFace;
	return *this;
}
PrimitiveState& PrimitiveState::setCullMode(CullMode cullMode) {
	this->cullMode = cullMode;
	return *this;
}
PrimitiveState& PrimitiveState::setUnclippedDepth(WGPUBool unclippedDepth) {
	this->unclippedDepth = unclippedDepth;
	return *this;
}


// Methods of QuerySetDescriptor
QuerySetDescriptor& QuerySetDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setType(QueryType type) {
	this->type = type;
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setCount(uint32_t count) {
	this->count = count;
	return *this;
}


// Methods of QueueDescriptor
QueueDescriptor& QueueDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
QueueDescriptor& QueueDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of RenderBundleDescriptor
RenderBundleDescriptor& RenderBundleDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RenderBundleDescriptor& RenderBundleDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of RenderBundleEncoderDescriptor
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setColorFormats(size_t colorFormatCount, const TextureFormat * colorFormats) {
	this->colorFormatCount = colorFormatCount;
	this->colorFormats = colorFormats;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setColorFormats(const std::vector<TextureFormat>& colorFormats) {
	this->colorFormatCount = static_cast<size_t>(colorFormats.size());
	this->colorFormats = reinterpret_cast<const TextureFormat *>(colorFormats.data());
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setColorFormats(const std::span<const TextureFormat>& colorFormats) {
	this->colorFormatCount = static_cast<size_t>(colorFormats.size());
	this->colorFormats = reinterpret_cast<const TextureFormat *>(colorFormats.data());
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setDepthStencilFormat(TextureFormat depthStencilFormat) {
	this->depthStencilFormat = depthStencilFormat;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setSampleCount(uint32_t sampleCount) {
	this->sampleCount = sampleCount;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setDepthReadOnly(WGPUBool depthReadOnly) {
	this->depthReadOnly = depthReadOnly;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setStencilReadOnly(WGPUBool stencilReadOnly) {
	this->stencilReadOnly = stencilReadOnly;
	return *this;
}


// Methods of RenderPassDepthStencilAttachment
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setView(raw::TextureView view) {
	this->view = view;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDepthLoadOp(LoadOp depthLoadOp) {
	this->depthLoadOp = depthLoadOp;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDepthStoreOp(StoreOp depthStoreOp) {
	this->depthStoreOp = depthStoreOp;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDepthClearValue(float depthClearValue) {
	this->depthClearValue = depthClearValue;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDepthReadOnly(WGPUBool depthReadOnly) {
	this->depthReadOnly = depthReadOnly;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setStencilLoadOp(LoadOp stencilLoadOp) {
	this->stencilLoadOp = stencilLoadOp;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setStencilStoreOp(StoreOp stencilStoreOp) {
	this->stencilStoreOp = stencilStoreOp;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setStencilClearValue(uint32_t stencilClearValue) {
	this->stencilClearValue = stencilClearValue;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setStencilReadOnly(WGPUBool stencilReadOnly) {
	this->stencilReadOnly = stencilReadOnly;
	return *this;
}


// Methods of RenderPassMaxDrawCount
RenderPassMaxDrawCount& RenderPassMaxDrawCount::setMaxDrawCount(uint64_t maxDrawCount) {
	this->maxDrawCount = maxDrawCount;
	return *this;
}


// Methods of RenderPassTimestampWrites
RenderPassTimestampWrites& RenderPassTimestampWrites::setQuerySet(raw::QuerySet querySet) {
	this->querySet = querySet;
	return *this;
}
RenderPassTimestampWrites& RenderPassTimestampWrites::setBeginningOfPassWriteIndex(uint32_t beginningOfPassWriteIndex) {
	this->beginningOfPassWriteIndex = beginningOfPassWriteIndex;
	return *this;
}
RenderPassTimestampWrites& RenderPassTimestampWrites::setEndOfPassWriteIndex(uint32_t endOfPassWriteIndex) {
	this->endOfPassWriteIndex = endOfPassWriteIndex;
	return *this;
}


// Methods of RequestAdapterOptions
RequestAdapterOptions& RequestAdapterOptions::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setFeatureLevel(FeatureLevel featureLevel) {
	this->featureLevel = featureLevel;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setPowerPreference(PowerPreference powerPreference) {
	this->powerPreference = powerPreference;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setForceFallbackAdapter(WGPUBool forceFallbackAdapter) {
	this->forceFallbackAdapter = forceFallbackAdapter;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setBackendType(BackendType backendType) {
	this->backendType = backendType;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setCompatibleSurface(raw::Surface compatibleSurface) {
	this->compatibleSurface = compatibleSurface;
	return *this;
}


// Methods of SamplerBindingLayout
SamplerBindingLayout& SamplerBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
SamplerBindingLayout& SamplerBindingLayout::setType(SamplerBindingType type) {
	this->type = type;
	return *this;
}


// Methods of SamplerDescriptor
SamplerDescriptor& SamplerDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setAddressModeU(AddressMode addressModeU) {
	this->addressModeU = addressModeU;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setAddressModeV(AddressMode addressModeV) {
	this->addressModeV = addressModeV;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setAddressModeW(AddressMode addressModeW) {
	this->addressModeW = addressModeW;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMagFilter(FilterMode magFilter) {
	this->magFilter = magFilter;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMinFilter(FilterMode minFilter) {
	this->minFilter = minFilter;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMipmapFilter(MipmapFilterMode mipmapFilter) {
	this->mipmapFilter = mipmapFilter;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setLodMinClamp(float lodMinClamp) {
	this->lodMinClamp = lodMinClamp;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setLodMaxClamp(float lodMaxClamp) {
	this->lodMaxClamp = lodMaxClamp;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setCompare(CompareFunction compare) {
	this->compare = compare;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMaxAnisotropy(uint16_t maxAnisotropy) {
	this->maxAnisotropy = maxAnisotropy;
	return *this;
}


// Methods of ShaderModuleDescriptor
ShaderModuleDescriptor& ShaderModuleDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
ShaderModuleDescriptor& ShaderModuleDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of ShaderSourceSPIRV
ShaderSourceSPIRV& ShaderSourceSPIRV::setCodeSize(uint32_t codeSize) {
	this->codeSize = codeSize;
	return *this;
}
ShaderSourceSPIRV& ShaderSourceSPIRV::setCode(const uint32_t * code) {
	this->code = code;
	return *this;
}


// Methods of ShaderSourceWGSL
ShaderSourceWGSL& ShaderSourceWGSL::setCode(StringView code) {
	this->code = code;
	return *this;
}


// Methods of StencilFaceState
StencilFaceState& StencilFaceState::setCompare(CompareFunction compare) {
	this->compare = compare;
	return *this;
}
StencilFaceState& StencilFaceState::setFailOp(StencilOperation failOp) {
	this->failOp = failOp;
	return *this;
}
StencilFaceState& StencilFaceState::setDepthFailOp(StencilOperation depthFailOp) {
	this->depthFailOp = depthFailOp;
	return *this;
}
StencilFaceState& StencilFaceState::setPassOp(StencilOperation passOp) {
	this->passOp = passOp;
	return *this;
}


// Methods of StorageTextureBindingLayout
StorageTextureBindingLayout& StorageTextureBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setAccess(StorageTextureAccess access) {
	this->access = access;
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setFormat(TextureFormat format) {
	this->format = format;
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setViewDimension(TextureViewDimension viewDimension) {
	this->viewDimension = viewDimension;
	return *this;
}


// Methods of SupportedFeatures
SupportedFeatures& SupportedFeatures::setFeatures(size_t featureCount, const FeatureName * features) {
	this->featureCount = featureCount;
	this->features = features;
	return *this;
}
SupportedFeatures& SupportedFeatures::setFeatures(const std::vector<FeatureName>& features) {
	this->featureCount = static_cast<size_t>(features.size());
	this->features = reinterpret_cast<const FeatureName *>(features.data());
	return *this;
}
SupportedFeatures& SupportedFeatures::setFeatures(const std::span<const FeatureName>& features) {
	this->featureCount = static_cast<size_t>(features.size());
	this->features = reinterpret_cast<const FeatureName *>(features.data());
	return *this;
}
void SupportedFeatures::freeMembers() {
	return wgpuSupportedFeaturesFreeMembers(*this);
}


// Methods of SupportedWGSLLanguageFeatures
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setFeatures(size_t featureCount, const WGSLLanguageFeatureName * features) {
	this->featureCount = featureCount;
	this->features = features;
	return *this;
}
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setFeatures(const std::vector<WGSLLanguageFeatureName>& features) {
	this->featureCount = static_cast<size_t>(features.size());
	this->features = reinterpret_cast<const WGSLLanguageFeatureName *>(features.data());
	return *this;
}
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setFeatures(const std::span<const WGSLLanguageFeatureName>& features) {
	this->featureCount = static_cast<size_t>(features.size());
	this->features = reinterpret_cast<const WGSLLanguageFeatureName *>(features.data());
	return *this;
}
void SupportedWGSLLanguageFeatures::freeMembers() {
	return wgpuSupportedWGSLLanguageFeaturesFreeMembers(*this);
}


// Methods of SurfaceCapabilities
SurfaceCapabilities& SurfaceCapabilities::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setUsages(TextureUsage usages) {
	this->usages = usages;
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setFormats(size_t formatCount, const TextureFormat * formats) {
	this->formatCount = formatCount;
	this->formats = formats;
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setFormats(const std::vector<TextureFormat>& formats) {
	this->formatCount = static_cast<size_t>(formats.size());
	this->formats = reinterpret_cast<const TextureFormat *>(formats.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setFormats(const std::span<const TextureFormat>& formats) {
	this->formatCount = static_cast<size_t>(formats.size());
	this->formats = reinterpret_cast<const TextureFormat *>(formats.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setPresentModes(size_t presentModeCount, const PresentMode * presentModes) {
	this->presentModeCount = presentModeCount;
	this->presentModes = presentModes;
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setPresentModes(const std::vector<PresentMode>& presentModes) {
	this->presentModeCount = static_cast<size_t>(presentModes.size());
	this->presentModes = reinterpret_cast<const PresentMode *>(presentModes.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setPresentModes(const std::span<const PresentMode>& presentModes) {
	this->presentModeCount = static_cast<size_t>(presentModes.size());
	this->presentModes = reinterpret_cast<const PresentMode *>(presentModes.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setAlphaModes(size_t alphaModeCount, const CompositeAlphaMode * alphaModes) {
	this->alphaModeCount = alphaModeCount;
	this->alphaModes = alphaModes;
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setAlphaModes(const std::vector<CompositeAlphaMode>& alphaModes) {
	this->alphaModeCount = static_cast<size_t>(alphaModes.size());
	this->alphaModes = reinterpret_cast<const CompositeAlphaMode *>(alphaModes.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setAlphaModes(const std::span<const CompositeAlphaMode>& alphaModes) {
	this->alphaModeCount = static_cast<size_t>(alphaModes.size());
	this->alphaModes = reinterpret_cast<const CompositeAlphaMode *>(alphaModes.data());
	return *this;
}
void SurfaceCapabilities::freeMembers() {
	return wgpuSurfaceCapabilitiesFreeMembers(*this);
}


// Methods of SurfaceConfiguration
SurfaceConfiguration& SurfaceConfiguration::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setDevice(raw::Device device) {
	this->device = device;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setFormat(TextureFormat format) {
	this->format = format;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setUsage(TextureUsage usage) {
	this->usage = usage;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setWidth(uint32_t width) {
	this->width = width;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setHeight(uint32_t height) {
	this->height = height;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setViewFormats(size_t viewFormatCount, const TextureFormat * viewFormats) {
	this->viewFormatCount = viewFormatCount;
	this->viewFormats = viewFormats;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setViewFormats(const std::vector<TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<size_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<const TextureFormat *>(viewFormats.data());
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setViewFormats(const std::span<const TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<size_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<const TextureFormat *>(viewFormats.data());
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setAlphaMode(CompositeAlphaMode alphaMode) {
	this->alphaMode = alphaMode;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setPresentMode(PresentMode presentMode) {
	this->presentMode = presentMode;
	return *this;
}


// Methods of SurfaceDescriptor
SurfaceDescriptor& SurfaceDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
SurfaceDescriptor& SurfaceDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of SurfaceSourceAndroidNativeWindow
SurfaceSourceAndroidNativeWindow& SurfaceSourceAndroidNativeWindow::setWindow(void * window) {
	this->window = window;
	return *this;
}


// Methods of SurfaceSourceMetalLayer
SurfaceSourceMetalLayer& SurfaceSourceMetalLayer::setLayer(void * layer) {
	this->layer = layer;
	return *this;
}


// Methods of SurfaceSourceWaylandSurface
SurfaceSourceWaylandSurface& SurfaceSourceWaylandSurface::setDisplay(void * display) {
	this->display = display;
	return *this;
}
SurfaceSourceWaylandSurface& SurfaceSourceWaylandSurface::setSurface(void * surface) {
	this->surface = surface;
	return *this;
}


// Methods of SurfaceSourceWindowsHWND
SurfaceSourceWindowsHWND& SurfaceSourceWindowsHWND::setHinstance(void * hinstance) {
	this->hinstance = hinstance;
	return *this;
}
SurfaceSourceWindowsHWND& SurfaceSourceWindowsHWND::setHwnd(void * hwnd) {
	this->hwnd = hwnd;
	return *this;
}


// Methods of SurfaceSourceXCBWindow
SurfaceSourceXCBWindow& SurfaceSourceXCBWindow::setConnection(void * connection) {
	this->connection = connection;
	return *this;
}
SurfaceSourceXCBWindow& SurfaceSourceXCBWindow::setWindow(uint32_t window) {
	this->window = window;
	return *this;
}


// Methods of SurfaceSourceXlibWindow
SurfaceSourceXlibWindow& SurfaceSourceXlibWindow::setDisplay(void * display) {
	this->display = display;
	return *this;
}
SurfaceSourceXlibWindow& SurfaceSourceXlibWindow::setWindow(uint64_t window) {
	this->window = window;
	return *this;
}


// Methods of SurfaceTexture
SurfaceTexture& SurfaceTexture::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
SurfaceTexture& SurfaceTexture::setTexture(raw::Texture texture) {
	this->texture = texture;
	return *this;
}
SurfaceTexture& SurfaceTexture::setStatus(SurfaceGetCurrentTextureStatus status) {
	this->status = status;
	return *this;
}


// Methods of TexelCopyBufferLayout
TexelCopyBufferLayout& TexelCopyBufferLayout::setOffset(uint64_t offset) {
	this->offset = offset;
	return *this;
}
TexelCopyBufferLayout& TexelCopyBufferLayout::setBytesPerRow(uint32_t bytesPerRow) {
	this->bytesPerRow = bytesPerRow;
	return *this;
}
TexelCopyBufferLayout& TexelCopyBufferLayout::setRowsPerImage(uint32_t rowsPerImage) {
	this->rowsPerImage = rowsPerImage;
	return *this;
}


// Methods of TextureBindingLayout
TextureBindingLayout& TextureBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setSampleType(TextureSampleType sampleType) {
	this->sampleType = sampleType;
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setViewDimension(TextureViewDimension viewDimension) {
	this->viewDimension = viewDimension;
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setMultisampled(WGPUBool multisampled) {
	this->multisampled = multisampled;
	return *this;
}


// Methods of TextureViewDescriptor
TextureViewDescriptor& TextureViewDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setFormat(TextureFormat format) {
	this->format = format;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setDimension(TextureViewDimension dimension) {
	this->dimension = dimension;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setBaseMipLevel(uint32_t baseMipLevel) {
	this->baseMipLevel = baseMipLevel;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setMipLevelCount(uint32_t mipLevelCount) {
	this->mipLevelCount = mipLevelCount;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setBaseArrayLayer(uint32_t baseArrayLayer) {
	this->baseArrayLayer = baseArrayLayer;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setArrayLayerCount(uint32_t arrayLayerCount) {
	this->arrayLayerCount = arrayLayerCount;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setAspect(TextureAspect aspect) {
	this->aspect = aspect;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setUsage(TextureUsage usage) {
	this->usage = usage;
	return *this;
}


// Methods of VertexAttribute
VertexAttribute& VertexAttribute::setFormat(VertexFormat format) {
	this->format = format;
	return *this;
}
VertexAttribute& VertexAttribute::setOffset(uint64_t offset) {
	this->offset = offset;
	return *this;
}
VertexAttribute& VertexAttribute::setShaderLocation(uint32_t shaderLocation) {
	this->shaderLocation = shaderLocation;
	return *this;
}


// Methods of BindGroupDescriptor
BindGroupDescriptor& BindGroupDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setLayout(raw::BindGroupLayout layout) {
	this->layout = layout;
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setEntries(size_t entryCount, const BindGroupEntry * entries) {
	this->entryCount = entryCount;
	this->entries = entries;
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setEntries(const std::vector<BindGroupEntry>& entries) {
	this->entryCount = static_cast<size_t>(entries.size());
	this->entries = reinterpret_cast<const BindGroupEntry *>(entries.data());
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setEntries(const std::span<const BindGroupEntry>& entries) {
	this->entryCount = static_cast<size_t>(entries.size());
	this->entries = reinterpret_cast<const BindGroupEntry *>(entries.data());
	return *this;
}


// Methods of BindGroupLayoutEntry
BindGroupLayoutEntry& BindGroupLayoutEntry::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setBinding(uint32_t binding) {
	this->binding = binding;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setVisibility(ShaderStage visibility) {
	this->visibility = visibility;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setBuffer(BufferBindingLayout buffer) {
	this->buffer = buffer;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setSampler(SamplerBindingLayout sampler) {
	this->sampler = sampler;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setTexture(TextureBindingLayout texture) {
	this->texture = texture;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setStorageTexture(StorageTextureBindingLayout storageTexture) {
	this->storageTexture = storageTexture;
	return *this;
}


// Methods of BlendState
BlendState& BlendState::setColor(BlendComponent color) {
	this->color = color;
	return *this;
}
BlendState& BlendState::setAlpha(BlendComponent alpha) {
	this->alpha = alpha;
	return *this;
}


// Methods of CompilationInfo
CompilationInfo& CompilationInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
CompilationInfo& CompilationInfo::setMessages(size_t messageCount, const CompilationMessage * messages) {
	this->messageCount = messageCount;
	this->messages = messages;
	return *this;
}
CompilationInfo& CompilationInfo::setMessages(const std::vector<CompilationMessage>& messages) {
	this->messageCount = static_cast<size_t>(messages.size());
	this->messages = reinterpret_cast<const CompilationMessage *>(messages.data());
	return *this;
}
CompilationInfo& CompilationInfo::setMessages(const std::span<const CompilationMessage>& messages) {
	this->messageCount = static_cast<size_t>(messages.size());
	this->messages = reinterpret_cast<const CompilationMessage *>(messages.data());
	return *this;
}


// Methods of ComputePassDescriptor
ComputePassDescriptor& ComputePassDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
ComputePassDescriptor& ComputePassDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
ComputePassDescriptor& ComputePassDescriptor::setTimestampWrites(const ComputePassTimestampWrites * timestampWrites) {
	this->timestampWrites = timestampWrites;
	return *this;
}


// Methods of DepthStencilState
DepthStencilState& DepthStencilState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
DepthStencilState& DepthStencilState::setFormat(TextureFormat format) {
	this->format = format;
	return *this;
}
DepthStencilState& DepthStencilState::setDepthWriteEnabled(OptionalBool depthWriteEnabled) {
	this->depthWriteEnabled = depthWriteEnabled;
	return *this;
}
DepthStencilState& DepthStencilState::setDepthCompare(CompareFunction depthCompare) {
	this->depthCompare = depthCompare;
	return *this;
}
DepthStencilState& DepthStencilState::setStencilFront(StencilFaceState stencilFront) {
	this->stencilFront = stencilFront;
	return *this;
}
DepthStencilState& DepthStencilState::setStencilBack(StencilFaceState stencilBack) {
	this->stencilBack = stencilBack;
	return *this;
}
DepthStencilState& DepthStencilState::setStencilReadMask(uint32_t stencilReadMask) {
	this->stencilReadMask = stencilReadMask;
	return *this;
}
DepthStencilState& DepthStencilState::setStencilWriteMask(uint32_t stencilWriteMask) {
	this->stencilWriteMask = stencilWriteMask;
	return *this;
}
DepthStencilState& DepthStencilState::setDepthBias(int32_t depthBias) {
	this->depthBias = depthBias;
	return *this;
}
DepthStencilState& DepthStencilState::setDepthBiasSlopeScale(float depthBiasSlopeScale) {
	this->depthBiasSlopeScale = depthBiasSlopeScale;
	return *this;
}
DepthStencilState& DepthStencilState::setDepthBiasClamp(float depthBiasClamp) {
	this->depthBiasClamp = depthBiasClamp;
	return *this;
}


// Methods of DeviceDescriptor
DeviceDescriptor& DeviceDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredFeatures(size_t requiredFeatureCount, const FeatureName * requiredFeatures) {
	this->requiredFeatureCount = requiredFeatureCount;
	this->requiredFeatures = requiredFeatures;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredFeatures(const std::vector<FeatureName>& requiredFeatures) {
	this->requiredFeatureCount = static_cast<size_t>(requiredFeatures.size());
	this->requiredFeatures = reinterpret_cast<const FeatureName *>(requiredFeatures.data());
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredFeatures(const std::span<const FeatureName>& requiredFeatures) {
	this->requiredFeatureCount = static_cast<size_t>(requiredFeatures.size());
	this->requiredFeatures = reinterpret_cast<const FeatureName *>(requiredFeatures.data());
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredLimits(const Limits * requiredLimits) {
	this->requiredLimits = requiredLimits;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setDefaultQueue(QueueDescriptor defaultQueue) {
	this->defaultQueue = defaultQueue;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setDeviceLostCallbackInfo(DeviceLostCallbackInfo deviceLostCallbackInfo) {
	this->deviceLostCallbackInfo = deviceLostCallbackInfo;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setUncapturedErrorCallbackInfo(UncapturedErrorCallbackInfo uncapturedErrorCallbackInfo) {
	this->uncapturedErrorCallbackInfo = uncapturedErrorCallbackInfo;
	return *this;
}


// Methods of FutureWaitInfo
FutureWaitInfo& FutureWaitInfo::setFuture(Future future) {
	this->future = future;
	return *this;
}
FutureWaitInfo& FutureWaitInfo::setCompleted(WGPUBool completed) {
	this->completed = completed;
	return *this;
}


// Methods of InstanceDescriptor
InstanceDescriptor& InstanceDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
InstanceDescriptor& InstanceDescriptor::setFeatures(InstanceCapabilities features) {
	this->features = features;
	return *this;
}


// Methods of ProgrammableStageDescriptor
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setModule(raw::ShaderModule module) {
	this->module = module;
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setEntryPoint(StringView entryPoint) {
	this->entryPoint = entryPoint;
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setConstants(size_t constantCount, const ConstantEntry * constants) {
	this->constantCount = constantCount;
	this->constants = constants;
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setConstants(const std::vector<ConstantEntry>& constants) {
	this->constantCount = static_cast<size_t>(constants.size());
	this->constants = reinterpret_cast<const ConstantEntry *>(constants.data());
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setConstants(const std::span<const ConstantEntry>& constants) {
	this->constantCount = static_cast<size_t>(constants.size());
	this->constants = reinterpret_cast<const ConstantEntry *>(constants.data());
	return *this;
}


// Methods of RenderPassColorAttachment
RenderPassColorAttachment& RenderPassColorAttachment::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setView(raw::TextureView view) {
	this->view = view;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setDepthSlice(uint32_t depthSlice) {
	this->depthSlice = depthSlice;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setResolveTarget(raw::TextureView resolveTarget) {
	this->resolveTarget = resolveTarget;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setLoadOp(LoadOp loadOp) {
	this->loadOp = loadOp;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setStoreOp(StoreOp storeOp) {
	this->storeOp = storeOp;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setClearValue(Color clearValue) {
	this->clearValue = clearValue;
	return *this;
}


// Methods of TexelCopyBufferInfo
TexelCopyBufferInfo& TexelCopyBufferInfo::setLayout(TexelCopyBufferLayout layout) {
	this->layout = layout;
	return *this;
}
TexelCopyBufferInfo& TexelCopyBufferInfo::setBuffer(raw::Buffer buffer) {
	this->buffer = buffer;
	return *this;
}


// Methods of TexelCopyTextureInfo
TexelCopyTextureInfo& TexelCopyTextureInfo::setTexture(raw::Texture texture) {
	this->texture = texture;
	return *this;
}
TexelCopyTextureInfo& TexelCopyTextureInfo::setMipLevel(uint32_t mipLevel) {
	this->mipLevel = mipLevel;
	return *this;
}
TexelCopyTextureInfo& TexelCopyTextureInfo::setOrigin(Origin3D origin) {
	this->origin = origin;
	return *this;
}
TexelCopyTextureInfo& TexelCopyTextureInfo::setAspect(TextureAspect aspect) {
	this->aspect = aspect;
	return *this;
}


// Methods of TextureDescriptor
TextureDescriptor& TextureDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
TextureDescriptor& TextureDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
TextureDescriptor& TextureDescriptor::setUsage(TextureUsage usage) {
	this->usage = usage;
	return *this;
}
TextureDescriptor& TextureDescriptor::setDimension(TextureDimension dimension) {
	this->dimension = dimension;
	return *this;
}
TextureDescriptor& TextureDescriptor::setSize(Extent3D size) {
	this->size = size;
	return *this;
}
TextureDescriptor& TextureDescriptor::setFormat(TextureFormat format) {
	this->format = format;
	return *this;
}
TextureDescriptor& TextureDescriptor::setMipLevelCount(uint32_t mipLevelCount) {
	this->mipLevelCount = mipLevelCount;
	return *this;
}
TextureDescriptor& TextureDescriptor::setSampleCount(uint32_t sampleCount) {
	this->sampleCount = sampleCount;
	return *this;
}
TextureDescriptor& TextureDescriptor::setViewFormats(size_t viewFormatCount, const TextureFormat * viewFormats) {
	this->viewFormatCount = viewFormatCount;
	this->viewFormats = viewFormats;
	return *this;
}
TextureDescriptor& TextureDescriptor::setViewFormats(const std::vector<TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<size_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<const TextureFormat *>(viewFormats.data());
	return *this;
}
TextureDescriptor& TextureDescriptor::setViewFormats(const std::span<const TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<size_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<const TextureFormat *>(viewFormats.data());
	return *this;
}


// Methods of VertexBufferLayout
VertexBufferLayout& VertexBufferLayout::setStepMode(VertexStepMode stepMode) {
	this->stepMode = stepMode;
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setArrayStride(uint64_t arrayStride) {
	this->arrayStride = arrayStride;
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setAttributes(size_t attributeCount, const VertexAttribute * attributes) {
	this->attributeCount = attributeCount;
	this->attributes = attributes;
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setAttributes(const std::vector<VertexAttribute>& attributes) {
	this->attributeCount = static_cast<size_t>(attributes.size());
	this->attributes = reinterpret_cast<const VertexAttribute *>(attributes.data());
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setAttributes(const std::span<const VertexAttribute>& attributes) {
	this->attributeCount = static_cast<size_t>(attributes.size());
	this->attributes = reinterpret_cast<const VertexAttribute *>(attributes.data());
	return *this;
}


// Methods of BindGroupLayoutDescriptor
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setEntries(size_t entryCount, const BindGroupLayoutEntry * entries) {
	this->entryCount = entryCount;
	this->entries = entries;
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setEntries(const std::vector<BindGroupLayoutEntry>& entries) {
	this->entryCount = static_cast<size_t>(entries.size());
	this->entries = reinterpret_cast<const BindGroupLayoutEntry *>(entries.data());
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setEntries(const std::span<const BindGroupLayoutEntry>& entries) {
	this->entryCount = static_cast<size_t>(entries.size());
	this->entries = reinterpret_cast<const BindGroupLayoutEntry *>(entries.data());
	return *this;
}


// Methods of ColorTargetState
ColorTargetState& ColorTargetState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
ColorTargetState& ColorTargetState::setFormat(TextureFormat format) {
	this->format = format;
	return *this;
}
ColorTargetState& ColorTargetState::setBlend(const BlendState * blend) {
	this->blend = blend;
	return *this;
}
ColorTargetState& ColorTargetState::setWriteMask(ColorWriteMask writeMask) {
	this->writeMask = writeMask;
	return *this;
}


// Methods of ComputePipelineDescriptor
ComputePipelineDescriptor& ComputePipelineDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setLayout(raw::PipelineLayout layout) {
	this->layout = layout;
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setCompute(ProgrammableStageDescriptor compute) {
	this->compute = compute;
	return *this;
}


// Methods of RenderPassDescriptor
RenderPassDescriptor& RenderPassDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setColorAttachments(size_t colorAttachmentCount, const RenderPassColorAttachment * colorAttachments) {
	this->colorAttachmentCount = colorAttachmentCount;
	this->colorAttachments = colorAttachments;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setColorAttachments(const std::vector<RenderPassColorAttachment>& colorAttachments) {
	this->colorAttachmentCount = static_cast<size_t>(colorAttachments.size());
	this->colorAttachments = reinterpret_cast<const RenderPassColorAttachment *>(colorAttachments.data());
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setColorAttachments(const std::span<const RenderPassColorAttachment>& colorAttachments) {
	this->colorAttachmentCount = static_cast<size_t>(colorAttachments.size());
	this->colorAttachments = reinterpret_cast<const RenderPassColorAttachment *>(colorAttachments.data());
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setDepthStencilAttachment(const RenderPassDepthStencilAttachment * depthStencilAttachment) {
	this->depthStencilAttachment = depthStencilAttachment;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setOcclusionQuerySet(raw::QuerySet occlusionQuerySet) {
	this->occlusionQuerySet = occlusionQuerySet;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setTimestampWrites(const RenderPassTimestampWrites * timestampWrites) {
	this->timestampWrites = timestampWrites;
	return *this;
}


// Methods of VertexState
VertexState& VertexState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
VertexState& VertexState::setModule(raw::ShaderModule module) {
	this->module = module;
	return *this;
}
VertexState& VertexState::setEntryPoint(StringView entryPoint) {
	this->entryPoint = entryPoint;
	return *this;
}
VertexState& VertexState::setConstants(size_t constantCount, const ConstantEntry * constants) {
	this->constantCount = constantCount;
	this->constants = constants;
	return *this;
}
VertexState& VertexState::setConstants(const std::vector<ConstantEntry>& constants) {
	this->constantCount = static_cast<size_t>(constants.size());
	this->constants = reinterpret_cast<const ConstantEntry *>(constants.data());
	return *this;
}
VertexState& VertexState::setConstants(const std::span<const ConstantEntry>& constants) {
	this->constantCount = static_cast<size_t>(constants.size());
	this->constants = reinterpret_cast<const ConstantEntry *>(constants.data());
	return *this;
}
VertexState& VertexState::setBuffers(size_t bufferCount, const VertexBufferLayout * buffers) {
	this->bufferCount = bufferCount;
	this->buffers = buffers;
	return *this;
}
VertexState& VertexState::setBuffers(const std::vector<VertexBufferLayout>& buffers) {
	this->bufferCount = static_cast<size_t>(buffers.size());
	this->buffers = reinterpret_cast<const VertexBufferLayout *>(buffers.data());
	return *this;
}
VertexState& VertexState::setBuffers(const std::span<const VertexBufferLayout>& buffers) {
	this->bufferCount = static_cast<size_t>(buffers.size());
	this->buffers = reinterpret_cast<const VertexBufferLayout *>(buffers.data());
	return *this;
}


// Methods of FragmentState
FragmentState& FragmentState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
FragmentState& FragmentState::setModule(raw::ShaderModule module) {
	this->module = module;
	return *this;
}
FragmentState& FragmentState::setEntryPoint(StringView entryPoint) {
	this->entryPoint = entryPoint;
	return *this;
}
FragmentState& FragmentState::setConstants(size_t constantCount, const ConstantEntry * constants) {
	this->constantCount = constantCount;
	this->constants = constants;
	return *this;
}
FragmentState& FragmentState::setConstants(const std::vector<ConstantEntry>& constants) {
	this->constantCount = static_cast<size_t>(constants.size());
	this->constants = reinterpret_cast<const ConstantEntry *>(constants.data());
	return *this;
}
FragmentState& FragmentState::setConstants(const std::span<const ConstantEntry>& constants) {
	this->constantCount = static_cast<size_t>(constants.size());
	this->constants = reinterpret_cast<const ConstantEntry *>(constants.data());
	return *this;
}
FragmentState& FragmentState::setTargets(size_t targetCount, const ColorTargetState * targets) {
	this->targetCount = targetCount;
	this->targets = targets;
	return *this;
}
FragmentState& FragmentState::setTargets(const std::vector<ColorTargetState>& targets) {
	this->targetCount = static_cast<size_t>(targets.size());
	this->targets = reinterpret_cast<const ColorTargetState *>(targets.data());
	return *this;
}
FragmentState& FragmentState::setTargets(const std::span<const ColorTargetState>& targets) {
	this->targetCount = static_cast<size_t>(targets.size());
	this->targets = reinterpret_cast<const ColorTargetState *>(targets.data());
	return *this;
}


// Methods of RenderPipelineDescriptor
RenderPipelineDescriptor& RenderPipelineDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setLayout(raw::PipelineLayout layout) {
	this->layout = layout;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setVertex(VertexState vertex) {
	this->vertex = vertex;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setPrimitive(PrimitiveState primitive) {
	this->primitive = primitive;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setDepthStencil(const DepthStencilState * depthStencil) {
	this->depthStencil = depthStencil;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setMultisample(MultisampleState multisample) {
	this->multisample = multisample;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setFragment(const FragmentState * fragment) {
	this->fragment = fragment;
	return *this;
}


// Methods of InstanceExtras
InstanceExtras& InstanceExtras::setBackends(InstanceBackend backends) {
	this->backends = backends;
	return *this;
}
InstanceExtras& InstanceExtras::setFlags(InstanceFlag flags) {
	this->flags = flags;
	return *this;
}
InstanceExtras& InstanceExtras::setDx12ShaderCompiler(Dx12Compiler dx12ShaderCompiler) {
	this->dx12ShaderCompiler = dx12ShaderCompiler;
	return *this;
}
InstanceExtras& InstanceExtras::setGles3MinorVersion(Gles3MinorVersion gles3MinorVersion) {
	this->gles3MinorVersion = gles3MinorVersion;
	return *this;
}
InstanceExtras& InstanceExtras::setGlFenceBehaviour(GLFenceBehaviour glFenceBehaviour) {
	this->glFenceBehaviour = glFenceBehaviour;
	return *this;
}
InstanceExtras& InstanceExtras::setDxcPath(StringView dxcPath) {
	this->dxcPath = dxcPath;
	return *this;
}
InstanceExtras& InstanceExtras::setDxcMaxShaderModel(DxcMaxShaderModel dxcMaxShaderModel) {
	this->dxcMaxShaderModel = dxcMaxShaderModel;
	return *this;
}
InstanceExtras& InstanceExtras::setDx12PresentationSystem(Dx12SwapchainKind dx12PresentationSystem) {
	this->dx12PresentationSystem = dx12PresentationSystem;
	return *this;
}
InstanceExtras& InstanceExtras::setBudgetForDeviceCreation(const uint8_t * budgetForDeviceCreation) {
	this->budgetForDeviceCreation = budgetForDeviceCreation;
	return *this;
}
InstanceExtras& InstanceExtras::setBudgetForDeviceLoss(const uint8_t * budgetForDeviceLoss) {
	this->budgetForDeviceLoss = budgetForDeviceLoss;
	return *this;
}


// Methods of DeviceExtras
DeviceExtras& DeviceExtras::setTracePath(StringView tracePath) {
	this->tracePath = tracePath;
	return *this;
}


// Methods of NativeLimits
NativeLimits& NativeLimits::setMaxPushConstantSize(uint32_t maxPushConstantSize) {
	this->maxPushConstantSize = maxPushConstantSize;
	return *this;
}
NativeLimits& NativeLimits::setMaxNonSamplerBindings(uint32_t maxNonSamplerBindings) {
	this->maxNonSamplerBindings = maxNonSamplerBindings;
	return *this;
}


// Methods of PushConstantRange
PushConstantRange& PushConstantRange::setStages(ShaderStage stages) {
	this->stages = stages;
	return *this;
}
PushConstantRange& PushConstantRange::setStart(uint32_t start) {
	this->start = start;
	return *this;
}
PushConstantRange& PushConstantRange::setEnd(uint32_t end) {
	this->end = end;
	return *this;
}


// Methods of PipelineLayoutExtras
PipelineLayoutExtras& PipelineLayoutExtras::setPushConstantRanges(size_t pushConstantRangeCount, const PushConstantRange * pushConstantRanges) {
	this->pushConstantRangeCount = pushConstantRangeCount;
	this->pushConstantRanges = pushConstantRanges;
	return *this;
}
PipelineLayoutExtras& PipelineLayoutExtras::setPushConstantRanges(const std::vector<PushConstantRange>& pushConstantRanges) {
	this->pushConstantRangeCount = static_cast<size_t>(pushConstantRanges.size());
	this->pushConstantRanges = reinterpret_cast<const PushConstantRange *>(pushConstantRanges.data());
	return *this;
}
PipelineLayoutExtras& PipelineLayoutExtras::setPushConstantRanges(const std::span<const PushConstantRange>& pushConstantRanges) {
	this->pushConstantRangeCount = static_cast<size_t>(pushConstantRanges.size());
	this->pushConstantRanges = reinterpret_cast<const PushConstantRange *>(pushConstantRanges.data());
	return *this;
}


// Methods of ShaderDefine
ShaderDefine& ShaderDefine::setName(StringView name) {
	this->name = name;
	return *this;
}
ShaderDefine& ShaderDefine::setValue(StringView value) {
	this->value = value;
	return *this;
}


// Methods of ShaderSourceGLSL
ShaderSourceGLSL& ShaderSourceGLSL::setStage(ShaderStage stage) {
	this->stage = stage;
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setCode(StringView code) {
	this->code = code;
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setDefines(uint32_t defineCount, ShaderDefine * defines) {
	this->defineCount = defineCount;
	this->defines = defines;
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setDefines(std::vector<ShaderDefine>& defines) {
	this->defineCount = static_cast<uint32_t>(defines.size());
	this->defines = reinterpret_cast<ShaderDefine *>(defines.data());
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setDefines(const std::span<ShaderDefine>& defines) {
	this->defineCount = static_cast<uint32_t>(defines.size());
	this->defines = reinterpret_cast<ShaderDefine *>(defines.data());
	return *this;
}


// Methods of ShaderModuleDescriptorSpirV
ShaderModuleDescriptorSpirV& ShaderModuleDescriptorSpirV::setLabel(StringView label) {
	this->label = label;
	return *this;
}
ShaderModuleDescriptorSpirV& ShaderModuleDescriptorSpirV::setSourceSize(uint32_t sourceSize) {
	this->sourceSize = sourceSize;
	return *this;
}
ShaderModuleDescriptorSpirV& ShaderModuleDescriptorSpirV::setSource(const uint32_t * source) {
	this->source = source;
	return *this;
}


// Methods of RegistryReport
RegistryReport& RegistryReport::setNumAllocated(size_t numAllocated) {
	this->numAllocated = numAllocated;
	return *this;
}
RegistryReport& RegistryReport::setNumKeptFromUser(size_t numKeptFromUser) {
	this->numKeptFromUser = numKeptFromUser;
	return *this;
}
RegistryReport& RegistryReport::setNumReleasedFromUser(size_t numReleasedFromUser) {
	this->numReleasedFromUser = numReleasedFromUser;
	return *this;
}
RegistryReport& RegistryReport::setElementSize(size_t elementSize) {
	this->elementSize = elementSize;
	return *this;
}


// Methods of HubReport
HubReport& HubReport::setAdapters(RegistryReport adapters) {
	this->adapters = adapters;
	return *this;
}
HubReport& HubReport::setDevices(RegistryReport devices) {
	this->devices = devices;
	return *this;
}
HubReport& HubReport::setQueues(RegistryReport queues) {
	this->queues = queues;
	return *this;
}
HubReport& HubReport::setPipelineLayouts(RegistryReport pipelineLayouts) {
	this->pipelineLayouts = pipelineLayouts;
	return *this;
}
HubReport& HubReport::setShaderModules(RegistryReport shaderModules) {
	this->shaderModules = shaderModules;
	return *this;
}
HubReport& HubReport::setBindGroupLayouts(RegistryReport bindGroupLayouts) {
	this->bindGroupLayouts = bindGroupLayouts;
	return *this;
}
HubReport& HubReport::setBindGroups(RegistryReport bindGroups) {
	this->bindGroups = bindGroups;
	return *this;
}
HubReport& HubReport::setCommandBuffers(RegistryReport commandBuffers) {
	this->commandBuffers = commandBuffers;
	return *this;
}
HubReport& HubReport::setRenderBundles(RegistryReport renderBundles) {
	this->renderBundles = renderBundles;
	return *this;
}
HubReport& HubReport::setRenderPipelines(RegistryReport renderPipelines) {
	this->renderPipelines = renderPipelines;
	return *this;
}
HubReport& HubReport::setComputePipelines(RegistryReport computePipelines) {
	this->computePipelines = computePipelines;
	return *this;
}
HubReport& HubReport::setPipelineCaches(RegistryReport pipelineCaches) {
	this->pipelineCaches = pipelineCaches;
	return *this;
}
HubReport& HubReport::setQuerySets(RegistryReport querySets) {
	this->querySets = querySets;
	return *this;
}
HubReport& HubReport::setBuffers(RegistryReport buffers) {
	this->buffers = buffers;
	return *this;
}
HubReport& HubReport::setTextures(RegistryReport textures) {
	this->textures = textures;
	return *this;
}
HubReport& HubReport::setTextureViews(RegistryReport textureViews) {
	this->textureViews = textureViews;
	return *this;
}
HubReport& HubReport::setSamplers(RegistryReport samplers) {
	this->samplers = samplers;
	return *this;
}


// Methods of GlobalReport
GlobalReport& GlobalReport::setSurfaces(RegistryReport surfaces) {
	this->surfaces = surfaces;
	return *this;
}
GlobalReport& GlobalReport::setHub(HubReport hub) {
	this->hub = hub;
	return *this;
}


// Methods of InstanceEnumerateAdapterOptions
InstanceEnumerateAdapterOptions& InstanceEnumerateAdapterOptions::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = nextInChain;
	return *this;
}
InstanceEnumerateAdapterOptions& InstanceEnumerateAdapterOptions::setBackends(InstanceBackend backends) {
	this->backends = backends;
	return *this;
}


// Methods of BindGroupEntryExtras
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(size_t bufferCount, const raw::Buffer * buffers) {
	this->bufferCount = bufferCount;
	this->buffers = buffers;
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(const std::vector<raw::Buffer>& buffers) {
	this->bufferCount = static_cast<size_t>(buffers.size());
	this->buffers = reinterpret_cast<const raw::Buffer *>(buffers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(const std::span<const raw::Buffer>& buffers) {
	this->bufferCount = static_cast<size_t>(buffers.size());
	this->buffers = reinterpret_cast<const raw::Buffer *>(buffers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(const std::vector<Buffer>& buffers) {
	this->bufferCount = static_cast<size_t>(buffers.size());
	this->buffers = reinterpret_cast<const raw::Buffer *>(buffers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(const std::span<const Buffer>& buffers) {
	this->bufferCount = static_cast<size_t>(buffers.size());
	this->buffers = reinterpret_cast<const raw::Buffer *>(buffers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(size_t samplerCount, const raw::Sampler * samplers) {
	this->samplerCount = samplerCount;
	this->samplers = samplers;
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(const std::vector<raw::Sampler>& samplers) {
	this->samplerCount = static_cast<size_t>(samplers.size());
	this->samplers = reinterpret_cast<const raw::Sampler *>(samplers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(const std::span<const raw::Sampler>& samplers) {
	this->samplerCount = static_cast<size_t>(samplers.size());
	this->samplers = reinterpret_cast<const raw::Sampler *>(samplers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(const std::vector<Sampler>& samplers) {
	this->samplerCount = static_cast<size_t>(samplers.size());
	this->samplers = reinterpret_cast<const raw::Sampler *>(samplers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(const std::span<const Sampler>& samplers) {
	this->samplerCount = static_cast<size_t>(samplers.size());
	this->samplers = reinterpret_cast<const raw::Sampler *>(samplers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(size_t textureViewCount, const raw::TextureView * textureViews) {
	this->textureViewCount = textureViewCount;
	this->textureViews = textureViews;
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(const std::vector<raw::TextureView>& textureViews) {
	this->textureViewCount = static_cast<size_t>(textureViews.size());
	this->textureViews = reinterpret_cast<const raw::TextureView *>(textureViews.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(const std::span<const raw::TextureView>& textureViews) {
	this->textureViewCount = static_cast<size_t>(textureViews.size());
	this->textureViews = reinterpret_cast<const raw::TextureView *>(textureViews.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(const std::vector<TextureView>& textureViews) {
	this->textureViewCount = static_cast<size_t>(textureViews.size());
	this->textureViews = reinterpret_cast<const raw::TextureView *>(textureViews.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(const std::span<const TextureView>& textureViews) {
	this->textureViewCount = static_cast<size_t>(textureViews.size());
	this->textureViews = reinterpret_cast<const raw::TextureView *>(textureViews.data());
	return *this;
}


// Methods of BindGroupLayoutEntryExtras
BindGroupLayoutEntryExtras& BindGroupLayoutEntryExtras::setCount(uint32_t count) {
	this->count = count;
	return *this;
}


// Methods of QuerySetDescriptorExtras
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setPipelineStatistics(size_t pipelineStatisticCount, const PipelineStatisticName * pipelineStatistics) {
	this->pipelineStatisticCount = pipelineStatisticCount;
	this->pipelineStatistics = pipelineStatistics;
	return *this;
}
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setPipelineStatistics(const std::vector<PipelineStatisticName>& pipelineStatistics) {
	this->pipelineStatisticCount = static_cast<size_t>(pipelineStatistics.size());
	this->pipelineStatistics = reinterpret_cast<const PipelineStatisticName *>(pipelineStatistics.data());
	return *this;
}
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setPipelineStatistics(const std::span<const PipelineStatisticName>& pipelineStatistics) {
	this->pipelineStatisticCount = static_cast<size_t>(pipelineStatistics.size());
	this->pipelineStatistics = reinterpret_cast<const PipelineStatisticName *>(pipelineStatistics.data());
	return *this;
}


// Methods of SurfaceConfigurationExtras
SurfaceConfigurationExtras& SurfaceConfigurationExtras::setDesiredMaximumFrameLatency(uint32_t desiredMaximumFrameLatency) {
	this->desiredMaximumFrameLatency = desiredMaximumFrameLatency;
	return *this;
}


// Methods of SurfaceSourceSwapChainPanel
SurfaceSourceSwapChainPanel& SurfaceSourceSwapChainPanel::setPanelNative(void * panelNative) {
	this->panelNative = panelNative;
	return *this;
}


// Methods of PrimitiveStateExtras
PrimitiveStateExtras& PrimitiveStateExtras::setPolygonMode(PolygonMode polygonMode) {
	this->polygonMode = polygonMode;
	return *this;
}
PrimitiveStateExtras& PrimitiveStateExtras::setConservative(WGPUBool conservative) {
	this->conservative = conservative;
	return *this;
}


} // namespace wgpu

namespace wgpu {
namespace raw {
// Methods of Adapter
void Adapter::getFeatures(SupportedFeatures& features) const {
	return wgpuAdapterGetFeatures(m_raw, reinterpret_cast<WGPUSupportedFeatures *>(&features));
}
Status Adapter::getInfo(AdapterInfo& info) const {
	return static_cast<Status>(wgpuAdapterGetInfo(m_raw, reinterpret_cast<WGPUAdapterInfo *>(&info)));
}
Status Adapter::getLimits(Limits& limits) const {
	return static_cast<Status>(wgpuAdapterGetLimits(m_raw, reinterpret_cast<WGPULimits *>(&limits)));
}
Bool Adapter::hasFeature(FeatureName feature) const {
	return wgpuAdapterHasFeature(m_raw, static_cast<WGPUFeatureName>(feature));
}
void Adapter::addRef() const {
	return wgpuAdapterAddRef(m_raw);
}
void Adapter::release() const {
	return wgpuAdapterRelease(m_raw);
}


// Methods of BindGroup
void BindGroup::setLabel(StringView label) const {
	return wgpuBindGroupSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void BindGroup::addRef() const {
	return wgpuBindGroupAddRef(m_raw);
}
void BindGroup::release() const {
	return wgpuBindGroupRelease(m_raw);
}


// Methods of BindGroupLayout
void BindGroupLayout::setLabel(StringView label) const {
	return wgpuBindGroupLayoutSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void BindGroupLayout::addRef() const {
	return wgpuBindGroupLayoutAddRef(m_raw);
}
void BindGroupLayout::release() const {
	return wgpuBindGroupLayoutRelease(m_raw);
}


// Methods of Buffer
void Buffer::destroy() const {
	return wgpuBufferDestroy(m_raw);
}
void const * Buffer::getConstMappedRange(size_t offset, size_t size) const {
	return wgpuBufferGetConstMappedRange(m_raw, offset, size);
}
BufferMapState Buffer::getMapState() const {
	return static_cast<BufferMapState>(wgpuBufferGetMapState(m_raw));
}
void * Buffer::getMappedRange(size_t offset, size_t size) const {
	return wgpuBufferGetMappedRange(m_raw, offset, size);
}
uint64_t Buffer::getSize() const {
	return wgpuBufferGetSize(m_raw);
}
BufferUsage Buffer::getUsage() const {
	return static_cast<BufferUsage>(wgpuBufferGetUsage(m_raw));
}
void Buffer::setLabel(StringView label) const {
	return wgpuBufferSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void Buffer::unmap() const {
	return wgpuBufferUnmap(m_raw);
}
void Buffer::addRef() const {
	return wgpuBufferAddRef(m_raw);
}
void Buffer::release() const {
	return wgpuBufferRelease(m_raw);
}


// Methods of CommandBuffer
void CommandBuffer::setLabel(StringView label) const {
	return wgpuCommandBufferSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void CommandBuffer::addRef() const {
	return wgpuCommandBufferAddRef(m_raw);
}
void CommandBuffer::release() const {
	return wgpuCommandBufferRelease(m_raw);
}


// Methods of CommandEncoder
raw::ComputePassEncoder CommandEncoder::beginComputePass(const ComputePassDescriptor& descriptor) const {
	return wgpuCommandEncoderBeginComputePass(m_raw, reinterpret_cast<const WGPUComputePassDescriptor *>(&descriptor));
}
raw::RenderPassEncoder CommandEncoder::beginRenderPass(const RenderPassDescriptor& descriptor) const {
	return wgpuCommandEncoderBeginRenderPass(m_raw, reinterpret_cast<const WGPURenderPassDescriptor *>(&descriptor));
}
void CommandEncoder::clearBuffer(raw::Buffer buffer, uint64_t offset, uint64_t size) const {
	return wgpuCommandEncoderClearBuffer(m_raw, buffer, offset, size);
}
void CommandEncoder::copyBufferToBuffer(raw::Buffer source, uint64_t sourceOffset, raw::Buffer destination, uint64_t destinationOffset, uint64_t size) const {
	return wgpuCommandEncoderCopyBufferToBuffer(m_raw, source, sourceOffset, destination, destinationOffset, size);
}
void CommandEncoder::copyBufferToTexture(const TexelCopyBufferInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const {
	return wgpuCommandEncoderCopyBufferToTexture(m_raw, reinterpret_cast<const WGPUTexelCopyBufferInfo *>(&source), reinterpret_cast<const WGPUTexelCopyTextureInfo *>(&destination), reinterpret_cast<const WGPUExtent3D *>(&copySize));
}
void CommandEncoder::copyTextureToBuffer(const TexelCopyTextureInfo& source, const TexelCopyBufferInfo& destination, const Extent3D& copySize) const {
	return wgpuCommandEncoderCopyTextureToBuffer(m_raw, reinterpret_cast<const WGPUTexelCopyTextureInfo *>(&source), reinterpret_cast<const WGPUTexelCopyBufferInfo *>(&destination), reinterpret_cast<const WGPUExtent3D *>(&copySize));
}
void CommandEncoder::copyTextureToTexture(const TexelCopyTextureInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const {
	return wgpuCommandEncoderCopyTextureToTexture(m_raw, reinterpret_cast<const WGPUTexelCopyTextureInfo *>(&source), reinterpret_cast<const WGPUTexelCopyTextureInfo *>(&destination), reinterpret_cast<const WGPUExtent3D *>(&copySize));
}
raw::CommandBuffer CommandEncoder::finish(const CommandBufferDescriptor& descriptor) const {
	return wgpuCommandEncoderFinish(m_raw, reinterpret_cast<const WGPUCommandBufferDescriptor *>(&descriptor));
}
void CommandEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuCommandEncoderInsertDebugMarker(m_raw, *reinterpret_cast<WGPUStringView *>(&markerLabel));
}
void CommandEncoder::popDebugGroup() const {
	return wgpuCommandEncoderPopDebugGroup(m_raw);
}
void CommandEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuCommandEncoderPushDebugGroup(m_raw, *reinterpret_cast<WGPUStringView *>(&groupLabel));
}
void CommandEncoder::resolveQuerySet(raw::QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, raw::Buffer destination, uint64_t destinationOffset) const {
	return wgpuCommandEncoderResolveQuerySet(m_raw, querySet, firstQuery, queryCount, destination, destinationOffset);
}
void CommandEncoder::setLabel(StringView label) const {
	return wgpuCommandEncoderSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void CommandEncoder::writeTimestamp(raw::QuerySet querySet, uint32_t queryIndex) const {
	return wgpuCommandEncoderWriteTimestamp(m_raw, querySet, queryIndex);
}
void CommandEncoder::addRef() const {
	return wgpuCommandEncoderAddRef(m_raw);
}
void CommandEncoder::release() const {
	return wgpuCommandEncoderRelease(m_raw);
}


// Methods of ComputePassEncoder
void ComputePassEncoder::dispatchWorkgroups(uint32_t workgroupCountX, uint32_t workgroupCountY, uint32_t workgroupCountZ) const {
	return wgpuComputePassEncoderDispatchWorkgroups(m_raw, workgroupCountX, workgroupCountY, workgroupCountZ);
}
void ComputePassEncoder::dispatchWorkgroupsIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuComputePassEncoderDispatchWorkgroupsIndirect(m_raw, indirectBuffer, indirectOffset);
}
void ComputePassEncoder::end() const {
	return wgpuComputePassEncoderEnd(m_raw);
}
void ComputePassEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuComputePassEncoderInsertDebugMarker(m_raw, *reinterpret_cast<WGPUStringView *>(&markerLabel));
}
void ComputePassEncoder::popDebugGroup() const {
	return wgpuComputePassEncoderPopDebugGroup(m_raw);
}
void ComputePassEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuComputePassEncoderPushDebugGroup(m_raw, *reinterpret_cast<WGPUStringView *>(&groupLabel));
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), reinterpret_cast<const uint32_t *>(dynamicOffsets.data()));
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::span<const uint32_t>& dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), reinterpret_cast<const uint32_t *>(dynamicOffsets.data()));
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const uint32_t& dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, 1, reinterpret_cast<const uint32_t *>(&dynamicOffsets));
}
void ComputePassEncoder::setLabel(StringView label) const {
	return wgpuComputePassEncoderSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void ComputePassEncoder::setPipeline(raw::ComputePipeline pipeline) const {
	return wgpuComputePassEncoderSetPipeline(m_raw, pipeline);
}
void ComputePassEncoder::addRef() const {
	return wgpuComputePassEncoderAddRef(m_raw);
}
void ComputePassEncoder::release() const {
	return wgpuComputePassEncoderRelease(m_raw);
}
void ComputePassEncoder::setPushConstants(uint32_t offset, uint32_t sizeBytes, void const * data) const {
	return wgpuComputePassEncoderSetPushConstants(m_raw, offset, sizeBytes, data);
}
void ComputePassEncoder::beginPipelineStatisticsQuery(raw::QuerySet querySet, uint32_t queryIndex) const {
	return wgpuComputePassEncoderBeginPipelineStatisticsQuery(m_raw, querySet, queryIndex);
}
void ComputePassEncoder::endPipelineStatisticsQuery() const {
	return wgpuComputePassEncoderEndPipelineStatisticsQuery(m_raw);
}
void ComputePassEncoder::writeTimestamp(raw::QuerySet querySet, uint32_t queryIndex) const {
	return wgpuComputePassEncoderWriteTimestamp(m_raw, querySet, queryIndex);
}


// Methods of ComputePipeline
raw::BindGroupLayout ComputePipeline::getBindGroupLayout(uint32_t groupIndex) const {
	return wgpuComputePipelineGetBindGroupLayout(m_raw, groupIndex);
}
void ComputePipeline::setLabel(StringView label) const {
	return wgpuComputePipelineSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void ComputePipeline::addRef() const {
	return wgpuComputePipelineAddRef(m_raw);
}
void ComputePipeline::release() const {
	return wgpuComputePipelineRelease(m_raw);
}


// Methods of Device
raw::BindGroup Device::createBindGroup(const BindGroupDescriptor& descriptor) const {
	return wgpuDeviceCreateBindGroup(m_raw, reinterpret_cast<const WGPUBindGroupDescriptor *>(&descriptor));
}
raw::BindGroupLayout Device::createBindGroupLayout(const BindGroupLayoutDescriptor& descriptor) const {
	return wgpuDeviceCreateBindGroupLayout(m_raw, reinterpret_cast<const WGPUBindGroupLayoutDescriptor *>(&descriptor));
}
raw::Buffer Device::createBuffer(const BufferDescriptor& descriptor) const {
	return wgpuDeviceCreateBuffer(m_raw, reinterpret_cast<const WGPUBufferDescriptor *>(&descriptor));
}
raw::CommandEncoder Device::createCommandEncoder(const CommandEncoderDescriptor& descriptor) const {
	return wgpuDeviceCreateCommandEncoder(m_raw, reinterpret_cast<const WGPUCommandEncoderDescriptor *>(&descriptor));
}
raw::ComputePipeline Device::createComputePipeline(const ComputePipelineDescriptor& descriptor) const {
	return wgpuDeviceCreateComputePipeline(m_raw, reinterpret_cast<const WGPUComputePipelineDescriptor *>(&descriptor));
}
raw::PipelineLayout Device::createPipelineLayout(const PipelineLayoutDescriptor& descriptor) const {
	return wgpuDeviceCreatePipelineLayout(m_raw, reinterpret_cast<const WGPUPipelineLayoutDescriptor *>(&descriptor));
}
raw::QuerySet Device::createQuerySet(const QuerySetDescriptor& descriptor) const {
	return wgpuDeviceCreateQuerySet(m_raw, reinterpret_cast<const WGPUQuerySetDescriptor *>(&descriptor));
}
raw::RenderBundleEncoder Device::createRenderBundleEncoder(const RenderBundleEncoderDescriptor& descriptor) const {
	return wgpuDeviceCreateRenderBundleEncoder(m_raw, reinterpret_cast<const WGPURenderBundleEncoderDescriptor *>(&descriptor));
}
raw::RenderPipeline Device::createRenderPipeline(const RenderPipelineDescriptor& descriptor) const {
	return wgpuDeviceCreateRenderPipeline(m_raw, reinterpret_cast<const WGPURenderPipelineDescriptor *>(&descriptor));
}
raw::Sampler Device::createSampler(const SamplerDescriptor& descriptor) const {
	return wgpuDeviceCreateSampler(m_raw, reinterpret_cast<const WGPUSamplerDescriptor *>(&descriptor));
}
raw::ShaderModule Device::createShaderModule(const ShaderModuleDescriptor& descriptor) const {
	return wgpuDeviceCreateShaderModule(m_raw, reinterpret_cast<const WGPUShaderModuleDescriptor *>(&descriptor));
}
raw::Texture Device::createTexture(const TextureDescriptor& descriptor) const {
	return wgpuDeviceCreateTexture(m_raw, reinterpret_cast<const WGPUTextureDescriptor *>(&descriptor));
}
void Device::destroy() const {
	return wgpuDeviceDestroy(m_raw);
}
AdapterInfo Device::getAdapterInfo() const {
	return wgpuDeviceGetAdapterInfo(m_raw);
}
void Device::getFeatures(SupportedFeatures& features) const {
	return wgpuDeviceGetFeatures(m_raw, reinterpret_cast<WGPUSupportedFeatures *>(&features));
}
Status Device::getLimits(Limits& limits) const {
	return static_cast<Status>(wgpuDeviceGetLimits(m_raw, reinterpret_cast<WGPULimits *>(&limits)));
}
raw::Queue Device::getQueue() const {
	return wgpuDeviceGetQueue(m_raw);
}
Bool Device::hasFeature(FeatureName feature) const {
	return wgpuDeviceHasFeature(m_raw, static_cast<WGPUFeatureName>(feature));
}
void Device::pushErrorScope(ErrorFilter filter) const {
	return wgpuDevicePushErrorScope(m_raw, static_cast<WGPUErrorFilter>(filter));
}
void Device::setLabel(StringView label) const {
	return wgpuDeviceSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void Device::addRef() const {
	return wgpuDeviceAddRef(m_raw);
}
void Device::release() const {
	return wgpuDeviceRelease(m_raw);
}
Bool Device::poll(Bool wait, SubmissionIndex const * submissionIndex) const {
	return wgpuDevicePoll(m_raw, wait, submissionIndex);
}
raw::ShaderModule Device::createShaderModuleSpirV(const ShaderModuleDescriptorSpirV& descriptor) const {
	return wgpuDeviceCreateShaderModuleSpirV(m_raw, reinterpret_cast<const WGPUShaderModuleDescriptorSpirV *>(&descriptor));
}


// Methods of Instance
raw::Surface Instance::createSurface(const SurfaceDescriptor& descriptor) const {
	return wgpuInstanceCreateSurface(m_raw, reinterpret_cast<const WGPUSurfaceDescriptor *>(&descriptor));
}
Status Instance::getWGSLLanguageFeatures(SupportedWGSLLanguageFeatures& features) const {
	return static_cast<Status>(wgpuInstanceGetWGSLLanguageFeatures(m_raw, reinterpret_cast<WGPUSupportedWGSLLanguageFeatures *>(&features)));
}
Bool Instance::hasWGSLLanguageFeature(WGSLLanguageFeatureName feature) const {
	return wgpuInstanceHasWGSLLanguageFeature(m_raw, static_cast<WGPUWGSLLanguageFeatureName>(feature));
}
void Instance::processEvents() const {
	return wgpuInstanceProcessEvents(m_raw);
}
WaitStatus Instance::waitAny(size_t futureCount, FutureWaitInfo& futures, uint64_t timeoutNS) const {
	return static_cast<WaitStatus>(wgpuInstanceWaitAny(m_raw, futureCount, reinterpret_cast<WGPUFutureWaitInfo *>(&futures), timeoutNS));
}
void Instance::addRef() const {
	return wgpuInstanceAddRef(m_raw);
}
void Instance::release() const {
	return wgpuInstanceRelease(m_raw);
}
size_t Instance::enumerateAdapters(const InstanceEnumerateAdapterOptions& options, Adapter * adapters) const {
	return wgpuInstanceEnumerateAdapters(m_raw, reinterpret_cast<const WGPUInstanceEnumerateAdapterOptions *>(&options), reinterpret_cast<WGPUAdapter *>(adapters));
}


// Methods of PipelineLayout
void PipelineLayout::setLabel(StringView label) const {
	return wgpuPipelineLayoutSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void PipelineLayout::addRef() const {
	return wgpuPipelineLayoutAddRef(m_raw);
}
void PipelineLayout::release() const {
	return wgpuPipelineLayoutRelease(m_raw);
}


// Methods of QuerySet
void QuerySet::destroy() const {
	return wgpuQuerySetDestroy(m_raw);
}
uint32_t QuerySet::getCount() const {
	return wgpuQuerySetGetCount(m_raw);
}
QueryType QuerySet::getType() const {
	return static_cast<QueryType>(wgpuQuerySetGetType(m_raw));
}
void QuerySet::setLabel(StringView label) const {
	return wgpuQuerySetSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void QuerySet::addRef() const {
	return wgpuQuerySetAddRef(m_raw);
}
void QuerySet::release() const {
	return wgpuQuerySetRelease(m_raw);
}


// Methods of Queue
void Queue::setLabel(StringView label) const {
	return wgpuQueueSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void Queue::submit(size_t commandCount, CommandBuffer const * commands) const {
	return wgpuQueueSubmit(m_raw, commandCount, reinterpret_cast<WGPUCommandBuffer const *>(commands));
}
void Queue::submit(const std::vector<CommandBuffer>& commands) const {
	return wgpuQueueSubmit(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
void Queue::submit(const std::span<const CommandBuffer>& commands) const {
	return wgpuQueueSubmit(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
void Queue::submit(const CommandBuffer& commands) const {
	return wgpuQueueSubmit(m_raw, 1, reinterpret_cast<const WGPUCommandBuffer *>(&commands));
}
void Queue::submit(const std::vector<wgpu::CommandBuffer>& commands) const {
	return wgpuQueueSubmit(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
void Queue::submit(const std::span<const wgpu::CommandBuffer>& commands) const {
	return wgpuQueueSubmit(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
void Queue::submit(const wgpu::CommandBuffer& commands) const {
	return wgpuQueueSubmit(m_raw, 1, reinterpret_cast<const WGPUCommandBuffer *>(&commands));
}
void Queue::writeBuffer(raw::Buffer buffer, uint64_t bufferOffset, void const * data, size_t size) const {
	return wgpuQueueWriteBuffer(m_raw, buffer, bufferOffset, data, size);
}
void Queue::writeTexture(const TexelCopyTextureInfo& destination, void const * data, size_t dataSize, const TexelCopyBufferLayout& dataLayout, const Extent3D& writeSize) const {
	return wgpuQueueWriteTexture(m_raw, reinterpret_cast<const WGPUTexelCopyTextureInfo *>(&destination), data, dataSize, reinterpret_cast<const WGPUTexelCopyBufferLayout *>(&dataLayout), reinterpret_cast<const WGPUExtent3D *>(&writeSize));
}
void Queue::addRef() const {
	return wgpuQueueAddRef(m_raw);
}
void Queue::release() const {
	return wgpuQueueRelease(m_raw);
}
SubmissionIndex Queue::submitForIndex(size_t commandCount, CommandBuffer const * commands) const {
	return wgpuQueueSubmitForIndex(m_raw, commandCount, reinterpret_cast<WGPUCommandBuffer const *>(commands));
}
SubmissionIndex Queue::submitForIndex(const std::vector<CommandBuffer>& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
SubmissionIndex Queue::submitForIndex(const std::span<const CommandBuffer>& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
SubmissionIndex Queue::submitForIndex(const CommandBuffer& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, 1, reinterpret_cast<const WGPUCommandBuffer *>(&commands));
}
SubmissionIndex Queue::submitForIndex(const std::vector<wgpu::CommandBuffer>& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
SubmissionIndex Queue::submitForIndex(const std::span<const wgpu::CommandBuffer>& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, static_cast<size_t>(commands.size()), reinterpret_cast<const WGPUCommandBuffer *>(commands.data()));
}
SubmissionIndex Queue::submitForIndex(const wgpu::CommandBuffer& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, 1, reinterpret_cast<const WGPUCommandBuffer *>(&commands));
}
float Queue::getTimestampPeriod() const {
	return wgpuQueueGetTimestampPeriod(m_raw);
}


// Methods of RenderBundle
void RenderBundle::setLabel(StringView label) const {
	return wgpuRenderBundleSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void RenderBundle::addRef() const {
	return wgpuRenderBundleAddRef(m_raw);
}
void RenderBundle::release() const {
	return wgpuRenderBundleRelease(m_raw);
}


// Methods of RenderBundleEncoder
void RenderBundleEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const {
	return wgpuRenderBundleEncoderDraw(m_raw, vertexCount, instanceCount, firstVertex, firstInstance);
}
void RenderBundleEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) const {
	return wgpuRenderBundleEncoderDrawIndexed(m_raw, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}
void RenderBundleEncoder::drawIndexedIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderBundleEncoderDrawIndexedIndirect(m_raw, indirectBuffer, indirectOffset);
}
void RenderBundleEncoder::drawIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderBundleEncoderDrawIndirect(m_raw, indirectBuffer, indirectOffset);
}
raw::RenderBundle RenderBundleEncoder::finish(const RenderBundleDescriptor& descriptor) const {
	return wgpuRenderBundleEncoderFinish(m_raw, reinterpret_cast<const WGPURenderBundleDescriptor *>(&descriptor));
}
void RenderBundleEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuRenderBundleEncoderInsertDebugMarker(m_raw, *reinterpret_cast<WGPUStringView *>(&markerLabel));
}
void RenderBundleEncoder::popDebugGroup() const {
	return wgpuRenderBundleEncoderPopDebugGroup(m_raw);
}
void RenderBundleEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuRenderBundleEncoderPushDebugGroup(m_raw, *reinterpret_cast<WGPUStringView *>(&groupLabel));
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), reinterpret_cast<const uint32_t *>(dynamicOffsets.data()));
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::span<const uint32_t>& dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), reinterpret_cast<const uint32_t *>(dynamicOffsets.data()));
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const uint32_t& dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, 1, reinterpret_cast<const uint32_t *>(&dynamicOffsets));
}
void RenderBundleEncoder::setIndexBuffer(raw::Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const {
	return wgpuRenderBundleEncoderSetIndexBuffer(m_raw, buffer, static_cast<WGPUIndexFormat>(format), offset, size);
}
void RenderBundleEncoder::setLabel(StringView label) const {
	return wgpuRenderBundleEncoderSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void RenderBundleEncoder::setPipeline(raw::RenderPipeline pipeline) const {
	return wgpuRenderBundleEncoderSetPipeline(m_raw, pipeline);
}
void RenderBundleEncoder::setVertexBuffer(uint32_t slot, raw::Buffer buffer, uint64_t offset, uint64_t size) const {
	return wgpuRenderBundleEncoderSetVertexBuffer(m_raw, slot, buffer, offset, size);
}
void RenderBundleEncoder::addRef() const {
	return wgpuRenderBundleEncoderAddRef(m_raw);
}
void RenderBundleEncoder::release() const {
	return wgpuRenderBundleEncoderRelease(m_raw);
}
void RenderBundleEncoder::setPushConstants(ShaderStage stages, uint32_t offset, uint32_t sizeBytes, void const * data) const {
	return wgpuRenderBundleEncoderSetPushConstants(m_raw, static_cast<WGPUShaderStage>(stages), offset, sizeBytes, data);
}


// Methods of RenderPassEncoder
void RenderPassEncoder::beginOcclusionQuery(uint32_t queryIndex) const {
	return wgpuRenderPassEncoderBeginOcclusionQuery(m_raw, queryIndex);
}
void RenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const {
	return wgpuRenderPassEncoderDraw(m_raw, vertexCount, instanceCount, firstVertex, firstInstance);
}
void RenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) const {
	return wgpuRenderPassEncoderDrawIndexed(m_raw, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}
void RenderPassEncoder::drawIndexedIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderPassEncoderDrawIndexedIndirect(m_raw, indirectBuffer, indirectOffset);
}
void RenderPassEncoder::drawIndirect(raw::Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderPassEncoderDrawIndirect(m_raw, indirectBuffer, indirectOffset);
}
void RenderPassEncoder::end() const {
	return wgpuRenderPassEncoderEnd(m_raw);
}
void RenderPassEncoder::endOcclusionQuery() const {
	return wgpuRenderPassEncoderEndOcclusionQuery(m_raw);
}
void RenderPassEncoder::executeBundles(size_t bundleCount, RenderBundle const * bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, bundleCount, reinterpret_cast<WGPURenderBundle const *>(bundles));
}
void RenderPassEncoder::executeBundles(const std::vector<RenderBundle>& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, static_cast<size_t>(bundles.size()), reinterpret_cast<const WGPURenderBundle *>(bundles.data()));
}
void RenderPassEncoder::executeBundles(const std::span<const RenderBundle>& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, static_cast<size_t>(bundles.size()), reinterpret_cast<const WGPURenderBundle *>(bundles.data()));
}
void RenderPassEncoder::executeBundles(const RenderBundle& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, 1, reinterpret_cast<const WGPURenderBundle *>(&bundles));
}
void RenderPassEncoder::executeBundles(const std::vector<wgpu::RenderBundle>& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, static_cast<size_t>(bundles.size()), reinterpret_cast<const WGPURenderBundle *>(bundles.data()));
}
void RenderPassEncoder::executeBundles(const std::span<const wgpu::RenderBundle>& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, static_cast<size_t>(bundles.size()), reinterpret_cast<const WGPURenderBundle *>(bundles.data()));
}
void RenderPassEncoder::executeBundles(const wgpu::RenderBundle& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, 1, reinterpret_cast<const WGPURenderBundle *>(&bundles));
}
void RenderPassEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuRenderPassEncoderInsertDebugMarker(m_raw, *reinterpret_cast<WGPUStringView *>(&markerLabel));
}
void RenderPassEncoder::popDebugGroup() const {
	return wgpuRenderPassEncoderPopDebugGroup(m_raw);
}
void RenderPassEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuRenderPassEncoderPushDebugGroup(m_raw, *reinterpret_cast<WGPUStringView *>(&groupLabel));
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), reinterpret_cast<const uint32_t *>(dynamicOffsets.data()));
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const std::span<const uint32_t>& dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), reinterpret_cast<const uint32_t *>(dynamicOffsets.data()));
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, raw::BindGroup group, const uint32_t& dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, 1, reinterpret_cast<const uint32_t *>(&dynamicOffsets));
}
void RenderPassEncoder::setBlendConstant(const Color& color) const {
	return wgpuRenderPassEncoderSetBlendConstant(m_raw, reinterpret_cast<const WGPUColor *>(&color));
}
void RenderPassEncoder::setIndexBuffer(raw::Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const {
	return wgpuRenderPassEncoderSetIndexBuffer(m_raw, buffer, static_cast<WGPUIndexFormat>(format), offset, size);
}
void RenderPassEncoder::setLabel(StringView label) const {
	return wgpuRenderPassEncoderSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void RenderPassEncoder::setPipeline(raw::RenderPipeline pipeline) const {
	return wgpuRenderPassEncoderSetPipeline(m_raw, pipeline);
}
void RenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const {
	return wgpuRenderPassEncoderSetScissorRect(m_raw, x, y, width, height);
}
void RenderPassEncoder::setStencilReference(uint32_t reference) const {
	return wgpuRenderPassEncoderSetStencilReference(m_raw, reference);
}
void RenderPassEncoder::setVertexBuffer(uint32_t slot, raw::Buffer buffer, uint64_t offset, uint64_t size) const {
	return wgpuRenderPassEncoderSetVertexBuffer(m_raw, slot, buffer, offset, size);
}
void RenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) const {
	return wgpuRenderPassEncoderSetViewport(m_raw, x, y, width, height, minDepth, maxDepth);
}
void RenderPassEncoder::addRef() const {
	return wgpuRenderPassEncoderAddRef(m_raw);
}
void RenderPassEncoder::release() const {
	return wgpuRenderPassEncoderRelease(m_raw);
}
void RenderPassEncoder::setPushConstants(ShaderStage stages, uint32_t offset, uint32_t sizeBytes, void const * data) const {
	return wgpuRenderPassEncoderSetPushConstants(m_raw, static_cast<WGPUShaderStage>(stages), offset, sizeBytes, data);
}
void RenderPassEncoder::multiDrawIndirect(raw::Buffer buffer, uint64_t offset, uint32_t count) const {
	return wgpuRenderPassEncoderMultiDrawIndirect(m_raw, buffer, offset, count);
}
void RenderPassEncoder::multiDrawIndexedIndirect(raw::Buffer buffer, uint64_t offset, uint32_t count) const {
	return wgpuRenderPassEncoderMultiDrawIndexedIndirect(m_raw, buffer, offset, count);
}
void RenderPassEncoder::multiDrawIndirectCount(raw::Buffer buffer, uint64_t offset, raw::Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const {
	return wgpuRenderPassEncoderMultiDrawIndirectCount(m_raw, buffer, offset, count_buffer, count_buffer_offset, max_count);
}
void RenderPassEncoder::multiDrawIndexedIndirectCount(raw::Buffer buffer, uint64_t offset, raw::Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const {
	return wgpuRenderPassEncoderMultiDrawIndexedIndirectCount(m_raw, buffer, offset, count_buffer, count_buffer_offset, max_count);
}
void RenderPassEncoder::beginPipelineStatisticsQuery(raw::QuerySet querySet, uint32_t queryIndex) const {
	return wgpuRenderPassEncoderBeginPipelineStatisticsQuery(m_raw, querySet, queryIndex);
}
void RenderPassEncoder::endPipelineStatisticsQuery() const {
	return wgpuRenderPassEncoderEndPipelineStatisticsQuery(m_raw);
}
void RenderPassEncoder::writeTimestamp(raw::QuerySet querySet, uint32_t queryIndex) const {
	return wgpuRenderPassEncoderWriteTimestamp(m_raw, querySet, queryIndex);
}


// Methods of RenderPipeline
raw::BindGroupLayout RenderPipeline::getBindGroupLayout(uint32_t groupIndex) const {
	return wgpuRenderPipelineGetBindGroupLayout(m_raw, groupIndex);
}
void RenderPipeline::setLabel(StringView label) const {
	return wgpuRenderPipelineSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void RenderPipeline::addRef() const {
	return wgpuRenderPipelineAddRef(m_raw);
}
void RenderPipeline::release() const {
	return wgpuRenderPipelineRelease(m_raw);
}


// Methods of Sampler
void Sampler::setLabel(StringView label) const {
	return wgpuSamplerSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void Sampler::addRef() const {
	return wgpuSamplerAddRef(m_raw);
}
void Sampler::release() const {
	return wgpuSamplerRelease(m_raw);
}


// Methods of ShaderModule
void ShaderModule::setLabel(StringView label) const {
	return wgpuShaderModuleSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void ShaderModule::addRef() const {
	return wgpuShaderModuleAddRef(m_raw);
}
void ShaderModule::release() const {
	return wgpuShaderModuleRelease(m_raw);
}


// Methods of Surface
void Surface::configure(const SurfaceConfiguration& config) const {
	return wgpuSurfaceConfigure(m_raw, reinterpret_cast<const WGPUSurfaceConfiguration *>(&config));
}
Status Surface::getCapabilities(raw::Adapter adapter, SurfaceCapabilities& capabilities) const {
	return static_cast<Status>(wgpuSurfaceGetCapabilities(m_raw, adapter, reinterpret_cast<WGPUSurfaceCapabilities *>(&capabilities)));
}
void Surface::getCurrentTexture(SurfaceTexture& surfaceTexture) const {
	return wgpuSurfaceGetCurrentTexture(m_raw, reinterpret_cast<WGPUSurfaceTexture *>(&surfaceTexture));
}
Status Surface::present() const {
	return static_cast<Status>(wgpuSurfacePresent(m_raw));
}
void Surface::setLabel(StringView label) const {
	return wgpuSurfaceSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void Surface::unconfigure() const {
	return wgpuSurfaceUnconfigure(m_raw);
}
void Surface::addRef() const {
	return wgpuSurfaceAddRef(m_raw);
}
void Surface::release() const {
	return wgpuSurfaceRelease(m_raw);
}


// Methods of Texture
raw::TextureView Texture::createView(const TextureViewDescriptor& descriptor) const {
	return wgpuTextureCreateView(m_raw, reinterpret_cast<const WGPUTextureViewDescriptor *>(&descriptor));
}
void Texture::destroy() const {
	return wgpuTextureDestroy(m_raw);
}
uint32_t Texture::getDepthOrArrayLayers() const {
	return wgpuTextureGetDepthOrArrayLayers(m_raw);
}
TextureDimension Texture::getDimension() const {
	return static_cast<TextureDimension>(wgpuTextureGetDimension(m_raw));
}
TextureFormat Texture::getFormat() const {
	return static_cast<TextureFormat>(wgpuTextureGetFormat(m_raw));
}
uint32_t Texture::getHeight() const {
	return wgpuTextureGetHeight(m_raw);
}
uint32_t Texture::getMipLevelCount() const {
	return wgpuTextureGetMipLevelCount(m_raw);
}
uint32_t Texture::getSampleCount() const {
	return wgpuTextureGetSampleCount(m_raw);
}
TextureUsage Texture::getUsage() const {
	return static_cast<TextureUsage>(wgpuTextureGetUsage(m_raw));
}
uint32_t Texture::getWidth() const {
	return wgpuTextureGetWidth(m_raw);
}
void Texture::setLabel(StringView label) const {
	return wgpuTextureSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void Texture::addRef() const {
	return wgpuTextureAddRef(m_raw);
}
void Texture::release() const {
	return wgpuTextureRelease(m_raw);
}


// Methods of TextureView
void TextureView::setLabel(StringView label) const {
	return wgpuTextureViewSetLabel(m_raw, *reinterpret_cast<WGPUStringView *>(&label));
}
void TextureView::addRef() const {
	return wgpuTextureViewAddRef(m_raw);
}
void TextureView::release() const {
	return wgpuTextureViewRelease(m_raw);
}


}

} // namespace wgpu

namespace wgpu {
std::string_view to_string(AdapterType v) {
    switch (v) {
    case AdapterType::DiscreteGPU: return "wgpu::AdapterType::DiscreteGPU";
    case AdapterType::IntegratedGPU: return "wgpu::AdapterType::IntegratedGPU";
    case AdapterType::CPU: return "wgpu::AdapterType::CPU";
    case AdapterType::Unknown: return "wgpu::AdapterType::Unknown";
    case AdapterType::Force32: return "wgpu::AdapterType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(AddressMode v) {
    switch (v) {
    case AddressMode::Undefined: return "wgpu::AddressMode::Undefined";
    case AddressMode::ClampToEdge: return "wgpu::AddressMode::ClampToEdge";
    case AddressMode::Repeat: return "wgpu::AddressMode::Repeat";
    case AddressMode::MirrorRepeat: return "wgpu::AddressMode::MirrorRepeat";
    case AddressMode::Force32: return "wgpu::AddressMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(BackendType v) {
    switch (v) {
    case BackendType::Undefined: return "wgpu::BackendType::Undefined";
    case BackendType::Null: return "wgpu::BackendType::Null";
    case BackendType::WebGPU: return "wgpu::BackendType::WebGPU";
    case BackendType::D3D11: return "wgpu::BackendType::D3D11";
    case BackendType::D3D12: return "wgpu::BackendType::D3D12";
    case BackendType::Metal: return "wgpu::BackendType::Metal";
    case BackendType::Vulkan: return "wgpu::BackendType::Vulkan";
    case BackendType::OpenGL: return "wgpu::BackendType::OpenGL";
    case BackendType::OpenGLES: return "wgpu::BackendType::OpenGLES";
    case BackendType::Force32: return "wgpu::BackendType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(BlendFactor v) {
    switch (v) {
    case BlendFactor::Undefined: return "wgpu::BlendFactor::Undefined";
    case BlendFactor::Zero: return "wgpu::BlendFactor::Zero";
    case BlendFactor::One: return "wgpu::BlendFactor::One";
    case BlendFactor::Src: return "wgpu::BlendFactor::Src";
    case BlendFactor::OneMinusSrc: return "wgpu::BlendFactor::OneMinusSrc";
    case BlendFactor::SrcAlpha: return "wgpu::BlendFactor::SrcAlpha";
    case BlendFactor::OneMinusSrcAlpha: return "wgpu::BlendFactor::OneMinusSrcAlpha";
    case BlendFactor::Dst: return "wgpu::BlendFactor::Dst";
    case BlendFactor::OneMinusDst: return "wgpu::BlendFactor::OneMinusDst";
    case BlendFactor::DstAlpha: return "wgpu::BlendFactor::DstAlpha";
    case BlendFactor::OneMinusDstAlpha: return "wgpu::BlendFactor::OneMinusDstAlpha";
    case BlendFactor::SrcAlphaSaturated: return "wgpu::BlendFactor::SrcAlphaSaturated";
    case BlendFactor::Constant: return "wgpu::BlendFactor::Constant";
    case BlendFactor::OneMinusConstant: return "wgpu::BlendFactor::OneMinusConstant";
    case BlendFactor::Src1: return "wgpu::BlendFactor::Src1";
    case BlendFactor::OneMinusSrc1: return "wgpu::BlendFactor::OneMinusSrc1";
    case BlendFactor::Src1Alpha: return "wgpu::BlendFactor::Src1Alpha";
    case BlendFactor::OneMinusSrc1Alpha: return "wgpu::BlendFactor::OneMinusSrc1Alpha";
    case BlendFactor::Force32: return "wgpu::BlendFactor::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(BlendOperation v) {
    switch (v) {
    case BlendOperation::Undefined: return "wgpu::BlendOperation::Undefined";
    case BlendOperation::Add: return "wgpu::BlendOperation::Add";
    case BlendOperation::Subtract: return "wgpu::BlendOperation::Subtract";
    case BlendOperation::ReverseSubtract: return "wgpu::BlendOperation::ReverseSubtract";
    case BlendOperation::Min: return "wgpu::BlendOperation::Min";
    case BlendOperation::Max: return "wgpu::BlendOperation::Max";
    case BlendOperation::Force32: return "wgpu::BlendOperation::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(BufferBindingType v) {
    switch (v) {
    case BufferBindingType::BindingNotUsed: return "wgpu::BufferBindingType::BindingNotUsed";
    case BufferBindingType::Undefined: return "wgpu::BufferBindingType::Undefined";
    case BufferBindingType::Uniform: return "wgpu::BufferBindingType::Uniform";
    case BufferBindingType::Storage: return "wgpu::BufferBindingType::Storage";
    case BufferBindingType::ReadOnlyStorage: return "wgpu::BufferBindingType::ReadOnlyStorage";
    case BufferBindingType::Force32: return "wgpu::BufferBindingType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(BufferMapState v) {
    switch (v) {
    case BufferMapState::Unmapped: return "wgpu::BufferMapState::Unmapped";
    case BufferMapState::Pending: return "wgpu::BufferMapState::Pending";
    case BufferMapState::Mapped: return "wgpu::BufferMapState::Mapped";
    case BufferMapState::Force32: return "wgpu::BufferMapState::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CallbackMode v) {
    switch (v) {
    case CallbackMode::WaitAnyOnly: return "wgpu::CallbackMode::WaitAnyOnly";
    case CallbackMode::AllowProcessEvents: return "wgpu::CallbackMode::AllowProcessEvents";
    case CallbackMode::AllowSpontaneous: return "wgpu::CallbackMode::AllowSpontaneous";
    case CallbackMode::Force32: return "wgpu::CallbackMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CompareFunction v) {
    switch (v) {
    case CompareFunction::Undefined: return "wgpu::CompareFunction::Undefined";
    case CompareFunction::Never: return "wgpu::CompareFunction::Never";
    case CompareFunction::Less: return "wgpu::CompareFunction::Less";
    case CompareFunction::Equal: return "wgpu::CompareFunction::Equal";
    case CompareFunction::LessEqual: return "wgpu::CompareFunction::LessEqual";
    case CompareFunction::Greater: return "wgpu::CompareFunction::Greater";
    case CompareFunction::NotEqual: return "wgpu::CompareFunction::NotEqual";
    case CompareFunction::GreaterEqual: return "wgpu::CompareFunction::GreaterEqual";
    case CompareFunction::Always: return "wgpu::CompareFunction::Always";
    case CompareFunction::Force32: return "wgpu::CompareFunction::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CompilationInfoRequestStatus v) {
    switch (v) {
    case CompilationInfoRequestStatus::Success: return "wgpu::CompilationInfoRequestStatus::Success";
    case CompilationInfoRequestStatus::InstanceDropped: return "wgpu::CompilationInfoRequestStatus::InstanceDropped";
    case CompilationInfoRequestStatus::Error: return "wgpu::CompilationInfoRequestStatus::Error";
    case CompilationInfoRequestStatus::Unknown: return "wgpu::CompilationInfoRequestStatus::Unknown";
    case CompilationInfoRequestStatus::Force32: return "wgpu::CompilationInfoRequestStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CompilationMessageType v) {
    switch (v) {
    case CompilationMessageType::Error: return "wgpu::CompilationMessageType::Error";
    case CompilationMessageType::Warning: return "wgpu::CompilationMessageType::Warning";
    case CompilationMessageType::Info: return "wgpu::CompilationMessageType::Info";
    case CompilationMessageType::Force32: return "wgpu::CompilationMessageType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CompositeAlphaMode v) {
    switch (v) {
    case CompositeAlphaMode::Auto: return "wgpu::CompositeAlphaMode::Auto";
    case CompositeAlphaMode::Opaque: return "wgpu::CompositeAlphaMode::Opaque";
    case CompositeAlphaMode::Premultiplied: return "wgpu::CompositeAlphaMode::Premultiplied";
    case CompositeAlphaMode::Unpremultiplied: return "wgpu::CompositeAlphaMode::Unpremultiplied";
    case CompositeAlphaMode::Inherit: return "wgpu::CompositeAlphaMode::Inherit";
    case CompositeAlphaMode::Force32: return "wgpu::CompositeAlphaMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CreatePipelineAsyncStatus v) {
    switch (v) {
    case CreatePipelineAsyncStatus::Success: return "wgpu::CreatePipelineAsyncStatus::Success";
    case CreatePipelineAsyncStatus::InstanceDropped: return "wgpu::CreatePipelineAsyncStatus::InstanceDropped";
    case CreatePipelineAsyncStatus::ValidationError: return "wgpu::CreatePipelineAsyncStatus::ValidationError";
    case CreatePipelineAsyncStatus::InternalError: return "wgpu::CreatePipelineAsyncStatus::InternalError";
    case CreatePipelineAsyncStatus::Unknown: return "wgpu::CreatePipelineAsyncStatus::Unknown";
    case CreatePipelineAsyncStatus::Force32: return "wgpu::CreatePipelineAsyncStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(CullMode v) {
    switch (v) {
    case CullMode::Undefined: return "wgpu::CullMode::Undefined";
    case CullMode::None: return "wgpu::CullMode::None";
    case CullMode::Front: return "wgpu::CullMode::Front";
    case CullMode::Back: return "wgpu::CullMode::Back";
    case CullMode::Force32: return "wgpu::CullMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(DeviceLostReason v) {
    switch (v) {
    case DeviceLostReason::Unknown: return "wgpu::DeviceLostReason::Unknown";
    case DeviceLostReason::Destroyed: return "wgpu::DeviceLostReason::Destroyed";
    case DeviceLostReason::InstanceDropped: return "wgpu::DeviceLostReason::InstanceDropped";
    case DeviceLostReason::FailedCreation: return "wgpu::DeviceLostReason::FailedCreation";
    case DeviceLostReason::Force32: return "wgpu::DeviceLostReason::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(ErrorFilter v) {
    switch (v) {
    case ErrorFilter::Validation: return "wgpu::ErrorFilter::Validation";
    case ErrorFilter::OutOfMemory: return "wgpu::ErrorFilter::OutOfMemory";
    case ErrorFilter::Internal: return "wgpu::ErrorFilter::Internal";
    case ErrorFilter::Force32: return "wgpu::ErrorFilter::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(ErrorType v) {
    switch (v) {
    case ErrorType::NoError: return "wgpu::ErrorType::NoError";
    case ErrorType::Validation: return "wgpu::ErrorType::Validation";
    case ErrorType::OutOfMemory: return "wgpu::ErrorType::OutOfMemory";
    case ErrorType::Internal: return "wgpu::ErrorType::Internal";
    case ErrorType::Unknown: return "wgpu::ErrorType::Unknown";
    case ErrorType::Force32: return "wgpu::ErrorType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(FeatureLevel v) {
    switch (v) {
    case FeatureLevel::Compatibility: return "wgpu::FeatureLevel::Compatibility";
    case FeatureLevel::Core: return "wgpu::FeatureLevel::Core";
    case FeatureLevel::Force32: return "wgpu::FeatureLevel::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(FeatureName v) {
    switch (v) {
    case FeatureName::Undefined: return "wgpu::FeatureName::Undefined";
    case FeatureName::DepthClipControl: return "wgpu::FeatureName::DepthClipControl";
    case FeatureName::Depth32FloatStencil8: return "wgpu::FeatureName::Depth32FloatStencil8";
    case FeatureName::TimestampQuery: return "wgpu::FeatureName::TimestampQuery";
    case FeatureName::TextureCompressionBC: return "wgpu::FeatureName::TextureCompressionBC";
    case FeatureName::TextureCompressionBCSliced3D: return "wgpu::FeatureName::TextureCompressionBCSliced3D";
    case FeatureName::TextureCompressionETC2: return "wgpu::FeatureName::TextureCompressionETC2";
    case FeatureName::TextureCompressionASTC: return "wgpu::FeatureName::TextureCompressionASTC";
    case FeatureName::TextureCompressionASTCSliced3D: return "wgpu::FeatureName::TextureCompressionASTCSliced3D";
    case FeatureName::IndirectFirstInstance: return "wgpu::FeatureName::IndirectFirstInstance";
    case FeatureName::ShaderF16: return "wgpu::FeatureName::ShaderF16";
    case FeatureName::RG11B10UfloatRenderable: return "wgpu::FeatureName::RG11B10UfloatRenderable";
    case FeatureName::BGRA8UnormStorage: return "wgpu::FeatureName::BGRA8UnormStorage";
    case FeatureName::Float32Filterable: return "wgpu::FeatureName::Float32Filterable";
    case FeatureName::Float32Blendable: return "wgpu::FeatureName::Float32Blendable";
    case FeatureName::ClipDistances: return "wgpu::FeatureName::ClipDistances";
    case FeatureName::DualSourceBlending: return "wgpu::FeatureName::DualSourceBlending";
    case FeatureName::Force32: return "wgpu::FeatureName::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(FilterMode v) {
    switch (v) {
    case FilterMode::Undefined: return "wgpu::FilterMode::Undefined";
    case FilterMode::Nearest: return "wgpu::FilterMode::Nearest";
    case FilterMode::Linear: return "wgpu::FilterMode::Linear";
    case FilterMode::Force32: return "wgpu::FilterMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(FrontFace v) {
    switch (v) {
    case FrontFace::Undefined: return "wgpu::FrontFace::Undefined";
    case FrontFace::CCW: return "wgpu::FrontFace::CCW";
    case FrontFace::CW: return "wgpu::FrontFace::CW";
    case FrontFace::Force32: return "wgpu::FrontFace::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(IndexFormat v) {
    switch (v) {
    case IndexFormat::Undefined: return "wgpu::IndexFormat::Undefined";
    case IndexFormat::Uint16: return "wgpu::IndexFormat::Uint16";
    case IndexFormat::Uint32: return "wgpu::IndexFormat::Uint32";
    case IndexFormat::Force32: return "wgpu::IndexFormat::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(LoadOp v) {
    switch (v) {
    case LoadOp::Undefined: return "wgpu::LoadOp::Undefined";
    case LoadOp::Load: return "wgpu::LoadOp::Load";
    case LoadOp::Clear: return "wgpu::LoadOp::Clear";
    case LoadOp::Force32: return "wgpu::LoadOp::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(MapAsyncStatus v) {
    switch (v) {
    case MapAsyncStatus::Success: return "wgpu::MapAsyncStatus::Success";
    case MapAsyncStatus::InstanceDropped: return "wgpu::MapAsyncStatus::InstanceDropped";
    case MapAsyncStatus::Error: return "wgpu::MapAsyncStatus::Error";
    case MapAsyncStatus::Aborted: return "wgpu::MapAsyncStatus::Aborted";
    case MapAsyncStatus::Unknown: return "wgpu::MapAsyncStatus::Unknown";
    case MapAsyncStatus::Force32: return "wgpu::MapAsyncStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(MipmapFilterMode v) {
    switch (v) {
    case MipmapFilterMode::Undefined: return "wgpu::MipmapFilterMode::Undefined";
    case MipmapFilterMode::Nearest: return "wgpu::MipmapFilterMode::Nearest";
    case MipmapFilterMode::Linear: return "wgpu::MipmapFilterMode::Linear";
    case MipmapFilterMode::Force32: return "wgpu::MipmapFilterMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(OptionalBool v) {
    switch (v) {
    case OptionalBool::False: return "wgpu::OptionalBool::False";
    case OptionalBool::True: return "wgpu::OptionalBool::True";
    case OptionalBool::Undefined: return "wgpu::OptionalBool::Undefined";
    case OptionalBool::Force32: return "wgpu::OptionalBool::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(PopErrorScopeStatus v) {
    switch (v) {
    case PopErrorScopeStatus::Success: return "wgpu::PopErrorScopeStatus::Success";
    case PopErrorScopeStatus::InstanceDropped: return "wgpu::PopErrorScopeStatus::InstanceDropped";
    case PopErrorScopeStatus::EmptyStack: return "wgpu::PopErrorScopeStatus::EmptyStack";
    case PopErrorScopeStatus::Force32: return "wgpu::PopErrorScopeStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(PowerPreference v) {
    switch (v) {
    case PowerPreference::Undefined: return "wgpu::PowerPreference::Undefined";
    case PowerPreference::LowPower: return "wgpu::PowerPreference::LowPower";
    case PowerPreference::HighPerformance: return "wgpu::PowerPreference::HighPerformance";
    case PowerPreference::Force32: return "wgpu::PowerPreference::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(PresentMode v) {
    switch (v) {
    case PresentMode::Undefined: return "wgpu::PresentMode::Undefined";
    case PresentMode::Fifo: return "wgpu::PresentMode::Fifo";
    case PresentMode::FifoRelaxed: return "wgpu::PresentMode::FifoRelaxed";
    case PresentMode::Immediate: return "wgpu::PresentMode::Immediate";
    case PresentMode::Mailbox: return "wgpu::PresentMode::Mailbox";
    case PresentMode::Force32: return "wgpu::PresentMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(PrimitiveTopology v) {
    switch (v) {
    case PrimitiveTopology::Undefined: return "wgpu::PrimitiveTopology::Undefined";
    case PrimitiveTopology::PointList: return "wgpu::PrimitiveTopology::PointList";
    case PrimitiveTopology::LineList: return "wgpu::PrimitiveTopology::LineList";
    case PrimitiveTopology::LineStrip: return "wgpu::PrimitiveTopology::LineStrip";
    case PrimitiveTopology::TriangleList: return "wgpu::PrimitiveTopology::TriangleList";
    case PrimitiveTopology::TriangleStrip: return "wgpu::PrimitiveTopology::TriangleStrip";
    case PrimitiveTopology::Force32: return "wgpu::PrimitiveTopology::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(QueryType v) {
    switch (v) {
    case QueryType::Occlusion: return "wgpu::QueryType::Occlusion";
    case QueryType::Timestamp: return "wgpu::QueryType::Timestamp";
    case QueryType::Force32: return "wgpu::QueryType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(QueueWorkDoneStatus v) {
    switch (v) {
    case QueueWorkDoneStatus::Success: return "wgpu::QueueWorkDoneStatus::Success";
    case QueueWorkDoneStatus::InstanceDropped: return "wgpu::QueueWorkDoneStatus::InstanceDropped";
    case QueueWorkDoneStatus::Error: return "wgpu::QueueWorkDoneStatus::Error";
    case QueueWorkDoneStatus::Unknown: return "wgpu::QueueWorkDoneStatus::Unknown";
    case QueueWorkDoneStatus::Force32: return "wgpu::QueueWorkDoneStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(RequestAdapterStatus v) {
    switch (v) {
    case RequestAdapterStatus::Success: return "wgpu::RequestAdapterStatus::Success";
    case RequestAdapterStatus::InstanceDropped: return "wgpu::RequestAdapterStatus::InstanceDropped";
    case RequestAdapterStatus::Unavailable: return "wgpu::RequestAdapterStatus::Unavailable";
    case RequestAdapterStatus::Error: return "wgpu::RequestAdapterStatus::Error";
    case RequestAdapterStatus::Unknown: return "wgpu::RequestAdapterStatus::Unknown";
    case RequestAdapterStatus::Force32: return "wgpu::RequestAdapterStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(RequestDeviceStatus v) {
    switch (v) {
    case RequestDeviceStatus::Success: return "wgpu::RequestDeviceStatus::Success";
    case RequestDeviceStatus::InstanceDropped: return "wgpu::RequestDeviceStatus::InstanceDropped";
    case RequestDeviceStatus::Error: return "wgpu::RequestDeviceStatus::Error";
    case RequestDeviceStatus::Unknown: return "wgpu::RequestDeviceStatus::Unknown";
    case RequestDeviceStatus::Force32: return "wgpu::RequestDeviceStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(SType v) {
    switch (v) {
    case SType::ShaderSourceSPIRV: return "wgpu::SType::ShaderSourceSPIRV";
    case SType::ShaderSourceWGSL: return "wgpu::SType::ShaderSourceWGSL";
    case SType::RenderPassMaxDrawCount: return "wgpu::SType::RenderPassMaxDrawCount";
    case SType::SurfaceSourceMetalLayer: return "wgpu::SType::SurfaceSourceMetalLayer";
    case SType::SurfaceSourceWindowsHWND: return "wgpu::SType::SurfaceSourceWindowsHWND";
    case SType::SurfaceSourceXlibWindow: return "wgpu::SType::SurfaceSourceXlibWindow";
    case SType::SurfaceSourceWaylandSurface: return "wgpu::SType::SurfaceSourceWaylandSurface";
    case SType::SurfaceSourceAndroidNativeWindow: return "wgpu::SType::SurfaceSourceAndroidNativeWindow";
    case SType::SurfaceSourceXCBWindow: return "wgpu::SType::SurfaceSourceXCBWindow";
    case SType::Force32: return "wgpu::SType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(SamplerBindingType v) {
    switch (v) {
    case SamplerBindingType::BindingNotUsed: return "wgpu::SamplerBindingType::BindingNotUsed";
    case SamplerBindingType::Undefined: return "wgpu::SamplerBindingType::Undefined";
    case SamplerBindingType::Filtering: return "wgpu::SamplerBindingType::Filtering";
    case SamplerBindingType::NonFiltering: return "wgpu::SamplerBindingType::NonFiltering";
    case SamplerBindingType::Comparison: return "wgpu::SamplerBindingType::Comparison";
    case SamplerBindingType::Force32: return "wgpu::SamplerBindingType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(Status v) {
    switch (v) {
    case Status::Success: return "wgpu::Status::Success";
    case Status::Error: return "wgpu::Status::Error";
    case Status::Force32: return "wgpu::Status::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(StencilOperation v) {
    switch (v) {
    case StencilOperation::Undefined: return "wgpu::StencilOperation::Undefined";
    case StencilOperation::Keep: return "wgpu::StencilOperation::Keep";
    case StencilOperation::Zero: return "wgpu::StencilOperation::Zero";
    case StencilOperation::Replace: return "wgpu::StencilOperation::Replace";
    case StencilOperation::Invert: return "wgpu::StencilOperation::Invert";
    case StencilOperation::IncrementClamp: return "wgpu::StencilOperation::IncrementClamp";
    case StencilOperation::DecrementClamp: return "wgpu::StencilOperation::DecrementClamp";
    case StencilOperation::IncrementWrap: return "wgpu::StencilOperation::IncrementWrap";
    case StencilOperation::DecrementWrap: return "wgpu::StencilOperation::DecrementWrap";
    case StencilOperation::Force32: return "wgpu::StencilOperation::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(StorageTextureAccess v) {
    switch (v) {
    case StorageTextureAccess::BindingNotUsed: return "wgpu::StorageTextureAccess::BindingNotUsed";
    case StorageTextureAccess::Undefined: return "wgpu::StorageTextureAccess::Undefined";
    case StorageTextureAccess::WriteOnly: return "wgpu::StorageTextureAccess::WriteOnly";
    case StorageTextureAccess::ReadOnly: return "wgpu::StorageTextureAccess::ReadOnly";
    case StorageTextureAccess::ReadWrite: return "wgpu::StorageTextureAccess::ReadWrite";
    case StorageTextureAccess::Force32: return "wgpu::StorageTextureAccess::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(StoreOp v) {
    switch (v) {
    case StoreOp::Undefined: return "wgpu::StoreOp::Undefined";
    case StoreOp::Store: return "wgpu::StoreOp::Store";
    case StoreOp::Discard: return "wgpu::StoreOp::Discard";
    case StoreOp::Force32: return "wgpu::StoreOp::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(SurfaceGetCurrentTextureStatus v) {
    switch (v) {
    case SurfaceGetCurrentTextureStatus::SuccessOptimal: return "wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal";
    case SurfaceGetCurrentTextureStatus::SuccessSuboptimal: return "wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal";
    case SurfaceGetCurrentTextureStatus::Timeout: return "wgpu::SurfaceGetCurrentTextureStatus::Timeout";
    case SurfaceGetCurrentTextureStatus::Outdated: return "wgpu::SurfaceGetCurrentTextureStatus::Outdated";
    case SurfaceGetCurrentTextureStatus::Lost: return "wgpu::SurfaceGetCurrentTextureStatus::Lost";
    case SurfaceGetCurrentTextureStatus::OutOfMemory: return "wgpu::SurfaceGetCurrentTextureStatus::OutOfMemory";
    case SurfaceGetCurrentTextureStatus::DeviceLost: return "wgpu::SurfaceGetCurrentTextureStatus::DeviceLost";
    case SurfaceGetCurrentTextureStatus::Error: return "wgpu::SurfaceGetCurrentTextureStatus::Error";
    case SurfaceGetCurrentTextureStatus::Force32: return "wgpu::SurfaceGetCurrentTextureStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(TextureAspect v) {
    switch (v) {
    case TextureAspect::Undefined: return "wgpu::TextureAspect::Undefined";
    case TextureAspect::All: return "wgpu::TextureAspect::All";
    case TextureAspect::StencilOnly: return "wgpu::TextureAspect::StencilOnly";
    case TextureAspect::DepthOnly: return "wgpu::TextureAspect::DepthOnly";
    case TextureAspect::Force32: return "wgpu::TextureAspect::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(TextureDimension v) {
    switch (v) {
    case TextureDimension::Undefined: return "wgpu::TextureDimension::Undefined";
    case TextureDimension::_1D: return "wgpu::TextureDimension::_1D";
    case TextureDimension::_2D: return "wgpu::TextureDimension::_2D";
    case TextureDimension::_3D: return "wgpu::TextureDimension::_3D";
    case TextureDimension::Force32: return "wgpu::TextureDimension::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(TextureFormat v) {
    switch (v) {
    case TextureFormat::Undefined: return "wgpu::TextureFormat::Undefined";
    case TextureFormat::R8Unorm: return "wgpu::TextureFormat::R8Unorm";
    case TextureFormat::R8Snorm: return "wgpu::TextureFormat::R8Snorm";
    case TextureFormat::R8Uint: return "wgpu::TextureFormat::R8Uint";
    case TextureFormat::R8Sint: return "wgpu::TextureFormat::R8Sint";
    case TextureFormat::R16Uint: return "wgpu::TextureFormat::R16Uint";
    case TextureFormat::R16Sint: return "wgpu::TextureFormat::R16Sint";
    case TextureFormat::R16Float: return "wgpu::TextureFormat::R16Float";
    case TextureFormat::RG8Unorm: return "wgpu::TextureFormat::RG8Unorm";
    case TextureFormat::RG8Snorm: return "wgpu::TextureFormat::RG8Snorm";
    case TextureFormat::RG8Uint: return "wgpu::TextureFormat::RG8Uint";
    case TextureFormat::RG8Sint: return "wgpu::TextureFormat::RG8Sint";
    case TextureFormat::R32Float: return "wgpu::TextureFormat::R32Float";
    case TextureFormat::R32Uint: return "wgpu::TextureFormat::R32Uint";
    case TextureFormat::R32Sint: return "wgpu::TextureFormat::R32Sint";
    case TextureFormat::RG16Uint: return "wgpu::TextureFormat::RG16Uint";
    case TextureFormat::RG16Sint: return "wgpu::TextureFormat::RG16Sint";
    case TextureFormat::RG16Float: return "wgpu::TextureFormat::RG16Float";
    case TextureFormat::RGBA8Unorm: return "wgpu::TextureFormat::RGBA8Unorm";
    case TextureFormat::RGBA8UnormSrgb: return "wgpu::TextureFormat::RGBA8UnormSrgb";
    case TextureFormat::RGBA8Snorm: return "wgpu::TextureFormat::RGBA8Snorm";
    case TextureFormat::RGBA8Uint: return "wgpu::TextureFormat::RGBA8Uint";
    case TextureFormat::RGBA8Sint: return "wgpu::TextureFormat::RGBA8Sint";
    case TextureFormat::BGRA8Unorm: return "wgpu::TextureFormat::BGRA8Unorm";
    case TextureFormat::BGRA8UnormSrgb: return "wgpu::TextureFormat::BGRA8UnormSrgb";
    case TextureFormat::RGB10A2Uint: return "wgpu::TextureFormat::RGB10A2Uint";
    case TextureFormat::RGB10A2Unorm: return "wgpu::TextureFormat::RGB10A2Unorm";
    case TextureFormat::RG11B10Ufloat: return "wgpu::TextureFormat::RG11B10Ufloat";
    case TextureFormat::RGB9E5Ufloat: return "wgpu::TextureFormat::RGB9E5Ufloat";
    case TextureFormat::RG32Float: return "wgpu::TextureFormat::RG32Float";
    case TextureFormat::RG32Uint: return "wgpu::TextureFormat::RG32Uint";
    case TextureFormat::RG32Sint: return "wgpu::TextureFormat::RG32Sint";
    case TextureFormat::RGBA16Uint: return "wgpu::TextureFormat::RGBA16Uint";
    case TextureFormat::RGBA16Sint: return "wgpu::TextureFormat::RGBA16Sint";
    case TextureFormat::RGBA16Float: return "wgpu::TextureFormat::RGBA16Float";
    case TextureFormat::RGBA32Float: return "wgpu::TextureFormat::RGBA32Float";
    case TextureFormat::RGBA32Uint: return "wgpu::TextureFormat::RGBA32Uint";
    case TextureFormat::RGBA32Sint: return "wgpu::TextureFormat::RGBA32Sint";
    case TextureFormat::Stencil8: return "wgpu::TextureFormat::Stencil8";
    case TextureFormat::Depth16Unorm: return "wgpu::TextureFormat::Depth16Unorm";
    case TextureFormat::Depth24Plus: return "wgpu::TextureFormat::Depth24Plus";
    case TextureFormat::Depth24PlusStencil8: return "wgpu::TextureFormat::Depth24PlusStencil8";
    case TextureFormat::Depth32Float: return "wgpu::TextureFormat::Depth32Float";
    case TextureFormat::Depth32FloatStencil8: return "wgpu::TextureFormat::Depth32FloatStencil8";
    case TextureFormat::BC1RGBAUnorm: return "wgpu::TextureFormat::BC1RGBAUnorm";
    case TextureFormat::BC1RGBAUnormSrgb: return "wgpu::TextureFormat::BC1RGBAUnormSrgb";
    case TextureFormat::BC2RGBAUnorm: return "wgpu::TextureFormat::BC2RGBAUnorm";
    case TextureFormat::BC2RGBAUnormSrgb: return "wgpu::TextureFormat::BC2RGBAUnormSrgb";
    case TextureFormat::BC3RGBAUnorm: return "wgpu::TextureFormat::BC3RGBAUnorm";
    case TextureFormat::BC3RGBAUnormSrgb: return "wgpu::TextureFormat::BC3RGBAUnormSrgb";
    case TextureFormat::BC4RUnorm: return "wgpu::TextureFormat::BC4RUnorm";
    case TextureFormat::BC4RSnorm: return "wgpu::TextureFormat::BC4RSnorm";
    case TextureFormat::BC5RGUnorm: return "wgpu::TextureFormat::BC5RGUnorm";
    case TextureFormat::BC5RGSnorm: return "wgpu::TextureFormat::BC5RGSnorm";
    case TextureFormat::BC6HRGBUfloat: return "wgpu::TextureFormat::BC6HRGBUfloat";
    case TextureFormat::BC6HRGBFloat: return "wgpu::TextureFormat::BC6HRGBFloat";
    case TextureFormat::BC7RGBAUnorm: return "wgpu::TextureFormat::BC7RGBAUnorm";
    case TextureFormat::BC7RGBAUnormSrgb: return "wgpu::TextureFormat::BC7RGBAUnormSrgb";
    case TextureFormat::ETC2RGB8Unorm: return "wgpu::TextureFormat::ETC2RGB8Unorm";
    case TextureFormat::ETC2RGB8UnormSrgb: return "wgpu::TextureFormat::ETC2RGB8UnormSrgb";
    case TextureFormat::ETC2RGB8A1Unorm: return "wgpu::TextureFormat::ETC2RGB8A1Unorm";
    case TextureFormat::ETC2RGB8A1UnormSrgb: return "wgpu::TextureFormat::ETC2RGB8A1UnormSrgb";
    case TextureFormat::ETC2RGBA8Unorm: return "wgpu::TextureFormat::ETC2RGBA8Unorm";
    case TextureFormat::ETC2RGBA8UnormSrgb: return "wgpu::TextureFormat::ETC2RGBA8UnormSrgb";
    case TextureFormat::EACR11Unorm: return "wgpu::TextureFormat::EACR11Unorm";
    case TextureFormat::EACR11Snorm: return "wgpu::TextureFormat::EACR11Snorm";
    case TextureFormat::EACRG11Unorm: return "wgpu::TextureFormat::EACRG11Unorm";
    case TextureFormat::EACRG11Snorm: return "wgpu::TextureFormat::EACRG11Snorm";
    case TextureFormat::ASTC4x4Unorm: return "wgpu::TextureFormat::ASTC4x4Unorm";
    case TextureFormat::ASTC4x4UnormSrgb: return "wgpu::TextureFormat::ASTC4x4UnormSrgb";
    case TextureFormat::ASTC5x4Unorm: return "wgpu::TextureFormat::ASTC5x4Unorm";
    case TextureFormat::ASTC5x4UnormSrgb: return "wgpu::TextureFormat::ASTC5x4UnormSrgb";
    case TextureFormat::ASTC5x5Unorm: return "wgpu::TextureFormat::ASTC5x5Unorm";
    case TextureFormat::ASTC5x5UnormSrgb: return "wgpu::TextureFormat::ASTC5x5UnormSrgb";
    case TextureFormat::ASTC6x5Unorm: return "wgpu::TextureFormat::ASTC6x5Unorm";
    case TextureFormat::ASTC6x5UnormSrgb: return "wgpu::TextureFormat::ASTC6x5UnormSrgb";
    case TextureFormat::ASTC6x6Unorm: return "wgpu::TextureFormat::ASTC6x6Unorm";
    case TextureFormat::ASTC6x6UnormSrgb: return "wgpu::TextureFormat::ASTC6x6UnormSrgb";
    case TextureFormat::ASTC8x5Unorm: return "wgpu::TextureFormat::ASTC8x5Unorm";
    case TextureFormat::ASTC8x5UnormSrgb: return "wgpu::TextureFormat::ASTC8x5UnormSrgb";
    case TextureFormat::ASTC8x6Unorm: return "wgpu::TextureFormat::ASTC8x6Unorm";
    case TextureFormat::ASTC8x6UnormSrgb: return "wgpu::TextureFormat::ASTC8x6UnormSrgb";
    case TextureFormat::ASTC8x8Unorm: return "wgpu::TextureFormat::ASTC8x8Unorm";
    case TextureFormat::ASTC8x8UnormSrgb: return "wgpu::TextureFormat::ASTC8x8UnormSrgb";
    case TextureFormat::ASTC10x5Unorm: return "wgpu::TextureFormat::ASTC10x5Unorm";
    case TextureFormat::ASTC10x5UnormSrgb: return "wgpu::TextureFormat::ASTC10x5UnormSrgb";
    case TextureFormat::ASTC10x6Unorm: return "wgpu::TextureFormat::ASTC10x6Unorm";
    case TextureFormat::ASTC10x6UnormSrgb: return "wgpu::TextureFormat::ASTC10x6UnormSrgb";
    case TextureFormat::ASTC10x8Unorm: return "wgpu::TextureFormat::ASTC10x8Unorm";
    case TextureFormat::ASTC10x8UnormSrgb: return "wgpu::TextureFormat::ASTC10x8UnormSrgb";
    case TextureFormat::ASTC10x10Unorm: return "wgpu::TextureFormat::ASTC10x10Unorm";
    case TextureFormat::ASTC10x10UnormSrgb: return "wgpu::TextureFormat::ASTC10x10UnormSrgb";
    case TextureFormat::ASTC12x10Unorm: return "wgpu::TextureFormat::ASTC12x10Unorm";
    case TextureFormat::ASTC12x10UnormSrgb: return "wgpu::TextureFormat::ASTC12x10UnormSrgb";
    case TextureFormat::ASTC12x12Unorm: return "wgpu::TextureFormat::ASTC12x12Unorm";
    case TextureFormat::ASTC12x12UnormSrgb: return "wgpu::TextureFormat::ASTC12x12UnormSrgb";
    case TextureFormat::Force32: return "wgpu::TextureFormat::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(TextureSampleType v) {
    switch (v) {
    case TextureSampleType::BindingNotUsed: return "wgpu::TextureSampleType::BindingNotUsed";
    case TextureSampleType::Undefined: return "wgpu::TextureSampleType::Undefined";
    case TextureSampleType::Float: return "wgpu::TextureSampleType::Float";
    case TextureSampleType::UnfilterableFloat: return "wgpu::TextureSampleType::UnfilterableFloat";
    case TextureSampleType::Depth: return "wgpu::TextureSampleType::Depth";
    case TextureSampleType::Sint: return "wgpu::TextureSampleType::Sint";
    case TextureSampleType::Uint: return "wgpu::TextureSampleType::Uint";
    case TextureSampleType::Force32: return "wgpu::TextureSampleType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(TextureViewDimension v) {
    switch (v) {
    case TextureViewDimension::Undefined: return "wgpu::TextureViewDimension::Undefined";
    case TextureViewDimension::_1D: return "wgpu::TextureViewDimension::_1D";
    case TextureViewDimension::_2D: return "wgpu::TextureViewDimension::_2D";
    case TextureViewDimension::_2DArray: return "wgpu::TextureViewDimension::_2DArray";
    case TextureViewDimension::Cube: return "wgpu::TextureViewDimension::Cube";
    case TextureViewDimension::CubeArray: return "wgpu::TextureViewDimension::CubeArray";
    case TextureViewDimension::_3D: return "wgpu::TextureViewDimension::_3D";
    case TextureViewDimension::Force32: return "wgpu::TextureViewDimension::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(VertexFormat v) {
    switch (v) {
    case VertexFormat::Uint8: return "wgpu::VertexFormat::Uint8";
    case VertexFormat::Uint8x2: return "wgpu::VertexFormat::Uint8x2";
    case VertexFormat::Uint8x4: return "wgpu::VertexFormat::Uint8x4";
    case VertexFormat::Sint8: return "wgpu::VertexFormat::Sint8";
    case VertexFormat::Sint8x2: return "wgpu::VertexFormat::Sint8x2";
    case VertexFormat::Sint8x4: return "wgpu::VertexFormat::Sint8x4";
    case VertexFormat::Unorm8: return "wgpu::VertexFormat::Unorm8";
    case VertexFormat::Unorm8x2: return "wgpu::VertexFormat::Unorm8x2";
    case VertexFormat::Unorm8x4: return "wgpu::VertexFormat::Unorm8x4";
    case VertexFormat::Snorm8: return "wgpu::VertexFormat::Snorm8";
    case VertexFormat::Snorm8x2: return "wgpu::VertexFormat::Snorm8x2";
    case VertexFormat::Snorm8x4: return "wgpu::VertexFormat::Snorm8x4";
    case VertexFormat::Uint16: return "wgpu::VertexFormat::Uint16";
    case VertexFormat::Uint16x2: return "wgpu::VertexFormat::Uint16x2";
    case VertexFormat::Uint16x4: return "wgpu::VertexFormat::Uint16x4";
    case VertexFormat::Sint16: return "wgpu::VertexFormat::Sint16";
    case VertexFormat::Sint16x2: return "wgpu::VertexFormat::Sint16x2";
    case VertexFormat::Sint16x4: return "wgpu::VertexFormat::Sint16x4";
    case VertexFormat::Unorm16: return "wgpu::VertexFormat::Unorm16";
    case VertexFormat::Unorm16x2: return "wgpu::VertexFormat::Unorm16x2";
    case VertexFormat::Unorm16x4: return "wgpu::VertexFormat::Unorm16x4";
    case VertexFormat::Snorm16: return "wgpu::VertexFormat::Snorm16";
    case VertexFormat::Snorm16x2: return "wgpu::VertexFormat::Snorm16x2";
    case VertexFormat::Snorm16x4: return "wgpu::VertexFormat::Snorm16x4";
    case VertexFormat::Float16: return "wgpu::VertexFormat::Float16";
    case VertexFormat::Float16x2: return "wgpu::VertexFormat::Float16x2";
    case VertexFormat::Float16x4: return "wgpu::VertexFormat::Float16x4";
    case VertexFormat::Float32: return "wgpu::VertexFormat::Float32";
    case VertexFormat::Float32x2: return "wgpu::VertexFormat::Float32x2";
    case VertexFormat::Float32x3: return "wgpu::VertexFormat::Float32x3";
    case VertexFormat::Float32x4: return "wgpu::VertexFormat::Float32x4";
    case VertexFormat::Uint32: return "wgpu::VertexFormat::Uint32";
    case VertexFormat::Uint32x2: return "wgpu::VertexFormat::Uint32x2";
    case VertexFormat::Uint32x3: return "wgpu::VertexFormat::Uint32x3";
    case VertexFormat::Uint32x4: return "wgpu::VertexFormat::Uint32x4";
    case VertexFormat::Sint32: return "wgpu::VertexFormat::Sint32";
    case VertexFormat::Sint32x2: return "wgpu::VertexFormat::Sint32x2";
    case VertexFormat::Sint32x3: return "wgpu::VertexFormat::Sint32x3";
    case VertexFormat::Sint32x4: return "wgpu::VertexFormat::Sint32x4";
    case VertexFormat::Unorm10_10_10_2: return "wgpu::VertexFormat::Unorm10_10_10_2";
    case VertexFormat::Unorm8x4BGRA: return "wgpu::VertexFormat::Unorm8x4BGRA";
    case VertexFormat::Force32: return "wgpu::VertexFormat::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(VertexStepMode v) {
    switch (v) {
    case VertexStepMode::VertexBufferNotUsed: return "wgpu::VertexStepMode::VertexBufferNotUsed";
    case VertexStepMode::Undefined: return "wgpu::VertexStepMode::Undefined";
    case VertexStepMode::Vertex: return "wgpu::VertexStepMode::Vertex";
    case VertexStepMode::Instance: return "wgpu::VertexStepMode::Instance";
    case VertexStepMode::Force32: return "wgpu::VertexStepMode::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(WGSLLanguageFeatureName v) {
    switch (v) {
    case WGSLLanguageFeatureName::ReadonlyAndReadwriteStorageTextures: return "wgpu::WGSLLanguageFeatureName::ReadonlyAndReadwriteStorageTextures";
    case WGSLLanguageFeatureName::Packed4x8IntegerDotProduct: return "wgpu::WGSLLanguageFeatureName::Packed4x8IntegerDotProduct";
    case WGSLLanguageFeatureName::UnrestrictedPointerParameters: return "wgpu::WGSLLanguageFeatureName::UnrestrictedPointerParameters";
    case WGSLLanguageFeatureName::PointerCompositeAccess: return "wgpu::WGSLLanguageFeatureName::PointerCompositeAccess";
    case WGSLLanguageFeatureName::Force32: return "wgpu::WGSLLanguageFeatureName::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(WaitStatus v) {
    switch (v) {
    case WaitStatus::Success: return "wgpu::WaitStatus::Success";
    case WaitStatus::TimedOut: return "wgpu::WaitStatus::TimedOut";
    case WaitStatus::UnsupportedTimeout: return "wgpu::WaitStatus::UnsupportedTimeout";
    case WaitStatus::UnsupportedCount: return "wgpu::WaitStatus::UnsupportedCount";
    case WaitStatus::UnsupportedMixedSources: return "wgpu::WaitStatus::UnsupportedMixedSources";
    case WaitStatus::Force32: return "wgpu::WaitStatus::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(BufferUsage v) {
    switch (v) {
    case BufferUsage::None: return "wgpu::BufferUsage::None";
    case BufferUsage::MapRead: return "wgpu::BufferUsage::MapRead";
    case BufferUsage::MapWrite: return "wgpu::BufferUsage::MapWrite";
    case BufferUsage::CopySrc: return "wgpu::BufferUsage::CopySrc";
    case BufferUsage::CopyDst: return "wgpu::BufferUsage::CopyDst";
    case BufferUsage::Index: return "wgpu::BufferUsage::Index";
    case BufferUsage::Vertex: return "wgpu::BufferUsage::Vertex";
    case BufferUsage::Uniform: return "wgpu::BufferUsage::Uniform";
    case BufferUsage::Storage: return "wgpu::BufferUsage::Storage";
    case BufferUsage::Indirect: return "wgpu::BufferUsage::Indirect";
    case BufferUsage::QueryResolve: return "wgpu::BufferUsage::QueryResolve";
    default: return "Unknown";
    }
}

std::string_view to_string(ColorWriteMask v) {
    switch (v) {
    case ColorWriteMask::None: return "wgpu::ColorWriteMask::None";
    case ColorWriteMask::Red: return "wgpu::ColorWriteMask::Red";
    case ColorWriteMask::Green: return "wgpu::ColorWriteMask::Green";
    case ColorWriteMask::Blue: return "wgpu::ColorWriteMask::Blue";
    case ColorWriteMask::Alpha: return "wgpu::ColorWriteMask::Alpha";
    case ColorWriteMask::All: return "wgpu::ColorWriteMask::All";
    default: return "Unknown";
    }
}

std::string_view to_string(MapMode v) {
    switch (v) {
    case MapMode::None: return "wgpu::MapMode::None";
    case MapMode::Read: return "wgpu::MapMode::Read";
    case MapMode::Write: return "wgpu::MapMode::Write";
    default: return "Unknown";
    }
}

std::string_view to_string(ShaderStage v) {
    switch (v) {
    case ShaderStage::None: return "wgpu::ShaderStage::None";
    case ShaderStage::Vertex: return "wgpu::ShaderStage::Vertex";
    case ShaderStage::Fragment: return "wgpu::ShaderStage::Fragment";
    case ShaderStage::Compute: return "wgpu::ShaderStage::Compute";
    default: return "Unknown";
    }
}

std::string_view to_string(TextureUsage v) {
    switch (v) {
    case TextureUsage::None: return "wgpu::TextureUsage::None";
    case TextureUsage::CopySrc: return "wgpu::TextureUsage::CopySrc";
    case TextureUsage::CopyDst: return "wgpu::TextureUsage::CopyDst";
    case TextureUsage::TextureBinding: return "wgpu::TextureUsage::TextureBinding";
    case TextureUsage::StorageBinding: return "wgpu::TextureUsage::StorageBinding";
    case TextureUsage::RenderAttachment: return "wgpu::TextureUsage::RenderAttachment";
    default: return "Unknown";
    }
}

std::string_view to_string(NativeSType v) {
    switch (v) {
    case NativeSType::DeviceExtras: return "wgpu::NativeSType::DeviceExtras";
    case NativeSType::NativeLimits: return "wgpu::NativeSType::NativeLimits";
    case NativeSType::PipelineLayoutExtras: return "wgpu::NativeSType::PipelineLayoutExtras";
    case NativeSType::ShaderSourceGLSL: return "wgpu::NativeSType::ShaderSourceGLSL";
    case NativeSType::InstanceExtras: return "wgpu::NativeSType::InstanceExtras";
    case NativeSType::BindGroupEntryExtras: return "wgpu::NativeSType::BindGroupEntryExtras";
    case NativeSType::BindGroupLayoutEntryExtras: return "wgpu::NativeSType::BindGroupLayoutEntryExtras";
    case NativeSType::QuerySetDescriptorExtras: return "wgpu::NativeSType::QuerySetDescriptorExtras";
    case NativeSType::SurfaceConfigurationExtras: return "wgpu::NativeSType::SurfaceConfigurationExtras";
    case NativeSType::SurfaceSourceSwapChainPanel: return "wgpu::NativeSType::SurfaceSourceSwapChainPanel";
    case NativeSType::PrimitiveStateExtras: return "wgpu::NativeSType::PrimitiveStateExtras";
    case NativeSType::Force32: return "wgpu::NativeSType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(NativeFeature v) {
    switch (v) {
    case NativeFeature::PushConstants: return "wgpu::NativeFeature::PushConstants";
    case NativeFeature::TextureAdapterSpecificFormatFeatures: return "wgpu::NativeFeature::TextureAdapterSpecificFormatFeatures";
    case NativeFeature::MultiDrawIndirectCount: return "wgpu::NativeFeature::MultiDrawIndirectCount";
    case NativeFeature::VertexWritableStorage: return "wgpu::NativeFeature::VertexWritableStorage";
    case NativeFeature::TextureBindingArray: return "wgpu::NativeFeature::TextureBindingArray";
    case NativeFeature::SampledTextureAndStorageBufferArrayNonUniformIndexing: return "wgpu::NativeFeature::SampledTextureAndStorageBufferArrayNonUniformIndexing";
    case NativeFeature::PipelineStatisticsQuery: return "wgpu::NativeFeature::PipelineStatisticsQuery";
    case NativeFeature::StorageResourceBindingArray: return "wgpu::NativeFeature::StorageResourceBindingArray";
    case NativeFeature::PartiallyBoundBindingArray: return "wgpu::NativeFeature::PartiallyBoundBindingArray";
    case NativeFeature::TextureFormat16bitNorm: return "wgpu::NativeFeature::TextureFormat16bitNorm";
    case NativeFeature::TextureCompressionAstcHdr: return "wgpu::NativeFeature::TextureCompressionAstcHdr";
    case NativeFeature::MappablePrimaryBuffers: return "wgpu::NativeFeature::MappablePrimaryBuffers";
    case NativeFeature::BufferBindingArray: return "wgpu::NativeFeature::BufferBindingArray";
    case NativeFeature::UniformBufferAndStorageTextureArrayNonUniformIndexing: return "wgpu::NativeFeature::UniformBufferAndStorageTextureArrayNonUniformIndexing";
    case NativeFeature::PolygonModeLine: return "wgpu::NativeFeature::PolygonModeLine";
    case NativeFeature::PolygonModePoint: return "wgpu::NativeFeature::PolygonModePoint";
    case NativeFeature::ConservativeRasterization: return "wgpu::NativeFeature::ConservativeRasterization";
    case NativeFeature::SpirvShaderPassthrough: return "wgpu::NativeFeature::SpirvShaderPassthrough";
    case NativeFeature::VertexAttribute64bit: return "wgpu::NativeFeature::VertexAttribute64bit";
    case NativeFeature::TextureFormatNv12: return "wgpu::NativeFeature::TextureFormatNv12";
    case NativeFeature::RayQuery: return "wgpu::NativeFeature::RayQuery";
    case NativeFeature::ShaderF64: return "wgpu::NativeFeature::ShaderF64";
    case NativeFeature::ShaderI16: return "wgpu::NativeFeature::ShaderI16";
    case NativeFeature::ShaderPrimitiveIndex: return "wgpu::NativeFeature::ShaderPrimitiveIndex";
    case NativeFeature::ShaderEarlyDepthTest: return "wgpu::NativeFeature::ShaderEarlyDepthTest";
    case NativeFeature::Subgroup: return "wgpu::NativeFeature::Subgroup";
    case NativeFeature::SubgroupVertex: return "wgpu::NativeFeature::SubgroupVertex";
    case NativeFeature::SubgroupBarrier: return "wgpu::NativeFeature::SubgroupBarrier";
    case NativeFeature::TimestampQueryInsideEncoders: return "wgpu::NativeFeature::TimestampQueryInsideEncoders";
    case NativeFeature::TimestampQueryInsidePasses: return "wgpu::NativeFeature::TimestampQueryInsidePasses";
    case NativeFeature::ShaderInt64: return "wgpu::NativeFeature::ShaderInt64";
    case NativeFeature::Force32: return "wgpu::NativeFeature::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(LogLevel v) {
    switch (v) {
    case LogLevel::Off: return "wgpu::LogLevel::Off";
    case LogLevel::Error: return "wgpu::LogLevel::Error";
    case LogLevel::Warn: return "wgpu::LogLevel::Warn";
    case LogLevel::Info: return "wgpu::LogLevel::Info";
    case LogLevel::Debug: return "wgpu::LogLevel::Debug";
    case LogLevel::Trace: return "wgpu::LogLevel::Trace";
    case LogLevel::Force32: return "wgpu::LogLevel::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(InstanceBackend v) {
    switch (v) {
    case InstanceBackend::All: return "wgpu::InstanceBackend::All";
    case InstanceBackend::Force32: return "wgpu::InstanceBackend::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(InstanceFlag v) {
    switch (v) {
    case InstanceFlag::Default: return "wgpu::InstanceFlag::Default";
    case InstanceFlag::Force32: return "wgpu::InstanceFlag::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(Dx12Compiler v) {
    switch (v) {
    case Dx12Compiler::Undefined: return "wgpu::Dx12Compiler::Undefined";
    case Dx12Compiler::Fxc: return "wgpu::Dx12Compiler::Fxc";
    case Dx12Compiler::Dxc: return "wgpu::Dx12Compiler::Dxc";
    case Dx12Compiler::Force32: return "wgpu::Dx12Compiler::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(Gles3MinorVersion v) {
    switch (v) {
    case Gles3MinorVersion::Automatic: return "wgpu::Gles3MinorVersion::Automatic";
    case Gles3MinorVersion::Version0: return "wgpu::Gles3MinorVersion::Version0";
    case Gles3MinorVersion::Version1: return "wgpu::Gles3MinorVersion::Version1";
    case Gles3MinorVersion::Version2: return "wgpu::Gles3MinorVersion::Version2";
    case Gles3MinorVersion::Force32: return "wgpu::Gles3MinorVersion::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(PipelineStatisticName v) {
    switch (v) {
    case PipelineStatisticName::VertexShaderInvocations: return "wgpu::PipelineStatisticName::VertexShaderInvocations";
    case PipelineStatisticName::ClipperInvocations: return "wgpu::PipelineStatisticName::ClipperInvocations";
    case PipelineStatisticName::ClipperPrimitivesOut: return "wgpu::PipelineStatisticName::ClipperPrimitivesOut";
    case PipelineStatisticName::FragmentShaderInvocations: return "wgpu::PipelineStatisticName::FragmentShaderInvocations";
    case PipelineStatisticName::ComputeShaderInvocations: return "wgpu::PipelineStatisticName::ComputeShaderInvocations";
    case PipelineStatisticName::Force32: return "wgpu::PipelineStatisticName::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(NativeQueryType v) {
    switch (v) {
    case NativeQueryType::PipelineStatistics: return "wgpu::NativeQueryType::PipelineStatistics";
    case NativeQueryType::Force32: return "wgpu::NativeQueryType::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(DxcMaxShaderModel v) {
    switch (v) {
    case DxcMaxShaderModel::V6_0: return "wgpu::DxcMaxShaderModel::V6_0";
    case DxcMaxShaderModel::V6_1: return "wgpu::DxcMaxShaderModel::V6_1";
    case DxcMaxShaderModel::V6_2: return "wgpu::DxcMaxShaderModel::V6_2";
    case DxcMaxShaderModel::V6_3: return "wgpu::DxcMaxShaderModel::V6_3";
    case DxcMaxShaderModel::V6_4: return "wgpu::DxcMaxShaderModel::V6_4";
    case DxcMaxShaderModel::V6_5: return "wgpu::DxcMaxShaderModel::V6_5";
    case DxcMaxShaderModel::V6_6: return "wgpu::DxcMaxShaderModel::V6_6";
    case DxcMaxShaderModel::V6_7: return "wgpu::DxcMaxShaderModel::V6_7";
    case DxcMaxShaderModel::Force32: return "wgpu::DxcMaxShaderModel::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(GLFenceBehaviour v) {
    switch (v) {
    case GLFenceBehaviour::Normal: return "wgpu::GLFenceBehaviour::Normal";
    case GLFenceBehaviour::AutoFinish: return "wgpu::GLFenceBehaviour::AutoFinish";
    case GLFenceBehaviour::Force32: return "wgpu::GLFenceBehaviour::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(Dx12SwapchainKind v) {
    switch (v) {
    case Dx12SwapchainKind::Undefined: return "wgpu::Dx12SwapchainKind::Undefined";
    case Dx12SwapchainKind::DxgiFromHwnd: return "wgpu::Dx12SwapchainKind::DxgiFromHwnd";
    case Dx12SwapchainKind::DxgiFromVisual: return "wgpu::Dx12SwapchainKind::DxgiFromVisual";
    case Dx12SwapchainKind::Force32: return "wgpu::Dx12SwapchainKind::Force32";
    default: return "Unknown";
    }
}

std::string_view to_string(PolygonMode v) {
    switch (v) {
    case PolygonMode::Fill: return "wgpu::PolygonMode::Fill";
    case PolygonMode::Line: return "wgpu::PolygonMode::Line";
    case PolygonMode::Point: return "wgpu::PolygonMode::Point";
    default: return "Unknown";
    }
}

std::string_view to_string(NativeTextureFormat v) {
    switch (v) {
    case NativeTextureFormat::R16Unorm: return "wgpu::NativeTextureFormat::R16Unorm";
    case NativeTextureFormat::R16Snorm: return "wgpu::NativeTextureFormat::R16Snorm";
    case NativeTextureFormat::Rg16Unorm: return "wgpu::NativeTextureFormat::Rg16Unorm";
    case NativeTextureFormat::Rg16Snorm: return "wgpu::NativeTextureFormat::Rg16Snorm";
    case NativeTextureFormat::Rgba16Unorm: return "wgpu::NativeTextureFormat::Rgba16Unorm";
    case NativeTextureFormat::Rgba16Snorm: return "wgpu::NativeTextureFormat::Rgba16Snorm";
    case NativeTextureFormat::NV12: return "wgpu::NativeTextureFormat::NV12";
    case NativeTextureFormat::P010: return "wgpu::NativeTextureFormat::P010";
    default: return "Unknown";
    }
}

} // namespace wgpu


// Template methods of handles
namespace wgpu {
namespace raw {
// Template methods of Adapter
template<std::invocable<RequestDeviceStatus, raw::Device, StringView> Lambda>
Future Adapter::requestDevice(const DeviceDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<RequestDeviceStatus>(status), device, *reinterpret_cast<StringView *>(&message));
	};
	WGPURequestDeviceCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuAdapterRequestDevice(m_raw, reinterpret_cast<const WGPUDeviceDescriptor *>(&descriptor), *reinterpret_cast<WGPURequestDeviceCallbackInfo *>(&callbackInfo));
}


// Template methods of Buffer
template<std::invocable<MapAsyncStatus, StringView> Lambda>
Future Buffer::mapAsync(MapMode mode, size_t offset, size_t size, CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<MapAsyncStatus>(status), *reinterpret_cast<StringView *>(&message));
	};
	WGPUBufferMapCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuBufferMapAsync(m_raw, static_cast<WGPUMapMode>(mode), offset, size, *reinterpret_cast<WGPUBufferMapCallbackInfo *>(&callbackInfo));
}


// Template methods of Device
template<std::invocable<CreatePipelineAsyncStatus, raw::ComputePipeline, StringView> Lambda>
Future Device::createComputePipelineAsync(const ComputePipelineDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline, WGPUStringView message, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<CreatePipelineAsyncStatus>(status), pipeline, *reinterpret_cast<StringView *>(&message));
	};
	WGPUCreateComputePipelineAsyncCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuDeviceCreateComputePipelineAsync(m_raw, reinterpret_cast<const WGPUComputePipelineDescriptor *>(&descriptor), *reinterpret_cast<WGPUCreateComputePipelineAsyncCallbackInfo *>(&callbackInfo));
}
template<std::invocable<CreatePipelineAsyncStatus, raw::RenderPipeline, StringView> Lambda>
Future Device::createRenderPipelineAsync(const RenderPipelineDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline, WGPUStringView message, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<CreatePipelineAsyncStatus>(status), pipeline, *reinterpret_cast<StringView *>(&message));
	};
	WGPUCreateRenderPipelineAsyncCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuDeviceCreateRenderPipelineAsync(m_raw, reinterpret_cast<const WGPURenderPipelineDescriptor *>(&descriptor), *reinterpret_cast<WGPUCreateRenderPipelineAsyncCallbackInfo *>(&callbackInfo));
}
template<std::invocable<PopErrorScopeStatus, ErrorType, StringView> Lambda>
Future Device::popErrorScope(CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<PopErrorScopeStatus>(status), static_cast<ErrorType>(type), *reinterpret_cast<StringView *>(&message));
	};
	WGPUPopErrorScopeCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuDevicePopErrorScope(m_raw, *reinterpret_cast<WGPUPopErrorScopeCallbackInfo *>(&callbackInfo));
}


// Template methods of Instance
template<std::invocable<RequestAdapterStatus, raw::Adapter, StringView> Lambda>
Future Instance::requestAdapter(const RequestAdapterOptions& options, CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<RequestAdapterStatus>(status), adapter, *reinterpret_cast<StringView *>(&message));
	};
	WGPURequestAdapterCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuInstanceRequestAdapter(m_raw, reinterpret_cast<const WGPURequestAdapterOptions *>(&options), *reinterpret_cast<WGPURequestAdapterCallbackInfo *>(&callbackInfo));
}


// Template methods of Queue
template<std::invocable<QueueWorkDoneStatus> Lambda>
Future Queue::onSubmittedWorkDone(CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPUQueueWorkDoneStatus status, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<QueueWorkDoneStatus>(status));
	};
	WGPUQueueWorkDoneCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuQueueOnSubmittedWorkDone(m_raw, *reinterpret_cast<WGPUQueueWorkDoneCallbackInfo *>(&callbackInfo));
}


// Template methods of ShaderModule
template<std::invocable<CompilationInfoRequestStatus, const CompilationInfo&> Lambda>
Future ShaderModule::getCompilationInfo(CallbackMode callbackMode, const Lambda& callback) const {
	auto* lambda = new Lambda(callback);
	auto cCallback = [](WGPUCompilationInfoRequestStatus status, struct WGPUCompilationInfo const * compilationInfo, void* userdata1, void*) -> void {
		std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
		(*lambda)(static_cast<CompilationInfoRequestStatus>(status), *reinterpret_cast<const CompilationInfo*>(compilationInfo));
	};
	WGPUCompilationInfoCallbackInfo callbackInfo = {
		/* nextInChain = */ nullptr,
		/* mode = */ callbackMode,
		/* callback = */ cCallback,
		/* userdata1 = */ (void*)lambda,
		/* userdata2 = */ nullptr,
	};
	return wgpuShaderModuleGetCompilationInfo(m_raw, *reinterpret_cast<WGPUCompilationInfoCallbackInfo *>(&callbackInfo));
}


}

} // namespace wgpu

namespace wgpu {

} // namespace wgpu


// Extra implementations
#ifdef WEBGPU_CPP_NAMESPACE
namespace WEBGPU_CPP_NAMESPACE
#endif
{
StringView::operator std::string_view() const {
	return
		length == WGPU_STRLEN
		? std::string_view(data)
		: std::string_view(data, length);
}

InstanceHandle createInstance() {
	return wgpuCreateInstance(nullptr);
}

InstanceHandle createInstance(const InstanceDescriptor& descriptor) {
	return wgpuCreateInstance(reinterpret_cast<const WGPUInstanceDescriptor*>(&descriptor));
}

}

#ifdef WEBGPU_CPP_NAMESPACE
namespace WEBGPU_CPP_NAMESPACE
#endif
{
#ifdef WEBGPU_CPP_USE_RAW_NAMESPACE
namespace raw 
#endif
{
Adapter Instance::requestAdapter(const RequestAdapterOptions& options) {
	struct Context {
		Adapter adapter = nullptr;
		bool requestEnded = false;
	};
	Context context;

	RequestAdapterCallbackInfo callbackInfo;
	callbackInfo.nextInChain = nullptr;
	callbackInfo.userdata1 = &context;
	callbackInfo.callback = [](
		WGPURequestAdapterStatus status,
		WGPUAdapter adapter,
		WGPUStringView message,
		void* userdata1,
		[[maybe_unused]] void* userdata2
	) {
		Context& context = *reinterpret_cast<Context*>(userdata1);
		if (status == RequestAdapterStatus::Success) {
			context.adapter = adapter;
		}
		else {
			std::cout << "Could not get WebGPU adapter: " << StringView(message) << std::endl;
		}
		context.requestEnded = true;
	};
	callbackInfo.mode = CallbackMode::AllowSpontaneous;
	wgpuInstanceRequestAdapter(*this, reinterpret_cast<const WGPURequestAdapterOptions*>(&options), callbackInfo);

#if __EMSCRIPTEN__
	while (!context.requestEnded) {
		emscripten_sleep(50);
	}
#endif

	assert(context.requestEnded);
	return context.adapter;
}

Device Adapter::requestDevice(const DeviceDescriptor& descriptor) {
	struct Context {
		Device device = nullptr;
		bool requestEnded = false;
	};
	Context context;

	RequestDeviceCallbackInfo callbackInfo;
	callbackInfo.nextInChain = nullptr;
	callbackInfo.userdata1 = &context;
	callbackInfo.callback = [](
		WGPURequestDeviceStatus status,
		WGPUDevice device,
		WGPUStringView message,
		void* userdata1,
		[[maybe_unused]] void* userdata2
	) {
		Context& context = *reinterpret_cast<Context*>(userdata1);
		if (status == RequestDeviceStatus::Success) {
			context.device = device;
		}
		else {
			std::cout << "Could not get WebGPU device: " << StringView(message) << std::endl;
		}
		context.requestEnded = true;
	};
	callbackInfo.mode = CallbackMode::AllowSpontaneous;
	wgpuAdapterRequestDevice(*this, reinterpret_cast<const WGPUDeviceDescriptor*>(&descriptor), callbackInfo);

#if __EMSCRIPTEN__
	while (!context.requestEnded) {
		emscripten_sleep(50);
	}
#endif

	assert(context.requestEnded);
	return context.device;
}
}
}

#undef HANDLE
#undef STRUCT
#undef STRUCT_NO_OSTREAM
#undef HANDLE_RAII
#undef DESCRIPTOR
#undef ENUM
#undef ENUM_ENTRY
#undef END

#undef WEBGPU_CPP_USE_RAW_NAMESPACE

#undef WEBGPU_CPP_NAMESPACE
