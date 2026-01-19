module;

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <iostream>
#include <vector>
#include <functional>
#include <cassert>
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

/**
 * A namespace providing a more C++ idiomatic API to WebGPU.
 */
export namespace wgpu {

struct DefaultFlag {};
constexpr DefaultFlag Default;

#define HANDLE(Type) \
class Type { \
public: \
	typedef Type S; \
	typedef WGPU ## Type W; \
	constexpr Type() : m_raw(nullptr) {} \
	constexpr Type(const W& w) : m_raw(w) {} \
	Type(const Type& other) : m_raw(other.m_raw) { \
		if (m_raw) wgpu ## Type ## AddRef(m_raw); \
	} \
	Type(Type&& other) noexcept : m_raw(other.m_raw) { \
		other.m_raw = nullptr; \
	} \
	Type& operator=(const Type& other) { \
		if (this != &other) { \
			if (m_raw) wgpu ## Type ## Release(m_raw); \
			m_raw = other.m_raw; \
			if (m_raw) wgpu ## Type ## AddRef(m_raw); \
		} \
		return *this; \
	} \
	Type& operator=(Type&& other) noexcept { \
		if (this != &other) { \
			if (m_raw) wgpu ## Type ## Release(m_raw); \
			m_raw = other.m_raw; \
			other.m_raw = nullptr; \
		} \
		return *this; \
	} \
	~Type() { \
		if (m_raw) wgpu ## Type ## Release(m_raw); \
	} \
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

#define DESCRIPTOR(Type) \
struct Type : public WGPU ## Type { \
public: \
	typedef Type S; /* S == Self */ \
	typedef WGPU ## Type W; /* W == WGPU Type */ \
	Type() : W() { setDefault(); nextInChain = nullptr; } \
	Type(const W &other) : W(other) { nextInChain = nullptr; } \
	Type(const DefaultFlag &) : W() { setDefault(); } \
	Type& operator=(const DefaultFlag &) { setDefault(); return *this; } \
	friend auto operator<<(std::ostream &stream, const S&) -> std::ostream & { \
		return stream << "<wgpu::" << #Type << ">"; \
	} \
public:

#define STRUCT_NO_OSTREAM(Type) \
struct Type : public WGPU ## Type { \
public: \
	typedef Type S; /* S == Self */ \
	typedef WGPU ## Type W; /* W == WGPU Type */ \
	Type() : W() { setDefault(); } \
	Type(const W &other) : W(other) {} \
	Type(const DefaultFlag &) : W() { setDefault(); } \
	Type& operator=(const DefaultFlag &) { setDefault(); return *this; } \
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



// Other type aliases
using Flags = uint64_t;
using Bool = uint32_t;
using SubmissionIndex = uint64_t;

// Enumerations
ENUM(AdapterType)
	ENUM_ENTRY(DiscreteGPU, WGPUAdapterType_DiscreteGPU)
	ENUM_ENTRY(IntegratedGPU, WGPUAdapterType_IntegratedGPU)
	ENUM_ENTRY(CPU, WGPUAdapterType_CPU)
	ENUM_ENTRY(Unknown, WGPUAdapterType_Unknown)
	ENUM_ENTRY(Force32, WGPUAdapterType_Force32)
END
ENUM(AddressMode)
	ENUM_ENTRY(Undefined, WGPUAddressMode_Undefined)
	ENUM_ENTRY(ClampToEdge, WGPUAddressMode_ClampToEdge)
	ENUM_ENTRY(Repeat, WGPUAddressMode_Repeat)
	ENUM_ENTRY(MirrorRepeat, WGPUAddressMode_MirrorRepeat)
	ENUM_ENTRY(Force32, WGPUAddressMode_Force32)
END
ENUM(BackendType)
	ENUM_ENTRY(Undefined, WGPUBackendType_Undefined)
	ENUM_ENTRY(Null, WGPUBackendType_Null)
	ENUM_ENTRY(WebGPU, WGPUBackendType_WebGPU)
	ENUM_ENTRY(D3D11, WGPUBackendType_D3D11)
	ENUM_ENTRY(D3D12, WGPUBackendType_D3D12)
	ENUM_ENTRY(Metal, WGPUBackendType_Metal)
	ENUM_ENTRY(Vulkan, WGPUBackendType_Vulkan)
	ENUM_ENTRY(OpenGL, WGPUBackendType_OpenGL)
	ENUM_ENTRY(OpenGLES, WGPUBackendType_OpenGLES)
	ENUM_ENTRY(Force32, WGPUBackendType_Force32)
END
ENUM(BlendFactor)
	ENUM_ENTRY(Undefined, WGPUBlendFactor_Undefined)
	ENUM_ENTRY(Zero, WGPUBlendFactor_Zero)
	ENUM_ENTRY(One, WGPUBlendFactor_One)
	ENUM_ENTRY(Src, WGPUBlendFactor_Src)
	ENUM_ENTRY(OneMinusSrc, WGPUBlendFactor_OneMinusSrc)
	ENUM_ENTRY(SrcAlpha, WGPUBlendFactor_SrcAlpha)
	ENUM_ENTRY(OneMinusSrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha)
	ENUM_ENTRY(Dst, WGPUBlendFactor_Dst)
	ENUM_ENTRY(OneMinusDst, WGPUBlendFactor_OneMinusDst)
	ENUM_ENTRY(DstAlpha, WGPUBlendFactor_DstAlpha)
	ENUM_ENTRY(OneMinusDstAlpha, WGPUBlendFactor_OneMinusDstAlpha)
	ENUM_ENTRY(SrcAlphaSaturated, WGPUBlendFactor_SrcAlphaSaturated)
	ENUM_ENTRY(Constant, WGPUBlendFactor_Constant)
	ENUM_ENTRY(OneMinusConstant, WGPUBlendFactor_OneMinusConstant)
	ENUM_ENTRY(Src1, WGPUBlendFactor_Src1)
	ENUM_ENTRY(OneMinusSrc1, WGPUBlendFactor_OneMinusSrc1)
	ENUM_ENTRY(Src1Alpha, WGPUBlendFactor_Src1Alpha)
	ENUM_ENTRY(OneMinusSrc1Alpha, WGPUBlendFactor_OneMinusSrc1Alpha)
	ENUM_ENTRY(Force32, WGPUBlendFactor_Force32)
END
ENUM(BlendOperation)
	ENUM_ENTRY(Undefined, WGPUBlendOperation_Undefined)
	ENUM_ENTRY(Add, WGPUBlendOperation_Add)
	ENUM_ENTRY(Subtract, WGPUBlendOperation_Subtract)
	ENUM_ENTRY(ReverseSubtract, WGPUBlendOperation_ReverseSubtract)
	ENUM_ENTRY(Min, WGPUBlendOperation_Min)
	ENUM_ENTRY(Max, WGPUBlendOperation_Max)
	ENUM_ENTRY(Force32, WGPUBlendOperation_Force32)
END
ENUM(BufferBindingType)
	ENUM_ENTRY(BindingNotUsed, WGPUBufferBindingType_BindingNotUsed)
	ENUM_ENTRY(Undefined, WGPUBufferBindingType_Undefined)
	ENUM_ENTRY(Uniform, WGPUBufferBindingType_Uniform)
	ENUM_ENTRY(Storage, WGPUBufferBindingType_Storage)
	ENUM_ENTRY(ReadOnlyStorage, WGPUBufferBindingType_ReadOnlyStorage)
	ENUM_ENTRY(Force32, WGPUBufferBindingType_Force32)
END
ENUM(BufferMapState)
	ENUM_ENTRY(Unmapped, WGPUBufferMapState_Unmapped)
	ENUM_ENTRY(Pending, WGPUBufferMapState_Pending)
	ENUM_ENTRY(Mapped, WGPUBufferMapState_Mapped)
	ENUM_ENTRY(Force32, WGPUBufferMapState_Force32)
END
ENUM(CallbackMode)
	ENUM_ENTRY(WaitAnyOnly, WGPUCallbackMode_WaitAnyOnly)
	ENUM_ENTRY(AllowProcessEvents, WGPUCallbackMode_AllowProcessEvents)
	ENUM_ENTRY(AllowSpontaneous, WGPUCallbackMode_AllowSpontaneous)
	ENUM_ENTRY(Force32, WGPUCallbackMode_Force32)
END
ENUM(CompareFunction)
	ENUM_ENTRY(Undefined, WGPUCompareFunction_Undefined)
	ENUM_ENTRY(Never, WGPUCompareFunction_Never)
	ENUM_ENTRY(Less, WGPUCompareFunction_Less)
	ENUM_ENTRY(Equal, WGPUCompareFunction_Equal)
	ENUM_ENTRY(LessEqual, WGPUCompareFunction_LessEqual)
	ENUM_ENTRY(Greater, WGPUCompareFunction_Greater)
	ENUM_ENTRY(NotEqual, WGPUCompareFunction_NotEqual)
	ENUM_ENTRY(GreaterEqual, WGPUCompareFunction_GreaterEqual)
	ENUM_ENTRY(Always, WGPUCompareFunction_Always)
	ENUM_ENTRY(Force32, WGPUCompareFunction_Force32)
END
ENUM(CompilationInfoRequestStatus)
	ENUM_ENTRY(Success, WGPUCompilationInfoRequestStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPUCompilationInfoRequestStatus_InstanceDropped)
	ENUM_ENTRY(Error, WGPUCompilationInfoRequestStatus_Error)
	ENUM_ENTRY(Unknown, WGPUCompilationInfoRequestStatus_Unknown)
	ENUM_ENTRY(Force32, WGPUCompilationInfoRequestStatus_Force32)
END
ENUM(CompilationMessageType)
	ENUM_ENTRY(Error, WGPUCompilationMessageType_Error)
	ENUM_ENTRY(Warning, WGPUCompilationMessageType_Warning)
	ENUM_ENTRY(Info, WGPUCompilationMessageType_Info)
	ENUM_ENTRY(Force32, WGPUCompilationMessageType_Force32)
END
ENUM(CompositeAlphaMode)
	ENUM_ENTRY(Auto, WGPUCompositeAlphaMode_Auto)
	ENUM_ENTRY(Opaque, WGPUCompositeAlphaMode_Opaque)
	ENUM_ENTRY(Premultiplied, WGPUCompositeAlphaMode_Premultiplied)
	ENUM_ENTRY(Unpremultiplied, WGPUCompositeAlphaMode_Unpremultiplied)
	ENUM_ENTRY(Inherit, WGPUCompositeAlphaMode_Inherit)
	ENUM_ENTRY(Force32, WGPUCompositeAlphaMode_Force32)
END
ENUM(CreatePipelineAsyncStatus)
	ENUM_ENTRY(Success, WGPUCreatePipelineAsyncStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPUCreatePipelineAsyncStatus_InstanceDropped)
	ENUM_ENTRY(ValidationError, WGPUCreatePipelineAsyncStatus_ValidationError)
	ENUM_ENTRY(InternalError, WGPUCreatePipelineAsyncStatus_InternalError)
	ENUM_ENTRY(Unknown, WGPUCreatePipelineAsyncStatus_Unknown)
	ENUM_ENTRY(Force32, WGPUCreatePipelineAsyncStatus_Force32)
END
ENUM(CullMode)
	ENUM_ENTRY(Undefined, WGPUCullMode_Undefined)
	ENUM_ENTRY(None, WGPUCullMode_None)
	ENUM_ENTRY(Front, WGPUCullMode_Front)
	ENUM_ENTRY(Back, WGPUCullMode_Back)
	ENUM_ENTRY(Force32, WGPUCullMode_Force32)
END
ENUM(DeviceLostReason)
	ENUM_ENTRY(Unknown, WGPUDeviceLostReason_Unknown)
	ENUM_ENTRY(Destroyed, WGPUDeviceLostReason_Destroyed)
	ENUM_ENTRY(InstanceDropped, WGPUDeviceLostReason_InstanceDropped)
	ENUM_ENTRY(FailedCreation, WGPUDeviceLostReason_FailedCreation)
	ENUM_ENTRY(Force32, WGPUDeviceLostReason_Force32)
END
ENUM(ErrorFilter)
	ENUM_ENTRY(Validation, WGPUErrorFilter_Validation)
	ENUM_ENTRY(OutOfMemory, WGPUErrorFilter_OutOfMemory)
	ENUM_ENTRY(Internal, WGPUErrorFilter_Internal)
	ENUM_ENTRY(Force32, WGPUErrorFilter_Force32)
END
ENUM(ErrorType)
	ENUM_ENTRY(NoError, WGPUErrorType_NoError)
	ENUM_ENTRY(Validation, WGPUErrorType_Validation)
	ENUM_ENTRY(OutOfMemory, WGPUErrorType_OutOfMemory)
	ENUM_ENTRY(Internal, WGPUErrorType_Internal)
	ENUM_ENTRY(Unknown, WGPUErrorType_Unknown)
	ENUM_ENTRY(Force32, WGPUErrorType_Force32)
END
ENUM(FeatureLevel)
	ENUM_ENTRY(Compatibility, WGPUFeatureLevel_Compatibility)
	ENUM_ENTRY(Core, WGPUFeatureLevel_Core)
	ENUM_ENTRY(Force32, WGPUFeatureLevel_Force32)
END
ENUM(FeatureName)
	ENUM_ENTRY(Undefined, WGPUFeatureName_Undefined)
	ENUM_ENTRY(DepthClipControl, WGPUFeatureName_DepthClipControl)
	ENUM_ENTRY(Depth32FloatStencil8, WGPUFeatureName_Depth32FloatStencil8)
	ENUM_ENTRY(TimestampQuery, WGPUFeatureName_TimestampQuery)
	ENUM_ENTRY(TextureCompressionBC, WGPUFeatureName_TextureCompressionBC)
	ENUM_ENTRY(TextureCompressionBCSliced3D, WGPUFeatureName_TextureCompressionBCSliced3D)
	ENUM_ENTRY(TextureCompressionETC2, WGPUFeatureName_TextureCompressionETC2)
	ENUM_ENTRY(TextureCompressionASTC, WGPUFeatureName_TextureCompressionASTC)
	ENUM_ENTRY(TextureCompressionASTCSliced3D, WGPUFeatureName_TextureCompressionASTCSliced3D)
	ENUM_ENTRY(IndirectFirstInstance, WGPUFeatureName_IndirectFirstInstance)
	ENUM_ENTRY(ShaderF16, WGPUFeatureName_ShaderF16)
	ENUM_ENTRY(RG11B10UfloatRenderable, WGPUFeatureName_RG11B10UfloatRenderable)
	ENUM_ENTRY(BGRA8UnormStorage, WGPUFeatureName_BGRA8UnormStorage)
	ENUM_ENTRY(Float32Filterable, WGPUFeatureName_Float32Filterable)
	ENUM_ENTRY(Float32Blendable, WGPUFeatureName_Float32Blendable)
	ENUM_ENTRY(ClipDistances, WGPUFeatureName_ClipDistances)
	ENUM_ENTRY(DualSourceBlending, WGPUFeatureName_DualSourceBlending)
	ENUM_ENTRY(Force32, WGPUFeatureName_Force32)
END
ENUM(FilterMode)
	ENUM_ENTRY(Undefined, WGPUFilterMode_Undefined)
	ENUM_ENTRY(Nearest, WGPUFilterMode_Nearest)
	ENUM_ENTRY(Linear, WGPUFilterMode_Linear)
	ENUM_ENTRY(Force32, WGPUFilterMode_Force32)
END
ENUM(FrontFace)
	ENUM_ENTRY(Undefined, WGPUFrontFace_Undefined)
	ENUM_ENTRY(CCW, WGPUFrontFace_CCW)
	ENUM_ENTRY(CW, WGPUFrontFace_CW)
	ENUM_ENTRY(Force32, WGPUFrontFace_Force32)
END
ENUM(IndexFormat)
	ENUM_ENTRY(Undefined, WGPUIndexFormat_Undefined)
	ENUM_ENTRY(Uint16, WGPUIndexFormat_Uint16)
	ENUM_ENTRY(Uint32, WGPUIndexFormat_Uint32)
	ENUM_ENTRY(Force32, WGPUIndexFormat_Force32)
END
ENUM(LoadOp)
	ENUM_ENTRY(Undefined, WGPULoadOp_Undefined)
	ENUM_ENTRY(Load, WGPULoadOp_Load)
	ENUM_ENTRY(Clear, WGPULoadOp_Clear)
	ENUM_ENTRY(Force32, WGPULoadOp_Force32)
END
ENUM(MapAsyncStatus)
	ENUM_ENTRY(Success, WGPUMapAsyncStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPUMapAsyncStatus_InstanceDropped)
	ENUM_ENTRY(Error, WGPUMapAsyncStatus_Error)
	ENUM_ENTRY(Aborted, WGPUMapAsyncStatus_Aborted)
	ENUM_ENTRY(Unknown, WGPUMapAsyncStatus_Unknown)
	ENUM_ENTRY(Force32, WGPUMapAsyncStatus_Force32)
END
ENUM(MipmapFilterMode)
	ENUM_ENTRY(Undefined, WGPUMipmapFilterMode_Undefined)
	ENUM_ENTRY(Nearest, WGPUMipmapFilterMode_Nearest)
	ENUM_ENTRY(Linear, WGPUMipmapFilterMode_Linear)
	ENUM_ENTRY(Force32, WGPUMipmapFilterMode_Force32)
END
ENUM(OptionalBool)
	ENUM_ENTRY(False, WGPUOptionalBool_False)
	ENUM_ENTRY(True, WGPUOptionalBool_True)
	ENUM_ENTRY(Undefined, WGPUOptionalBool_Undefined)
	ENUM_ENTRY(Force32, WGPUOptionalBool_Force32)
END
ENUM(PopErrorScopeStatus)
	ENUM_ENTRY(Success, WGPUPopErrorScopeStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPUPopErrorScopeStatus_InstanceDropped)
	ENUM_ENTRY(EmptyStack, WGPUPopErrorScopeStatus_EmptyStack)
	ENUM_ENTRY(Force32, WGPUPopErrorScopeStatus_Force32)
END
ENUM(PowerPreference)
	ENUM_ENTRY(Undefined, WGPUPowerPreference_Undefined)
	ENUM_ENTRY(LowPower, WGPUPowerPreference_LowPower)
	ENUM_ENTRY(HighPerformance, WGPUPowerPreference_HighPerformance)
	ENUM_ENTRY(Force32, WGPUPowerPreference_Force32)
END
ENUM(PresentMode)
	ENUM_ENTRY(Undefined, WGPUPresentMode_Undefined)
	ENUM_ENTRY(Fifo, WGPUPresentMode_Fifo)
	ENUM_ENTRY(FifoRelaxed, WGPUPresentMode_FifoRelaxed)
	ENUM_ENTRY(Immediate, WGPUPresentMode_Immediate)
	ENUM_ENTRY(Mailbox, WGPUPresentMode_Mailbox)
	ENUM_ENTRY(Force32, WGPUPresentMode_Force32)
END
ENUM(PrimitiveTopology)
	ENUM_ENTRY(Undefined, WGPUPrimitiveTopology_Undefined)
	ENUM_ENTRY(PointList, WGPUPrimitiveTopology_PointList)
	ENUM_ENTRY(LineList, WGPUPrimitiveTopology_LineList)
	ENUM_ENTRY(LineStrip, WGPUPrimitiveTopology_LineStrip)
	ENUM_ENTRY(TriangleList, WGPUPrimitiveTopology_TriangleList)
	ENUM_ENTRY(TriangleStrip, WGPUPrimitiveTopology_TriangleStrip)
	ENUM_ENTRY(Force32, WGPUPrimitiveTopology_Force32)
END
ENUM(QueryType)
	ENUM_ENTRY(Occlusion, WGPUQueryType_Occlusion)
	ENUM_ENTRY(Timestamp, WGPUQueryType_Timestamp)
	ENUM_ENTRY(Force32, WGPUQueryType_Force32)
END
ENUM(QueueWorkDoneStatus)
	ENUM_ENTRY(Success, WGPUQueueWorkDoneStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPUQueueWorkDoneStatus_InstanceDropped)
	ENUM_ENTRY(Error, WGPUQueueWorkDoneStatus_Error)
	ENUM_ENTRY(Unknown, WGPUQueueWorkDoneStatus_Unknown)
	ENUM_ENTRY(Force32, WGPUQueueWorkDoneStatus_Force32)
END
ENUM(RequestAdapterStatus)
	ENUM_ENTRY(Success, WGPURequestAdapterStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPURequestAdapterStatus_InstanceDropped)
	ENUM_ENTRY(Unavailable, WGPURequestAdapterStatus_Unavailable)
	ENUM_ENTRY(Error, WGPURequestAdapterStatus_Error)
	ENUM_ENTRY(Unknown, WGPURequestAdapterStatus_Unknown)
	ENUM_ENTRY(Force32, WGPURequestAdapterStatus_Force32)
END
ENUM(RequestDeviceStatus)
	ENUM_ENTRY(Success, WGPURequestDeviceStatus_Success)
	ENUM_ENTRY(InstanceDropped, WGPURequestDeviceStatus_InstanceDropped)
	ENUM_ENTRY(Error, WGPURequestDeviceStatus_Error)
	ENUM_ENTRY(Unknown, WGPURequestDeviceStatus_Unknown)
	ENUM_ENTRY(Force32, WGPURequestDeviceStatus_Force32)
END
ENUM(SType)
	ENUM_ENTRY(ShaderSourceSPIRV, WGPUSType_ShaderSourceSPIRV)
	ENUM_ENTRY(ShaderSourceWGSL, WGPUSType_ShaderSourceWGSL)
	ENUM_ENTRY(RenderPassMaxDrawCount, WGPUSType_RenderPassMaxDrawCount)
	ENUM_ENTRY(SurfaceSourceMetalLayer, WGPUSType_SurfaceSourceMetalLayer)
	ENUM_ENTRY(SurfaceSourceWindowsHWND, WGPUSType_SurfaceSourceWindowsHWND)
	ENUM_ENTRY(SurfaceSourceXlibWindow, WGPUSType_SurfaceSourceXlibWindow)
	ENUM_ENTRY(SurfaceSourceWaylandSurface, WGPUSType_SurfaceSourceWaylandSurface)
	ENUM_ENTRY(SurfaceSourceAndroidNativeWindow, WGPUSType_SurfaceSourceAndroidNativeWindow)
	ENUM_ENTRY(SurfaceSourceXCBWindow, WGPUSType_SurfaceSourceXCBWindow)
	ENUM_ENTRY(Force32, WGPUSType_Force32)
END
ENUM(SamplerBindingType)
	ENUM_ENTRY(BindingNotUsed, WGPUSamplerBindingType_BindingNotUsed)
	ENUM_ENTRY(Undefined, WGPUSamplerBindingType_Undefined)
	ENUM_ENTRY(Filtering, WGPUSamplerBindingType_Filtering)
	ENUM_ENTRY(NonFiltering, WGPUSamplerBindingType_NonFiltering)
	ENUM_ENTRY(Comparison, WGPUSamplerBindingType_Comparison)
	ENUM_ENTRY(Force32, WGPUSamplerBindingType_Force32)
END
ENUM(Status)
	ENUM_ENTRY(Success, WGPUStatus_Success)
	ENUM_ENTRY(Error, WGPUStatus_Error)
	ENUM_ENTRY(Force32, WGPUStatus_Force32)
END
ENUM(StencilOperation)
	ENUM_ENTRY(Undefined, WGPUStencilOperation_Undefined)
	ENUM_ENTRY(Keep, WGPUStencilOperation_Keep)
	ENUM_ENTRY(Zero, WGPUStencilOperation_Zero)
	ENUM_ENTRY(Replace, WGPUStencilOperation_Replace)
	ENUM_ENTRY(Invert, WGPUStencilOperation_Invert)
	ENUM_ENTRY(IncrementClamp, WGPUStencilOperation_IncrementClamp)
	ENUM_ENTRY(DecrementClamp, WGPUStencilOperation_DecrementClamp)
	ENUM_ENTRY(IncrementWrap, WGPUStencilOperation_IncrementWrap)
	ENUM_ENTRY(DecrementWrap, WGPUStencilOperation_DecrementWrap)
	ENUM_ENTRY(Force32, WGPUStencilOperation_Force32)
END
ENUM(StorageTextureAccess)
	ENUM_ENTRY(BindingNotUsed, WGPUStorageTextureAccess_BindingNotUsed)
	ENUM_ENTRY(Undefined, WGPUStorageTextureAccess_Undefined)
	ENUM_ENTRY(WriteOnly, WGPUStorageTextureAccess_WriteOnly)
	ENUM_ENTRY(ReadOnly, WGPUStorageTextureAccess_ReadOnly)
	ENUM_ENTRY(ReadWrite, WGPUStorageTextureAccess_ReadWrite)
	ENUM_ENTRY(Force32, WGPUStorageTextureAccess_Force32)
END
ENUM(StoreOp)
	ENUM_ENTRY(Undefined, WGPUStoreOp_Undefined)
	ENUM_ENTRY(Store, WGPUStoreOp_Store)
	ENUM_ENTRY(Discard, WGPUStoreOp_Discard)
	ENUM_ENTRY(Force32, WGPUStoreOp_Force32)
END
ENUM(SurfaceGetCurrentTextureStatus)
	ENUM_ENTRY(SuccessOptimal, WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal)
	ENUM_ENTRY(SuccessSuboptimal, WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
	ENUM_ENTRY(Timeout, WGPUSurfaceGetCurrentTextureStatus_Timeout)
	ENUM_ENTRY(Outdated, WGPUSurfaceGetCurrentTextureStatus_Outdated)
	ENUM_ENTRY(Lost, WGPUSurfaceGetCurrentTextureStatus_Lost)
	ENUM_ENTRY(OutOfMemory, WGPUSurfaceGetCurrentTextureStatus_OutOfMemory)
	ENUM_ENTRY(DeviceLost, WGPUSurfaceGetCurrentTextureStatus_DeviceLost)
	ENUM_ENTRY(Error, WGPUSurfaceGetCurrentTextureStatus_Error)
	ENUM_ENTRY(Force32, WGPUSurfaceGetCurrentTextureStatus_Force32)
END
ENUM(TextureAspect)
	ENUM_ENTRY(Undefined, WGPUTextureAspect_Undefined)
	ENUM_ENTRY(All, WGPUTextureAspect_All)
	ENUM_ENTRY(StencilOnly, WGPUTextureAspect_StencilOnly)
	ENUM_ENTRY(DepthOnly, WGPUTextureAspect_DepthOnly)
	ENUM_ENTRY(Force32, WGPUTextureAspect_Force32)
END
ENUM(TextureDimension)
	ENUM_ENTRY(Undefined, WGPUTextureDimension_Undefined)
	ENUM_ENTRY(_1D, WGPUTextureDimension_1D)
	ENUM_ENTRY(_2D, WGPUTextureDimension_2D)
	ENUM_ENTRY(_3D, WGPUTextureDimension_3D)
	ENUM_ENTRY(Force32, WGPUTextureDimension_Force32)
END
ENUM(TextureFormat)
	ENUM_ENTRY(Undefined, WGPUTextureFormat_Undefined)
	ENUM_ENTRY(R8Unorm, WGPUTextureFormat_R8Unorm)
	ENUM_ENTRY(R8Snorm, WGPUTextureFormat_R8Snorm)
	ENUM_ENTRY(R8Uint, WGPUTextureFormat_R8Uint)
	ENUM_ENTRY(R8Sint, WGPUTextureFormat_R8Sint)
	ENUM_ENTRY(R16Uint, WGPUTextureFormat_R16Uint)
	ENUM_ENTRY(R16Sint, WGPUTextureFormat_R16Sint)
	ENUM_ENTRY(R16Float, WGPUTextureFormat_R16Float)
	ENUM_ENTRY(RG8Unorm, WGPUTextureFormat_RG8Unorm)
	ENUM_ENTRY(RG8Snorm, WGPUTextureFormat_RG8Snorm)
	ENUM_ENTRY(RG8Uint, WGPUTextureFormat_RG8Uint)
	ENUM_ENTRY(RG8Sint, WGPUTextureFormat_RG8Sint)
	ENUM_ENTRY(R32Float, WGPUTextureFormat_R32Float)
	ENUM_ENTRY(R32Uint, WGPUTextureFormat_R32Uint)
	ENUM_ENTRY(R32Sint, WGPUTextureFormat_R32Sint)
	ENUM_ENTRY(RG16Uint, WGPUTextureFormat_RG16Uint)
	ENUM_ENTRY(RG16Sint, WGPUTextureFormat_RG16Sint)
	ENUM_ENTRY(RG16Float, WGPUTextureFormat_RG16Float)
	ENUM_ENTRY(RGBA8Unorm, WGPUTextureFormat_RGBA8Unorm)
	ENUM_ENTRY(RGBA8UnormSrgb, WGPUTextureFormat_RGBA8UnormSrgb)
	ENUM_ENTRY(RGBA8Snorm, WGPUTextureFormat_RGBA8Snorm)
	ENUM_ENTRY(RGBA8Uint, WGPUTextureFormat_RGBA8Uint)
	ENUM_ENTRY(RGBA8Sint, WGPUTextureFormat_RGBA8Sint)
	ENUM_ENTRY(BGRA8Unorm, WGPUTextureFormat_BGRA8Unorm)
	ENUM_ENTRY(BGRA8UnormSrgb, WGPUTextureFormat_BGRA8UnormSrgb)
	ENUM_ENTRY(RGB10A2Uint, WGPUTextureFormat_RGB10A2Uint)
	ENUM_ENTRY(RGB10A2Unorm, WGPUTextureFormat_RGB10A2Unorm)
	ENUM_ENTRY(RG11B10Ufloat, WGPUTextureFormat_RG11B10Ufloat)
	ENUM_ENTRY(RGB9E5Ufloat, WGPUTextureFormat_RGB9E5Ufloat)
	ENUM_ENTRY(RG32Float, WGPUTextureFormat_RG32Float)
	ENUM_ENTRY(RG32Uint, WGPUTextureFormat_RG32Uint)
	ENUM_ENTRY(RG32Sint, WGPUTextureFormat_RG32Sint)
	ENUM_ENTRY(RGBA16Uint, WGPUTextureFormat_RGBA16Uint)
	ENUM_ENTRY(RGBA16Sint, WGPUTextureFormat_RGBA16Sint)
	ENUM_ENTRY(RGBA16Float, WGPUTextureFormat_RGBA16Float)
	ENUM_ENTRY(RGBA32Float, WGPUTextureFormat_RGBA32Float)
	ENUM_ENTRY(RGBA32Uint, WGPUTextureFormat_RGBA32Uint)
	ENUM_ENTRY(RGBA32Sint, WGPUTextureFormat_RGBA32Sint)
	ENUM_ENTRY(Stencil8, WGPUTextureFormat_Stencil8)
	ENUM_ENTRY(Depth16Unorm, WGPUTextureFormat_Depth16Unorm)
	ENUM_ENTRY(Depth24Plus, WGPUTextureFormat_Depth24Plus)
	ENUM_ENTRY(Depth24PlusStencil8, WGPUTextureFormat_Depth24PlusStencil8)
	ENUM_ENTRY(Depth32Float, WGPUTextureFormat_Depth32Float)
	ENUM_ENTRY(Depth32FloatStencil8, WGPUTextureFormat_Depth32FloatStencil8)
	ENUM_ENTRY(BC1RGBAUnorm, WGPUTextureFormat_BC1RGBAUnorm)
	ENUM_ENTRY(BC1RGBAUnormSrgb, WGPUTextureFormat_BC1RGBAUnormSrgb)
	ENUM_ENTRY(BC2RGBAUnorm, WGPUTextureFormat_BC2RGBAUnorm)
	ENUM_ENTRY(BC2RGBAUnormSrgb, WGPUTextureFormat_BC2RGBAUnormSrgb)
	ENUM_ENTRY(BC3RGBAUnorm, WGPUTextureFormat_BC3RGBAUnorm)
	ENUM_ENTRY(BC3RGBAUnormSrgb, WGPUTextureFormat_BC3RGBAUnormSrgb)
	ENUM_ENTRY(BC4RUnorm, WGPUTextureFormat_BC4RUnorm)
	ENUM_ENTRY(BC4RSnorm, WGPUTextureFormat_BC4RSnorm)
	ENUM_ENTRY(BC5RGUnorm, WGPUTextureFormat_BC5RGUnorm)
	ENUM_ENTRY(BC5RGSnorm, WGPUTextureFormat_BC5RGSnorm)
	ENUM_ENTRY(BC6HRGBUfloat, WGPUTextureFormat_BC6HRGBUfloat)
	ENUM_ENTRY(BC6HRGBFloat, WGPUTextureFormat_BC6HRGBFloat)
	ENUM_ENTRY(BC7RGBAUnorm, WGPUTextureFormat_BC7RGBAUnorm)
	ENUM_ENTRY(BC7RGBAUnormSrgb, WGPUTextureFormat_BC7RGBAUnormSrgb)
	ENUM_ENTRY(ETC2RGB8Unorm, WGPUTextureFormat_ETC2RGB8Unorm)
	ENUM_ENTRY(ETC2RGB8UnormSrgb, WGPUTextureFormat_ETC2RGB8UnormSrgb)
	ENUM_ENTRY(ETC2RGB8A1Unorm, WGPUTextureFormat_ETC2RGB8A1Unorm)
	ENUM_ENTRY(ETC2RGB8A1UnormSrgb, WGPUTextureFormat_ETC2RGB8A1UnormSrgb)
	ENUM_ENTRY(ETC2RGBA8Unorm, WGPUTextureFormat_ETC2RGBA8Unorm)
	ENUM_ENTRY(ETC2RGBA8UnormSrgb, WGPUTextureFormat_ETC2RGBA8UnormSrgb)
	ENUM_ENTRY(EACR11Unorm, WGPUTextureFormat_EACR11Unorm)
	ENUM_ENTRY(EACR11Snorm, WGPUTextureFormat_EACR11Snorm)
	ENUM_ENTRY(EACRG11Unorm, WGPUTextureFormat_EACRG11Unorm)
	ENUM_ENTRY(EACRG11Snorm, WGPUTextureFormat_EACRG11Snorm)
	ENUM_ENTRY(ASTC4x4Unorm, WGPUTextureFormat_ASTC4x4Unorm)
	ENUM_ENTRY(ASTC4x4UnormSrgb, WGPUTextureFormat_ASTC4x4UnormSrgb)
	ENUM_ENTRY(ASTC5x4Unorm, WGPUTextureFormat_ASTC5x4Unorm)
	ENUM_ENTRY(ASTC5x4UnormSrgb, WGPUTextureFormat_ASTC5x4UnormSrgb)
	ENUM_ENTRY(ASTC5x5Unorm, WGPUTextureFormat_ASTC5x5Unorm)
	ENUM_ENTRY(ASTC5x5UnormSrgb, WGPUTextureFormat_ASTC5x5UnormSrgb)
	ENUM_ENTRY(ASTC6x5Unorm, WGPUTextureFormat_ASTC6x5Unorm)
	ENUM_ENTRY(ASTC6x5UnormSrgb, WGPUTextureFormat_ASTC6x5UnormSrgb)
	ENUM_ENTRY(ASTC6x6Unorm, WGPUTextureFormat_ASTC6x6Unorm)
	ENUM_ENTRY(ASTC6x6UnormSrgb, WGPUTextureFormat_ASTC6x6UnormSrgb)
	ENUM_ENTRY(ASTC8x5Unorm, WGPUTextureFormat_ASTC8x5Unorm)
	ENUM_ENTRY(ASTC8x5UnormSrgb, WGPUTextureFormat_ASTC8x5UnormSrgb)
	ENUM_ENTRY(ASTC8x6Unorm, WGPUTextureFormat_ASTC8x6Unorm)
	ENUM_ENTRY(ASTC8x6UnormSrgb, WGPUTextureFormat_ASTC8x6UnormSrgb)
	ENUM_ENTRY(ASTC8x8Unorm, WGPUTextureFormat_ASTC8x8Unorm)
	ENUM_ENTRY(ASTC8x8UnormSrgb, WGPUTextureFormat_ASTC8x8UnormSrgb)
	ENUM_ENTRY(ASTC10x5Unorm, WGPUTextureFormat_ASTC10x5Unorm)
	ENUM_ENTRY(ASTC10x5UnormSrgb, WGPUTextureFormat_ASTC10x5UnormSrgb)
	ENUM_ENTRY(ASTC10x6Unorm, WGPUTextureFormat_ASTC10x6Unorm)
	ENUM_ENTRY(ASTC10x6UnormSrgb, WGPUTextureFormat_ASTC10x6UnormSrgb)
	ENUM_ENTRY(ASTC10x8Unorm, WGPUTextureFormat_ASTC10x8Unorm)
	ENUM_ENTRY(ASTC10x8UnormSrgb, WGPUTextureFormat_ASTC10x8UnormSrgb)
	ENUM_ENTRY(ASTC10x10Unorm, WGPUTextureFormat_ASTC10x10Unorm)
	ENUM_ENTRY(ASTC10x10UnormSrgb, WGPUTextureFormat_ASTC10x10UnormSrgb)
	ENUM_ENTRY(ASTC12x10Unorm, WGPUTextureFormat_ASTC12x10Unorm)
	ENUM_ENTRY(ASTC12x10UnormSrgb, WGPUTextureFormat_ASTC12x10UnormSrgb)
	ENUM_ENTRY(ASTC12x12Unorm, WGPUTextureFormat_ASTC12x12Unorm)
	ENUM_ENTRY(ASTC12x12UnormSrgb, WGPUTextureFormat_ASTC12x12UnormSrgb)
	ENUM_ENTRY(Force32, WGPUTextureFormat_Force32)
END
ENUM(TextureSampleType)
	ENUM_ENTRY(BindingNotUsed, WGPUTextureSampleType_BindingNotUsed)
	ENUM_ENTRY(Undefined, WGPUTextureSampleType_Undefined)
	ENUM_ENTRY(Float, WGPUTextureSampleType_Float)
	ENUM_ENTRY(UnfilterableFloat, WGPUTextureSampleType_UnfilterableFloat)
	ENUM_ENTRY(Depth, WGPUTextureSampleType_Depth)
	ENUM_ENTRY(Sint, WGPUTextureSampleType_Sint)
	ENUM_ENTRY(Uint, WGPUTextureSampleType_Uint)
	ENUM_ENTRY(Force32, WGPUTextureSampleType_Force32)
END
ENUM(TextureViewDimension)
	ENUM_ENTRY(Undefined, WGPUTextureViewDimension_Undefined)
	ENUM_ENTRY(_1D, WGPUTextureViewDimension_1D)
	ENUM_ENTRY(_2D, WGPUTextureViewDimension_2D)
	ENUM_ENTRY(_2DArray, WGPUTextureViewDimension_2DArray)
	ENUM_ENTRY(Cube, WGPUTextureViewDimension_Cube)
	ENUM_ENTRY(CubeArray, WGPUTextureViewDimension_CubeArray)
	ENUM_ENTRY(_3D, WGPUTextureViewDimension_3D)
	ENUM_ENTRY(Force32, WGPUTextureViewDimension_Force32)
END
ENUM(VertexFormat)
	ENUM_ENTRY(Uint8, WGPUVertexFormat_Uint8)
	ENUM_ENTRY(Uint8x2, WGPUVertexFormat_Uint8x2)
	ENUM_ENTRY(Uint8x4, WGPUVertexFormat_Uint8x4)
	ENUM_ENTRY(Sint8, WGPUVertexFormat_Sint8)
	ENUM_ENTRY(Sint8x2, WGPUVertexFormat_Sint8x2)
	ENUM_ENTRY(Sint8x4, WGPUVertexFormat_Sint8x4)
	ENUM_ENTRY(Unorm8, WGPUVertexFormat_Unorm8)
	ENUM_ENTRY(Unorm8x2, WGPUVertexFormat_Unorm8x2)
	ENUM_ENTRY(Unorm8x4, WGPUVertexFormat_Unorm8x4)
	ENUM_ENTRY(Snorm8, WGPUVertexFormat_Snorm8)
	ENUM_ENTRY(Snorm8x2, WGPUVertexFormat_Snorm8x2)
	ENUM_ENTRY(Snorm8x4, WGPUVertexFormat_Snorm8x4)
	ENUM_ENTRY(Uint16, WGPUVertexFormat_Uint16)
	ENUM_ENTRY(Uint16x2, WGPUVertexFormat_Uint16x2)
	ENUM_ENTRY(Uint16x4, WGPUVertexFormat_Uint16x4)
	ENUM_ENTRY(Sint16, WGPUVertexFormat_Sint16)
	ENUM_ENTRY(Sint16x2, WGPUVertexFormat_Sint16x2)
	ENUM_ENTRY(Sint16x4, WGPUVertexFormat_Sint16x4)
	ENUM_ENTRY(Unorm16, WGPUVertexFormat_Unorm16)
	ENUM_ENTRY(Unorm16x2, WGPUVertexFormat_Unorm16x2)
	ENUM_ENTRY(Unorm16x4, WGPUVertexFormat_Unorm16x4)
	ENUM_ENTRY(Snorm16, WGPUVertexFormat_Snorm16)
	ENUM_ENTRY(Snorm16x2, WGPUVertexFormat_Snorm16x2)
	ENUM_ENTRY(Snorm16x4, WGPUVertexFormat_Snorm16x4)
	ENUM_ENTRY(Float16, WGPUVertexFormat_Float16)
	ENUM_ENTRY(Float16x2, WGPUVertexFormat_Float16x2)
	ENUM_ENTRY(Float16x4, WGPUVertexFormat_Float16x4)
	ENUM_ENTRY(Float32, WGPUVertexFormat_Float32)
	ENUM_ENTRY(Float32x2, WGPUVertexFormat_Float32x2)
	ENUM_ENTRY(Float32x3, WGPUVertexFormat_Float32x3)
	ENUM_ENTRY(Float32x4, WGPUVertexFormat_Float32x4)
	ENUM_ENTRY(Uint32, WGPUVertexFormat_Uint32)
	ENUM_ENTRY(Uint32x2, WGPUVertexFormat_Uint32x2)
	ENUM_ENTRY(Uint32x3, WGPUVertexFormat_Uint32x3)
	ENUM_ENTRY(Uint32x4, WGPUVertexFormat_Uint32x4)
	ENUM_ENTRY(Sint32, WGPUVertexFormat_Sint32)
	ENUM_ENTRY(Sint32x2, WGPUVertexFormat_Sint32x2)
	ENUM_ENTRY(Sint32x3, WGPUVertexFormat_Sint32x3)
	ENUM_ENTRY(Sint32x4, WGPUVertexFormat_Sint32x4)
	ENUM_ENTRY(Unorm10_10_10_2, WGPUVertexFormat_Unorm10_10_10_2)
	ENUM_ENTRY(Unorm8x4BGRA, WGPUVertexFormat_Unorm8x4BGRA)
	ENUM_ENTRY(Force32, WGPUVertexFormat_Force32)
END
ENUM(VertexStepMode)
	ENUM_ENTRY(VertexBufferNotUsed, WGPUVertexStepMode_VertexBufferNotUsed)
	ENUM_ENTRY(Undefined, WGPUVertexStepMode_Undefined)
	ENUM_ENTRY(Vertex, WGPUVertexStepMode_Vertex)
	ENUM_ENTRY(Instance, WGPUVertexStepMode_Instance)
	ENUM_ENTRY(Force32, WGPUVertexStepMode_Force32)
END
ENUM(WGSLLanguageFeatureName)
	ENUM_ENTRY(ReadonlyAndReadwriteStorageTextures, WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures)
	ENUM_ENTRY(Packed4x8IntegerDotProduct, WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct)
	ENUM_ENTRY(UnrestrictedPointerParameters, WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters)
	ENUM_ENTRY(PointerCompositeAccess, WGPUWGSLLanguageFeatureName_PointerCompositeAccess)
	ENUM_ENTRY(Force32, WGPUWGSLLanguageFeatureName_Force32)
END
ENUM(WaitStatus)
	ENUM_ENTRY(Success, WGPUWaitStatus_Success)
	ENUM_ENTRY(TimedOut, WGPUWaitStatus_TimedOut)
	ENUM_ENTRY(UnsupportedTimeout, WGPUWaitStatus_UnsupportedTimeout)
	ENUM_ENTRY(UnsupportedCount, WGPUWaitStatus_UnsupportedCount)
	ENUM_ENTRY(UnsupportedMixedSources, WGPUWaitStatus_UnsupportedMixedSources)
	ENUM_ENTRY(Force32, WGPUWaitStatus_Force32)
END
ENUM(BufferUsage)
	ENUM_ENTRY(None, 0x0000000000000000)
	ENUM_ENTRY(MapRead, 0x0000000000000001)
	ENUM_ENTRY(MapWrite, 0x0000000000000002)
	ENUM_ENTRY(CopySrc, 0x0000000000000004)
	ENUM_ENTRY(CopyDst, 0x0000000000000008)
	ENUM_ENTRY(Index, 0x0000000000000010)
	ENUM_ENTRY(Vertex, 0x0000000000000020)
	ENUM_ENTRY(Uniform, 0x0000000000000040)
	ENUM_ENTRY(Storage, 0x0000000000000080)
	ENUM_ENTRY(Indirect, 0x0000000000000100)
	ENUM_ENTRY(QueryResolve, 0x0000000000000200)
END
ENUM(ColorWriteMask)
	ENUM_ENTRY(None, 0x0000000000000000)
	ENUM_ENTRY(Red, 0x0000000000000001)
	ENUM_ENTRY(Green, 0x0000000000000002)
	ENUM_ENTRY(Blue, 0x0000000000000004)
	ENUM_ENTRY(Alpha, 0x0000000000000008)
	ENUM_ENTRY(All, 0x000000000000000F)
END
ENUM(MapMode)
	ENUM_ENTRY(None, 0x0000000000000000)
	ENUM_ENTRY(Read, 0x0000000000000001)
	ENUM_ENTRY(Write, 0x0000000000000002)
END
ENUM(ShaderStage)
	ENUM_ENTRY(None, 0x0000000000000000)
	ENUM_ENTRY(Vertex, 0x0000000000000001)
	ENUM_ENTRY(Fragment, 0x0000000000000002)
	ENUM_ENTRY(Compute, 0x0000000000000004)
END
ENUM(TextureUsage)
	ENUM_ENTRY(None, 0x0000000000000000)
	ENUM_ENTRY(CopySrc, 0x0000000000000001)
	ENUM_ENTRY(CopyDst, 0x0000000000000002)
	ENUM_ENTRY(TextureBinding, 0x0000000000000004)
	ENUM_ENTRY(StorageBinding, 0x0000000000000008)
	ENUM_ENTRY(RenderAttachment, 0x0000000000000010)
END
ENUM(NativeSType)
	ENUM_ENTRY(DeviceExtras, WGPUSType_DeviceExtras)
	ENUM_ENTRY(NativeLimits, WGPUSType_NativeLimits)
	ENUM_ENTRY(PipelineLayoutExtras, WGPUSType_PipelineLayoutExtras)
	ENUM_ENTRY(ShaderSourceGLSL, WGPUSType_ShaderSourceGLSL)
	ENUM_ENTRY(InstanceExtras, WGPUSType_InstanceExtras)
	ENUM_ENTRY(BindGroupEntryExtras, WGPUSType_BindGroupEntryExtras)
	ENUM_ENTRY(BindGroupLayoutEntryExtras, WGPUSType_BindGroupLayoutEntryExtras)
	ENUM_ENTRY(QuerySetDescriptorExtras, WGPUSType_QuerySetDescriptorExtras)
	ENUM_ENTRY(SurfaceConfigurationExtras, WGPUSType_SurfaceConfigurationExtras)
	ENUM_ENTRY(SurfaceSourceSwapChainPanel, WGPUSType_SurfaceSourceSwapChainPanel)
	ENUM_ENTRY(PrimitiveStateExtras, WGPUSType_PrimitiveStateExtras)
	ENUM_ENTRY(Force32, WGPUNativeSType_Force32)
END
ENUM(NativeFeature)
	ENUM_ENTRY(PushConstants, WGPUNativeFeature_PushConstants)
	ENUM_ENTRY(TextureAdapterSpecificFormatFeatures, WGPUNativeFeature_TextureAdapterSpecificFormatFeatures)
	ENUM_ENTRY(MultiDrawIndirectCount, WGPUNativeFeature_MultiDrawIndirectCount)
	ENUM_ENTRY(VertexWritableStorage, WGPUNativeFeature_VertexWritableStorage)
	ENUM_ENTRY(TextureBindingArray, WGPUNativeFeature_TextureBindingArray)
	ENUM_ENTRY(SampledTextureAndStorageBufferArrayNonUniformIndexing, WGPUNativeFeature_SampledTextureAndStorageBufferArrayNonUniformIndexing)
	ENUM_ENTRY(PipelineStatisticsQuery, WGPUNativeFeature_PipelineStatisticsQuery)
	ENUM_ENTRY(StorageResourceBindingArray, WGPUNativeFeature_StorageResourceBindingArray)
	ENUM_ENTRY(PartiallyBoundBindingArray, WGPUNativeFeature_PartiallyBoundBindingArray)
	ENUM_ENTRY(TextureFormat16bitNorm, WGPUNativeFeature_TextureFormat16bitNorm)
	ENUM_ENTRY(TextureCompressionAstcHdr, WGPUNativeFeature_TextureCompressionAstcHdr)
	ENUM_ENTRY(MappablePrimaryBuffers, WGPUNativeFeature_MappablePrimaryBuffers)
	ENUM_ENTRY(BufferBindingArray, WGPUNativeFeature_BufferBindingArray)
	ENUM_ENTRY(UniformBufferAndStorageTextureArrayNonUniformIndexing, WGPUNativeFeature_UniformBufferAndStorageTextureArrayNonUniformIndexing)
	ENUM_ENTRY(PolygonModeLine, WGPUNativeFeature_PolygonModeLine)
	ENUM_ENTRY(PolygonModePoint, WGPUNativeFeature_PolygonModePoint)
	ENUM_ENTRY(ConservativeRasterization, WGPUNativeFeature_ConservativeRasterization)
	ENUM_ENTRY(SpirvShaderPassthrough, WGPUNativeFeature_SpirvShaderPassthrough)
	ENUM_ENTRY(VertexAttribute64bit, WGPUNativeFeature_VertexAttribute64bit)
	ENUM_ENTRY(TextureFormatNv12, WGPUNativeFeature_TextureFormatNv12)
	ENUM_ENTRY(RayQuery, WGPUNativeFeature_RayQuery)
	ENUM_ENTRY(ShaderF64, WGPUNativeFeature_ShaderF64)
	ENUM_ENTRY(ShaderI16, WGPUNativeFeature_ShaderI16)
	ENUM_ENTRY(ShaderPrimitiveIndex, WGPUNativeFeature_ShaderPrimitiveIndex)
	ENUM_ENTRY(ShaderEarlyDepthTest, WGPUNativeFeature_ShaderEarlyDepthTest)
	ENUM_ENTRY(Subgroup, WGPUNativeFeature_Subgroup)
	ENUM_ENTRY(SubgroupVertex, WGPUNativeFeature_SubgroupVertex)
	ENUM_ENTRY(SubgroupBarrier, WGPUNativeFeature_SubgroupBarrier)
	ENUM_ENTRY(TimestampQueryInsideEncoders, WGPUNativeFeature_TimestampQueryInsideEncoders)
	ENUM_ENTRY(TimestampQueryInsidePasses, WGPUNativeFeature_TimestampQueryInsidePasses)
	ENUM_ENTRY(ShaderInt64, WGPUNativeFeature_ShaderInt64)
	ENUM_ENTRY(Force32, WGPUNativeFeature_Force32)
END
ENUM(LogLevel)
	ENUM_ENTRY(Off, WGPULogLevel_Off)
	ENUM_ENTRY(Error, WGPULogLevel_Error)
	ENUM_ENTRY(Warn, WGPULogLevel_Warn)
	ENUM_ENTRY(Info, WGPULogLevel_Info)
	ENUM_ENTRY(Debug, WGPULogLevel_Debug)
	ENUM_ENTRY(Trace, WGPULogLevel_Trace)
	ENUM_ENTRY(Force32, WGPULogLevel_Force32)
END
ENUM(InstanceBackend)
	ENUM_ENTRY(All, 0x00000000)
	ENUM_ENTRY(Force32, 0x7FFFFFFF)
END
ENUM(InstanceFlag)
	ENUM_ENTRY(Default, 0x00000000)
	ENUM_ENTRY(Force32, 0x7FFFFFFF)
END
ENUM(Dx12Compiler)
	ENUM_ENTRY(Undefined, WGPUDx12Compiler_Undefined)
	ENUM_ENTRY(Fxc, WGPUDx12Compiler_Fxc)
	ENUM_ENTRY(Dxc, WGPUDx12Compiler_Dxc)
	ENUM_ENTRY(Force32, WGPUDx12Compiler_Force32)
END
ENUM(Gles3MinorVersion)
	ENUM_ENTRY(Automatic, WGPUGles3MinorVersion_Automatic)
	ENUM_ENTRY(Version0, WGPUGles3MinorVersion_Version0)
	ENUM_ENTRY(Version1, WGPUGles3MinorVersion_Version1)
	ENUM_ENTRY(Version2, WGPUGles3MinorVersion_Version2)
	ENUM_ENTRY(Force32, WGPUGles3MinorVersion_Force32)
END
ENUM(PipelineStatisticName)
	ENUM_ENTRY(VertexShaderInvocations, WGPUPipelineStatisticName_VertexShaderInvocations)
	ENUM_ENTRY(ClipperInvocations, WGPUPipelineStatisticName_ClipperInvocations)
	ENUM_ENTRY(ClipperPrimitivesOut, WGPUPipelineStatisticName_ClipperPrimitivesOut)
	ENUM_ENTRY(FragmentShaderInvocations, WGPUPipelineStatisticName_FragmentShaderInvocations)
	ENUM_ENTRY(ComputeShaderInvocations, WGPUPipelineStatisticName_ComputeShaderInvocations)
	ENUM_ENTRY(Force32, WGPUPipelineStatisticName_Force32)
END
ENUM(NativeQueryType)
	ENUM_ENTRY(PipelineStatistics, WGPUNativeQueryType_PipelineStatistics)
	ENUM_ENTRY(Force32, WGPUNativeQueryType_Force32)
END
ENUM(DxcMaxShaderModel)
	ENUM_ENTRY(V6_0, WGPUDxcMaxShaderModel_V6_0)
	ENUM_ENTRY(V6_1, WGPUDxcMaxShaderModel_V6_1)
	ENUM_ENTRY(V6_2, WGPUDxcMaxShaderModel_V6_2)
	ENUM_ENTRY(V6_3, WGPUDxcMaxShaderModel_V6_3)
	ENUM_ENTRY(V6_4, WGPUDxcMaxShaderModel_V6_4)
	ENUM_ENTRY(V6_5, WGPUDxcMaxShaderModel_V6_5)
	ENUM_ENTRY(V6_6, WGPUDxcMaxShaderModel_V6_6)
	ENUM_ENTRY(V6_7, WGPUDxcMaxShaderModel_V6_7)
	ENUM_ENTRY(Force32, WGPUDxcMaxShaderModel_Force32)
END
ENUM(GLFenceBehaviour)
	ENUM_ENTRY(Normal, WGPUGLFenceBehaviour_Normal)
	ENUM_ENTRY(AutoFinish, WGPUGLFenceBehaviour_AutoFinish)
	ENUM_ENTRY(Force32, WGPUGLFenceBehaviour_Force32)
END
ENUM(Dx12SwapchainKind)
	ENUM_ENTRY(Undefined, WGPUDx12SwapchainKind_Undefined)
	ENUM_ENTRY(DxgiFromHwnd, WGPUDx12SwapchainKind_DxgiFromHwnd)
	ENUM_ENTRY(DxgiFromVisual, WGPUDx12SwapchainKind_DxgiFromVisual)
	ENUM_ENTRY(Force32, WGPUDx12SwapchainKind_Force32)
END
ENUM(PolygonMode)
	ENUM_ENTRY(Fill, WGPUPolygonMode_Fill)
	ENUM_ENTRY(Line, WGPUPolygonMode_Line)
	ENUM_ENTRY(Point, WGPUPolygonMode_Point)
END
ENUM(NativeTextureFormat)
	ENUM_ENTRY(R16Unorm, WGPUNativeTextureFormat_R16Unorm)
	ENUM_ENTRY(R16Snorm, WGPUNativeTextureFormat_R16Snorm)
	ENUM_ENTRY(Rg16Unorm, WGPUNativeTextureFormat_Rg16Unorm)
	ENUM_ENTRY(Rg16Snorm, WGPUNativeTextureFormat_Rg16Snorm)
	ENUM_ENTRY(Rgba16Unorm, WGPUNativeTextureFormat_Rgba16Unorm)
	ENUM_ENTRY(Rgba16Snorm, WGPUNativeTextureFormat_Rgba16Snorm)
	ENUM_ENTRY(NV12, WGPUNativeTextureFormat_NV12)
	ENUM_ENTRY(P010, WGPUNativeTextureFormat_P010)
END

// Handles forward declarations
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

// Structs
STRUCT_NO_OSTREAM(StringView)
	StringView& setDefault();
	StringView& setData(const char * data);
	StringView& setLength(size_t length);
	StringView(const std::string_view& cpp) : WGPUStringView{ cpp.data(), cpp.length() } {}
	operator std::string_view() const;
	friend auto operator<<(std::ostream& stream, const S& self) -> std::ostream& {
		return stream << std::string_view(self);
	}
END

STRUCT(ChainedStruct)
	ChainedStruct& setDefault();
	ChainedStruct& setNext(const struct WGPUChainedStruct * next);
	ChainedStruct& setSType(SType sType);
END

STRUCT(ChainedStructOut)
	ChainedStructOut& setDefault();
	ChainedStructOut& setNext(struct WGPUChainedStructOut * next);
	ChainedStructOut& setSType(SType sType);
END

STRUCT(BlendComponent)
	BlendComponent& setDefault();
	BlendComponent& setOperation(BlendOperation operation);
	BlendComponent& setSrcFactor(BlendFactor srcFactor);
	BlendComponent& setDstFactor(BlendFactor dstFactor);
END

STRUCT(Color)
	Color& setDefault();
	Color& setR(double r);
	Color& setG(double g);
	Color& setB(double b);
	Color& setA(double a);
	Color(double r, double g, double b, double a) : WGPUColor{ r, g, b, a } {}
END

STRUCT(ComputePassTimestampWrites)
	ComputePassTimestampWrites& setDefault();
	ComputePassTimestampWrites& setQuerySet(QuerySet querySet);
	ComputePassTimestampWrites& setBeginningOfPassWriteIndex(uint32_t beginningOfPassWriteIndex);
	ComputePassTimestampWrites& setEndOfPassWriteIndex(uint32_t endOfPassWriteIndex);
END

STRUCT(Extent3D)
	Extent3D& setDefault();
	Extent3D& setWidth(uint32_t width);
	Extent3D& setHeight(uint32_t height);
	Extent3D& setDepthOrArrayLayers(uint32_t depthOrArrayLayers);
	Extent3D(uint32_t width, uint32_t height, uint32_t depthOrArrayLayers) : WGPUExtent3D{ width, height, depthOrArrayLayers } {}
END

STRUCT(Future)
	Future& setDefault();
	Future& setId(uint64_t id);
END

STRUCT(Origin3D)
	Origin3D& setDefault();
	Origin3D& setX(uint32_t x);
	Origin3D& setY(uint32_t y);
	Origin3D& setZ(uint32_t z);
	Origin3D(uint32_t x, uint32_t y, uint32_t z) : WGPUOrigin3D{ x, y, z } {}
END

STRUCT(RenderPassDepthStencilAttachment)
	RenderPassDepthStencilAttachment& setDefault();
	RenderPassDepthStencilAttachment& setView(TextureView view);
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
	RenderPassMaxDrawCount& setDefault();
	RenderPassMaxDrawCount& setMaxDrawCount(uint64_t maxDrawCount);
END

STRUCT(RenderPassTimestampWrites)
	RenderPassTimestampWrites& setDefault();
	RenderPassTimestampWrites& setQuerySet(QuerySet querySet);
	RenderPassTimestampWrites& setBeginningOfPassWriteIndex(uint32_t beginningOfPassWriteIndex);
	RenderPassTimestampWrites& setEndOfPassWriteIndex(uint32_t endOfPassWriteIndex);
END

STRUCT(ShaderSourceSPIRV)
	ShaderSourceSPIRV& setDefault();
	ShaderSourceSPIRV& setCodeSize(uint32_t codeSize);
	ShaderSourceSPIRV& setCode(const uint32_t * code);
END

STRUCT(ShaderSourceWGSL)
	ShaderSourceWGSL& setDefault();
	ShaderSourceWGSL& setCode(StringView code);
END

STRUCT(StencilFaceState)
	StencilFaceState& setDefault();
	StencilFaceState& setCompare(CompareFunction compare);
	StencilFaceState& setFailOp(StencilOperation failOp);
	StencilFaceState& setDepthFailOp(StencilOperation depthFailOp);
	StencilFaceState& setPassOp(StencilOperation passOp);
END

STRUCT(SupportedFeatures)
	SupportedFeatures& setDefault();
	SupportedFeatures& setFeatures(uint32_t featureCount, const FeatureName * features);
	SupportedFeatures& setFeatures(const std::vector<FeatureName>& features);
	SupportedFeatures& setFeatures(const std::span<const FeatureName>& features);
	void freeMembers();
END

STRUCT(SupportedWGSLLanguageFeatures)
	SupportedWGSLLanguageFeatures& setDefault();
	SupportedWGSLLanguageFeatures& setFeatures(uint32_t featureCount, const WGSLLanguageFeatureName * features);
	SupportedWGSLLanguageFeatures& setFeatures(const std::vector<WGSLLanguageFeatureName>& features);
	SupportedWGSLLanguageFeatures& setFeatures(const std::span<const WGSLLanguageFeatureName>& features);
	void freeMembers();
END

STRUCT(SurfaceSourceAndroidNativeWindow)
	SurfaceSourceAndroidNativeWindow& setDefault();
	SurfaceSourceAndroidNativeWindow& setWindow(void * window);
END

STRUCT(SurfaceSourceMetalLayer)
	SurfaceSourceMetalLayer& setDefault();
	SurfaceSourceMetalLayer& setLayer(void * layer);
END

STRUCT(SurfaceSourceWaylandSurface)
	SurfaceSourceWaylandSurface& setDefault();
	SurfaceSourceWaylandSurface& setDisplay(void * display);
	SurfaceSourceWaylandSurface& setSurface(void * surface);
END

STRUCT(SurfaceSourceWindowsHWND)
	SurfaceSourceWindowsHWND& setDefault();
	SurfaceSourceWindowsHWND& setHinstance(void * hinstance);
	SurfaceSourceWindowsHWND& setHwnd(void * hwnd);
END

STRUCT(SurfaceSourceXCBWindow)
	SurfaceSourceXCBWindow& setDefault();
	SurfaceSourceXCBWindow& setConnection(void * connection);
	SurfaceSourceXCBWindow& setWindow(uint32_t window);
END

STRUCT(SurfaceSourceXlibWindow)
	SurfaceSourceXlibWindow& setDefault();
	SurfaceSourceXlibWindow& setDisplay(void * display);
	SurfaceSourceXlibWindow& setWindow(uint64_t window);
END

STRUCT(TexelCopyBufferLayout)
	TexelCopyBufferLayout& setDefault();
	TexelCopyBufferLayout& setOffset(uint64_t offset);
	TexelCopyBufferLayout& setBytesPerRow(uint32_t bytesPerRow);
	TexelCopyBufferLayout& setRowsPerImage(uint32_t rowsPerImage);
END

STRUCT(VertexAttribute)
	VertexAttribute& setDefault();
	VertexAttribute& setFormat(VertexFormat format);
	VertexAttribute& setOffset(uint64_t offset);
	VertexAttribute& setShaderLocation(uint32_t shaderLocation);
END

STRUCT(BlendState)
	BlendState& setDefault();
	BlendState& setColor(BlendComponent color);
	BlendState& setAlpha(BlendComponent alpha);
END

STRUCT(FutureWaitInfo)
	FutureWaitInfo& setDefault();
	FutureWaitInfo& setFuture(Future future);
	FutureWaitInfo& setCompleted(WGPUBool completed);
END

STRUCT(TexelCopyBufferInfo)
	TexelCopyBufferInfo& setDefault();
	TexelCopyBufferInfo& setLayout(TexelCopyBufferLayout layout);
	TexelCopyBufferInfo& setBuffer(Buffer buffer);
END

STRUCT(TexelCopyTextureInfo)
	TexelCopyTextureInfo& setDefault();
	TexelCopyTextureInfo& setTexture(Texture texture);
	TexelCopyTextureInfo& setMipLevel(uint32_t mipLevel);
	TexelCopyTextureInfo& setOrigin(Origin3D origin);
	TexelCopyTextureInfo& setAspect(TextureAspect aspect);
END

STRUCT(VertexBufferLayout)
	VertexBufferLayout& setDefault();
	VertexBufferLayout& setStepMode(VertexStepMode stepMode);
	VertexBufferLayout& setArrayStride(uint64_t arrayStride);
	VertexBufferLayout& setAttributes(uint32_t attributeCount, const VertexAttribute * attributes);
	VertexBufferLayout& setAttributes(const std::vector<VertexAttribute>& attributes);
	VertexBufferLayout& setAttributes(const std::span<const VertexAttribute>& attributes);
END

STRUCT(InstanceExtras)
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
	DeviceExtras& setDefault();
	DeviceExtras& setTracePath(StringView tracePath);
END

STRUCT(NativeLimits)
	NativeLimits& setDefault();
	NativeLimits& setMaxPushConstantSize(uint32_t maxPushConstantSize);
	NativeLimits& setMaxNonSamplerBindings(uint32_t maxNonSamplerBindings);
END

STRUCT(PushConstantRange)
	PushConstantRange& setDefault();
	PushConstantRange& setStages(ShaderStage stages);
	PushConstantRange& setStart(uint32_t start);
	PushConstantRange& setEnd(uint32_t end);
END

STRUCT(PipelineLayoutExtras)
	PipelineLayoutExtras& setDefault();
	PipelineLayoutExtras& setPushConstantRanges(uint32_t pushConstantRangeCount, const PushConstantRange * pushConstantRanges);
	PipelineLayoutExtras& setPushConstantRanges(const std::vector<PushConstantRange>& pushConstantRanges);
	PipelineLayoutExtras& setPushConstantRanges(const std::span<const PushConstantRange>& pushConstantRanges);
END

STRUCT(ShaderDefine)
	ShaderDefine& setDefault();
	ShaderDefine& setName(StringView name);
	ShaderDefine& setValue(StringView value);
END

STRUCT(ShaderSourceGLSL)
	ShaderSourceGLSL& setDefault();
	ShaderSourceGLSL& setStage(ShaderStage stage);
	ShaderSourceGLSL& setCode(StringView code);
	ShaderSourceGLSL& setDefines(uint32_t defineCount, ShaderDefine * defines);
	ShaderSourceGLSL& setDefines(std::vector<ShaderDefine>& defines);
	ShaderSourceGLSL& setDefines(const std::span<ShaderDefine>& defines);
END

STRUCT(ShaderModuleDescriptorSpirV)
	ShaderModuleDescriptorSpirV& setDefault();
	ShaderModuleDescriptorSpirV& setLabel(StringView label);
	ShaderModuleDescriptorSpirV& setSourceSize(uint32_t sourceSize);
	ShaderModuleDescriptorSpirV& setSource(const uint32_t * source);
END

STRUCT(RegistryReport)
	RegistryReport& setDefault();
	RegistryReport& setNumAllocated(size_t numAllocated);
	RegistryReport& setNumKeptFromUser(size_t numKeptFromUser);
	RegistryReport& setNumReleasedFromUser(size_t numReleasedFromUser);
	RegistryReport& setElementSize(size_t elementSize);
END

STRUCT(HubReport)
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
	GlobalReport& setDefault();
	GlobalReport& setSurfaces(RegistryReport surfaces);
	GlobalReport& setHub(HubReport hub);
END

STRUCT(BindGroupEntryExtras)
	BindGroupEntryExtras& setDefault();
	BindGroupEntryExtras& setBuffers(uint32_t bufferCount, const Buffer * buffers);
	BindGroupEntryExtras& setBuffers(const std::vector<Buffer>& buffers);
	BindGroupEntryExtras& setBuffers(const std::span<const Buffer>& buffers);
	BindGroupEntryExtras& setSamplers(uint32_t samplerCount, const Sampler * samplers);
	BindGroupEntryExtras& setSamplers(const std::vector<Sampler>& samplers);
	BindGroupEntryExtras& setSamplers(const std::span<const Sampler>& samplers);
	BindGroupEntryExtras& setTextureViews(uint32_t textureViewCount, const TextureView * textureViews);
	BindGroupEntryExtras& setTextureViews(const std::vector<TextureView>& textureViews);
	BindGroupEntryExtras& setTextureViews(const std::span<const TextureView>& textureViews);
END

STRUCT(BindGroupLayoutEntryExtras)
	BindGroupLayoutEntryExtras& setDefault();
	BindGroupLayoutEntryExtras& setCount(uint32_t count);
END

STRUCT(QuerySetDescriptorExtras)
	QuerySetDescriptorExtras& setDefault();
	QuerySetDescriptorExtras& setPipelineStatistics(uint32_t pipelineStatisticCount, const PipelineStatisticName * pipelineStatistics);
	QuerySetDescriptorExtras& setPipelineStatistics(const std::vector<PipelineStatisticName>& pipelineStatistics);
	QuerySetDescriptorExtras& setPipelineStatistics(const std::span<const PipelineStatisticName>& pipelineStatistics);
END

STRUCT(SurfaceConfigurationExtras)
	SurfaceConfigurationExtras& setDefault();
	SurfaceConfigurationExtras& setDesiredMaximumFrameLatency(uint32_t desiredMaximumFrameLatency);
END

STRUCT(SurfaceSourceSwapChainPanel)
	SurfaceSourceSwapChainPanel& setDefault();
	SurfaceSourceSwapChainPanel& setPanelNative(void * panelNative);
END

STRUCT(PrimitiveStateExtras)
	PrimitiveStateExtras& setDefault();
	PrimitiveStateExtras& setPolygonMode(PolygonMode polygonMode);
	PrimitiveStateExtras& setConservative(WGPUBool conservative);
END


// Descriptors
DESCRIPTOR(BufferMapCallbackInfo)
	BufferMapCallbackInfo& setDefault();
	BufferMapCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	BufferMapCallbackInfo& setMode(CallbackMode mode);
	BufferMapCallbackInfo& setCallback(WGPUBufferMapCallback callback);
	BufferMapCallbackInfo& setUserdata1(void * userdata1);
	BufferMapCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(CompilationInfoCallbackInfo)
	CompilationInfoCallbackInfo& setDefault();
	CompilationInfoCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	CompilationInfoCallbackInfo& setMode(CallbackMode mode);
	CompilationInfoCallbackInfo& setCallback(WGPUCompilationInfoCallback callback);
	CompilationInfoCallbackInfo& setUserdata1(void * userdata1);
	CompilationInfoCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(CreateComputePipelineAsyncCallbackInfo)
	CreateComputePipelineAsyncCallbackInfo& setDefault();
	CreateComputePipelineAsyncCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	CreateComputePipelineAsyncCallbackInfo& setMode(CallbackMode mode);
	CreateComputePipelineAsyncCallbackInfo& setCallback(WGPUCreateComputePipelineAsyncCallback callback);
	CreateComputePipelineAsyncCallbackInfo& setUserdata1(void * userdata1);
	CreateComputePipelineAsyncCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(CreateRenderPipelineAsyncCallbackInfo)
	CreateRenderPipelineAsyncCallbackInfo& setDefault();
	CreateRenderPipelineAsyncCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	CreateRenderPipelineAsyncCallbackInfo& setMode(CallbackMode mode);
	CreateRenderPipelineAsyncCallbackInfo& setCallback(WGPUCreateRenderPipelineAsyncCallback callback);
	CreateRenderPipelineAsyncCallbackInfo& setUserdata1(void * userdata1);
	CreateRenderPipelineAsyncCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(DeviceLostCallbackInfo)
	DeviceLostCallbackInfo& setDefault();
	DeviceLostCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	DeviceLostCallbackInfo& setMode(CallbackMode mode);
	DeviceLostCallbackInfo& setCallback(WGPUDeviceLostCallback callback);
	DeviceLostCallbackInfo& setUserdata1(void * userdata1);
	DeviceLostCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(PopErrorScopeCallbackInfo)
	PopErrorScopeCallbackInfo& setDefault();
	PopErrorScopeCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	PopErrorScopeCallbackInfo& setMode(CallbackMode mode);
	PopErrorScopeCallbackInfo& setCallback(WGPUPopErrorScopeCallback callback);
	PopErrorScopeCallbackInfo& setUserdata1(void * userdata1);
	PopErrorScopeCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(QueueWorkDoneCallbackInfo)
	QueueWorkDoneCallbackInfo& setDefault();
	QueueWorkDoneCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	QueueWorkDoneCallbackInfo& setMode(CallbackMode mode);
	QueueWorkDoneCallbackInfo& setCallback(WGPUQueueWorkDoneCallback callback);
	QueueWorkDoneCallbackInfo& setUserdata1(void * userdata1);
	QueueWorkDoneCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(RequestAdapterCallbackInfo)
	RequestAdapterCallbackInfo& setDefault();
	RequestAdapterCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	RequestAdapterCallbackInfo& setMode(CallbackMode mode);
	RequestAdapterCallbackInfo& setCallback(WGPURequestAdapterCallback callback);
	RequestAdapterCallbackInfo& setUserdata1(void * userdata1);
	RequestAdapterCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(RequestDeviceCallbackInfo)
	RequestDeviceCallbackInfo& setDefault();
	RequestDeviceCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	RequestDeviceCallbackInfo& setMode(CallbackMode mode);
	RequestDeviceCallbackInfo& setCallback(WGPURequestDeviceCallback callback);
	RequestDeviceCallbackInfo& setUserdata1(void * userdata1);
	RequestDeviceCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(UncapturedErrorCallbackInfo)
	UncapturedErrorCallbackInfo& setDefault();
	UncapturedErrorCallbackInfo& setNextInChain(const ChainedStruct * nextInChain);
	UncapturedErrorCallbackInfo& setCallback(WGPUUncapturedErrorCallback callback);
	UncapturedErrorCallbackInfo& setUserdata1(void * userdata1);
	UncapturedErrorCallbackInfo& setUserdata2(void * userdata2);
END

DESCRIPTOR(AdapterInfo)
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
	BindGroupEntry& setDefault();
	BindGroupEntry& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupEntry& setBinding(uint32_t binding);
	BindGroupEntry& setBuffer(Buffer buffer);
	BindGroupEntry& setOffset(uint64_t offset);
	BindGroupEntry& setSize(uint64_t size);
	BindGroupEntry& setSampler(Sampler sampler);
	BindGroupEntry& setTextureView(TextureView textureView);
END

DESCRIPTOR(BufferBindingLayout)
	BufferBindingLayout& setDefault();
	BufferBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	BufferBindingLayout& setType(BufferBindingType type);
	BufferBindingLayout& setHasDynamicOffset(WGPUBool hasDynamicOffset);
	BufferBindingLayout& setMinBindingSize(uint64_t minBindingSize);
END

DESCRIPTOR(BufferDescriptor)
	BufferDescriptor& setDefault();
	BufferDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	BufferDescriptor& setLabel(StringView label);
	BufferDescriptor& setUsage(BufferUsage usage);
	BufferDescriptor& setSize(uint64_t size);
	BufferDescriptor& setMappedAtCreation(WGPUBool mappedAtCreation);
END

DESCRIPTOR(CommandBufferDescriptor)
	CommandBufferDescriptor& setDefault();
	CommandBufferDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	CommandBufferDescriptor& setLabel(StringView label);
END

DESCRIPTOR(CommandEncoderDescriptor)
	CommandEncoderDescriptor& setDefault();
	CommandEncoderDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	CommandEncoderDescriptor& setLabel(StringView label);
END

DESCRIPTOR(CompilationMessage)
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
	ConstantEntry& setDefault();
	ConstantEntry& setNextInChain(const ChainedStruct * nextInChain);
	ConstantEntry& setKey(StringView key);
	ConstantEntry& setValue(double value);
END

DESCRIPTOR(InstanceCapabilities)
	InstanceCapabilities& setDefault();
	InstanceCapabilities& setNextInChain(ChainedStructOut * nextInChain);
	InstanceCapabilities& setTimedWaitAnyEnable(WGPUBool timedWaitAnyEnable);
	InstanceCapabilities& setTimedWaitAnyMaxCount(size_t timedWaitAnyMaxCount);
END

DESCRIPTOR(Limits)
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
	MultisampleState& setDefault();
	MultisampleState& setNextInChain(const ChainedStruct * nextInChain);
	MultisampleState& setCount(uint32_t count);
	MultisampleState& setMask(uint32_t mask);
	MultisampleState& setAlphaToCoverageEnabled(WGPUBool alphaToCoverageEnabled);
END

DESCRIPTOR(PipelineLayoutDescriptor)
	PipelineLayoutDescriptor& setDefault();
	PipelineLayoutDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	PipelineLayoutDescriptor& setLabel(StringView label);
	PipelineLayoutDescriptor& setBindGroupLayouts(uint32_t bindGroupLayoutCount, const BindGroupLayout * bindGroupLayouts);
	PipelineLayoutDescriptor& setBindGroupLayouts(const std::vector<BindGroupLayout>& bindGroupLayouts);
	PipelineLayoutDescriptor& setBindGroupLayouts(const std::span<const BindGroupLayout>& bindGroupLayouts);
END

DESCRIPTOR(PrimitiveState)
	PrimitiveState& setDefault();
	PrimitiveState& setNextInChain(const ChainedStruct * nextInChain);
	PrimitiveState& setTopology(PrimitiveTopology topology);
	PrimitiveState& setStripIndexFormat(IndexFormat stripIndexFormat);
	PrimitiveState& setFrontFace(FrontFace frontFace);
	PrimitiveState& setCullMode(CullMode cullMode);
	PrimitiveState& setUnclippedDepth(WGPUBool unclippedDepth);
END

DESCRIPTOR(QuerySetDescriptor)
	QuerySetDescriptor& setDefault();
	QuerySetDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	QuerySetDescriptor& setLabel(StringView label);
	QuerySetDescriptor& setType(QueryType type);
	QuerySetDescriptor& setCount(uint32_t count);
END

DESCRIPTOR(QueueDescriptor)
	QueueDescriptor& setDefault();
	QueueDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	QueueDescriptor& setLabel(StringView label);
END

DESCRIPTOR(RenderBundleDescriptor)
	RenderBundleDescriptor& setDefault();
	RenderBundleDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderBundleDescriptor& setLabel(StringView label);
END

DESCRIPTOR(RenderBundleEncoderDescriptor)
	RenderBundleEncoderDescriptor& setDefault();
	RenderBundleEncoderDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderBundleEncoderDescriptor& setLabel(StringView label);
	RenderBundleEncoderDescriptor& setColorFormats(uint32_t colorFormatCount, const TextureFormat * colorFormats);
	RenderBundleEncoderDescriptor& setColorFormats(const std::vector<TextureFormat>& colorFormats);
	RenderBundleEncoderDescriptor& setColorFormats(const std::span<const TextureFormat>& colorFormats);
	RenderBundleEncoderDescriptor& setDepthStencilFormat(TextureFormat depthStencilFormat);
	RenderBundleEncoderDescriptor& setDepthReadOnly(WGPUBool depthReadOnly);
	RenderBundleEncoderDescriptor& setStencilReadOnly(WGPUBool stencilReadOnly);
	RenderBundleEncoderDescriptor& setSampleCount(uint32_t sampleCount);
END

DESCRIPTOR(RequestAdapterOptions)
	RequestAdapterOptions& setDefault();
	RequestAdapterOptions& setNextInChain(const ChainedStruct * nextInChain);
	RequestAdapterOptions& setFeatureLevel(FeatureLevel featureLevel);
	RequestAdapterOptions& setPowerPreference(PowerPreference powerPreference);
	RequestAdapterOptions& setForceFallbackAdapter(WGPUBool forceFallbackAdapter);
	RequestAdapterOptions& setBackendType(BackendType backendType);
	RequestAdapterOptions& setCompatibleSurface(Surface compatibleSurface);
END

DESCRIPTOR(SamplerBindingLayout)
	SamplerBindingLayout& setDefault();
	SamplerBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	SamplerBindingLayout& setType(SamplerBindingType type);
END

DESCRIPTOR(SamplerDescriptor)
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
	ShaderModuleDescriptor& setDefault();
	ShaderModuleDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ShaderModuleDescriptor& setLabel(StringView label);
END

DESCRIPTOR(StorageTextureBindingLayout)
	StorageTextureBindingLayout& setDefault();
	StorageTextureBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	StorageTextureBindingLayout& setAccess(StorageTextureAccess access);
	StorageTextureBindingLayout& setFormat(TextureFormat format);
	StorageTextureBindingLayout& setViewDimension(TextureViewDimension viewDimension);
END

DESCRIPTOR(SurfaceCapabilities)
	SurfaceCapabilities& setDefault();
	SurfaceCapabilities& setNextInChain(ChainedStructOut * nextInChain);
	SurfaceCapabilities& setUsages(TextureUsage usages);
	SurfaceCapabilities& setFormats(uint32_t formatCount, const TextureFormat * formats);
	SurfaceCapabilities& setFormats(const std::vector<TextureFormat>& formats);
	SurfaceCapabilities& setFormats(const std::span<const TextureFormat>& formats);
	SurfaceCapabilities& setPresentModes(uint32_t presentModeCount, const PresentMode * presentModes);
	SurfaceCapabilities& setPresentModes(const std::vector<PresentMode>& presentModes);
	SurfaceCapabilities& setPresentModes(const std::span<const PresentMode>& presentModes);
	SurfaceCapabilities& setAlphaModes(uint32_t alphaModeCount, const CompositeAlphaMode * alphaModes);
	SurfaceCapabilities& setAlphaModes(const std::vector<CompositeAlphaMode>& alphaModes);
	SurfaceCapabilities& setAlphaModes(const std::span<const CompositeAlphaMode>& alphaModes);
	void freeMembers();
END

DESCRIPTOR(SurfaceConfiguration)
	SurfaceConfiguration& setDefault();
	SurfaceConfiguration& setNextInChain(const ChainedStruct * nextInChain);
	SurfaceConfiguration& setDevice(Device device);
	SurfaceConfiguration& setFormat(TextureFormat format);
	SurfaceConfiguration& setUsage(TextureUsage usage);
	SurfaceConfiguration& setWidth(uint32_t width);
	SurfaceConfiguration& setHeight(uint32_t height);
	SurfaceConfiguration& setViewFormats(uint32_t viewFormatCount, const TextureFormat * viewFormats);
	SurfaceConfiguration& setViewFormats(const std::vector<TextureFormat>& viewFormats);
	SurfaceConfiguration& setViewFormats(const std::span<const TextureFormat>& viewFormats);
	SurfaceConfiguration& setAlphaMode(CompositeAlphaMode alphaMode);
	SurfaceConfiguration& setPresentMode(PresentMode presentMode);
END

DESCRIPTOR(SurfaceDescriptor)
	SurfaceDescriptor& setDefault();
	SurfaceDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	SurfaceDescriptor& setLabel(StringView label);
END

DESCRIPTOR(SurfaceTexture)
	SurfaceTexture& setDefault();
	SurfaceTexture& setNextInChain(ChainedStructOut * nextInChain);
	SurfaceTexture& setTexture(Texture texture);
	SurfaceTexture& setStatus(SurfaceGetCurrentTextureStatus status);
END

DESCRIPTOR(TextureBindingLayout)
	TextureBindingLayout& setDefault();
	TextureBindingLayout& setNextInChain(const ChainedStruct * nextInChain);
	TextureBindingLayout& setSampleType(TextureSampleType sampleType);
	TextureBindingLayout& setViewDimension(TextureViewDimension viewDimension);
	TextureBindingLayout& setMultisampled(WGPUBool multisampled);
END

DESCRIPTOR(TextureViewDescriptor)
	TextureViewDescriptor& setDefault();
	TextureViewDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	TextureViewDescriptor& setLabel(StringView label);
	TextureViewDescriptor& setFormat(TextureFormat format);
	TextureViewDescriptor& setDimension(TextureViewDimension dimension);
	TextureViewDescriptor& setBaseMipLevel(uint32_t baseMipLevel);
	TextureViewDescriptor& setBaseArrayLayer(uint32_t baseArrayLayer);
	TextureViewDescriptor& setAspect(TextureAspect aspect);
	TextureViewDescriptor& setUsage(TextureUsage usage);
	TextureViewDescriptor& setMipLevelCount(uint32_t mipLevelCount);
	TextureViewDescriptor& setArrayLayerCount(uint32_t arrayLayerCount);
END

DESCRIPTOR(BindGroupDescriptor)
	BindGroupDescriptor& setDefault();
	BindGroupDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupDescriptor& setLabel(StringView label);
	BindGroupDescriptor& setLayout(BindGroupLayout layout);
	BindGroupDescriptor& setEntries(uint32_t entryCount, const BindGroupEntry * entries);
	BindGroupDescriptor& setEntries(const std::vector<BindGroupEntry>& entries);
	BindGroupDescriptor& setEntries(const std::span<const BindGroupEntry>& entries);
END

DESCRIPTOR(BindGroupLayoutEntry)
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
	CompilationInfo& setDefault();
	CompilationInfo& setNextInChain(const ChainedStruct * nextInChain);
	CompilationInfo& setMessages(uint32_t messageCount, const CompilationMessage * messages);
	CompilationInfo& setMessages(const std::vector<CompilationMessage>& messages);
	CompilationInfo& setMessages(const std::span<const CompilationMessage>& messages);
END

DESCRIPTOR(ComputePassDescriptor)
	ComputePassDescriptor& setDefault();
	ComputePassDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ComputePassDescriptor& setLabel(StringView label);
	ComputePassDescriptor& setTimestampWrites(const ComputePassTimestampWrites * timestampWrites);
END

DESCRIPTOR(DepthStencilState)
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
	DeviceDescriptor& setDefault();
	DeviceDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	DeviceDescriptor& setLabel(StringView label);
	DeviceDescriptor& setRequiredFeatures(uint32_t requiredFeatureCount, const FeatureName * requiredFeatures);
	DeviceDescriptor& setRequiredFeatures(const std::vector<FeatureName>& requiredFeatures);
	DeviceDescriptor& setRequiredFeatures(const std::span<const FeatureName>& requiredFeatures);
	DeviceDescriptor& setRequiredLimits(const Limits * requiredLimits);
	DeviceDescriptor& setDefaultQueue(QueueDescriptor defaultQueue);
	DeviceDescriptor& setDeviceLostCallbackInfo(DeviceLostCallbackInfo deviceLostCallbackInfo);
	DeviceDescriptor& setUncapturedErrorCallbackInfo(UncapturedErrorCallbackInfo uncapturedErrorCallbackInfo);
END

DESCRIPTOR(InstanceDescriptor)
	InstanceDescriptor& setDefault();
	InstanceDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	InstanceDescriptor& setFeatures(InstanceCapabilities features);
END

DESCRIPTOR(ProgrammableStageDescriptor)
	ProgrammableStageDescriptor& setDefault();
	ProgrammableStageDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ProgrammableStageDescriptor& setModule(ShaderModule module);
	ProgrammableStageDescriptor& setEntryPoint(StringView entryPoint);
	ProgrammableStageDescriptor& setConstants(uint32_t constantCount, const ConstantEntry * constants);
	ProgrammableStageDescriptor& setConstants(const std::vector<ConstantEntry>& constants);
	ProgrammableStageDescriptor& setConstants(const std::span<const ConstantEntry>& constants);
END

DESCRIPTOR(RenderPassColorAttachment)
	RenderPassColorAttachment& setDefault();
	RenderPassColorAttachment& setNextInChain(const ChainedStruct * nextInChain);
	RenderPassColorAttachment& setView(TextureView view);
	RenderPassColorAttachment& setDepthSlice(uint32_t depthSlice);
	RenderPassColorAttachment& setResolveTarget(TextureView resolveTarget);
	RenderPassColorAttachment& setLoadOp(LoadOp loadOp);
	RenderPassColorAttachment& setStoreOp(StoreOp storeOp);
	RenderPassColorAttachment& setClearValue(Color clearValue);
END

DESCRIPTOR(TextureDescriptor)
	TextureDescriptor& setDefault();
	TextureDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	TextureDescriptor& setLabel(StringView label);
	TextureDescriptor& setUsage(TextureUsage usage);
	TextureDescriptor& setDimension(TextureDimension dimension);
	TextureDescriptor& setSize(Extent3D size);
	TextureDescriptor& setFormat(TextureFormat format);
	TextureDescriptor& setViewFormats(uint32_t viewFormatCount, const TextureFormat * viewFormats);
	TextureDescriptor& setViewFormats(const std::vector<TextureFormat>& viewFormats);
	TextureDescriptor& setViewFormats(const std::span<const TextureFormat>& viewFormats);
	TextureDescriptor& setMipLevelCount(uint32_t mipLevelCount);
	TextureDescriptor& setSampleCount(uint32_t sampleCount);
END

DESCRIPTOR(BindGroupLayoutDescriptor)
	BindGroupLayoutDescriptor& setDefault();
	BindGroupLayoutDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	BindGroupLayoutDescriptor& setLabel(StringView label);
	BindGroupLayoutDescriptor& setEntries(uint32_t entryCount, const BindGroupLayoutEntry * entries);
	BindGroupLayoutDescriptor& setEntries(const std::vector<BindGroupLayoutEntry>& entries);
	BindGroupLayoutDescriptor& setEntries(const std::span<const BindGroupLayoutEntry>& entries);
END

DESCRIPTOR(ColorTargetState)
	ColorTargetState& setDefault();
	ColorTargetState& setNextInChain(const ChainedStruct * nextInChain);
	ColorTargetState& setFormat(TextureFormat format);
	ColorTargetState& setBlend(const BlendState * blend);
	ColorTargetState& setWriteMask(ColorWriteMask writeMask);
END

DESCRIPTOR(ComputePipelineDescriptor)
	ComputePipelineDescriptor& setDefault();
	ComputePipelineDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	ComputePipelineDescriptor& setLabel(StringView label);
	ComputePipelineDescriptor& setLayout(PipelineLayout layout);
	ComputePipelineDescriptor& setCompute(ProgrammableStageDescriptor compute);
END

DESCRIPTOR(RenderPassDescriptor)
	RenderPassDescriptor& setDefault();
	RenderPassDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderPassDescriptor& setLabel(StringView label);
	RenderPassDescriptor& setColorAttachments(uint32_t colorAttachmentCount, const RenderPassColorAttachment * colorAttachments);
	RenderPassDescriptor& setColorAttachments(const std::vector<RenderPassColorAttachment>& colorAttachments);
	RenderPassDescriptor& setColorAttachments(const std::span<const RenderPassColorAttachment>& colorAttachments);
	RenderPassDescriptor& setDepthStencilAttachment(const RenderPassDepthStencilAttachment * depthStencilAttachment);
	RenderPassDescriptor& setOcclusionQuerySet(QuerySet occlusionQuerySet);
	RenderPassDescriptor& setTimestampWrites(const RenderPassTimestampWrites * timestampWrites);
END

DESCRIPTOR(VertexState)
	VertexState& setDefault();
	VertexState& setNextInChain(const ChainedStruct * nextInChain);
	VertexState& setModule(ShaderModule module);
	VertexState& setEntryPoint(StringView entryPoint);
	VertexState& setConstants(uint32_t constantCount, const ConstantEntry * constants);
	VertexState& setConstants(const std::vector<ConstantEntry>& constants);
	VertexState& setConstants(const std::span<const ConstantEntry>& constants);
	VertexState& setBuffers(uint32_t bufferCount, const VertexBufferLayout * buffers);
	VertexState& setBuffers(const std::vector<VertexBufferLayout>& buffers);
	VertexState& setBuffers(const std::span<const VertexBufferLayout>& buffers);
END

DESCRIPTOR(FragmentState)
	FragmentState& setDefault();
	FragmentState& setNextInChain(const ChainedStruct * nextInChain);
	FragmentState& setModule(ShaderModule module);
	FragmentState& setEntryPoint(StringView entryPoint);
	FragmentState& setConstants(uint32_t constantCount, const ConstantEntry * constants);
	FragmentState& setConstants(const std::vector<ConstantEntry>& constants);
	FragmentState& setConstants(const std::span<const ConstantEntry>& constants);
	FragmentState& setTargets(uint32_t targetCount, const ColorTargetState * targets);
	FragmentState& setTargets(const std::vector<ColorTargetState>& targets);
	FragmentState& setTargets(const std::span<const ColorTargetState>& targets);
END

DESCRIPTOR(RenderPipelineDescriptor)
	RenderPipelineDescriptor& setDefault();
	RenderPipelineDescriptor& setNextInChain(const ChainedStruct * nextInChain);
	RenderPipelineDescriptor& setLabel(StringView label);
	RenderPipelineDescriptor& setLayout(PipelineLayout layout);
	RenderPipelineDescriptor& setVertex(VertexState vertex);
	RenderPipelineDescriptor& setPrimitive(PrimitiveState primitive);
	RenderPipelineDescriptor& setDepthStencil(const DepthStencilState * depthStencil);
	RenderPipelineDescriptor& setMultisample(MultisampleState multisample);
	RenderPipelineDescriptor& setFragment(const FragmentState * fragment);
END

DESCRIPTOR(InstanceEnumerateAdapterOptions)
	InstanceEnumerateAdapterOptions& setDefault();
	InstanceEnumerateAdapterOptions& setNextInChain(const ChainedStruct * nextInChain);
	InstanceEnumerateAdapterOptions& setBackends(InstanceBackend backends);
END


// Callback types
using BufferMapCallback = std::function<void(MapAsyncStatus status, StringView message, void* userdata1)>;
using CompilationInfoCallback = std::function<void(CompilationInfoRequestStatus status, const CompilationInfo& compilationInfo, void* userdata1)>;
using CreateComputePipelineAsyncCallback = std::function<void(CreatePipelineAsyncStatus status, ComputePipeline pipeline, StringView message, void* userdata1)>;
using CreateRenderPipelineAsyncCallback = std::function<void(CreatePipelineAsyncStatus status, RenderPipeline pipeline, StringView message, void* userdata1)>;
using DeviceLostCallback = std::function<void(Device const * device, DeviceLostReason reason, StringView message, void* userdata1)>;
using PopErrorScopeCallback = std::function<void(PopErrorScopeStatus status, ErrorType type, StringView message, void* userdata1)>;
using QueueWorkDoneCallback = std::function<void(QueueWorkDoneStatus status, void* userdata1)>;
using RequestAdapterCallback = std::function<void(RequestAdapterStatus status, Adapter adapter, StringView message, void* userdata1)>;
using RequestDeviceCallback = std::function<void(RequestDeviceStatus status, Device device, StringView message, void* userdata1)>;
using UncapturedErrorCallback = std::function<void(Device const * device, ErrorType type, StringView message, void* userdata1)>;
using LogCallback = std::function<void(LogLevel level, StringView message)>;

// RAII Handles
HANDLE(Adapter)
	void getFeatures(SupportedFeatures * features) const;
	Status getInfo(AdapterInfo * info) const;
	Status getLimits(Limits * limits) const;
	Bool hasFeature(FeatureName feature) const;
	template<typename Lambda>
	Future requestDevice(const DeviceDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<RequestDeviceStatus>(status), device, message);
		};
		WGPURequestDeviceCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuAdapterRequestDevice(m_raw, &descriptor, callbackInfo);
	}
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
	template<typename Lambda>
	Future mapAsync(MapMode mode, size_t offset, size_t size, CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<MapAsyncStatus>(status), message);
		};
		WGPUBufferMapCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuBufferMapAsync(m_raw, static_cast<WGPUMapMode>(mode), offset, size, callbackInfo);
	}
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
	ComputePassEncoder beginComputePass(const ComputePassDescriptor& descriptor) const;
	ComputePassEncoder beginComputePass() const;
	RenderPassEncoder beginRenderPass(const RenderPassDescriptor& descriptor) const;
	void clearBuffer(Buffer buffer, uint64_t offset, uint64_t size) const;
	void copyBufferToBuffer(Buffer source, uint64_t sourceOffset, Buffer destination, uint64_t destinationOffset, uint64_t size) const;
	void copyBufferToTexture(const TexelCopyBufferInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const;
	void copyTextureToBuffer(const TexelCopyTextureInfo& source, const TexelCopyBufferInfo& destination, const Extent3D& copySize) const;
	void copyTextureToTexture(const TexelCopyTextureInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const;
	CommandBuffer finish(const CommandBufferDescriptor& descriptor) const;
	CommandBuffer finish() const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, Buffer destination, uint64_t destinationOffset) const;
	void setLabel(StringView label) const;
	void writeTimestamp(QuerySet querySet, uint32_t queryIndex) const;
	void addRef() const;
	void release() const;
END

HANDLE(ComputePassEncoder)
	void dispatchWorkgroups(uint32_t workgroupCountX, uint32_t workgroupCountY, uint32_t workgroupCountZ) const;
	void dispatchWorkgroupsIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const;
	void end() const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, const uint32_t& dynamicOffsets) const;
	void setLabel(StringView label) const;
	void setPipeline(ComputePipeline pipeline) const;
	void addRef() const;
	void release() const;
	void setPushConstants(uint32_t offset, uint32_t sizeBytes, void const * data) const;
	void beginPipelineStatisticsQuery(QuerySet querySet, uint32_t queryIndex) const;
	void endPipelineStatisticsQuery() const;
	void writeTimestamp(QuerySet querySet, uint32_t queryIndex) const;
