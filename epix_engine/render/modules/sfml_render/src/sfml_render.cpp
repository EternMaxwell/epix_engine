module;

#include <SFML/Window.hpp>
#include <webgpu/webgpu.h>

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
#elif defined(EPIX_SFML_WAYLAND)
#define WGPU_TARGET WGPU_TARGET_LINUX_WAYLAND
#else
#define WGPU_TARGET WGPU_TARGET_LINUX_X11
#endif

#if WGPU_TARGET == WGPU_TARGET_WINDOWS
#include <windows.h>
#elif WGPU_TARGET == WGPU_TARGET_MACOS
#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
#include <X11/Xlib.h>
#elif WGPU_TARGET == WGPU_TARGET_LINUX_WAYLAND
#include <wayland-client.h>
#endif

module epix.sfml.render;

import std;
import epix.core;
import epix.render;
import epix.sfml.core;
import epix.window;
import webgpu;

using namespace core;
using render::window::SurfaceCreation;

namespace {
WGPUSurface sfmlGetWGPUSurfaceRaw(WGPUInstance instance, sf::Window* window) {
#if WGPU_TARGET == WGPU_TARGET_WINDOWS
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
#elif WGPU_TARGET == WGPU_TARGET_MACOS
    id metal_layer      = [CAMetalLayer layer];
    NSWindow* ns_window = static_cast<NSWindow*>(window->getNativeHandle());
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
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
    static Display* display = XOpenDisplay(nullptr);
    if (!display) {
        throw std::runtime_error("Failed to open X11 display for SFMLRenderPlugin");
    }
    ::Window x11_window = static_cast<::Window>(window->getNativeHandle());

    WGPUSurfaceSourceXlibWindow fromXlibWindow;
    fromXlibWindow.chain.next  = NULL;
    fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
    fromXlibWindow.display     = display;
    fromXlibWindow.window      = x11_window;

    WGPUSurfaceDescriptor surfaceDescriptor;
    surfaceDescriptor.nextInChain = &fromXlibWindow.chain;
    surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

    return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
#elif WGPU_TARGET == WGPU_TARGET_LINUX_WAYLAND
    static wl_display* display = wl_display_connect(nullptr);
    if (!display) {
        throw std::runtime_error("Failed to connect Wayland display for SFMLRenderPlugin");
    }
    wl_surface* surface = static_cast<wl_surface*>(window->getNativeHandle());

    WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
    fromWaylandSurface.chain.next  = NULL;
    fromWaylandSurface.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
    fromWaylandSurface.display     = display;
    fromWaylandSurface.surface     = surface;

    WGPUSurfaceDescriptor surfaceDescriptor;
    surfaceDescriptor.nextInChain = &fromWaylandSurface.chain;
    surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

    return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
#elif WGPU_TARGET == WGPU_TARGET_EMSCRIPTEN
    WGPUSurfaceSourceCanvasHTMLSelector fromCanvasHTMLSelector;
    fromCanvasHTMLSelector.chain.next  = NULL;
    fromCanvasHTMLSelector.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
    fromCanvasHTMLSelector.selector    = "canvas";

    WGPUSurfaceDescriptor surfaceDescriptor;
    surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
    surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

    return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
#else
#error "Unsupported WGPU_TARGET"
#endif
}

wgpu::Surface sfmlGetWGPUSurface(const wgpu::Instance& instance, sf::Window* window) {
    auto res = sfmlGetWGPUSurfaceRaw(instance, window);
    return std::move(*reinterpret_cast<wgpu::Surface*>(&res));
}
}  // namespace

void sfml::render::SFMLRenderPlugin::build(App& app) {
    auto system = make_system_unique(
        [](Commands commands, Query<Item<Entity>, Filter<With<window::Window>, Without<SurfaceCreation>>> windows,
           ResMut<SFMLwindows> sfml_windows) {
            for (auto&& [id] : windows.iter()) {
                auto it = sfml_windows->find(id);
                if (it == sfml_windows->end()) continue;
                sf::Window* sfml_window = it->second.get();
                if (!sfml_window) continue;
                commands.entity(id).insert(SurfaceCreation([sfml_window](const wgpu::Instance& instance) {
                    return sfmlGetWGPUSurface(instance, sfml_window);
                }));
            }
        });
    app.runner_scope([system = std::move(system)](SFMLRunner& runner) mutable {
           runner.set_render_app(::render::Render);
           runner.append_system(std::move(system));
       })
        .transform_error([](App::RunnerError error) {
            if (error == App::RunnerError::RunnerNotSet) {
                throw std::runtime_error("SFMLRenderPlugin requires an AppRunner to be set before building");
            } else if (error == App::RunnerError::RunnerMismatch) {
                throw std::runtime_error("SFMLRenderPlugin requires an SFMLRunner as the AppRunner");
            }
            return error;
        });
}
