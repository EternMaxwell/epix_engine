/**
 * @file epix.render.cppm
 * @brief C++20 module interface for the render system.
 *
 * This module provides Vulkan-based rendering functionality using NVRHI.
 */
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Third-party headers that cannot be modularized
#include <nvrhi/nvrhi.h>
#include <glm/glm.hpp>

export module epix.render;

export import epix.core;
export import epix.assets;
export import epix.window;
export import epix.image;

export namespace epix::render {

// Forward declarations for render types
struct RenderT;

/**
 * @brief Render pipeline identifier.
 */
struct RenderPipelineId {
    uint64_t id = 0;
};

/**
 * @brief Plugin for the render system.
 */
struct RenderPlugin {
    int validation = 0;

    /**
     * @brief Set the validation level for the render plugin.
     * 0 - No validation
     * 1 - Nvrhi validation
     * 2 - Vulkan validation layers
     */
    RenderPlugin& set_validation(int level = 0);
    void build(epix::core::App& app);
    void finish(epix::core::App& app);
    void finalize(epix::core::App& app);
};

/**
 * @brief Main render system function.
 */
void render_system(epix::core::World& world);

}  // namespace epix::render

export namespace epix::render::camera {

/**
 * @brief Extracted camera data for rendering.
 */
struct ExtractedCamera {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

}  // namespace epix::render::camera

export namespace epix::render::view {

/**
 * @brief Extracted view data for rendering.
 */
struct ExtractedView {
    glm::mat4 world_to_clip;
    glm::mat4 clip_to_world;
};

/**
 * @brief Render target for a view.
 */
struct ViewTarget {
    nvrhi::TextureHandle color_target;
    nvrhi::TextureHandle depth_target;
};

}  // namespace epix::render::view

export namespace epix::render::render_phase {

/**
 * @brief Draw context for render commands.
 */
struct DrawContext {
    nvrhi::CommandListHandle commandlist;
    nvrhi::GraphicsState graphics_state;
};

/**
 * @brief Concept for render phase items.
 */
template <typename T>
concept PhaseItem = requires(T t) {
    { t.entity() } -> std::same_as<epix::core::Entity>;
    { t.batch_size() } -> std::same_as<uint32_t>;
};

/**
 * @brief Render phase container.
 * @tparam T The phase item type.
 */
template <typename T>
struct RenderPhase {
    std::vector<T> items;

    void clear() { items.clear(); }
    void push(T&& item) { items.push_back(std::move(item)); }
};

/**
 * @brief Draw functions registry.
 * @tparam T The phase item type.
 */
template <typename T>
struct DrawFunctions {
    // Implementation details omitted
};

}  // namespace epix::render::render_phase

export namespace epix::render::core_2d {

/**
 * @brief Transparent 2D render phase item.
 */
struct Transparent2D {
    epix::core::Entity _entity;
    uint32_t _batch_size = 1;
    float sort_key       = 0.0f;

    epix::core::Entity entity() const { return _entity; }
    uint32_t batch_size() const { return _batch_size; }
};

}  // namespace epix::render::core_2d

export namespace epix::render::assets {

/**
 * @brief Render assets container.
 * @tparam T The asset type.
 */
template <typename T>
struct RenderAssets {
    // Maps asset IDs to GPU resources
    // Implementation details omitted
};

}  // namespace epix::render::assets
