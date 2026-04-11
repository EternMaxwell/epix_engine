export module epix.extension.grid_gpu:svo;

import std;
import epix.extension.grid;

// Bring fixed-width integer types into scope (MSVC only exports them under std::)
using std::int32_t;
using std::int64_t;
using std::size_t;
using std::uint32_t;
using std::uint64_t;

// ============================================================
// SvoBuffer - Laine & Karras 2010-style flat sparse voxel tree
//             for N-dimensional grids
//
//   cpn = CC^Dim must fit in a uint32 node descriptor using 2*cpn bits,
//   so the hard constraint is:  CC^Dim <= 16
//   (e.g. CC=2 allows Dim 1-4; CC=4 allows Dim 1-2; CC=8 allows Dim=1)
//
// The approach serialises any tree_extendible_grid or tree_grid by
// walking the public API only (iter_pos / count / coverage).
// An independent internal tree is built from the occupied positions,
// then serialised to a flat uint32 buffer.
//
// ---- Buffer layout (uint32 words) ----
//
//   [0]              : header word
//                       bits  [7:0]  = Dim
//                       bits [15:8]  = depth (number of tree levels)
//                       bits [23:16] = child_per_node (= ChildCount^Dim)
//                       bits [31:24] = reserved
//   [1]              : data_count  (number of occupied cells)
//   [2 .. 2+Dim-1]   : origin[i] reinterpreted as uint32
//   [2+Dim ..]       : node pool, root descriptor first (DFS pre-order)
//
// ---- Node descriptor layout (1 uint32 per interior node) ----
//
//   bits [0         .. cpn-1]:     valid_mask   -- which of the cpn children exist
//   bits [cpn       .. 2*cpn-1]:   leaf_mask    -- which valid children are leaves
//   child_offset is always 1 (child block immediately follows descriptor word)
//                                                  and is NOT encoded in the word
//
// ---- Child block (cpn slots, densely packed for valid children only) ----
//   Slot for child i: slot = child_block_base[popcount(valid_mask & ((1<<i)-1))]
//   Leaf  slot = DATA INDEX (ordinal in CPU iter_pos/iter_cells sequence)
//   Inner slot = absolute word index of child's node descriptor in the pool
//
// ---- Coordinate / child-index convention ----
//   Coverage = ChildCount^depth per axis.
//   Origin stored in buffer; rel[axis] = coord[axis] - origin[axis]
//   At level L (root=0), stride = ChildCount^(depth-1-L)
//   digit[axis] = (rel[axis] / stride) % ChildCount
//   flat_child_index (row-major, axis 0 is MSB):
//     idx = 0; for axis in [0,Dim): idx = idx*ChildCount + digit[axis]
// ============================================================

namespace epix::ext::grid_gpu {

// -------------------------------------------------------
// Public types
// -------------------------------------------------------

/// Decoded header fields from an SvoBuffer.
export struct SvoHeader {
    uint32_t dim;
    uint32_t depth;
    uint32_t child_per_node;
    uint32_t data_count;
};

/**
 * @brief Flat single-buffer Sparse Voxel Tree ready for GPU upload.
 *
 * Produced by svo_upload() from any tree grid.  Upload words.data()
 * (byte_size() bytes) to a GPU storage buffer or ByteAddressBuffer.
 *
 * The DATA INDEX in leaf slots is the 0-based ordinal of that cell in the
 * grid's iter_pos() / iter_cells() sequence (matches m_data[index] on CPU).
 */
export struct SvoBuffer {
    std::vector<uint32_t> words;

    SvoHeader header() const noexcept {
        if (words.size() < 2) return {};
        uint32_t h = words[0];
        return {
            .dim            = (h) & 0xFFu,
            .depth          = (h >> 8u) & 0xFFu,
            .child_per_node = (h >> 16u) & 0xFFu,
            .data_count     = words[1],
        };
    }

