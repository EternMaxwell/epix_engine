#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#endif

namespace epix::transform {
struct GlobalTransform;
}

namespace epix::camera {

struct Frustum;

/** @brief A viewport-sized slice of a larger camera image (Bevy
 * SubCameraView). */
EPIX_EXPORT struct SubCameraView {
    glm::uvec2 full_size{1, 1};
    glm::vec2 offset{0.0f, 0.0f};
    glm::uvec2 size{1, 1};
    bool operator==(const SubCameraView&) const = default;
};

/** @brief Remaps a full-view clip matrix to a sub-view's NDC rectangle. */
inline glm::mat4 crop_to_sub_view(const glm::mat4& projection, const SubCameraView& sub_view) {
    if (sub_view.full_size.x == 0 || sub_view.full_size.y == 0 || sub_view.size.x == 0 || sub_view.size.y == 0) {
        return projection;
    }
    const glm::vec2 full_size(sub_view.full_size);
    const glm::vec2 sub_size(sub_view.size);
    const glm::vec2 scale = full_size / sub_size;
    // Input offsets use a top-left origin while clip-space Y points upward.
    const glm::vec2 center = glm::vec2(2.0f * (sub_view.offset.x + sub_size.x * 0.5f) / full_size.x - 1.0f,
                                       1.0f - 2.0f * (sub_view.offset.y + sub_size.y * 0.5f) / full_size.y);
    glm::mat4 crop(1.0f);
    crop[0][0] = scale.x;
    crop[1][1] = scale.y;
    crop[3][0] = -center.x * scale.x;
    crop[3][1] = -center.y * scale.y;
    return crop * projection;
}

namespace detail {
struct ScalingModeWindowSize {};
struct ScalingModeFixed {
    float width;
    float height;
};
struct ScalingModeAutoMin {
    float min_width;
    float min_height;
};
struct ScalingModeAutoMax {
    float max_width;
    float max_height;
};
struct ScalingModeFixedVertical {
    float viewport_height;
};
struct ScalingModeFixedHorizontal {
    float viewport_width;
};
}  // namespace detail

/** @brief Scaling mode controlling how an orthographic projection adapts to
 * the viewport size (Bevy `ScalingMode`). */
EPIX_EXPORT struct ScalingMode : std::variant<detail::ScalingModeWindowSize,
                                              detail::ScalingModeFixed,
                                              detail::ScalingModeAutoMin,
                                              detail::ScalingModeAutoMax,
                                              detail::ScalingModeFixedVertical,
                                              detail::ScalingModeFixedHorizontal> {
    using WindowSize      = detail::ScalingModeWindowSize;
    using Fixed           = detail::ScalingModeFixed;
    using AutoMin         = detail::ScalingModeAutoMin;
    using AutoMax         = detail::ScalingModeAutoMax;
    using FixedVertical   = detail::ScalingModeFixedVertical;
    using FixedHorizontal = detail::ScalingModeFixedHorizontal;

   private:
    using Base = std::variant<WindowSize, Fixed, AutoMin, AutoMax, FixedVertical, FixedHorizontal>;

   public:
    using Base::Base;
    constexpr ScalingMode() noexcept : Base(WindowSize{}) {}
};

/** @brief Orthographic camera projection with configurable scaling, near/far
 * planes, and viewport origin. */
EPIX_EXPORT struct OrthographicProjection {
    float near_plane = 0.0f;     // Bevy default_3d near clipping plane
    float far_plane  = 1000.0f;  // Far clipping plane
    ScalingMode scaling_mode{};
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
    /** @brief Bevy's default orthographic projection for 2D rendering. */
    static OrthographicProjection default_2d() {
        auto projection       = default_3d();
        projection.near_plane = -1000.0f;
        return projection;
    }
    /** @brief Bevy's default orthographic projection for 3D rendering. */
    static OrthographicProjection default_3d() { return OrthographicProjection{}; }
    /** @brief Compute the right-handed reverse-Z orthographic matrix used by
     * Bevy (`Mat4::orthographic_rh(..., far, near)`). */
    glm::mat4 get_clip_from_view() const {
        return glm::orthoRH_ZO(rect.left, rect.right, rect.bottom, rect.top, far_plane, near_plane);
    }
    /** @brief Projection cropped to a sub-camera view (Bevy
     * get_clip_from_view_for_sub). */
    glm::mat4 get_clip_from_view_for_sub(const SubCameraView& sub_view) const {
        return crop_to_sub_view(get_clip_from_view(), sub_view);
    }
    float get_far() const { return far_plane; }
    /** @brief Compute frustum corners at the caller-provided view-space depths. */
    std::array<glm::vec3, 8> get_frustum_corners(float z_near, float z_far) const {
        return {glm::vec3(rect.right, rect.bottom, z_near), glm::vec3(rect.right, rect.top, z_near),
                glm::vec3(rect.left, rect.top, z_near),    glm::vec3(rect.left, rect.bottom, z_near),
                glm::vec3(rect.right, rect.bottom, z_far), glm::vec3(rect.right, rect.top, z_far),
                glm::vec3(rect.left, rect.top, z_far),     glm::vec3(rect.left, rect.bottom, z_far)};
    }
    /** @brief Compute this projection's world-space frustum (Bevy
     * `CameraProjection::compute_frustum`). */
    Frustum compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const;
};

/** @brief Perspective camera projection with field of view, aspect ratio,
 * and near/far planes. */
EPIX_EXPORT struct PerspectiveProjection {
    float fov          = glm::radians(45.0f);  // Field of view in radians
    float aspect_ratio = 1.0f;                 // Aspect ratio (width / height)
    float near_plane   = 0.1f;                 // Near clipping plane
    float far_plane    = 1000.0f;              // Far clipping plane
    /** @brief Optional oblique near clipping plane in view space. Defaults
     * to Bevy's ordinary near plane `(0, 0, -1, -near)`. */
    glm::vec4 near_clip_plane{0.0f, 0.0f, -1.0f, -0.1f};

    /** @brief Update the aspect ratio from viewport dimensions. */
    void update(float width, float height) { aspect_ratio = width / height; }
    /** @brief Compute Bevy's infinite right-handed reverse-Z perspective
     * projection, including its optional oblique clip-plane adjustment. */
    glm::mat4 get_clip_from_view() const {
        const float f = 1.0f / std::tan(fov * 0.5f);
        glm::mat4 matrix(0.0f);
        matrix[0][0] = f / aspect_ratio;
        matrix[1][1] = f;
        matrix[2][3] = -1.0f;
        matrix[3][2] = near_plane;
        const glm::vec4 default_plane{0.0f, 0.0f, -1.0f, -near_plane};
        if (near_clip_plane == default_plane) return matrix;
        const glm::vec4 q_prime{std::copysign(1.0f, near_clip_plane.x), std::copysign(1.0f, near_clip_plane.y), 0.0f,
                                1.0f};
        const glm::vec4 q       = glm::inverse(matrix) * q_prime;
        const float denominator = glm::dot(near_clip_plane, q);
        if (denominator == 0.0f || !std::isfinite(denominator)) return matrix;
        const glm::vec4 third_row = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f) - near_clip_plane * (-q.z / denominator);
        matrix[0][2]              = third_row.x;
        matrix[1][2]              = third_row.y;
        matrix[2][2]              = third_row.z;
        matrix[3][2]              = third_row.w;
        return matrix;
    }
    /** @brief Projection cropped to a sub-camera view (Bevy
     * get_clip_from_view_for_sub). */
    glm::mat4 get_clip_from_view_for_sub(const SubCameraView& sub_view) const {
        if (sub_view.full_size.x == 0 || sub_view.full_size.y == 0 || sub_view.size.x == 0 || sub_view.size.y == 0) {
            return get_clip_from_view();
        }
        const float full_width  = static_cast<float>(sub_view.full_size.x);
        const float full_height = static_cast<float>(sub_view.full_size.y);
        const float sub_width   = static_cast<float>(sub_view.size.x);
        const float sub_height  = static_cast<float>(sub_view.size.y);
        const float offset_x    = sub_view.offset.x;
        // Bevy sub-view offsets are top-left based while view-space Y is up.
        const float offset_y     = full_height - (sub_view.offset.y + sub_height);
        const float top          = near_plane * std::tan(0.5f * fov);
        const float bottom       = -top;
        const float right        = top * (full_width / full_height);
        const float left         = -right;
        const float left_prime   = left + (right - left) * offset_x / full_width;
        const float right_prime  = left + (right - left) * (offset_x + sub_width) / full_width;
        const float bottom_prime = bottom + (top - bottom) * offset_y / full_height;
        const float top_prime    = bottom + (top - bottom) * (offset_y + sub_height) / full_height;
        glm::mat4 matrix(0.0f);
        matrix[0][0] = 2.0f * near_plane / (right_prime - left_prime);
        matrix[1][1] = 2.0f * near_plane / (top_prime - bottom_prime);
        matrix[2][0] = (right_prime + left_prime) / (right_prime - left_prime);
        matrix[2][1] = (top_prime + bottom_prime) / (top_prime - bottom_prime);
        matrix[2][3] = -1.0f;
        matrix[3][2] = near_plane;
        const glm::vec4 default_plane{0.0f, 0.0f, -1.0f, -near_plane};
        if (near_clip_plane == default_plane) return matrix;
        const glm::vec4 q_prime{std::copysign(1.0f, near_clip_plane.x), std::copysign(1.0f, near_clip_plane.y), 0.0f,
                                1.0f};
        const glm::vec4 q       = glm::inverse(matrix) * q_prime;
        const float denominator = glm::dot(near_clip_plane, q);
        if (denominator == 0.0f || !std::isfinite(denominator)) return matrix;
        const glm::vec4 third_row = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f) - near_clip_plane * (-q.z / denominator);
        matrix[0][2]              = third_row.x;
        matrix[1][2]              = third_row.y;
        matrix[2][2]              = third_row.z;
        matrix[3][2]              = third_row.w;
        return matrix;
    }
    float get_far() const { return far_plane; }
    /** @brief Compute frustum corners at the caller-provided view-space depths. */
    std::array<glm::vec3, 8> get_frustum_corners(float z_near, float z_far) const {
        float tan_half_fov = glm::tan(fov / 2.0f);
        float near_height  = std::abs(z_near) * tan_half_fov;
        float near_width   = near_height * aspect_ratio;
        float far_height   = std::abs(z_far) * tan_half_fov;
        float far_width    = far_height * aspect_ratio;

        return {glm::vec3(near_width, -near_height, z_near), glm::vec3(near_width, near_height, z_near),
                glm::vec3(-near_width, near_height, z_near), glm::vec3(-near_width, -near_height, z_near),
                glm::vec3(far_width, -far_height, z_far),    glm::vec3(far_width, far_height, z_far),
                glm::vec3(-far_width, far_height, z_far),    glm::vec3(-far_width, -far_height, z_far)};
    }
    /** @brief Compute this projection's world-space frustum (Bevy
     * `CameraProjection::compute_frustum`). */
    Frustum compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const;
};

