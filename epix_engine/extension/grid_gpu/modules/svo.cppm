module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif

export module epix.extension.grid_gpu:svo;
#ifdef EPIX_IMPORT_STD
import std;
#endif
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

/// Decoded header fields from an SvoBuffer64.
export struct SvoHeader64 {
    uint64_t dim;
    uint64_t depth;
    uint64_t child_per_node;
    uint64_t data_count;
};

/**
 * @brief Flat single-buffer Sparse Voxel Tree in 64-bit words, ready for GPU upload.
 *
 * Mirrors SvoBuffer but every word is a uint64_t, which allows:
 *   - more than 4 billion occupied cells  (data_count and leaf slots are uint64)
 *   - coordinates that do not fit in int32 / uint32
 *   - node-pool word indices beyond 4 billion
 *   - up to 5 dimensions with a binary (CC=2) tree  (cpn <= 32)
 *
 * ---- Buffer layout (uint64 words) ----
 *   [0]            : header word
 *                      bits  [7:0]  = Dim
 *                      bits [15:8]  = depth
 *                      bits [23:16] = child_per_node  (= ChildCount^Dim)
 *                      bits [63:24] = reserved
 *   [1]            : data_count  (uint64)
 *   [2..2+Dim-1]   : origin[i]  (bit-cast uint64; int64 for signed coords)
 *   [2+Dim ..]     : node pool (root first, DFS pre-order)
 *
 * ---- Node descriptor (1 uint64 per interior node) ----
 *   bits [0..cpn-1]       : valid_mask
 *   bits [cpn..2*cpn-1]   : leaf_mask
 *   child block is always at desc+1 (offset NOT encoded)
 *
 * ---- Child block (valid_count compact slots) ----
 *   Leaf  slot = DATA INDEX (uint64, ordinal in iter_pos sequence)
 *   Inner slot = absolute word index of child descriptor in the pool
 *
 * Constraint: ChildCount^Dim <= 32  (so valid_mask+leaf_mask fit in 64 bits)
 *
 * GPU note: bind as StructuredBuffer<uint> (kSvoGridSlangSource64 shader).
 * No shaderInt64 required -- each word is read as a little-endian uint32 pair.
 * The GPU shader further restricts cpn <= 16 so masks fit in the low uint32.
 */
export struct SvoBuffer64 {
    std::vector<uint64_t> words;

    SvoHeader64 header() const noexcept {
        if (words.size() < 2) return {};
        uint64_t h = words[0];
        return {
            .dim            = (h) & 0xFFu,
            .depth          = (h >> 8u) & 0xFFu,
            .child_per_node = (h >> 16u) & 0xFFu,
            .data_count     = words[1],
        };
    }

    std::size_t size() const noexcept { return words.size(); }
    std::size_t byte_size() const noexcept { return words.size() * sizeof(uint64_t); }
    const uint64_t* data() const noexcept { return words.data(); }
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
// Construct with the bound buffer: `SvoGrid3D grid = SvoGrid3D(svo_buf);`

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

public struct SvoProbe<let Dim : int>
{
    public int data_index;
    public uint state;      // 0 = outside, 1 = empty child, 2 = interior node, 3 = leaf hit
    public uint level;      // root = 0, first child level = 1
    public uint cell_size;  // coverage per axis for the resolved cell
    public int[Dim] cell_min;
};

public struct SvoGrid<let Dim : int, let CC : int>
{
    StructuredBuffer<uint> buf;

    uint depth_cache_;
    uint data_count_cache_;
    uint pool_base_cache_;
    uint coverage_cache_;
    uint cpn_cache_;
    int[Dim] origin_cache_;

    public __init(StructuredBuffer<uint> buffer) {
        this.buf          = buffer;
        this.depth_cache_ = (buffer[0] >> 8u) & 0xFFu;
        this.data_count_cache_ = buffer[1];
        this.pool_base_cache_  = 2u + uint(Dim);
        this.cpn_cache_        = 1u;
        for (int a = 0; a < Dim; ++a) {
            this.origin_cache_[a] = int(buffer[2 + a]);
            this.cpn_cache_ *= uint(CC);
        }
        this.coverage_cache_ = __pow_cc(this.depth_cache_);
    }

    public uint depth() { return depth_cache_; }

    public uint data_count() { return data_count_cache_; }

    public uint pool_base() { return pool_base_cache_; }

    public uint coverage() { return coverage_cache_; }

    public int origin(int axis) { return origin_cache_[axis]; }

    uint __pow_cc(uint exp) {
        uint r = 1u;
        for (uint i = 0u; i < exp; ++i) r *= uint(CC);
        return r;
    }

    void __clear_probe(out SvoProbe<Dim> probe) {
        probe.data_index = -1;
        probe.state      = 0u;
        probe.level      = 0u;
        probe.cell_size  = 0u;
        for (int a = 0; a < Dim; ++a)
            probe.cell_min[a] = 0;
    }

    void __store_probe(out SvoProbe<Dim> probe, int data_index, uint state, uint level, uint cell_size, int[Dim] cell_min) {
        probe.data_index = data_index;
        probe.state      = state;
        probe.level      = level;
        probe.cell_size  = cell_size;
        for (int a = 0; a < Dim; ++a)
            probe.cell_min[a] = cell_min[a];
    }

    uint __flat_ci(uint[Dim] rel, uint stride, out uint[Dim] digits) {
        if (stride == 0u) {
            for (int a = 0; a < Dim; ++a)
                digits[a] = 0u;
            return 0u;
        }
        uint idx = 0u;
        for (int a = 0; a < Dim; ++a) {
            uint digit = (rel[a] / stride) % uint(CC);
            digits[a]  = digit;
            idx        = idx * uint(CC) + digit;
        }
        return idx;
    }

