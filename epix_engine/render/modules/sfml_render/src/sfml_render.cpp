module;

#include <SFML/Window.hpp>
#include <webgpu/webgpu.h>

#if defined(_WIN32)
#include <windows.h>
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
#if defined(_WIN32)
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
#else
    throw std::runtime_error("SFMLRenderPlugin surface creation is currently implemented for Windows only");
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
