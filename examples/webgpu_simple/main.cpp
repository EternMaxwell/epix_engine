// Simple WebGPU example using C++20 modules
// This demonstrates creating a WebGPU instance and requesting an adapter

import std;
import webgpu;

int main() {
    std::cout << "WebGPU Simple Example with C++20 Modules\n";
    std::cout << "=========================================\n\n";

    // Create a WebGPU instance
    wgpu::InstanceDescriptor instanceDesc = wgpu::Default;
    wgpu::Instance instance = wgpu::createInstance(instanceDesc);
    
    if (!instance) {
        std::cerr << "Error: Failed to create WebGPU instance\n";
        return 1;
    }
    
    std::cout << "✓ WebGPU instance created successfully\n";

    // Request an adapter
    wgpu::RequestAdapterOptions adapterOpts = wgpu::Default;
    adapterOpts.powerPreference = wgpu::PowerPreference::HighPerformance;
    
    wgpu::Adapter adapter = nullptr;
    bool adapterReceived = false;
    
    instance.requestAdapter(
        adapterOpts,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adpt, char const* message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                adapter = std::move(adpt);
                adapterReceived = true;
                std::cout << "✓ Adapter acquired successfully\n";
            } else {
                std::cerr << "✗ Failed to acquire adapter: " << message << "\n";
            }
        }
    );

    // Poll for adapter result (in a real app, you'd integrate this with your event loop)
    if (adapterReceived && adapter) {
        // Get adapter properties
        wgpu::AdapterInfo adapterInfo;
        adapter.getInfo(&adapterInfo);
        
        std::cout << "\nAdapter Information:\n";
        std::cout << "  Vendor: " << (adapterInfo.vendor ? adapterInfo.vendor : "Unknown") << "\n";
        std::cout << "  Device: " << (adapterInfo.device ? adapterInfo.device : "Unknown") << "\n";
        std::cout << "  Backend: ";
        switch (adapterInfo.backendType) {
            case wgpu::BackendType::Vulkan: std::cout << "Vulkan\n"; break;
            case wgpu::BackendType::D3D12: std::cout << "D3D12\n"; break;
            case wgpu::BackendType::Metal: std::cout << "Metal\n"; break;
            case wgpu::BackendType::OpenGL: std::cout << "OpenGL\n"; break;
            default: std::cout << "Unknown\n"; break;
        }
        
        // Request a device
        wgpu::DeviceDescriptor deviceDesc = wgpu::Default;
        deviceDesc.label = "Main Device";
        
        wgpu::Device device = nullptr;
        bool deviceReceived = false;
        
        adapter.requestDevice(
            deviceDesc,
            [&](wgpu::RequestDeviceStatus status, wgpu::Device dev, char const* message) {
                if (status == wgpu::RequestDeviceStatus::Success) {
                    device = std::move(dev);
                    deviceReceived = true;
                    std::cout << "✓ Device created successfully\n";
                } else {
                    std::cerr << "✗ Failed to create device: " << message << "\n";
                }
            }
        );
        
        if (deviceReceived && device) {
            // Set error callback
            device.setUncapturedErrorCallback([](wgpu::ErrorType type, char const* message) {
                std::cerr << "WebGPU Error (" << static_cast<int>(type) << "): " << message << "\n";
            });
            
            std::cout << "\n✓ All WebGPU initialization successful!\n";
            std::cout << "  RAII handles are working - device will be automatically cleaned up\n";
            
            // Device will be automatically released when it goes out of scope (RAII)
            return 0;
        }
    }
    
    std::cerr << "\n✗ Failed to initialize WebGPU\n";
    return 1;
}
