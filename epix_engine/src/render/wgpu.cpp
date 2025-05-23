#define WEBGPU_CPP_IMPLEMENTATION
#include <epix/wgpu.h>

#include <webgpu/webgpu.hpp>


// === GLFW === //
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#define GLFW_EXPOSE_NATIVE_EMSCRIPTEN
#ifndef GLFW_PLATFORM_EMSCRIPTEN  // not defined in older versions of emscripten
#define GLFW_PLATFORM_EMSCRIPTEN 0
#endif
#else  // __EMSCRIPTEN__
#ifdef _GLFW_X11
#define GLFW_EXPOSE_NATIVE_X11
#endif
#ifdef _GLFW_WAYLAND
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#ifdef _GLFW_COCOA
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#ifdef _GLFW_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#endif  // __EMSCRIPTEN__

#ifdef GLFW_EXPOSE_NATIVE_COCOA
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#ifndef __EMSCRIPTEN__
#include <GLFW/glfw3native.h>
#endif

EPIX_API WGPUSurface
epix::webgpu::utils::create_surface(WGPUInstance instance, GLFWwindow* window) {
#ifndef __EMSCRIPTEN__
    switch (glfwGetPlatform()) {
#else
    // glfwGetPlatform is not available in older versions of emscripten
    switch (GLFW_PLATFORM_EMSCRIPTEN) {
#endif

#ifdef GLFW_EXPOSE_NATIVE_X11
        case GLFW_PLATFORM_X11: {
            Display* x11_display = glfwGetX11Display();
            Window x11_window    = glfwGetX11Window(window);

            WGPUSurfaceSourceXlibWindow fromXlibWindow;
            fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
            fromXlibWindow.chain.next  = NULL;
            fromXlibWindow.display     = x11_display;
            fromXlibWindow.window      = x11_window;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromXlibWindow.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_X11

#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
        case GLFW_PLATFORM_WAYLAND: {
            struct wl_display* wayland_display = glfwGetWaylandDisplay();
            struct wl_surface* wayland_surface = glfwGetWaylandWindow(window);

            WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
            fromWaylandSurface.chain.sType =
                WGPUSType_SurfaceSourceWaylandSurface;
            fromWaylandSurface.chain.next = NULL;
            fromWaylandSurface.display    = wayland_display;
            fromWaylandSurface.surface    = wayland_surface;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromWaylandSurface.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_WAYLAND

#ifdef GLFW_EXPOSE_NATIVE_COCOA
        case GLFW_PLATFORM_COCOA: {
            id metal_layer      = [CAMetalLayer layer];
            NSWindow* ns_window = glfwGetCocoaWindow(window);
            [ns_window.contentView setWantsLayer:YES];
            [ns_window.contentView setLayer:metal_layer];

            WGPUSurfaceSourceMetalLayer fromMetalLayer;
            fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
            fromMetalLayer.chain.next  = NULL;
            fromMetalLayer.layer       = metal_layer;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromMetalLayer.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_COCOA

#ifdef GLFW_EXPOSE_NATIVE_WIN32
        case GLFW_PLATFORM_WIN32: {
            HWND hwnd           = glfwGetWin32Window(window);
            HINSTANCE hinstance = GetModuleHandle(NULL);

            WGPUSurfaceSourceWindowsHWND fromWindowsHWND;
            fromWindowsHWND.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
            fromWindowsHWND.chain.next  = NULL;
            fromWindowsHWND.hinstance   = hinstance;
            fromWindowsHWND.hwnd        = hwnd;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromWindowsHWND.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_WIN32

#ifdef GLFW_EXPOSE_NATIVE_EMSCRIPTEN
        case GLFW_PLATFORM_EMSCRIPTEN: {
#ifdef WEBGPU_BACKEND_DAWN
            WGPUSurfaceSourceCanvasHTMLSelector_Emscripten
                fromCanvasHTMLSelector;
            fromCanvasHTMLSelector.chain.sType =
                WGPUSType_SurfaceSourceCanvasHTMLSelector_Emscripten;
#else
            WGPUSurfaceDescriptorFromCanvasHTMLSelector fromCanvasHTMLSelector;
            fromCanvasHTMLSelector.chain.sType =
                WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
#endif
            fromCanvasHTMLSelector.chain.next = NULL;
            fromCanvasHTMLSelector.selector   = "canvas";

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromCanvasHTMLSelector.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_EMSCRIPTEN

        default:
            // Unsupported platform
            return NULL;
    }
}
