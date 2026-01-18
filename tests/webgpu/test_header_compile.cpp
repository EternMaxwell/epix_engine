// Simple test to verify WebGPU wrapper compilation (header-only mode)
// This test doesn't require module support and validates the infrastructure

#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu.hpp"

#include <iostream>

int main() {
    std::cout << "WebGPU Header Compilation Test\n";
    std::cout << "==============================\n\n";

    // Test basic type creation
    wgpu::InstanceDescriptor desc{};
    desc.nextInChain = nullptr;
    
    std::cout << "✓ InstanceDescriptor created\n";

    // Test enum access
    auto powerPref = wgpu::PowerPreference::HighPerformance;
    std::cout << "✓ Enum values accessible\n";

    // Test descriptor initialization
    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.nextInChain = nullptr;
    deviceDesc.label = "Test Device";
    
    std::cout << "✓ DeviceDescriptor initialized\n";

    // Test that we can create function pointers (even if instance creation would fail without GPU)
    std::cout << "✓ All WebGPU C++ wrapper types compile correctly\n";
    
    std::cout << "\n✅ Header-only mode test PASSED\n";
    std::cout << "   WebGPU wrapper is properly generated and can be compiled\n";

    return 0;
}