    std::size_t size() const noexcept { return words.size(); }
    std::size_t byte_size() const noexcept { return words.size() * sizeof(uint32_t); }
    const uint32_t* data() const noexcept { return words.data(); }
};

// -------------------------------------------------------
// Embedded Slang shader library source
// -------------------------------------------------------

/**
 * @brief Slang source for the GPU-side SVO traversal library.
 *
 * This source declares `module "epix/ext/grid/svo"` so other Slang shaders can do:
 *   import epix.ext.grid.svo;
 * and then use SvoGrid1D, SvoGrid2D, or SvoGrid3D.
 *
 * Register with the shader cache via:
 *   Shader::from_slang(std::string(kSvoGridSlangSource), "embedded://epix/shaders/grid/svo.slang")
 */
export constexpr std::string_view kSvoGridSlangSource = R"slang(
// epix.ext.grid.svo - GPU-side SVO traversal for sparse voxel trees
// Companion to epix.extension.grid_gpu (C++ module).
//
// Register the SvoBuffer produced by svo_upload() as a StructuredBuffer<uint>
// and use the SvoGrid1D / SvoGrid2D / SvoGrid3D structs for lookups.

module epix.ext.grid.svo;

// ---- Buffer layout (uint32 words, mirrors SvoBuffer::words) ----
//   [0]             : header = dim(8b) | depth(8b) | cpn(8b) | reserved(8b)
//   [1]             : data_count
//   [2 .. 2+Dim-1]  : origin[i] (bit-cast uint32; int32 for signed, uint32 for fixed grids)
//   [2+Dim ..]      : node pool (root descriptor first, DFS pre-order)
//
// ---- Node descriptor (1 uint32) ----
//   bits [0..cpn-1]     : valid_mask
//   bits [cpn..2*cpn-1] : leaf_mask
//   (no further bits used; child block is always at desc+1)
//
// ---- Child block (valid_count compact slots) ----
//   Slot for child i = child_block_base[popcount(valid_mask & ((1<<i)-1))]
//   Leaf  slot = DATA INDEX into the parallel user data buffer
//   Inner slot = absolute word index of child descriptor in the pool
//
// ---- Coordinate convention (CC = branching factor of the SvoGrid generic below) ----
//   rel[axis] = coord[axis] - origin[axis]
//   stride at level L = 2^(depth-1-L)
//   digit[axis] = (rel[axis] >> (depth-1-L)) & 1
//   flat_child_index (row-major, axis 0 MSB):
//     idx = 0; for axis in [0,Dim): idx = idx*2 + digit[axis]

namespace epix::ext::grid {

// ============================================================
// Shared helpers
// ============================================================

uint svo_popcount_below(uint mask, uint i)
{
    return countbits(mask & ((1u << i) - 1u));
}

void svo_decode_node(uint word, uint cpn, out uint valid_mask, out uint leaf_mask)
{
    valid_mask = word & ((1u << cpn) - 1u);
    leaf_mask  = (word >> cpn) & ((1u << cpn) - 1u);
}

// ============================================================
// SvoGrid<Dim, CC> - generic N-D / CC-ary SVO traversal
// ============================================================

public struct SvoGrid<let Dim : int, let CC : int>
{
    public StructuredBuffer<uint> buf;

    public uint depth()          { return (buf[0] >> 8u) & 0xFFu; }
    public uint data_count()     { return buf[1]; }
    public uint pool_base()      { return 2u + uint(Dim); }
    public int  origin(int axis) { return int(buf[2 + axis]); }

    uint __pow_cc(uint exp) {
        uint r = 1u;
        for (uint i = 0u; i < exp; ++i) r *= uint(CC);
        return r;
    }

    uint __flat_ci(uint[Dim] rel, uint stride) {
        uint idx = 0u;
        for (int a = 0; a < Dim; ++a)
            idx = idx * uint(CC) + (rel[a] / stride) % uint(CC);
        return idx;
    }

    public int lookup(int[Dim] pos) {
        uint d = depth();
        if (d == 0u) return -1;
        uint cpn = 1u;
        for (int i = 0; i < Dim; ++i) cpn *= uint(CC);
        uint[Dim] rel;
        for (int a = 0; a < Dim; ++a)
            rel[a] = uint(pos[a] - origin(a));
        uint cov = __pow_cc(d);
        for (int a = 0; a < Dim; ++a)
            if (rel[a] >= cov) return -1;
        uint stride = cov / uint(CC);
        uint node_idx = pool_base();
        for (uint level = 0u; level < d; ++level) {
            uint ci = __flat_ci(rel, stride);
            stride /= uint(CC);
            uint word = buf[node_idx];
            uint valid_mask, leaf_mask;
            svo_decode_node(word, cpn, valid_mask, leaf_mask);
            if ((valid_mask & (1u << ci)) == 0u) return -1;
            uint slot_pos = node_idx + 1u + svo_popcount_below(valid_mask, ci);
            if ((leaf_mask & (1u << ci)) != 0u) return int(buf[slot_pos]);
            node_idx = buf[slot_pos];
        }
        return -1;
    }

