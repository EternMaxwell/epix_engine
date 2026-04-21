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
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#endif

export module epix.extension.grid_gpu:brickmap;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.extension.grid;

using std::int32_t;
using std::int64_t;
using std::size_t;
using std::uint32_t;
using std::uint64_t;

// ============================================================
// BrickmapBuffer -- flat GPU-ready buffer for a "layered grid" voxel structure
//
//   Based on the approach described in:
//     - "Real-time Ray tracing and Editing of Large Voxel Scenes"
//       (van Wingerden, 2015, Utrecht University)
//     - github.com/stijnherfst/BrickMap
//
//   The world is divided into a regular grid of "bricks".
//   Each brick covers BrickSize^Dim voxels and stores occupancy as a bitmask
//   plus a base data index for reconstructing the ordinal of each voxel.
//
//   DDA traversal at the brick level + DDA within bricks gives O(surface)
//   per-ray cost without a hierarchical tree structure. This makes edits O(1)
//   per voxel, and memory layout is cache-friendly for ray marching.
//
//   Dimension-generic: BrickSize and Dim are template parameters, no
//   dimension-specific branching.
//
// ---- Buffer layout (uint32 words) ----
//
//   [0]                     : header word
//                              bits [7:0]   = Dim
//                              bits [15:8]  = brick_size (per axis)
//                              bits [23:16] = reserved
//   [1]                     : data_count (total occupied voxels)
//   [2 .. 2+Dim-1]          : extent[axis] (number of bricks per axis)
//   [2+Dim .. 2+2*Dim-1]    : origin[axis] (int32, voxel-space origin)
//   [2+2*Dim]               : brick_count  (number of allocated bricks)
//   [2+2*Dim+1]             : words_per_brick (= 1 + ceil(BS^Dim/32))
//   [grid_base ..]          : grid entries (product(extent) uint32 words)
//                              0 = empty brick
//                              nonzero = 1-based index into brick pool
//   [pool_base ..]          : brick pool
//                              each brick = 1 base_data_index word
//                                         + ceil(BS^Dim/32) occupancy words
//   [map_base ..]           : data_index_map (data_count uint32 words)
//                              maps packed spatial index -> iter_pos ordinal
//
// ---- Voxel data index (user-visible) ----
//   The data_index returned by GPU probe()/lookup()/trace_ray() is the
//   0-based ordinal of that cell in the CPU grid's iter_pos() sequence,
//   matching the SVO convention.  The GPU shader resolves this via the
//   embedded data_index_map.
// ============================================================

namespace epix::ext::grid_gpu {

// -------------------------------------------------------
// Public types
// -------------------------------------------------------

/// Decoded header fields from a BrickmapBuffer.
export struct BrickmapHeader {
    uint32_t dim;
    uint32_t brick_size;
    uint32_t data_count;
    uint32_t brick_count;
    uint32_t words_per_brick;
};

/**
 * @brief Flat single-buffer brickmap ready for GPU upload.
 *
 * Produced by brickmap_upload() from any grid.  Upload words.data()
 * (byte_size() bytes) to a GPU StructuredBuffer<uint>.
 */
export struct BrickmapBuffer {
    std::vector<uint32_t> words;

    BrickmapHeader header() const noexcept {
        if (words.size() < 2) return {};
        uint32_t h = words[0];
        uint32_t dim = h & 0xFFu;
        uint32_t bs  = (h >> 8u) & 0xFFu;
        uint32_t bc_offset  = 2u + 2u * dim;
        uint32_t wpb_offset = bc_offset + 1u;
        return {
            .dim             = dim,
            .brick_size      = bs,
            .data_count      = words[1],
            .brick_count     = (bc_offset < words.size()) ? words[bc_offset] : 0u,
            .words_per_brick = (wpb_offset < words.size()) ? words[wpb_offset] : 0u,
        };
    }

