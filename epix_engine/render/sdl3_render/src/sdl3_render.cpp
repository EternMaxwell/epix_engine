
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
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <webgpu/webgpu.h>

#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/window.hpp>

WGPUSurface sdl3GetWGPUSurfaceRaw(WGPUInstance instance, SDL_Window* window) {
#if WGPU_TARGET == WGPU_TARGET_MACOS
    {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        NSWindow* ns_window =
            (NSWindow*)SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
        if (ns_window == nil) {
            throw std::runtime_error("SDL3 window has no Cocoa window handle");
        }
        id metal_layer = [CAMetalLayer layer];
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
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
            Display* x11_display = (Display*)SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            Uint64 x11_window    = (Uint64)SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
            if (x11_display == nullptr || x11_window == 0) {
                throw std::runtime_error("SDL3 selected X11, but no X11 display/window handle is available");
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
        if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
            void* wayland_display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            void* wayland_surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            if (wayland_display == nullptr || wayland_surface == nullptr) {
                throw std::runtime_error("SDL3 selected Wayland, but no Wayland display/surface handle is available");
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
        throw std::runtime_error("SDL3 selected an unsupported Linux platform for WebGPU surface creation");
    }
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
    {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        void* hwnd                  = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        void* hinstance = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
        if (hwnd == nullptr) {
            throw std::runtime_error("SDL3 window has no Win32 window handle");
        }
        if (hinstance == nullptr) {
            hinstance = GetModuleHandle(NULL);
        }

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

wgpu::Surface sdl3GetWGPUSurface(const wgpu::Instance& instance, SDL_Window* window) {
    auto res = sdl3GetWGPUSurfaceRaw(instance, window);
    return std::move(*reinterpret_cast<wgpu::Surface*>(&res));
}

wgpu::Surface epix::sdl3::render::get_wgpu_surface(const wgpu::Instance& instance, SDL_Window* window) {
    return sdl3GetWGPUSurface(instance, window);
}

using namespace epix::ecs;
using namespace epix::app;

using epix::render::window::SurfaceCreation;

void epix::sdl3::render::SDL3RenderPlugin::attach(App& app) {
    spdlog::debug("[sdl3.render] Attaching SDL3RenderPlugin.");
    auto system = make_system_unique(
        [](Commands commands, Query<Item<Entity>, Filter<With<epix::window::Window>, Without<SurfaceCreation>>> windows,
           ResMut<SDL3windows> sdl_windows) {
            for (auto&& [id] : windows.iter()) {
                auto it = sdl_windows->find(id);
                if (it == sdl_windows->end()) continue;
                SDL_Window* sdl_window = it->second;
                if (!sdl_window) continue;
                commands.entity(id).insert(SurfaceCreation([sdl_window](const wgpu::Instance& instance) {
                    return sdl3GetWGPUSurface(instance, sdl_window);
                }));
            }
        });
    auto res = app.runner_scope([system = std::move(system)](SDL3Runner& runner) mutable {
                      runner.set_render_app(::epix::render::Render);
                      runner.append_system(std::move(system));
                  })
                   .transform_error([](App::RunnerError error) {
                       if (error == App::RunnerError::RunnerNotSet) {
                           throw std::runtime_error("SDL3RenderPlugin requires an AppRunner to be set before building");
                       } else if (error == App::RunnerError::RunnerMismatch) {
                           throw std::runtime_error("SDL3RenderPlugin requires an SDL3Runner as the AppRunner");
                       }
                       return error;
                   });
}
