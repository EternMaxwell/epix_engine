module;

#define WGPU_TARGET_MACOS 1
#define WGPU_TARGET_LINUX 2
#define WGPU_TARGET_WINDOWS 3
#define WGPU_TARGET_EMSCRIPTEN 5

#if defined(__EMSCRIPTEN__)
#define WGPU_TARGET WGPU_TARGET_EMSCRIPTEN
#elif defined(_WIN32)
#define WGPU_TARGET WGPU_TARGET_WINDOWS
#elif defined(__APPLE__)
#define WGPU_TARGET WGPU_TARGET_MACOS
#else
#define WGPU_TARGET WGPU_TARGET_LINUX
#endif

#if WGPU_TARGET == WGPU_TARGET_MACOS
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif  // __EMSCRIPTEN__

#if WGPU_TARGET == WGPU_TARGET_MACOS
#define GLFW_EXPOSE_NATIVE_COCOA
#elif WGPU_TARGET == WGPU_TARGET_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#if !defined(__EMSCRIPTEN__)
#include <GLFW/glfw3native.h>
#endif
#include <spdlog/spdlog.h>
#include <webgpu/webgpu.h>

module epix.glfw.render;

import std;
import epix.core;
import epix.render;
import epix.glfw.core;
import epix.window;
import webgpu;

WGPUSurface glfwGetWGPUSurfaceRaw(WGPUInstance instance, GLFWwindow* window) {
#if WGPU_TARGET == WGPU_TARGET_MACOS
    {
        id metal_layer      = [CAMetalLayer layer];
        NSWindow* ns_window = glfwGetCocoaWindow(window);
        [ns_window.contentView setWantsLayer:YES];
        [ns_window.contentView setLayer:metal_layer];

        WGPUSurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.next  = NULL;
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromMetalLayer.layer       = metal_layer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif WGPU_TARGET == WGPU_TARGET_LINUX
    {
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_X11: {
                Display* x11_display = glfwGetX11Display();
                Window x11_window    = glfwGetX11Window(window);
                if (x11_display == nullptr || x11_window == 0) {
                    throw std::runtime_error("GLFW selected X11, but no X11 display/window handle is available");
                }

                WGPUSurfaceSourceXlibWindow fromXlibWindow;
                fromXlibWindow.chain.next  = NULL;
                fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
                fromXlibWindow.display     = x11_display;
                fromXlibWindow.window      = x11_window;

                WGPUSurfaceDescriptor surfaceDescriptor;
                surfaceDescriptor.nextInChain = &fromXlibWindow.chain;
                surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

                return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
            }
            case GLFW_PLATFORM_WAYLAND: {
                wl_display* wayland_display = glfwGetWaylandDisplay();
                wl_surface* wayland_surface = glfwGetWaylandWindow(window);
                if (wayland_display == nullptr || wayland_surface == nullptr) {
                    throw std::runtime_error(
                        "GLFW selected Wayland, but no Wayland display/surface handle is available");
                }

                WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
                fromWaylandSurface.chain.next  = NULL;
                fromWaylandSurface.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
                fromWaylandSurface.display     = wayland_display;
                fromWaylandSurface.surface     = wayland_surface;

                WGPUSurfaceDescriptor surfaceDescriptor;
                surfaceDescriptor.nextInChain = &fromWaylandSurface.chain;
                surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

                return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
            }
            default:
                throw std::runtime_error("GLFW selected an unsupported Linux platform for WebGPU surface creation");
        }
    }
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
    {
        HWND hwnd           = glfwGetWin32Window(window);
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUSurfaceSourceWindowsHWND fromWindowsHWND;
        fromWindowsHWND.chain.next  = NULL;
        fromWindowsHWND.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        fromWindowsHWND.hinstance   = hinstance;
        fromWindowsHWND.hwnd        = hwnd;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWindowsHWND.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif WGPU_TARGET == WGPU_TARGET_EMSCRIPTEN
    {
        WGPUSurfaceSourceCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.next  = NULL;
        fromCanvasHTMLSelector.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
        fromCanvasHTMLSelector.selector    = "canvas";

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#else
#error "Unsupported WGPU_TARGET"
#endif
}

wgpu::Surface glfwGetWGPUSurface(const wgpu::Instance& instance, GLFWwindow* window) {
    auto res = glfwGetWGPUSurfaceRaw(instance, window);
    return std::move(*reinterpret_cast<wgpu::Surface*>(&res));
}

using namespace epix::core;

using epix::render::window::SurfaceCreation;

void epix::glfw::render::GLFWRenderPlugin::build(App& app) {
    spdlog::debug("[glfw.render] Building GLFWRenderPlugin.");
    auto system = make_system_unique(
        [](Commands commands, Query<Item<Entity>, Filter<With<epix::window::Window>, Without<SurfaceCreation>>> windows,
           ResMut<GLFWwindows> glfw_windows) {
            for (auto&& [id] : windows.iter()) {
                auto it = glfw_windows->find(id);
                if (it == glfw_windows->end()) continue;
                GLFWwindow* glfw_window = it->second;
                if (!glfw_window) continue;
                commands.entity(id).insert(SurfaceCreation([glfw_window](const wgpu::Instance& instance) {
                    return glfwGetWGPUSurface(instance, glfw_window);
                }));
            }
        });
    auto res = app.runner_scope([system = std::move(system)](GLFWRunner& runner) mutable {
                      runner.set_render_app(::epix::render::Render);
                      runner.append_system(std::move(system));
                  })
                   .transform_error([](App::RunnerError error) {
                       if (error == App::RunnerError::RunnerNotSet) {
                           throw std::runtime_error("GlfwRenderPlugin requires an AppRunner to be set before building");
                       } else if (error == App::RunnerError::RunnerMismatch) {
                           throw std::runtime_error("GlfwRenderPlugin requires a GLFWRunner as the AppRunner");
                       }
                       return error;
                   });
}
