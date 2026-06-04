module;
#include <epix/extension/grid.hpp>

export module epix.extension.grid;

export namespace epix::ext::grid {
using epix::ext::grid::any_grid;
using epix::ext::grid::any_grid_view;
using epix::ext::grid::basic_grid;
using epix::ext::grid::BinaryGrid;
using epix::ext::grid::bit_grid;
using epix::ext::grid::Chunk;
using epix::ext::grid::chunk_element;
using epix::ext::grid::chunk_element_const_view;
using epix::ext::grid::chunk_element_view;
using epix::ext::grid::ChunkGridError;
using epix::ext::grid::ChunkLayer;
using epix::ext::grid::ChunkLayerError;
using epix::ext::grid::counted_grid;
using epix::ext::grid::dense_extendible_grid;
using epix::ext::grid::dense_grid;
using epix::ext::grid::ExtendibleChunkGrid;
using epix::ext::grid::ExtendibleChunkRefGrid;
using epix::ext::grid::ExtendibleMutChunkRefGrid;
using epix::ext::grid::ExtendibleRefChunkRefGrid;
using epix::ext::grid::find_holes;
using epix::ext::grid::find_outline;
using epix::ext::grid::get_category;
using epix::ext::grid::get_polygon;
using epix::ext::grid::get_polygon_simplified;
using epix::ext::grid::get_polygons_multi;
using epix::ext::grid::get_polygons_simplified_multi;
using epix::ext::grid::grid_category;
using epix::ext::grid::grid_container;
using epix::ext::grid::grid_error;
using epix::ext::grid::grid_trait;
using epix::ext::grid::iterable_grid;
using epix::ext::grid::LayerError;
using epix::ext::grid::packed_grid;
using epix::ext::grid::Polygon;
using epix::ext::grid::rasterise;
using epix::ext::grid::recursive_grid;
using epix::ext::grid::Ring;
using epix::ext::grid::satisfies_category;
using epix::ext::grid::sparse_grid;
using epix::ext::grid::tree_extendible_grid;
using epix::ext::grid::tree_grid;
using epix::ext::grid::unsafe_grid_container;
using epix::ext::grid::unsafe_viewable_grid;
using epix::ext::grid::viewable_grid;
using epix::ext::grid::operator|;
using epix::ext::grid::operator&;
}  // namespace epix::ext::grid

export namespace epix::ext::grid::layers {
using epix::ext::grid::layers::BasicGridLayer;
using epix::ext::grid::layers::BasicGridRefLayer;
using epix::ext::grid::layers::DenseLayer;
using epix::ext::grid::layers::PackedLayer;
using epix::ext::grid::layers::SparseLayer;
using epix::ext::grid::layers::TreeLayer;
}  // namespace epix::ext::grid::layers

export namespace epix::ext::grid::views {
using epix::ext::grid::views::filter;
using epix::ext::grid::views::filter_view;
using epix::ext::grid::views::offset;
using epix::ext::grid::views::offset_view;
using epix::ext::grid::views::shadow;
using epix::ext::grid::views::shadow_view;
using epix::ext::grid::views::transform;
using epix::ext::grid::views::transform_view;
}  // namespace epix::ext::grid::views
