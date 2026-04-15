#include <imgui.h>
import std;
import glm;
import webgpu;
import epix.assets;
import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.core_graph;
import epix.mesh;
import epix.transform;
import epix.input;
import epix.extension.grid;
import epix.extension.grid_gpu;
import epix.render.imgui;
import epix.shader;
import BS.thread_pool;

using std::uint32_t;
using std::size_t;

namespace {
using namespace epix;
using namespace epix::core;
using epix::ext::grid::packed_grid;
using epix::ext::grid_gpu::kSvoGridSlangSource;
using epix::ext::grid_gpu::svo_upload;
using epix::ext::grid_gpu::SvoBuffer;
using epix::ext::grid_gpu::SvoConfig;

// ------- Grid / simulation constants -------
constexpr int kN          = 200;
constexpr int kIter       = 20;
constexpr float kGravity  = 0.5f;
constexpr float kTargetD  = 1.0f;
constexpr float kMaxVel   = 8.0f;
constexpr float kCellSize = 4.0f;

// ------- Chunk layout constants -------
constexpr int kChunkSize = 16;
// ceil(kN / kChunkSize) → 13 chunks per axis covering cells [0, 207]; grid uses [0, 199]
constexpr int kChunksX    = (kN + kChunkSize - 1) / kChunkSize;  // 13
constexpr int kChunksY    = (kN + kChunkSize - 1) / kChunkSize;  // 13
constexpr int kMaxChunks  = kChunksX * kChunksY;                 // 169
constexpr int kChunkCells = kChunkSize * kChunkSize;             // 256

// ------- GPU buffer planar field sections (in i32 elements from buffer start) -------
// packed_grid::offset({x,y}) = x * kChunksY + y  →  chunk index = cx * kChunksY + cy
constexpr int kGpuDBase      = 0;
constexpr int kGpuSBase      = kMaxChunks * kChunkCells;      // 43264
constexpr int kGpuPBase      = 2 * kMaxChunks * kChunkCells;  // 86528
constexpr int kGpuUBase      = 3 * kMaxChunks * kChunkCells;  // 129792
constexpr int kGpuVBase      = 4 * kMaxChunks * kChunkCells;  // 173056
constexpr int kGpuTotalElems = 5 * kMaxChunks * kChunkCells;  // 216320

constexpr std::size_t kGpuChunkDataBytes    = static_cast<std::size_t>(kGpuTotalElems) * sizeof(std::int32_t);
constexpr std::size_t kGpuFieldSectionBytes = static_cast<std::size_t>(kMaxChunks * kChunkCells) * sizeof(std::int32_t);

using fx32                                 = std::int32_t;
constexpr fx32 kFxShift                    = 16;
constexpr fx32 kFxOne                      = static_cast<fx32>(1 << kFxShift);
constexpr fx32 kFxHalf                     = kFxOne / 2;
constexpr fx32 kFx005                      = 3277;
constexpr fx32 kFx008                      = 5243;
constexpr fx32 kFx01                       = 6554;
constexpr fx32 kFx02                       = 13107;
constexpr fx32 kFx08                       = 52429;
constexpr fx32 kFx13                       = 85197;
constexpr fx32 kFx60                       = 3932160;
constexpr fx32 kFx0001                     = 7;
constexpr fx32 kFxSoftStart                = 72090;
constexpr fx32 kFxSoftRange                = 16384;
constexpr fx32 kFxMaxDensity               = 88474;
constexpr fx32 kFxSoftZero                 = 66;
constexpr fx32 kFxExp1mMaxT                = 524288;
constexpr fx32 kFxExp1mLutLast             = 65514;
constexpr std::array<fx32, 33> kFxExp1mLut = {
    0,     14497, 25786, 34579, 41427, 46760, 50913, 54148, 56667, 58629, 60156,
    61346, 62273, 62995, 63557, 63995, 64336, 64601, 64808, 64969, 65094, 65192,
    65268, 65327, 65374, 65409, 65437, 65459, 65476, 65489, 65500, 65508, 65514,
};

inline fx32 fx_from_float(float v) {
    return static_cast<fx32>(std::llround(static_cast<double>(v) * static_cast<double>(kFxOne)));
}

inline float fx_to_float(fx32 v) { return static_cast<float>(static_cast<double>(v) / static_cast<double>(kFxOne)); }

inline fx32 fx_mul(fx32 a, fx32 b) {
    return static_cast<fx32>((static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> kFxShift);
}

inline fx32 fx_div(fx32 a, fx32 b) {
    if (b == 0) return 0;
    return static_cast<fx32>((static_cast<std::int64_t>(a) << kFxShift) / static_cast<std::int64_t>(b));
}

inline fx32 fx_abs(fx32 v) { return v < 0 ? static_cast<fx32>(-v) : v; }

inline fx32 fx_exp1m_approx(fx32 t_fx) {
    if (t_fx <= 0) return 0;
    if (t_fx >= kFxExp1mMaxT) return kFxExp1mLutLast;
    const fx32 idx     = t_fx >> 14;
    const fx32 rem     = t_fx - (idx << 14);
    const fx32 frac_fx = rem << 2;
    const fx32 a       = kFxExp1mLut[static_cast<std::size_t>(idx)];
    const fx32 b       = kFxExp1mLut[static_cast<std::size_t>(idx + 1)];
    return a + fx_mul(b - a, frac_fx);
}

inline fx32 soft_bound_density_fx(fx32 v_fx) {
    if (v_fx <= 0) return 0;
    if (v_fx <= kFxSoftStart) return v_fx;
    const fx32 over_fx  = v_fx - kFxSoftStart;
    const fx32 t_fx     = over_fx << 2;
    const fx32 eased_fx = fx_exp1m_approx(t_fx);
    fx32 bounded_fx     = kFxSoftStart + fx_mul(kFxSoftRange, eased_fx);
    if (bounded_fx > kFxMaxDensity) bounded_fx = kFxMaxDensity;
    if (bounded_fx < kFxSoftZero) bounded_fx = 0;
    return bounded_fx;
}

struct Fluid;
struct GpuPressureProjector;
bool gpu_pressure_project(Fluid& sim,
                          GpuPressureProjector* projector,
                          const wgpu::Device* device,
                          const wgpu::Queue* queue);
bool gpu_advect_velocity_rk2(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue);
bool gpu_density_transport(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue);
bool gpu_add_gravity(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue);
bool gpu_postprocess_velocities(Fluid& sim,
                                GpuPressureProjector* projector,
                                const wgpu::Device* device,
                                const wgpu::Queue* queue);
bool gpu_surface_tension(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue);
bool gpu_viscosity(Fluid& sim,
                   fx32 sticky_fx,
                   fx32 dt_fx,
                   GpuPressureProjector* projector,
                   const wgpu::Device* device,
                   const wgpu::Queue* queue);
bool gpu_extrapolate_velocity(Fluid& sim,
                              GpuPressureProjector* projector,
                              const wgpu::Device* device,
                              const wgpu::Queue* queue);
bool gpu_clamp_velocities(Fluid& sim,
                          GpuPressureProjector* projector,
                          const wgpu::Device* device,
                          const wgpu::Queue* queue);
bool gpu_run_full_step_chain(Fluid& sim,
                             fx32 dt_fx,
                             fx32 sticky_fx,
                             GpuPressureProjector* projector,
                             const wgpu::Device* device,
                             const wgpu::Queue* queue,
                             render::PipelineServer* pipeline_server);

enum class PaintTool {
    Water,
    Wall,
    Eraser,
};

// Per-chunk simulation data.  Chunk (cx, cy) has index = cx * kChunksY + cy
// (matches packed_grid<2,T>{{kChunksX,kChunksY}} iteration order).
// U and V are "CS×CS" arrays; the staggered face at global gx belongs to
// chunk cx = gx / kChunkSize, local lx = gx % kChunkSize.  This works because
// N % kChunkSize ≠ 0, so the boundary face at gx = kN = 200 lands in chunk 12
// at local lx = 8 (< kChunkSize), and no chunk dimension exceeds kChunkSize.
struct ChunkData {
    packed_grid<2, fx32> D{{kChunkSize, kChunkSize}, 0};
    packed_grid<2, std::uint8_t> S{{kChunkSize, kChunkSize}, 0};
    packed_grid<2, fx32> P{{kChunkSize, kChunkSize}, 0};
    packed_grid<2, fx32> U{{kChunkSize, kChunkSize}, 0};
    packed_grid<2, fx32> V{{kChunkSize, kChunkSize}, 0};
};

struct Fluid {
    std::array<ChunkData, kMaxChunks> chunks{};

    std::vector<int> active_indices{};
    std::vector<int> core_indices{};
    fx32 target_total_mass_fx  = 0;
    fx32 current_total_mass_fx = 0;

    PaintTool tool   = PaintTool::Water;
    bool paused      = false;
    int pen_size     = 2;
    float dt_scale   = 5.0f;
    float stickiness = 0.0f;

    // ------- Helpers -------
    static constexpr std::uint32_t u32(int v) { return static_cast<std::uint32_t>(v); }
    static constexpr int idx(int x, int y) { return x + y * kN; }
    static constexpr auto deref = [](auto&& r) { return r.get(); };

    // Index into chunks[] for chunk at (cx, cy)
    static constexpr int ci(int cx, int cy) { return cx * kChunksY + cy; }

    ChunkData& chunk_at(int cx, int cy) { return chunks[ci(cx, cy)]; }
    const ChunkData& chunk_at(int cx, int cy) const { return chunks[ci(cx, cy)]; }

    // Global → chunk coord
    static int cx_of(int gx) { return gx / kChunkSize; }
    static int cy_of(int gy) { return gy / kChunkSize; }
    static int lx_of(int gx) { return gx % kChunkSize; }
    static int ly_of(int gy) { return gy % kChunkSize; }

    // ------- Cell-centred accessors -------
    fx32 getD_fx(int gx, int gy) const {
        if (gx < 0 || gx >= kN || gy < 0 || gy >= kN) return 0;
        const int c = ci(cx_of(gx), cy_of(gy));
        return chunks[c].D.get({u32(lx_of(gx)), u32(ly_of(gy))}).transform(deref).value_or(0);
    }
    float getD(int x, int y) const { return fx_to_float(getD_fx(x, y)); }

    std::uint8_t getS(int gx, int gy) const {
        if (gx < 0 || gx >= kN || gy < 0 || gy >= kN) return 1;
        const int c = ci(cx_of(gx), cy_of(gy));
        return chunks[c].S.get({u32(lx_of(gx)), u32(ly_of(gy))}).transform(deref).value_or(std::uint8_t{1});
    }

    // ------- Staggered velocity accessors -------
    // U valid at gx ∈ [0, kN], gy ∈ [0, kN-1]
    fx32 getU_fx(int gx, int gy) const {
        if (gx < 0 || gx > kN || gy < 0 || gy >= kN) return 0;
        if (cx_of(gx) >= kChunksX || cy_of(gy) >= kChunksY) return 0;
        const int c = ci(cx_of(gx), cy_of(gy));
        return chunks[c].U.get({u32(lx_of(gx)), u32(ly_of(gy))}).transform(deref).value_or(0);
    }
    float getU(int x, int y) const { return fx_to_float(getU_fx(x, y)); }

    // V valid at gx ∈ [0, kN-1], gy ∈ [0, kN]
    fx32 getV_fx(int gx, int gy) const {
        if (gx < 0 || gx >= kN || gy < 0 || gy > kN) return 0;
        if (cx_of(gx) >= kChunksX || cy_of(gy) >= kChunksY) return 0;
        const int c = ci(cx_of(gx), cy_of(gy));
        return chunks[c].V.get({u32(lx_of(gx)), u32(ly_of(gy))}).transform(deref).value_or(0);
    }
    float getV(int x, int y) const { return fx_to_float(getV_fx(x, y)); }

    // ------- Setters -------
    void setD(int gx, int gy, float v) {
        if (gx < 0 || gx >= kN || gy < 0 || gy >= kN) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].D.set({u32(lx_of(gx)), u32(ly_of(gy))},
                                                     soft_bound_density_fx(fx_from_float(v)));
    }
    void setD_fx(int gx, int gy, fx32 v) {
        if (gx < 0 || gx >= kN || gy < 0 || gy >= kN) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].D.set({u32(lx_of(gx)), u32(ly_of(gy))}, v);
    }
    void setS(int gx, int gy, std::uint8_t v) {
        if (gx < 0 || gx >= kN || gy < 0 || gy >= kN) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].S.set({u32(lx_of(gx)), u32(ly_of(gy))}, v);
    }
    void setP(int gx, int gy, float v) {
        if (gx < 0 || gx >= kN || gy < 0 || gy >= kN) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].P.set({u32(lx_of(gx)), u32(ly_of(gy))}, fx_from_float(v));
    }
    void setU(int gx, int gy, float v) {
        if (gx < 0 || gx > kN || gy < 0 || gy >= kN || cx_of(gx) >= kChunksX || cy_of(gy) >= kChunksY) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].U.set({u32(lx_of(gx)), u32(ly_of(gy))}, fx_from_float(v));
    }
    void setU_fx(int gx, int gy, fx32 v) {
        if (gx < 0 || gx > kN || gy < 0 || gy >= kN || cx_of(gx) >= kChunksX || cy_of(gy) >= kChunksY) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].U.set({u32(lx_of(gx)), u32(ly_of(gy))}, v);
    }
    void setV(int gx, int gy, float v) {
        if (gx < 0 || gx >= kN || gy < 0 || gy > kN || cx_of(gx) >= kChunksX || cy_of(gy) >= kChunksY) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].V.set({u32(lx_of(gx)), u32(ly_of(gy))}, fx_from_float(v));
    }
    void setV_fx(int gx, int gy, fx32 v) {
        if (gx < 0 || gx >= kN || gy < 0 || gy > kN || cx_of(gx) >= kChunksX || cy_of(gy) >= kChunksY) return;
        (void)chunks[ci(cx_of(gx), cy_of(gy))].V.set({u32(lx_of(gx)), u32(ly_of(gy))}, v);
    }

    // ------- Reset -------
    void reset() {
        for (auto& chunk : chunks) {
            chunk.D.clear();
            chunk.S.clear();
            chunk.P.clear();
            chunk.U.clear();
            chunk.V.clear();
        }
        target_total_mass_fx  = 0;
        current_total_mass_fx = 0;

        for (int i = 0; i < kN; ++i) {
            setS(i, 0, 1);
            setS(i, kN - 1, 1);
            setS(0, i, 1);
            setS(kN - 1, i, 1);
        }
        for (int x = 30; x < 70; ++x) setS(x, 70, 1);
        for (int y = 30; y < 70; ++y) {
            setS(30, y, 1);
            setS(70, y, 1);
        }
    }

    // ------- Paint brush -------
    void apply_brush(int cx, int cy, PaintTool paint) {
        for (int oy = -pen_size; oy <= pen_size; ++oy) {
            for (int ox = -pen_size; ox <= pen_size; ++ox) {
                const int x = cx + ox;
                const int y = cy + oy;
                if (x <= 0 || x >= kN - 1 || y <= 0 || y >= kN - 1) continue;

                const int id = idx(x, y);
                if (paint == PaintTool::Water && getS(x, y) == 0) {
                    if (getD(x, y) < 1.0f) {
                        const fx32 d_fx = getD_fx(x, y);
                        const fx32 add  = kFxOne - d_fx;
                        setD(x, y, 1.0f);
                        target_total_mass_fx = static_cast<fx32>(target_total_mass_fx + add);
                    }
                } else if (paint == PaintTool::Wall && getS(x, y) == 0) {
                    target_total_mass_fx = static_cast<fx32>(target_total_mass_fx - getD_fx(x, y));
                    setS(x, y, 1);
                    setD(x, y, 0.0f);
                    setU(x, y, 0.0f);
                    setU(x + 1, y, 0.0f);
                    setV(x, y, 0.0f);
                    setV(x, y + 1, 0.0f);
                } else if (paint == PaintTool::Eraser) {
                    target_total_mass_fx = static_cast<fx32>(target_total_mass_fx - getD_fx(x, y));
                    setS(x, y, 0);
                    setD(x, y, 0.0f);
                }
                (void)id;
            }
        }
        if (target_total_mass_fx < 0) target_total_mass_fx = 0;
    }

    // ------- Physics step -------
    void physicsStep(float dt,
                     BS::thread_pool<>* pool                 = nullptr,
                     GpuPressureProjector* projector         = nullptr,
                     const wgpu::Device* device              = nullptr,
                     const wgpu::Queue* queue                = nullptr,
                     render::PipelineServer* pipeline_server = nullptr) {
        if (projector == nullptr || device == nullptr || queue == nullptr || pipeline_server == nullptr)
            throw std::runtime_error("GPU simulation requires projector/device/queue/pipeline_server");

        const fx32 sticky_fx = fx_from_float(stickiness + 0.001f);
        const fx32 dt_fx     = fx_from_float(dt);
        const fx32 active_fx = kFx005;
        const fx32 core_lo   = kFx08;
        const fx32 core_hi   = kFx13;

        active_indices.clear();
        core_indices.clear();
        std::int64_t current_mass_acc_fx = 0;

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                if (getS(x, y) != 0) continue;
                const fx32 d_fx = getD_fx(x, y);
                current_mass_acc_fx += static_cast<std::int64_t>(d_fx);
                if (d_fx > active_fx) active_indices.push_back(idx(x, y));
                if (d_fx > core_lo && d_fx < core_hi) core_indices.push_back(idx(x, y));
            }
        }
        current_total_mass_fx = static_cast<fx32>(std::clamp<std::int64_t>(
            current_mass_acc_fx, std::numeric_limits<fx32>::min(), std::numeric_limits<fx32>::max()));

        const std::int64_t target_mass_fx = static_cast<std::int64_t>(target_total_mass_fx);
        if (target_mass_fx > kFx0001 && !core_indices.empty()) {
            std::int64_t diff_fx         = target_mass_fx - current_mass_acc_fx;
            const fx32 max_correction_fx = fx_mul(static_cast<fx32>(target_mass_fx), fx_mul(kFx01, dt_fx));
            diff_fx = std::clamp<std::int64_t>(diff_fx, -static_cast<std::int64_t>(max_correction_fx),
                                               static_cast<std::int64_t>(max_correction_fx));
            if (std::llabs(diff_fx) > kFx0001) {
                const std::int64_t add_per_cell_fx = diff_fx / static_cast<std::int64_t>(core_indices.size());
                for (const int id : core_indices) {
                    const int x   = id % kN;
                    const int y   = id / kN;
                    const fx32 d0 = getD_fx(x, y);
                    setD_fx(x, y, static_cast<fx32>(d0 + add_per_cell_fx));
                }
            }
        } else if (current_mass_acc_fx < kFxOne) {
            target_total_mass_fx = 0;
        }

        // If GPU pipelines are still compiling, skip the GPU step this frame rather than throwing.
        if (!gpu_run_full_step_chain(*this, dt_fx, sticky_fx, projector, device, queue, pipeline_server)) return;
    }
    void solve(float totalDt,
               BS::thread_pool<>* pool                 = nullptr,
               GpuPressureProjector* projector         = nullptr,
               const wgpu::Device* device              = nullptr,
               const wgpu::Queue* queue                = nullptr,
               render::PipelineServer* pipeline_server = nullptr) {
        fx32 max_vel_fx              = 0;
        const std::size_t task_count = pool == nullptr ? 0 : std::max<std::size_t>(1, pool->get_thread_count());
        if (pool != nullptr && task_count > 1) {
            const auto u_maxes = pool->submit_blocks(
                                         0, static_cast<std::int64_t>(kN),
                                         [this](std::int64_t ys, std::int64_t ye) {
                                             fx32 local_max = 0;
                                             for (std::int64_t y = ys; y < ye; ++y)
                                                 for (int x = 0; x <= kN; ++x)
                                                     local_max =
                                                         std::max(local_max, fx_abs(getU_fx(x, static_cast<int>(y))));
                                             return local_max;
                                         },
                                         task_count)
                                     .get();
            const auto v_maxes = pool->submit_blocks(
                                         0, static_cast<std::int64_t>(kN + 1),
                                         [this](std::int64_t ys, std::int64_t ye) {
                                             fx32 local_max = 0;
                                             for (std::int64_t y = ys; y < ye; ++y)
                                                 for (int x = 0; x < kN; ++x)
                                                     local_max =
                                                         std::max(local_max, fx_abs(getV_fx(x, static_cast<int>(y))));
                                             return local_max;
                                         },
                                         task_count)
                                     .get();
            for (const fx32 v : u_maxes) max_vel_fx = std::max(max_vel_fx, v);
            for (const fx32 v : v_maxes) max_vel_fx = std::max(max_vel_fx, v);
        } else {
            for (int y = 0; y < kN; ++y)
                for (int x = 0; x <= kN; ++x) max_vel_fx = std::max(max_vel_fx, fx_abs(getU_fx(x, y)));
            for (int y = 0; y <= kN; ++y)
                for (int x = 0; x < kN; ++x) max_vel_fx = std::max(max_vel_fx, fx_abs(getV_fx(x, y)));
        }

        fx32 max_vel_safe_fx   = std::max(max_vel_fx, kFx01);
        fx32 max_allowed_dt_fx = fx_div(kFx08, max_vel_safe_fx);
        if (max_allowed_dt_fx > kFx02) max_allowed_dt_fx = kFx02;

        fx32 remaining_fx = fx_from_float(totalDt);
        int substeps      = 0;
        while (remaining_fx > kFx0001 && substeps < 10) {
            fx32 step_fx = remaining_fx;
            if (step_fx > max_allowed_dt_fx) step_fx = max_allowed_dt_fx;
            physicsStep(fx_to_float(step_fx), pool, projector, device, queue, pipeline_server);
            remaining_fx = static_cast<fx32>(remaining_fx - step_fx);
            ++substeps;
        }
    }
};