END

HANDLE(ComputePipeline)
	BindGroupLayout getBindGroupLayout(uint32_t groupIndex) const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Device)
	BindGroup createBindGroup(const BindGroupDescriptor& descriptor) const;
	BindGroupLayout createBindGroupLayout(const BindGroupLayoutDescriptor& descriptor) const;
	Buffer createBuffer(const BufferDescriptor& descriptor) const;
	CommandEncoder createCommandEncoder(const CommandEncoderDescriptor& descriptor) const;
	CommandEncoder createCommandEncoder() const;
	ComputePipeline createComputePipeline(const ComputePipelineDescriptor& descriptor) const;
	template<typename Lambda>
	Future createComputePipelineAsync(const ComputePipelineDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline, WGPUStringView message, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<CreatePipelineAsyncStatus>(status), pipeline, message);
		};
		WGPUCreateComputePipelineAsyncCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuDeviceCreateComputePipelineAsync(m_raw, &descriptor, callbackInfo);
	}
	PipelineLayout createPipelineLayout(const PipelineLayoutDescriptor& descriptor) const;
	QuerySet createQuerySet(const QuerySetDescriptor& descriptor) const;
	RenderBundleEncoder createRenderBundleEncoder(const RenderBundleEncoderDescriptor& descriptor) const;
	RenderPipeline createRenderPipeline(const RenderPipelineDescriptor& descriptor) const;
	template<typename Lambda>
	Future createRenderPipelineAsync(const RenderPipelineDescriptor& descriptor, CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline, WGPUStringView message, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<CreatePipelineAsyncStatus>(status), pipeline, message);
		};
		WGPUCreateRenderPipelineAsyncCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuDeviceCreateRenderPipelineAsync(m_raw, &descriptor, callbackInfo);
	}
	Sampler createSampler(const SamplerDescriptor& descriptor) const;
	Sampler createSampler() const;
	ShaderModule createShaderModule(const ShaderModuleDescriptor& descriptor) const;
	Texture createTexture(const TextureDescriptor& descriptor) const;
	void destroy() const;
	AdapterInfo getAdapterInfo() const;
	void getFeatures(SupportedFeatures * features) const;
	Status getLimits(Limits * limits) const;
	Queue getQueue() const;
	Bool hasFeature(FeatureName feature) const;
	template<typename Lambda>
	Future popErrorScope(CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<PopErrorScopeStatus>(status), static_cast<ErrorType>(type), message);
		};
		WGPUPopErrorScopeCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuDevicePopErrorScope(m_raw, callbackInfo);
	}
	void pushErrorScope(ErrorFilter filter) const;
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
	Bool poll(Bool wait, SubmissionIndex const * submissionIndex) const;
	ShaderModule createShaderModuleSpirV(const ShaderModuleDescriptorSpirV& descriptor) const;