    void __child_min(out int[Dim] dst, int[Dim] base_min, uint[Dim] digits, uint stride) {
        for (int a = 0; a < Dim; ++a)
            dst[a] = base_min[a] + int(digits[a] * stride);
    }

    public SvoProbe<Dim> probe_lod(int[Dim] pos, uint max_levels) {
        SvoProbe<Dim> result;
        __clear_probe(result);

        if (depth_cache_ == 0u) return result;

        uint[Dim] rel;
        for (int a = 0; a < Dim; ++a)
            rel[a] = uint(pos[a] - origin_cache_[a]);
        for (int a = 0; a < Dim; ++a)
            if (rel[a] >= coverage_cache_) return result;

        __store_probe(result, -1, 2u, 0u, coverage_cache_, origin_cache_);
        if (max_levels == 0u) return result;

        uint stop_levels = min(max_levels, depth_cache_);
        uint stride      = coverage_cache_ / uint(CC);
        uint node_idx    = pool_base_cache_;
        int[Dim] cell_min;
        for (int a = 0; a < Dim; ++a)
            cell_min[a] = origin_cache_[a];

        for (uint level = 0u; level < stop_levels; ++level) {
            uint[Dim] digits;
            uint ci = __flat_ci(rel, stride, digits);
            int[Dim] child_min;
            __child_min(child_min, cell_min, digits, stride);
            uint word = buf[node_idx];
            uint valid_mask, leaf_mask;
            svo_decode_node(word, cpn_cache_, valid_mask, leaf_mask);
            uint child_bit = 1u << ci;
            if ((valid_mask & child_bit) == 0u) {
                __store_probe(result, -1, 1u, level + 1u, stride, child_min);
                return result;
            }
            uint slot_pos = node_idx + 1u + svo_popcount_below(valid_mask, ci);
            if ((leaf_mask & child_bit) != 0u) {
                __store_probe(result, int(buf[slot_pos]), 3u, level + 1u, stride, child_min);
                return result;
            }

            __store_probe(result, -1, 2u, level + 1u, stride, child_min);
            if (level + 1u >= stop_levels) return result;

            node_idx = buf[slot_pos];
            for (int a = 0; a < Dim; ++a)
                cell_min[a] = child_min[a];
            stride /= uint(CC);
        }

        return result;
    }

    public SvoProbe<Dim> probe(int[Dim] pos) {
        return probe_lod(pos, depth());
    }

    public int lookup(int[Dim] pos) {
        SvoProbe<Dim> result = probe(pos);
        return result.state == 3u ? result.data_index : -1;
    }

    public bool contains(int[Dim] pos) { return lookup(pos) >= 0; }

    public SvoRayHit<Dim> trace_ray(float[Dim] ray_origin, float[Dim] ray_dir, int max_steps) {
        SvoRayHit<Dim> result;
        result.data_index = -1;
        result.t          = 0.0f;
        result.hit_axis   = -1;
        result.hit_sign   = 0;
        for (int a = 0; a < Dim; ++a) result.cell_pos[a] = 0;
    
        // Compute safe inverse direction (avoid division by zero).
        float[Dim] inv_dir;
        float[Dim] sign_dir;
        for (int a = 0; a < Dim; ++a) {
            sign_dir[a] = ray_dir[a] >= 0.0f ? 1.0f : -1.0f;
            inv_dir[a]  = sign_dir[a] / max(abs(ray_dir[a]), 1e-20f);
        }
    
        // Scene AABB.
        float[Dim] scene_min, scene_max;
        float scene_dim = float(coverage());
        for (int a = 0; a < Dim; ++a) {
            scene_min[a] = float(origin(a));
            scene_max[a] = scene_min[a] + scene_dim;
        }
    
        // Ray-AABB intersection (dimension-generic slab method).
        float t_enter = 0.0f;
        float t_exit  = 1e30f;
        int   enter_axis = -1;
        for (int a = 0; a < Dim; ++a) {
            float t0 = (scene_min[a] - ray_origin[a]) * inv_dir[a];
            float t1 = (scene_max[a] - ray_origin[a]) * inv_dir[a];
            float tlo = min(t0, t1);
            float thi = max(t0, t1);
            if (tlo > t_enter) { t_enter = tlo; enter_axis = a; }
            t_exit = min(t_exit, thi);
        }
        if (t_exit < max(t_enter, 0.0f)) return result;
    
        float t_cur = max(t_enter, 0.0f);
        float t_limit = min(t_exit, 1e20f);
        float eps = 1e-4f;
    
        for (int step_i = 0; step_i < max_steps && t_cur <= t_limit; ++step_i) {
            // Sample position along ray (slightly inside to avoid surface issues).
            float sample_t = min(t_cur + eps, t_limit);
            int[Dim] cell;
            bool inside = true;
            for (int a = 0; a < Dim; ++a) {
                float p = clamp(ray_origin[a] + ray_dir[a] * sample_t, scene_min[a] + eps, scene_max[a] - eps);
                cell[a] = int(floor(p));
                if (float(cell[a]) < scene_min[a] || float(cell[a]) >= scene_max[a])
                    inside = false;
            }
            if (!inside) return result;
    
            SvoProbe<Dim> probe = probe(cell);
            if (probe.state == 0u || probe.cell_size == 0u) return result;
    
            // Cell AABB for the resolved node.
            float cell_size = float(probe.cell_size);
            float[Dim] cmin, cmax;
            for (int a = 0; a < Dim; ++a) {
                cmin[a] = float(probe.cell_min[a]);
                cmax[a] = cmin[a] + cell_size;
            }
    
            // Ray-cell AABB intersection.
            float cell_t_enter = -1e30f;
            float cell_t_exit  = 1e30f;
            int   cell_hit_axis = -1;
            for (int a = 0; a < Dim; ++a) {
                float ct0 = (cmin[a] - ray_origin[a]) * inv_dir[a];
                float ct1 = (cmax[a] - ray_origin[a]) * inv_dir[a];
                float ctlo = min(ct0, ct1);
                float cthi = max(ct0, ct1);
                if (ctlo > cell_t_enter) { cell_t_enter = ctlo; cell_hit_axis = a; }
                cell_t_exit = min(cell_t_exit, cthi);
            }
    
            if (probe.state == 3u) {
                // Leaf hit.
                result.data_index = probe.data_index;
                result.t          = max(cell_t_enter, 0.0f);
                result.hit_axis   = cell_hit_axis;
                result.hit_sign   = ray_dir[cell_hit_axis] >= 0.0f ? -1 : 1;
                for (int a = 0; a < Dim; ++a) result.cell_pos[a] = probe.cell_min[a];
                return result;
            }
    
            // Empty or interior node -- skip past the cell.
            float step_eps = max(eps, cell_size * eps);
            t_cur = max(cell_t_exit + step_eps, t_cur + step_eps);
        }
        return result;
    }
};

// ============================================================
// SvoRayHit<Dim> -- result of a ray traversal through an SVO
// ============================================================

public struct SvoRayHit<let Dim : int>
{
    public int    data_index;    // voxel data index; -1 = miss
    public float  t;             // entry distance along ray (from origin)
    public int    hit_axis;      // axis of the face that was hit (-1 if miss)
    public int    hit_sign;      // +1 or -1: sign of the normal along hit_axis
    public int[Dim] cell_pos;    // integer coords of the hit voxel
};

// ============================================================
// Free-standing ray march using SvoGrid<Dim, CC>.
//
// Uses hierarchical DDA: probes the SVO at the current ray position,
// then advances by the resolved cell size (skipping large empty regions),
// exactly like the AdamYuan/ESVO approach but dimension-generic.
// ============================================================

public SvoRayHit<Dim> svo_trace_ray<let Dim : int, let CC : int>(
    SvoGrid<Dim, CC> svo,
    float[Dim] origin,
    float[Dim] dir,
    int   max_steps
) {
    return svo.trace_ray(origin, dir, max_steps);
}

public typealias SvoGrid1D = SvoGrid<1, 2>;
public typealias SvoGrid2D = SvoGrid<2, 2>;
public typealias SvoGrid3D = SvoGrid<3, 2>;

}  // namespace epix::ext::grid
)slang";

