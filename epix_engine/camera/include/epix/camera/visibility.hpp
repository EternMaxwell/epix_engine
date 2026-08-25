#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace epix::camera {

struct Camera;  // defined in camera.hpp; used by the visibility systems

/** @brief Marker disabling CPU frustum culling for a camera (Bevy
 * bevy_camera::visibility::NoCpuCulling). */
EPIX_EXPORT struct NoCpuCulling {};

/** @brief Visibility state across the current and previous frames (Bevy
 * `ViewVisibility`). Bit 0 is current visibility and bit 1 previous
 * visibility, avoiding a fixed per-camera bit budget. */
EPIX_EXPORT struct ViewVisibility {
    /** @brief Raw flag storage. */
    std::uint32_t flags = 0;

    /** @brief Visible to any view in the current frame. */
    bool get() const noexcept { return (flags & 1u) != 0; }
    /** @brief Compatibility accessor; Bevy 0.18 exposes aggregate rather
     * than capped per-camera visibility. */
    bool get_in_view(std::uint32_t) const noexcept { return get(); }
    void set_in_view(std::uint32_t, bool is_visible) noexcept {
        if (is_visible) set_visible();
    }
    void culled() noexcept { flags &= ~1u; }
    void set_visible() noexcept { flags |= 1u; }
    /** Compatibility spelling retained for existing Epix callers. */
    void visible() noexcept { set_visible(); }
    /** Advance the current bit to previous-frame scratch storage. */
    void update() noexcept { flags = (flags & 1u) << 1u; }
    /** True after `update` when visibility was lost this frame (Bevy
     * `was_visible_now_hidden`). */
    bool was_visible_now_hidden() const noexcept { return flags == 0b10u; }
};

/** @brief C++ equivalent of Bevy's `SetViewVisibility` trait. */
EPIX_EXPORT template <typename T>
concept SetViewVisibility = requires(T& visibility) { visibility.set_visible(); };

/** @brief User indication of whether an entity is visible (Bevy
 * bevy_camera::visibility::Visibility). Propagates down the entity hierarchy;
 * epix has no ChildOf hierarchy yet, so the marker and its toggles match
 * Bevy's interface and propagation collapses to the entity's own state. */
EPIX_EXPORT struct Visibility {
    /** @brief Visibility kind (Bevy Visibility variants). */
    enum class Type { Inherited, Hidden, Visible };

    /** @brief The visibility kind; default Inherited (Bevy #[default]). */
    Type type = Type::Inherited;

    static Visibility inherited() noexcept { return {Type::Inherited}; }
    static Visibility hidden() noexcept { return {Type::Hidden}; }
    static Visibility visible() noexcept { return {Type::Visible}; }

    /** @brief Toggle between Inherited and Visible (Bevy
     * Visibility::toggle_inherited_visible; Hidden unaffected). */
    void toggle_inherited_visible() noexcept {
        type = type == Type::Inherited ? Type::Visible : type == Type::Visible ? Type::Inherited : type;
    }
    /** @brief Toggle between Inherited and Hidden (Bevy
     * Visibility::toggle_inherited_hidden; Visible unaffected). */
    void toggle_inherited_hidden() noexcept {
        type = type == Type::Inherited ? Type::Hidden : type == Type::Hidden ? Type::Inherited : type;
    }
    /** @brief Toggle between Visible and Hidden (Bevy
     * Visibility::toggle_visible_hidden; Inherited unaffected). */
    void toggle_visible_hidden() noexcept {
        type = type == Type::Visible ? Type::Hidden : type == Type::Hidden ? Type::Visible : type;
    }

    bool operator==(const Visibility&) const = default;
};

/** @brief Whether or not an entity is visible in the hierarchy (Bevy
 * bevy_camera::visibility::InheritedVisibility). Not accurate until
 * visibility propagation runs. */