    public bool contains(int[Dim] pos) { return lookup(pos) >= 0; }
};

public typealias SvoGrid1D = SvoGrid<1, 2>;
public typealias SvoGrid2D = SvoGrid<2, 2>;
public typealias SvoGrid3D = SvoGrid<3, 2>;

}  // namespace epix::ext::grid
)slang";

// -------------------------------------------------------
// Internal implementation
// -------------------------------------------------------
namespace detail {

static constexpr uint32_t SVO_EMPTY = 0xFFFF'FFFFu;

// child_per_node = ChildCount^Dim at compile time
template <std::size_t Dim, std::size_t ChildCount>
inline constexpr uint32_t cpn_v = [] {
    uint32_t r = 1;
    for (std::size_t i = 0; i < Dim; ++i) r *= static_cast<uint32_t>(ChildCount);
    return r;
}();

// popcount of bits below position i
inline uint32_t popcount_below(uint32_t mask, uint32_t i) noexcept {
    return static_cast<uint32_t>(std::popcount(mask & ((1u << i) - 1u)));
}

// Build one node descriptor word.
// child_offset is always 1 and is NOT encoded in the descriptor;
// the child block is implicitly at desc_pos+1.
inline uint32_t make_descriptor(uint32_t valid, uint32_t leaf, uint32_t cpn) noexcept { return valid | (leaf << cpn); }

// -------------------------------------------------------
// Builder tree (built from occupied positions via public API)
// -------------------------------------------------------

struct BuildNode {
    std::vector<uint32_t> children;  // size = cpn; SVO_EMPTY = absent slot
    explicit BuildNode(uint32_t cpn) : children(cpn, SVO_EMPTY) {}
};

struct BuildTree {
    uint32_t cpn;
    uint32_t depth;
    uint32_t child_count;  // per-axis branching = ChildCount
    std::vector<BuildNode> nodes;

    BuildTree(uint32_t cpn_, uint32_t depth_, uint32_t child_count_)
        : cpn(cpn_), depth(depth_), child_count(child_count_) {
        nodes.emplace_back(cpn);  // root at index 0
    }

    uint32_t pow_cc(uint32_t exp) const noexcept {
        uint32_t r = 1;
        for (uint32_t i = 0; i < exp; ++i) r *= child_count;
        return r;
    }

    template <std::size_t Dim>
    uint32_t flat_child_idx(const std::array<uint32_t, Dim>& rel, uint32_t stride) const noexcept {
        uint32_t idx = 0;
        for (std::size_t axis = 0; axis < Dim; ++axis) {
            uint32_t digit = (rel[axis] / stride) % child_count;
            idx            = idx * child_count + digit;
        }
        return idx;
    }

    template <std::size_t Dim>
    void insert(const std::array<uint32_t, Dim>& rel, uint32_t data_index) {
        uint32_t cur = 0;
        for (uint32_t level = 0; level < depth; ++level) {
            uint32_t stride = pow_cc(depth - level - 1u);
            uint32_t ci     = flat_child_idx<Dim>(rel, stride);

            if (level + 1u == depth) {
                // Parent-of-leaves level: slot holds data index
                nodes[cur].children[ci] = data_index;
            } else {
                uint32_t& slot = nodes[cur].children[ci];
                if (slot == SVO_EMPTY) {
                    slot = static_cast<uint32_t>(nodes.size());
                    nodes.emplace_back(cpn);
                }
                cur = slot;
            }
        }
    }