/**
 * @brief Slang source for the GPU-side SVO traversal library backed by a 64-bit word buffer.
 *
 * This source declares `module "epix/ext/grid/svo64"` so other Slang shaders can do:
 *   import epix.ext.grid.svo64;
 * and then use SvoGrid64_1D, SvoGrid64_2D, or SvoGrid64_3D.
 *
 * Register with the shader cache via:
 *   Shader::from_slang(std::string(kSvoGridSlangSource64), "embedded://epix/shaders/grid/svo64.slang")
 *
 * No shaderInt64 / Int64Ops GPU capability is required.  The buffer is bound as
 * StructuredBuffer<uint> where each logical 64-bit word at index W occupies two
 * consecutive uint elements: buf[W*2] (low) and buf[W*2+1] (high).  All traversal
 * arithmetic stays in 32-bit; the returned data index is int (32-bit).
 *
 * GPU constraint: cpn = ChildCount^Dim <= 16 so that valid_mask + leaf_mask both
 * fit in the low uint32 of each descriptor word.  This covers CC=2 for Dim 1–4
 * and CC=4 for Dim 1–2.  The C++ SvoBuffer64 may be built with larger cpn values
 * but those cannot be traversed with this shader.
 */
export constexpr std::string_view kSvoGridSlangSource64 = R"slang(
// epix.ext.grid.svo64 - GPU-side SVO traversal for sparse voxel trees (64-bit word buffer).
// Companion to epix.extension.grid_gpu (C++ module), 64-bit buffer variant.
//
// Bind the SvoBuffer64 produced by svo_upload64() as a StructuredBuffer<uint>.
// No shaderInt64 required -- each uint64 word is stored as a little-endian pair of uint32:
//   logical word W  →  buf[W * 2u]        (low  32 bits)
//                      buf[W * 2u + 1u]   (high 32 bits, unused by traversal)
// Construct with the bound buffer: `SvoGrid64_3D grid = SvoGrid64_3D(svo_buf);`
//
// GPU constraint: cpn = CC^Dim <= 16 (valid_mask + leaf_mask fit in the low uint32).
// Returned data index is int (32-bit).
//
// ---- Buffer layout (logical uint64 word indices, mirrors SvoBuffer64::words) ----
//   [0]           : header = dim(8b) | depth(8b) | cpn(8b) | reserved(40b)
//   [1]           : data_count (low 32 bits used on GPU)
//   [2..2+Dim-1]  : origin[axis] as int32 (stored in low half of each word)
//   [2+Dim ..]    : node pool (root descriptor first, DFS pre-order)
//
// ---- Node descriptor (low uint32 of one uint64 word) ----
//   bits [0..cpn-1]     : valid_mask
//   bits [cpn..2*cpn-1] : leaf_mask
//   (child block always at desc+1)
//
// ---- Child block (valid_count compact word-index slots) ----
//   Leaf  slot = DATA INDEX (low uint32); returned as int
//   Inner slot = absolute uint64 word index of child descriptor (low uint32)

module epix.ext.grid.svo64;