EPIX_EXPORT struct InheritedVisibility {
    /** @brief Raw visibility flag (Bevy's newtype field is unnamed). */
    bool is_visible = true;

    /** @brief An entity invisible in the hierarchy (Bevy
     * InheritedVisibility::HIDDEN). */
    static InheritedVisibility hidden() noexcept { return {false}; }
    /** @brief An entity visible in the hierarchy (Bevy
     * InheritedVisibility::VISIBLE). */
    static InheritedVisibility visible() noexcept { return {true}; }

    /** @brief True if the entity is visible in the hierarchy (Bevy
     * InheritedVisibility::get). */
    bool get() const noexcept { return is_visible; }
};


/** @brief An identifier for a render layer (Bevy `Layer`). */
using Layer = std::size_t;

/** @brief Dynamically extensible render-layer mask. Its finite positive-mask
 * operations match Bevy; Epix additionally supports an inverted mask for
 * convenient `all()` / `all_except()` helpers. */
EPIX_EXPORT struct RenderLayers {
   private:
    std::vector<std::uint64_t> blocks_{0};
    bool inverted_ = false;

    explicit RenderLayers(std::vector<std::uint64_t> values, bool inverted = false)
        : blocks_(std::move(values)), inverted_(inverted) {
        trim();
    }
    static constexpr std::size_t block_index(Layer layer) noexcept { return layer / 64; }
    static constexpr std::uint64_t layer_bit(Layer layer) noexcept { return std::uint64_t{1} << (layer % 64); }
    void extend(std::size_t count) {
        if (blocks_.size() < count) blocks_.resize(count, 0);
    }
    void trim() noexcept {
        while (blocks_.size() > 1 && blocks_.back() == 0) blocks_.pop_back();
    }
    template <typename Combine>
    RenderLayers combine_blocks(const RenderLayers& other, Combine&& op, bool inverted = false) const {
        std::vector<std::uint64_t> result(std::max(blocks_.size(), other.blocks_.size()), 0);
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto lhs = i < blocks_.size() ? blocks_[i] : 0;
            const auto rhs = i < other.blocks_.size() ? other.blocks_[i] : 0;
            result[i] = op(lhs, rhs);
        }
        return RenderLayers(std::move(result), inverted);
    }

   public:
    /** @brief Default: entity belongs to layer 0. */
    RenderLayers() { blocks_[0] = 1; }
    static RenderLayers layer(Layer layer) {
        auto result = none();
        result.extend(block_index(layer) + 1);
        result.blocks_[block_index(layer)] |= layer_bit(layer);
        return result;
    }
    static RenderLayers none() { return RenderLayers(std::vector<std::uint64_t>{0}); }
    /** @brief Epix extension: match every layer without enumerating it. */
    static RenderLayers all() { return RenderLayers(std::vector<std::uint64_t>{0}, true); }
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, Layer>
    static RenderLayers from_layers(R&& layers) {
        auto result = none();
        for (auto layer : layers) result = result.with(static_cast<Layer>(layer));
        return result;
    }
    /** @brief Epix extension: match every layer except the supplied layers. */
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, Layer>
    static RenderLayers all_except(R&& layers) {
        auto result = all();
        for (auto layer : layers) result = result.without(static_cast<Layer>(layer));
        return result;
    }
    RenderLayers with(Layer layer) const {
        auto result = *this;
        result.extend(block_index(layer) + 1);
        if (result.inverted_)
            result.blocks_[block_index(layer)] &= ~layer_bit(layer);
        else
            result.blocks_[block_index(layer)] |= layer_bit(layer);
        result.trim();
        return result;
    }
    RenderLayers without(Layer layer) const {
        auto result = *this;
        const auto index = block_index(layer);
        if (result.inverted_) result.extend(index + 1);
        if (index < result.blocks_.size())
            if (result.inverted_)
                result.blocks_[index] |= layer_bit(layer);
            else
                result.blocks_[index] &= ~layer_bit(layer);
        result.trim();
        return result;
    }
    /** @brief Return the finite stored layers. For an inverted Epix mask these
     * are the exclusions, since its included infinite set cannot be iterated. */
    std::vector<Layer> iter() const {
        std::vector<Layer> result;
        for (std::size_t block = 0; block < blocks_.size(); ++block) {
            auto value = blocks_[block];
            while (value != 0) {
                const auto bit = static_cast<std::size_t>(std::countr_zero(value));
                result.push_back(block * 64 + bit);
                value &= value - 1;
            }
        }
        return result;
    }
    std::span<const std::uint64_t> bits() const noexcept { return blocks_; }
    bool is_inverted() const noexcept { return inverted_; }
    bool contains(Layer layer) const noexcept {
        const auto index = block_index(layer);
        const bool stored = index < blocks_.size() && (blocks_[index] & layer_bit(layer)) != 0;
        return inverted_ ? !stored : stored;
    }
    bool intersects(const RenderLayers& other) const noexcept {
        if (inverted_ && other.inverted_) return true;
        if (!inverted_ && !other.inverted_) {
            const auto count = std::min(blocks_.size(), other.blocks_.size());
            for (std::size_t i = 0; i < count; ++i)
                if ((blocks_[i] & other.blocks_[i]) != 0) return true;
            return false;
        }
        const auto& finite = inverted_ ? other : *this;
        const auto& excluded = inverted_ ? *this : other;
        for (std::size_t i = 0; i < finite.blocks_.size(); ++i) {
            const auto excluded_block = i < excluded.blocks_.size() ? excluded.blocks_[i] : 0;
            if ((finite.blocks_[i] & ~excluded_block) != 0) return true;
        }
        return false;
    }
    RenderLayers intersection(const RenderLayers& other) const {
        if (!inverted_ && !other.inverted_) return combine_blocks(other, [](auto lhs, auto rhs) { return lhs & rhs; });
        if (!inverted_ && other.inverted_) return combine_blocks(other, [](auto lhs, auto rhs) { return lhs & ~rhs; });
        if (inverted_ && !other.inverted_) return other.intersection(*this);
        return combine_blocks(other, [](auto lhs, auto rhs) { return lhs | rhs; }, true);
    }
    RenderLayers union_with(const RenderLayers& other) const {
        if (!inverted_ && !other.inverted_) return combine_blocks(other, [](auto lhs, auto rhs) { return lhs | rhs; });
        if (!inverted_ && other.inverted_) return combine_blocks(other, [](auto lhs, auto rhs) { return rhs & ~lhs; }, true);
        if (inverted_ && !other.inverted_) return other.union_with(*this);
        return combine_blocks(other, [](auto lhs, auto rhs) { return lhs & rhs; }, true);
    }
    RenderLayers symmetric_difference(const RenderLayers& other) const {
        if (inverted_ == other.inverted_)
            return combine_blocks(other, [](auto lhs, auto rhs) { return lhs ^ rhs; });
        return combine_blocks(other, [](auto lhs, auto rhs) { return lhs ^ rhs; }, true);
    }
    friend RenderLayers operator&(const RenderLayers& lhs, const RenderLayers& rhs) { return lhs.intersection(rhs); }
    friend RenderLayers operator|(const RenderLayers& lhs, const RenderLayers& rhs) { return lhs.union_with(rhs); }
    friend RenderLayers operator^(const RenderLayers& lhs, const RenderLayers& rhs) {
        return lhs.symmetric_difference(rhs);
    }
    bool operator==(const RenderLayers&) const = default;
};