    std::size_t size() const noexcept { return words.size(); }
    std::size_t byte_size() const noexcept { return words.size() * sizeof(uint32_t); }
    const uint32_t* data() const noexcept { return words.data(); }
};

// -------------------------------------------------------
// Embedded Slang shader source
// -------------------------------------------------------
export constexpr std::string_view kBrickmapGridSlangSource = R"slang(
module epix.ext.grid.brickmap;

namespace epix::ext::grid {

uint bm_pow(uint base, int exp) {
    uint r = 1u;
    for (int i = 0; i < exp; ++i) r *= base;
    return r;
}

public struct BrickmapProbe<let Dim : int> {
    public int  data_index;
    public uint state;  // 0=outside, 1=empty brick, 2=empty voxel, 3=hit
    public int[Dim] brick_pos;
    public int[Dim] local_pos;
};

public struct BrickmapRayHit<let Dim : int> {
    public int    data_index;
    public float  t;
    public int    hit_axis;
    public int    hit_sign;
    public int[Dim] cell_pos;
};

public struct BrickmapGrid<let Dim : int, let BS : int> {
    StructuredBuffer<uint> buf;
    uint data_count_cache_;
    uint brick_count_cache_;
    uint words_per_brick_cache_;
    uint grid_base_cache_;
    uint pool_base_cache_;
    uint map_base_cache_;
    uint voxels_per_brick_cache_;
    uint[Dim] extent_cache_;
    int[Dim] origin_cache_;

    public __init(StructuredBuffer<uint> buffer) {
        this.buf = buffer;
        this.data_count_cache_ = buffer[1];
        this.voxels_per_brick_cache_ = bm_pow(uint(BS), Dim);
        uint grid_total = 1u;
        for (int a = 0; a < Dim; ++a) {
            this.extent_cache_[a] = buffer[2 + a];
            this.origin_cache_[a] = int(buffer[2 + Dim + a]);
            grid_total *= this.extent_cache_[a];
        }
        this.brick_count_cache_     = buffer[2 + 2 * Dim];
        this.words_per_brick_cache_ = buffer[2 + 2 * Dim + 1];
        this.grid_base_cache_       = uint(2 + 2 * Dim + 2);
        this.pool_base_cache_       = this.grid_base_cache_ + grid_total;
        this.map_base_cache_        = this.pool_base_cache_ + this.brick_count_cache_ * this.words_per_brick_cache_;
    }

    public uint data_count() { return data_count_cache_; }
    public uint brick_count() { return brick_count_cache_; }
    public int origin(int axis) { return origin_cache_[axis]; }
    public uint extent(int axis) { return extent_cache_[axis]; }

    uint __grid_index(uint[Dim] bp) {
        uint idx = 0u; uint stride = 1u;
        for (int a = 0; a < Dim; ++a) { idx += bp[a] * stride; stride *= extent_cache_[a]; }
        return idx;
    }
    uint __local_flat(uint[Dim] local) {
        uint idx = 0u; uint stride = 1u;
        for (int a = 0; a < Dim; ++a) { idx += local[a] * stride; stride *= uint(BS); }
        return idx;
    }
    uint __popcount_below_bit(uint bpi, uint flat_bit) {
        uint occ_base = pool_base_cache_ + bpi * words_per_brick_cache_ + 1u;
        uint count = 0u;
        uint fw = flat_bit / 32u; uint rb = flat_bit % 32u;
        for (uint w = 0u; w < fw; ++w) count += countbits(buf[occ_base + w]);
        if (rb > 0u) count += countbits(buf[occ_base + fw] & ((1u << rb) - 1u));
        return count;
    }
    bool __test_bit(uint bpi, uint flat_bit) {
        uint occ_base = pool_base_cache_ + bpi * words_per_brick_cache_ + 1u;
        return (buf[occ_base + flat_bit / 32u] & (1u << (flat_bit % 32u))) != 0u;
    }
    uint __base_data_index(uint bpi) {
        return buf[pool_base_cache_ + bpi * words_per_brick_cache_];
    }
    int __resolve_data_index(uint packed_idx) {
        return int(buf[map_base_cache_ + packed_idx]);
    }