    // DFS serialise into out[].  Returns absolute word index of the descriptor written.
    uint32_t serialize(std::vector<uint32_t>& out, uint32_t node_idx, uint32_t level) {
        const BuildNode& node       = nodes[node_idx];
        const bool parent_of_leaves = (level + 1u == depth);

        uint32_t valid_mask = 0u;
        uint32_t leaf_mask  = 0u;
        for (uint32_t i = 0; i < cpn; ++i) {
            if (node.children[i] != SVO_EMPTY) {
                valid_mask |= (1u << i);
                if (parent_of_leaves) leaf_mask |= (1u << i);
            }
        }

        uint32_t valid_count = static_cast<uint32_t>(std::popcount(valid_mask));

        // Allocate: 1 descriptor word + valid_count child slots (contiguous)
        uint32_t desc_pos      = static_cast<uint32_t>(out.size());
        uint32_t child_blk_pos = desc_pos + 1u;
        out.resize(out.size() + 1u + valid_count, 0u);

        // child_offset = 1 is NOT encoded; child block is always at desc_pos+1
        out[desc_pos] = make_descriptor(valid_mask, leaf_mask, cpn);

        // Collect interior children for post-recursion fixup
        struct Pending {
            uint32_t slot_pos;
            uint32_t child_node;
        };
        std::vector<Pending> pending;

        for (uint32_t i = 0; i < cpn; ++i) {
            if (!((valid_mask >> i) & 1u)) continue;
            uint32_t slot_pos = child_blk_pos + popcount_below(valid_mask, i);
            if (parent_of_leaves) {
                out[slot_pos] = node.children[i];  // data index
            } else {
                pending.push_back({slot_pos, node.children[i]});
            }
        }

        // Recurse -- must be done after the current block is fully allocated
        for (auto& p : pending) {
            uint32_t child_desc_pos = serialize(out, p.child_node, level + 1u);
            out[p.slot_pos]         = child_desc_pos;  // absolute word index
        }

        return desc_pos;
    }
};

// -------------------------------------------------------
// Header helpers
// -------------------------------------------------------

template <std::size_t Dim>
void push_header(std::vector<uint32_t>& words,
                 uint32_t depth,
                 uint32_t cpn,
                 uint32_t data_count,
                 const std::array<int32_t, Dim>& origin) {
    uint32_t h = (static_cast<uint32_t>(Dim) & 0xFFu) | ((depth & 0xFFu) << 8u) | ((cpn & 0xFFu) << 16u);
    words.push_back(h);
    words.push_back(data_count);
    for (std::size_t i = 0; i < Dim; ++i) words.push_back(static_cast<uint32_t>(origin[i]));
}

template <std::size_t Dim>
void push_header(std::vector<uint32_t>& words,
                 uint32_t depth,
                 uint32_t cpn,
                 uint32_t data_count,
                 const std::array<uint32_t, Dim>& origin) {
    uint32_t h = (static_cast<uint32_t>(Dim) & 0xFFu) | ((depth & 0xFFu) << 8u) | ((cpn & 0xFFu) << 16u);
    words.push_back(h);
    words.push_back(data_count);
    for (std::size_t i = 0; i < Dim; ++i) words.push_back(origin[i]);
}

// -------------------------------------------------------
// Core serialise helper
// -------------------------------------------------------

template <std::size_t Dim, std::size_t ChildCount>
void build_and_serialize(std::vector<uint32_t>& words,
                         uint32_t depth,
                         const std::vector<std::pair<std::array<uint32_t, Dim>, uint32_t>>& cells) {
    constexpr uint32_t CPN = cpn_v<Dim, ChildCount>;
    if (cells.empty()) return;
    BuildTree tree(CPN, depth, static_cast<uint32_t>(ChildCount));
    for (auto& [rel, idx] : cells) tree.insert<Dim>(rel, idx);
    tree.serialize(words, 0u, 0u);
}

// -------------------------------------------------------
// Depth computation from a coverage value
// -------------------------------------------------------
inline uint32_t depth_from_coverage(uint32_t cov, uint32_t child_count) noexcept {
    if (cov <= 1u) return 1u;
    uint32_t depth = 0u;
    uint32_t c     = 1u;
    while (c < cov) {
        c *= child_count;
        ++depth;
    }
    return depth;
}

// -------------------------------------------------------
// Compile-time-CC upload helpers (dispatch targets)
// -------------------------------------------------------

template <std::size_t CC, std::size_t Dim, typename CoordT, epix::ext::grid::tree_based_grid G>
    requires(cpn_v<Dim, CC> <= 16)
SvoBuffer svo_upload_tree_cc(const G& grid) {
    constexpr uint32_t CPN = cpn_v<Dim, CC>;
    SvoBuffer buf;
    const uint32_t n = static_cast<uint32_t>(grid.count());

    if (n == 0) {
        std::array<CoordT, Dim> zero_origin{};
        push_header<Dim>(buf.words, 0u, CPN, 0u, zero_origin);
        return buf;
    }

    std::array<CoordT, Dim> origin;
    {
        bool first = true;
        for (const auto& pos : grid.iter_pos()) {
            if (first) {
                origin = pos;
                first  = false;
                continue;
            }
            for (std::size_t axis = 0; axis < Dim; ++axis) origin[axis] = std::min(origin[axis], pos[axis]);
        }
    }

    const uint32_t depth = depth_from_coverage(grid.coverage(), static_cast<uint32_t>(CC));

    std::vector<std::pair<std::array<uint32_t, Dim>, uint32_t>> cells;
    cells.reserve(n);
    uint32_t idx = 0;
    for (const auto& pos : grid.iter_pos()) {
        std::array<uint32_t, Dim> rel;
        for (std::size_t axis = 0; axis < Dim; ++axis) rel[axis] = static_cast<uint32_t>(pos[axis] - origin[axis]);
        cells.push_back({rel, idx++});
    }

    push_header<Dim>(buf.words, depth, CPN, n, origin);
    build_and_serialize<Dim, CC>(buf.words, depth, cells);
    return buf;
}

template <std::size_t CC, std::size_t Dim, typename CoordT, epix::ext::grid::any_grid G>
    requires(!epix::ext::grid::tree_based_grid<G> && cpn_v<Dim, CC> <= 16)
SvoBuffer svo_upload_flat_cc(const G& grid) {
    constexpr uint32_t CPN = cpn_v<Dim, CC>;
    SvoBuffer buf;
    const uint32_t n = static_cast<uint32_t>(grid.count());

    if (n == 0) {
        std::array<CoordT, Dim> zero_origin{};
        push_header<Dim>(buf.words, 0u, CPN, 0u, zero_origin);
        return buf;
    }

    std::array<CoordT, Dim> origin;
    {
        bool first = true;
        for (const auto& pos : grid.iter_pos()) {
            if (first) {
                origin = pos;
                first  = false;
                continue;
            }
            for (std::size_t axis = 0; axis < Dim; ++axis) origin[axis] = std::min(origin[axis], pos[axis]);
        }
    }

    std::vector<std::pair<std::array<uint32_t, Dim>, uint32_t>> cells;
    cells.reserve(n);
    uint32_t max_rel = 0u, idx = 0;
    for (const auto& pos : grid.iter_pos()) {
        std::array<uint32_t, Dim> rel;
        for (std::size_t axis = 0; axis < Dim; ++axis) {
            rel[axis] = static_cast<uint32_t>(pos[axis] - origin[axis]);
            max_rel   = std::max(max_rel, rel[axis]);
        }
        cells.push_back({rel, idx++});
    }

    const uint32_t depth = depth_from_coverage(max_rel + 1u, static_cast<uint32_t>(CC));
    push_header<Dim>(buf.words, depth, CPN, n, origin);
    build_and_serialize<Dim, CC>(buf.words, depth, cells);
    return buf;
}

}  // namespace detail