struct GpuPressureProjector {
    // ------- GPU buffers -------
    // One large "chunk_data" buffer (planar layout, 5 sections: D, S, P, U, V)
    wgpu::Buffer chunk_data_buf;
    // Per-field output buffers (advect / density / surface / clamp write output here)
    wgpu::Buffer chunk_u_out_buf;
    wgpu::Buffer chunk_v_out_buf;
    wgpu::Buffer chunk_d_out_buf;
    // SVO buffer (chunk positions, uploaded from CPU-side SvoBuffer)
    wgpu::Buffer chunk_svo_buf;
    // Uniform parameter buffer
    wgpu::Buffer param_buf;
    // Readback buffer (copy of chunk_data after simulation step)
    wgpu::Buffer readback_buf;

    // ------- Compute pipelines -------
    wgpu::ComputePipeline pipeline_even;
    wgpu::ComputePipeline pipeline_odd;
    wgpu::ComputePipeline advect_u_pipeline;
    wgpu::ComputePipeline advect_v_pipeline;
    wgpu::ComputePipeline density_pipeline;
    wgpu::ComputePipeline gravity_pipeline;
    wgpu::ComputePipeline post_u_pipeline;
    wgpu::ComputePipeline post_v_pipeline;
    wgpu::ComputePipeline surface_u_pipeline;
    wgpu::ComputePipeline surface_v_pipeline;
    wgpu::ComputePipeline visc_u_even_pipeline;
    wgpu::ComputePipeline visc_u_odd_pipeline;
    wgpu::ComputePipeline visc_v_even_pipeline;
    wgpu::ComputePipeline visc_v_odd_pipeline;
    wgpu::ComputePipeline visc_wall_u_pipeline;
    wgpu::ComputePipeline visc_wall_v_pipeline;
    wgpu::ComputePipeline extrap_cell_even_pipeline;
    wgpu::ComputePipeline extrap_cell_odd_pipeline;
    wgpu::ComputePipeline clamp_u_pipeline;
    wgpu::ComputePipeline clamp_v_pipeline;

    // ------- Bind groups -------
    wgpu::BindGroup bind_group_even;
    wgpu::BindGroup bind_group_odd;
    wgpu::BindGroup advect_u_bind_group;
    wgpu::BindGroup advect_v_bind_group;
    wgpu::BindGroup density_bind_group;
    wgpu::BindGroup gravity_bind_group;
    wgpu::BindGroup post_u_bind_group;
    wgpu::BindGroup post_v_bind_group;
    wgpu::BindGroup surface_u_bind_group;
    wgpu::BindGroup surface_v_bind_group;
    wgpu::BindGroup visc_u_even_bind_group;
    wgpu::BindGroup visc_u_odd_bind_group;
    wgpu::BindGroup visc_v_even_bind_group;
    wgpu::BindGroup visc_v_odd_bind_group;
    wgpu::BindGroup visc_wall_u_bind_group;
    wgpu::BindGroup visc_wall_v_bind_group;
    wgpu::BindGroup extrap_cell_even_bind_group;
    wgpu::BindGroup extrap_cell_odd_bind_group;
    wgpu::BindGroup clamp_u_bind_group;
    wgpu::BindGroup clamp_v_bind_group;

    // ------- CPU-side staging data -------
    // kGpuTotalElems i32 values: [D_section | S_section | P_section | U_section | V_section]
    std::vector<std::int32_t> host_data = std::vector<std::int32_t>(static_cast<std::size_t>(kGpuTotalElems), 0);

    static constexpr std::size_t kParamBytes = 32;

    bool ready = false;

    // SVO data for the chunk-position grid (built once in init, uploaded on first use)
    SvoBuffer svo_chunk_pos;
    bool svo_uploaded = false;

    bool buffers_ready_ = false;
    bool queued_        = false;

    std::optional<assets::Handle<shader::Shader>> svo_library_handle_;

    render::CachedPipelineId pipeline_even_id{};
    render::CachedPipelineId pipeline_odd_id{};
    render::CachedPipelineId gravity_pipeline_id{};
    render::CachedPipelineId post_u_pipeline_id{};
    render::CachedPipelineId post_v_pipeline_id{};
    render::CachedPipelineId visc_u_even_pipeline_id{};
    render::CachedPipelineId visc_u_odd_pipeline_id{};
    render::CachedPipelineId visc_v_even_pipeline_id{};
    render::CachedPipelineId visc_v_odd_pipeline_id{};
    render::CachedPipelineId visc_wall_u_pipeline_id{};
    render::CachedPipelineId visc_wall_v_pipeline_id{};
    render::CachedPipelineId extrap_cell_even_pipeline_id{};
    render::CachedPipelineId extrap_cell_odd_pipeline_id{};
    render::CachedPipelineId advect_u_pipeline_id{};
    render::CachedPipelineId advect_v_pipeline_id{};
    render::CachedPipelineId density_pipeline_id{};
    render::CachedPipelineId surface_u_pipeline_id{};
    render::CachedPipelineId surface_v_pipeline_id{};
    render::CachedPipelineId clamp_u_pipeline_id{};
    render::CachedPipelineId clamp_v_pipeline_id{};