/** @brief Visibility class(es) an entity is collected under (Bevy
 * `VisibilityClass`). */
EPIX_EXPORT struct VisibilityClass {
    std::vector<meta::type_index> classes;
    VisibilityClass() = default;
    explicit VisibilityClass(meta::type_index type) : classes{type} {}
    void add(meta::type_index type) {
        if (std::ranges::find(classes, type) == classes.end()) classes.push_back(type);
    }
    void push(meta::type_index type) { add(type); }
    auto begin() const noexcept { return classes.begin(); }
    auto end() const noexcept { return classes.end(); }
    bool empty() const noexcept { return classes.empty(); }
};

/** @brief Component-add hook that appends C's type to an existing
 * `VisibilityClass` (Bevy `add_visibility_class<C>`).  Renderable component
 * types may expose this as their `on_add` hook after arranging for a
 * `VisibilityClass` required component. */
template <typename C>
void add_visibility_class(epix::ecs::World& world, epix::ecs::HookContext context) {
    world.get_entity_mut(context.entity).transform([](epix::ecs::EntityWorldMut&& entity) -> int {
        if (auto visibility_class = entity.template get_mut<VisibilityClass>()) {
            visibility_class->get_mut().add(meta::type_index(meta::type_id<C>()));
        }
        return 0;
    });
}