    public BrickmapProbe<Dim> probe(int[Dim] pos) {
        BrickmapProbe<Dim> result;
        result.data_index = -1; result.state = 0u;
        for (int a = 0; a < Dim; ++a) { result.brick_pos[a] = 0; result.local_pos[a] = 0; }
        uint[Dim] bp; uint[Dim] local;
        for (int a = 0; a < Dim; ++a) {
            int rel = pos[a] - origin_cache_[a];
            if (rel < 0 || uint(rel) >= extent_cache_[a] * uint(BS)) return result;
            bp[a] = uint(rel) / uint(BS); local[a] = uint(rel) % uint(BS);
            result.brick_pos[a] = int(bp[a]); result.local_pos[a] = int(local[a]);
        }
        uint gi = __grid_index(bp);
        uint bref = buf[grid_base_cache_ + gi];
        if (bref == 0u) { result.state = 1u; return result; }
        uint bpi = bref - 1u;
        uint fb = __local_flat(local);
        if (!__test_bit(bpi, fb)) { result.state = 2u; return result; }
        result.data_index = __resolve_data_index(__base_data_index(bpi) + __popcount_below_bit(bpi, fb));
        result.state = 3u;
        return result;
    }
    public int lookup(int[Dim] pos) {
        BrickmapProbe<Dim> r = probe(pos); return r.state == 3u ? r.data_index : -1;
    }
    public bool contains(int[Dim] pos) { return lookup(pos) >= 0; }

