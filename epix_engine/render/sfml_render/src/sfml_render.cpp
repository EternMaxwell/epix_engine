module;

#include <spdlog/spdlog.h>

#define WGPU_TARGET_MACOS 1
#define WGPU_TARGET_LINUX_X11 2
#define WGPU_TARGET_WINDOWS 3
#define WGPU_TARGET_LINUX_WAYLAND 4
#define WGPU_TARGET_EMSCRIPTEN 5

#if defined(__EMSCRIPTEN__)
#define WGPU_TARGET WGPU_TARGET_EMSCRIPTEN
#elif defined(_WIN32)
#define WGPU_TARGET WGPU_TARGET_WINDOWS
#elif defined(__APPLE__)
#define WGPU_TARGET WGPU_TARGET_MACOS
#elif defined(_GLFW_WAYLAND)
#define WGPU_TARGET WGPU_TARGET_LINUX_WAYLAND
#else
#define WGPU_TARGET WGPU_TARGET_LINUX_X11
#endif

#if WGPU_TARGET == WGPU_TARGET_MACOS
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#include <SFML/Window/WindowBase.hpp>
#include <SFML/Window/WindowHandle.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if WGPU_TARGET == WGPU_TARGET_WINDOWS
#include <windows.h>
#endif

#include <webgpu/webgpu.h>

module epix.sfml.render;

import std;
import epix.core;
import epix.render;
import epix.sfml.core;
import epix.window;
import webgpu;

WGPUSurface sfmlGetWGPUSurfaceRaw(WGPUInstance instance, sf::WindowBase* window) {
#if WGPU_TARGET == WGPU_TARGET_WINDOWS
    {
        HWND hwnd           = static_cast<HWND>(window->getNativeHandle());
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
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
    {
        // On X11, SFML's getNativeHandle() returns an X11 Window (unsigned long)
        // We need to get the display separately
        auto x11_window = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(window->getNativeHandle()));

        // SFML doesn't directly expose the X11 display; use the default display
        Display* x11_display = XOpenDisplay(nullptr);

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
#elif WGPU_TARGET == WGPU_TARGET_MACOS
    {
        id metal_layer      = [CAMetalLayer layer];
        NSWindow* ns_window = (__bridge NSWindow*)window->getNativeHandle();
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

wgpu::Surface sfmlGetWGPUSurface(const wgpu::Instance& instance, sf::WindowBase* window) {
    auto res = sfmlGetWGPUSurfaceRaw(instance, window);
    return std::move(*reinterpret_cast<wgpu::Surface*>(&res));
}

using namespace epix::core;

using epix::render::window::SurfaceCreation;

void epix::sfml::render::SFMLRenderPlugin::build(App& app) {
    spdlog::debug("[sfml.render] Building SFMLRenderPlugin.");
    auto system = make_system_unique(
        [](Commands commands, Query<Item<Entity>, Filter<With<epix::window::Window>, Without<SurfaceCreation>>> windows,
           ResMut<SFMLwindows> sfml_windows) {
            for (auto&& [id] : windows.iter()) {
                auto it = sfml_windows->find(id);
                if (it == sfml_windows->end()) continue;
                sf::WindowBase* sfml_window = it->second.get();
                if (!sfml_window) continue;
                commands.entity(id).insert(SurfaceCreation([sfml_window](const wgpu::Instance& instance) {
                    return sfmlGetWGPUSurface(instance, sfml_window);
                }));
            }
        });
    app.runner_scope([system = std::move(system)](SFMLRunner& runner) mutable {
           runner.set_render_app(::epix::render::Render);
           runner.append_system(std::move(system));
       })
        .transform_error([](App::RunnerError error) {
            if (error == App::RunnerError::RunnerNotSet) {
                throw std::runtime_error("SfmlRenderPlugin requires an AppRunner to be set before building");
            } else if (error == App::RunnerError::RunnerMismatch) {
                throw std::runtime_error("SfmlRenderPlugin requires a SFMLRunner as the AppRunner");
            }
            return error;
        });
}