/** @brief Opt an entity out of frustum culling (Bevy `NoFrustumCulling`). */
EPIX_EXPORT struct NoFrustumCulling {};

/** @brief Prevent automatic mesh-bound generation (Bevy `NoAutoAabb`). */
EPIX_EXPORT struct NoAutoAabb {};

/** @brief Distance range in which an entity is visible (Bevy
 * `bevy_camera::visibility::VisibilityRange`). GPU upload is performed by
 * render's `RenderVisibilityRanges`, but this is a main-world camera
 * component. */
EPIX_EXPORT struct VisibilityRange {
    float start_margin_start = 0.0f;
    float start_margin_end   = 0.0f;
    float end_margin_start   = 0.0f;
    float end_margin_end     = 0.0f;
    bool use_aabb            = false;
    bool operator==(const VisibilityRange&) const = default;
    static VisibilityRange abrupt(float start, float end) noexcept {
        return {.start_margin_start = start,
                .start_margin_end   = start,
                .end_margin_start   = end,
                .end_margin_end     = end};
    }
    bool is_abrupt() const noexcept {
        return start_margin_start == start_margin_end && end_margin_start == end_margin_end;
    }
    bool is_visible_at_all(float camera_distance) const noexcept {
        return camera_distance >= start_margin_start && camera_distance < end_margin_end;
    }
    bool is_culled(float camera_distance) const noexcept { return !is_visible_at_all(camera_distance); }
};

/** @brief Per-view membership of `VisibilityRange` entities (Bevy
 * `VisibleEntityRanges`). */
EPIX_EXPORT struct VisibleEntityRanges {
    std::unordered_map<epix::ecs::Entity, std::uint8_t> views;
    std::unordered_map<epix::ecs::Entity, std::uint32_t> entities;
    void clear() noexcept {
        views.clear();
        entities.clear();
    }
    bool entity_is_in_range_of_view(epix::ecs::Entity entity, epix::ecs::Entity view) const noexcept {
        const auto entity_it = entities.find(entity);
        const auto view_it   = views.find(view);
        return entity_it != entities.end() && view_it != views.end() &&
               (entity_it->second & (std::uint32_t{1} << view_it->second)) != 0;
    }
    bool entity_is_in_range_of_any_view(epix::ecs::Entity entity) const noexcept { return entities.contains(entity); }
};

/** @brief Enables camera-side distance-range visibility checks (Bevy
 * `VisibilityRangePlugin`). */
EPIX_EXPORT struct VisibilityRangePlugin {
    void attach(epix::app::App& app);
};

/** @brief Installs the base visibility propagation, frustum-culling, and
 * visibility-state systems (Bevy `VisibilityPlugin`).  `CameraPlugin`
 * composes this plugin with projection and range support. */
EPIX_EXPORT struct VisibilityPlugin {
    void attach(epix::app::App& app);
};

/** @brief A normalized inward-facing plane half-space (Bevy `HalfSpace`). */
EPIX_EXPORT struct HalfSpace {
    glm::vec4 normal_d{0.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity()};
    static HalfSpace from_normal_d(glm::vec4 value) noexcept {
        const float length = glm::length(glm::vec3(value));
        return HalfSpace{length > 0.0f ? value / length : value};
    }
    glm::vec3 normal() const noexcept { return glm::vec3(normal_d); }
    float d() const noexcept { return normal_d.w; }
};