    public BrickmapRayHit<Dim> trace_ray(float[Dim] ray_origin, float[Dim] ray_dir, int max_steps) {
        BrickmapRayHit<Dim> result;
        result.data_index = -1; result.t = 0.0f;
        result.hit_axis = -1; result.hit_sign = 0;
        for (int a = 0; a < Dim; ++a) result.cell_pos[a] = 0;
        float bs = float(BS);
        float[Dim] scene_min, scene_max, inv_dir;
        for (int a = 0; a < Dim; ++a) {
            scene_min[a] = float(origin(a));
            scene_max[a] = scene_min[a] + float(extent(a)) * bs;
            float s = ray_dir[a] >= 0.0f ? 1.0f : -1.0f;
            inv_dir[a] = s / max(abs(ray_dir[a]), 1e-20f);
        }
        float t_enter = 0.0f; float t_exit = 1e30f;
        for (int a = 0; a < Dim; ++a) {
            float t0 = (scene_min[a] - ray_origin[a]) * inv_dir[a];
            float t1 = (scene_max[a] - ray_origin[a]) * inv_dir[a];
            t_enter = max(t_enter, min(t0, t1));
            t_exit  = min(t_exit, max(t0, t1));
        }
        if (t_exit < max(t_enter, 0.0f)) return result;
        float t_start = max(t_enter, 0.0f);
        float[Dim] epos;
        for (int a = 0; a < Dim; ++a)
            epos[a] = (ray_origin[a] + ray_dir[a] * (t_start + 1e-4f) - scene_min[a]) / bs;
        int[Dim] bp, stp, outv;
        float[Dim] tmax_b, tdelta_b;
        for (int a = 0; a < Dim; ++a) {
            bp[a] = clamp(int(floor(epos[a])), 0, int(extent(a)) - 1);
            stp[a] = ray_dir[a] >= 0.0f ? 1 : -1;
            outv[a] = ray_dir[a] >= 0.0f ? int(extent(a)) : -1;
            if (abs(ray_dir[a]) < 1e-20f) { tmax_b[a] = 1e30f; tdelta_b[a] = 1e30f; }
            else {
                float bd = ray_dir[a] >= 0.0f ? float(bp[a]+1)*bs+scene_min[a] : float(bp[a])*bs+scene_min[a];
                tmax_b[a] = (bd - ray_origin[a]) * inv_dir[a];
                tdelta_b[a] = abs(bs * inv_dir[a]);
            }
        }
        int last_axis = -1;
        for (int i = 0; i < max_steps; ++i) {
            bool valid = true;
            for (int a = 0; a < Dim; ++a) if (bp[a]<0||bp[a]>=int(extent(a))) { valid=false; break; }
            if (!valid) break;
            uint[Dim] ubp; for (int a=0;a<Dim;++a) ubp[a]=uint(bp[a]);
            uint bref = buf[grid_base_cache_ + __grid_index(ubp)];
            if (bref != 0u) {
                uint bpi = bref - 1u;
                float bt_enter = last_axis >= 0 ? tmax_b[last_axis]-tdelta_b[last_axis] : t_start;
                float[Dim] lorg;
                for (int a=0;a<Dim;++a)
                    lorg[a] = clamp(ray_origin[a]+ray_dir[a]*max(bt_enter+1e-4f,t_start)-
                        (float(bp[a])*bs+scene_min[a]), 0.001f, bs-0.001f);
                int[Dim] vp, vs, vo; float[Dim] vtm, vtd;
                for (int a=0;a<Dim;++a) {
                    vp[a]=clamp(int(floor(lorg[a])),0,BS-1);
                    vs[a]=ray_dir[a]>=0.0f?1:-1; vo[a]=ray_dir[a]>=0.0f?BS:-1;
                    if(abs(ray_dir[a])<1e-20f){vtm[a]=1e30f;vtd[a]=1e30f;}
                    else{float vi=1.0f/ray_dir[a];
                        float bnd=ray_dir[a]>=0.0f?float(vp[a]+1):float(vp[a]);
                        vtm[a]=(bnd-lorg[a])*vi; vtd[a]=float(vs[a])*vi;}
                }
                for (int vi=0; vi < BS*BS; ++vi) {
                    uint[Dim] lu; for(int a=0;a<Dim;++a) lu[a]=uint(vp[a]);
                    uint fb=__local_flat(lu);
                    if(__test_bit(bpi,fb)){
                        result.data_index=__resolve_data_index(__base_data_index(bpi)+__popcount_below_bit(bpi,fb));
                        float[Dim] vmin,vmax;
                        for(int a=0;a<Dim;++a){
                            vmin[a]=float(bp[a])*bs+scene_min[a]+float(vp[a]);
                            vmax[a]=vmin[a]+1.0f;
                        }
                        float vte=-1e30f; int vta=-1;
                        for(int a=0;a<Dim;++a){
                            float vt0=(vmin[a]-ray_origin[a])*inv_dir[a];
                            float vt1=(vmax[a]-ray_origin[a])*inv_dir[a];
                            float vtlo=min(vt0,vt1);
                            if(vtlo>vte){vte=vtlo;vta=a;}
                        }
                        result.t=max(vte,0.0f); result.hit_axis=vta;
                        result.hit_sign=ray_dir[vta]>=0.0f?-1:1;
                        for(int a=0;a<Dim;++a)
                            result.cell_pos[a]=int(float(bp[a])*bs+scene_min[a])+vp[a];
                        return result;
                    }
                    int ma=0; float mt=vtm[0];
                    for(int a=1;a<Dim;++a) if(vtm[a]<mt){mt=vtm[a];ma=a;}
                    vp[ma]+=vs[ma]; if(vp[ma]==vo[ma]) break;
                    vtm[ma]+=vtd[ma];
                }
            }
            int ma=0; float mt=tmax_b[0];
            for(int a=1;a<Dim;++a) if(tmax_b[a]<mt){mt=tmax_b[a];ma=a;}
            last_axis=ma; bp[ma]+=stp[ma];
            if(bp[ma]==outv[ma]) break;
            tmax_b[ma]+=tdelta_b[ma];
        }
        return result;
    }
};

public BrickmapRayHit<Dim> brickmap_trace_ray<let Dim : int, let BS : int>(
    BrickmapGrid<Dim, BS> bm, float[Dim] origin, float[Dim] dir, int max_steps
) {
    return bm.trace_ray(origin, dir, max_steps);
}

public typealias BrickmapGrid1D_8 = BrickmapGrid<1, 8>;
public typealias BrickmapGrid2D_8 = BrickmapGrid<2, 8>;
public typealias BrickmapGrid3D_8 = BrickmapGrid<3, 8>;

}
)slang";

