#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace epix::camera {

struct Camera;  // defined in camera.hpp; used by the visibility systems

/** @brief Per-view visibility flags of a render-world entity (Bevy
 * bevy_camera::visibility::ViewVisibility). Bit 0 is CULLED; bits 1..16 are
 * per-view visibility (up to 16 views). get() reports whether the entity is
 * visible to any view. Populated by the visibility systems; extraction
 * filters on it. */
EPIX_EXPORT struct ViewVisibility {
    /** @brief Raw flag storage. */
    std::uint32_t flags = 0;

    /** @brief Visible to any view (Bevy ViewVisibility::get: not culled). */
    bool get() const noexcept { return (flags & (1u << 0)) == 0; }
    /** @brief Visible in the given view (Bevy get_in_view). */
    bool get_in_view(std::uint32_t view_index) const noexcept { return (flags & (1u << (view_index + 1))) != 0; }
    /** @brief Set visibility for the given view (Bevy set_in_view). */
    void set_in_view(std::uint32_t view_index, bool visible) noexcept {
        const std::uint32_t mask = 1u << (view_index + 1);
        if (visible) {
            flags |= mask;
        } else {
            flags &= ~mask;
        }
    }
    /** @brief Mark the entity culled for all views (Bevy culled). */
    void culled() noexcept { flags |= (1u << 0); }
    /** @brief Mark the entity visible for all views (Bevy visible). */
    void visible() noexcept { flags &= ~(1u << 0); }
};

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

/** @brief Identifies which render layers a camera renders or an entity belongs
 * to (Bevy bevy_camera::visibility::RenderLayers).
 *
 * Backed by a dynamic bit vector. When inverted is false (the default) the
 * component represents a finite set of active layer indices. When inverted is
 * true it represents the complement — i.e. "all layers except those in bits" —
 * which allows expressing "render everything" without enumerating every index.
 *
 * Default construction yields layer 0 (bits = {0}, inverted = false). */
EPIX_EXPORT struct RenderLayers {
    utils::bit_vector bits;
    bool inverted = false;

    /** @brief Default: entity is on layer 0. */
    RenderLayers() { bits.set(0); }

    /** @brief Internal constructor for factory methods. */
    RenderLayers(utils::bit_vector b, bool inv) : bits(std::move(b)), inverted(inv) {}

    /** @brief Matches all layers (camera default). */
    static RenderLayers all() { return RenderLayers(utils::bit_vector{}, true); }

    /** @brief Matches no layers. */
    static RenderLayers none() { return RenderLayers(utils::bit_vector{}, false); }

    /** @brief Matches exactly one layer. */
    static RenderLayers layer(std::size_t n) {
        utils::bit_vector b;
        b.set(n);
        return RenderLayers(std::move(b), false);
    }

    /** @brief Matches a set of layers (accepts any input range of std::size_t). */
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, std::size_t>
    static RenderLayers layers(R&& ns) {
        utils::bit_vector b;
        for (auto n : ns) b.set(static_cast<std::size_t>(n));
        return RenderLayers(std::move(b), false);
    }
    /** @brief Matches all layers except those in the range. */
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, std::size_t>
    static RenderLayers all_except(R&& ns) {
        utils::bit_vector b;
        for (auto n : ns) b.set(static_cast<std::size_t>(n));
        return RenderLayers(std::move(b), true);
    }

    /** @brief Returns true if layer index n is active on this RenderLayers. */
    bool contains(std::size_t n) const noexcept {
        bool in_bits = bits.contains(n);
        return inverted ? !in_bits : in_bits;
    }

    /** @brief Returns true if this and other share at least one active layer.
     *
     * Truth table for the four (inverted, inverted) combinations:
     *  - normal ∩ normal   → any shared set bit
     *  - normal ∩ inverted → any bit in self not excluded by other
     *  - inverted ∩ normal → any bit in other not excluded by self
     *  - inverted ∩ inverted → always true (both cover infinitely many layers
     *                           beyond their finite exclusion sets)
     */
    bool intersects(const RenderLayers& other) const noexcept {
        if (!inverted && !other.inverted) {
            return bits.intersect(other.bits);
        }
        if (!inverted && other.inverted) {
            // any bit in self.bits that is NOT excluded by other
            return bits.difference_count(other.bits) > 0;
        }
        if (inverted && !other.inverted) {
            // any bit in other.bits that is NOT excluded by self
            return other.bits.difference_count(bits) > 0;
        }
        // inverted ∩ inverted: both represent infinite sets — always overlap
        return true;
    }
};

/** @brief View frustum as 6 plane half-spaces (Bevy bevy_camera
 * primitives::Frustum). Each plane is (normal.xyz, d) with a point in front
 * when dot(normal, p) + d >= 0. */
EPIX_EXPORT struct Frustum {
    /** @brief The six planes: left, right, bottom, top, near, far. */
    std::array<glm::vec4, 6> planes{};

    /** @brief Extract the planes from a clip-from-world matrix (Bevy
     * Frustum::from_view_projection, Gribb-Hartmann). */
    static Frustum from_view_projection(const glm::mat4& clip_from_world) noexcept {
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
        extract(2, row3 + row(1));  // bottom
        extract(3, row3 - row(1));  // top
        extract(4, row3 + row(2));  // near
        extract(5, row3 - row(2));  // far
        return frustum;
    }
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
};

/** @brief MSAA sample count for a camera view (Bevy 0.18 Msaa). */
EPIX_EXPORT enum class Msaa : std::uint32_t {
    Off     = 1,
    Sample2 = 2,
    Sample4 = 4,
    Sample8 = 8,
};

/** @brief Sample count of an Msaa value. */
EPIX_EXPORT inline std::uint32_t samples(Msaa msaa) noexcept { return static_cast<std::uint32_t>(msaa); }

/** @brief Convert a raw sample count to Msaa. Throws for unsupported counts. */
EPIX_EXPORT inline Msaa msaa_from_samples(std::uint32_t sample_count) {
    switch (sample_count) {
        case 1:
            return Msaa::Off;
        case 2:
            return Msaa::Sample2;
        case 4:
            return Msaa::Sample4;
        case 8:
            return Msaa::Sample8;
        default:
            throw std::runtime_error("Unsupported MSAA sample count: " + std::to_string(sample_count));
    }
}

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

/** @brief Marks entities visible per camera view: sets the per-view bit and
 * collects them into the camera's VisibleEntities (Bevy check_visibility).
 * Frustum culling is not applied yet — epix has no per-entity bounds; the
 * Frustum is kept in the query for that work. */
EPIX_EXPORT void check_visibility_system(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                     const Camera&,
                                     epix::ecs::Mut<VisibleEntities>,
                                     const RenderLayers&,
                                     const Frustum&>> cameras,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                     const InheritedVisibility&,
                                     epix::ecs::Mut<ViewVisibility>,
                                     epix::ecs::Opt<const RenderLayers&>>> entities);

/** @brief Recomputes each camera's Frustum from its projection and transform
 * (Bevy update_frusta). */
EPIX_EXPORT void update_frusta(
    epix::ecs::Query<epix::ecs::Item<const Camera&, const ::epix::transform::GlobalTransform&, epix::ecs::Mut<Frustum>>>
        cameras);

}  // namespace epix::camera