/** @brief Axis-aligned local-space bounds (Bevy `Aabb`). */
EPIX_EXPORT struct Aabb {
    glm::vec3 center{0.0f};
    glm::vec3 half_extents{0.0f};
    static Aabb from_min_max(glm::vec3 minimum, glm::vec3 maximum) noexcept {
        return {.center = 0.5f * (minimum + maximum), .half_extents = 0.5f * (maximum - minimum)};
    }
    /** @brief Bounds all points, or returns no value for an empty range (Bevy
     * `Aabb::enclosing`). */
    template <std::ranges::input_range R>
    static std::optional<Aabb> enclosing(R&& points) noexcept {
        auto first = std::ranges::begin(points);
        const auto last = std::ranges::end(points);
        if (first == last) return std::nullopt;
        glm::vec3 minimum = *first;
        glm::vec3 maximum = minimum;
        for (++first; first != last; ++first) {
            minimum = glm::min(minimum, glm::vec3(*first));
            maximum = glm::max(maximum, glm::vec3(*first));
        }
        return from_min_max(minimum, maximum);
    }
    glm::vec3 min() const noexcept { return center - half_extents; }
    glm::vec3 max() const noexcept { return center + half_extents; }
    float relative_radius(const glm::vec3& normal, const glm::mat3& world_from_local) const noexcept {
        return glm::dot(glm::abs(glm::transpose(world_from_local) * normal), glm::abs(half_extents));
    }
    bool is_in_half_space(const HalfSpace& half_space, const glm::mat4& world_from_local) const noexcept {
        const glm::vec3 normal = half_space.normal();
        const float radius = glm::dot(glm::abs(glm::mat3(world_from_local)) * glm::abs(half_extents), glm::abs(normal));
        const glm::vec3 transformed_center = glm::vec3(world_from_local * glm::vec4(center, 1.0f));
        return glm::dot(normal, transformed_center) + half_space.d() > radius;
    }
    bool is_in_half_space_identity(const HalfSpace& half_space) const noexcept {
        const glm::vec3 normal = half_space.normal();
        return glm::dot(normal, center) + half_space.d() > glm::dot(glm::abs(half_extents), glm::abs(normal));
    }
};

/** @brief C++ equivalent of Bevy's `MeshAabb` trait. */
EPIX_EXPORT template <typename T>
concept MeshAabb = requires(const T& mesh) {
    { mesh.compute_aabb() } -> std::same_as<std::optional<Aabb>>;
};

/** @brief Bounding sphere primitive (Bevy `Sphere`). */
EPIX_EXPORT struct Sphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    bool intersects_obb(const Aabb& aabb, const glm::mat4& world_from_local) const noexcept {
        const glm::vec3 aabb_center = glm::vec3(world_from_local * glm::vec4(aabb.center, 1.0f));
        const glm::vec3 offset = aabb_center - center;
        const float distance = glm::length(offset);
        if (distance == 0.0f) return true;
        return distance < radius + aabb.relative_radius(offset / distance, glm::mat3(world_from_local));
    }
};

/** @brief View frustum as 6 plane half-spaces (Bevy bevy_camera
 * primitives::Frustum). Each plane is (normal.xyz, d) with a point in front
 * when dot(normal, p) + d >= 0. */