template <typename T>
concept CameraProjection = requires(T t) {
    { t.get_clip_from_view() } -> std::convertible_to<glm::mat4>;
    { t.get_clip_from_view_for_sub(std::declval<const SubCameraView&>()) } -> std::convertible_to<glm::mat4>;
    { t.get_frustum_corners(std::declval<float>(), std::declval<float>()) } -> std::convertible_to<std::array<glm::vec3, 8>>;
    { t.get_far() } -> std::convertible_to<float>;
    { t.update(std::declval<float>(), std::declval<float>()) };
};

/** @brief Type-erased custom camera projection (Bevy `CustomProjection`).
 * The stored projection is cloned on copy, so copying a `Projection` retains
 * ordinary component value semantics. */
struct DynCameraProjection {
    virtual ~DynCameraProjection()                                          = default;
    virtual std::shared_ptr<DynCameraProjection> clone() const              = 0;
    virtual glm::mat4 clip_from_view() const                                = 0;
    virtual glm::mat4 clip_from_view_for_sub(const SubCameraView&) const    = 0;
    virtual std::array<glm::vec3, 8> frustum_corners(float, float) const    = 0;
    virtual float far_value() const                                         = 0;
    virtual void update_projection(float, float)                            = 0;
};

template <CameraProjection P>
    requires std::copy_constructible<P>