namespace epix::ext::grid {

// ============================================================
// Shared helpers
// ============================================================

uint svo64_popcount_below(uint mask, uint i)
{
    return countbits(mask & ((1u << i) - 1u));
}

// Read the low uint32 of logical uint64 word `wi`.
uint svo64_lo(StructuredBuffer<uint> buf, uint wi)
{
    return buf[wi * 2u];
}

void svo64_decode_node(uint lo_word, uint cpn, out uint valid_mask, out uint leaf_mask)
{
    uint mask  = (1u << cpn) - 1u;
    valid_mask = lo_word & mask;
    leaf_mask  = (lo_word >> cpn) & mask;
}

// ============================================================
// SvoGrid64<Dim, CC> -- generic N-D / CC-ary SVO traversal
// Uses StructuredBuffer<uint>, 2 elements per uint64 word.
// cpn = CC^Dim must be <= 16.
// ============================================================

public struct SvoProbe64<let Dim : int>
{
    public int data_index;
    public uint state;      // 0 = outside, 1 = empty child, 2 = interior node, 3 = leaf hit
    public uint level;      // root = 0, first child level = 1
    public uint cell_size;  // coverage per axis for the resolved cell
    public int[Dim] cell_min;
};

public struct SvoGrid64<let Dim : int, let CC : int>
{
    StructuredBuffer<uint> buf;

    uint depth_cache_;
    uint data_count_cache_;
    uint pool_base_cache_;
    uint coverage_cache_;
    uint cpn_cache_;
    int[Dim] origin_cache_;

    public __init(StructuredBuffer<uint> buffer) {
        this.buf          = buffer;
        this.depth_cache_ = (svo64_lo(buffer, 0u) >> 8u) & 0xFFu;
        this.data_count_cache_ = svo64_lo(buffer, 1u);
        this.pool_base_cache_  = 2u + uint(Dim);
        this.cpn_cache_        = 1u;
        for (int a = 0; a < Dim; ++a) {
            this.origin_cache_[a] = int(svo64_lo(buffer, uint(2 + a)));
            this.cpn_cache_ *= uint(CC);
        }
        this.coverage_cache_ = __pow_cc(this.depth_cache_);
    }

    public uint depth() { return depth_cache_; }

    public uint data_count() { return data_count_cache_; }

    public uint pool_base() { return pool_base_cache_; }

    public uint coverage() { return coverage_cache_; }

    public int origin(int axis) { return origin_cache_[axis]; }

    uint __pow_cc(uint exp) {
        uint r = 1u;
        for (uint i = 0u; i < exp; ++i) r *= uint(CC);
        return r;
    }

    void __clear_probe(out SvoProbe64<Dim> probe) {
        probe.data_index = -1;
        probe.state      = 0u;
        probe.level      = 0u;
        probe.cell_size  = 0u;
        for (int a = 0; a < Dim; ++a)
            probe.cell_min[a] = 0;
    }

    void __store_probe(out SvoProbe64<Dim> probe, int data_index, uint state, uint level, uint cell_size, int[Dim] cell_min) {
        probe.data_index = data_index;
        probe.state      = state;
        probe.level      = level;
        probe.cell_size  = cell_size;
        for (int a = 0; a < Dim; ++a)
            probe.cell_min[a] = cell_min[a];
    }

    uint __flat_ci(uint[Dim] rel, uint stride, out uint[Dim] digits) {
        if (stride == 0u) {
            for (int a = 0; a < Dim; ++a)
                digits[a] = 0u;
            return 0u;
        }
        uint idx = 0u;
        for (int a = 0; a < Dim; ++a) {
            uint digit = (rel[a] / stride) % uint(CC);
            digits[a]  = digit;
            idx        = idx * uint(CC) + digit;
        }
        return idx;
    }

    void __child_min(out int[Dim] dst, int[Dim] base_min, uint[Dim] digits, uint stride) {
        for (int a = 0; a < Dim; ++a)
            dst[a] = base_min[a] + int(digits[a] * stride);
    }

    public SvoProbe64<Dim> probe_lod(int[Dim] pos, uint max_levels) {
        SvoProbe64<Dim> result;
        __clear_probe(result);

        if (depth_cache_ == 0u) return result;

        uint[Dim] rel;
        for (int a = 0; a < Dim; ++a)
            rel[a] = uint(pos[a] - origin_cache_[a]);
        for (int a = 0; a < Dim; ++a)
            if (rel[a] >= coverage_cache_) return result;

        __store_probe(result, -1, 2u, 0u, coverage_cache_, origin_cache_);
        if (max_levels == 0u) return result;

        uint stop_levels = min(max_levels, depth_cache_);
        uint stride      = coverage_cache_ / uint(CC);
        uint node_idx    = pool_base_cache_;
        int[Dim] cell_min;
        for (int a = 0; a < Dim; ++a)
            cell_min[a] = origin_cache_[a];

        for (uint level = 0u; level < stop_levels; ++level) {
            uint[Dim] digits;
            uint ci = __flat_ci(rel, stride, digits);
            int[Dim] child_min;
            __child_min(child_min, cell_min, digits, stride);
            uint lo = svo64_lo(buf, node_idx);
            uint valid_mask, leaf_mask;
            svo64_decode_node(lo, cpn_cache_, valid_mask, leaf_mask);
            uint child_bit = 1u << ci;
            if ((valid_mask & child_bit) == 0u) {
                __store_probe(result, -1, 1u, level + 1u, stride, child_min);
                return result;
            }
            uint slot_pos = node_idx + 1u + svo64_popcount_below(valid_mask, ci);
            if ((leaf_mask & child_bit) != 0u) {
                __store_probe(result, int(svo64_lo(buf, slot_pos)), 3u, level + 1u, stride, child_min);
                return result;
            }

            __store_probe(result, -1, 2u, level + 1u, stride, child_min);
            if (level + 1u >= stop_levels) return result;

            node_idx = svo64_lo(buf, slot_pos);
            for (int a = 0; a < Dim; ++a)
                cell_min[a] = child_min[a];
            stride /= uint(CC);
        }

        return result;
    }

