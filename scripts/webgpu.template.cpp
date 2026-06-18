#include <webgpu/webgpu.hpp>

{{webgpu_includes}}

// Enum implementations
{{enums_impl}}
// Struct implementations
{{structs_impl}}
// Handle implementations
{{handles_impl}}
// Callback implementations
{{callbacks_impl}}
// Non member function implementations
{{functions_impl}}

namespace WEBGPU_CPP_NAMESPACE
{
#ifdef WEBGPU_CPP_USE_RAII
namespace raw
{
#endif
WEBGPU_CPP_NAMESPACE::Adapter Instance::requestAdapter(const RequestAdapterOptions& options) const {
    struct Context {
        WEBGPU_CPP_NAMESPACE::Adapter adapter = nullptr;
        bool requestEnded = false;
    };
    Context context;

    RequestAdapterCallbackInfo callbackInfo;
    callbackInfo.callback = [&](
        RequestAdapterStatus status,
        WEBGPU_CPP_NAMESPACE::Adapter adapter,
        StringView message
    ) {
        if (status == RequestAdapterStatus::eSuccess) {
            context.adapter = std::move(adapter);
        }
        else {
            std::cout << "Could not get WebGPU adapter: " << std::string_view(StringView(message)) << std::endl;
        }
        context.requestEnded = true;
    };
    callbackInfo.mode = CallbackMode::eAllowSpontaneous;
    RequestAdapterOptions::CStruct options_c;
    options.to_cstruct(&options_c);
    RequestAdapterCallbackInfo::CStruct callbackInfo_c;
    callbackInfo.to_cstruct(&callbackInfo_c);
    wgpuInstanceRequestAdapter(*this, &options_c, callbackInfo_c);

#if __EMSCRIPTEN__
    while (!context.requestEnded) {
        emscripten_sleep(50);
    }
#endif

    assert(context.requestEnded);
    return context.adapter;
}
WEBGPU_CPP_NAMESPACE::Device Adapter::requestDevice(const DeviceDescriptor& descriptor) const {
    struct Context {
        WEBGPU_CPP_NAMESPACE::Device device = nullptr;
        bool requestEnded = false;
    };
    Context context;

    RequestDeviceCallbackInfo callbackInfo;
    callbackInfo.callback = [&](
        RequestDeviceStatus status,
        WEBGPU_CPP_NAMESPACE::Device device,
        StringView message
    ) {
        if (status == RequestDeviceStatus::eSuccess) {
            context.device = std::move(device);
        }
        else {
            std::cout << "Could not get WebGPU device: " << std::string_view(StringView(message)) << std::endl;
        }
        context.requestEnded = true;
    };
    callbackInfo.mode = CallbackMode::eAllowSpontaneous;
    DeviceDescriptor::CStruct descriptor_c;
    descriptor.to_cstruct(&descriptor_c);
    RequestDeviceCallbackInfo::CStruct callbackInfo_c;
    callbackInfo.to_cstruct(&callbackInfo_c);
    wgpuAdapterRequestDevice(*this, &descriptor_c, callbackInfo_c);

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