struct DynCameraProjectionImpl final : DynCameraProjection {
    P value;
    explicit DynCameraProjectionImpl(P projection) : value(std::move(projection)) {}
    std::shared_ptr<DynCameraProjection> clone() const override {
        return std::make_shared<DynCameraProjectionImpl>(value);
    }
    glm::mat4 clip_from_view() const override { return value.get_clip_from_view(); }
    glm::mat4 clip_from_view_for_sub(const SubCameraView& sub_view) const override {
        return value.get_clip_from_view_for_sub(sub_view);
    }
    std::array<glm::vec3, 8> frustum_corners(float z_near, float z_far) const override {
        return value.get_frustum_corners(z_near, z_far);
    }
    float far_value() const override { return value.get_far(); }
    void update_projection(float width, float height) override { value.update(width, height); }
};

EPIX_EXPORT struct CustomProjection {
   private:
    std::shared_ptr<DynCameraProjection> dyn_projection;

   public:
    CustomProjection()
        : dyn_projection(std::make_shared<DynCameraProjectionImpl<PerspectiveProjection>>(PerspectiveProjection{})) {}
    CustomProjection(const CustomProjection& other)
        : dyn_projection(other.dyn_projection ? other.dyn_projection->clone() : nullptr) {}
    CustomProjection(CustomProjection&&) noexcept = default;
    CustomProjection& operator=(const CustomProjection& other) {
        dyn_projection = other.dyn_projection ? other.dyn_projection->clone() : nullptr;
        return *this;
    }
    CustomProjection& operator=(CustomProjection&&) noexcept = default;

    template <CameraProjection P>
        requires std::copy_constructible<P>
    explicit CustomProjection(P projection)
        : dyn_projection(std::make_shared<DynCameraProjectionImpl<P>>(std::move(projection))) {}

    template <CameraProjection P>
    P* get() noexcept {
        if (auto* impl = dynamic_cast<DynCameraProjectionImpl<P>*>(dyn_projection.get())) return &impl->value;
        return nullptr;
    }
    template <CameraProjection P>
    const P* get() const noexcept {
        if (auto* impl = dynamic_cast<const DynCameraProjectionImpl<P>*>(dyn_projection.get())) return &impl->value;
        return nullptr;
    }
    glm::mat4 get_clip_from_view() const { return dyn_projection->clip_from_view(); }
    glm::mat4 get_clip_from_view_for_sub(const SubCameraView& sub_view) const {
        return dyn_projection->clip_from_view_for_sub(sub_view);
    }
    std::array<glm::vec3, 8> get_frustum_corners(float z_near, float z_far) const {
        return dyn_projection->frustum_corners(z_near, z_far);
    }
    float get_far() const { return dyn_projection->far_value(); }
    void update(float width, float height) { dyn_projection->update_projection(width, height); }
    /** @brief Compute this projection's world-space frustum (Bevy's default
     * `CameraProjection::compute_frustum`). */
    Frustum compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const;
};