    public SvoProbe64<Dim> probe(int[Dim] pos) {
        return probe_lod(pos, depth());
    }

    public int lookup(int[Dim] pos) {
        SvoProbe64<Dim> result = probe(pos);
        return result.state == 3u ? result.data_index : -1;
    }

    public bool contains(int[Dim] pos) { return lookup(pos) >= 0; }

    public SvoRayHit64<Dim> trace_ray(float[Dim] ray_origin, float[Dim] ray_dir, int max_steps) {
        SvoRayHit64<Dim> result;
        result.data_index = -1;
        result.t          = 0.0f;
        result.hit_axis   = -1;
        result.hit_sign   = 0;
        for (int a = 0; a < Dim; ++a) result.cell_pos[a] = 0;
    
        float[Dim] inv_dir;
        for (int a = 0; a < Dim; ++a) {
            float s = ray_dir[a] >= 0.0f ? 1.0f : -1.0f;
            inv_dir[a] = s / max(abs(ray_dir[a]), 1e-20f);
        }
    
        float[Dim] scene_min, scene_max;
        float scene_dim = float(coverage());
        for (int a = 0; a < Dim; ++a) {
            scene_min[a] = float(origin(a));
            scene_max[a] = scene_min[a] + scene_dim;
        }
    
        float t_enter = 0.0f;
        float t_exit  = 1e30f;
        int   enter_axis = -1;
        for (int a = 0; a < Dim; ++a) {
            float t0 = (scene_min[a] - ray_origin[a]) * inv_dir[a];
            float t1 = (scene_max[a] - ray_origin[a]) * inv_dir[a];
            float tlo = min(t0, t1);
            float thi = max(t0, t1);
            if (tlo > t_enter) { t_enter = tlo; enter_axis = a; }
            t_exit = min(t_exit, thi);
        }
        if (t_exit < max(t_enter, 0.0f)) return result;
    
        float t_cur = max(t_enter, 0.0f);
        float t_limit = min(t_exit, 1e20f);
        float eps = 1e-4f;
    
        for (int step_i = 0; step_i < max_steps && t_cur <= t_limit; ++step_i) {
            float sample_t = min(t_cur + eps, t_limit);
            int[Dim] cell;
            bool inside = true;
            for (int a = 0; a < Dim; ++a) {
                float p = clamp(ray_origin[a] + ray_dir[a] * sample_t, scene_min[a] + eps, scene_max[a] - eps);
                cell[a] = int(floor(p));
                if (float(cell[a]) < scene_min[a] || float(cell[a]) >= scene_max[a])
                    inside = false;
            }
            if (!inside) return result;
    
            SvoProbe64<Dim> probe = probe(cell);
            if (probe.state == 0u || probe.cell_size == 0u) return result;
    
            float cell_size = float(probe.cell_size);
            float[Dim] cmin, cmax;
            for (int a = 0; a < Dim; ++a) {
                cmin[a] = float(probe.cell_min[a]);
                cmax[a] = cmin[a] + cell_size;
            }
    
            float cell_t_enter = -1e30f;
            float cell_t_exit  = 1e30f;
            int   cell_hit_axis = -1;
            for (int a = 0; a < Dim; ++a) {
                float ct0 = (cmin[a] - ray_origin[a]) * inv_dir[a];
                float ct1 = (cmax[a] - ray_origin[a]) * inv_dir[a];
                float ctlo = min(ct0, ct1);
                float cthi = max(ct0, ct1);
                if (ctlo > cell_t_enter) { cell_t_enter = ctlo; cell_hit_axis = a; }
                cell_t_exit = min(cell_t_exit, cthi);
            }
    
            if (probe.state == 3u) {
                result.data_index = probe.data_index;
                result.t          = max(cell_t_enter, 0.0f);
                result.hit_axis   = cell_hit_axis;
                result.hit_sign   = ray_dir[cell_hit_axis] >= 0.0f ? -1 : 1;
                for (int a = 0; a < Dim; ++a) result.cell_pos[a] = probe.cell_min[a];
                return result;
            }
    
            float step_eps = max(eps, cell_size * eps);
            t_cur = max(cell_t_exit + step_eps, t_cur + step_eps);
        }
        return result;
    }
};

// ============================================================
// SvoRayHit64<Dim> -- result of a ray traversal through a 64-bit SVO
// ============================================================

public struct SvoRayHit64<let Dim : int>
{
    public int    data_index;    // voxel data index; -1 = miss
    public float  t;             // entry distance along ray
    public int    hit_axis;      // axis of the face that was hit (-1 if miss)
    public int    hit_sign;      // +1 or -1: sign of the normal along hit_axis
    public int[Dim] cell_pos;    // integer coords of the hit voxel
};

// ============================================================
// Free-standing ray march using SvoGrid64<Dim, CC>.
// ============================================================


public SvoRayHit64<Dim> svo64_trace_ray<let Dim : int, let CC : int>(
    SvoGrid64<Dim, CC> svo,
    float[Dim] origin,
    float[Dim] dir,
    int   max_steps
) {
    return svo.trace_ray(origin, dir, max_steps);
}

public typealias SvoGrid64_1D = SvoGrid64<1, 2>;
public typealias SvoGrid64_2D = SvoGrid64<2, 2>;
public typealias SvoGrid64_3D = SvoGrid64<3, 2>;

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

// -------------------------------------------------------
// 64-bit variants
// -------------------------------------------------------

static constexpr uint64_t SVO_EMPTY_64 = 0xFFFF'FFFF'FFFF'FFFFull;

// popcount of bits below position i (64-bit)
inline uint64_t popcount_below64(uint64_t mask, uint64_t i) noexcept {
    return static_cast<uint64_t>(std::popcount(mask & ((uint64_t{1} << i) - 1u)));
}

// Build one 64-bit node descriptor word.
inline uint64_t make_descriptor64(uint64_t valid, uint64_t leaf, uint64_t cpn) noexcept {
    return valid | (leaf << cpn);
}

// -------------------------------------------------------
// BuildNode64 / BuildTree64
// -------------------------------------------------------

struct BuildNode64 {
    std::vector<uint64_t> children;  // size = cpn; SVO_EMPTY_64 = absent slot
    explicit BuildNode64(uint32_t cpn) : children(cpn, SVO_EMPTY_64) {}
};

struct BuildTree64 {
    uint32_t cpn;
    uint32_t depth;
    uint32_t child_count;  // per-axis branching = ChildCount
    std::vector<BuildNode64> nodes;