    // Creates GPU buffers and builds the chunk-position SVO. Called lazily each frame.
    void init(const wgpu::Device& device) {
        if (buffers_ready_) return;

        // ------- Slang shader common preambles (SVO-indexed chunk lookup via kSvoGridSlangSource) -------
        // Buffer layout: one flat array<int32> for [D|S|P|U|V] field sections,
        // each kMaxChunks*kChunkCells elements; chunk index via SvoGrid2D.lookup(cx,cy).
        // -----------------------------------------------------------------------
        // Slang shader sources �?use epix.ext.grid.svo library (SvoGrid2D) for
        // chunk-position lookup instead of hand-rolled WGSL SVO traversal.
        // -----------------------------------------------------------------------

        // Common preamble: inplace (bindings 0=chunk_data RW, 1=chunk_svo)
        static const std::string kSlangCommonInplace = R"slg(
import epix.ext.grid.svo;
[[vk::binding(0,0)]] RWStructuredBuffer<int>  chunk_data;
[[vk::binding(1,0)]] StructuredBuffer<uint>   chunk_svo;
static const int CHUNK_SIZE         = 16;
static const int CHUNK_CELLS        = 256;
static const int CHUNKS_X           = 13;
static const int CHUNKS_Y           = 13;
static const int GRID_N             = 200;
static const int D_BASE             = 0;
static const int S_BASE             = 43264;
static const int P_BASE             = 86528;
static const int U_BASE             = 129792;
static const int V_BASE             = 173056;
static const int MAXV_FX            = 524288;
static const int OMEGA_FX           = 117965;
static const int TARGET_DIV_GAIN_FX = 6554;
int chunk_idx(int cx, int cy) {
    if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) return -1;
    epix::ext::grid::SvoGrid2D g = epix::ext::grid::SvoGrid2D(chunk_svo);
    return g.lookup(int[2](cx, cy));
}
int fx_mul(int a, int b) {
    int ah = a >> 16; int bh = b >> 16; int al = a & 65535; int bl = b & 65535;
    int hi = (ah * bh) << 16; int mid = ah * bl + al * bh;
    int lo = int((uint(al) * uint(bl)) >> 16u);
    return hi + mid + lo;
}
int d_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return 0;
    return chunk_data[D_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
bool s_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return true;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return true;
    return chunk_data[S_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] != 0;
}
int p_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return 0;
    return chunk_data[P_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
int u_at(int gx, int gy) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[U_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
int v_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy > GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[V_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
void set_d(int gx, int gy, int val) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return;
    chunk_data[D_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
void set_p(int gx, int gy, int val) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return;
    chunk_data[P_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
void set_u(int gx, int gy, int val) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy >= GRID_N) return;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return;
    int c = chunk_idx(cx2, cy2); if (c < 0) return;
    chunk_data[U_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
void set_v(int gx, int gy, int val) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy > GRID_N) return;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return;
    int c = chunk_idx(cx2, cy2); if (c < 0) return;
    chunk_data[V_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
)slg";

        // Common preamble: output (bindings 0=chunk_data RO, 1=chunk_out RW, 2=chunk_svo)
        static const std::string kSlangCommonOutput = R"slg(
import epix.ext.grid.svo;
[[vk::binding(0,0)]] StructuredBuffer<int>    chunk_data;
[[vk::binding(1,0)]] RWStructuredBuffer<int>  chunk_out;
[[vk::binding(2,0)]] StructuredBuffer<uint>   chunk_svo;
static const int CHUNK_SIZE  = 16;
static const int CHUNK_CELLS = 256;
static const int CHUNKS_X    = 13;
static const int CHUNKS_Y    = 13;
static const int GRID_N      = 200;
static const int D_BASE      = 0;
static const int S_BASE      = 43264;
static const int P_BASE      = 86528;
static const int U_BASE      = 129792;
static const int V_BASE      = 173056;
static const int MAXV_FX     = 524288;
int chunk_idx(int cx, int cy) {
    if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) return -1;
    epix::ext::grid::SvoGrid2D g = epix::ext::grid::SvoGrid2D(chunk_svo);
    return g.lookup(int[2](cx, cy));
}
int fx_mul(int a, int b) {
    int ah = a >> 16; int bh = b >> 16; int al = a & 65535; int bl = b & 65535;
    int hi = (ah * bh) << 16; int mid = ah * bl + al * bh;
    int lo = int((uint(al) * uint(bl)) >> 16u);
    return hi + mid + lo;
}
int d_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return 0;
    return chunk_data[D_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
bool s_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return true;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return true;
    return chunk_data[S_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] != 0;
}
int u_at(int gx, int gy) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[U_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
int v_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy > GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[V_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
void out_write(int base, int gx, int gy, int val) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy > GRID_N) return;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return;
    int c = chunk_idx(cx2, cy2); if (c < 0) return;
    chunk_out[base + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
)slg";

        // Pressure projection even (inplace, no param)
        static const std::string kShaderEven = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x <= 0 || y <= 0 || x >= GRID_N-1 || y >= GRID_N-1) return;
    if (((x+y)&1) != 0) return;
    int dFx = d_at(x,y); if (dFx < 13107) return;
    int uLFx = u_at(x,y); int uRFx = u_at(x+1,y);
    int vTFx = v_at(x,y); int vBFx = v_at(x,y+1);
    int velDivFx = (uRFx-uLFx) + (vBFx-vTFx);
    int densityErrFx = dFx - 65536;
    int targetDivFx = densityErrFx > 0 ? fx_mul(densityErrFx, TARGET_DIV_GAIN_FX) : 0;
    int totalDivFx = velDivFx - targetDivFx;
    int sL = s_at(x-1,y)?1:0; int sR = s_at(x+1,y)?1:0;
    int sT = s_at(x,y-1)?1:0; int sB = s_at(x,y+1)?1:0;
    int n = 4 - (sL+sR+sT+sB); if (n == 0) return;
    int pCorrFx = int(-totalDivFx / n);
    pCorrFx = fx_mul(pCorrFx, OMEGA_FX);
    int weightFx = dFx >= 65536 ? 65536 : fx_mul(dFx, dFx);
    pCorrFx = fx_mul(pCorrFx, weightFx);
    set_p(x,y, p_at(x,y)+pCorrFx);
    if (sL==0) set_u(x,y, uLFx-pCorrFx);
    if (sR==0) set_u(x+1,y, uRFx+pCorrFx);
    if (sT==0) set_v(x,y, vTFx-pCorrFx);
    if (sB==0) set_v(x,y+1, vBFx+pCorrFx);
}
)slg";

        // Pressure projection odd (inplace, no param)
        static const std::string kShaderOdd = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x <= 0 || y <= 0 || x >= GRID_N-1 || y >= GRID_N-1) return;
    if (((x+y)&1) != 1) return;
    int dFx = d_at(x,y); if (dFx < 13107) return;
    int uLFx = u_at(x,y); int uRFx = u_at(x+1,y);
    int vTFx = v_at(x,y); int vBFx = v_at(x,y+1);
    int velDivFx = (uRFx-uLFx) + (vBFx-vTFx);
    int densityErrFx = dFx - 65536;
    int targetDivFx = densityErrFx > 0 ? fx_mul(densityErrFx, TARGET_DIV_GAIN_FX) : 0;
    int totalDivFx = velDivFx - targetDivFx;
    int sL = s_at(x-1,y)?1:0; int sR = s_at(x+1,y)?1:0;
    int sT = s_at(x,y-1)?1:0; int sB = s_at(x,y+1)?1:0;
    int n = 4 - (sL+sR+sT+sB); if (n == 0) return;
    int pCorrFx = int(-totalDivFx / n);
    pCorrFx = fx_mul(pCorrFx, OMEGA_FX);
    int weightFx = dFx >= 65536 ? 65536 : fx_mul(dFx, dFx);
    pCorrFx = fx_mul(pCorrFx, weightFx);
    set_p(x,y, p_at(x,y)+pCorrFx);
    if (sL==0) set_u(x,y, uLFx-pCorrFx);
    if (sR==0) set_u(x+1,y, uRFx+pCorrFx);
    if (sT==0) set_v(x,y, vTFx-pCorrFx);
    if (sB==0) set_v(x,y+1, vBFx+pCorrFx);
}
)slg";

        // Gravity (inplace, param binding 2)
        static const std::string kShaderGravity = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y <= 0 || y >= GRID_N) return;
    int d0 = d_at(x,y); int d1 = d_at(x,y-1);
    if (d0 > 655 || d1 > 655) set_v(x,y, v_at(x,y) + param.p0/2);
}
)slg";

        // Post-process U (inplace, no param)
        static const std::string kShaderPostU = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    int uFx = u_at(x,y);
    if (abs(uFx) > MAXV_FX) uFx = uFx > 0 ? MAXV_FX : -MAXV_FX;
    bool sL = x == 0 || s_at(x-1,y); bool sR = x == GRID_N || s_at(x,y);
    if (sL || sR) uFx = 0;
    set_u(x,y,uFx);
}
)slg";

        // Post-process V (inplace, no param)
        static const std::string kShaderPostV = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    int vFx = v_at(x,y);
    if (abs(vFx) > MAXV_FX) vFx = vFx > 0 ? MAXV_FX : -MAXV_FX;
    bool sT = y == 0 || s_at(x,y-1); bool sB = y == GRID_N || s_at(x,y);
    if (sT || sB) vFx = 0;
    set_v(x,y,vFx);
}
)slg";

        // Viscosity U even (inplace, param binding 2)
        static const std::string kShaderViscUEven = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_u(int x, int y) { return max(d_at(x-1,y), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 0) return;
    int uFx = u_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (x > 0 && x < GRID_N && y > 0 && y < GRID_N-1 && face_active_u(x,y)) {
        int nFx = u_at(x-1,y)+u_at(x+1,y)+u_at(x,y-1)+u_at(x,y+1);
        uFx = uFx + fx_mul(nFx - 4*uFx, scaleFx);
    }
    set_u(x,y,uFx);
}
)slg";

        // Viscosity U odd (inplace, param binding 2)
        static const std::string kShaderViscUOdd = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_u(int x, int y) { return max(d_at(x-1,y), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 1) return;
    int uFx = u_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (x > 0 && x < GRID_N && y > 0 && y < GRID_N-1 && face_active_u(x,y)) {
        int nFx = u_at(x-1,y)+u_at(x+1,y)+u_at(x,y-1)+u_at(x,y+1);
        uFx = uFx + fx_mul(nFx - 4*uFx, scaleFx);
    }
    set_u(x,y,uFx);
}
)slg";

        // Viscosity V even (inplace, param binding 2)
        static const std::string kShaderViscVEven = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_v(int x, int y) { return max(d_at(x,y-1), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    if (((x+y)&1) != 0) return;
    int vFx = v_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (y > 0 && y < GRID_N && x > 0 && x < GRID_N-1 && face_active_v(x,y)) {
        int nFx = v_at(x-1,y)+v_at(x+1,y)+v_at(x,y-1)+v_at(x,y+1);
        vFx = vFx + fx_mul(nFx - 4*vFx, scaleFx);
    }
    set_v(x,y,vFx);
}
)slg";

        // Viscosity V odd (inplace, param binding 2)
        static const std::string kShaderViscVOdd = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_v(int x, int y) { return max(d_at(x,y-1), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    if (((x+y)&1) != 1) return;
    int vFx = v_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (y > 0 && y < GRID_N && x > 0 && x < GRID_N-1 && face_active_v(x,y)) {
        int nFx = v_at(x-1,y)+v_at(x+1,y)+v_at(x,y-1)+v_at(x,y+1);
        vFx = vFx + fx_mul(nFx - 4*vFx, scaleFx);
    }
    set_v(x,y,vFx);
}
)slg";

        // Wall friction U (inplace, param binding 2)
        static const std::string kShaderViscWallU = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool cell_active(int cx, int cy) { return d_at(cx,cy) > 3276; }
bool cell_near_wall(int cx, int cy) {
    if (!cell_active(cx,cy)) return false;
    if (cx > 0 && s_at(cx-1,cy)) return true;
    if (cx < GRID_N-1 && s_at(cx+1,cy)) return true;
    if (cy > 0 && s_at(cx,cy-1)) return true;
    if (cy < GRID_N-1 && s_at(cx,cy+1)) return true;
    return false;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    int uFx = u_at(x,y); int hits = 0;
    if (cell_near_wall(x-1,y)) hits++;
    if (cell_near_wall(x,y))   hits++;
    if (hits > 0) {
        int dampFx = 65536;
        for (int h = 0; h < hits; h++) dampFx = fx_mul(dampFx, param.p2);
        uFx = fx_mul(uFx, dampFx);
    }
    set_u(x,y,uFx);
}
)slg";

        // Wall friction V (inplace, param binding 2)
        static const std::string kShaderViscWallV = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool cell_active(int cx, int cy) { return d_at(cx,cy) > 3276; }
bool cell_near_wall(int cx, int cy) {
    if (!cell_active(cx,cy)) return false;
    if (cx > 0 && s_at(cx-1,cy)) return true;
    if (cx < GRID_N-1 && s_at(cx+1,cy)) return true;
    if (cy > 0 && s_at(cx,cy-1)) return true;
    if (cy < GRID_N-1 && s_at(cx,cy+1)) return true;
    return false;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    int vFx = v_at(x,y); int hits = 0;
    if (cell_near_wall(x,y-1)) hits++;
    if (cell_near_wall(x,y))   hits++;
    if (hits > 0) {
        int dampFx = 65536;
        for (int h = 0; h < hits; h++) dampFx = fx_mul(dampFx, param.p2);
        vFx = fx_mul(vFx, dampFx);
    }
    set_v(x,y,vFx);
}
)slg";

        // Extrapolate even (inplace, no param)
        static const std::string kShaderExtrapCellEven = kSlangCommonInplace + R"slg(
bool cell_fluid(int x, int y) { return d_at(x,y) > 13107; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 0) return;
    if (d_at(x,y) >= 6554) return;
    int sumU=0,sumV=0,cU=0,cV=0;
    if (x>0 && cell_fluid(x-1,y))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (x<GRID_N-1&&cell_fluid(x+1,y))  { sumU+=u_at(x+1,y); sumV+=v_at(x,y);   cU++;cV++; }
    if (y>0 && cell_fluid(x,y-1))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (y<GRID_N-1&&cell_fluid(x,y+1))  { sumU+=u_at(x,y);   sumV+=v_at(x,y+1); cU++;cV++; }
    if (cU>0 && cV>0) {
        int aU = sumU>=0?(sumU+(cU/2))/cU:(sumU-(cU/2))/cU;
        int aV = sumV>=0?(sumV+(cV/2))/cV:(sumV-(cV/2))/cV;
        set_u(x,y,aU); set_u(x+1,y,aU); set_v(x,y,aV); set_v(x,y+1,aV);
    }
}
)slg";

        // Extrapolate odd (inplace, no param)
        static const std::string kShaderExtrapCellOdd = kSlangCommonInplace + R"slg(
bool cell_fluid(int x, int y) { return d_at(x,y) > 13107; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 1) return;
    if (d_at(x,y) >= 6554) return;
    int sumU=0,sumV=0,cU=0,cV=0;
    if (x>0 && cell_fluid(x-1,y))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (x<GRID_N-1&&cell_fluid(x+1,y))  { sumU+=u_at(x+1,y); sumV+=v_at(x,y);   cU++;cV++; }
    if (y>0 && cell_fluid(x,y-1))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (y<GRID_N-1&&cell_fluid(x,y+1))  { sumU+=u_at(x,y);   sumV+=v_at(x,y+1); cU++;cV++; }
    if (cU>0 && cV>0) {
        int aU = sumU>=0?(sumU+(cU/2))/cU:(sumU-(cU/2))/cU;
        int aV = sumV>=0?(sumV+(cV/2))/cV:(sumV-(cV/2))/cV;
        set_u(x,y,aU); set_u(x+1,y,aU); set_v(x,y,aV); set_v(x,y+1,aV);
    }
}
)slg";

        // Advect U (output, param binding 3)
        static const std::string kShaderAdvectU = kSlangCommonOutput + R"slg(
struct Param { int dt,_p0,_p1,_p2; };
[[vk::binding(3,0)]] ConstantBuffer<Param> param;
int fx_lerp(int a, int b, int tFx) { return a + fx_mul(b-a, tFx); }
int sample_u(int xFx, int yFx) {
    xFx = clamp(xFx,0,GRID_N*65536); yFx = clamp(yFx,0,(GRID_N-1)*65536);
    int x0 = clamp(xFx>>16,0,GRID_N-1); int y0 = clamp((yFx-32768)>>16,0,GRID_N-2);
    int s = xFx-(x0<<16); int t2 = yFx-((y0<<16)+32768);
    return fx_lerp(fx_lerp(u_at(x0,y0),u_at(min(x0+1,GRID_N),y0),s),
                   fx_lerp(u_at(x0,y0+1),u_at(min(x0+1,GRID_N),y0+1),s),t2);
}
int sample_v(int xFx, int yFx) {
    xFx = clamp(xFx,0,(GRID_N-1)*65536); yFx = clamp(yFx,0,GRID_N*65536);
    int x0 = clamp((xFx-32768)>>16,0,GRID_N-2); int y0 = clamp(yFx>>16,0,GRID_N-1);
    int s = xFx-((x0<<16)+32768); int t2 = yFx-(y0<<16);
    return fx_lerp(fx_lerp(v_at(x0,y0),v_at(x0+1,y0),s),
                   fx_lerp(v_at(x0,min(y0+1,GRID_N)),v_at(x0+1,min(y0+1,GRID_N)),s),t2);
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    if (x <= 0 || x >= GRID_N) { out_write(0,x,y,0); return; }
    if (s_at(x,y) || s_at(x-1,y)) { out_write(0,x,y,0); return; }
    int hdt = param.dt/2;
    int xFx = x*65536; int yHFx = y*65536+32768;
    int uV = u_at(x,y); int vA = (v_at(x,y)+v_at(x-1,y)+v_at(x,y+1)+v_at(x-1,y+1))/4;
    int mxFx = xFx-fx_mul(uV,hdt); int myFx = yHFx-fx_mul(vA,hdt);
    int muFx = sample_u(mxFx,myFx); int mvFx = sample_v(mxFx,myFx);
    out_write(0,x,y, sample_u(xFx-fx_mul(muFx,param.dt), yHFx-fx_mul(mvFx,param.dt)));
}
)slg";

        // Advect V (output, param binding 3)
        static const std::string kShaderAdvectV = kSlangCommonOutput + R"slg(
