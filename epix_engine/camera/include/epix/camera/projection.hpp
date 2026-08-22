#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <concepts>
#include <cstddef>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>
#include <variant>
#endif

namespace epix::camera {

/** @brief Scaling mode controlling how an orthographic projection adapts to
 * the viewport size.
 *
 * Constructed via static factory methods (e.g. fixed(), window_size(),
 * auto_min()).
 */
EPIX_EXPORT struct ScalingMode {
   private:
    enum class Mode {
        Fixed,
        WindowSize,
        AutoMin,
        AutoMax,
        FixedVertical,
        FixedHorizontal,
    } mode;
    union {
        struct {
            float width;
            float height;
        } _fixed;
        struct {
            float pixels_per_unit;
        } _window_size;
        struct {
            float min_width;
            float min_height;
        } _auto_min;
        struct {
            float max_width;
            float max_height;
        } _auto_max;
        struct {
            float vertical;
        } _fixed_vertical;
        struct {
            float horizontal;
        } _fixed_horizontal;
    };

   public:
    /** @brief A fixed size in world units. */
    static ScalingMode fixed(float width, float height) {
        ScalingMode m;
        m.mode        = Mode::Fixed;
        m._fixed      = {width, height};
        return m;
    }
    /** @brief Window size in pixels divided by the pixels-per-unit scale. */
    static ScalingMode window_size(float pixels_per_unit) {
        ScalingMode m;
        m.mode           = Mode::WindowSize;
        m._window_size   = {pixels_per_unit};
        return m;
    }
    /** @brief Fit the window while respecting minimum bounds. */
    static ScalingMode auto_min(float min_width, float min_height) {
        ScalingMode m;
        m.mode      = Mode::AutoMin;
        m._auto_min = {min_width, min_height};
        return m;
    }
    /** @brief Fit the window while respecting maximum bounds. */
    static ScalingMode auto_max(float max_width, float max_height) {
        ScalingMode m;
        m.mode      = Mode::AutoMax;
        m._auto_max = {max_width, max_height};
        return m;
    }
    /** @brief Fixed vertical extent; horizontal scales with the aspect ratio. */
    static ScalingMode fixed_vertical(float vertical) {
        ScalingMode m;
        m.mode             = Mode::FixedVertical;
        m._fixed_vertical  = {vertical};
        return m;
    }
    /** @brief Fixed horizontal extent; vertical scales with the aspect ratio. */
    static ScalingMode fixed_horizontal(float horizontal) {
        ScalingMode m;
        m.mode              = Mode::FixedHorizontal;
        m._fixed_horizontal = {horizontal};
        return m;
    }

    template <std::invocable<float&, float&> Func>
    ScalingMode& on_fixed(Func&& func) {
        if (mode == Mode::Fixed) {
            func(_fixed.width, _fixed.height);
        }
        return *this;
    }
    template <std::invocable<float&> Func>
    ScalingMode& on_window_size(Func&& func) {
        if (mode == Mode::WindowSize) {
            func(_window_size.pixels_per_unit);
        }
        return *this;
    }
    template <std::invocable<float&, float&> Func>
    ScalingMode& on_auto_min(Func&& func) {
        if (mode == Mode::AutoMin) {
            func(_auto_min.min_width, _auto_min.min_height);
        }
        return *this;
    }
    template <std::invocable<float&, float&> Func>
    ScalingMode& on_auto_max(Func&& func) {
        if (mode == Mode::AutoMax) {
            func(_auto_max.max_width, _auto_max.max_height);
        }
        return *this;
    }
    template <std::invocable<float&> Func>
    ScalingMode& on_fixed_vertical(Func&& func) {
        if (mode == Mode::FixedVertical) {
            func(_fixed_vertical.vertical);
        }
        return *this;
    }
    template <std::invocable<float&> Func>
    ScalingMode& on_fixed_horizontal(Func&& func) {
        if (mode == Mode::FixedHorizontal) {
            func(_fixed_horizontal.horizontal);
        }
        return *this;
    }
};

/** @brief Orthographic camera projection with configurable scaling, near/far
 * planes, and viewport origin. */
EPIX_EXPORT struct OrthographicProjection {
    float near_plane          = -1000.0f;  // Near clipping plane
    float far_plane           = 1000.0f;   // Far clipping plane
    ScalingMode scaling_mode  = ScalingMode::window_size(1.0f);
    float scale               = 1.0f;                   // Additional scale factor
    glm::vec2 viewport_origin = glm::vec2(0.5f, 0.5f);  // Viewport origin (0 to 1)
    struct {
        float left   = -1.0f;
        float right  = 1.0f;
        float bottom = -1.0f;
        float top    = 1.0f;
    } rect;

    /** @brief Update the projection for new viewport dimensions. */
    void update(float width, float height);
    /** @brief Get the far clipping plane distance. */
    float get_far() const { return far_plane; }
    /** @brief Get the near clipping plane distance. */
    float get_near() const { return near_plane; }
    /** @brief Set the far clipping plane distance. */
    void set_far(float far_plane) { this->far_plane = far_plane; }
    /** @brief Set the near clipping plane distance. */
    void set_near(float near_plane) { this->near_plane = near_plane; }
    /** @brief Compute the orthographic projection matrix. */
    glm::mat4 get_projection_matrix() const {
        return glm::orthoLH(rect.left, rect.right, rect.bottom, rect.top, near_plane, far_plane);
    }
    /** @brief Compute the 8 corners of the view frustum. */
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return {glm::vec3(rect.left, rect.bottom, near_plane), glm::vec3(rect.right, rect.bottom, near_plane),
                glm::vec3(rect.right, rect.top, near_plane),   glm::vec3(rect.left, rect.top, near_plane),
                glm::vec3(rect.left, rect.bottom, far_plane),  glm::vec3(rect.right, rect.bottom, far_plane),
                glm::vec3(rect.right, rect.top, far_plane),    glm::vec3(rect.left, rect.top, far_plane)};
    }
};