// -------------------------------------------------------
// GPU upload configuration
// -------------------------------------------------------

/**
 * @brief Detailed error returned by svo_upload().
 *
 * Internally holds a std::variant of public sub-structs, one per error category.
 * Use is<E>() to check the active type, get<E>() to access it, or message() for
 * a human-readable string.
 *
 * Example:
 *   if (auto e = err.get<SvoUploadError::InvalidChildCount>())
 *       std::println("bad child_count: {}", e->provided);
 */
export struct SvoUploadError {
    // ---- error sub-types ------------------------------------------------

    /** @brief child_count was unsupported or incompatible with the grid dimension. */
    struct InvalidChildCount {
        std::size_t provided; /**< The child_count value that was supplied. */
        std::size_t dim;      /**< The grid dimension for which this was attempted. */

        std::string message() const {
            if (provided != 2 && provided != 4 && provided != 8)
                return std::format("svo_upload: unsupported child_count {} (must be 2, 4, or 8)", provided);
            std::size_t cpn = 1;
            for (std::size_t i = 0; i < dim; ++i) cpn *= provided;
            return std::format(
                "svo_upload: child_count {} is incompatible with a {}-dimensional grid "
                "(cpn = {}^{} = {} > 16; constraint: cpn <= 16)",
                provided, dim, provided, dim, cpn);
        }
    };

    // ---- variant --------------------------------------------------------

    using Data = std::variant<InvalidChildCount>;

    Data data;

    // ---- constructors ---------------------------------------------------

    template <typename E>
        requires std::constructible_from<Data, E>
    explicit SvoUploadError(E&& e) : data(std::forward<E>(e)) {}

    // ---- query helpers --------------------------------------------------

    /** @brief Returns true when the active error type is E. */
    template <typename E>
    bool is() const noexcept {
        return std::holds_alternative<E>(data);
    }