struct Param { int dt,_p0,_p1,_p2; };
[[vk::binding(3,0)]] ConstantBuffer<Param> param;
int fx_lerp(int a, int b, int tFx) { return a + fx_mul(b-a, tFx); }
int sample_u(int xFx, int yFx) {
    xFx = clamp(xFx,0,GRID_N*65536); yFx = clamp(yFx,0,(GRID_N-1)*65536);
    int x0 = clamp(xFx>>16,0,GRID_N-1); int y0 = clamp((yFx-32768)>>16,0,GRID_N-2);
    int s = xFx-(x0<<16); int t2 = yFx-((y0<<16)+32768);
    return fx_lerp(fx_lerp(u_at(x0,y0),u_at(min(x0+1,GRID_N),y0),s),
                   fx_lerp(u_at(x0,y0+1),u_at(min(x0+1,GRID_N),y0+1),s),t2);
}
int sample_v(int xFx, int yFx) {
    xFx = clamp(xFx,0,(GRID_N-1)*65536); yFx = clamp(yFx,0,GRID_N*65536);
    int x0 = clamp((xFx-32768)>>16,0,GRID_N-2); int y0 = clamp(yFx>>16,0,GRID_N-1);
    int s = xFx-((x0<<16)+32768); int t2 = yFx-(y0<<16);
    return fx_lerp(fx_lerp(v_at(x0,y0),v_at(x0+1,y0),s),
                   fx_lerp(v_at(x0,min(y0+1,GRID_N)),v_at(x0+1,min(y0+1,GRID_N)),s),t2);
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    if (y <= 0 || y >= GRID_N) { out_write(0,x,y,0); return; }
    if (s_at(x,y) || s_at(x,y-1)) { out_write(0,x,y,0); return; }
    int hdt = param.dt/2;
    int xHFx = x*65536+32768; int yFx = y*65536;
    int vV = v_at(x,y); int uA = (u_at(x,y)+u_at(x,y-1)+u_at(x+1,y)+u_at(x+1,y-1))/4;
    int mxFx = xHFx-fx_mul(uA,hdt); int myFx = yFx-fx_mul(vV,hdt);
    int muFx = sample_u(mxFx,myFx); int mvFx = sample_v(mxFx,myFx);
    out_write(0,x,y, sample_v(xHFx-fx_mul(muFx,param.dt), yFx-fx_mul(mvFx,param.dt)));
}
)slg";

        // Density transport (output, param binding 3)
        static const std::string kShaderDensity = kSlangCommonOutput + R"slg(
static const int MAX_DENSITY_FX  = 88474;
static const int SOFT_START_FX   = 72090;
static const int SOFT_RANGE_FX   = 16384;
static const int SOFT_ZERO_FX    = 66;
static const int EXP1M_MAX_T_FX  = 524288;
static const int EXP1M_LUT_LAST  = 65514;
static const int EXP1M_LUT[33]   = {0,14497,25786,34579,41427,46760,50913,54148,56667,58629,60156,
    61346,62273,62995,63557,63995,64336,64601,64808,64969,65094,65192,
    65268,65327,65374,65409,65437,65459,65476,65489,65500,65508,65514};
struct Param { int dt,_p0,_p1,_p2; };
[[vk::binding(3,0)]] ConstantBuffer<Param> param;
int exp1m_fx(int tFx) {
    if (tFx<=0) return 0; if (tFx>=EXP1M_MAX_T_FX) return EXP1M_LUT_LAST;
    int idx=tFx>>14; int rem=tFx-(idx<<14);
    return EXP1M_LUT[idx]+fx_mul(EXP1M_LUT[idx+1]-EXP1M_LUT[idx],rem<<2);
}
int soft_bound(int dFx) {
    if (dFx<=0) return 0; if (dFx<=SOFT_START_FX) return dFx;
    int ov=dFx-SOFT_START_FX;
    return min(SOFT_START_FX+fx_mul(SOFT_RANGE_FX,exp1m_fx(ov<<2)),MAX_DENSITY_FX);
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x<0||x>=GRID_N||y<0||y>=GRID_N) return;
    if (s_at(x,y)) { out_write(0,x,y,d_at(x,y)); return; }
    int fL=0,fR=0,fT=0,fB=0; int dtFx=param.dt;
    if (x>0&&!s_at(x-1,y)) { int u=u_at(x,y); fL=fx_mul(fx_mul(d_at(u>0?x-1:x,y),u),dtFx); }
    if (x<GRID_N-1&&!s_at(x+1,y)) { int u=u_at(x+1,y); fR=fx_mul(fx_mul(d_at(u>0?x:x+1,y),u),dtFx); }
    if (y>0&&!s_at(x,y-1)) { int v=v_at(x,y); fT=fx_mul(fx_mul(d_at(x,v>0?y-1:y),v),dtFx); }
    if (y<GRID_N-1&&!s_at(x,y+1)) { int v=v_at(x,y+1); fB=fx_mul(fx_mul(d_at(x,v>0?y:y+1),v),dtFx); }
    int dFx=soft_bound(d_at(x,y)+(fL-fR)+(fT-fB));
    if (dFx<SOFT_ZERO_FX) dFx=0;
    out_write(0,x,y,dFx);
}
)slg";

        // Surface tension U (output, param binding 3)
        static const std::string kShaderSurfaceU = kSlangCommonOutput + R"slg(
static const int STRENGTH_FX  = 13107;
static const int D_MIN_FX     = 13107;
static const int D_MAX_FX     = 52429;
static const int GRAD_MIN_FX  = 6554;
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(3,0)]] ConstantBuffer<SimParam> param;
int contrib_fx(int cx, int cy) {
    if (cx<=0||cx>=GRID_N-1||cy<=0||cy>=GRID_N-1) return 0;
    int dFx=d_at(cx,cy); if (dFx<D_MIN_FX||dFx>D_MAX_FX) return 0;
    int nx=d_at(cx+1,cy)-d_at(cx-1,cy); if (abs(nx)<=GRAD_MIN_FX) return 0; return nx;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>GRID_N||y<0||y>=GRID_N) return;
    out_write(0,x,y, u_at(x,y)+fx_mul(fx_mul(contrib_fx(x,y)+contrib_fx(x-1,y),STRENGTH_FX),param.p0));
}
)slg";

        // Surface tension V (output, param binding 3)
        static const std::string kShaderSurfaceV = kSlangCommonOutput + R"slg(
static const int STRENGTH_FX  = 13107;
static const int D_MIN_FX     = 13107;
static const int D_MAX_FX     = 52429;
static const int GRAD_MIN_FX  = 6554;
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(3,0)]] ConstantBuffer<SimParam> param;
int contrib_fy(int cx, int cy) {
    if (cx<=0||cx>=GRID_N-1||cy<=0||cy>=GRID_N-1) return 0;
    int dFx=d_at(cx,cy); if (dFx<D_MIN_FX||dFx>D_MAX_FX) return 0;
    int ny=d_at(cx,cy+1)-d_at(cx,cy-1); if (abs(ny)<=GRAD_MIN_FX) return 0; return ny;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>=GRID_N||y<0||y>GRID_N) return;
    out_write(0,x,y, v_at(x,y)+fx_mul(fx_mul(contrib_fy(x,y)+contrib_fy(x,y-1),STRENGTH_FX),param.p0));
}
)slg";

        // Clamp U (output, NO param �?only 3 bindings)
        static const std::string kShaderClampU = kSlangCommonOutput + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>GRID_N||y<0||y>=GRID_N) return;
    int uFx=u_at(x,y);
    if (abs(uFx)>MAXV_FX) uFx = uFx>0?MAXV_FX:-MAXV_FX;
    out_write(0,x,y,uFx);
}
)slg";

        // Clamp V (output, NO param �?only 3 bindings)
        static const std::string kShaderClampV = kSlangCommonOutput + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>=GRID_N||y<0||y>GRID_N) return;
    int vFx=v_at(x,y);
    if (abs(vFx)>MAXV_FX) vFx = vFx>0?MAXV_FX:-MAXV_FX;
    out_write(0,x,y,vFx);
}
)slg";
        // ------- Buffer creation -------
        const auto mk_buf = [&](std::string_view label, std::size_t size, wgpu::BufferUsage usage) {
            return device.createBuffer(wgpu::BufferDescriptor().setLabel(label).setSize(size).setUsage(usage));
        };

        chunk_data_buf =
            mk_buf("liquid_chunk_data", kGpuChunkDataBytes,
                   wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eCopySrc);
        chunk_u_out_buf = mk_buf("liquid_chunk_u_out", kGpuFieldSectionBytes,
                                 wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc);
        chunk_v_out_buf = mk_buf("liquid_chunk_v_out", kGpuFieldSectionBytes,
                                 wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc);
        chunk_d_out_buf = mk_buf("liquid_chunk_d_out", kGpuFieldSectionBytes,
                                 wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc);
        param_buf = mk_buf("liquid_param", kParamBytes, wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst);
        readback_buf =
            mk_buf("liquid_readback", kGpuChunkDataBytes, wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst);

        // ------- Build chunk-position SVO (all kChunksX*kChunksY chunks present) -------
        {
            epix::ext::grid::packed_grid<2, int> chunk_pos_grid{
                {static_cast<std::uint32_t>(kChunksX), static_cast<std::uint32_t>(kChunksY)}, 0};
            auto svo_result = svo_upload(chunk_pos_grid, SvoConfig{.child_count = 2});
            svo_chunk_pos   = std::move(svo_result.value());
        }
        chunk_svo_buf = mk_buf("liquid_chunk_svo", svo_chunk_pos.byte_size(),
                               wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst);

        buffers_ready_ = true;
    }

    // Registers compute shaders and queues GPU pipelines via the embedded asset system.
    // Call once from Plugin::finish() before inserting FluidState into the world.
    void register_pipelines(core::World& world) {
        if (queued_) return;

        auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = world.get_resource<assets::AssetServer>();
        auto& ps      = world.resource_mut<render::PipelineServer>();
        if (!registry || !server) {
            return;  // Resources not yet available; called too early or missing plugins
        }

        const auto bytes = [](std::string_view s) {
            return std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size());
        };

        // Register the SVO grid library (resolves "import epix.ext.grid.svo;")
        registry->get().insert_asset_static("epix/shaders/grid/svo.slang", bytes(kSvoGridSlangSource));
        svo_library_handle_ = server->get().load<shader::Shader>("embedded://epix/shaders/grid/svo.slang");

        // ------- Slang shader sources -------

        // Common preamble: inplace (bindings 0=chunk_data RW, 1=chunk_svo)
        static const std::string kSlangCommonInplace = R"slg(
import epix.ext.grid.svo;
[[vk::binding(0,0)]] RWStructuredBuffer<int>  chunk_data;
[[vk::binding(1,0)]] StructuredBuffer<uint>   chunk_svo;
static const int CHUNK_SIZE         = 16;
static const int CHUNK_CELLS        = 256;
static const int CHUNKS_X           = 13;
static const int CHUNKS_Y           = 13;
static const int GRID_N             = 200;
static const int D_BASE             = 0;
static const int S_BASE             = 43264;
static const int P_BASE             = 86528;
static const int U_BASE             = 129792;
static const int V_BASE             = 173056;
static const int MAXV_FX            = 524288;
static const int OMEGA_FX           = 117965;
static const int TARGET_DIV_GAIN_FX = 6554;
int chunk_idx(int cx, int cy) {
    if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) return -1;
    epix::ext::grid::SvoGrid2D g = epix::ext::grid::SvoGrid2D(chunk_svo);
    return g.lookup(int[2](cx, cy));
}
int fx_mul(int a, int b) {
    int ah = a >> 16; int bh = b >> 16; int al = a & 65535; int bl = b & 65535;
    int hi = (ah * bh) << 16; int mid = ah * bl + al * bh;
    int lo = int((uint(al) * uint(bl)) >> 16u);
    return hi + mid + lo;
}
int d_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return 0;
    return chunk_data[D_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
bool s_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return true;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return true;
    return chunk_data[S_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] != 0;
}
int p_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return 0;
    return chunk_data[P_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
int u_at(int gx, int gy) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[U_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
int v_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy > GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[V_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
void set_d(int gx, int gy, int val) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return;
    chunk_data[D_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
void set_p(int gx, int gy, int val) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return;
    chunk_data[P_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
void set_u(int gx, int gy, int val) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy >= GRID_N) return;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return;
    int c = chunk_idx(cx2, cy2); if (c < 0) return;
    chunk_data[U_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
void set_v(int gx, int gy, int val) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy > GRID_N) return;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return;
    int c = chunk_idx(cx2, cy2); if (c < 0) return;
    chunk_data[V_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
)slg";

        // Common preamble: output (bindings 0=chunk_data RO, 1=chunk_out RW, 2=chunk_svo)
        static const std::string kSlangCommonOutput = R"slg(
import epix.ext.grid.svo;
[[vk::binding(0,0)]] StructuredBuffer<int>    chunk_data;
[[vk::binding(1,0)]] RWStructuredBuffer<int>  chunk_out;
[[vk::binding(2,0)]] StructuredBuffer<uint>   chunk_svo;
static const int CHUNK_SIZE  = 16;
static const int CHUNK_CELLS = 256;
static const int CHUNKS_X    = 13;
static const int CHUNKS_Y    = 13;
static const int GRID_N      = 200;
static const int D_BASE      = 0;
static const int S_BASE      = 43264;
static const int P_BASE      = 86528;
static const int U_BASE      = 129792;
static const int V_BASE      = 173056;
static const int MAXV_FX     = 524288;
int chunk_idx(int cx, int cy) {
    if (cx < 0 || cx >= CHUNKS_X || cy < 0 || cy >= CHUNKS_Y) return -1;
    epix::ext::grid::SvoGrid2D g = epix::ext::grid::SvoGrid2D(chunk_svo);
    return g.lookup(int[2](cx, cy));
}
int fx_mul(int a, int b) {
    int ah = a >> 16; int bh = b >> 16; int al = a & 65535; int bl = b & 65535;
    int hi = (ah * bh) << 16; int mid = ah * bl + al * bh;
    int lo = int((uint(al) * uint(bl)) >> 16u);
    return hi + mid + lo;
}
int d_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return 0;
    return chunk_data[D_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
bool s_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy >= GRID_N) return true;
    int c = chunk_idx(gx / CHUNK_SIZE, gy / CHUNK_SIZE); if (c < 0) return true;
    return chunk_data[S_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] != 0;
}
int u_at(int gx, int gy) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy >= GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[U_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
int v_at(int gx, int gy) {
    if (gx < 0 || gx >= GRID_N || gy < 0 || gy > GRID_N) return 0;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return 0;
    int c = chunk_idx(cx2, cy2); if (c < 0) return 0;
    return chunk_data[V_BASE + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE];
}
void out_write(int base, int gx, int gy, int val) {
    if (gx < 0 || gx > GRID_N || gy < 0 || gy > GRID_N) return;
    int cx2 = gx / CHUNK_SIZE; int cy2 = gy / CHUNK_SIZE;
    if (cx2 >= CHUNKS_X || cy2 >= CHUNKS_Y) return;
    int c = chunk_idx(cx2, cy2); if (c < 0) return;
    chunk_out[base + c * CHUNK_CELLS + (gx % CHUNK_SIZE) + (gy % CHUNK_SIZE) * CHUNK_SIZE] = val;
}
)slg";

        static const std::string kShaderEven = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x <= 0 || y <= 0 || x >= GRID_N-1 || y >= GRID_N-1) return;
    if (((x+y)&1) != 0) return;
    int dFx = d_at(x,y); if (dFx < 13107) return;
    int uLFx = u_at(x,y); int uRFx = u_at(x+1,y);
    int vTFx = v_at(x,y); int vBFx = v_at(x,y+1);
    int velDivFx = (uRFx-uLFx) + (vBFx-vTFx);
    int densityErrFx = dFx - 65536;
    int targetDivFx = densityErrFx > 0 ? fx_mul(densityErrFx, TARGET_DIV_GAIN_FX) : 0;
    int totalDivFx = velDivFx - targetDivFx;
    int sL = s_at(x-1,y)?1:0; int sR = s_at(x+1,y)?1:0;
    int sT = s_at(x,y-1)?1:0; int sB = s_at(x,y+1)?1:0;
    int n = 4 - (sL+sR+sT+sB); if (n == 0) return;
    int pCorrFx = int(-totalDivFx / n);
    pCorrFx = fx_mul(pCorrFx, OMEGA_FX);
    int weightFx = dFx >= 65536 ? 65536 : fx_mul(dFx, dFx);
    pCorrFx = fx_mul(pCorrFx, weightFx);
    set_p(x,y, p_at(x,y)+pCorrFx);
    if (sL==0) set_u(x,y, uLFx-pCorrFx);
    if (sR==0) set_u(x+1,y, uRFx+pCorrFx);
    if (sT==0) set_v(x,y, vTFx-pCorrFx);
    if (sB==0) set_v(x,y+1, vBFx+pCorrFx);
}
)slg";

        static const std::string kShaderOdd = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x <= 0 || y <= 0 || x >= GRID_N-1 || y >= GRID_N-1) return;
    if (((x+y)&1) != 1) return;
    int dFx = d_at(x,y); if (dFx < 13107) return;
    int uLFx = u_at(x,y); int uRFx = u_at(x+1,y);
    int vTFx = v_at(x,y); int vBFx = v_at(x,y+1);
    int velDivFx = (uRFx-uLFx) + (vBFx-vTFx);
    int densityErrFx = dFx - 65536;
    int targetDivFx = densityErrFx > 0 ? fx_mul(densityErrFx, TARGET_DIV_GAIN_FX) : 0;
    int totalDivFx = velDivFx - targetDivFx;
    int sL = s_at(x-1,y)?1:0; int sR = s_at(x+1,y)?1:0;
    int sT = s_at(x,y-1)?1:0; int sB = s_at(x,y+1)?1:0;
    int n = 4 - (sL+sR+sT+sB); if (n == 0) return;
    int pCorrFx = int(-totalDivFx / n);
    pCorrFx = fx_mul(pCorrFx, OMEGA_FX);
    int weightFx = dFx >= 65536 ? 65536 : fx_mul(dFx, dFx);
    pCorrFx = fx_mul(pCorrFx, weightFx);
    set_p(x,y, p_at(x,y)+pCorrFx);
    if (sL==0) set_u(x,y, uLFx-pCorrFx);
    if (sR==0) set_u(x+1,y, uRFx+pCorrFx);
    if (sT==0) set_v(x,y, vTFx-pCorrFx);
    if (sB==0) set_v(x,y+1, vBFx+pCorrFx);
}
)slg";

        static const std::string kShaderGravity = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y <= 0 || y >= GRID_N) return;
    int d0 = d_at(x,y); int d1 = d_at(x,y-1);
    if (d0 > 655 || d1 > 655) set_v(x,y, v_at(x,y) + param.p0/2);
}
)slg";

        static const std::string kShaderPostU = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    int uFx = u_at(x,y);
    if (abs(uFx) > MAXV_FX) uFx = uFx > 0 ? MAXV_FX : -MAXV_FX;
    bool sL = x == 0 || s_at(x-1,y); bool sR = x == GRID_N || s_at(x,y);
    if (sL || sR) uFx = 0;
    set_u(x,y,uFx);
}
)slg";

        static const std::string kShaderPostV = kSlangCommonInplace + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    int vFx = v_at(x,y);
    if (abs(vFx) > MAXV_FX) vFx = vFx > 0 ? MAXV_FX : -MAXV_FX;
    bool sT = y == 0 || s_at(x,y-1); bool sB = y == GRID_N || s_at(x,y);
    if (sT || sB) vFx = 0;
    set_v(x,y,vFx);
}
)slg";

        static const std::string kShaderViscUEven = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_u(int x, int y) { return max(d_at(x-1,y), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 0) return;
    int uFx = u_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (x > 0 && x < GRID_N && y > 0 && y < GRID_N-1 && face_active_u(x,y)) {
        int nFx = u_at(x-1,y)+u_at(x+1,y)+u_at(x,y-1)+u_at(x,y+1);
        uFx = uFx + fx_mul(nFx - 4*uFx, scaleFx);
    }
    set_u(x,y,uFx);
}
)slg";

        static const std::string kShaderViscUOdd = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_u(int x, int y) { return max(d_at(x-1,y), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 1) return;
    int uFx = u_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (x > 0 && x < GRID_N && y > 0 && y < GRID_N-1 && face_active_u(x,y)) {
        int nFx = u_at(x-1,y)+u_at(x+1,y)+u_at(x,y-1)+u_at(x,y+1);
        uFx = uFx + fx_mul(nFx - 4*uFx, scaleFx);
    }
    set_u(x,y,uFx);
}
)slg";

        static const std::string kShaderViscVEven = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_v(int x, int y) { return max(d_at(x,y-1), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    if (((x+y)&1) != 0) return;
    int vFx = v_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (y > 0 && y < GRID_N && x > 0 && x < GRID_N-1 && face_active_v(x,y)) {
        int nFx = v_at(x-1,y)+v_at(x+1,y)+v_at(x,y-1)+v_at(x,y+1);
        vFx = vFx + fx_mul(nFx - 4*vFx, scaleFx);
    }
    set_v(x,y,vFx);
}
)slg";

        static const std::string kShaderViscVOdd = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool face_active_v(int x, int y) { return max(d_at(x,y-1), d_at(x,y)) > 3276; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    if (((x+y)&1) != 1) return;
    int vFx = v_at(x,y);
    int scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (y > 0 && y < GRID_N && x > 0 && x < GRID_N-1 && face_active_v(x,y)) {
        int nFx = v_at(x-1,y)+v_at(x+1,y)+v_at(x,y-1)+v_at(x,y+1);
        vFx = vFx + fx_mul(nFx - 4*vFx, scaleFx);
    }
    set_v(x,y,vFx);
}
)slg";

        static const std::string kShaderViscWallU = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool cell_active(int cx, int cy) { return d_at(cx,cy) > 3276; }