EPIX_EXPORT struct Frustum {
    /** @brief The six planes: left, right, top, bottom, near, far. */
    std::array<glm::vec4, 6> planes = [] {
        std::array<glm::vec4, 6> result{};
        result.fill(glm::vec4(0.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity()));
        return result;
    }();

    /** @brief Extract the planes from a clip-from-world matrix (Bevy
     * `Frustum::from_clip_from_world`). */
    static Frustum from_clip_from_world(const glm::mat4& clip_from_world) noexcept {
        Frustum frustum;
        // glm mat4 is column-major; extract matrix rows first.
        auto row = [&](std::size_t i) {
            return glm::vec4{clip_from_world[0][i], clip_from_world[1][i], clip_from_world[2][i],
                             clip_from_world[3][i]};
        };
        const auto row3 = row(3);
        auto extract    = [&](std::size_t index, glm::vec4 plane) {
            const float len = glm::length(glm::vec3(plane));
            if (len > 0.0f) plane /= len;
            frustum.planes[index] = plane;
        };
        extract(0, row3 + row(0));  // left
        extract(1, row3 - row(0));  // right
        extract(2, row3 + row(1));  // top
        extract(3, row3 - row(1));  // bottom
        extract(4, row3 + row(2));  // near
        // Bevy (and Epix's GLM configuration) use a zero-to-one clip-space
        // depth range, so the far plane is row2, not row3 - row2.
        extract(5, row(2));          // far
        return frustum;
    }

    /** @brief Backward-compatible spelling for `from_clip_from_world`. */
    static Frustum from_view_projection(const glm::mat4& clip_from_world) noexcept {
        return from_clip_from_world(clip_from_world);
    }

    /** @brief Creates a frustum with an explicit far plane (Bevy
     * `Frustum::from_clip_from_world_custom_far`). */
    static Frustum from_clip_from_world_custom_far(const glm::mat4& clip_from_world,
                                                   const glm::vec3& view_translation,
                                                   const glm::vec3& view_backward,
                                                   float far_distance) noexcept {
        Frustum frustum = from_clip_from_world(clip_from_world);
        const glm::vec3 far_center = view_translation - far_distance * view_backward;
        glm::vec4 far_plane(view_backward, -glm::dot(view_backward, far_center));
        const float length = glm::length(glm::vec3(far_plane));
        frustum.planes[5] = length > 0.0f ? far_plane / length : far_plane;
        return frustum;
    }

    /** Bevy-compatible sphere/frustum intersection.  `intersect_far` permits
     * infinite-far camera projections to ignore the far plane. */
    bool intersects_sphere(const Sphere& sphere, bool intersect_far = true) const noexcept {
        const std::size_t last_plane = intersect_far ? planes.size() : 5;
        for (std::size_t i = 0; i < last_plane; ++i) {
            if (glm::dot(planes[i], glm::vec4(sphere.center, 1.0f)) + sphere.radius <= 0.0f) return false;
        }
        return true;
    }

    /** Frustum test for a local AABB transformed by `world_from_local`.
     * `intersect_near`/`intersect_far` match Bevy's OBB helper. */
    bool intersects_obb(const Aabb& aabb,
                        const glm::mat4& world_from_local,
                        bool intersect_near = true,
                        bool intersect_far = true) const noexcept {
        const glm::vec3 center = glm::vec3(world_from_local * glm::vec4(aabb.center, 1.0f));
        const glm::mat3 basis(world_from_local);
        for (std::size_t i = 0; i < planes.size(); ++i) {
            if ((i == 4 && !intersect_near) || (i == 5 && !intersect_far)) continue;
            const glm::vec3 normal = glm::vec3(planes[i]);
            if (glm::dot(normal, center) + planes[i].w + aabb.relative_radius(normal, basis) <= 0.0f) return false;
        }
        return true;
    }
    bool intersects_obb_identity(const Aabb& aabb) const noexcept {
        return intersects_obb(aabb, glm::mat4(1.0f));
    }
    bool contains_aabb(const Aabb& aabb, const glm::mat4& world_from_local) const noexcept {
        for (const auto& plane : planes) {
            if (!aabb.is_in_half_space(HalfSpace::from_normal_d(plane), world_from_local)) return false;
        }
        return true;
    }
    bool contains_aabb_identity(const Aabb& aabb) const noexcept {
        for (const auto& plane : planes) {
            if (!aabb.is_in_half_space_identity(HalfSpace::from_normal_d(plane))) return false;
        }
        return true;
    }
};

/** @brief Six cubemap face directions in WebGPU layer order (Bevy
 * `CUBE_MAP_FACES`). */
EPIX_EXPORT struct CubeMapFace {
    glm::vec3 target{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};
EPIX_EXPORT inline const std::array<CubeMapFace, 6> CubeMapFaces{{
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}}, {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
}};

/** @brief Bevy `face_index_to_name` equivalent. */
EPIX_EXPORT inline std::string_view face_index_to_name(std::size_t face_index) noexcept {
    constexpr std::array<std::string_view, 6> names{"+x", "-x", "+y", "-y", "+z", "-z"};
    return face_index < names.size() ? names[face_index] : "invalid";
}