END

HANDLE(Instance)
	Surface createSurface(const SurfaceDescriptor& descriptor) const;
	Status getWGSLLanguageFeatures(SupportedWGSLLanguageFeatures * features) const;
	Bool hasWGSLLanguageFeature(WGSLLanguageFeatureName feature) const;
	void processEvents() const;
	template<typename Lambda>
	Future requestAdapter(const RequestAdapterOptions& options, CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<RequestAdapterStatus>(status), adapter, message);
		};
		WGPURequestAdapterCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuInstanceRequestAdapter(m_raw, &options, callbackInfo);
	}
	WaitStatus waitAny(size_t futureCount, FutureWaitInfo * futures, uint64_t timeoutNS) const;
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
	template<typename Lambda>
	Future onSubmittedWorkDone(CallbackMode callbackMode, const Lambda& callback) const {
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
		return wgpuQueueOnSubmittedWorkDone(m_raw, callbackInfo);
	}
	void setLabel(StringView label) const;
	void submit(size_t commandCount, CommandBuffer const * commands) const;
	void submit(const std::vector<WGPUCommandBuffer>& commands) const;
	void submit(const WGPUCommandBuffer& commands) const;
	void writeBuffer(Buffer buffer, uint64_t bufferOffset, void const * data, size_t size) const;
	void writeTexture(const TexelCopyTextureInfo& destination, void const * data, size_t dataSize, const TexelCopyBufferLayout& dataLayout, const Extent3D& writeSize) const;
	void addRef() const;
	void release() const;
	SubmissionIndex submitForIndex(size_t commandCount, CommandBuffer const * commands) const;
	SubmissionIndex submitForIndex(const std::vector<WGPUCommandBuffer>& commands) const;
	SubmissionIndex submitForIndex(const WGPUCommandBuffer& commands) const;
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
	void drawIndexedIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const;
	void drawIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const;
	RenderBundle finish(const RenderBundleDescriptor& descriptor) const;
	RenderBundle finish() const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, const uint32_t& dynamicOffsets) const;
	void setIndexBuffer(Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const;
	void setLabel(StringView label) const;
	void setPipeline(RenderPipeline pipeline) const;
	void setVertexBuffer(uint32_t slot, Buffer buffer, uint64_t offset, uint64_t size) const;
	void addRef() const;
	void release() const;
	void setPushConstants(ShaderStage stages, uint32_t offset, uint32_t sizeBytes, void const * data) const;
END

HANDLE(RenderPassEncoder)
	void beginOcclusionQuery(uint32_t queryIndex) const;
	void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const;
	void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) const;
	void drawIndexedIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const;
	void drawIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const;
	void end() const;
	void endOcclusionQuery() const;
	void executeBundles(size_t bundleCount, RenderBundle const * bundles) const;
	void executeBundles(const std::vector<WGPURenderBundle>& bundles) const;
	void executeBundles(const WGPURenderBundle& bundles) const;
	void insertDebugMarker(StringView markerLabel) const;
	void popDebugGroup() const;
	void pushDebugGroup(StringView groupLabel) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const;
	void setBindGroup(uint32_t groupIndex, BindGroup group, const uint32_t& dynamicOffsets) const;
	void setBlendConstant(const Color& color) const;
	void setIndexBuffer(Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const;
	void setLabel(StringView label) const;
	void setPipeline(RenderPipeline pipeline) const;
	void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const;
	void setStencilReference(uint32_t reference) const;
	void setVertexBuffer(uint32_t slot, Buffer buffer, uint64_t offset, uint64_t size) const;
	void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) const;
	void addRef() const;
	void release() const;
	void setPushConstants(ShaderStage stages, uint32_t offset, uint32_t sizeBytes, void const * data) const;
	void multiDrawIndirect(Buffer buffer, uint64_t offset, uint32_t count) const;
	void multiDrawIndexedIndirect(Buffer buffer, uint64_t offset, uint32_t count) const;
	void multiDrawIndirectCount(Buffer buffer, uint64_t offset, Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const;
	void multiDrawIndexedIndirectCount(Buffer buffer, uint64_t offset, Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const;
	void beginPipelineStatisticsQuery(QuerySet querySet, uint32_t queryIndex) const;
	void endPipelineStatisticsQuery() const;
	void writeTimestamp(QuerySet querySet, uint32_t queryIndex) const;
END

HANDLE(RenderPipeline)
	BindGroupLayout getBindGroupLayout(uint32_t groupIndex) const;
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
	template<typename Lambda>
	Future getCompilationInfo(CallbackMode callbackMode, const Lambda& callback) const {
		auto* lambda = new Lambda(callback);
		auto cCallback = [](WGPUCompilationInfoRequestStatus status, struct WGPUCompilationInfo const * compilationInfo, void* userdata1, void*) -> void {
			std::unique_ptr<Lambda> lambda(reinterpret_cast<Lambda*>(userdata1));
			(*lambda)(static_cast<CompilationInfoRequestStatus>(status), *reinterpret_cast<CompilationInfo const *>(compilationInfo));
		};
		WGPUCompilationInfoCallbackInfo callbackInfo = {
			/* nextInChain = */ nullptr,
			/* mode = */ callbackMode,
			/* callback = */ cCallback,
			/* userdata1 = */ (void*)lambda,
			/* userdata2 = */ nullptr,
		};
		return wgpuShaderModuleGetCompilationInfo(m_raw, callbackInfo);
	}
	void setLabel(StringView label) const;
	void addRef() const;
	void release() const;
END

HANDLE(Surface)
	void configure(const SurfaceConfiguration& config) const;
	Status getCapabilities(Adapter adapter, SurfaceCapabilities * capabilities) const;
	void getCurrentTexture(SurfaceTexture * surfaceTexture) const;
	Status present() const;
	void setLabel(StringView label) const;
	void unconfigure() const;
	void addRef() const;
	void release() const;
END

HANDLE(Texture)
	TextureView createView(const TextureViewDescriptor& descriptor) const;
	TextureView createView() const;
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


// Non-member procedures


Instance createInstance();
Instance createInstance(const InstanceDescriptor& descriptor);

}

// Implementations

namespace wgpu {

Instance createInstance() {
	return wgpuCreateInstance(nullptr);
}

Instance createInstance(const InstanceDescriptor& descriptor) {
	return wgpuCreateInstance(&descriptor);
}

StringView::operator std::string_view() const {
	return
		length == WGPU_STRLEN
		? std::string_view(data)
		: std::string_view(data, length);
}

// RAII Implementations
// Methods of StringView
StringView& StringView::setDefault() {
	*this = WGPUStringView WGPU_STRING_VIEW_INIT;
	return *this;
}
StringView& StringView::setData(const char * data) {
	this->data = data;
	return *this;
}
StringView& StringView::setLength(size_t length) {
	this->length = length;
	return *this;
}


// Methods of ChainedStruct
ChainedStruct& ChainedStruct::setDefault() {
	*this = WGPUChainedStruct {};
	return *this;
}
ChainedStruct& ChainedStruct::setNext(const struct WGPUChainedStruct * next) {
	this->next = next;
	return *this;
}
ChainedStruct& ChainedStruct::setSType(SType sType) {
	this->sType = static_cast<WGPUSType>(sType);
	return *this;
}


// Methods of ChainedStructOut
ChainedStructOut& ChainedStructOut::setDefault() {
	*this = WGPUChainedStructOut {};
	return *this;
}
ChainedStructOut& ChainedStructOut::setNext(struct WGPUChainedStructOut * next) {
	this->next = next;
	return *this;
}
ChainedStructOut& ChainedStructOut::setSType(SType sType) {
	this->sType = static_cast<WGPUSType>(sType);
	return *this;
}


// Methods of BufferMapCallbackInfo
BufferMapCallbackInfo& BufferMapCallbackInfo::setDefault() {
	*this = WGPUBufferMapCallbackInfo {};
	return *this;
}
BufferMapCallbackInfo& BufferMapCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BufferMapCallbackInfo& BufferMapCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setDefault() {
	*this = WGPUCompilationInfoCallbackInfo {};
	return *this;
}
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CompilationInfoCallbackInfo& CompilationInfoCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setDefault() {
	*this = WGPUCreateComputePipelineAsyncCallbackInfo {};
	return *this;
}
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CreateComputePipelineAsyncCallbackInfo& CreateComputePipelineAsyncCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setDefault() {
	*this = WGPUCreateRenderPipelineAsyncCallbackInfo {};
	return *this;
}
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CreateRenderPipelineAsyncCallbackInfo& CreateRenderPipelineAsyncCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setDefault() {
	*this = WGPUDeviceLostCallbackInfo {};
	return *this;
}
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
DeviceLostCallbackInfo& DeviceLostCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setDefault() {
	*this = WGPUPopErrorScopeCallbackInfo {};
	return *this;
}
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
PopErrorScopeCallbackInfo& PopErrorScopeCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setDefault() {
	*this = WGPUQueueWorkDoneCallbackInfo {};
	return *this;
}
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
QueueWorkDoneCallbackInfo& QueueWorkDoneCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setDefault() {
	*this = WGPURequestAdapterCallbackInfo {};
	return *this;
}
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RequestAdapterCallbackInfo& RequestAdapterCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setDefault() {
	*this = WGPURequestDeviceCallbackInfo {};
	return *this;
}
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RequestDeviceCallbackInfo& RequestDeviceCallbackInfo::setMode(CallbackMode mode) {
	this->mode = static_cast<WGPUCallbackMode>(mode);
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
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setDefault() {
	*this = WGPUUncapturedErrorCallbackInfo {};
	return *this;
}
UncapturedErrorCallbackInfo& UncapturedErrorCallbackInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
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
AdapterInfo& AdapterInfo::setDefault() {
    backendType = BackendType::Undefined;
    ((StringView*)&vendor)->setDefault();
    ((StringView*)&architecture)->setDefault();
    ((StringView*)&device)->setDefault();
    ((StringView*)&description)->setDefault();
	return *this;
}
AdapterInfo& AdapterInfo::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStructOut *>(nextInChain);
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
	this->backendType = static_cast<WGPUBackendType>(backendType);
	return *this;
}
AdapterInfo& AdapterInfo::setAdapterType(AdapterType adapterType) {
	this->adapterType = static_cast<WGPUAdapterType>(adapterType);
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
BindGroupEntry& BindGroupEntry::setDefault() {
	offset = 0;
	return *this;
}
BindGroupEntry& BindGroupEntry::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BindGroupEntry& BindGroupEntry::setBinding(uint32_t binding) {
	this->binding = binding;
	return *this;
}
BindGroupEntry& BindGroupEntry::setBuffer(Buffer buffer) {
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
BindGroupEntry& BindGroupEntry::setSampler(Sampler sampler) {
	this->sampler = sampler;
	return *this;
}
BindGroupEntry& BindGroupEntry::setTextureView(TextureView textureView) {
	this->textureView = textureView;
	return *this;
}


// Methods of BlendComponent
BlendComponent& BlendComponent::setDefault() {
    operation = BlendOperation::Add;
    srcFactor = BlendFactor::One;
    dstFactor = BlendFactor::Zero;
	return *this;
}
BlendComponent& BlendComponent::setOperation(BlendOperation operation) {
	this->operation = static_cast<WGPUBlendOperation>(operation);
	return *this;
}
BlendComponent& BlendComponent::setSrcFactor(BlendFactor srcFactor) {
	this->srcFactor = static_cast<WGPUBlendFactor>(srcFactor);
	return *this;
}
BlendComponent& BlendComponent::setDstFactor(BlendFactor dstFactor) {
	this->dstFactor = static_cast<WGPUBlendFactor>(dstFactor);
	return *this;
}


// Methods of BufferBindingLayout
BufferBindingLayout& BufferBindingLayout::setDefault() {
    type             = BufferBindingType::Uniform;
    hasDynamicOffset = false;
    minBindingSize   = 0;
	return *this;
}
BufferBindingLayout& BufferBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BufferBindingLayout& BufferBindingLayout::setType(BufferBindingType type) {
	this->type = static_cast<WGPUBufferBindingType>(type);
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
BufferDescriptor& BufferDescriptor::setDefault() {
    mappedAtCreation = false;
    ((StringView*)&label)->setDefault();
	return *this;
}
BufferDescriptor& BufferDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BufferDescriptor& BufferDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
BufferDescriptor& BufferDescriptor::setUsage(BufferUsage usage) {
	this->usage = static_cast<WGPUBufferUsage>(usage);
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
Color& Color::setDefault() {
	*this = WGPUColor {};
	return *this;
}
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
CommandBufferDescriptor& CommandBufferDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
CommandBufferDescriptor& CommandBufferDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CommandBufferDescriptor& CommandBufferDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of CommandEncoderDescriptor
CommandEncoderDescriptor& CommandEncoderDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
CommandEncoderDescriptor& CommandEncoderDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CommandEncoderDescriptor& CommandEncoderDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of CompilationMessage
CompilationMessage& CompilationMessage::setDefault() {
	((StringView*)&message)->setDefault();
	return *this;
}
CompilationMessage& CompilationMessage::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CompilationMessage& CompilationMessage::setMessage(StringView message) {
	this->message = message;
	return *this;
}
CompilationMessage& CompilationMessage::setType(CompilationMessageType type) {
	this->type = static_cast<WGPUCompilationMessageType>(type);
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
ComputePassTimestampWrites& ComputePassTimestampWrites::setDefault() {
	*this = WGPUComputePassTimestampWrites {};
	return *this;
}
ComputePassTimestampWrites& ComputePassTimestampWrites::setQuerySet(QuerySet querySet) {
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
ConstantEntry& ConstantEntry::setDefault() {
	((StringView*)&key)->setDefault();
	return *this;
}
ConstantEntry& ConstantEntry::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
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
Extent3D& Extent3D::setDefault() {
    height             = 1;
    depthOrArrayLayers = 1;
	return *this;
}
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
Future& Future::setDefault() {
	*this = WGPUFuture {};
	return *this;
}
Future& Future::setId(uint64_t id) {
	this->id = id;
	return *this;
}


// Methods of InstanceCapabilities
InstanceCapabilities& InstanceCapabilities::setDefault() {
	*this = WGPUInstanceCapabilities {};
	return *this;
}
InstanceCapabilities& InstanceCapabilities::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStructOut *>(nextInChain);
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
Limits& Limits::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStructOut *>(nextInChain);
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
MultisampleState& MultisampleState::setDefault() {
    count                  = 1;
    mask                   = 0xFFFFFFFF;
    alphaToCoverageEnabled = false;
	return *this;
}
MultisampleState& MultisampleState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
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
Origin3D& Origin3D::setDefault() {
    x = 0;
    y = 0;
    z = 0;
	return *this;
}
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
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(uint32_t bindGroupLayoutCount, const BindGroupLayout * bindGroupLayouts) {
	this->bindGroupLayoutCount = bindGroupLayoutCount;
	this->bindGroupLayouts = reinterpret_cast<WGPUBindGroupLayout const *>(bindGroupLayouts);
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(const std::vector<BindGroupLayout>& bindGroupLayouts) {
	this->bindGroupLayoutCount = static_cast<uint32_t>(bindGroupLayouts.size());
	this->bindGroupLayouts = reinterpret_cast<WGPUBindGroupLayout const *>(bindGroupLayouts.data());
	return *this;
}
PipelineLayoutDescriptor& PipelineLayoutDescriptor::setBindGroupLayouts(const std::span<const BindGroupLayout>& bindGroupLayouts) {
	this->bindGroupLayoutCount = static_cast<uint32_t>(bindGroupLayouts.size());
	this->bindGroupLayouts = reinterpret_cast<WGPUBindGroupLayout const *>(bindGroupLayouts.data());
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
PrimitiveState& PrimitiveState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
PrimitiveState& PrimitiveState::setTopology(PrimitiveTopology topology) {
	this->topology = static_cast<WGPUPrimitiveTopology>(topology);
	return *this;
}
PrimitiveState& PrimitiveState::setStripIndexFormat(IndexFormat stripIndexFormat) {
	this->stripIndexFormat = static_cast<WGPUIndexFormat>(stripIndexFormat);
	return *this;
}
PrimitiveState& PrimitiveState::setFrontFace(FrontFace frontFace) {
	this->frontFace = static_cast<WGPUFrontFace>(frontFace);
	return *this;
}
PrimitiveState& PrimitiveState::setCullMode(CullMode cullMode) {
	this->cullMode = static_cast<WGPUCullMode>(cullMode);
	return *this;
}
PrimitiveState& PrimitiveState::setUnclippedDepth(WGPUBool unclippedDepth) {
	this->unclippedDepth = unclippedDepth;
	return *this;
}


// Methods of QuerySetDescriptor
QuerySetDescriptor& QuerySetDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setType(QueryType type) {
	this->type = static_cast<WGPUQueryType>(type);
	return *this;
}
QuerySetDescriptor& QuerySetDescriptor::setCount(uint32_t count) {
	this->count = count;
	return *this;
}


// Methods of QueueDescriptor
QueueDescriptor& QueueDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
QueueDescriptor& QueueDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
QueueDescriptor& QueueDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of RenderBundleDescriptor
RenderBundleDescriptor& RenderBundleDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
RenderBundleDescriptor& RenderBundleDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RenderBundleDescriptor& RenderBundleDescriptor::setLabel(StringView label) {
	this->label = label;
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
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setColorFormats(uint32_t colorFormatCount, const TextureFormat * colorFormats) {
	this->colorFormatCount = colorFormatCount;
	this->colorFormats = reinterpret_cast<WGPUTextureFormat const *>(colorFormats);
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setColorFormats(const std::vector<TextureFormat>& colorFormats) {
	this->colorFormatCount = static_cast<uint32_t>(colorFormats.size());
	this->colorFormats = reinterpret_cast<WGPUTextureFormat const *>(colorFormats.data());
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setColorFormats(const std::span<const TextureFormat>& colorFormats) {
	this->colorFormatCount = static_cast<uint32_t>(colorFormats.size());
	this->colorFormats = reinterpret_cast<WGPUTextureFormat const *>(colorFormats.data());
	return *this;
}
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setDepthStencilFormat(TextureFormat depthStencilFormat) {
	this->depthStencilFormat = static_cast<WGPUTextureFormat>(depthStencilFormat);
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
RenderBundleEncoderDescriptor& RenderBundleEncoderDescriptor::setSampleCount(uint32_t sampleCount) {
	this->sampleCount = sampleCount;
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
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setView(TextureView view) {
	this->view = view;
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDepthLoadOp(LoadOp depthLoadOp) {
	this->depthLoadOp = static_cast<WGPULoadOp>(depthLoadOp);
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setDepthStoreOp(StoreOp depthStoreOp) {
	this->depthStoreOp = static_cast<WGPUStoreOp>(depthStoreOp);
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
	this->stencilLoadOp = static_cast<WGPULoadOp>(stencilLoadOp);
	return *this;
}
RenderPassDepthStencilAttachment& RenderPassDepthStencilAttachment::setStencilStoreOp(StoreOp stencilStoreOp) {
	this->stencilStoreOp = static_cast<WGPUStoreOp>(stencilStoreOp);
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
RenderPassMaxDrawCount& RenderPassMaxDrawCount::setDefault() {
    maxDrawCount = 50000000;
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::RenderPassMaxDrawCount;
    chain.next  = nullptr;
	return *this;
}
RenderPassMaxDrawCount& RenderPassMaxDrawCount::setMaxDrawCount(uint64_t maxDrawCount) {
	this->maxDrawCount = maxDrawCount;
	return *this;
}


// Methods of RenderPassTimestampWrites
RenderPassTimestampWrites& RenderPassTimestampWrites::setDefault() {
	*this = WGPURenderPassTimestampWrites {};
	return *this;
}
RenderPassTimestampWrites& RenderPassTimestampWrites::setQuerySet(QuerySet querySet) {
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
RequestAdapterOptions& RequestAdapterOptions::setDefault() {
    powerPreference      = PowerPreference::Undefined;
    forceFallbackAdapter = false;
    backendType          = BackendType::Undefined;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setFeatureLevel(FeatureLevel featureLevel) {
	this->featureLevel = static_cast<WGPUFeatureLevel>(featureLevel);
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setPowerPreference(PowerPreference powerPreference) {
	this->powerPreference = static_cast<WGPUPowerPreference>(powerPreference);
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setForceFallbackAdapter(WGPUBool forceFallbackAdapter) {
	this->forceFallbackAdapter = forceFallbackAdapter;
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setBackendType(BackendType backendType) {
	this->backendType = static_cast<WGPUBackendType>(backendType);
	return *this;
}
RequestAdapterOptions& RequestAdapterOptions::setCompatibleSurface(Surface compatibleSurface) {
	this->compatibleSurface = compatibleSurface;
	return *this;
}


// Methods of SamplerBindingLayout
SamplerBindingLayout& SamplerBindingLayout::setDefault() {
	type = SamplerBindingType::Filtering;
	return *this;
}
SamplerBindingLayout& SamplerBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
SamplerBindingLayout& SamplerBindingLayout::setType(SamplerBindingType type) {
	this->type = static_cast<WGPUSamplerBindingType>(type);
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
SamplerDescriptor& SamplerDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setAddressModeU(AddressMode addressModeU) {
	this->addressModeU = static_cast<WGPUAddressMode>(addressModeU);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setAddressModeV(AddressMode addressModeV) {
	this->addressModeV = static_cast<WGPUAddressMode>(addressModeV);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setAddressModeW(AddressMode addressModeW) {
	this->addressModeW = static_cast<WGPUAddressMode>(addressModeW);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMagFilter(FilterMode magFilter) {
	this->magFilter = static_cast<WGPUFilterMode>(magFilter);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMinFilter(FilterMode minFilter) {
	this->minFilter = static_cast<WGPUFilterMode>(minFilter);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMipmapFilter(MipmapFilterMode mipmapFilter) {
	this->mipmapFilter = static_cast<WGPUMipmapFilterMode>(mipmapFilter);
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
	this->compare = static_cast<WGPUCompareFunction>(compare);
	return *this;
}
SamplerDescriptor& SamplerDescriptor::setMaxAnisotropy(uint16_t maxAnisotropy) {
	this->maxAnisotropy = maxAnisotropy;
	return *this;
}


// Methods of ShaderModuleDescriptor
ShaderModuleDescriptor& ShaderModuleDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
ShaderModuleDescriptor& ShaderModuleDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
ShaderModuleDescriptor& ShaderModuleDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of ShaderSourceSPIRV
ShaderSourceSPIRV& ShaderSourceSPIRV::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::ShaderSourceSPIRV;
    chain.next  = nullptr;
	return *this;
}
ShaderSourceSPIRV& ShaderSourceSPIRV::setCodeSize(uint32_t codeSize) {
	this->codeSize = codeSize;
	return *this;
}
ShaderSourceSPIRV& ShaderSourceSPIRV::setCode(const uint32_t * code) {
	this->code = code;
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
ShaderSourceWGSL& ShaderSourceWGSL::setCode(StringView code) {
	this->code = code;
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
StencilFaceState& StencilFaceState::setCompare(CompareFunction compare) {
	this->compare = static_cast<WGPUCompareFunction>(compare);
	return *this;
}
StencilFaceState& StencilFaceState::setFailOp(StencilOperation failOp) {
	this->failOp = static_cast<WGPUStencilOperation>(failOp);
	return *this;
}
StencilFaceState& StencilFaceState::setDepthFailOp(StencilOperation depthFailOp) {
	this->depthFailOp = static_cast<WGPUStencilOperation>(depthFailOp);
	return *this;
}
StencilFaceState& StencilFaceState::setPassOp(StencilOperation passOp) {
	this->passOp = static_cast<WGPUStencilOperation>(passOp);
	return *this;
}


// Methods of StorageTextureBindingLayout
StorageTextureBindingLayout& StorageTextureBindingLayout::setDefault() {
    access        = StorageTextureAccess::WriteOnly;
    format        = TextureFormat::Undefined;
    viewDimension = TextureViewDimension::_2D;
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setAccess(StorageTextureAccess access) {
	this->access = static_cast<WGPUStorageTextureAccess>(access);
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setFormat(TextureFormat format) {
	this->format = static_cast<WGPUTextureFormat>(format);
	return *this;
}
StorageTextureBindingLayout& StorageTextureBindingLayout::setViewDimension(TextureViewDimension viewDimension) {
	this->viewDimension = static_cast<WGPUTextureViewDimension>(viewDimension);
	return *this;
}


// Methods of SupportedFeatures
SupportedFeatures& SupportedFeatures::setDefault() {
	*this = WGPUSupportedFeatures {};
	return *this;
}
SupportedFeatures& SupportedFeatures::setFeatures(uint32_t featureCount, const FeatureName * features) {
	this->featureCount = featureCount;
	this->features = reinterpret_cast<WGPUFeatureName const *>(features);
	return *this;
}
SupportedFeatures& SupportedFeatures::setFeatures(const std::vector<FeatureName>& features) {
	this->featureCount = static_cast<uint32_t>(features.size());
	this->features = reinterpret_cast<WGPUFeatureName const *>(features.data());
	return *this;
}
SupportedFeatures& SupportedFeatures::setFeatures(const std::span<const FeatureName>& features) {
	this->featureCount = static_cast<uint32_t>(features.size());
	this->features = reinterpret_cast<WGPUFeatureName const *>(features.data());
	return *this;
}
void SupportedFeatures::freeMembers() {
	return wgpuSupportedFeaturesFreeMembers(*this);
}


// Methods of SupportedWGSLLanguageFeatures
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setDefault() {
	*this = WGPUSupportedWGSLLanguageFeatures {};
	return *this;
}
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setFeatures(uint32_t featureCount, const WGSLLanguageFeatureName * features) {
	this->featureCount = featureCount;
	this->features = reinterpret_cast<WGPUWGSLLanguageFeatureName const *>(features);
	return *this;
}
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setFeatures(const std::vector<WGSLLanguageFeatureName>& features) {
	this->featureCount = static_cast<uint32_t>(features.size());
	this->features = reinterpret_cast<WGPUWGSLLanguageFeatureName const *>(features.data());
	return *this;
}
SupportedWGSLLanguageFeatures& SupportedWGSLLanguageFeatures::setFeatures(const std::span<const WGSLLanguageFeatureName>& features) {
	this->featureCount = static_cast<uint32_t>(features.size());
	this->features = reinterpret_cast<WGPUWGSLLanguageFeatureName const *>(features.data());
	return *this;
}
void SupportedWGSLLanguageFeatures::freeMembers() {
	return wgpuSupportedWGSLLanguageFeaturesFreeMembers(*this);
}


// Methods of SurfaceCapabilities
SurfaceCapabilities& SurfaceCapabilities::setDefault() {
	*this = WGPUSurfaceCapabilities {};
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStructOut *>(nextInChain);
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setUsages(TextureUsage usages) {
	this->usages = static_cast<WGPUTextureUsage>(usages);
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setFormats(uint32_t formatCount, const TextureFormat * formats) {
	this->formatCount = formatCount;
	this->formats = reinterpret_cast<WGPUTextureFormat const *>(formats);
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setFormats(const std::vector<TextureFormat>& formats) {
	this->formatCount = static_cast<uint32_t>(formats.size());
	this->formats = reinterpret_cast<WGPUTextureFormat const *>(formats.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setFormats(const std::span<const TextureFormat>& formats) {
	this->formatCount = static_cast<uint32_t>(formats.size());
	this->formats = reinterpret_cast<WGPUTextureFormat const *>(formats.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setPresentModes(uint32_t presentModeCount, const PresentMode * presentModes) {
	this->presentModeCount = presentModeCount;
	this->presentModes = reinterpret_cast<WGPUPresentMode const *>(presentModes);
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setPresentModes(const std::vector<PresentMode>& presentModes) {
	this->presentModeCount = static_cast<uint32_t>(presentModes.size());
	this->presentModes = reinterpret_cast<WGPUPresentMode const *>(presentModes.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setPresentModes(const std::span<const PresentMode>& presentModes) {
	this->presentModeCount = static_cast<uint32_t>(presentModes.size());
	this->presentModes = reinterpret_cast<WGPUPresentMode const *>(presentModes.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setAlphaModes(uint32_t alphaModeCount, const CompositeAlphaMode * alphaModes) {
	this->alphaModeCount = alphaModeCount;
	this->alphaModes = reinterpret_cast<WGPUCompositeAlphaMode const *>(alphaModes);
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setAlphaModes(const std::vector<CompositeAlphaMode>& alphaModes) {
	this->alphaModeCount = static_cast<uint32_t>(alphaModes.size());
	this->alphaModes = reinterpret_cast<WGPUCompositeAlphaMode const *>(alphaModes.data());
	return *this;
}
SurfaceCapabilities& SurfaceCapabilities::setAlphaModes(const std::span<const CompositeAlphaMode>& alphaModes) {
	this->alphaModeCount = static_cast<uint32_t>(alphaModes.size());
	this->alphaModes = reinterpret_cast<WGPUCompositeAlphaMode const *>(alphaModes.data());
	return *this;
}
void SurfaceCapabilities::freeMembers() {
	return wgpuSurfaceCapabilitiesFreeMembers(*this);
}


// Methods of SurfaceConfiguration
SurfaceConfiguration& SurfaceConfiguration::setDefault() {
    format      = TextureFormat::Undefined;
    presentMode = PresentMode::Undefined;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setDevice(Device device) {
	this->device = device;
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setFormat(TextureFormat format) {
	this->format = static_cast<WGPUTextureFormat>(format);
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setUsage(TextureUsage usage) {
	this->usage = static_cast<WGPUTextureUsage>(usage);
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
SurfaceConfiguration& SurfaceConfiguration::setViewFormats(uint32_t viewFormatCount, const TextureFormat * viewFormats) {
	this->viewFormatCount = viewFormatCount;
	this->viewFormats = reinterpret_cast<WGPUTextureFormat const *>(viewFormats);
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setViewFormats(const std::vector<TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<uint32_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<WGPUTextureFormat const *>(viewFormats.data());
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setViewFormats(const std::span<const TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<uint32_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<WGPUTextureFormat const *>(viewFormats.data());
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setAlphaMode(CompositeAlphaMode alphaMode) {
	this->alphaMode = static_cast<WGPUCompositeAlphaMode>(alphaMode);
	return *this;
}
SurfaceConfiguration& SurfaceConfiguration::setPresentMode(PresentMode presentMode) {
	this->presentMode = static_cast<WGPUPresentMode>(presentMode);
	return *this;
}


// Methods of SurfaceDescriptor
SurfaceDescriptor& SurfaceDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
SurfaceDescriptor& SurfaceDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
SurfaceDescriptor& SurfaceDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}


// Methods of SurfaceSourceAndroidNativeWindow
SurfaceSourceAndroidNativeWindow& SurfaceSourceAndroidNativeWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceAndroidNativeWindow;
    chain.next  = nullptr;
	return *this;
}
SurfaceSourceAndroidNativeWindow& SurfaceSourceAndroidNativeWindow::setWindow(void * window) {
	this->window = window;
	return *this;
}


// Methods of SurfaceSourceMetalLayer
SurfaceSourceMetalLayer& SurfaceSourceMetalLayer::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceMetalLayer;
    chain.next  = nullptr;
	return *this;
}
SurfaceSourceMetalLayer& SurfaceSourceMetalLayer::setLayer(void * layer) {
	this->layer = layer;
	return *this;
}


// Methods of SurfaceSourceWaylandSurface
SurfaceSourceWaylandSurface& SurfaceSourceWaylandSurface::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceWaylandSurface;
    chain.next  = nullptr;
	return *this;
}
SurfaceSourceWaylandSurface& SurfaceSourceWaylandSurface::setDisplay(void * display) {
	this->display = display;
	return *this;
}
SurfaceSourceWaylandSurface& SurfaceSourceWaylandSurface::setSurface(void * surface) {
	this->surface = surface;
	return *this;
}


// Methods of SurfaceSourceWindowsHWND
SurfaceSourceWindowsHWND& SurfaceSourceWindowsHWND::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceWindowsHWND;
    chain.next  = nullptr;
	return *this;
}
SurfaceSourceWindowsHWND& SurfaceSourceWindowsHWND::setHinstance(void * hinstance) {
	this->hinstance = hinstance;
	return *this;
}
SurfaceSourceWindowsHWND& SurfaceSourceWindowsHWND::setHwnd(void * hwnd) {
	this->hwnd = hwnd;
	return *this;
}


// Methods of SurfaceSourceXCBWindow
SurfaceSourceXCBWindow& SurfaceSourceXCBWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceXCBWindow;
    chain.next  = nullptr;
	return *this;
}
SurfaceSourceXCBWindow& SurfaceSourceXCBWindow::setConnection(void * connection) {
	this->connection = connection;
	return *this;
}
SurfaceSourceXCBWindow& SurfaceSourceXCBWindow::setWindow(uint32_t window) {
	this->window = window;
	return *this;
}


// Methods of SurfaceSourceXlibWindow
SurfaceSourceXlibWindow& SurfaceSourceXlibWindow::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = SType::SurfaceSourceXlibWindow;
    chain.next  = nullptr;
	return *this;
}
SurfaceSourceXlibWindow& SurfaceSourceXlibWindow::setDisplay(void * display) {
	this->display = display;
	return *this;
}
SurfaceSourceXlibWindow& SurfaceSourceXlibWindow::setWindow(uint64_t window) {
	this->window = window;
	return *this;
}


// Methods of SurfaceTexture
SurfaceTexture& SurfaceTexture::setDefault() {
	*this = WGPUSurfaceTexture {};
	return *this;
}
SurfaceTexture& SurfaceTexture::setNextInChain(ChainedStructOut * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStructOut *>(nextInChain);
	return *this;
}
SurfaceTexture& SurfaceTexture::setTexture(Texture texture) {
	this->texture = texture;
	return *this;
}
SurfaceTexture& SurfaceTexture::setStatus(SurfaceGetCurrentTextureStatus status) {
	this->status = static_cast<WGPUSurfaceGetCurrentTextureStatus>(status);
	return *this;
}


// Methods of TexelCopyBufferLayout
TexelCopyBufferLayout& TexelCopyBufferLayout::setDefault() {
	*this = WGPUTexelCopyBufferLayout {};
	return *this;
}
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
TextureBindingLayout& TextureBindingLayout::setDefault() {
    sampleType    = TextureSampleType::Float;
    viewDimension = TextureViewDimension::_2D;
    multisampled  = false;
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setSampleType(TextureSampleType sampleType) {
	this->sampleType = static_cast<WGPUTextureSampleType>(sampleType);
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setViewDimension(TextureViewDimension viewDimension) {
	this->viewDimension = static_cast<WGPUTextureViewDimension>(viewDimension);
	return *this;
}
TextureBindingLayout& TextureBindingLayout::setMultisampled(WGPUBool multisampled) {
	this->multisampled = multisampled;
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
TextureViewDescriptor& TextureViewDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setFormat(TextureFormat format) {
	this->format = static_cast<WGPUTextureFormat>(format);
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setDimension(TextureViewDimension dimension) {
	this->dimension = static_cast<WGPUTextureViewDimension>(dimension);
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setBaseMipLevel(uint32_t baseMipLevel) {
	this->baseMipLevel = baseMipLevel;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setBaseArrayLayer(uint32_t baseArrayLayer) {
	this->baseArrayLayer = baseArrayLayer;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setAspect(TextureAspect aspect) {
	this->aspect = static_cast<WGPUTextureAspect>(aspect);
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setUsage(TextureUsage usage) {
	this->usage = static_cast<WGPUTextureUsage>(usage);
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setMipLevelCount(uint32_t mipLevelCount) {
	this->mipLevelCount = mipLevelCount;
	return *this;
}
TextureViewDescriptor& TextureViewDescriptor::setArrayLayerCount(uint32_t arrayLayerCount) {
	this->arrayLayerCount = arrayLayerCount;
	return *this;
}


// Methods of VertexAttribute
VertexAttribute& VertexAttribute::setDefault() {
	*this = WGPUVertexAttribute {};
	return *this;
}
VertexAttribute& VertexAttribute::setFormat(VertexFormat format) {
	this->format = static_cast<WGPUVertexFormat>(format);
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
BindGroupDescriptor& BindGroupDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setLayout(BindGroupLayout layout) {
	this->layout = layout;
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setEntries(uint32_t entryCount, const BindGroupEntry * entries) {
	this->entryCount = entryCount;
	this->entries = reinterpret_cast<WGPUBindGroupEntry const *>(entries);
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setEntries(const std::vector<BindGroupEntry>& entries) {
	this->entryCount = static_cast<uint32_t>(entries.size());
	this->entries = reinterpret_cast<WGPUBindGroupEntry const *>(entries.data());
	return *this;
}
BindGroupDescriptor& BindGroupDescriptor::setEntries(const std::span<const BindGroupEntry>& entries) {
	this->entryCount = static_cast<uint32_t>(entries.size());
	this->entries = reinterpret_cast<WGPUBindGroupEntry const *>(entries.data());
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
BindGroupLayoutEntry& BindGroupLayoutEntry::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setBinding(uint32_t binding) {
	this->binding = binding;
	return *this;
}
BindGroupLayoutEntry& BindGroupLayoutEntry::setVisibility(ShaderStage visibility) {
	this->visibility = static_cast<WGPUShaderStage>(visibility);
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
BlendState& BlendState::setDefault() {
    ((BlendComponent*)&color)->setDefault();
    ((BlendComponent*)&alpha)->setDefault();
	return *this;
}
BlendState& BlendState::setColor(BlendComponent color) {
	this->color = color;
	return *this;
}
BlendState& BlendState::setAlpha(BlendComponent alpha) {
	this->alpha = alpha;
	return *this;
}


// Methods of CompilationInfo
CompilationInfo& CompilationInfo::setDefault() {
	*this = WGPUCompilationInfo {};
	return *this;
}
CompilationInfo& CompilationInfo::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
CompilationInfo& CompilationInfo::setMessages(uint32_t messageCount, const CompilationMessage * messages) {
	this->messageCount = messageCount;
	this->messages = reinterpret_cast<WGPUCompilationMessage const *>(messages);
	return *this;
}
CompilationInfo& CompilationInfo::setMessages(const std::vector<CompilationMessage>& messages) {
	this->messageCount = static_cast<uint32_t>(messages.size());
	this->messages = reinterpret_cast<WGPUCompilationMessage const *>(messages.data());
	return *this;
}
CompilationInfo& CompilationInfo::setMessages(const std::span<const CompilationMessage>& messages) {
	this->messageCount = static_cast<uint32_t>(messages.size());
	this->messages = reinterpret_cast<WGPUCompilationMessage const *>(messages.data());
	return *this;
}


// Methods of ComputePassDescriptor
ComputePassDescriptor& ComputePassDescriptor::setDefault() {
	*this = WGPUComputePassDescriptor {};
	return *this;
}
ComputePassDescriptor& ComputePassDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
ComputePassDescriptor& ComputePassDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
ComputePassDescriptor& ComputePassDescriptor::setTimestampWrites(const ComputePassTimestampWrites * timestampWrites) {
	this->timestampWrites = reinterpret_cast<WGPU_NULLABLE WGPUComputePassTimestampWrites const *>(timestampWrites);
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
DepthStencilState& DepthStencilState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
DepthStencilState& DepthStencilState::setFormat(TextureFormat format) {
	this->format = static_cast<WGPUTextureFormat>(format);
	return *this;
}
DepthStencilState& DepthStencilState::setDepthWriteEnabled(OptionalBool depthWriteEnabled) {
	this->depthWriteEnabled = static_cast<WGPUOptionalBool>(depthWriteEnabled);
	return *this;
}
DepthStencilState& DepthStencilState::setDepthCompare(CompareFunction depthCompare) {
	this->depthCompare = static_cast<WGPUCompareFunction>(depthCompare);
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
DeviceDescriptor& DeviceDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((QueueDescriptor*)&defaultQueue)->setDefault();
    ((DeviceLostCallbackInfo*)&deviceLostCallbackInfo)->setDefault();
    ((UncapturedErrorCallbackInfo*)&uncapturedErrorCallbackInfo)->setDefault();
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredFeatures(uint32_t requiredFeatureCount, const FeatureName * requiredFeatures) {
	this->requiredFeatureCount = requiredFeatureCount;
	this->requiredFeatures = reinterpret_cast<WGPUFeatureName const *>(requiredFeatures);
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredFeatures(const std::vector<FeatureName>& requiredFeatures) {
	this->requiredFeatureCount = static_cast<uint32_t>(requiredFeatures.size());
	this->requiredFeatures = reinterpret_cast<WGPUFeatureName const *>(requiredFeatures.data());
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredFeatures(const std::span<const FeatureName>& requiredFeatures) {
	this->requiredFeatureCount = static_cast<uint32_t>(requiredFeatures.size());
	this->requiredFeatures = reinterpret_cast<WGPUFeatureName const *>(requiredFeatures.data());
	return *this;
}
DeviceDescriptor& DeviceDescriptor::setRequiredLimits(const Limits * requiredLimits) {
	this->requiredLimits = reinterpret_cast<WGPU_NULLABLE WGPULimits const *>(requiredLimits);
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
FutureWaitInfo& FutureWaitInfo::setDefault() {
	((Future*)&future)->setDefault();
	return *this;
}
FutureWaitInfo& FutureWaitInfo::setFuture(Future future) {
	this->future = future;
	return *this;
}
FutureWaitInfo& FutureWaitInfo::setCompleted(WGPUBool completed) {
	this->completed = completed;
	return *this;
}


// Methods of InstanceDescriptor
InstanceDescriptor& InstanceDescriptor::setDefault() {
	((InstanceCapabilities*)&features)->setDefault();
	return *this;
}
InstanceDescriptor& InstanceDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
InstanceDescriptor& InstanceDescriptor::setFeatures(InstanceCapabilities features) {
	this->features = features;
	return *this;
}


// Methods of ProgrammableStageDescriptor
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setDefault() {
	((StringView*)&entryPoint)->setDefault();
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setModule(ShaderModule module) {
	this->module = module;
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setEntryPoint(StringView entryPoint) {
	this->entryPoint = entryPoint;
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setConstants(uint32_t constantCount, const ConstantEntry * constants) {
	this->constantCount = constantCount;
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants);
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setConstants(const std::vector<ConstantEntry>& constants) {
	this->constantCount = static_cast<uint32_t>(constants.size());
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants.data());
	return *this;
}
ProgrammableStageDescriptor& ProgrammableStageDescriptor::setConstants(const std::span<const ConstantEntry>& constants) {
	this->constantCount = static_cast<uint32_t>(constants.size());
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants.data());
	return *this;
}


// Methods of RenderPassColorAttachment
RenderPassColorAttachment& RenderPassColorAttachment::setDefault() {
    loadOp  = LoadOp::Undefined;
    storeOp = StoreOp::Undefined;
    ((Color*)&clearValue)->setDefault();
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setView(TextureView view) {
	this->view = view;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setDepthSlice(uint32_t depthSlice) {
	this->depthSlice = depthSlice;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setResolveTarget(TextureView resolveTarget) {
	this->resolveTarget = resolveTarget;
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setLoadOp(LoadOp loadOp) {
	this->loadOp = static_cast<WGPULoadOp>(loadOp);
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setStoreOp(StoreOp storeOp) {
	this->storeOp = static_cast<WGPUStoreOp>(storeOp);
	return *this;
}
RenderPassColorAttachment& RenderPassColorAttachment::setClearValue(Color clearValue) {
	this->clearValue = clearValue;
	return *this;
}


// Methods of TexelCopyBufferInfo
TexelCopyBufferInfo& TexelCopyBufferInfo::setDefault() {
	((TexelCopyBufferLayout*)&layout)->setDefault();
	return *this;
}
TexelCopyBufferInfo& TexelCopyBufferInfo::setLayout(TexelCopyBufferLayout layout) {
	this->layout = layout;
	return *this;
}
TexelCopyBufferInfo& TexelCopyBufferInfo::setBuffer(Buffer buffer) {
	this->buffer = buffer;
	return *this;
}


// Methods of TexelCopyTextureInfo
TexelCopyTextureInfo& TexelCopyTextureInfo::setDefault() {
    mipLevel = 0;
    aspect   = TextureAspect::All;
    ((Origin3D*)&origin)->setDefault();
	return *this;
}
TexelCopyTextureInfo& TexelCopyTextureInfo::setTexture(Texture texture) {
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
	this->aspect = static_cast<WGPUTextureAspect>(aspect);
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
TextureDescriptor& TextureDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
TextureDescriptor& TextureDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
TextureDescriptor& TextureDescriptor::setUsage(TextureUsage usage) {
	this->usage = static_cast<WGPUTextureUsage>(usage);
	return *this;
}
TextureDescriptor& TextureDescriptor::setDimension(TextureDimension dimension) {
	this->dimension = static_cast<WGPUTextureDimension>(dimension);
	return *this;
}
TextureDescriptor& TextureDescriptor::setSize(Extent3D size) {
	this->size = size;
	return *this;
}
TextureDescriptor& TextureDescriptor::setFormat(TextureFormat format) {
	this->format = static_cast<WGPUTextureFormat>(format);
	return *this;
}
TextureDescriptor& TextureDescriptor::setViewFormats(uint32_t viewFormatCount, const TextureFormat * viewFormats) {
	this->viewFormatCount = viewFormatCount;
	this->viewFormats = reinterpret_cast<WGPUTextureFormat const *>(viewFormats);
	return *this;
}
TextureDescriptor& TextureDescriptor::setViewFormats(const std::vector<TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<uint32_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<WGPUTextureFormat const *>(viewFormats.data());
	return *this;
}
TextureDescriptor& TextureDescriptor::setViewFormats(const std::span<const TextureFormat>& viewFormats) {
	this->viewFormatCount = static_cast<uint32_t>(viewFormats.size());
	this->viewFormats = reinterpret_cast<WGPUTextureFormat const *>(viewFormats.data());
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


// Methods of VertexBufferLayout
VertexBufferLayout& VertexBufferLayout::setDefault() {
	stepMode = VertexStepMode::Vertex;
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setStepMode(VertexStepMode stepMode) {
	this->stepMode = static_cast<WGPUVertexStepMode>(stepMode);
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setArrayStride(uint64_t arrayStride) {
	this->arrayStride = arrayStride;
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setAttributes(uint32_t attributeCount, const VertexAttribute * attributes) {
	this->attributeCount = attributeCount;
	this->attributes = reinterpret_cast<WGPUVertexAttribute const *>(attributes);
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setAttributes(const std::vector<VertexAttribute>& attributes) {
	this->attributeCount = static_cast<uint32_t>(attributes.size());
	this->attributes = reinterpret_cast<WGPUVertexAttribute const *>(attributes.data());
	return *this;
}
VertexBufferLayout& VertexBufferLayout::setAttributes(const std::span<const VertexAttribute>& attributes) {
	this->attributeCount = static_cast<uint32_t>(attributes.size());
	this->attributes = reinterpret_cast<WGPUVertexAttribute const *>(attributes.data());
	return *this;
}


// Methods of BindGroupLayoutDescriptor
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setEntries(uint32_t entryCount, const BindGroupLayoutEntry * entries) {
	this->entryCount = entryCount;
	this->entries = reinterpret_cast<WGPUBindGroupLayoutEntry const *>(entries);
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setEntries(const std::vector<BindGroupLayoutEntry>& entries) {
	this->entryCount = static_cast<uint32_t>(entries.size());
	this->entries = reinterpret_cast<WGPUBindGroupLayoutEntry const *>(entries.data());
	return *this;
}
BindGroupLayoutDescriptor& BindGroupLayoutDescriptor::setEntries(const std::span<const BindGroupLayoutEntry>& entries) {
	this->entryCount = static_cast<uint32_t>(entries.size());
	this->entries = reinterpret_cast<WGPUBindGroupLayoutEntry const *>(entries.data());
	return *this;
}


// Methods of ColorTargetState
ColorTargetState& ColorTargetState::setDefault() {
	format = TextureFormat::Undefined;
	return *this;
}
ColorTargetState& ColorTargetState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
ColorTargetState& ColorTargetState::setFormat(TextureFormat format) {
	this->format = static_cast<WGPUTextureFormat>(format);
	return *this;
}
ColorTargetState& ColorTargetState::setBlend(const BlendState * blend) {
	this->blend = reinterpret_cast<WGPU_NULLABLE WGPUBlendState const *>(blend);
	return *this;
}
ColorTargetState& ColorTargetState::setWriteMask(ColorWriteMask writeMask) {
	this->writeMask = static_cast<WGPUColorWriteMask>(writeMask);
	return *this;
}


// Methods of ComputePipelineDescriptor
ComputePipelineDescriptor& ComputePipelineDescriptor::setDefault() {
    ((StringView*)&label)->setDefault();
    ((ProgrammableStageDescriptor*)&compute)->setDefault();
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setLayout(PipelineLayout layout) {
	this->layout = layout;
	return *this;
}
ComputePipelineDescriptor& ComputePipelineDescriptor::setCompute(ProgrammableStageDescriptor compute) {
	this->compute = compute;
	return *this;
}


// Methods of RenderPassDescriptor
RenderPassDescriptor& RenderPassDescriptor::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setColorAttachments(uint32_t colorAttachmentCount, const RenderPassColorAttachment * colorAttachments) {
	this->colorAttachmentCount = colorAttachmentCount;
	this->colorAttachments = reinterpret_cast<WGPURenderPassColorAttachment const *>(colorAttachments);
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setColorAttachments(const std::vector<RenderPassColorAttachment>& colorAttachments) {
	this->colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
	this->colorAttachments = reinterpret_cast<WGPURenderPassColorAttachment const *>(colorAttachments.data());
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setColorAttachments(const std::span<const RenderPassColorAttachment>& colorAttachments) {
	this->colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
	this->colorAttachments = reinterpret_cast<WGPURenderPassColorAttachment const *>(colorAttachments.data());
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setDepthStencilAttachment(const RenderPassDepthStencilAttachment * depthStencilAttachment) {
	this->depthStencilAttachment = reinterpret_cast<WGPU_NULLABLE WGPURenderPassDepthStencilAttachment const *>(depthStencilAttachment);
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setOcclusionQuerySet(QuerySet occlusionQuerySet) {
	this->occlusionQuerySet = occlusionQuerySet;
	return *this;
}
RenderPassDescriptor& RenderPassDescriptor::setTimestampWrites(const RenderPassTimestampWrites * timestampWrites) {
	this->timestampWrites = reinterpret_cast<WGPU_NULLABLE WGPURenderPassTimestampWrites const *>(timestampWrites);
	return *this;
}


// Methods of VertexState
VertexState& VertexState::setDefault() {
	((StringView*)&entryPoint)->setDefault();
	return *this;
}
VertexState& VertexState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
VertexState& VertexState::setModule(ShaderModule module) {
	this->module = module;
	return *this;
}
VertexState& VertexState::setEntryPoint(StringView entryPoint) {
	this->entryPoint = entryPoint;
	return *this;
}
VertexState& VertexState::setConstants(uint32_t constantCount, const ConstantEntry * constants) {
	this->constantCount = constantCount;
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants);
	return *this;
}
VertexState& VertexState::setConstants(const std::vector<ConstantEntry>& constants) {
	this->constantCount = static_cast<uint32_t>(constants.size());
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants.data());
	return *this;
}
VertexState& VertexState::setConstants(const std::span<const ConstantEntry>& constants) {
	this->constantCount = static_cast<uint32_t>(constants.size());
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants.data());
	return *this;
}
VertexState& VertexState::setBuffers(uint32_t bufferCount, const VertexBufferLayout * buffers) {
	this->bufferCount = bufferCount;
	this->buffers = reinterpret_cast<WGPUVertexBufferLayout const *>(buffers);
	return *this;
}
VertexState& VertexState::setBuffers(const std::vector<VertexBufferLayout>& buffers) {
	this->bufferCount = static_cast<uint32_t>(buffers.size());
	this->buffers = reinterpret_cast<WGPUVertexBufferLayout const *>(buffers.data());
	return *this;
}
VertexState& VertexState::setBuffers(const std::span<const VertexBufferLayout>& buffers) {
	this->bufferCount = static_cast<uint32_t>(buffers.size());
	this->buffers = reinterpret_cast<WGPUVertexBufferLayout const *>(buffers.data());
	return *this;
}


// Methods of FragmentState
FragmentState& FragmentState::setDefault() {
	((StringView*)&entryPoint)->setDefault();
	return *this;
}
FragmentState& FragmentState::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
FragmentState& FragmentState::setModule(ShaderModule module) {
	this->module = module;
	return *this;
}
FragmentState& FragmentState::setEntryPoint(StringView entryPoint) {
	this->entryPoint = entryPoint;
	return *this;
}
FragmentState& FragmentState::setConstants(uint32_t constantCount, const ConstantEntry * constants) {
	this->constantCount = constantCount;
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants);
	return *this;
}
FragmentState& FragmentState::setConstants(const std::vector<ConstantEntry>& constants) {
	this->constantCount = static_cast<uint32_t>(constants.size());
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants.data());
	return *this;
}
FragmentState& FragmentState::setConstants(const std::span<const ConstantEntry>& constants) {
	this->constantCount = static_cast<uint32_t>(constants.size());
	this->constants = reinterpret_cast<WGPUConstantEntry const *>(constants.data());
	return *this;
}
FragmentState& FragmentState::setTargets(uint32_t targetCount, const ColorTargetState * targets) {
	this->targetCount = targetCount;
	this->targets = reinterpret_cast<WGPUColorTargetState const *>(targets);
	return *this;
}
FragmentState& FragmentState::setTargets(const std::vector<ColorTargetState>& targets) {
	this->targetCount = static_cast<uint32_t>(targets.size());
	this->targets = reinterpret_cast<WGPUColorTargetState const *>(targets.data());
	return *this;
}
FragmentState& FragmentState::setTargets(const std::span<const ColorTargetState>& targets) {
	this->targetCount = static_cast<uint32_t>(targets.size());
	this->targets = reinterpret_cast<WGPUColorTargetState const *>(targets.data());
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
RenderPipelineDescriptor& RenderPipelineDescriptor::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setLabel(StringView label) {
	this->label = label;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setLayout(PipelineLayout layout) {
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
	this->depthStencil = reinterpret_cast<WGPU_NULLABLE WGPUDepthStencilState const *>(depthStencil);
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setMultisample(MultisampleState multisample) {
	this->multisample = multisample;
	return *this;
}
RenderPipelineDescriptor& RenderPipelineDescriptor::setFragment(const FragmentState * fragment) {
	this->fragment = reinterpret_cast<WGPU_NULLABLE WGPUFragmentState const *>(fragment);
	return *this;
}


// Methods of InstanceExtras
InstanceExtras& InstanceExtras::setDefault() {
    dx12ShaderCompiler = Dx12Compiler::Undefined;
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&dxcPath)->setDefault();
    chain.sType = (WGPUSType)NativeSType::InstanceExtras;
    chain.next  = nullptr;
	return *this;
}
InstanceExtras& InstanceExtras::setBackends(InstanceBackend backends) {
	this->backends = static_cast<WGPUInstanceBackend>(backends);
	return *this;
}
InstanceExtras& InstanceExtras::setFlags(InstanceFlag flags) {
	this->flags = static_cast<WGPUInstanceFlag>(flags);
	return *this;
}
InstanceExtras& InstanceExtras::setDx12ShaderCompiler(Dx12Compiler dx12ShaderCompiler) {
	this->dx12ShaderCompiler = static_cast<WGPUDx12Compiler>(dx12ShaderCompiler);
	return *this;
}
InstanceExtras& InstanceExtras::setGles3MinorVersion(Gles3MinorVersion gles3MinorVersion) {
	this->gles3MinorVersion = static_cast<WGPUGles3MinorVersion>(gles3MinorVersion);
	return *this;
}
InstanceExtras& InstanceExtras::setGlFenceBehaviour(GLFenceBehaviour glFenceBehaviour) {
	this->glFenceBehaviour = static_cast<WGPUGLFenceBehaviour>(glFenceBehaviour);
	return *this;
}
InstanceExtras& InstanceExtras::setDxcPath(StringView dxcPath) {
	this->dxcPath = dxcPath;
	return *this;
}
InstanceExtras& InstanceExtras::setDxcMaxShaderModel(DxcMaxShaderModel dxcMaxShaderModel) {
	this->dxcMaxShaderModel = static_cast<WGPUDxcMaxShaderModel>(dxcMaxShaderModel);
	return *this;
}
InstanceExtras& InstanceExtras::setDx12PresentationSystem(Dx12SwapchainKind dx12PresentationSystem) {
	this->dx12PresentationSystem = static_cast<WGPUDx12SwapchainKind>(dx12PresentationSystem);
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
DeviceExtras& DeviceExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    ((StringView*)&tracePath)->setDefault();
    chain.sType = (WGPUSType)NativeSType::DeviceExtras;
    chain.next  = nullptr;
	return *this;
}
DeviceExtras& DeviceExtras::setTracePath(StringView tracePath) {
	this->tracePath = tracePath;
	return *this;
}


// Methods of NativeLimits
NativeLimits& NativeLimits::setDefault() {
    ((ChainedStructOut*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::NativeLimits;
    chain.next  = nullptr;
	return *this;
}
NativeLimits& NativeLimits::setMaxPushConstantSize(uint32_t maxPushConstantSize) {
	this->maxPushConstantSize = maxPushConstantSize;
	return *this;
}
NativeLimits& NativeLimits::setMaxNonSamplerBindings(uint32_t maxNonSamplerBindings) {
	this->maxNonSamplerBindings = maxNonSamplerBindings;
	return *this;
}


// Methods of PushConstantRange
PushConstantRange& PushConstantRange::setDefault() {
	*this = WGPUPushConstantRange {};
	return *this;
}
PushConstantRange& PushConstantRange::setStages(ShaderStage stages) {
	this->stages = static_cast<WGPUShaderStage>(stages);
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
PipelineLayoutExtras& PipelineLayoutExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::PipelineLayoutExtras;
    chain.next  = nullptr;
	return *this;
}
PipelineLayoutExtras& PipelineLayoutExtras::setPushConstantRanges(uint32_t pushConstantRangeCount, const PushConstantRange * pushConstantRanges) {
	this->pushConstantRangeCount = pushConstantRangeCount;
	this->pushConstantRanges = reinterpret_cast<WGPUPushConstantRange const *>(pushConstantRanges);
	return *this;
}
PipelineLayoutExtras& PipelineLayoutExtras::setPushConstantRanges(const std::vector<PushConstantRange>& pushConstantRanges) {
	this->pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	this->pushConstantRanges = reinterpret_cast<WGPUPushConstantRange const *>(pushConstantRanges.data());
	return *this;
}
PipelineLayoutExtras& PipelineLayoutExtras::setPushConstantRanges(const std::span<const PushConstantRange>& pushConstantRanges) {
	this->pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	this->pushConstantRanges = reinterpret_cast<WGPUPushConstantRange const *>(pushConstantRanges.data());
	return *this;
}


// Methods of ShaderDefine
ShaderDefine& ShaderDefine::setDefault() {
    ((StringView*)&name)->setDefault();
    ((StringView*)&value)->setDefault();
	return *this;
}
ShaderDefine& ShaderDefine::setName(StringView name) {
	this->name = name;
	return *this;
}
ShaderDefine& ShaderDefine::setValue(StringView value) {
	this->value = value;
	return *this;
}


// Methods of ShaderSourceGLSL
ShaderSourceGLSL& ShaderSourceGLSL::setDefault() {
	*this = WGPUShaderSourceGLSL {};
	chain.sType = (WGPUSType)NativeSType::ShaderSourceGLSL;
	chain.next = nullptr;
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setStage(ShaderStage stage) {
	this->stage = static_cast<WGPUShaderStage>(stage);
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setCode(StringView code) {
	this->code = code;
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setDefines(uint32_t defineCount, ShaderDefine * defines) {
	this->defineCount = defineCount;
	this->defines = reinterpret_cast<WGPUShaderDefine *>(defines);
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setDefines(std::vector<ShaderDefine>& defines) {
	this->defineCount = static_cast<uint32_t>(defines.size());
	this->defines = reinterpret_cast<WGPUShaderDefine *>(defines.data());
	return *this;
}
ShaderSourceGLSL& ShaderSourceGLSL::setDefines(const std::span<ShaderDefine>& defines) {
	this->defineCount = static_cast<uint32_t>(defines.size());
	this->defines = reinterpret_cast<WGPUShaderDefine *>(defines.data());
	return *this;
}


// Methods of ShaderModuleDescriptorSpirV
ShaderModuleDescriptorSpirV& ShaderModuleDescriptorSpirV::setDefault() {
	((StringView*)&label)->setDefault();
	return *this;
}
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
RegistryReport& RegistryReport::setDefault() {
	*this = WGPURegistryReport {};
	return *this;
}
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
GlobalReport& GlobalReport::setDefault() {
    ((RegistryReport*)&surfaces)->setDefault();
    ((HubReport*)&hub)->setDefault();
	return *this;
}
GlobalReport& GlobalReport::setSurfaces(RegistryReport surfaces) {
	this->surfaces = surfaces;
	return *this;
}
GlobalReport& GlobalReport::setHub(HubReport hub) {
	this->hub = hub;
	return *this;
}


// Methods of InstanceEnumerateAdapterOptions
InstanceEnumerateAdapterOptions& InstanceEnumerateAdapterOptions::setDefault() {
	*this = WGPUInstanceEnumerateAdapterOptions {};
	return *this;
}
InstanceEnumerateAdapterOptions& InstanceEnumerateAdapterOptions::setNextInChain(const ChainedStruct * nextInChain) {
	this->nextInChain = reinterpret_cast<WGPUChainedStruct const *>(nextInChain);
	return *this;
}
InstanceEnumerateAdapterOptions& InstanceEnumerateAdapterOptions::setBackends(InstanceBackend backends) {
	this->backends = static_cast<WGPUInstanceBackend>(backends);
	return *this;
}


// Methods of BindGroupEntryExtras
BindGroupEntryExtras& BindGroupEntryExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::BindGroupEntryExtras;
    chain.next  = nullptr;
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(uint32_t bufferCount, const Buffer * buffers) {
	this->bufferCount = bufferCount;
	this->buffers = reinterpret_cast<WGPUBuffer const *>(buffers);
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(const std::vector<Buffer>& buffers) {
	this->bufferCount = static_cast<uint32_t>(buffers.size());
	this->buffers = reinterpret_cast<WGPUBuffer const *>(buffers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setBuffers(const std::span<const Buffer>& buffers) {
	this->bufferCount = static_cast<uint32_t>(buffers.size());
	this->buffers = reinterpret_cast<WGPUBuffer const *>(buffers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(uint32_t samplerCount, const Sampler * samplers) {
	this->samplerCount = samplerCount;
	this->samplers = reinterpret_cast<WGPUSampler const *>(samplers);
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(const std::vector<Sampler>& samplers) {
	this->samplerCount = static_cast<uint32_t>(samplers.size());
	this->samplers = reinterpret_cast<WGPUSampler const *>(samplers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setSamplers(const std::span<const Sampler>& samplers) {
	this->samplerCount = static_cast<uint32_t>(samplers.size());
	this->samplers = reinterpret_cast<WGPUSampler const *>(samplers.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(uint32_t textureViewCount, const TextureView * textureViews) {
	this->textureViewCount = textureViewCount;
	this->textureViews = reinterpret_cast<WGPUTextureView const *>(textureViews);
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(const std::vector<TextureView>& textureViews) {
	this->textureViewCount = static_cast<uint32_t>(textureViews.size());
	this->textureViews = reinterpret_cast<WGPUTextureView const *>(textureViews.data());
	return *this;
}
BindGroupEntryExtras& BindGroupEntryExtras::setTextureViews(const std::span<const TextureView>& textureViews) {
	this->textureViewCount = static_cast<uint32_t>(textureViews.size());
	this->textureViews = reinterpret_cast<WGPUTextureView const *>(textureViews.data());
	return *this;
}


// Methods of BindGroupLayoutEntryExtras
BindGroupLayoutEntryExtras& BindGroupLayoutEntryExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::BindGroupLayoutEntryExtras;
    chain.next  = nullptr;
	return *this;
}
BindGroupLayoutEntryExtras& BindGroupLayoutEntryExtras::setCount(uint32_t count) {
	this->count = count;
	return *this;
}


// Methods of QuerySetDescriptorExtras
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::QuerySetDescriptorExtras;
    chain.next  = nullptr;
	return *this;
}
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setPipelineStatistics(uint32_t pipelineStatisticCount, const PipelineStatisticName * pipelineStatistics) {
	this->pipelineStatisticCount = pipelineStatisticCount;
	this->pipelineStatistics = reinterpret_cast<WGPUPipelineStatisticName const *>(pipelineStatistics);
	return *this;
}
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setPipelineStatistics(const std::vector<PipelineStatisticName>& pipelineStatistics) {
	this->pipelineStatisticCount = static_cast<uint32_t>(pipelineStatistics.size());
	this->pipelineStatistics = reinterpret_cast<WGPUPipelineStatisticName const *>(pipelineStatistics.data());
	return *this;
}
QuerySetDescriptorExtras& QuerySetDescriptorExtras::setPipelineStatistics(const std::span<const PipelineStatisticName>& pipelineStatistics) {
	this->pipelineStatisticCount = static_cast<uint32_t>(pipelineStatistics.size());
	this->pipelineStatistics = reinterpret_cast<WGPUPipelineStatisticName const *>(pipelineStatistics.data());
	return *this;
}


// Methods of SurfaceConfigurationExtras
SurfaceConfigurationExtras& SurfaceConfigurationExtras::setDefault() {
    ((ChainedStruct*)&chain)->setDefault();
    chain.sType = (WGPUSType)NativeSType::SurfaceConfigurationExtras;
    chain.next  = nullptr;
	return *this;
}
SurfaceConfigurationExtras& SurfaceConfigurationExtras::setDesiredMaximumFrameLatency(uint32_t desiredMaximumFrameLatency) {
	this->desiredMaximumFrameLatency = desiredMaximumFrameLatency;
	return *this;
}


// Methods of SurfaceSourceSwapChainPanel
SurfaceSourceSwapChainPanel& SurfaceSourceSwapChainPanel::setDefault() {
	*this = WGPUSurfaceSourceSwapChainPanel {};
	chain.sType = (WGPUSType)NativeSType::SurfaceSourceSwapChainPanel;
	chain.next = nullptr;
	return *this;
}
SurfaceSourceSwapChainPanel& SurfaceSourceSwapChainPanel::setPanelNative(void * panelNative) {
	this->panelNative = panelNative;
	return *this;
}


// Methods of PrimitiveStateExtras
PrimitiveStateExtras& PrimitiveStateExtras::setDefault() {
	*this = WGPUPrimitiveStateExtras {};
	chain.sType = (WGPUSType)NativeSType::PrimitiveStateExtras;
	chain.next = nullptr;
	return *this;
}
PrimitiveStateExtras& PrimitiveStateExtras::setPolygonMode(PolygonMode polygonMode) {
	this->polygonMode = static_cast<WGPUPolygonMode>(polygonMode);
	return *this;
}
PrimitiveStateExtras& PrimitiveStateExtras::setConservative(WGPUBool conservative) {
	this->conservative = conservative;
	return *this;
}


// Methods of Adapter
void Adapter::getFeatures(SupportedFeatures * features) const {
	return wgpuAdapterGetFeatures(m_raw, features);
}
Status Adapter::getInfo(AdapterInfo * info) const {
	return static_cast<Status>(wgpuAdapterGetInfo(m_raw, info));
}
Status Adapter::getLimits(Limits * limits) const {
	return static_cast<Status>(wgpuAdapterGetLimits(m_raw, limits));
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
	return wgpuBindGroupSetLabel(m_raw, label);
}
void BindGroup::addRef() const {
	return wgpuBindGroupAddRef(m_raw);
}
void BindGroup::release() const {
	return wgpuBindGroupRelease(m_raw);
}


// Methods of BindGroupLayout
void BindGroupLayout::setLabel(StringView label) const {
	return wgpuBindGroupLayoutSetLabel(m_raw, label);
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
	return wgpuBufferSetLabel(m_raw, label);
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
	return wgpuCommandBufferSetLabel(m_raw, label);
}
void CommandBuffer::addRef() const {
	return wgpuCommandBufferAddRef(m_raw);
}
void CommandBuffer::release() const {
	return wgpuCommandBufferRelease(m_raw);
}


// Methods of CommandEncoder
ComputePassEncoder CommandEncoder::beginComputePass(const ComputePassDescriptor& descriptor) const {
	return wgpuCommandEncoderBeginComputePass(m_raw, &descriptor);
}
ComputePassEncoder CommandEncoder::beginComputePass() const {
	return wgpuCommandEncoderBeginComputePass(m_raw, nullptr);
}
RenderPassEncoder CommandEncoder::beginRenderPass(const RenderPassDescriptor& descriptor) const {
	return wgpuCommandEncoderBeginRenderPass(m_raw, &descriptor);
}
void CommandEncoder::clearBuffer(Buffer buffer, uint64_t offset, uint64_t size) const {
	return wgpuCommandEncoderClearBuffer(m_raw, buffer, offset, size);
}
void CommandEncoder::copyBufferToBuffer(Buffer source, uint64_t sourceOffset, Buffer destination, uint64_t destinationOffset, uint64_t size) const {
	return wgpuCommandEncoderCopyBufferToBuffer(m_raw, source, sourceOffset, destination, destinationOffset, size);
}
void CommandEncoder::copyBufferToTexture(const TexelCopyBufferInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const {
	return wgpuCommandEncoderCopyBufferToTexture(m_raw, &source, &destination, &copySize);
}
void CommandEncoder::copyTextureToBuffer(const TexelCopyTextureInfo& source, const TexelCopyBufferInfo& destination, const Extent3D& copySize) const {
	return wgpuCommandEncoderCopyTextureToBuffer(m_raw, &source, &destination, &copySize);
}
void CommandEncoder::copyTextureToTexture(const TexelCopyTextureInfo& source, const TexelCopyTextureInfo& destination, const Extent3D& copySize) const {
	return wgpuCommandEncoderCopyTextureToTexture(m_raw, &source, &destination, &copySize);
}
CommandBuffer CommandEncoder::finish(const CommandBufferDescriptor& descriptor) const {
	return wgpuCommandEncoderFinish(m_raw, &descriptor);
}
CommandBuffer CommandEncoder::finish() const {
	return wgpuCommandEncoderFinish(m_raw, nullptr);
}
void CommandEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuCommandEncoderInsertDebugMarker(m_raw, markerLabel);
}
void CommandEncoder::popDebugGroup() const {
	return wgpuCommandEncoderPopDebugGroup(m_raw);
}
void CommandEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuCommandEncoderPushDebugGroup(m_raw, groupLabel);
}
void CommandEncoder::resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, Buffer destination, uint64_t destinationOffset) const {
	return wgpuCommandEncoderResolveQuerySet(m_raw, querySet, firstQuery, queryCount, destination, destinationOffset);
}
void CommandEncoder::setLabel(StringView label) const {
	return wgpuCommandEncoderSetLabel(m_raw, label);
}
void CommandEncoder::writeTimestamp(QuerySet querySet, uint32_t queryIndex) const {
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
void ComputePassEncoder::dispatchWorkgroupsIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuComputePassEncoderDispatchWorkgroupsIndirect(m_raw, indirectBuffer, indirectOffset);
}
void ComputePassEncoder::end() const {
	return wgpuComputePassEncoderEnd(m_raw);
}
void ComputePassEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuComputePassEncoderInsertDebugMarker(m_raw, markerLabel);
}
void ComputePassEncoder::popDebugGroup() const {
	return wgpuComputePassEncoderPopDebugGroup(m_raw);
}
void ComputePassEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuComputePassEncoderPushDebugGroup(m_raw, groupLabel);
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), dynamicOffsets.data());
}
void ComputePassEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, const uint32_t& dynamicOffsets) const {
	return wgpuComputePassEncoderSetBindGroup(m_raw, groupIndex, group, 1, &dynamicOffsets);
}
void ComputePassEncoder::setLabel(StringView label) const {
	return wgpuComputePassEncoderSetLabel(m_raw, label);
}
void ComputePassEncoder::setPipeline(ComputePipeline pipeline) const {
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
void ComputePassEncoder::beginPipelineStatisticsQuery(QuerySet querySet, uint32_t queryIndex) const {
	return wgpuComputePassEncoderBeginPipelineStatisticsQuery(m_raw, querySet, queryIndex);
}
void ComputePassEncoder::endPipelineStatisticsQuery() const {
	return wgpuComputePassEncoderEndPipelineStatisticsQuery(m_raw);
}
void ComputePassEncoder::writeTimestamp(QuerySet querySet, uint32_t queryIndex) const {
	return wgpuComputePassEncoderWriteTimestamp(m_raw, querySet, queryIndex);
}


// Methods of ComputePipeline
BindGroupLayout ComputePipeline::getBindGroupLayout(uint32_t groupIndex) const {
	return wgpuComputePipelineGetBindGroupLayout(m_raw, groupIndex);
}
void ComputePipeline::setLabel(StringView label) const {
	return wgpuComputePipelineSetLabel(m_raw, label);
}
void ComputePipeline::addRef() const {
	return wgpuComputePipelineAddRef(m_raw);
}
void ComputePipeline::release() const {
	return wgpuComputePipelineRelease(m_raw);
}


// Methods of Device
BindGroup Device::createBindGroup(const BindGroupDescriptor& descriptor) const {
	return wgpuDeviceCreateBindGroup(m_raw, &descriptor);
}
BindGroupLayout Device::createBindGroupLayout(const BindGroupLayoutDescriptor& descriptor) const {
	return wgpuDeviceCreateBindGroupLayout(m_raw, &descriptor);
}
Buffer Device::createBuffer(const BufferDescriptor& descriptor) const {
	return wgpuDeviceCreateBuffer(m_raw, &descriptor);
}
CommandEncoder Device::createCommandEncoder(const CommandEncoderDescriptor& descriptor) const {
	return wgpuDeviceCreateCommandEncoder(m_raw, &descriptor);
}
CommandEncoder Device::createCommandEncoder() const {
	return wgpuDeviceCreateCommandEncoder(m_raw, nullptr);
}
ComputePipeline Device::createComputePipeline(const ComputePipelineDescriptor& descriptor) const {
	return wgpuDeviceCreateComputePipeline(m_raw, &descriptor);
}
PipelineLayout Device::createPipelineLayout(const PipelineLayoutDescriptor& descriptor) const {
	return wgpuDeviceCreatePipelineLayout(m_raw, &descriptor);
}
QuerySet Device::createQuerySet(const QuerySetDescriptor& descriptor) const {
	return wgpuDeviceCreateQuerySet(m_raw, &descriptor);
}
RenderBundleEncoder Device::createRenderBundleEncoder(const RenderBundleEncoderDescriptor& descriptor) const {
	return wgpuDeviceCreateRenderBundleEncoder(m_raw, &descriptor);
}
RenderPipeline Device::createRenderPipeline(const RenderPipelineDescriptor& descriptor) const {
	return wgpuDeviceCreateRenderPipeline(m_raw, &descriptor);
}
Sampler Device::createSampler(const SamplerDescriptor& descriptor) const {
	return wgpuDeviceCreateSampler(m_raw, &descriptor);
}
Sampler Device::createSampler() const {
	return wgpuDeviceCreateSampler(m_raw, nullptr);
}
ShaderModule Device::createShaderModule(const ShaderModuleDescriptor& descriptor) const {
	return wgpuDeviceCreateShaderModule(m_raw, &descriptor);
}
Texture Device::createTexture(const TextureDescriptor& descriptor) const {
	return wgpuDeviceCreateTexture(m_raw, &descriptor);
}
void Device::destroy() const {
	return wgpuDeviceDestroy(m_raw);
}
AdapterInfo Device::getAdapterInfo() const {
	return wgpuDeviceGetAdapterInfo(m_raw);
}
void Device::getFeatures(SupportedFeatures * features) const {
	return wgpuDeviceGetFeatures(m_raw, features);
}
Status Device::getLimits(Limits * limits) const {
	return static_cast<Status>(wgpuDeviceGetLimits(m_raw, limits));
}
Queue Device::getQueue() const {
	return wgpuDeviceGetQueue(m_raw);
}
Bool Device::hasFeature(FeatureName feature) const {
	return wgpuDeviceHasFeature(m_raw, static_cast<WGPUFeatureName>(feature));
}
void Device::pushErrorScope(ErrorFilter filter) const {
	return wgpuDevicePushErrorScope(m_raw, static_cast<WGPUErrorFilter>(filter));
}
void Device::setLabel(StringView label) const {
	return wgpuDeviceSetLabel(m_raw, label);
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
ShaderModule Device::createShaderModuleSpirV(const ShaderModuleDescriptorSpirV& descriptor) const {
	return wgpuDeviceCreateShaderModuleSpirV(m_raw, &descriptor);
}


// Methods of Instance
Surface Instance::createSurface(const SurfaceDescriptor& descriptor) const {
	return wgpuInstanceCreateSurface(m_raw, &descriptor);
}
Status Instance::getWGSLLanguageFeatures(SupportedWGSLLanguageFeatures * features) const {
	return static_cast<Status>(wgpuInstanceGetWGSLLanguageFeatures(m_raw, features));
}
Bool Instance::hasWGSLLanguageFeature(WGSLLanguageFeatureName feature) const {
	return wgpuInstanceHasWGSLLanguageFeature(m_raw, static_cast<WGPUWGSLLanguageFeatureName>(feature));
}
void Instance::processEvents() const {
	return wgpuInstanceProcessEvents(m_raw);
}
WaitStatus Instance::waitAny(size_t futureCount, FutureWaitInfo * futures, uint64_t timeoutNS) const {
	return static_cast<WaitStatus>(wgpuInstanceWaitAny(m_raw, futureCount, futures, timeoutNS));
}
void Instance::addRef() const {
	return wgpuInstanceAddRef(m_raw);
}
void Instance::release() const {
	return wgpuInstanceRelease(m_raw);
}
size_t Instance::enumerateAdapters(const InstanceEnumerateAdapterOptions& options, Adapter * adapters) const {
	return wgpuInstanceEnumerateAdapters(m_raw, &options, reinterpret_cast<WGPUAdapter *>(adapters));
}


// Methods of PipelineLayout
void PipelineLayout::setLabel(StringView label) const {
	return wgpuPipelineLayoutSetLabel(m_raw, label);
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
	return wgpuQuerySetSetLabel(m_raw, label);
}
void QuerySet::addRef() const {
	return wgpuQuerySetAddRef(m_raw);
}
void QuerySet::release() const {
	return wgpuQuerySetRelease(m_raw);
}


// Methods of Queue
void Queue::setLabel(StringView label) const {
	return wgpuQueueSetLabel(m_raw, label);
}
void Queue::submit(size_t commandCount, CommandBuffer const * commands) const {
	return wgpuQueueSubmit(m_raw, commandCount, reinterpret_cast<WGPUCommandBuffer const *>(commands));
}
void Queue::submit(const std::vector<WGPUCommandBuffer>& commands) const {
	return wgpuQueueSubmit(m_raw, static_cast<size_t>(commands.size()), commands.data());
}
void Queue::submit(const WGPUCommandBuffer& commands) const {
	return wgpuQueueSubmit(m_raw, 1, &commands);
}
void Queue::writeBuffer(Buffer buffer, uint64_t bufferOffset, void const * data, size_t size) const {
	return wgpuQueueWriteBuffer(m_raw, buffer, bufferOffset, data, size);
}
void Queue::writeTexture(const TexelCopyTextureInfo& destination, void const * data, size_t dataSize, const TexelCopyBufferLayout& dataLayout, const Extent3D& writeSize) const {
	return wgpuQueueWriteTexture(m_raw, &destination, data, dataSize, &dataLayout, &writeSize);
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
SubmissionIndex Queue::submitForIndex(const std::vector<WGPUCommandBuffer>& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, static_cast<size_t>(commands.size()), commands.data());
}
SubmissionIndex Queue::submitForIndex(const WGPUCommandBuffer& commands) const {
	return wgpuQueueSubmitForIndex(m_raw, 1, &commands);
}
float Queue::getTimestampPeriod() const {
	return wgpuQueueGetTimestampPeriod(m_raw);
}


// Methods of RenderBundle
void RenderBundle::setLabel(StringView label) const {
	return wgpuRenderBundleSetLabel(m_raw, label);
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
void RenderBundleEncoder::drawIndexedIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderBundleEncoderDrawIndexedIndirect(m_raw, indirectBuffer, indirectOffset);
}
void RenderBundleEncoder::drawIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderBundleEncoderDrawIndirect(m_raw, indirectBuffer, indirectOffset);
}
RenderBundle RenderBundleEncoder::finish(const RenderBundleDescriptor& descriptor) const {
	return wgpuRenderBundleEncoderFinish(m_raw, &descriptor);
}
RenderBundle RenderBundleEncoder::finish() const {
	return wgpuRenderBundleEncoderFinish(m_raw, nullptr);
}
void RenderBundleEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuRenderBundleEncoderInsertDebugMarker(m_raw, markerLabel);
}
void RenderBundleEncoder::popDebugGroup() const {
	return wgpuRenderBundleEncoderPopDebugGroup(m_raw);
}
void RenderBundleEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuRenderBundleEncoderPushDebugGroup(m_raw, groupLabel);
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), dynamicOffsets.data());
}
void RenderBundleEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, const uint32_t& dynamicOffsets) const {
	return wgpuRenderBundleEncoderSetBindGroup(m_raw, groupIndex, group, 1, &dynamicOffsets);
}
void RenderBundleEncoder::setIndexBuffer(Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const {
	return wgpuRenderBundleEncoderSetIndexBuffer(m_raw, buffer, static_cast<WGPUIndexFormat>(format), offset, size);
}
void RenderBundleEncoder::setLabel(StringView label) const {
	return wgpuRenderBundleEncoderSetLabel(m_raw, label);
}
void RenderBundleEncoder::setPipeline(RenderPipeline pipeline) const {
	return wgpuRenderBundleEncoderSetPipeline(m_raw, pipeline);
}
void RenderBundleEncoder::setVertexBuffer(uint32_t slot, Buffer buffer, uint64_t offset, uint64_t size) const {
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
void RenderPassEncoder::drawIndexedIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const {
	return wgpuRenderPassEncoderDrawIndexedIndirect(m_raw, indirectBuffer, indirectOffset);
}
void RenderPassEncoder::drawIndirect(Buffer indirectBuffer, uint64_t indirectOffset) const {
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
void RenderPassEncoder::executeBundles(const std::vector<WGPURenderBundle>& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, static_cast<size_t>(bundles.size()), bundles.data());
}
void RenderPassEncoder::executeBundles(const WGPURenderBundle& bundles) const {
	return wgpuRenderPassEncoderExecuteBundles(m_raw, 1, &bundles);
}
void RenderPassEncoder::insertDebugMarker(StringView markerLabel) const {
	return wgpuRenderPassEncoderInsertDebugMarker(m_raw, markerLabel);
}
void RenderPassEncoder::popDebugGroup() const {
	return wgpuRenderPassEncoderPopDebugGroup(m_raw);
}
void RenderPassEncoder::pushDebugGroup(StringView groupLabel) const {
	return wgpuRenderPassEncoderPushDebugGroup(m_raw, groupLabel);
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, size_t dynamicOffsetCount, uint32_t const * dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, dynamicOffsetCount, dynamicOffsets);
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, const std::vector<uint32_t>& dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, static_cast<size_t>(dynamicOffsets.size()), dynamicOffsets.data());
}
void RenderPassEncoder::setBindGroup(uint32_t groupIndex, BindGroup group, const uint32_t& dynamicOffsets) const {
	return wgpuRenderPassEncoderSetBindGroup(m_raw, groupIndex, group, 1, &dynamicOffsets);
}
void RenderPassEncoder::setBlendConstant(const Color& color) const {
	return wgpuRenderPassEncoderSetBlendConstant(m_raw, &color);
}
void RenderPassEncoder::setIndexBuffer(Buffer buffer, IndexFormat format, uint64_t offset, uint64_t size) const {
	return wgpuRenderPassEncoderSetIndexBuffer(m_raw, buffer, static_cast<WGPUIndexFormat>(format), offset, size);
}
void RenderPassEncoder::setLabel(StringView label) const {
	return wgpuRenderPassEncoderSetLabel(m_raw, label);
}
void RenderPassEncoder::setPipeline(RenderPipeline pipeline) const {
	return wgpuRenderPassEncoderSetPipeline(m_raw, pipeline);
}
void RenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const {
	return wgpuRenderPassEncoderSetScissorRect(m_raw, x, y, width, height);
}
void RenderPassEncoder::setStencilReference(uint32_t reference) const {
	return wgpuRenderPassEncoderSetStencilReference(m_raw, reference);
}
void RenderPassEncoder::setVertexBuffer(uint32_t slot, Buffer buffer, uint64_t offset, uint64_t size) const {
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
void RenderPassEncoder::multiDrawIndirect(Buffer buffer, uint64_t offset, uint32_t count) const {
	return wgpuRenderPassEncoderMultiDrawIndirect(m_raw, buffer, offset, count);
}
void RenderPassEncoder::multiDrawIndexedIndirect(Buffer buffer, uint64_t offset, uint32_t count) const {
	return wgpuRenderPassEncoderMultiDrawIndexedIndirect(m_raw, buffer, offset, count);
}
void RenderPassEncoder::multiDrawIndirectCount(Buffer buffer, uint64_t offset, Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const {
	return wgpuRenderPassEncoderMultiDrawIndirectCount(m_raw, buffer, offset, count_buffer, count_buffer_offset, max_count);
}
void RenderPassEncoder::multiDrawIndexedIndirectCount(Buffer buffer, uint64_t offset, Buffer count_buffer, uint64_t count_buffer_offset, uint32_t max_count) const {
	return wgpuRenderPassEncoderMultiDrawIndexedIndirectCount(m_raw, buffer, offset, count_buffer, count_buffer_offset, max_count);
}
void RenderPassEncoder::beginPipelineStatisticsQuery(QuerySet querySet, uint32_t queryIndex) const {
	return wgpuRenderPassEncoderBeginPipelineStatisticsQuery(m_raw, querySet, queryIndex);
}
void RenderPassEncoder::endPipelineStatisticsQuery() const {
	return wgpuRenderPassEncoderEndPipelineStatisticsQuery(m_raw);
}
void RenderPassEncoder::writeTimestamp(QuerySet querySet, uint32_t queryIndex) const {
	return wgpuRenderPassEncoderWriteTimestamp(m_raw, querySet, queryIndex);
}


// Methods of RenderPipeline
BindGroupLayout RenderPipeline::getBindGroupLayout(uint32_t groupIndex) const {
	return wgpuRenderPipelineGetBindGroupLayout(m_raw, groupIndex);
}
void RenderPipeline::setLabel(StringView label) const {
	return wgpuRenderPipelineSetLabel(m_raw, label);
}
void RenderPipeline::addRef() const {
	return wgpuRenderPipelineAddRef(m_raw);
}
void RenderPipeline::release() const {
	return wgpuRenderPipelineRelease(m_raw);
}


// Methods of Sampler
void Sampler::setLabel(StringView label) const {
	return wgpuSamplerSetLabel(m_raw, label);
}
void Sampler::addRef() const {
	return wgpuSamplerAddRef(m_raw);
}
void Sampler::release() const {
	return wgpuSamplerRelease(m_raw);
}


// Methods of ShaderModule
void ShaderModule::setLabel(StringView label) const {
	return wgpuShaderModuleSetLabel(m_raw, label);
}
void ShaderModule::addRef() const {
	return wgpuShaderModuleAddRef(m_raw);
}
void ShaderModule::release() const {
	return wgpuShaderModuleRelease(m_raw);
}


// Methods of Surface
void Surface::configure(const SurfaceConfiguration& config) const {
	return wgpuSurfaceConfigure(m_raw, &config);
}
Status Surface::getCapabilities(Adapter adapter, SurfaceCapabilities * capabilities) const {
	return static_cast<Status>(wgpuSurfaceGetCapabilities(m_raw, adapter, capabilities));
}
void Surface::getCurrentTexture(SurfaceTexture * surfaceTexture) const {
	return wgpuSurfaceGetCurrentTexture(m_raw, surfaceTexture);
}
Status Surface::present() const {
	return static_cast<Status>(wgpuSurfacePresent(m_raw));
}
void Surface::setLabel(StringView label) const {
	return wgpuSurfaceSetLabel(m_raw, label);
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
TextureView Texture::createView(const TextureViewDescriptor& descriptor) const {
	return wgpuTextureCreateView(m_raw, &descriptor);
}
TextureView Texture::createView() const {
	return wgpuTextureCreateView(m_raw, nullptr);
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
	return wgpuTextureSetLabel(m_raw, label);
}
void Texture::addRef() const {
	return wgpuTextureAddRef(m_raw);
}
void Texture::release() const {
	return wgpuTextureRelease(m_raw);
}


// Methods of TextureView
void TextureView::setLabel(StringView label) const {
	return wgpuTextureViewSetLabel(m_raw, label);
}
void TextureView::addRef() const {
	return wgpuTextureViewAddRef(m_raw);
}
void TextureView::release() const {
	return wgpuTextureViewRelease(m_raw);
}



// Extra implementations
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
	wgpuInstanceRequestAdapter(*this, &options, callbackInfo);

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
	wgpuAdapterRequestDevice(*this, &descriptor, callbackInfo);

#if __EMSCRIPTEN__
	while (!context.requestEnded) {
		emscripten_sleep(50);
	}
#endif

	assert(context.requestEnded);
	return context.device;
}

#undef HANDLE
#undef DESCRIPTOR
#undef ENUM
#undef ENUM_ENTRY
#undef END

} // namespace wgpu