// -------------------------------------------------------
// Detail helpers
// -------------------------------------------------------
namespace detail {

template <std::size_t Dim, std::size_t BS>
inline constexpr uint32_t voxels_per_brick_v = [] {
    uint32_t r = 1;
    for (std::size_t i = 0; i < Dim; ++i) r *= static_cast<uint32_t>(BS);
    return r;
}();

template <std::size_t Dim, std::size_t BS>
inline constexpr uint32_t occ_words_v =
    (voxels_per_brick_v<Dim, BS> + 31u) / 32u;

template <std::size_t Dim, std::size_t BS>
inline constexpr uint32_t words_per_brick_v =
    1u + occ_words_v<Dim, BS>; // base_data_index + occ words

template <std::size_t Dim, std::size_t BS>
uint32_t flat_local(const std::array<uint32_t, Dim>& local) noexcept {
    uint32_t idx = 0, stride = 1;
    for (std::size_t a = 0; a < Dim; ++a) {
        idx += local[a] * stride;
        stride *= static_cast<uint32_t>(BS);
    }
    return idx;
}

template <std::size_t Dim>
uint32_t flat_grid_index(const std::array<uint32_t, Dim>& bp,
                         const std::array<uint32_t, Dim>& extent) noexcept {
    uint32_t idx = 0, stride = 1;
    for (std::size_t a = 0; a < Dim; ++a) {
        idx += bp[a] * stride;
        stride *= extent[a];
    }
    return idx;
}

} // namespace detail

// -------------------------------------------------------
// Configuration and error types
// -------------------------------------------------------

export struct BrickmapConfig {
    std::size_t brick_size = 8; // per-axis (2, 4, 8, or 16)
};

export struct BrickmapUploadError {
    struct InvalidBrickSize {
        std::size_t provided;
        std::size_t dim;

        std::string message() const {
            if (provided != 2 && provided != 4 && provided != 8 && provided != 16)
                return std::format(
                    "brickmap_upload: unsupported brick_size {} (must be 2, 4, 8, or 16)",
                    provided);
            return std::format(
                "brickmap_upload: brick_size {} with dim {} yields {} voxels per brick "
                "(too large for efficient bitmask storage)",
                provided, dim,
                [&] {
                    std::size_t r = 1;
                    for (std::size_t i = 0; i < dim; ++i) r *= provided;
                    return r;
                }());
        }
    };

    using Data = std::variant<InvalidBrickSize>;
    Data data;

    template <typename E>
        requires std::constructible_from<Data, E>
    explicit BrickmapUploadError(E&& e) : data(std::forward<E>(e)) {}

    template <typename E>
    bool is() const noexcept { return std::holds_alternative<E>(data); }

    template <typename E>
    const E* get() const noexcept { return std::get_if<E>(&data); }

    std::string message() const {
        return std::visit([](const auto& e) { return e.message(); }, data);
    }
};