/** @brief Packed cubemap image layouts (Bevy `CubemapLayout`). */
EPIX_EXPORT enum class CubemapLayout : std::uint8_t { CrossVertical, CrossHorizontal, SequenceVertical, SequenceHorizontal };

/** @brief Per-cubemap-face frusta (Bevy `CubemapFrusta`). */
EPIX_EXPORT struct CubemapFrusta {
    std::array<Frustum, 6> frusta{};
    auto begin() const noexcept { return frusta.begin(); }
    auto end() const noexcept { return frusta.end(); }
    auto begin() noexcept { return frusta.begin(); }
    auto end() noexcept { return frusta.end(); }
};

/** @brief Per-cascade frusta keyed by light/view entity (Bevy `CascadesFrusta`). */
EPIX_EXPORT struct CascadesFrusta {
    std::unordered_map<epix::ecs::Entity, std::vector<Frustum>> frusta;
};

/** @brief Component listing entities visible to a camera view, keyed by
 * visibility class (Bevy bevy_camera::visibility::VisibleEntities). */
EPIX_EXPORT struct VisibleEntities {
    /** @brief Visible entity IDs per visibility class (Bevy
     * TypeIdMap<Vec<Entity>>). */
    std::unordered_map<meta::type_index, std::vector<epix::ecs::Entity>> entities;

    /** @brief Entities visible for the given type id; empty when absent
     * (Bevy VisibleEntities::get). */
    const std::vector<epix::ecs::Entity>& get(const meta::type_index& type_id) const {
        static const std::vector<epix::ecs::Entity> kEmpty;
        if (auto it = entities.find(type_id); it != entities.end()) return it->second;
        return kEmpty;
    }
    /** @brief Mutable access, inserting an empty list if absent (Bevy
     * VisibleEntities::get_mut). */
    std::vector<epix::ecs::Entity>& get_mut(const meta::type_index& type_id) { return entities[type_id]; }
    /** @brief Iterate the visible entities of the given type (Bevy
     * VisibleEntities::iter). */
    std::span<const epix::ecs::Entity> iter(const meta::type_index& type_id) const { return get(type_id); }
    /** @brief Number of visible entities of the given type (Bevy
     * VisibleEntities::len). */
    std::size_t len(const meta::type_index& type_id) const { return get(type_id).size(); }
    /** @brief Whether any entity of the given type is visible (Bevy
     * VisibleEntities::is_empty). */
    bool is_empty(const meta::type_index& type_id) const { return get(type_id).empty(); }
    /** @brief Clear the given type's list, keeping the allocation (Bevy
     * VisibleEntities::clear). */
    void clear(const meta::type_index& type_id) { get_mut(type_id).clear(); }
    /** @brief Clear all lists, keeping allocations (Bevy
     * VisibleEntities::clear_all). */
    void clear_all() {
        for (auto& [type_id, list] : entities) {
            (void)type_id;
            list.clear();
        }
    }
    /** @brief Append an entity to a visibility class (Bevy
     * `VisibleEntities::push`). */
    void push(epix::ecs::Entity entity, const meta::type_index& type_id) { get_mut(type_id).push_back(entity); }
};

/** @brief Mesh entities visible from one light/view (Bevy
 * `VisibleMeshEntities`). */
EPIX_EXPORT struct VisibleMeshEntities {
    std::vector<epix::ecs::Entity> entities;
    auto begin() const noexcept { return entities.begin(); }
    auto end() const noexcept { return entities.end(); }
    auto begin() noexcept { return entities.begin(); }
    auto end() noexcept { return entities.end(); }
};

/** @brief Visible mesh entities for all six cubemap faces (Bevy
 * `CubemapVisibleEntities`). */
EPIX_EXPORT struct CubemapVisibleEntities {
    std::array<VisibleMeshEntities, 6> data{};
    const VisibleMeshEntities& get(std::size_t index) const { return data.at(index); }
    VisibleMeshEntities& get_mut(std::size_t index) { return data.at(index); }
    auto begin() const noexcept { return data.begin(); }
    auto end() const noexcept { return data.end(); }
    auto begin() noexcept { return data.begin(); }
    auto end() noexcept { return data.end(); }
};