/** @brief Perspective camera projection with field of view, aspect ratio,
 * and near/far planes. */
EPIX_EXPORT struct PerspectiveProjection {
    float fov          = glm::radians(45.0f);  // Field of view in radians
    float aspect_ratio = 1.0f;                 // Aspect ratio (width / height)
    float near_plane   = 0.1f;                 // Near clipping plane
    float far_plane    = 1000.0f;              // Far clipping plane

    /** @brief Update the aspect ratio from viewport dimensions. */
    void update(float width, float height) { aspect_ratio = width / height; }
    /** @brief Get the far clipping plane distance. */
    float get_far() const { return far_plane; }
    /** @brief Get the near clipping plane distance. */
    float get_near() const { return near_plane; }
    /** @brief Set the far clipping plane distance. */
    void set_far(float far_plane) { this->far_plane = far_plane; }
    /** @brief Set the near clipping plane distance. */
    void set_near(float near_plane) { this->near_plane = near_plane; }
    /** @brief Compute the perspective projection matrix. */
    glm::mat4 get_projection_matrix() const { return glm::perspectiveLH(fov, aspect_ratio, near_plane, far_plane); }
    /** @brief Compute the 8 corners of the perspective frustum. */
    std::array<glm::vec3, 8> get_frustum_corners() const {
        float tan_half_fov = glm::tan(fov / 2.0f);
        float near_height  = near_plane * tan_half_fov;
        float near_width   = near_height * aspect_ratio;
        float far_height   = far_plane * tan_half_fov;
        float far_width    = far_height * aspect_ratio;

        return {glm::vec3(-near_width, -near_height, near_plane), glm::vec3(near_width, -near_height, near_plane),
                glm::vec3(near_width, near_height, near_plane),   glm::vec3(-near_width, near_height, near_plane),
                glm::vec3(-far_width, -far_height, far_plane),    glm::vec3(far_width, -far_height, far_plane),
                glm::vec3(far_width, far_height, far_plane),      glm::vec3(-far_width, far_height, far_plane)};
    }
};

template <typename T>
concept CameraProjection = requires(T t) {
    { t.get_projection_matrix() } -> std::convertible_to<glm::mat4>;
    { t.get_frustum_corners() } -> std::convertible_to<std::array<glm::vec3, 8>>;
    { t.get_far() } -> std::convertible_to<float>;
    { t.get_near() } -> std::convertible_to<float>;
    { t.set_far(std::declval<float>()) };
    { t.set_near(std::declval<float>()) };
    { t.update(std::declval<float>(), std::declval<float>()) };
};

/** @brief Variant projection type wrapping orthographic or perspective. */
EPIX_EXPORT struct Projection {
    std::variant<OrthographicProjection, PerspectiveProjection> projection;

    Projection() : projection(OrthographicProjection{}) {}
    Projection(const OrthographicProjection& ortho) : projection(ortho) {}
    Projection(const PerspectiveProjection& perspective) : projection(perspective) {}

    /** @brief Create an orthographic projection. */
    static Projection orthographic(const OrthographicProjection& ortho = {}) { return Projection(ortho); }

    /** @brief Create a perspective projection. */
    static Projection perspective(const PerspectiveProjection& perspective = {}) { return Projection(perspective); }

    /** @brief Get the projection matrix from the active variant. */
    glm::mat4 get_projection_matrix() const {
        return std::visit([](const auto& proj) { return proj.get_projection_matrix(); }, projection);
    }
    /** @brief Get the far clipping plane distance. */
    float get_far() const {
        return std::visit([](const auto& proj) { return proj.get_far(); }, projection);
    }
    /** @brief Get the near clipping plane distance. */
    float get_near() const {
        return std::visit([](const auto& proj) { return proj.get_near(); }, projection);
    }
    /** @brief Set the far clipping plane distance. */
    void set_far(float far_plane) {
        std::visit([far_plane](auto& proj) { proj.set_far(far_plane); }, projection);
    }
    /** @brief Set the near clipping plane distance. */
    void set_near(float near_plane) {
        std::visit([near_plane](auto& proj) { proj.set_near(near_plane); }, projection);
    }
    /** @brief Update the active projection for new viewport dimensions. */
    void update(float width, float height) {
        std::visit([width, height](auto& proj) { proj.update(width, height); }, projection);
    }
    /** @brief Compute the 8 frustum corner points. */
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return std::visit([](const auto& proj) { return proj.get_frustum_corners(); }, projection);
    }
    /** @brief Try to get a mutable pointer to the orthographic projection. */
    std::optional<OrthographicProjection*> as_orthographic() {
        if (auto ptr = std::get_if<OrthographicProjection>(&projection)) {
            return ptr;
        }
        return std::nullopt;
    }
    /** @brief Try to get a const pointer to the orthographic projection. */
    std::optional<const OrthographicProjection*> as_orthographic() const {
        if (auto ptr = std::get_if<OrthographicProjection>(&projection)) {
            return ptr;
        }
        return std::nullopt;
    }
    /** @brief Try to get a mutable pointer to the perspective projection. */
    std::optional<PerspectiveProjection*> as_perspective() {
        if (auto ptr = std::get_if<PerspectiveProjection>(&projection)) {
            return ptr;
        }
        return std::nullopt;
    }
    /** @brief Try to get a const pointer to the perspective projection. */
    std::optional<const PerspectiveProjection*> as_perspective() const {
        if (auto ptr = std::get_if<PerspectiveProjection>(&projection)) {
            return ptr;
        }
        return std::nullopt;
    }
};

}  // namespace epix::camera
