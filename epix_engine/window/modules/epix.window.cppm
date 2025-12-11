/**
 * @file epix.window.cppm
 * @brief Window module for window management via GLFW
 */

export module epix.window;

#include <epix/core.hpp>
#include <epix/assets.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// GLFW forward declaration
struct GLFWwindow;

export namespace epix::window {
    // Enums
    enum PosType {
        TopLeft,
        Centered,
        Relative,
    };
    
    enum class StandardCursor {
        Arrow,
        IBeam,
        Crosshair,
        Hand,
        ResizeAll,
        ResizeNS,
        ResizeEW,
        ResizeNWSE,
        ResizeNESW,
        NotAllowed,
    };
    
    enum class CursorMode {
        Normal,
        Hidden,
        Captured,
        Disabled,
    };
    
    enum class CompositeAlphaMode {
        Auto,
        Opacity,
        PreMultiplied,
        PostMultiplied,
        Inherit,
    };
    
    enum class PresentMode {
        AutoVsync,
        AutoNoVsync,
        Fifo,
        FifoRelaxed,
        Immediate,
        Mailbox,
    };
    
    enum class WindowLevel {
        AlwaysOnBottom,
        Normal,
        AlwaysOnTop,
    };
    
    enum class WindowMode {
        Windowed,
        Fullscreen,
        BorderlessFullscreen,
        SizedFullscreen,
    };
    
    // Structures
    struct FrameSize {
        int left   = 0;
        int right  = 0;
        int top    = 0;
        int bottom = 0;
    };
    
    struct SizeLimits {
        int min_width  = 160;
        int min_height = 120;
        int max_width  = -1;
        int max_height = -1;

        bool operator==(const SizeLimits& other) const;
        bool operator!=(const SizeLimits& other) const;
    };
    
    struct CursorIcon : std::variant<StandardCursor, epix::assets::UntypedHandle> {
        using std::variant<StandardCursor, epix::assets::UntypedHandle>::variant;
        using std::variant<StandardCursor, epix::assets::UntypedHandle>::operator=;
    };
    
    // Window component
    struct Window {
        GLFWwindow* handle = nullptr;
        std::string title = "Epix Window";
        int width = 800;
        int height = 600;
        // ... more fields from actual header
        
        Window();
        ~Window();
        Window(const Window&) = delete;
        Window(Window&&) noexcept;
        Window& operator=(const Window&) = delete;
        Window& operator=(Window&&) noexcept;
    };
    
    // Window events
    namespace events {
        struct WindowResized {
            epix::Entity window;
            int width;
            int height;
        };
        
        struct WindowCreated {
            epix::Entity window;
        };
        
        struct WindowClosed {
            epix::Entity window;
        };
        
        struct WindowCloseRequested {
            epix::Entity window;
        };
        
        struct WindowDestroyed {
            epix::Entity window;
        };
        
        struct CursorMoved {
            epix::Entity window;
            std::pair<double, double> position;
            std::pair<double, double> delta;
        };
        
        struct CursorEntered {
            epix::Entity window;
            bool entered;
        };
        
        struct ReceivedCharacter {
            epix::Entity window;
            char32_t character;
        };
        
        struct WindowFocused {
            epix::Entity window;
            bool focused;
        };
        
        struct FileDrop {
            epix::Entity window;
            std::vector<std::string> paths;
        };
        
        struct WindowMoved {
            epix::Entity window;
            std::pair<int, int> position;
        };
    }  // namespace events
    
    // Re-export event types
    using events::CursorEntered;
    using events::CursorMoved;
    using events::FileDrop;
    using events::ReceivedCharacter;
    using events::WindowClosed;
    using events::WindowCloseRequested;
    using events::WindowCreated;
    using events::WindowDestroyed;
    using events::WindowFocused;
    using events::WindowMoved;
    using events::WindowResized;
    
    // Window plugin
    enum class ExitCondition {
        OnAllClosed,
        OnPrimaryClosed,
        None,
    };
    
    struct WindowPlugin {
        std::optional<Window> primary_window = Window{};
        ExitCondition exit_condition         = ExitCondition::OnPrimaryClosed;
        bool close_when_requested            = true;
        void build(epix::App& app);
        void finish(epix::App& app);
    };
    
    // System function
    void log_events(epix::EventReader<events::WindowResized> resized,
                    epix::EventReader<events::WindowMoved> moved,
                    epix::EventReader<events::WindowCreated> created,
                    epix::EventReader<events::WindowClosed> closed,
                    epix::EventReader<events::WindowCloseRequested> close_requested,
                    epix::EventReader<events::WindowDestroyed> destroyed,
                    epix::EventReader<events::CursorMoved> cursor_moved,
                    epix::EventReader<events::CursorEntered> cursor_entered,
                    epix::EventReader<events::FileDrop> file_drop,
                    epix::EventReader<events::ReceivedCharacter> received_character,
                    epix::EventReader<events::WindowFocused> window_focused,
                    epix::Query<epix::Item<const Window&>> windows);
    
}  // namespace epix::window