bool cell_near_wall(int cx, int cy) {
    if (!cell_active(cx,cy)) return false;
    if (cx > 0 && s_at(cx-1,cy)) return true;
    if (cx < GRID_N-1 && s_at(cx+1,cy)) return true;
    if (cy > 0 && s_at(cx,cy-1)) return true;
    if (cy < GRID_N-1 && s_at(cx,cy+1)) return true;
    return false;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    int uFx = u_at(x,y); int hits = 0;
    if (cell_near_wall(x-1,y)) hits++;
    if (cell_near_wall(x,y))   hits++;
    if (hits > 0) {
        int dampFx = 65536;
        for (int h = 0; h < hits; h++) dampFx = fx_mul(dampFx, param.p2);
        uFx = fx_mul(uFx, dampFx);
    }
    set_u(x,y,uFx);
}
)slg";

        static const std::string kShaderViscWallV = kSlangCommonInplace + R"slg(
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(2,0)]] ConstantBuffer<SimParam> param;
bool cell_active(int cx, int cy) { return d_at(cx,cy) > 3276; }
bool cell_near_wall(int cx, int cy) {
    if (!cell_active(cx,cy)) return false;
    if (cx > 0 && s_at(cx-1,cy)) return true;
    if (cx < GRID_N-1 && s_at(cx+1,cy)) return true;
    if (cy > 0 && s_at(cx,cy-1)) return true;
    if (cy < GRID_N-1 && s_at(cx,cy+1)) return true;
    return false;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    int vFx = v_at(x,y); int hits = 0;
    if (cell_near_wall(x,y-1)) hits++;
    if (cell_near_wall(x,y))   hits++;
    if (hits > 0) {
        int dampFx = 65536;
        for (int h = 0; h < hits; h++) dampFx = fx_mul(dampFx, param.p2);
        vFx = fx_mul(vFx, dampFx);
    }
    set_v(x,y,vFx);
}
)slg";

        static const std::string kShaderExtrapCellEven = kSlangCommonInplace + R"slg(
bool cell_fluid(int x, int y) { return d_at(x,y) > 13107; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 0) return;
    if (d_at(x,y) >= 6554) return;
    int sumU=0,sumV=0,cU=0,cV=0;
    if (x>0 && cell_fluid(x-1,y))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (x<GRID_N-1&&cell_fluid(x+1,y))  { sumU+=u_at(x+1,y); sumV+=v_at(x,y);   cU++;cV++; }
    if (y>0 && cell_fluid(x,y-1))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (y<GRID_N-1&&cell_fluid(x,y+1))  { sumU+=u_at(x,y);   sumV+=v_at(x,y+1); cU++;cV++; }
    if (cU>0 && cV>0) {
        int aU = sumU>=0?(sumU+(cU/2))/cU:(sumU-(cU/2))/cU;
        int aV = sumV>=0?(sumV+(cV/2))/cV:(sumV-(cV/2))/cV;
        set_u(x,y,aU); set_u(x+1,y,aU); set_v(x,y,aV); set_v(x,y+1,aV);
    }
}
)slg";

        static const std::string kShaderExtrapCellOdd = kSlangCommonInplace + R"slg(
bool cell_fluid(int x, int y) { return d_at(x,y) > 13107; }
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y >= GRID_N) return;
    if (((x+y)&1) != 1) return;
    if (d_at(x,y) >= 6554) return;
    int sumU=0,sumV=0,cU=0,cV=0;
    if (x>0 && cell_fluid(x-1,y))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (x<GRID_N-1&&cell_fluid(x+1,y))  { sumU+=u_at(x+1,y); sumV+=v_at(x,y);   cU++;cV++; }
    if (y>0 && cell_fluid(x,y-1))       { sumU+=u_at(x,y);   sumV+=v_at(x,y);   cU++;cV++; }
    if (y<GRID_N-1&&cell_fluid(x,y+1))  { sumU+=u_at(x,y);   sumV+=v_at(x,y+1); cU++;cV++; }
    if (cU>0 && cV>0) {
        int aU = sumU>=0?(sumU+(cU/2))/cU:(sumU-(cU/2))/cU;
        int aV = sumV>=0?(sumV+(cV/2))/cV:(sumV-(cV/2))/cV;
        set_u(x,y,aU); set_u(x+1,y,aU); set_v(x,y,aV); set_v(x,y+1,aV);
    }
}
)slg";

        static const std::string kShaderAdvectU = kSlangCommonOutput + R"slg(
struct Param { int dt,_p0,_p1,_p2; };
[[vk::binding(3,0)]] ConstantBuffer<Param> param;
int fx_lerp(int a, int b, int tFx) { return a + fx_mul(b-a, tFx); }
int sample_u(int xFx, int yFx) {
    xFx = clamp(xFx,0,GRID_N*65536); yFx = clamp(yFx,0,(GRID_N-1)*65536);
    int x0 = clamp(xFx>>16,0,GRID_N-1); int y0 = clamp((yFx-32768)>>16,0,GRID_N-2);
    int s = xFx-(x0<<16); int t2 = yFx-((y0<<16)+32768);
    return fx_lerp(fx_lerp(u_at(x0,y0),u_at(min(x0+1,GRID_N),y0),s),
                   fx_lerp(u_at(x0,y0+1),u_at(min(x0+1,GRID_N),y0+1),s),t2);
}
int sample_v(int xFx, int yFx) {
    xFx = clamp(xFx,0,(GRID_N-1)*65536); yFx = clamp(yFx,0,GRID_N*65536);
    int x0 = clamp((xFx-32768)>>16,0,GRID_N-2); int y0 = clamp(yFx>>16,0,GRID_N-1);
    int s = xFx-((x0<<16)+32768); int t2 = yFx-(y0<<16);
    return fx_lerp(fx_lerp(v_at(x0,y0),v_at(x0+1,y0),s),
                   fx_lerp(v_at(x0,min(y0+1,GRID_N)),v_at(x0+1,min(y0+1,GRID_N)),s),t2);
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x > GRID_N || y < 0 || y >= GRID_N) return;
    if (x <= 0 || x >= GRID_N) { out_write(0,x,y,0); return; }
    if (s_at(x,y) || s_at(x-1,y)) { out_write(0,x,y,0); return; }
    int hdt = param.dt/2;
    int xFx = x*65536; int yHFx = y*65536+32768;
    int uV = u_at(x,y); int vA = (v_at(x,y)+v_at(x-1,y)+v_at(x,y+1)+v_at(x-1,y+1))/4;
    int mxFx = xFx-fx_mul(uV,hdt); int myFx = yHFx-fx_mul(vA,hdt);
    int muFx = sample_u(mxFx,myFx); int mvFx = sample_v(mxFx,myFx);
    out_write(0,x,y, sample_u(xFx-fx_mul(muFx,param.dt), yHFx-fx_mul(mvFx,param.dt)));
}
)slg";

        static const std::string kShaderAdvectV = kSlangCommonOutput + R"slg(
struct Param { int dt,_p0,_p1,_p2; };
[[vk::binding(3,0)]] ConstantBuffer<Param> param;
int fx_lerp(int a, int b, int tFx) { return a + fx_mul(b-a, tFx); }
int sample_u(int xFx, int yFx) {
    xFx = clamp(xFx,0,GRID_N*65536); yFx = clamp(yFx,0,(GRID_N-1)*65536);
    int x0 = clamp(xFx>>16,0,GRID_N-1); int y0 = clamp((yFx-32768)>>16,0,GRID_N-2);
    int s = xFx-(x0<<16); int t2 = yFx-((y0<<16)+32768);
    return fx_lerp(fx_lerp(u_at(x0,y0),u_at(min(x0+1,GRID_N),y0),s),
                   fx_lerp(u_at(x0,y0+1),u_at(min(x0+1,GRID_N),y0+1),s),t2);
}
int sample_v(int xFx, int yFx) {
    xFx = clamp(xFx,0,(GRID_N-1)*65536); yFx = clamp(yFx,0,GRID_N*65536);
    int x0 = clamp((xFx-32768)>>16,0,GRID_N-2); int y0 = clamp(yFx>>16,0,GRID_N-1);
    int s = xFx-((x0<<16)+32768); int t2 = yFx-(y0<<16);
    return fx_lerp(fx_lerp(v_at(x0,y0),v_at(x0+1,y0),s),
                   fx_lerp(v_at(x0,min(y0+1,GRID_N)),v_at(x0+1,min(y0+1,GRID_N)),s),t2);
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x < 0 || x >= GRID_N || y < 0 || y > GRID_N) return;
    if (y <= 0 || y >= GRID_N) { out_write(0,x,y,0); return; }
    if (s_at(x,y) || s_at(x,y-1)) { out_write(0,x,y,0); return; }
    int hdt = param.dt/2;
    int xHFx = x*65536+32768; int yFx = y*65536;
    int vV = v_at(x,y); int uA = (u_at(x,y)+u_at(x,y-1)+u_at(x+1,y)+u_at(x+1,y-1))/4;
    int mxFx = xHFx-fx_mul(uA,hdt); int myFx = yFx-fx_mul(vV,hdt);
    int muFx = sample_u(mxFx,myFx); int mvFx = sample_v(mxFx,myFx);
    out_write(0,x,y, sample_v(xHFx-fx_mul(muFx,param.dt), yFx-fx_mul(mvFx,param.dt)));
}
)slg";

        static const std::string kShaderDensity = kSlangCommonOutput + R"slg(
static const int MAX_DENSITY_FX  = 88474;
static const int SOFT_START_FX   = 72090;
static const int SOFT_RANGE_FX   = 16384;
static const int SOFT_ZERO_FX    = 66;
static const int EXP1M_MAX_T_FX  = 524288;
static const int EXP1M_LUT_LAST  = 65514;
static const int EXP1M_LUT[33]   = {0,14497,25786,34579,41427,46760,50913,54148,56667,58629,60156,
    61346,62273,62995,63557,63995,64336,64601,64808,64969,65094,65192,
    65268,65327,65374,65409,65437,65459,65476,65489,65500,65508,65514};