    BuildTree64(uint32_t cpn_, uint32_t depth_, uint32_t child_count_)
        : cpn(cpn_), depth(depth_), child_count(child_count_) {
        nodes.emplace_back(cpn);  // root at index 0
    }

    uint64_t pow_cc(uint32_t exp) const noexcept {
        uint64_t r = 1;
        for (uint32_t i = 0; i < exp; ++i) r *= child_count;
        return r;
    }

    template <std::size_t Dim>
    uint32_t flat_child_idx(const std::array<uint64_t, Dim>& rel, uint64_t stride) const noexcept {
        uint32_t idx = 0;
        for (std::size_t axis = 0; axis < Dim; ++axis) {
            uint32_t digit = static_cast<uint32_t>((rel[axis] / stride) % child_count);
            idx            = idx * child_count + digit;
        }
        return idx;
    }

    template <std::size_t Dim>
    void insert(const std::array<uint64_t, Dim>& rel, uint64_t data_index) {
        uint64_t cur = 0;
        for (uint32_t level = 0; level < depth; ++level) {
            uint64_t stride = pow_cc(depth - level - 1u);
            uint32_t ci     = flat_child_idx<Dim>(rel, stride);

            if (level + 1u == depth) {
                nodes[static_cast<std::size_t>(cur)].children[ci] = data_index;
            } else {
                uint64_t& slot = nodes[static_cast<std::size_t>(cur)].children[ci];
                if (slot == SVO_EMPTY_64) {
                    slot = static_cast<uint64_t>(nodes.size());
                    nodes.emplace_back(cpn);
                }
                cur = slot;
            }
        }
    }