/** @brief Variant projection type wrapping orthographic, perspective, or a
 * type-erased custom projection (Bevy `Projection`). */
EPIX_EXPORT struct Projection {
    std::variant<OrthographicProjection, PerspectiveProjection, CustomProjection> projection;

    Projection() : projection(PerspectiveProjection{}) {}
    Projection(const OrthographicProjection& ortho) : projection(ortho) {}
    Projection(const PerspectiveProjection& perspective) : projection(perspective) {}

    /** @brief Create an orthographic projection. */
    static Projection orthographic(const OrthographicProjection& ortho = {}) { return Projection(ortho); }

    /** @brief Create a perspective projection. */
    static Projection perspective(const PerspectiveProjection& perspective = {}) { return Projection(perspective); }
    template <CameraProjection P>
        requires std::copy_constructible<P>
    static Projection custom(P projection) {
        Projection result;
        result.projection = CustomProjection{std::move(projection)};
        return result;
    }

    /** @brief Get the clip-from-view matrix from the active variant. */
    glm::mat4 get_clip_from_view() const {
        return std::visit([](const auto& proj) { return proj.get_clip_from_view(); }, projection);
    }
    /** @brief Get the active projection matrix cropped to a sub-camera view. */
    glm::mat4 get_clip_from_view_for_sub(const SubCameraView& sub_view) const {
        return std::visit([&sub_view](const auto& proj) { return proj.get_clip_from_view_for_sub(sub_view); },
                          projection);
    }
    /** @brief Get the far clipping plane distance. */
    float get_far() const { return std::visit([](const auto& proj) { return proj.get_far(); }, projection); }
    /** @brief Update the active projection for new viewport dimensions. */
    void update(float width, float height) {
        std::visit([width, height](auto& proj) { proj.update(width, height); }, projection);
    }
    /** @brief Compute the 8 frustum corner points. */
    std::array<glm::vec3, 8> get_frustum_corners(float z_near, float z_far) const {
        return std::visit([=](const auto& proj) { return proj.get_frustum_corners(z_near, z_far); }, projection);
    }
    /** @brief Compute this projection's world-space frustum (Bevy's default
     * `CameraProjection::compute_frustum`). */
    Frustum compute_frustum(const ::epix::transform::GlobalTransform& camera_transform) const;
    /** @brief True for perspective projections. Custom projections follow
     * Bevy and are perspective when their clip matrix's w-axis has `w == 0`. */
    bool is_perspective() const {
        return std::visit(
            [](const auto& proj) {
                using ProjectionType = std::remove_cvref_t<decltype(proj)>;
                if constexpr (std::same_as<ProjectionType, PerspectiveProjection>) {
                    return true;
                } else if constexpr (std::same_as<ProjectionType, OrthographicProjection>) {
                    return false;
                } else {
                    return proj.get_clip_from_view()[3][3] == 0.0f;
                }
            },
            projection);
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
    /** Return the custom projection when its stored concrete type is `P`. */
    template <CameraProjection P>
    P* get_custom() noexcept {
        if (auto* custom = std::get_if<CustomProjection>(&projection)) return custom->get<P>();
        return nullptr;
    }
    template <CameraProjection P>
    const P* get_custom() const noexcept {
        if (auto* custom = std::get_if<CustomProjection>(&projection)) return custom->get<P>();
        return nullptr;
    }
};

}  // namespace epix::camera