// -------------------------------------------------------
// Internal upload implementation
// -------------------------------------------------------
namespace detail {

template <std::size_t BS, std::size_t Dim, typename CoordT,
          epix::ext::grid::any_grid G>
BrickmapBuffer brickmap_upload_cc(const G& grid) {
    constexpr uint32_t OCC_WORDS = occ_words_v<Dim, BS>;
    constexpr uint32_t WPB       = words_per_brick_v<Dim, BS>;
    BrickmapBuffer buf;
    const uint32_t n = static_cast<uint32_t>(grid.count());

    if (n == 0) {
        uint32_t h = (uint32_t(Dim) & 0xFFu) | ((uint32_t(BS) & 0xFFu) << 8u);
        buf.words.push_back(h);
        buf.words.push_back(0u);
        for (std::size_t a = 0; a < Dim; ++a) buf.words.push_back(0u);
        for (std::size_t a = 0; a < Dim; ++a) buf.words.push_back(0u);
        buf.words.push_back(0u);
        buf.words.push_back(WPB);
        return buf;
    }

    // 1. Bounding box
    std::array<CoordT, Dim> min_pos{}, max_pos{};
    {
        bool first = true;
        for (const auto& pos : grid.iter_pos()) {
            if (first) { min_pos = pos; max_pos = pos; first = false; continue; }
            for (std::size_t a = 0; a < Dim; ++a) {
                min_pos[a] = std::min(min_pos[a], pos[a]);
                max_pos[a] = std::max(max_pos[a], pos[a]);
            }
        }
    }

    // 2. Brick-space extents and origin (aligned to brick boundary)
    std::array<int32_t, Dim> origin;
    std::array<uint32_t, Dim> extent;
    for (std::size_t a = 0; a < Dim; ++a) {
        auto lo = static_cast<int32_t>(min_pos[a]);
        auto hi = static_cast<int32_t>(max_pos[a]);
        origin[a] = (lo >= 0)
            ? (lo / int32_t(BS)) * int32_t(BS)
            : ((lo - int32_t(BS) + 1) / int32_t(BS)) * int32_t(BS);
        extent[a] = static_cast<uint32_t>((hi - origin[a]) / int32_t(BS)) + 1u;
    }

    uint32_t grid_total = 1u;
    for (std::size_t a = 0; a < Dim; ++a) grid_total *= extent[a];

    // 3. Allocate brick grid and brick data
    constexpr uint32_t EMPTY = 0xFFFF'FFFFu;
    std::vector<uint32_t> brick_grid(grid_total, EMPTY);

    struct BrickOcc {
        std::vector<uint32_t> occ;
        BrickOcc() : occ(OCC_WORDS, 0u) {}
    };
    std::vector<BrickOcc> bricks;

    // Track iter_pos ordinal -> (brick_idx, flat_bit) for reorder
    struct VoxelRef { uint32_t brick_idx; uint32_t flat_bit; };
    std::vector<VoxelRef> voxel_refs;
    voxel_refs.reserve(n);

    // 4. First pass: assign voxels to bricks, set occupancy bits
    for (const auto& pos : grid.iter_pos()) {
        std::array<uint32_t, Dim> bp, local;
        for (std::size_t a = 0; a < Dim; ++a) {
            auto rel = static_cast<uint32_t>(
                static_cast<int32_t>(pos[a]) - origin[a]);
            bp[a]    = rel / uint32_t(BS);
            local[a] = rel % uint32_t(BS);
        }
        uint32_t gi = flat_grid_index<Dim>(bp, extent);
        if (brick_grid[gi] == EMPTY) {
            brick_grid[gi] = static_cast<uint32_t>(bricks.size());
            bricks.emplace_back();
        }
        uint32_t bi   = brick_grid[gi];
        uint32_t flat = flat_local<Dim, BS>(local);
        BrickOcc& occ = bricks[bi];
        occ.occ[flat / 32u] |= (1u << (flat % 32u));
        voxel_refs.push_back({bi, flat});
    }

    // 5. Compute base_data_index per brick (cumulative popcount)
    uint32_t brick_count = static_cast<uint32_t>(bricks.size());
    std::vector<uint32_t> base_data(brick_count, 0u);
    {
        uint32_t running = 0u;
        // Walk bricks in grid order (axis-0 fastest)
        std::vector<uint32_t> grid_order_bricks;
        grid_order_bricks.reserve(brick_count);
        // Map from old brick index -> grid-order index
        std::vector<uint32_t> brick_reindex(brick_count, EMPTY);
        for (uint32_t gi = 0; gi < grid_total; ++gi) {
            if (brick_grid[gi] != EMPTY) {
                uint32_t old_bi = brick_grid[gi];
                brick_reindex[old_bi] =
                    static_cast<uint32_t>(grid_order_bricks.size());
                grid_order_bricks.push_back(old_bi);
            }
        }
        for (uint32_t i = 0; i < brick_count; ++i) {
            uint32_t old_bi = grid_order_bricks[i];
            base_data[i]    = running;
            for (uint32_t w = 0; w < OCC_WORDS; ++w)
                running += static_cast<uint32_t>(
                    std::popcount(bricks[old_bi].occ[w]));
        }
        // Remap brick_grid and voxel_refs to grid-order indices
        for (uint32_t gi = 0; gi < grid_total; ++gi)
            if (brick_grid[gi] != EMPTY)
                brick_grid[gi] = brick_reindex[brick_grid[gi]];
        for (auto& vr : voxel_refs)
            vr.brick_idx = brick_reindex[vr.brick_idx];
        // Reorder bricks to grid order
        std::vector<BrickOcc> ordered_bricks(brick_count);
        for (uint32_t i = 0; i < brick_count; ++i)
            ordered_bricks[i] = std::move(bricks[grid_order_bricks[i]]);
        bricks = std::move(ordered_bricks);
    }

    // 6. Build data_index_map: packed_spatial_idx -> iter_pos ordinal
    std::vector<uint32_t> data_index_map(n, 0u);
    for (uint32_t ordinal = 0; ordinal < n; ++ordinal) {
        auto [bi, flat] = voxel_refs[ordinal];
        // popcount of bits below flat in brick bi
        uint32_t below = 0;
        uint32_t fw = flat / 32u, rb = flat % 32u;
        for (uint32_t w = 0; w < fw; ++w)
            below += static_cast<uint32_t>(std::popcount(bricks[bi].occ[w]));
        if (rb > 0u)
            below += static_cast<uint32_t>(
                std::popcount(bricks[bi].occ[fw] & ((1u << rb) - 1u)));
        uint32_t packed_idx = base_data[bi] + below;
        data_index_map[packed_idx] = ordinal;
    }

    // 7. Serialize flat buffer
    // Header
    uint32_t h = (uint32_t(Dim) & 0xFFu) | ((uint32_t(BS) & 0xFFu) << 8u);
    buf.words.push_back(h);
    buf.words.push_back(n);
    for (std::size_t a = 0; a < Dim; ++a)
        buf.words.push_back(extent[a]);
    for (std::size_t a = 0; a < Dim; ++a)
        buf.words.push_back(static_cast<uint32_t>(origin[a]));
    buf.words.push_back(brick_count);
    buf.words.push_back(WPB);
    // Grid section: 1-based brick indices
    for (uint32_t gi = 0; gi < grid_total; ++gi) {
        buf.words.push_back(
            brick_grid[gi] == EMPTY ? 0u : brick_grid[gi] + 1u);
    }
    // Brick pool
    for (uint32_t bi = 0; bi < brick_count; ++bi) {
        buf.words.push_back(base_data[bi]);
        for (uint32_t w = 0; w < OCC_WORDS; ++w)
            buf.words.push_back(bricks[bi].occ[w]);
    }
    // Data index map: packed_spatial_idx -> iter_pos ordinal
    for (uint32_t i = 0; i < n; ++i)
        buf.words.push_back(data_index_map[i]);
    return buf;
}

} // namespace detail

// -------------------------------------------------------
// Public upload API
// -------------------------------------------------------

export template <epix::ext::grid::any_grid G>
    requires(epix::ext::grid::grid_trait<G>::dim >= 1)
std::expected<BrickmapBuffer, BrickmapUploadError>
brickmap_upload(const G& grid, const BrickmapConfig& config = {}) {
    using Trait               = epix::ext::grid::grid_trait<G>;
    constexpr std::size_t Dim = Trait::dim;
    using CoordT              = typename Trait::coord_type;

    switch (config.brick_size) {
        case 2:
            return detail::brickmap_upload_cc<2, Dim, CoordT>(grid);
        case 4:
            return detail::brickmap_upload_cc<4, Dim, CoordT>(grid);
        case 8:
            return detail::brickmap_upload_cc<8, Dim, CoordT>(grid);
        case 16:
            return detail::brickmap_upload_cc<16, Dim, CoordT>(grid);
        default:
            break;
    }
    return std::unexpected(BrickmapUploadError{
        BrickmapUploadError::InvalidBrickSize{config.brick_size, Dim}});
}

} // namespace epix::ext::grid_gpu