    // DFS serialise into out[].  Returns absolute word index of the descriptor written.
    uint64_t serialize(std::vector<uint64_t>& out, uint64_t node_idx, uint32_t level) {
        const BuildNode64& node     = nodes[static_cast<std::size_t>(node_idx)];
        const bool parent_of_leaves = (level + 1u == depth);

        uint64_t valid_mask = 0u;
        uint64_t leaf_mask  = 0u;
        for (uint32_t i = 0; i < cpn; ++i) {
            if (node.children[i] != SVO_EMPTY_64) {
                valid_mask |= (uint64_t{1} << i);
                if (parent_of_leaves) leaf_mask |= (uint64_t{1} << i);
            }
        }

        uint64_t valid_count = static_cast<uint64_t>(std::popcount(valid_mask));

        uint64_t desc_pos      = static_cast<uint64_t>(out.size());
        uint64_t child_blk_pos = desc_pos + 1u;
        out.resize(out.size() + 1u + static_cast<std::size_t>(valid_count), 0u);

        out[static_cast<std::size_t>(desc_pos)] = make_descriptor64(valid_mask, leaf_mask, cpn);

        struct Pending {
            uint64_t slot_pos;
            uint64_t child_node;
        };
        std::vector<Pending> pending;

        for (uint32_t i = 0; i < cpn; ++i) {
            if (!((valid_mask >> i) & 1u)) continue;
            uint64_t slot_pos = child_blk_pos + popcount_below64(valid_mask, i);
            if (parent_of_leaves) {
                out[static_cast<std::size_t>(slot_pos)] = node.children[i];  // data index
            } else {
                pending.push_back({slot_pos, node.children[i]});
            }
        }

        for (auto& p : pending) {
            uint64_t child_desc_pos                   = serialize(out, p.child_node, level + 1u);
            out[static_cast<std::size_t>(p.slot_pos)] = child_desc_pos;
        }

        return desc_pos;
    }
};

// -------------------------------------------------------
// 64-bit header helpers
// -------------------------------------------------------

template <std::size_t Dim>
void push_header64(std::vector<uint64_t>& words,
                   uint32_t depth,
                   uint32_t cpn,
                   uint64_t data_count,
                   const std::array<int64_t, Dim>& origin) {
    uint64_t h = (static_cast<uint64_t>(Dim) & 0xFFu) | ((static_cast<uint64_t>(depth) & 0xFFu) << 8u) |
                 ((static_cast<uint64_t>(cpn) & 0xFFu) << 16u);
    words.push_back(h);
    words.push_back(data_count);
    for (std::size_t i = 0; i < Dim; ++i) words.push_back(static_cast<uint64_t>(origin[i]));
}

template <std::size_t Dim>
void push_header64(std::vector<uint64_t>& words,
                   uint32_t depth,
                   uint32_t cpn,
                   uint64_t data_count,
                   const std::array<uint64_t, Dim>& origin) {
    uint64_t h = (static_cast<uint64_t>(Dim) & 0xFFu) | ((static_cast<uint64_t>(depth) & 0xFFu) << 8u) |
                 ((static_cast<uint64_t>(cpn) & 0xFFu) << 16u);
    words.push_back(h);
    words.push_back(data_count);
    for (std::size_t i = 0; i < Dim; ++i) words.push_back(origin[i]);
}

// -------------------------------------------------------
// 64-bit core serialise helper
// -------------------------------------------------------

template <std::size_t Dim, std::size_t ChildCount>
void build_and_serialize64(std::vector<uint64_t>& words,
                           uint32_t depth,
                           const std::vector<std::pair<std::array<uint64_t, Dim>, uint64_t>>& cells) {
    constexpr uint32_t CPN = cpn_v<Dim, ChildCount>;
    if (cells.empty()) return;
    BuildTree64 tree(CPN, depth, static_cast<uint32_t>(ChildCount));
    for (auto& [rel, idx] : cells) tree.insert<Dim>(rel, idx);
    tree.serialize(words, 0u, 0u);
}

// -------------------------------------------------------
// 64-bit depth computation
// -------------------------------------------------------
inline uint32_t depth_from_coverage64(uint64_t cov, uint32_t child_count) noexcept {
    if (cov <= 1u) return 1u;
    uint32_t depth = 0u;
    uint64_t c     = 1u;
    while (c < cov) {
        c *= child_count;
        ++depth;
    }
    return depth;
}

// -------------------------------------------------------
// 64-bit compile-time-CC upload helpers
// Constraint: cpn <= 32 (valid_mask + leaf_mask fit in 64 bits)
// -------------------------------------------------------

template <std::size_t CC, std::size_t Dim, typename CoordT, epix::ext::grid::tree_based_grid G>
    requires(cpn_v<Dim, CC> <= 32)
SvoBuffer64 svo_upload_tree_cc64(const G& grid) {
    constexpr uint32_t CPN = cpn_v<Dim, CC>;
    SvoBuffer64 buf;
    const uint64_t n = static_cast<uint64_t>(grid.count());

    using OriginT = std::conditional_t<std::is_signed_v<CoordT>, int64_t, uint64_t>;
    if (n == 0) {
        std::array<OriginT, Dim> zero_origin{};
        push_header64<Dim>(buf.words, 0u, CPN, 0u, zero_origin);
        return buf;
    }

    std::array<OriginT, Dim> origin;
    {
        bool first = true;
        for (const auto& pos : grid.iter_pos()) {
            if (first) {
                for (std::size_t a = 0; a < Dim; ++a) origin[a] = static_cast<OriginT>(pos[a]);
                first = false;
                continue;
            }
            for (std::size_t axis = 0; axis < Dim; ++axis)
                origin[axis] = std::min(origin[axis], static_cast<OriginT>(pos[axis]));
        }
    }

    const uint32_t depth = depth_from_coverage64(static_cast<uint64_t>(grid.coverage()), static_cast<uint32_t>(CC));

    std::vector<std::pair<std::array<uint64_t, Dim>, uint64_t>> cells;
    cells.reserve(static_cast<std::size_t>(n));
    uint64_t idx = 0;
    for (const auto& pos : grid.iter_pos()) {
        std::array<uint64_t, Dim> rel;
        for (std::size_t axis = 0; axis < Dim; ++axis)
            rel[axis] = static_cast<uint64_t>(static_cast<OriginT>(pos[axis]) - origin[axis]);
        cells.push_back({rel, idx++});
    }

    push_header64<Dim>(buf.words, depth, CPN, n, origin);
    build_and_serialize64<Dim, CC>(buf.words, depth, cells);
    return buf;
}

template <std::size_t CC, std::size_t Dim, typename CoordT, epix::ext::grid::any_grid G>
    requires(!epix::ext::grid::tree_based_grid<G> && cpn_v<Dim, CC> <= 32)
SvoBuffer64 svo_upload_flat_cc64(const G& grid) {
    constexpr uint32_t CPN = cpn_v<Dim, CC>;
    SvoBuffer64 buf;
    const uint64_t n = static_cast<uint64_t>(grid.count());

    using OriginT = std::conditional_t<std::is_signed_v<CoordT>, int64_t, uint64_t>;
    if (n == 0) {
        std::array<OriginT, Dim> zero_origin{};
        push_header64<Dim>(buf.words, 0u, CPN, 0u, zero_origin);
        return buf;
    }

    std::array<OriginT, Dim> origin;
    {
        bool first = true;
        for (const auto& pos : grid.iter_pos()) {
            if (first) {
                for (std::size_t a = 0; a < Dim; ++a) origin[a] = static_cast<OriginT>(pos[a]);
                first = false;
                continue;
            }
            for (std::size_t axis = 0; axis < Dim; ++axis)
                origin[axis] = std::min(origin[axis], static_cast<OriginT>(pos[axis]));
        }
    }

    std::vector<std::pair<std::array<uint64_t, Dim>, uint64_t>> cells;
    cells.reserve(static_cast<std::size_t>(n));
    uint64_t max_rel = 0u, idx = 0;
    for (const auto& pos : grid.iter_pos()) {
        std::array<uint64_t, Dim> rel;
        for (std::size_t axis = 0; axis < Dim; ++axis) {
            rel[axis] = static_cast<uint64_t>(static_cast<OriginT>(pos[axis]) - origin[axis]);
            max_rel   = std::max(max_rel, rel[axis]);
        }
        cells.push_back({rel, idx++});
    }

    const uint32_t depth = depth_from_coverage64(max_rel + 1u, static_cast<uint32_t>(CC));
    push_header64<Dim>(buf.words, depth, CPN, n, origin);
    build_and_serialize64<Dim, CC>(buf.words, depth, cells);
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
 * The GPU SVO structure is independent of the CPU grid's storage -- it can use
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

// -------------------------------------------------------
// 64-bit public API
// -------------------------------------------------------

/**
 * @brief Detailed error returned by svo_upload64().
 *
 * Same structure as SvoUploadError, but the constraint is cpn <= 32
 * (valid_mask + leaf_mask must fit in a 64-bit word).
 */
export struct SvoUploadError64 {
    /** @brief child_count was unsupported or incompatible with the grid dimension. */
    struct InvalidChildCount {
        std::size_t provided;
        std::size_t dim;

        std::string message() const {
            if (provided != 2 && provided != 4 && provided != 8)
                return std::format("svo_upload64: unsupported child_count {} (must be 2, 4, or 8)", provided);
            std::size_t cpn = 1;
            for (std::size_t i = 0; i < dim; ++i) cpn *= provided;
            return std::format(
                "svo_upload64: child_count {} is incompatible with a {}-dimensional grid "
                "(cpn = {}^{} = {} > 32; constraint: cpn <= 32)",
                provided, dim, provided, dim, cpn);
        }
    };

    using Data = std::variant<InvalidChildCount>;
    Data data;

    template <typename E>
        requires std::constructible_from<Data, E>
    explicit SvoUploadError64(E&& e) : data(std::forward<E>(e)) {}

    template <typename E>
    bool is() const noexcept {
        return std::holds_alternative<E>(data);
    }

    template <typename E>
    const E* get() const noexcept {
        return std::get_if<E>(&data);
    }

    template <typename E>
    E* get() noexcept {
        return std::get_if<E>(&data);
    }

    std::string message() const {
        return std::visit([](const auto& e) { return e.message(); }, data);
    }
};

/**
 * @brief Configuration for the GPU-side SVO produced by svo_upload64().
 *
 * Supports the same child_count values as SvoConfig (2, 4, 8) but the
 * relaxed constraint allows larger grids: cpn = ChildCount^Dim <= 32.
 */
export struct SvoConfig64 {
    /** @brief Per-axis branching factor for the GPU tree (default: 2). */
    std::size_t child_count = 2;
};

/**
 * @brief Serialize any tree-based grid into a flat SvoBuffer64 (64-bit words).
 *
 * Identical semantics to svo_upload() but the output buffer uses uint64_t words,
 * enabling data_count > 4 billion, int64/uint64 coordinates, and node-pool indices
 * beyond 4 billion.  Also relaxes the dimension constraint: cpn <= 32 (vs cpn <= 16
 * for svo_upload), making binary trees valid up to 5 dimensions.
 *
 * @tparam G      Grid type satisfying tree_based_grid.
 * @param  grid   Source grid.
 * @param  config GPU tree configuration (default: binary tree with child_count=2).
 * @return SvoBuffer64 on success, or SvoUploadError64 on invalid child_count.
 */
export template <epix::ext::grid::tree_based_grid G>
    requires(epix::ext::grid::grid_trait<G>::dim >= 1)
std::expected<SvoBuffer64, SvoUploadError64> svo_upload64(const G& grid, const SvoConfig64& config = {}) {
    using Trait               = epix::ext::grid::grid_trait<G>;
    constexpr std::size_t Dim = Trait::dim;
    using CoordT              = typename Trait::coord_type;
    switch (config.child_count) {
        case 2:
            if constexpr (detail::cpn_v<Dim, 2> <= 32) return detail::svo_upload_tree_cc64<2, Dim, CoordT>(grid);
            break;
        case 4:
            if constexpr (detail::cpn_v<Dim, 4> <= 32) return detail::svo_upload_tree_cc64<4, Dim, CoordT>(grid);
            break;
        case 8:
            if constexpr (detail::cpn_v<Dim, 8> <= 32) return detail::svo_upload_tree_cc64<8, Dim, CoordT>(grid);
            break;
        default:
            break;
    }
    return std::unexpected(SvoUploadError64{SvoUploadError64::InvalidChildCount{config.child_count, Dim}});
}

/**
 * @brief Serialize any flat (non-tree) grid into a flat SvoBuffer64 (64-bit words).
 *
 * Works for dense_grid, sparse_grid, dense_extendible_grid, packed_grid, and any other
 * grid type without coverage().  Same relaxed constraint: cpn <= 32.
 *
 * @tparam G      Grid type satisfying any_grid but not tree_based_grid.
 * @param  grid   Source grid.
 * @param  config GPU tree configuration (default: binary tree with child_count=2).
 * @return SvoBuffer64 on success, or SvoUploadError64 on invalid child_count.
 */
export template <epix::ext::grid::any_grid G>
    requires(!epix::ext::grid::tree_based_grid<G> && epix::ext::grid::grid_trait<G>::dim >= 1)
std::expected<SvoBuffer64, SvoUploadError64> svo_upload64(const G& grid, const SvoConfig64& config = {}) {
    using Trait               = epix::ext::grid::grid_trait<G>;
    constexpr std::size_t Dim = Trait::dim;
    using CoordT              = typename Trait::coord_type;
    switch (config.child_count) {
        case 2:
            if constexpr (detail::cpn_v<Dim, 2> <= 32) return detail::svo_upload_flat_cc64<2, Dim, CoordT>(grid);
            break;
        case 4:
            if constexpr (detail::cpn_v<Dim, 4> <= 32) return detail::svo_upload_flat_cc64<4, Dim, CoordT>(grid);
            break;
        case 8:
            if constexpr (detail::cpn_v<Dim, 8> <= 32) return detail::svo_upload_flat_cc64<8, Dim, CoordT>(grid);
            break;
        default:
            break;
    }
    return std::unexpected(SvoUploadError64{SvoUploadError64::InvalidChildCount{config.child_count, Dim}});
}

}  // namespace epix::ext::grid_gpu
