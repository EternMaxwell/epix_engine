// epix - Unified root module interface
// This module re-exports all epix engine modules

export module epix;

// Re-export all engine modules
export import epix_core;
export import epix_input;
export import epix_assets;
export import epix_transform;
export import epix_image;
export import epix_window;
export import epix_glfw;
export import epix_render;
export import epix_core_graph;
export import epix_sprite;

// All namespaces are now available through this single import:
// - epix::core
// - epix::input
// - epix::assets
// - epix::transform
// - epix::image
// - epix::window
// - epix::glfw
// - epix::render
// - epix::core_graph
// - epix::sprite