    /**
     * @brief Returns a pointer to the active error value if it is of type E,
     *        or nullptr otherwise.
     */
    template <typename E>
    const E* get() const noexcept {
        return std::get_if<E>(&data);
    }

    template <typename E>
    E* get() noexcept {
        return std::get_if<E>(&data);
    }

    /** @brief Human-readable description dispatched through the variant. */
    std::string message() const {
        return std::visit([](const auto& e) { return e.message(); }, data);
    }
};

/**
 * @brief Configuration for the GPU-side SVO produced by svo_upload().
 *
 * The GPU SVO structure is independent of the CPU grid's storage — it can use
 * any branching factor regardless of how data is organised on the CPU.
 * Supported child_count values: 2, 4, 8.
 */
export struct SvoConfig {
    /** @brief Per-axis branching factor for the GPU tree (default: 2 = binary tree). */
    std::size_t child_count = 2;
};

// -------------------------------------------------------
// Public API
// -------------------------------------------------------

/**
 * @brief Serialize any tree-based grid (has coverage()) into a flat SvoBuffer.
 *
 * @tparam G      Grid type satisfying tree_based_grid.
 * @param  grid   Source grid.
 * @param  config GPU tree configuration (default: binary tree with child_count=2).
 * @return SvoBuffer on success, or SvoUploadError (kind=InvalidChildCount) for unsupported child_count.
 *
 * DATA INDEX in each leaf = 0-based ordinal of that cell in grid.iter_pos().
 */
export template <epix::ext::grid::tree_based_grid G>
    requires(epix::ext::grid::grid_trait<G>::dim >= 1)
std::expected<SvoBuffer, SvoUploadError> svo_upload(const G& grid, const SvoConfig& config = {}) {
    using Trait               = epix::ext::grid::grid_trait<G>;
    constexpr std::size_t Dim = Trait::dim;
    using CoordT              = typename Trait::coord_type;
    switch (config.child_count) {
        case 2:
            if constexpr (detail::cpn_v<Dim, 2> <= 16) return detail::svo_upload_tree_cc<2, Dim, CoordT>(grid);
            break;
        case 4:
            if constexpr (detail::cpn_v<Dim, 4> <= 16) return detail::svo_upload_tree_cc<4, Dim, CoordT>(grid);
            break;
        case 8:
            if constexpr (detail::cpn_v<Dim, 8> <= 16) return detail::svo_upload_tree_cc<8, Dim, CoordT>(grid);
            break;
        default:
            break;
    }
    return std::unexpected(SvoUploadError{SvoUploadError::InvalidChildCount{config.child_count, Dim}});
}

/**
 * @brief Serialize any flat (non-tree) grid into a flat SvoBuffer.
 *
 * Works for dense_grid, sparse_grid, dense_extendible_grid, packed_grid, and
 * any other grid type without coverage(). Depth is computed from the occupied
 * bounding box.
 *
 * @tparam G      Grid type satisfying any_grid but not tree_based_grid.
 * @param  grid   Source grid.
 * @param  config GPU tree configuration (default: binary tree with child_count=2).
 * @return SvoBuffer on success, or SvoUploadError (kind=InvalidChildCount) for unsupported child_count.
 */
export template <epix::ext::grid::any_grid G>
    requires(!epix::ext::grid::tree_based_grid<G> && epix::ext::grid::grid_trait<G>::dim >= 1)
std::expected<SvoBuffer, SvoUploadError> svo_upload(const G& grid, const SvoConfig& config = {}) {
    using Trait               = epix::ext::grid::grid_trait<G>;
    constexpr std::size_t Dim = Trait::dim;
    using CoordT              = typename Trait::coord_type;
    switch (config.child_count) {
        case 2:
            if constexpr (detail::cpn_v<Dim, 2> <= 16) return detail::svo_upload_flat_cc<2, Dim, CoordT>(grid);
            break;
        case 4:
            if constexpr (detail::cpn_v<Dim, 4> <= 16) return detail::svo_upload_flat_cc<4, Dim, CoordT>(grid);
            break;
        case 8:
            if constexpr (detail::cpn_v<Dim, 8> <= 16) return detail::svo_upload_flat_cc<8, Dim, CoordT>(grid);
            break;
        default:
            break;
    }
    return std::unexpected(SvoUploadError{SvoUploadError::InvalidChildCount{config.child_count, Dim}});
}

}  // namespace epix::ext::grid_gpu
