/**
 * @file epix.window.cppm
 * @brief Window module for window management
 */

export module epix.window;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// GLFW forward declarations
struct GLFWwindow;

// Module imports
#include <epix/core.hpp>

export namespace epix::window {
    // Window mode
    enum class WindowMode {
        Windowed,
        Fullscreen,
        BorderlessFullscreen,
    };
    
    // Window descriptor
    struct WindowDescriptor {
        std::string title = "Epix Engine";
        uint32_t width = 1280;
        uint32_t height = 720;
        WindowMode mode = WindowMode::Windowed;
        bool resizable = true;
        bool decorated = true;
        bool vsync = true;
    };
    
    // Window events
    struct WindowResized {
        uint32_t width;
        uint32_t height;
    };
    
    struct WindowClosed {};
    
    struct WindowFocused {
        bool focused;
    };
    
    // Primary window marker
    struct PrimaryWindow {};
    
    // Window component
    struct Window {
        GLFWwindow* handle = nullptr;
        WindowDescriptor descriptor;
        
        Window() = default;
        Window(const WindowDescriptor& desc);
        ~Window();
        
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) noexcept;
        Window& operator=(Window&&) noexcept;
        
        bool should_close() const;
        void set_should_close(bool value);
        
        uint32_t width() const { return descriptor.width; }
        uint32_t height() const { return descriptor.height; }
        
        void set_title(const std::string& title);
        void set_size(uint32_t width, uint32_t height);
        
        GLFWwindow* raw_handle() const { return handle; }
    };
    
    // Window plugin
    struct WindowPlugin {
        WindowDescriptor primary_window;
        
        WindowPlugin() = default;
        WindowPlugin(WindowDescriptor desc) : primary_window(std::move(desc)) {}
        
        void build(epix::App& app);
    };
}  // namespace epix::window