/** @brief Visible mesh entities for each shadow cascade (Bevy
 * `CascadesVisibleEntities`). */
EPIX_EXPORT struct CascadesVisibleEntities {
    std::unordered_map<epix::ecs::Entity, std::vector<VisibleMeshEntities>> entities;
};

/** @brief Camera visibility pipeline labels (Bevy `VisibilitySystems`). */
EPIX_EXPORT enum class VisibilitySystems {
    CalculateBounds,
    UpdateFrusta,
    VisibilityPropagate,
    CheckVisibility,
    MarkNewlyHidden,
};

// ==== Visibility systems (Bevy bevy_camera::visibility) ====

/** @brief Computes each entity's inherited visibility from its own Visibility
 * marker (Bevy visibility_propagate_system). epix has no ChildOf hierarchy
 * yet, so propagation collapses to the entity's own state. */
EPIX_EXPORT void visibility_propagate_system(
    epix::ecs::Query<epix::ecs::Item<const Visibility&, epix::ecs::Mut<InheritedVisibility>>> visibilities);

/** @brief Resets every ViewVisibility to the default (visible-by-default)
 * state before check_visibility runs (Bevy reset_view_visibility). */
EPIX_EXPORT void reset_view_visibility(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Mut<ViewVisibility>>> view_visibilities);

/** @brief Finalizes a visible-to-hidden transition without leaving stale
 * previous-frame state (Bevy `mark_newly_hidden_entities_invisible`). */
EPIX_EXPORT void mark_newly_hidden_entities_invisible(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Mut<ViewVisibility>>> view_visibilities);

/** @brief Computes per-camera distance-range membership before frustum
 * visibility (Bevy `check_visibility_ranges`). */
EPIX_EXPORT void check_visibility_ranges(
    epix::ecs::ResMut<VisibleEntityRanges> visible_entity_ranges,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const ::epix::transform::GlobalTransform&>,
                     epix::ecs::With<Camera>> cameras,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                     const ::epix::transform::GlobalTransform&,
                                     epix::ecs::Opt<const Aabb&>,
                                     const VisibilityRange&>> entities);

/** @brief Marks entities visible per camera view: sets the per-view bit and
 * collects them into the camera's VisibleEntities (Bevy `check_visibility`).
 * When bounds and a global transform are present, it performs Bevy's sphere
 * broad phase followed by transformed-AABB frustum culling. */
EPIX_EXPORT void check_visibility_system(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                     const Camera&,
                                     epix::ecs::Mut<VisibleEntities>,
                                     epix::ecs::Opt<const RenderLayers&>,
                                     const Frustum&,
                                     epix::ecs::Opt<const NoCpuCulling&>>> cameras,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                     const InheritedVisibility&,
                                     epix::ecs::Mut<ViewVisibility>,
                                     epix::ecs::Opt<const VisibilityClass&>,
                                     epix::ecs::Opt<const RenderLayers&>,
                                     epix::ecs::Opt<const Aabb&>,
                                     epix::ecs::Opt<const ::epix::transform::GlobalTransform&>,
                                     epix::ecs::Opt<const NoFrustumCulling&>,
                                     epix::ecs::Opt<const VisibilityRange&>>> entities,
    epix::ecs::Res<VisibleEntityRanges> visible_entity_ranges);

/** @brief Recomputes each camera's Frustum from its projection and transform
 * (Bevy update_frusta). */
EPIX_EXPORT void update_frusta(
    epix::ecs::Query<epix::ecs::Item<const ::epix::transform::GlobalTransform&,
                                     const ::epix::camera::Projection&,
                                     epix::ecs::Mut<Frustum>>>
        cameras);

}  // namespace epix::camera

template <>
struct std::hash<epix::camera::VisibilityRange> {
    std::size_t operator()(const epix::camera::VisibilityRange& range) const noexcept {
        std::size_t h = std::hash<float>{}(range.start_margin_start);
        h ^= std::hash<float>{}(range.start_margin_end) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(range.end_margin_start) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(range.end_margin_end) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