struct Param { int dt,_p0,_p1,_p2; };
[[vk::binding(3,0)]] ConstantBuffer<Param> param;
int exp1m_fx(int tFx) {
    if (tFx<=0) return 0; if (tFx>=EXP1M_MAX_T_FX) return EXP1M_LUT_LAST;
    int idx=tFx>>14; int rem=tFx-(idx<<14);
    return EXP1M_LUT[idx]+fx_mul(EXP1M_LUT[idx+1]-EXP1M_LUT[idx],rem<<2);
}
int soft_bound(int dFx) {
    if (dFx<=0) return 0; if (dFx<=SOFT_START_FX) return dFx;
    int ov=dFx-SOFT_START_FX;
    return min(SOFT_START_FX+fx_mul(SOFT_RANGE_FX,exp1m_fx(ov<<2)),MAX_DENSITY_FX);
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x = int(gid.x); int y = int(gid.y);
    if (x<0||x>=GRID_N||y<0||y>=GRID_N) return;
    if (s_at(x,y)) { out_write(0,x,y,d_at(x,y)); return; }
    int fL=0,fR=0,fT=0,fB=0; int dtFx=param.dt;
    if (x>0&&!s_at(x-1,y)) { int u=u_at(x,y); fL=fx_mul(fx_mul(d_at(u>0?x-1:x,y),u),dtFx); }
    if (x<GRID_N-1&&!s_at(x+1,y)) { int u=u_at(x+1,y); fR=fx_mul(fx_mul(d_at(u>0?x:x+1,y),u),dtFx); }
    if (y>0&&!s_at(x,y-1)) { int v=v_at(x,y); fT=fx_mul(fx_mul(d_at(x,v>0?y-1:y),v),dtFx); }
    if (y<GRID_N-1&&!s_at(x,y+1)) { int v=v_at(x,y+1); fB=fx_mul(fx_mul(d_at(x,v>0?y:y+1),v),dtFx); }
    int dFx=soft_bound(d_at(x,y)+(fL-fR)+(fT-fB));
    if (dFx<SOFT_ZERO_FX) dFx=0;
    out_write(0,x,y,dFx);
}
)slg";

        static const std::string kShaderSurfaceU = kSlangCommonOutput + R"slg(
static const int STRENGTH_FX  = 13107;
static const int D_MIN_FX     = 13107;
static const int D_MAX_FX     = 52429;
static const int GRAD_MIN_FX  = 6554;
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(3,0)]] ConstantBuffer<SimParam> param;
int contrib_fx(int cx, int cy) {
    if (cx<=0||cx>=GRID_N-1||cy<=0||cy>=GRID_N-1) return 0;
    int dFx=d_at(cx,cy); if (dFx<D_MIN_FX||dFx>D_MAX_FX) return 0;
    int nx=d_at(cx+1,cy)-d_at(cx-1,cy); if (abs(nx)<=GRAD_MIN_FX) return 0; return nx;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>GRID_N||y<0||y>=GRID_N) return;
    out_write(0,x,y, u_at(x,y)+fx_mul(fx_mul(contrib_fx(x,y)+contrib_fx(x-1,y),STRENGTH_FX),param.p0));
}
)slg";

        static const std::string kShaderSurfaceV = kSlangCommonOutput + R"slg(
static const int STRENGTH_FX  = 13107;
static const int D_MIN_FX     = 13107;
static const int D_MAX_FX     = 52429;
static const int GRAD_MIN_FX  = 6554;
struct SimParam { int p0,p1,p2,p3,p4,p5,p6,p7; };
[[vk::binding(3,0)]] ConstantBuffer<SimParam> param;
int contrib_fy(int cx, int cy) {
    if (cx<=0||cx>=GRID_N-1||cy<=0||cy>=GRID_N-1) return 0;
    int dFx=d_at(cx,cy); if (dFx<D_MIN_FX||dFx>D_MAX_FX) return 0;
    int ny=d_at(cx,cy+1)-d_at(cx,cy-1); if (abs(ny)<=GRAD_MIN_FX) return 0; return ny;
}
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>=GRID_N||y<0||y>GRID_N) return;
    out_write(0,x,y, v_at(x,y)+fx_mul(fx_mul(contrib_fy(x,y)+contrib_fy(x,y-1),STRENGTH_FX),param.p0));
}
)slg";

        static const std::string kShaderClampU = kSlangCommonOutput + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>GRID_N||y<0||y>=GRID_N) return;
    int uFx=u_at(x,y);
    if (abs(uFx)>MAXV_FX) uFx = uFx>0?MAXV_FX:-MAXV_FX;
    out_write(0,x,y,uFx);
}
)slg";

        static const std::string kShaderClampV = kSlangCommonOutput + R"slg(
[shader("compute")]
[numthreads(16,16,1)]
void computeMain(uint3 gid : SV_DispatchThreadID) {
    int x=int(gid.x); int y=int(gid.y);
    if (x<0||x>=GRID_N||y<0||y>GRID_N) return;
    int vFx=v_at(x,y);
    if (abs(vFx)>MAXV_FX) vFx = vFx>0?MAXV_FX:-MAXV_FX;
    out_write(0,x,y,vFx);
}
)slg";

        // Register + load + queue each compute pipeline
        const auto queue_one = [&](std::string_view path, const std::string& src, const char* label) {
            registry->get().insert_asset_static(path, bytes(src));
            auto handle = server->get().load<shader::Shader>(std::string("embedded://") + std::string(path));
            return ps.queue_compute_pipeline(render::ComputePipelineDescriptor{
                .label       = std::string(label),
                .shader      = std::move(handle),
                .entry_point = std::string("computeMain"),
            });
        };

        pipeline_even_id        = queue_one("liquid/pressure_even.slang", kShaderEven, "LiquidPressureProjectEven");
        pipeline_odd_id         = queue_one("liquid/pressure_odd.slang", kShaderOdd, "LiquidPressureProjectOdd");
        gravity_pipeline_id     = queue_one("liquid/gravity.slang", kShaderGravity, "LiquidGravity");
        post_u_pipeline_id      = queue_one("liquid/post_u.slang", kShaderPostU, "LiquidPostU");
        post_v_pipeline_id      = queue_one("liquid/post_v.slang", kShaderPostV, "LiquidPostV");
        visc_u_even_pipeline_id = queue_one("liquid/visc_u_even.slang", kShaderViscUEven, "LiquidViscosityUEven");
        visc_u_odd_pipeline_id  = queue_one("liquid/visc_u_odd.slang", kShaderViscUOdd, "LiquidViscosityUOdd");
        visc_v_even_pipeline_id = queue_one("liquid/visc_v_even.slang", kShaderViscVEven, "LiquidViscosityVEven");
        visc_v_odd_pipeline_id  = queue_one("liquid/visc_v_odd.slang", kShaderViscVOdd, "LiquidViscosityVOdd");
        visc_wall_u_pipeline_id = queue_one("liquid/visc_wall_u.slang", kShaderViscWallU, "LiquidViscosityWallU");
        visc_wall_v_pipeline_id = queue_one("liquid/visc_wall_v.slang", kShaderViscWallV, "LiquidViscosityWallV");
        extrap_cell_even_pipeline_id =
            queue_one("liquid/extrap_even.slang", kShaderExtrapCellEven, "LiquidExtrapolateCellEven");
        extrap_cell_odd_pipeline_id =
            queue_one("liquid/extrap_odd.slang", kShaderExtrapCellOdd, "LiquidExtrapolateCellOdd");
        advect_u_pipeline_id  = queue_one("liquid/advect_u.slang", kShaderAdvectU, "LiquidAdvectU");
        advect_v_pipeline_id  = queue_one("liquid/advect_v.slang", kShaderAdvectV, "LiquidAdvectV");
        density_pipeline_id   = queue_one("liquid/density.slang", kShaderDensity, "LiquidDensityTransport");
        surface_u_pipeline_id = queue_one("liquid/surface_u.slang", kShaderSurfaceU, "LiquidSurfaceU");
        surface_v_pipeline_id = queue_one("liquid/surface_v.slang", kShaderSurfaceV, "LiquidSurfaceV");
        clamp_u_pipeline_id   = queue_one("liquid/clamp_u.slang", kShaderClampU, "LiquidClampU");
        clamp_v_pipeline_id   = queue_one("liquid/clamp_v.slang", kShaderClampV, "LiquidClampV");

        queued_ = true;
    }

    // Called each frame until ready; creates bind groups once all async pipelines are available.
    bool ensure_ready(const wgpu::Device& device, render::PipelineServer& ps) {
        if (!queued_ || ready) return ready;

        const auto get_cp = [&](render::CachedPipelineId id) -> const wgpu::ComputePipeline* {
            auto result = ps.get_compute_pipeline(id);
            if (!result.has_value()) return nullptr;
            return &result->get().pipeline();
        };

        const auto* p_even             = get_cp(pipeline_even_id);
        const auto* p_odd              = get_cp(pipeline_odd_id);
        const auto* p_gravity          = get_cp(gravity_pipeline_id);
        const auto* p_post_u           = get_cp(post_u_pipeline_id);
        const auto* p_post_v           = get_cp(post_v_pipeline_id);
        const auto* p_visc_u_even      = get_cp(visc_u_even_pipeline_id);
        const auto* p_visc_u_odd       = get_cp(visc_u_odd_pipeline_id);
        const auto* p_visc_v_even      = get_cp(visc_v_even_pipeline_id);
        const auto* p_visc_v_odd       = get_cp(visc_v_odd_pipeline_id);
        const auto* p_visc_wall_u      = get_cp(visc_wall_u_pipeline_id);
        const auto* p_visc_wall_v      = get_cp(visc_wall_v_pipeline_id);
        const auto* p_extrap_cell_even = get_cp(extrap_cell_even_pipeline_id);
        const auto* p_extrap_cell_odd  = get_cp(extrap_cell_odd_pipeline_id);
        const auto* p_advect_u         = get_cp(advect_u_pipeline_id);
        const auto* p_advect_v         = get_cp(advect_v_pipeline_id);
        const auto* p_density          = get_cp(density_pipeline_id);
        const auto* p_surface_u        = get_cp(surface_u_pipeline_id);
        const auto* p_surface_v        = get_cp(surface_v_pipeline_id);
        const auto* p_clamp_u          = get_cp(clamp_u_pipeline_id);
        const auto* p_clamp_v          = get_cp(clamp_v_pipeline_id);

        if (!p_even || !p_odd || !p_gravity || !p_post_u || !p_post_v || !p_visc_u_even || !p_visc_u_odd ||
            !p_visc_v_even || !p_visc_v_odd || !p_visc_wall_u || !p_visc_wall_v || !p_extrap_cell_even ||
            !p_extrap_cell_odd || !p_advect_u || !p_advect_v || !p_density || !p_surface_u || !p_surface_v ||
            !p_clamp_u || !p_clamp_v)
            return false;

        // Fill wgpu::ComputePipeline members
        pipeline_even             = *p_even;
        pipeline_odd              = *p_odd;
        gravity_pipeline          = *p_gravity;
        post_u_pipeline           = *p_post_u;
        post_v_pipeline           = *p_post_v;
        visc_u_even_pipeline      = *p_visc_u_even;
        visc_u_odd_pipeline       = *p_visc_u_odd;
        visc_v_even_pipeline      = *p_visc_v_even;
        visc_v_odd_pipeline       = *p_visc_v_odd;
        visc_wall_u_pipeline      = *p_visc_wall_u;
        visc_wall_v_pipeline      = *p_visc_wall_v;
        extrap_cell_even_pipeline = *p_extrap_cell_even;
        extrap_cell_odd_pipeline  = *p_extrap_cell_odd;
        advect_u_pipeline         = *p_advect_u;
        advect_v_pipeline         = *p_advect_v;
        density_pipeline          = *p_density;
        surface_u_pipeline        = *p_surface_u;
        surface_v_pipeline        = *p_surface_v;
        clamp_u_pipeline          = *p_clamp_u;
        clamp_v_pipeline          = *p_clamp_v;

        // ------- Bind group creation -------
        // In-place, no param: {chunk_data(0,rw), chunk_svo(1,rd)}
        const auto make_inplace_bg = [&](const wgpu::ComputePipeline& p, std::string_view label) {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel(label)
                    .setLayout(p.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(chunk_data_buf).setSize(kGpuChunkDataBytes),
                        wgpu::BindGroupEntry()
                            .setBinding(1)
                            .setBuffer(chunk_svo_buf)
                            .setSize(svo_chunk_pos.byte_size()),
                    }));
        };

        // In-place with param: {chunk_data(0,rw), chunk_svo(1,rd), param(2,uniform)}
        const auto make_inplace_param_bg = [&](const wgpu::ComputePipeline& p, std::string_view label) {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel(label)
                    .setLayout(p.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(chunk_data_buf).setSize(kGpuChunkDataBytes),
                        wgpu::BindGroupEntry()
                            .setBinding(1)
                            .setBuffer(chunk_svo_buf)
                            .setSize(svo_chunk_pos.byte_size()),
                        wgpu::BindGroupEntry().setBinding(2).setBuffer(param_buf).setSize(kParamBytes),
                    }));
        };

        // Output: {chunk_data(0,rd), chunk_out(1,rw), chunk_svo(2,rd), param(3,uniform)}
        const auto make_output_bg = [&](const wgpu::ComputePipeline& p, std::string_view label,
                                        const wgpu::Buffer& out_buf) {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel(label)
                    .setLayout(p.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(chunk_data_buf).setSize(kGpuChunkDataBytes),
                        wgpu::BindGroupEntry().setBinding(1).setBuffer(out_buf).setSize(kGpuFieldSectionBytes),
                        wgpu::BindGroupEntry()
                            .setBinding(2)
                            .setBuffer(chunk_svo_buf)
                            .setSize(svo_chunk_pos.byte_size()),
                        wgpu::BindGroupEntry().setBinding(3).setBuffer(param_buf).setSize(kParamBytes),
                    }));
        };

        // Output without param: {chunk_data(0,rd), chunk_out(1,rw), chunk_svo(2,rd)} �� for clamp shaders
        const auto make_output_noparam_bg = [&](const wgpu::ComputePipeline& p, std::string_view label,
                                                const wgpu::Buffer& out_buf) {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel(label)
                    .setLayout(p.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(chunk_data_buf).setSize(kGpuChunkDataBytes),
                        wgpu::BindGroupEntry().setBinding(1).setBuffer(out_buf).setSize(kGpuFieldSectionBytes),
                        wgpu::BindGroupEntry()
                            .setBinding(2)
                            .setBuffer(chunk_svo_buf)
                            .setSize(svo_chunk_pos.byte_size()),
                    }));
        };

        bind_group_even             = make_inplace_bg(pipeline_even, "LiquidPressureProjectBGEven");
        bind_group_odd              = make_inplace_bg(pipeline_odd, "LiquidPressureProjectBGOdd");
        gravity_bind_group          = make_inplace_param_bg(gravity_pipeline, "LiquidGravityBG");
        post_u_bind_group           = make_inplace_bg(post_u_pipeline, "LiquidPostUBG");
        post_v_bind_group           = make_inplace_bg(post_v_pipeline, "LiquidPostVBG");
        visc_u_even_bind_group      = make_inplace_param_bg(visc_u_even_pipeline, "LiquidViscUEvenBG");
        visc_u_odd_bind_group       = make_inplace_param_bg(visc_u_odd_pipeline, "LiquidViscUOddBG");
        visc_v_even_bind_group      = make_inplace_param_bg(visc_v_even_pipeline, "LiquidViscVEvenBG");
        visc_v_odd_bind_group       = make_inplace_param_bg(visc_v_odd_pipeline, "LiquidViscVOddBG");
        visc_wall_u_bind_group      = make_inplace_param_bg(visc_wall_u_pipeline, "LiquidViscWallUBG");
        visc_wall_v_bind_group      = make_inplace_param_bg(visc_wall_v_pipeline, "LiquidViscWallVBG");
        extrap_cell_even_bind_group = make_inplace_bg(extrap_cell_even_pipeline, "LiquidExtrapCellEvenBG");
        extrap_cell_odd_bind_group  = make_inplace_bg(extrap_cell_odd_pipeline, "LiquidExtrapCellOddBG");

        advect_u_bind_group  = make_output_bg(advect_u_pipeline, "LiquidAdvectUBG", chunk_u_out_buf);
        advect_v_bind_group  = make_output_bg(advect_v_pipeline, "LiquidAdvectVBG", chunk_v_out_buf);
        density_bind_group   = make_output_bg(density_pipeline, "LiquidDensityBG", chunk_d_out_buf);
        surface_u_bind_group = make_output_bg(surface_u_pipeline, "LiquidSurfaceUBG", chunk_u_out_buf);
        surface_v_bind_group = make_output_bg(surface_v_pipeline, "LiquidSurfaceVBG", chunk_v_out_buf);
        clamp_u_bind_group   = make_output_noparam_bg(clamp_u_pipeline, "LiquidClampUBG", chunk_u_out_buf);
        clamp_v_bind_group   = make_output_noparam_bg(clamp_v_pipeline, "LiquidClampVBG", chunk_v_out_buf);

        ready = static_cast<bool>(chunk_data_buf) && static_cast<bool>(chunk_svo_buf) && static_cast<bool>(param_buf) &&
                static_cast<bool>(readback_buf) && static_cast<bool>(pipeline_even) &&
                static_cast<bool>(pipeline_odd) && static_cast<bool>(gravity_pipeline) &&
                static_cast<bool>(post_u_pipeline) && static_cast<bool>(post_v_pipeline) &&
                static_cast<bool>(advect_u_pipeline) && static_cast<bool>(advect_v_pipeline) &&
                static_cast<bool>(density_pipeline) && static_cast<bool>(surface_u_pipeline) &&
                static_cast<bool>(surface_v_pipeline) && static_cast<bool>(visc_u_even_pipeline) &&
                static_cast<bool>(visc_u_odd_pipeline) && static_cast<bool>(visc_v_even_pipeline) &&
                static_cast<bool>(visc_v_odd_pipeline) && static_cast<bool>(visc_wall_u_pipeline) &&
                static_cast<bool>(visc_wall_v_pipeline) && static_cast<bool>(extrap_cell_even_pipeline) &&
                static_cast<bool>(extrap_cell_odd_pipeline) && static_cast<bool>(clamp_u_pipeline) &&
                static_cast<bool>(clamp_v_pipeline) && static_cast<bool>(bind_group_even) &&
                static_cast<bool>(bind_group_odd) && static_cast<bool>(gravity_bind_group) &&
                static_cast<bool>(post_u_bind_group) && static_cast<bool>(post_v_bind_group) &&
                static_cast<bool>(advect_u_bind_group) && static_cast<bool>(advect_v_bind_group) &&
                static_cast<bool>(density_bind_group) && static_cast<bool>(surface_u_bind_group) &&
                static_cast<bool>(surface_v_bind_group) && static_cast<bool>(visc_u_even_bind_group) &&
                static_cast<bool>(visc_u_odd_bind_group) && static_cast<bool>(visc_v_even_bind_group) &&
                static_cast<bool>(visc_v_odd_bind_group) && static_cast<bool>(visc_wall_u_bind_group) &&
                static_cast<bool>(visc_wall_v_bind_group) && static_cast<bool>(extrap_cell_even_bind_group) &&
                static_cast<bool>(extrap_cell_odd_bind_group) && static_cast<bool>(clamp_u_bind_group) &&
                static_cast<bool>(clamp_v_bind_group);
        return ready;
    }

    void upload_from_sim(const Fluid& sim, const wgpu::Queue& queue) {
        // Pack all 5 fields from chunks[] into host_data[] using planar layout.
        // Section base offsets (element indices, not bytes):
        //   D: kGpuDBase, S: kGpuSBase, P: kGpuPBase, U: kGpuUBase, V: kGpuVBase
        // Element index for chunk (cx,cy), local (lx,ly):
        //   c = cx * kChunksY + cy
        //   li = lx + ly * kChunkSize
        //   elem = BASE + c * kChunkCells + li
        for (int cx = 0; cx < kChunksX; ++cx) {
            for (int cy = 0; cy < kChunksY; ++cy) {
                const int c            = cx * kChunksY + cy;
                const ChunkData& chunk = sim.chunk_at(cx, cy);
                const int base_d       = kGpuDBase + c * kChunkCells;
                const int base_s       = kGpuSBase + c * kChunkCells;
                const int base_p       = kGpuPBase + c * kChunkCells;
                const int base_u       = kGpuUBase + c * kChunkCells;
                const int base_v       = kGpuVBase + c * kChunkCells;
                for (int ly = 0; ly < kChunkSize; ++ly) {
                    for (int lx = 0; lx < kChunkSize; ++lx) {
                        const int li   = lx + ly * kChunkSize;
                        const auto u32 = [](int v) noexcept { return static_cast<std::uint32_t>(v); };
                        host_data[static_cast<std::size_t>(base_d + li)] =
                            chunk.D.get({u32(lx), u32(ly)}).transform(Fluid::deref).value_or(fx32{0});
                        host_data[static_cast<std::size_t>(base_s + li)] = static_cast<std::int32_t>(
                            chunk.S.get({u32(lx), u32(ly)}).transform(Fluid::deref).value_or(std::uint8_t{0}));
                        host_data[static_cast<std::size_t>(base_p + li)] = 0;  // reset pressure every frame
                        host_data[static_cast<std::size_t>(base_u + li)] =
                            chunk.U.get({u32(lx), u32(ly)}).transform(Fluid::deref).value_or(fx32{0});
                        host_data[static_cast<std::size_t>(base_v + li)] =
                            chunk.V.get({u32(lx), u32(ly)}).transform(Fluid::deref).value_or(fx32{0});
                    }
                }
            }
        }

        queue.writeBuffer(chunk_data_buf, 0, host_data.data(), kGpuChunkDataBytes);

        if (!svo_uploaded) {
            queue.writeBuffer(chunk_svo_buf, 0, svo_chunk_pos.data(), svo_chunk_pos.byte_size());
            svo_uploaded = true;
        }
    }

    bool readback_all_to_sim(Fluid& sim, const wgpu::Device& device) {
        bool done = false;
        bool ok   = false;

        (void)readback_buf.mapAsync(
            wgpu::MapMode::eRead, 0, kGpuChunkDataBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok   = status == wgpu::MapAsyncStatus::eSuccess;
                    done = true;
                })));

        while (!done) {
            (void)device.poll(true);
        }

        if (!ok) return false;

        const void* ptr = readback_buf.getConstMappedRange(0, kGpuChunkDataBytes);
        if (ptr == nullptr) {
            readback_buf.unmap();
            return false;
        }

        const auto* iptr = static_cast<const std::int32_t*>(ptr);

        // Unpack all fields back to sim chunks
        for (int cx = 0; cx < kChunksX; ++cx) {
            for (int cy = 0; cy < kChunksY; ++cy) {
                const int c      = cx * kChunksY + cy;
                ChunkData& chunk = sim.chunk_at(cx, cy);
                const int base_d = kGpuDBase + c * kChunkCells;
                const int base_u = kGpuUBase + c * kChunkCells;
                const int base_v = kGpuVBase + c * kChunkCells;
                for (int ly = 0; ly < kChunkSize; ++ly) {
                    for (int lx = 0; lx < kChunkSize; ++lx) {
                        const int li   = lx + ly * kChunkSize;
                        const auto u32 = [](int v) noexcept { return static_cast<std::uint32_t>(v); };
                        (void)chunk.D.set({u32(lx), u32(ly)}, iptr[static_cast<std::size_t>(base_d + li)]);
                        (void)chunk.U.set({u32(lx), u32(ly)}, iptr[static_cast<std::size_t>(base_u + li)]);
                        (void)chunk.V.set({u32(lx), u32(ly)}, iptr[static_cast<std::size_t>(base_v + li)]);
                    }
                }
            }
        }

        readback_buf.unmap();
        return true;
    }

    void dispatch_full_step_chain(const wgpu::Device& device, const wgpu::Queue& queue, fx32 dt_fx, fx32 sticky_fx) {
        const int iterations = std::max(0, static_cast<int>(((sticky_fx * 4) + (kFxOne / 2)) / kFxOne));
        const fx32 nu_fx     = std::min(kFxHalf, fx_mul(sticky_fx, kFx005));
        const fx32 wall_fx =
            std::clamp<fx32>(static_cast<fx32>(kFxOne - fx_mul(sticky_fx, kFx008)), static_cast<fx32>(0), kFxOne);

        std::array<fx32, 8> params{dt_fx, nu_fx, wall_fx, 0, 0, 0, 0, 0};
        queue.writeBuffer(param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidFullStepChain"));
        constexpr uint32_t kWG = 16;
        // Workgroup counts covering the full grid (padded up)
        const uint32_t gx_cell = (static_cast<uint32_t>(kN) + kWG - 1) / kWG;
        const uint32_t gy_cell = gx_cell;
        // U face: x in [0,kN], y in [0,kN-1]
        const uint32_t gx_u = (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG;
        const uint32_t gy_u = gx_cell;
        // V face: x in [0,kN-1], y in [0,kN]
        const uint32_t gx_v = gx_cell;
        const uint32_t gy_v = (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG;

        // 1. Gravity (adds dt/2 to V)
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(gravity_pipeline);
            pass.setBindGroup(0, gravity_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }

        // 2. Surface tension �� chunk_u_out_buf / chunk_v_out_buf
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(surface_u_pipeline);
            pass.setBindGroup(0, surface_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(surface_v_pipeline);
            pass.setBindGroup(0, surface_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }
        // Copy out �� chunk_data U/V sections
        encoder.copyBufferToBuffer(chunk_u_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuUBase) * 4u,
                                   kGpuFieldSectionBytes);
        encoder.copyBufferToBuffer(chunk_v_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuVBase) * 4u,
                                   kGpuFieldSectionBytes);

        // 3. Viscosity (Gauss-Seidel, in-place)
        for (int k = 0; k < iterations && nu_fx > 0; ++k) {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(visc_u_even_pipeline);
            pass.setBindGroup(0, visc_u_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(visc_u_odd_pipeline);
            pass.setBindGroup(0, visc_u_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(visc_v_even_pipeline);
            pass.setBindGroup(0, visc_v_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);

            pass.setPipeline(visc_v_odd_pipeline);
            pass.setBindGroup(0, visc_v_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }

        // 4. Wall viscosity damping (in-place)
        if (wall_fx < kFxOne) {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(visc_wall_u_pipeline);
            pass.setBindGroup(0, visc_wall_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(visc_wall_v_pipeline);
            pass.setBindGroup(0, visc_wall_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }

        // 5. Pressure projection (even/odd checkerboard, in-place)
        {
            auto pass              = encoder.beginComputePass();
            static bool even_first = true;
            for (int k = 0; k < kIter; ++k) {
                pass.setPipeline(even_first ? pipeline_even : pipeline_odd);
                pass.setBindGroup(0, even_first ? bind_group_even : bind_group_odd, std::span<const uint32_t>{});
                pass.dispatchWorkgroups(gx_cell, gy_cell, 1);
                pass.setPipeline(even_first ? pipeline_odd : pipeline_even);
                pass.setBindGroup(0, even_first ? bind_group_odd : bind_group_even, std::span<const uint32_t>{});
                pass.dispatchWorkgroups(gx_cell, gy_cell, 1);
            }
            even_first = !even_first;
            pass.end();
        }

        // 6. Velocity extrapolation to air cells (in-place)
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(extrap_cell_even_pipeline);
            pass.setBindGroup(0, extrap_cell_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_cell, gy_cell, 1);

            pass.setPipeline(extrap_cell_odd_pipeline);
            pass.setBindGroup(0, extrap_cell_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_cell, gy_cell, 1);
            pass.end();
        }

        // 7. Clamp velocities �� chunk_u/v_out_buf
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(clamp_u_pipeline);
            pass.setBindGroup(0, clamp_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(clamp_v_pipeline);
            pass.setBindGroup(0, clamp_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(chunk_u_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuUBase) * 4u,
                                   kGpuFieldSectionBytes);
        encoder.copyBufferToBuffer(chunk_v_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuVBase) * 4u,
                                   kGpuFieldSectionBytes);

        // 8. Advect velocities �� chunk_u/v_out_buf
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(advect_u_pipeline);
            pass.setBindGroup(0, advect_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(advect_v_pipeline);
            pass.setBindGroup(0, advect_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(chunk_u_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuUBase) * 4u,
                                   kGpuFieldSectionBytes);
        encoder.copyBufferToBuffer(chunk_v_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuVBase) * 4u,
                                   kGpuFieldSectionBytes);

        // 9. Density transport �� chunk_d_out_buf
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(density_pipeline);
            pass.setBindGroup(0, density_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_cell, gy_cell, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(chunk_d_out_buf, 0, chunk_data_buf, static_cast<std::uint64_t>(kGpuDBase) * 4u,
                                   kGpuFieldSectionBytes);

        // 10. Post-process (clamp+wall boundary enforcement, in-place)
        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(post_u_pipeline);
            pass.setBindGroup(0, post_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_u, gy_u, 1);

            pass.setPipeline(post_v_pipeline);
            pass.setBindGroup(0, post_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups(gx_v, gy_v, 1);
            pass.end();
        }

        // Readback: copy full chunk_data buffer to readback_buf
        encoder.copyBufferToBuffer(chunk_data_buf, 0, readback_buf, 0, kGpuChunkDataBytes);

        queue.submit(encoder.finish());
    }
};

// ------- Free GPU helper functions (called from Fluid::physicsStep) -------

bool gpu_run_full_step_chain(Fluid& sim,
                             fx32 dt_fx,
                             fx32 sticky_fx,
                             GpuPressureProjector* projector,
                             const wgpu::Device* device,
                             const wgpu::Queue* queue,
                             render::PipelineServer* ps) {
    if (projector == nullptr || device == nullptr || queue == nullptr || ps == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) projector->ensure_ready(*device, *ps);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_full_step_chain(*device, *queue, dt_fx, sticky_fx);
    return projector->readback_all_to_sim(sim, *device);
}

struct FluidState {
    enum class SyncLatencyMode {
        NoLatency,
        OneFrame,
    };

    Fluid sim;
    assets::Handle<mesh::Mesh> mesh_handle;
    std::unique_ptr<BS::thread_pool<>> thread_pool;
    SyncLatencyMode latency_mode = SyncLatencyMode::OneFrame;
    std::optional<mesh::Mesh> pending_mesh;
    std::unique_ptr<GpuPressureProjector> gpu_pressure;
    std::uint32_t step_count = 0;
    std::vector<std::string> feedback_log;
    char input_buf[256] = {};
};

glm::vec2 screen_to_world(glm::vec2 screen_pos,
                          glm::vec2 window_size,
                          const render::camera::Camera& camera,
                          const render::camera::Projection& projection,
                          const transform::Transform& cam_transform) {
    (void)projection;
    const float ndc_x = (screen_pos.x / window_size.x) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (screen_pos.y / window_size.y) * 2.0f;

    const glm::mat4 proj_matrix = camera.computed.projection;
    const glm::mat4 view_matrix = glm::inverse(cam_transform.to_matrix());
    const glm::mat4 vp_inv      = glm::inverse(proj_matrix * view_matrix);

    const glm::vec4 world = vp_inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return glm::vec2(world.x / world.w, world.y / world.w);
}

std::optional<std::pair<int, int>> world_to_cell(glm::vec2 world_pos) {
    const float world_w = static_cast<float>(kN) * kCellSize;
    const float world_h = static_cast<float>(kN) * kCellSize;
    const float min_x   = -0.5f * world_w;
    const float min_y   = -0.5f * world_h;

    const int gx       = static_cast<int>(std::floor((world_pos.x - min_x) / kCellSize));
    const int gy_world = static_cast<int>(std::floor((world_pos.y - min_y) / kCellSize));
    const int gy       = (kN - 1) - gy_world;

    if (gx < 0 || gy < 0 || gx >= kN || gy >= kN) return std::nullopt;
    return std::pair{gx, gy};
}

mesh::Mesh build_mesh(const Fluid& sim, BS::thread_pool<>* pool = nullptr) {
    const float world_w = static_cast<float>(kN) * kCellSize;
    const float world_h = static_cast<float>(kN) * kCellSize;
    const float min_x   = -0.5f * world_w;
    const float min_y   = -0.5f * world_h;

    struct MeshChunk {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
    };

    if (pool != nullptr && pool->get_thread_count() > 1) {
        const std::size_t task_count = std::max<std::size_t>(1, pool->get_thread_count());
        const auto chunks =
            pool->submit_blocks(
                    0, static_cast<std::int64_t>(kN),
                    [&sim, min_x, min_y](std::int64_t ys, std::int64_t ye) {
                        MeshChunk chunk;
                        chunk.positions.reserve(static_cast<std::size_t>((ye - ys) * kN * 4));
                        chunk.colors.reserve(static_cast<std::size_t>((ye - ys) * kN * 4));

                        for (std::int64_t y = ys; y < ye; ++y) {
                            for (int x = 0; x < kN; ++x) {
                                const bool wall = sim.getS(x, static_cast<int>(y)) != 0;
                                const float d   = sim.getD(x, static_cast<int>(y));
                                if (!wall && d < 0.01f) continue;

                                const int draw_y = (kN - 1) - static_cast<int>(y);

                                const float x0 = min_x + static_cast<float>(x) * kCellSize;
                                const float y0 = min_y + static_cast<float>(draw_y) * kCellSize;
                                const float x1 = x0 + kCellSize;
                                const float y1 = y0 + kCellSize;

                                chunk.positions.push_back({x0, y0, 0.0f});
                                chunk.positions.push_back({x1, y0, 0.0f});
                                chunk.positions.push_back({x1, y1, 0.0f});
                                chunk.positions.push_back({x0, y1, 0.0f});

                                glm::vec4 c;
                                if (wall) {
                                    c = glm::vec4(0.53f, 0.53f, 0.53f, 1.0f);
                                } else if (d < 0.8f) {
                                    const float t = std::clamp(d / 0.8f, 0.0f, 1.0f);
                                    c = glm::vec4((5.0f * (1.0f - t)) / 255.0f, (20.0f + 80.0f * t) / 255.0f,
                                                  (60.0f + 160.0f * t) / 255.0f, 1.0f);
                                } else if (d < 1.0f) {
                                    const float t = std::clamp((d - 0.8f) / 0.2f, 0.0f, 1.0f);
                                    c = glm::vec4(0.0f, (100.0f + 80.0f * t) / 255.0f, (220.0f + 35.0f * t) / 255.0f,
                                                  1.0f);
                                } else {
                                    const float t = std::clamp((d - 1.0f) / 0.3f, 0.0f, 1.0f);
                                    c = glm::vec4((220.0f * t) / 255.0f, (180.0f + 60.0f * t) / 255.0f, 1.0f, 1.0f);
                                }

                                chunk.colors.push_back(c);
                                chunk.colors.push_back(c);
                                chunk.colors.push_back(c);
                                chunk.colors.push_back(c);
                            }
                        }
                        return chunk;
                    },
                    task_count)
                .get();

        std::vector<glm::vec3> positions;
        std::vector<glm::vec4> colors;
        std::vector<std::uint32_t> indices;
        std::size_t total_vertices = 0;
        for (const auto& c : chunks) total_vertices += c.positions.size();

        positions.reserve(total_vertices);
        colors.reserve(total_vertices);
        indices.reserve((total_vertices / 4) * 6);

        std::uint32_t base = 0;
        for (const auto& c : chunks) {
            positions.insert(positions.end(), c.positions.begin(), c.positions.end());
            colors.insert(colors.end(), c.colors.begin(), c.colors.end());

            const std::uint32_t quad_count = static_cast<std::uint32_t>(c.positions.size() / 4);
            for (std::uint32_t i = 0; i < quad_count; ++i) {
                indices.push_back(base + 0);
                indices.push_back(base + 1);
                indices.push_back(base + 2);
                indices.push_back(base + 2);
                indices.push_back(base + 3);
                indices.push_back(base + 0);
                base += 4;
            }
        }

        return mesh::Mesh()
            .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
            .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
            .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
            .with_indices<std::uint32_t>(indices);
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    std::vector<std::uint32_t> indices;

    positions.reserve(static_cast<std::size_t>(kN * kN * 4));
    colors.reserve(static_cast<std::size_t>(kN * kN * 4));
    indices.reserve(static_cast<std::size_t>(kN * kN * 6));

    std::uint32_t base = 0;
    for (int y = 0; y < kN; ++y) {
        for (int x = 0; x < kN; ++x) {
            const bool wall = sim.getS(x, y) != 0;
            const float d   = sim.getD(x, y);
            if (!wall && d < 0.01f) continue;

            const int draw_y = (kN - 1) - y;

            const float x0 = min_x + static_cast<float>(x) * kCellSize;
            const float y0 = min_y + static_cast<float>(draw_y) * kCellSize;
            const float x1 = x0 + kCellSize;
            const float y1 = y0 + kCellSize;

            positions.push_back({x0, y0, 0.0f});
            positions.push_back({x1, y0, 0.0f});
            positions.push_back({x1, y1, 0.0f});
            positions.push_back({x0, y1, 0.0f});

            glm::vec4 c;
            if (wall) {
                c = glm::vec4(0.53f, 0.53f, 0.53f, 1.0f);
            } else if (d < 0.8f) {
                const float t = std::clamp(d / 0.8f, 0.0f, 1.0f);
                c = glm::vec4((5.0f * (1.0f - t)) / 255.0f, (20.0f + 80.0f * t) / 255.0f, (60.0f + 160.0f * t) / 255.0f,
                              1.0f);
            } else if (d < 1.0f) {
                const float t = std::clamp((d - 0.8f) / 0.2f, 0.0f, 1.0f);
                c             = glm::vec4(0.0f, (100.0f + 80.0f * t) / 255.0f, (220.0f + 35.0f * t) / 255.0f, 1.0f);
            } else {
                const float t = std::clamp((d - 1.0f) / 0.3f, 0.0f, 1.0f);
                c             = glm::vec4((220.0f * t) / 255.0f, (180.0f + 60.0f * t) / 255.0f, 1.0f, 1.0f);
            }

            colors.push_back(c);
            colors.push_back(c);
            colors.push_back(c);
            colors.push_back(c);

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 0);
            base += 4;
        }
    }

    return mesh::Mesh()
        .with_primitive_type(wgpu::PrimitiveTopology::eTriangleList)
        .with_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_indices<std::uint32_t>(indices);
}

void liquid_imgui_ui(imgui::Ctx imgui_ctx, core::ResMut<FluidState> state) {
    ImGui::Begin("Liquid Sim Feedback");

    ImGui::SeparatorText("Stats");
    ImGui::Text("Steps : %u", state->step_count);
    ImGui::Text("Paused: %s", state->sim.paused ? "yes" : "no");
    ImGui::Text("dt scale  : %.2f", state->sim.dt_scale);
    ImGui::Text("Stickiness: %.2f", state->sim.stickiness);
    ImGui::Text("Pen size  : %d", state->sim.pen_size);

    ImGui::SeparatorText("Controls");
    ImGui::SliderFloat("dt scale", &state->sim.dt_scale, 1.0f, 10.0f, "%.2f");
    ImGui::SliderFloat("Stickiness", &state->sim.stickiness, 0.0f, 10.0f, "%.2f");
    ImGui::SliderInt("Pen size", &state->sim.pen_size, 1, 20);

    const char* tools[] = {"Water", "Wall", "Eraser"};
    int t               = static_cast<int>(state->sim.tool);
    if (ImGui::Combo("Tool", &t, tools, 3)) state->sim.tool = static_cast<PaintTool>(t);

    if (ImGui::Button("Pause/Resume")) state->sim.paused = !state->sim.paused;
    ImGui::SameLine();
    if (ImGui::Button("Reset")) state->sim.reset();

    ImGui::SeparatorText("Feedback");
    ImGui::BeginChild("##log", ImVec2(0, 150), true);
    for (const auto& msg : state->feedback_log) ImGui::TextUnformatted(msg.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    bool send =
        ImGui::InputText("##input", state->input_buf, sizeof(state->input_buf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Send") || send) {
        if (state->input_buf[0] != '\0') {
            state->feedback_log.emplace_back(state->input_buf);
            std::println("{}", state->input_buf);
            state->input_buf[0] = '\0';
        }
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

struct Plugin {
    void finish(core::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        Fluid sim;
        sim.reset();

        auto thread_pool  = std::make_unique<BS::thread_pool<>>(std::max(1u, std::thread::hardware_concurrency()));
        auto gpu_pressure = std::make_unique<GpuPressureProjector>();
        gpu_pressure->register_pipelines(world);
        auto mesh_handle = mesh_assets.emplace(build_mesh(sim, thread_pool.get()));

        world.spawn(mesh::Mesh2d{mesh_handle},
                    mesh::MeshMaterial2d{.color = glm::vec4(1.0f), .alpha_mode = mesh::MeshAlphaMode2d::Opaque},
                    transform::Transform{});

        world.insert_resource(FluidState{.sim          = std::move(sim),
                                         .mesh_handle  = std::move(mesh_handle),
                                         .thread_pool  = std::move(thread_pool),
                                         .gpu_pressure = std::move(gpu_pressure)});

        app.add_systems(
            core::Update,
            core::into([](core::ResMut<FluidState> state, core::Res<wgpu::Device> device, core::Res<wgpu::Queue> queue,
                          core::ResMut<render::PipelineServer> pipeline_server,
                          core::Res<input::ButtonInput<input::MouseButton>> mouse_buttons,
                          core::Res<input::ButtonInput<input::KeyCode>> keys,
                          core::Query<core::Item<const window::CachedWindow&>, core::With<window::PrimaryWindow>>
                              window_query,
                          core::Query<core::Item<const render::camera::Camera&, const render::camera::Projection&,
                                                 const transform::Transform&>> camera_query,
                          core::ResMut<assets::Assets<mesh::Mesh>> meshes) {
                if (keys->just_pressed(input::KeyCode::KeySpace)) state->sim.paused = !state->sim.paused;
                if (keys->just_pressed(input::KeyCode::KeyR)) state->sim.reset();

                if (keys->just_pressed(input::KeyCode::Key0)) {
                    state->latency_mode = FluidState::SyncLatencyMode::NoLatency;
                    state->pending_mesh.reset();
                }
                if (keys->just_pressed(input::KeyCode::Key9)) {
                    state->latency_mode = FluidState::SyncLatencyMode::OneFrame;
                    state->pending_mesh.reset();
                }

                if (keys->just_pressed(input::KeyCode::Key1)) state->sim.tool = PaintTool::Water;
                if (keys->just_pressed(input::KeyCode::Key2)) state->sim.tool = PaintTool::Wall;
                if (keys->just_pressed(input::KeyCode::Key3)) state->sim.tool = PaintTool::Eraser;

                if (keys->just_pressed(input::KeyCode::KeyLeftBracket))
                    state->sim.pen_size = std::max(1, state->sim.pen_size - 1);
                if (keys->just_pressed(input::KeyCode::KeyRightBracket))
                    state->sim.pen_size = std::min(20, state->sim.pen_size + 1);

                if (keys->just_pressed(input::KeyCode::KeyMinus))
                    state->sim.dt_scale = std::max(1.0f, state->sim.dt_scale - 0.25f);
                if (keys->just_pressed(input::KeyCode::KeyEqual))
                    state->sim.dt_scale = std::min(10.0f, state->sim.dt_scale + 0.25f);

                if (keys->just_pressed(input::KeyCode::KeyComma))
                    state->sim.stickiness = std::max(0.0f, state->sim.stickiness - 0.1f);
                if (keys->just_pressed(input::KeyCode::KeyPeriod))
                    state->sim.stickiness = std::min(10.0f, state->sim.stickiness + 0.1f);

                auto win_opt = window_query.single();
                auto cam_opt = camera_query.single();

                if (win_opt && cam_opt) {
                    auto&& [window]                   = *win_opt;
                    auto&& [cam, proj, cam_transform] = *cam_opt;

                    const auto [cx, cy] = window.cursor_pos;
                    const auto [ww, wh] = window.size;
                    if (ww > 0 && wh > 0) {
                        const glm::vec2 world = screen_to_world(
                            glm::vec2(static_cast<float>(cx), static_cast<float>(cy)),
                            glm::vec2(static_cast<float>(ww), static_cast<float>(wh)), cam, proj, cam_transform);

                        if (auto cell = world_to_cell(world); cell.has_value()) {
                            const bool lmb = mouse_buttons->pressed(input::MouseButton::MouseButtonLeft);
                            const bool rmb = mouse_buttons->pressed(input::MouseButton::MouseButtonRight);
                            if (lmb || rmb) {
                                const PaintTool t = rmb ? PaintTool::Eraser : state->sim.tool;
                                state->sim.apply_brush(cell->first, cell->second, t);
                            }
                        }
                    }
                }

                if (!state->sim.paused) {
                    const float dt = state->sim.dt_scale * 0.05f;
                    state->sim.solve(dt, state->thread_pool.get(), state->gpu_pressure.get(), std::addressof(*device),
                                     std::addressof(*queue), std::addressof(*pipeline_server));
                    ++state->step_count;
                }

                auto latest_mesh = build_mesh(state->sim, state->thread_pool.get());

                if (state->latency_mode == FluidState::SyncLatencyMode::OneFrame) {
                    if (state->pending_mesh.has_value()) {
                        (void)meshes->insert(state->mesh_handle.id(), std::move(*state->pending_mesh));
                    }
                    state->pending_mesh.emplace(std::move(latest_mesh));
                } else {
                    (void)meshes->insert(state->mesh_handle.id(), std::move(latest_mesh));
                }
            }).set_name("liquid html-port update"));

        app.add_systems(core::PreUpdate, core::into(liquid_imgui_ui).after(imgui::BeginFrameSet));
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title =
        "Liquid HTML Port | 1/2/3 tool | [ ] pen | -/= dt | ,/. stick | 0 no-lat | 9 one-frame | Space pause | R reset";
    primary_window.size = {1280, 800};

    app.add_plugins(window::WindowPlugin{
                        .primary_window = primary_window,
                        .exit_condition = window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(imgui::ImGuiPlugin{})
        .add_plugins(Plugin{});

    app.run();
}
