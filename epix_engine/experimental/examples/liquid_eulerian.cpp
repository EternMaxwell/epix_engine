import std;
import glm;
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
import BS.thread_pool;

namespace {
using namespace core;
using ext::grid::packed_grid;

constexpr int kN          = 200;
constexpr int kIter       = 20;
constexpr float kGravity  = 0.5f;
constexpr float kTargetD  = 1.0f;
constexpr float kMaxVel   = 8.0f;
constexpr float kCellSize = 4.0f;

using fx32                                 = std::int32_t;
constexpr fx32 kFxShift                    = 16;
constexpr fx32 kFxOne                      = static_cast<fx32>(1 << kFxShift);
constexpr fx32 kFxHalf                     = 32768;
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
                             const wgpu::Queue* queue);

enum class PaintTool {
    Water,
    Wall,
    Eraser,
};

struct Fluid {
    packed_grid<2, fx32> D{{kN, kN}, 0};
    packed_grid<2, fx32> newD{{kN, kN}, 0};
    packed_grid<2, std::uint8_t> S{{kN, kN}, 0};
    packed_grid<2, fx32> P{{kN, kN}, 0};

    packed_grid<2, fx32> U{{kN + 1, kN}, 0};
    packed_grid<2, fx32> newU{{kN + 1, kN}, 0};
    packed_grid<2, fx32> V{{kN, kN + 1}, 0};
    packed_grid<2, fx32> newV{{kN, kN + 1}, 0};

    packed_grid<2, fx32> fluxU{{kN + 1, kN}, 0};
    packed_grid<2, fx32> fluxV{{kN, kN + 1}, 0};

    std::vector<int> active_indices{};
    std::vector<int> core_indices{};

    fx32 target_total_mass_fx  = 0;
    fx32 current_total_mass_fx = 0;

    PaintTool tool   = PaintTool::Water;
    bool paused      = false;
    int pen_size     = 2;
    float dt_scale   = 5.0f;
    float stickiness = 0.0f;

    static constexpr std::uint32_t u32(int v) { return static_cast<std::uint32_t>(v); }

    static constexpr int idx(int x, int y) { return x + y * kN; }

    static constexpr auto deref = [](auto&& value) { return value.get(); };

    float getD(int x, int y) const {
        return fx_to_float(D.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0)));
    }

    std::uint8_t getS(int x, int y) const { return S.get({u32(x), u32(y)}).transform(deref).value_or(1); }

    float getP(int x, int y) const {
        return fx_to_float(P.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0)));
    }

    float getU(int x, int y) const {
        return fx_to_float(U.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0)));
    }

    float getV(int x, int y) const {
        return fx_to_float(V.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0)));
    }

    void setD(int x, int y, float v) { (void)D.set({u32(x), u32(y)}, soft_bound_density_fx(fx_from_float(v))); }

    void setS(int x, int y, std::uint8_t v) { (void)S.set({u32(x), u32(y)}, v); }

    void setP(int x, int y, float v) { (void)P.set({u32(x), u32(y)}, fx_from_float(v)); }

    void setU(int x, int y, float v) { (void)U.set({u32(x), u32(y)}, fx_from_float(v)); }

    void setV(int x, int y, float v) { (void)V.set({u32(x), u32(y)}, fx_from_float(v)); }

    void reset() {
        D.clear();
        newD.clear();
        S.clear();
        P.clear();
        U.clear();
        newU.clear();
        V.clear();
        newV.clear();
        fluxU.clear();
        fluxV.clear();

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

    void apply_brush(int cx, int cy, PaintTool paint) {
        for (int oy = -pen_size; oy <= pen_size; ++oy) {
            for (int ox = -pen_size; ox <= pen_size; ++ox) {
                const int x = cx + ox;
                const int y = cy + oy;
                if (x <= 0 || x >= kN - 1 || y <= 0 || y >= kN - 1) continue;

                const int id = idx(x, y);
                if (paint == PaintTool::Water && getS(x, y) == 0) {
                    if (getD(x, y) < 1.0f) {
                        const fx32 d_fx = D.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0));
                        const fx32 add  = kFxOne - d_fx;
                        setD(x, y, 1.0f);
                        target_total_mass_fx = static_cast<fx32>(target_total_mass_fx + add);
                    }
                } else if (paint == PaintTool::Wall && getS(x, y) == 0) {
                    target_total_mass_fx =
                        static_cast<fx32>(target_total_mass_fx - D.get({u32(x), u32(y)}).transform(deref).value_or(0));
                    setS(x, y, 1);
                    setD(x, y, 0.0f);
                    setU(x, y, 0.0f);
                    setU(x + 1, y, 0.0f);
                    setV(x, y, 0.0f);
                    setV(x, y + 1, 0.0f);
                } else if (paint == PaintTool::Eraser) {
                    target_total_mass_fx =
                        static_cast<fx32>(target_total_mass_fx - D.get({u32(x), u32(y)}).transform(deref).value_or(0));
                    setS(x, y, 0);
                    setD(x, y, 0.0f);
                }
                (void)id;
            }
        }
        if (target_total_mass_fx < 0) target_total_mass_fx = 0;
    }

    void physicsStep(float dt,
                     BS::thread_pool<>* pool         = nullptr,
                     GpuPressureProjector* projector = nullptr,
                     const wgpu::Device* device      = nullptr,
                     const wgpu::Queue* queue        = nullptr) {
        if (projector == nullptr || device == nullptr || queue == nullptr) {
            throw std::runtime_error("GPU simulation requires projector/device/queue");
        }

        const auto require_gpu = [](bool ok, const char* stage) {
            if (!ok) {
                throw std::runtime_error(std::string("GPU stage failed: ") + stage);
            }
        };

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
                const fx32 d_fx = D.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0));
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
                    const fx32 d0 = D.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0));
                    (void)D.set({u32(x), u32(y)}, static_cast<fx32>(d0 + add_per_cell_fx));
                }
            }
        } else if (current_mass_acc_fx < kFxOne) {
            target_total_mass_fx = 0;
        }

        require_gpu(gpu_run_full_step_chain(*this, dt_fx, sticky_fx, projector, device, queue), "full_gpu_step_chain");
    }

    void solve(float totalDt,
               BS::thread_pool<>* pool         = nullptr,
               GpuPressureProjector* projector = nullptr,
               const wgpu::Device* device      = nullptr,
               const wgpu::Queue* queue        = nullptr) {
        fx32 max_vel_fx              = 0;
        const std::size_t task_count = pool == nullptr ? 0 : std::max<std::size_t>(1, pool->get_thread_count());
        if (pool != nullptr && task_count > 1) {
            const auto u_maxes = pool->submit_blocks(
                                         0, static_cast<std::int64_t>(kN),
                                         [this](std::int64_t ys, std::int64_t ye) {
                                             fx32 local_max = 0;
                                             for (std::int64_t y = ys; y < ye; ++y) {
                                                 for (int x = 0; x <= kN; ++x) {
                                                     const fx32 u_fx = U.get({u32(x), u32(static_cast<int>(y))})
                                                                           .transform(deref)
                                                                           .value_or(static_cast<fx32>(0));
                                                     local_max       = std::max(local_max, fx_abs(u_fx));
                                                 }
                                             }
                                             return local_max;
                                         },
                                         task_count)
                                     .get();

            const auto v_maxes = pool->submit_blocks(
                                         0, static_cast<std::int64_t>(kN + 1),
                                         [this](std::int64_t ys, std::int64_t ye) {
                                             fx32 local_max = 0;
                                             for (std::int64_t y = ys; y < ye; ++y) {
                                                 for (int x = 0; x < kN; ++x) {
                                                     const fx32 v_fx = V.get({u32(x), u32(static_cast<int>(y))})
                                                                           .transform(deref)
                                                                           .value_or(static_cast<fx32>(0));
                                                     local_max       = std::max(local_max, fx_abs(v_fx));
                                                 }
                                             }
                                             return local_max;
                                         },
                                         task_count)
                                     .get();

            for (const fx32 v : u_maxes) max_vel_fx = std::max(max_vel_fx, v);
            for (const fx32 v : v_maxes) max_vel_fx = std::max(max_vel_fx, v);
        } else {
            for (int y = 0; y < kN; ++y) {
                for (int x = 0; x <= kN; ++x) {
                    const fx32 u_fx = U.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0));
                    max_vel_fx      = std::max(max_vel_fx, fx_abs(u_fx));
                }
            }
            for (int y = 0; y <= kN; ++y) {
                for (int x = 0; x < kN; ++x) {
                    const fx32 v_fx = V.get({u32(x), u32(y)}).transform(deref).value_or(static_cast<fx32>(0));
                    max_vel_fx      = std::max(max_vel_fx, fx_abs(v_fx));
                }
            }
        }

        fx32 max_vel_safe_fx   = std::max(max_vel_fx, kFx01);
        fx32 max_allowed_dt_fx = fx_div(kFx08, max_vel_safe_fx);
        if (max_allowed_dt_fx > kFx02) max_allowed_dt_fx = kFx02;

        fx32 remaining_fx = fx_from_float(totalDt);
        int substeps      = 0;
        while (remaining_fx > kFx0001 && substeps < 10) {
            fx32 step_fx = remaining_fx;
            if (step_fx > max_allowed_dt_fx) step_fx = max_allowed_dt_fx;
            physicsStep(fx_to_float(step_fx), pool, projector, device, queue);
            remaining_fx = static_cast<fx32>(remaining_fx - step_fx);
            ++substeps;
        }
    }
};

struct GpuPressureProjector {
    wgpu::ComputePipeline pipeline_even;
    wgpu::ComputePipeline pipeline_odd;
    wgpu::BindGroup bind_group_even;
    wgpu::BindGroup bind_group_odd;

    wgpu::Buffer d_buf;
    wgpu::Buffer s_buf;
    wgpu::Buffer u_buf;
    wgpu::Buffer v_buf;
    wgpu::Buffer p_buf;
    wgpu::Buffer advect_u_out_buf;
    wgpu::Buffer advect_v_out_buf;
    wgpu::Buffer density_out_buf;
    wgpu::Buffer advect_param_buf;
    wgpu::Buffer readback_u;
    wgpu::Buffer readback_v;
    wgpu::Buffer readback_d;

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

    std::vector<fx32> host_d          = std::vector<fx32>(static_cast<std::size_t>(kN * kN), static_cast<fx32>(0));
    std::vector<std::uint32_t> host_s = std::vector<std::uint32_t>(static_cast<std::size_t>(kN * kN), 0);
    std::vector<fx32> host_u = std::vector<fx32>(static_cast<std::size_t>((kN + 1) * kN), static_cast<fx32>(0));
    std::vector<fx32> host_v = std::vector<fx32>(static_cast<std::size_t>(kN * (kN + 1)), static_cast<fx32>(0));
    std::vector<fx32> host_p = std::vector<fx32>(static_cast<std::size_t>(kN * kN), static_cast<fx32>(0));

    // Velocity staging buffers keep current float-based WGSL velocity paths working.

    bool ready = false;

    static constexpr std::size_t kDBytes     = static_cast<std::size_t>(kN * kN) * sizeof(fx32);
    static constexpr std::size_t kSBytes     = static_cast<std::size_t>(kN * kN) * sizeof(std::uint32_t);
    static constexpr std::size_t kUBytes     = static_cast<std::size_t>((kN + 1) * kN) * sizeof(fx32);
    static constexpr std::size_t kVBytes     = static_cast<std::size_t>(kN * (kN + 1)) * sizeof(fx32);
    static constexpr std::size_t kParamBytes = 32;

    void init(const wgpu::Device& device) {
        if (ready) return;

        static constexpr std::string_view kShaderEven = R"(
const N : u32 = 200u;
const NXU : u32 = 201u;
const NYU : u32 = 200u;
const NXV : u32 = 200u;
const OMEGA_FX : i32 = 117965;
const TARGET_DIV_GAIN_FX : i32 = 6554;

@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read> S : array<u32>;
@group(0) @binding(2) var<storage, read_write> U : array<i32>;
@group(0) @binding(3) var<storage, read_write> V : array<i32>;
@group(0) @binding(4) var<storage, read_write> P : array<i32>;

fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx(x : u32, y : u32) -> u32 { return x + y * N; }
fn idx_u(x : u32, y : u32) -> u32 { return x + y * NXU; }
fn idx_v(x : u32, y : u32) -> u32 { return x + y * NXV; }

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = gid.x;
    let y = gid.y;
    if (x <= 0u || y <= 0u || x >= N - 1u || y >= N - 1u) { return; }
    if (((x + y) & 1u) != 0u) { return; }

    let i = idx(x, y);
    let dFx = D[i];
    if (dFx < 13107) { return; }

    let uLFx = U[idx_u(x, y)];
    let uRFx = U[idx_u(x + 1u, y)];
    let vTFx = V[idx_v(x, y)];
    let vBFx = V[idx_v(x, y + 1u)];

    let velDivFx = (uRFx - uLFx) + (vBFx - vTFx);
    let densityErrFx = dFx - 65536;
    let targetDivFx = select(0, fx_mul(densityErrFx, TARGET_DIV_GAIN_FX), densityErrFx > 0);
    let totalDivFx = velDivFx - targetDivFx;

    let sL = select(0u, 1u, S[idx(x - 1u, y)] != 0u);
    let sR = select(0u, 1u, S[idx(x + 1u, y)] != 0u);
    let sT = select(0u, 1u, S[idx(x, y - 1u)] != 0u);
    let sB = select(0u, 1u, S[idx(x, y + 1u)] != 0u);

    let n = 4u - (sL + sR + sT + sB);
    if (n == 0u) { return; }

    var pCorrFx = i32(-totalDivFx / i32(n));
    pCorrFx = fx_mul(pCorrFx, OMEGA_FX);
    let weightFx = select(fx_mul(dFx, dFx), 65536, dFx >= 65536);
    pCorrFx = fx_mul(pCorrFx, weightFx);

    P[i] = P[i] + pCorrFx;
    if (sL == 0u) { U[idx_u(x, y)] = uLFx - pCorrFx; }
    if (sR == 0u) { U[idx_u(x + 1u, y)] = uRFx + pCorrFx; }
    if (sT == 0u) { V[idx_v(x, y)] = vTFx - pCorrFx; }
    if (sB == 0u) { V[idx_v(x, y + 1u)] = vBFx + pCorrFx; }
}
)";

        static constexpr std::string_view kShaderOdd = R"(
const N : u32 = 200u;
const NXU : u32 = 201u;
const NYU : u32 = 200u;
const NXV : u32 = 200u;
const OMEGA_FX : i32 = 117965;
const TARGET_DIV_GAIN_FX : i32 = 6554;

@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read> S : array<u32>;
@group(0) @binding(2) var<storage, read_write> U : array<i32>;
@group(0) @binding(3) var<storage, read_write> V : array<i32>;
@group(0) @binding(4) var<storage, read_write> P : array<i32>;

fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx(x : u32, y : u32) -> u32 { return x + y * N; }
fn idx_u(x : u32, y : u32) -> u32 { return x + y * NXU; }
fn idx_v(x : u32, y : u32) -> u32 { return x + y * NXV; }

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = gid.x;
    let y = gid.y;
    if (x <= 0u || y <= 0u || x >= N - 1u || y >= N - 1u) { return; }
    if (((x + y) & 1u) != 1u) { return; }

    let i = idx(x, y);
    let dFx = D[i];
    if (dFx < 13107) { return; }

    let uLFx = U[idx_u(x, y)];
    let uRFx = U[idx_u(x + 1u, y)];
    let vTFx = V[idx_v(x, y)];
    let vBFx = V[idx_v(x, y + 1u)];

    let velDivFx = (uRFx - uLFx) + (vBFx - vTFx);
    let densityErrFx = dFx - 65536;
    let targetDivFx = select(0, fx_mul(densityErrFx, TARGET_DIV_GAIN_FX), densityErrFx > 0);
    let totalDivFx = velDivFx - targetDivFx;

    let sL = select(0u, 1u, S[idx(x - 1u, y)] != 0u);
    let sR = select(0u, 1u, S[idx(x + 1u, y)] != 0u);
    let sT = select(0u, 1u, S[idx(x, y - 1u)] != 0u);
    let sB = select(0u, 1u, S[idx(x, y + 1u)] != 0u);

    let n = 4u - (sL + sR + sT + sB);
    if (n == 0u) { return; }

    var pCorrFx = i32(-totalDivFx / i32(n));
    pCorrFx = fx_mul(pCorrFx, OMEGA_FX);
    let weightFx = select(fx_mul(dFx, dFx), 65536, dFx >= 65536);
    pCorrFx = fx_mul(pCorrFx, weightFx);

    P[i] = P[i] + pCorrFx;
    if (sL == 0u) { U[idx_u(x, y)] = uLFx - pCorrFx; }
    if (sR == 0u) { U[idx_u(x + 1u, y)] = uRFx + pCorrFx; }
    if (sT == 0u) { V[idx_v(x, y)] = vTFx - pCorrFx; }
    if (sB == 0u) { V[idx_v(x, y + 1u)] = vBFx + pCorrFx; }
}
)";

        static constexpr std::string_view kShaderAdvectU = R"(
const N : i32 = 200;

@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read> U : array<i32>;
@group(0) @binding(2) var<storage, read> V : array<i32>;
@group(0) @binding(3) var<storage, read_write> outU : array<i32>;
struct AdvectParam { dt : i32, _p0 : vec3<i32> };
@group(0) @binding(4) var<uniform> param : AdvectParam;

fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

fn u_at_fx(x : i32, y : i32) -> i32 { return U[u32(idx_u(x, y))]; }
fn v_at_fx(x : i32, y : i32) -> i32 { return V[u32(idx_v(x, y))]; }
fn s_at(x : i32, y : i32) -> u32 { return S[u32(idx_s(x, y))]; }

fn fx_lerp(a : i32, b : i32, tFx : i32) -> i32 {
    return a + fx_mul(b - a, tFx);
}

fn sample_u_fx(xFxIn : i32, yFxIn : i32) -> i32 {
    let xFx = clamp(xFxIn, 0, 200 * 65536);
    let yFx = clamp(yFxIn, 0, 199 * 65536);
    let x0 = clamp(xFx >> 16, 0, 199);
    let y0 = clamp((yFx - 32768) >> 16, 0, 198);
    let sFx = xFx - (x0 << 16);
    let tFx = yFx - ((y0 << 16) + 32768);
    let aFx = u_at_fx(x0, y0);
    let bFx = u_at_fx(min(x0 + 1, 200), y0);
    let cFx = u_at_fx(x0, y0 + 1);
    let dFx = u_at_fx(min(x0 + 1, 200), y0 + 1);
    let abFx = fx_lerp(aFx, bFx, sFx);
    let cdFx = fx_lerp(cFx, dFx, sFx);
    return fx_lerp(abFx, cdFx, tFx);
}

fn sample_v_fx(xFxIn : i32, yFxIn : i32) -> i32 {
    let xFx = clamp(xFxIn, 0, 199 * 65536);
    let yFx = clamp(yFxIn, 0, 200 * 65536);
    let x0 = clamp((xFx - 32768) >> 16, 0, 198);
    let y0 = clamp(yFx >> 16, 0, 199);
    let sFx = xFx - ((x0 << 16) + 32768);
    let tFx = yFx - (y0 << 16);
    let aFx = v_at_fx(x0, y0);
    let bFx = v_at_fx(x0 + 1, y0);
    let cFx = v_at_fx(x0, min(y0 + 1, 200));
    let dFx = v_at_fx(x0 + 1, min(y0 + 1, 200));
    let abFx = fx_lerp(aFx, bFx, sFx);
    let cdFx = fx_lerp(cFx, dFx, sFx);
    return fx_lerp(abFx, cdFx, tFx);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > 200 || y < 0 || y >= 200) { return; }
    let out_i = idx_u(x, y);

    if (x <= 0 || x >= 200) {
        outU[u32(out_i)] = 0;
        return;
    }

    if (s_at(x, y) != 0u || s_at(x - 1, y) != 0u) {
        outU[u32(out_i)] = 0;
        return;
    }

    let halfDtFx = param.dt / 2;
    let xFx = x * 65536;
    let yHalfFx = y * 65536 + 32768;
    let uValFx = u_at_fx(x, y);
    let vAvgFx = (v_at_fx(x, y) + v_at_fx(x - 1, y) + v_at_fx(x, y + 1) + v_at_fx(x - 1, y + 1)) / 4;

    let midXFx = xFx - fx_mul(uValFx, halfDtFx);
    let midYFx = yHalfFx - fx_mul(vAvgFx, halfDtFx);
    let midUFx = sample_u_fx(midXFx, midYFx);
    let midVFx = sample_v_fx(midXFx, midYFx);

    let outXFx = xFx - fx_mul(midUFx, param.dt);
    let outYFx = yHalfFx - fx_mul(midVFx, param.dt);
    outU[u32(out_i)] = sample_u_fx(outXFx, outYFx);
}
)";

        static constexpr std::string_view kShaderAdvectV = R"(
const N : i32 = 200;

@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read> U : array<i32>;
@group(0) @binding(2) var<storage, read> V : array<i32>;
@group(0) @binding(3) var<storage, read_write> outV : array<i32>;
struct AdvectParam { dt : i32, _p0 : vec3<i32> };
@group(0) @binding(4) var<uniform> param : AdvectParam;

fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

fn u_at_fx(x : i32, y : i32) -> i32 { return U[u32(idx_u(x, y))]; }
fn v_at_fx(x : i32, y : i32) -> i32 { return V[u32(idx_v(x, y))]; }
fn s_at(x : i32, y : i32) -> u32 { return S[u32(idx_s(x, y))]; }

fn fx_lerp(a : i32, b : i32, tFx : i32) -> i32 {
    return a + fx_mul(b - a, tFx);
}

fn sample_u_fx(xFxIn : i32, yFxIn : i32) -> i32 {
    let xFx = clamp(xFxIn, 0, 200 * 65536);
    let yFx = clamp(yFxIn, 0, 199 * 65536);
    let x0 = clamp(xFx >> 16, 0, 199);
    let y0 = clamp((yFx - 32768) >> 16, 0, 198);
    let sFx = xFx - (x0 << 16);
    let tFx = yFx - ((y0 << 16) + 32768);
    let aFx = u_at_fx(x0, y0);
    let bFx = u_at_fx(min(x0 + 1, 200), y0);
    let cFx = u_at_fx(x0, y0 + 1);
    let dFx = u_at_fx(min(x0 + 1, 200), y0 + 1);
    let abFx = fx_lerp(aFx, bFx, sFx);
    let cdFx = fx_lerp(cFx, dFx, sFx);
    return fx_lerp(abFx, cdFx, tFx);
}

fn sample_v_fx(xFxIn : i32, yFxIn : i32) -> i32 {
    let xFx = clamp(xFxIn, 0, 199 * 65536);
    let yFx = clamp(yFxIn, 0, 200 * 65536);
    let x0 = clamp((xFx - 32768) >> 16, 0, 198);
    let y0 = clamp(yFx >> 16, 0, 199);
    let sFx = xFx - ((x0 << 16) + 32768);
    let tFx = yFx - (y0 << 16);
    let aFx = v_at_fx(x0, y0);
    let bFx = v_at_fx(x0 + 1, y0);
    let cFx = v_at_fx(x0, min(y0 + 1, 200));
    let dFx = v_at_fx(x0 + 1, min(y0 + 1, 200));
    let abFx = fx_lerp(aFx, bFx, sFx);
    let cdFx = fx_lerp(cFx, dFx, sFx);
    return fx_lerp(abFx, cdFx, tFx);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= 200 || y < 0 || y > 200) { return; }
    let out_i = idx_v(x, y);

    if (y <= 0 || y >= 200) {
        outV[u32(out_i)] = 0;
        return;
    }

    if (s_at(x, y) != 0u || s_at(x, y - 1) != 0u) {
        outV[u32(out_i)] = 0;
        return;
    }

    let halfDtFx = param.dt / 2;
    let xHalfFx = x * 65536 + 32768;
    let yFx = y * 65536;
    let vValFx = v_at_fx(x, y);
    let uAvgFx = (u_at_fx(x, y) + u_at_fx(x, y - 1) + u_at_fx(x + 1, y) + u_at_fx(x + 1, y - 1)) / 4;

    let midXFx = xHalfFx - fx_mul(uAvgFx, halfDtFx);
    let midYFx = yFx - fx_mul(vValFx, halfDtFx);
    let midUFx = sample_u_fx(midXFx, midYFx);
    let midVFx = sample_v_fx(midXFx, midYFx);

    let outXFx = xHalfFx - fx_mul(midUFx, param.dt);
    let outYFx = yFx - fx_mul(midVFx, param.dt);
    outV[u32(out_i)] = sample_v_fx(outXFx, outYFx);
}
)";

        static constexpr std::string_view kShaderDensity = R"(
const N : i32 = 200;
const MAX_DENSITY_FX : i32 = 88474;
const SOFT_START_FX : i32 = 72090;
const SOFT_RANGE_FX : i32 = 16384;
const SOFT_ZERO_FX : i32 = 66;
const EXP1M_MAX_T_FX : i32 = 524288;
const EXP1M_LUT_LAST : i32 = 65514;
const EXP1M_LUT : array<i32, 33> = array<i32, 33>(
    0, 14497, 25786, 34579, 41427, 46760, 50913, 54148, 56667, 58629, 60156,
    61346, 62273, 62995, 63557, 63995, 64336, 64601, 64808, 64969, 65094, 65192,
    65268, 65327, 65374, 65409, 65437, 65459, 65476, 65489, 65500, 65508, 65514
);

@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read> D : array<i32>;
@group(0) @binding(2) var<storage, read> U : array<i32>;
@group(0) @binding(3) var<storage, read> V : array<i32>;
@group(0) @binding(4) var<storage, read_write> outD : array<i32>;
struct AdvectParam { dt : i32, _p0 : vec3<i32> };
@group(0) @binding(5) var<uniform> param : AdvectParam;

fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

fn s_at(x : i32, y : i32) -> u32 { return S[u32(idx_s(x, y))]; }
fn d_at(x : i32, y : i32) -> i32 { return D[u32(idx_s(x, y))]; }
fn u_at_fx(x : i32, y : i32) -> i32 { return U[u32(idx_u(x, y))]; }
fn v_at_fx(x : i32, y : i32) -> i32 { return V[u32(idx_v(x, y))]; }

fn exp1m_approx_fx(tFx : i32) -> i32 {
    if (tFx <= 0) { return 0; }
    if (tFx >= EXP1M_MAX_T_FX) { return EXP1M_LUT_LAST; }

    let idx = tFx >> 14;
    let rem = tFx - (idx << 14);
    let fracFx = rem << 2;
    let a = EXP1M_LUT[u32(idx)];
    let b = EXP1M_LUT[u32(idx + 1)];
    return a + fx_mul(b - a, fracFx);
}

fn soft_bound_density_fx(dFx : i32) -> i32 {
    if (dFx <= 0) { return 0; }
    if (dFx <= SOFT_START_FX) { return dFx; }
    let overFx = dFx - SOFT_START_FX;
    let tFx = overFx << 2;
    let easedFx = exp1m_approx_fx(tFx);
    let boundedFx = SOFT_START_FX + fx_mul(SOFT_RANGE_FX, easedFx);
    return min(boundedFx, MAX_DENSITY_FX);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y >= N) { return; }

    let i = idx_s(x, y);
    if (s_at(x, y) != 0u) {
        outD[u32(i)] = d_at(x, y);
        return;
    }

    var fL : i32 = 0;
    var fR : i32 = 0;
    var fT : i32 = 0;
    var fB : i32 = 0;
    let dtFx = param.dt;

    if (x > 0 && s_at(x - 1, y) == 0u) {
        let uLfx = u_at_fx(x, y);
        let sx = select(x, x - 1, uLfx > 0);
        fL = fx_mul(fx_mul(d_at(sx, y), uLfx), dtFx);
    }
    if (x < N - 1 && s_at(x + 1, y) == 0u) {
        let uRfx = u_at_fx(x + 1, y);
        let sx = select(x + 1, x, uRfx > 0);
        fR = fx_mul(fx_mul(d_at(sx, y), uRfx), dtFx);
    }
    if (y > 0 && s_at(x, y - 1) == 0u) {
        let vTfx = v_at_fx(x, y);
        let sy = select(y, y - 1, vTfx > 0);
        fT = fx_mul(fx_mul(d_at(x, sy), vTfx), dtFx);
    }
    if (y < N - 1 && s_at(x, y + 1) == 0u) {
        let vBfx = v_at_fx(x, y + 1);
        let sy = select(y + 1, y, vBfx > 0);
        fB = fx_mul(fx_mul(d_at(x, sy), vBfx), dtFx);
    }

    var dFx = d_at(x, y) + (fL - fR) + (fT - fB);
    dFx = soft_bound_density_fx(dFx);
    if (dFx < SOFT_ZERO_FX) { dFx = 0; }
    outD[u32(i)] = dFx;
}
)";

        static constexpr std::string_view kShaderGravity = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> V : array<i32>;
struct AdvectParam { dt : i32, _p0 : vec3<i32> };
@group(0) @binding(2) var<uniform> param : AdvectParam;

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y <= 0 || y >= N) { return; }

    let d0 = D[u32(idx_d(x, y))];
    let d1 = D[u32(idx_d(x, y - 1))];
    if (d0 > 655 || d1 > 655) {
        let i = idx_v(x, y);
        let vFx = V[u32(i)];
        let dvFx = param.dt / 2;
        V[u32(i)] = vFx + dvFx;
    }
}
)";

        static constexpr std::string_view kShaderPostU = R"(
const N : i32 = 200;
    const MAXV_FX : i32 = 524288;
@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read_write> U : array<i32>;

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > N || y < 0 || y >= N) { return; }

    let i = idx_u(x, y);
    var uFx = U[u32(i)];
    if (abs(uFx) > MAXV_FX) {
        uFx = select(-MAXV_FX, MAXV_FX, uFx > 0);
    }

    let sL = select(0u, 1u, x == 0 || S[u32(idx_s(x - 1, y))] != 0u);
    let sR = select(0u, 1u, x == N || S[u32(idx_s(x, y))] != 0u);
    if (sL != 0u || sR != 0u) {
        uFx = 0;
    }
    U[u32(i)] = uFx;
}
)";

        static constexpr std::string_view kShaderPostV = R"(
const N : i32 = 200;
    const MAXV_FX : i32 = 524288;
@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read_write> V : array<i32>;

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y > N) { return; }

    let i = idx_v(x, y);
    var vFx = V[u32(i)];
    if (abs(vFx) > MAXV_FX) {
        vFx = select(-MAXV_FX, MAXV_FX, vFx > 0);
    }

    let sT = select(0u, 1u, y == 0 || S[u32(idx_s(x, y - 1))] != 0u);
    let sB = select(0u, 1u, y == N || S[u32(idx_s(x, y))] != 0u);
    if (sT != 0u || sB != 0u) {
        vFx = 0;
    }
    V[u32(i)] = vFx;
}
)";

        static constexpr std::string_view kShaderSurfaceU = R"(
const N : i32 = 200;
const STRENGTH_FX : i32 = 13107;
const D_MIN_FX : i32 = 13107;
const D_MAX_FX : i32 = 52429;
const GRAD_MIN_FX : i32 = 6554;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read> U : array<i32>;
@group(0) @binding(2) var<storage, read_write> outU : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(3) var<uniform> param : SimParam;
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
fn d_at_fx(x : i32, y : i32) -> i32 { return D[u32(idx_d(x, y))]; }

fn contrib_fx(cx : i32, cy : i32) -> i32 {
    if (cx <= 0 || cx >= N - 1 || cy <= 0 || cy >= N - 1) { return 0; }
    let dFx = d_at_fx(cx, cy);
    if (dFx < D_MIN_FX || dFx > D_MAX_FX) { return 0; }
    let nxFx = d_at_fx(cx + 1, cy) - d_at_fx(cx - 1, cy);
    if (abs(nxFx) <= GRAD_MIN_FX) { return 0; }
    return nxFx;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > N || y < 0 || y >= N) { return; }
    let i = idx_u(x, y);
    let cFx = contrib_fx(x, y) + contrib_fx(x - 1, y);
    let uFx = U[u32(i)];
    let duFx = fx_mul(fx_mul(cFx, STRENGTH_FX), param.p0);
    outU[u32(i)] = uFx + duFx;
}
)";

        static constexpr std::string_view kShaderSurfaceV = R"(
const N : i32 = 200;
const STRENGTH_FX : i32 = 13107;
const D_MIN_FX : i32 = 13107;
const D_MAX_FX : i32 = 52429;
const GRAD_MIN_FX : i32 = 6554;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read> V : array<i32>;
@group(0) @binding(2) var<storage, read_write> outV : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(3) var<uniform> param : SimParam;
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }
fn d_at_fx(x : i32, y : i32) -> i32 { return D[u32(idx_d(x, y))]; }

fn contrib_fx(cx : i32, cy : i32) -> i32 {
    if (cx <= 0 || cx >= N - 1 || cy <= 0 || cy >= N - 1) { return 0; }
    let dFx = d_at_fx(cx, cy);
    if (dFx < D_MIN_FX || dFx > D_MAX_FX) { return 0; }
    let nyFx = d_at_fx(cx, cy + 1) - d_at_fx(cx, cy - 1);
    if (abs(nyFx) <= GRAD_MIN_FX) { return 0; }
    return nyFx;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y > N) { return; }
    let i = idx_v(x, y);
    let cFx = contrib_fx(x, y) + contrib_fx(x, y - 1);
    let vFx = V[u32(i)];
    let dvFx = fx_mul(fx_mul(cFx, STRENGTH_FX), param.p0);
    outV[u32(i)] = vFx + dvFx;
}
)";

        static constexpr std::string_view kShaderViscUEven = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> U : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(2) var<uniform> param : SimParam;

fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }

fn d_cell(x : i32, y : i32) -> i32 {
    if (x < 0 || x >= N || y < 0 || y >= N) { return 0; }
    return D[u32(idx_d(x, y))];
}

fn face_active_u(x : i32, y : i32) -> bool {
    return max(d_cell(x - 1, y), d_cell(x, y)) > 3276;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > N || y < 0 || y >= N) { return; }
    if (((x + y) & 1) != 0) { return; }
    let i = idx_u(x, y);
    var uFx = U[u32(i)];
    let scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (x > 0 && x < N && y > 0 && y < N - 1 && face_active_u(x, y)) {
        let nFx = U[u32(idx_u(x - 1, y))] + U[u32(idx_u(x + 1, y))] +
                  U[u32(idx_u(x, y - 1))] + U[u32(idx_u(x, y + 1))];
        uFx = uFx + fx_mul(nFx - 4 * uFx, scaleFx);
    }
    U[u32(i)] = uFx;
}
)";

        static constexpr std::string_view kShaderViscUOdd = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> U : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(2) var<uniform> param : SimParam;

fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }

fn d_cell(x : i32, y : i32) -> i32 {
    if (x < 0 || x >= N || y < 0 || y >= N) { return 0; }
    return D[u32(idx_d(x, y))];
}

fn face_active_u(x : i32, y : i32) -> bool {
    return max(d_cell(x - 1, y), d_cell(x, y)) > 3276;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > N || y < 0 || y >= N) { return; }
    if (((x + y) & 1) != 1) { return; }
    let i = idx_u(x, y);
    var uFx = U[u32(i)];
    let scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (x > 0 && x < N && y > 0 && y < N - 1 && face_active_u(x, y)) {
        let nFx = U[u32(idx_u(x - 1, y))] + U[u32(idx_u(x + 1, y))] +
                  U[u32(idx_u(x, y - 1))] + U[u32(idx_u(x, y + 1))];
        uFx = uFx + fx_mul(nFx - 4 * uFx, scaleFx);
    }
    U[u32(i)] = uFx;
}
)";

        static constexpr std::string_view kShaderViscVEven = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> V : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(2) var<uniform> param : SimParam;

fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

fn d_cell(x : i32, y : i32) -> i32 {
    if (x < 0 || x >= N || y < 0 || y >= N) { return 0; }
    return D[u32(idx_d(x, y))];
}

fn face_active_v(x : i32, y : i32) -> bool {
    return max(d_cell(x, y - 1), d_cell(x, y)) > 3276;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y > N) { return; }
    if (((x + y) & 1) != 0) { return; }
    let i = idx_v(x, y);
    var vFx = V[u32(i)];
    let scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (y > 0 && y < N && x > 0 && x < N - 1 && face_active_v(x, y)) {
        let nFx = V[u32(idx_v(x - 1, y))] + V[u32(idx_v(x + 1, y))] +
                  V[u32(idx_v(x, y - 1))] + V[u32(idx_v(x, y + 1))];
        vFx = vFx + fx_mul(nFx - 4 * vFx, scaleFx);
    }
    V[u32(i)] = vFx;
}
)";

        static constexpr std::string_view kShaderViscVOdd = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> V : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(2) var<uniform> param : SimParam;

fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

fn d_cell(x : i32, y : i32) -> i32 {
    if (x < 0 || x >= N || y < 0 || y >= N) { return 0; }
    return D[u32(idx_d(x, y))];
}

fn face_active_v(x : i32, y : i32) -> bool {
    return max(d_cell(x, y - 1), d_cell(x, y)) > 3276;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y > N) { return; }
    if (((x + y) & 1) != 1) { return; }
    let i = idx_v(x, y);
    var vFx = V[u32(i)];
    let scaleFx = fx_mul(fx_mul(param.p1, param.p0), 3932160);
    if (y > 0 && y < N && x > 0 && x < N - 1 && face_active_v(x, y)) {
        let nFx = V[u32(idx_v(x - 1, y))] + V[u32(idx_v(x + 1, y))] +
                  V[u32(idx_v(x, y - 1))] + V[u32(idx_v(x, y + 1))];
        vFx = vFx + fx_mul(nFx - 4 * vFx, scaleFx);
    }
    V[u32(i)] = vFx;
}
)";

        static constexpr std::string_view kShaderViscWallU = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read> D : array<i32>;
@group(0) @binding(2) var<storage, read_write> U : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(3) var<uniform> param : SimParam;

fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }

fn cell_active(cx : i32, cy : i32) -> bool {
    if (cx < 0 || cx >= N || cy < 0 || cy >= N) { return false; }
    return D[u32(idx_s(cx, cy))] > 3276;
}

fn cell_near_wall(cx : i32, cy : i32) -> bool {
    if (!cell_active(cx, cy)) { return false; }
    if (cx > 0 && S[u32(idx_s(cx - 1, cy))] != 0u) { return true; }
    if (cx < N - 1 && S[u32(idx_s(cx + 1, cy))] != 0u) { return true; }
    if (cy > 0 && S[u32(idx_s(cx, cy - 1))] != 0u) { return true; }
    if (cy < N - 1 && S[u32(idx_s(cx, cy + 1))] != 0u) { return true; }
    return false;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > N || y < 0 || y >= N) { return; }
    let i = idx_u(x, y);
    var uFx = U[u32(i)];
    var hits : i32 = 0;
    if (cell_near_wall(x - 1, y)) { hits = hits + 1; }
    if (cell_near_wall(x, y)) { hits = hits + 1; }
    if (hits > 0) {
        var dampFx : i32 = 65536;
        for (var h : i32 = 0; h < hits; h = h + 1) {
            dampFx = fx_mul(dampFx, param.p2);
        }
        uFx = fx_mul(uFx, dampFx);
    }
    U[u32(i)] = uFx;
}
)";

        static constexpr std::string_view kShaderViscWallV = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> S : array<u32>;
@group(0) @binding(1) var<storage, read> D : array<i32>;
@group(0) @binding(2) var<storage, read_write> V : array<i32>;
struct SimParam { p0 : i32, p1 : i32, p2 : i32, p3 : i32, p4 : i32, p5 : i32, p6 : i32, p7 : i32 };
@group(0) @binding(3) var<uniform> param : SimParam;

fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }
fn fx_mul(a : i32, b : i32) -> i32 {
    let a_hi = a >> 16;
    let b_hi = b >> 16;
    let a_lo = a & 65535;
    let b_lo = b & 65535;
    let hi = (a_hi * b_hi) << 16;
    let mid = a_hi * b_lo + a_lo * b_hi;
    let lo = i32((u32(a_lo) * u32(b_lo)) >> 16u);
    return hi + mid + lo;
}

fn idx_s(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }

fn cell_active(cx : i32, cy : i32) -> bool {
    if (cx < 0 || cx >= N || cy < 0 || cy >= N) { return false; }
    return D[u32(idx_s(cx, cy))] > 3276;
}

fn cell_near_wall(cx : i32, cy : i32) -> bool {
    if (!cell_active(cx, cy)) { return false; }
    if (cx > 0 && S[u32(idx_s(cx - 1, cy))] != 0u) { return true; }
    if (cx < N - 1 && S[u32(idx_s(cx + 1, cy))] != 0u) { return true; }
    if (cy > 0 && S[u32(idx_s(cx, cy - 1))] != 0u) { return true; }
    if (cy < N - 1 && S[u32(idx_s(cx, cy + 1))] != 0u) { return true; }
    return false;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y > N) { return; }
    let i = idx_v(x, y);
    var vFx = V[u32(i)];
    var hits : i32 = 0;
    if (cell_near_wall(x, y - 1)) { hits = hits + 1; }
    if (cell_near_wall(x, y)) { hits = hits + 1; }
    if (hits > 0) {
        var dampFx : i32 = 65536;
        for (var h : i32 = 0; h < hits; h = h + 1) {
            dampFx = fx_mul(dampFx, param.p2);
        }
        vFx = fx_mul(vFx, dampFx);
    }
    V[u32(i)] = vFx;
}
)";

        static constexpr std::string_view kShaderExtrapCellEven = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> U : array<i32>;
@group(0) @binding(2) var<storage, read_write> V : array<i32>;

    fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
    fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }
fn d_cell(x : i32, y : i32) -> i32 {
    if (x < 0 || x >= N || y < 0 || y >= N) { return 0; }
    return D[u32(idx_d(x, y))];
}

fn cell_fluid(x : i32, y : i32) -> bool {
    return d_cell(x, y) > 13107;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y >= N) { return; }
    if (((x + y) & 1) != 0) { return; }
    if (d_cell(x, y) >= 6554) { return; }

    var sumUFx : i32 = 0;
    var sumVFx : i32 = 0;
    var cU : i32 = 0;
    var cV : i32 = 0;

    if (x > 0 && cell_fluid(x - 1, y)) {
        sumUFx = sumUFx + U[u32(idx_u(x, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y))];
        cU = cU + 1;
        cV = cV + 1;
    }
    if (x < N - 1 && cell_fluid(x + 1, y)) {
        sumUFx = sumUFx + U[u32(idx_u(x + 1, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y))];
        cU = cU + 1;
        cV = cV + 1;
    }
    if (y > 0 && cell_fluid(x, y - 1)) {
        sumUFx = sumUFx + U[u32(idx_u(x, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y))];
        cU = cU + 1;
        cV = cV + 1;
    }
    if (y < N - 1 && cell_fluid(x, y + 1)) {
        sumUFx = sumUFx + U[u32(idx_u(x, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y + 1))];
        cU = cU + 1;
        cV = cV + 1;
    }

    if (cU > 0 && cV > 0) {
        let aUFx = select((sumUFx - (cU / 2)) / cU, (sumUFx + (cU / 2)) / cU, sumUFx >= 0);
        let aVFx = select((sumVFx - (cV / 2)) / cV, (sumVFx + (cV / 2)) / cV, sumVFx >= 0);
        U[u32(idx_u(x, y))] = aUFx;
        U[u32(idx_u(x + 1, y))] = aUFx;
        V[u32(idx_v(x, y))] = aVFx;
        V[u32(idx_v(x, y + 1))] = aVFx;
    }
}
)";

        static constexpr std::string_view kShaderExtrapCellOdd = R"(
const N : i32 = 200;
@group(0) @binding(0) var<storage, read> D : array<i32>;
@group(0) @binding(1) var<storage, read_write> U : array<i32>;
@group(0) @binding(2) var<storage, read_write> V : array<i32>;

    fn f32_to_fx(v : f32) -> i32 { return i32(round(v * 65536.0)); }
    fn fx_to_f32(v : i32) -> f32 { return f32(v) / 65536.0; }

fn idx_d(x : i32, y : i32) -> i32 { return x + y * N; }
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }
fn d_cell(x : i32, y : i32) -> i32 {
    if (x < 0 || x >= N || y < 0 || y >= N) { return 0; }
    return D[u32(idx_d(x, y))];
}

fn cell_fluid(x : i32, y : i32) -> bool {
    return d_cell(x, y) > 13107;
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y >= N) { return; }
    if (((x + y) & 1) != 1) { return; }
    if (d_cell(x, y) >= 6554) { return; }

    var sumUFx : i32 = 0;
    var sumVFx : i32 = 0;
    var cU : i32 = 0;
    var cV : i32 = 0;

    if (x > 0 && cell_fluid(x - 1, y)) {
        sumUFx = sumUFx + U[u32(idx_u(x, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y))];
        cU = cU + 1;
        cV = cV + 1;
    }
    if (x < N - 1 && cell_fluid(x + 1, y)) {
        sumUFx = sumUFx + U[u32(idx_u(x + 1, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y))];
        cU = cU + 1;
        cV = cV + 1;
    }
    if (y > 0 && cell_fluid(x, y - 1)) {
        sumUFx = sumUFx + U[u32(idx_u(x, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y))];
        cU = cU + 1;
        cV = cV + 1;
    }
    if (y < N - 1 && cell_fluid(x, y + 1)) {
        sumUFx = sumUFx + U[u32(idx_u(x, y))];
        sumVFx = sumVFx + V[u32(idx_v(x, y + 1))];
        cU = cU + 1;
        cV = cV + 1;
    }

    if (cU > 0 && cV > 0) {
        let aUFx = select((sumUFx - (cU / 2)) / cU, (sumUFx + (cU / 2)) / cU, sumUFx >= 0);
        let aVFx = select((sumVFx - (cV / 2)) / cV, (sumVFx + (cV / 2)) / cV, sumVFx >= 0);
        U[u32(idx_u(x, y))] = aUFx;
        U[u32(idx_u(x + 1, y))] = aUFx;
        V[u32(idx_v(x, y))] = aVFx;
        V[u32(idx_v(x, y + 1))] = aVFx;
    }
}
)";

        static constexpr std::string_view kShaderClampU = R"(
const N : i32 = 200;
const MAXV_FX : i32 = 524288;
@group(0) @binding(0) var<storage, read> U : array<i32>;
@group(0) @binding(1) var<storage, read_write> outU : array<i32>;
fn idx_u(x : i32, y : i32) -> i32 { return x + y * (N + 1); }
@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x > N || y < 0 || y >= N) { return; }
    var uFx = U[u32(idx_u(x, y))];
    if (abs(uFx) > MAXV_FX) { uFx = select(-MAXV_FX, MAXV_FX, uFx > 0); }
    outU[u32(idx_u(x, y))] = uFx;
}
)";

        static constexpr std::string_view kShaderClampV = R"(
const N : i32 = 200;
const MAXV_FX : i32 = 524288;
@group(0) @binding(0) var<storage, read> V : array<i32>;
@group(0) @binding(1) var<storage, read_write> outV : array<i32>;
fn idx_v(x : i32, y : i32) -> i32 { return x + y * N; }
@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let x = i32(gid.x);
    let y = i32(gid.y);
    if (x < 0 || x >= N || y < 0 || y > N) { return; }
    var vFx = V[u32(idx_v(x, y))];
    if (abs(vFx) > MAXV_FX) { vFx = select(-MAXV_FX, MAXV_FX, vFx > 0); }
    outV[u32(idx_v(x, y))] = vFx;
}
)";

        const auto mk_buf = [&](std::string_view label, std::size_t size, wgpu::BufferUsage usage) {
            return device.createBuffer(wgpu::BufferDescriptor().setLabel(label).setSize(size).setUsage(usage));
        };

        d_buf = mk_buf("liquid_gpu_d", kDBytes, wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst);
        s_buf = mk_buf("liquid_gpu_s", kSBytes, wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst);
        u_buf = mk_buf("liquid_gpu_u", kUBytes,
                       wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eCopySrc);
        v_buf = mk_buf("liquid_gpu_v", kVBytes,
                       wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eCopySrc);
        p_buf = mk_buf("liquid_gpu_p", kDBytes,
                       wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eCopySrc);
        advect_u_out_buf =
            mk_buf("liquid_gpu_advect_u", kUBytes, wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc);
        advect_v_out_buf =
            mk_buf("liquid_gpu_advect_v", kVBytes, wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc);
        density_out_buf =
            mk_buf("liquid_gpu_density_out", kDBytes, wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc);
        advect_param_buf =
            mk_buf("liquid_gpu_advect_param", kParamBytes, wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst);
        readback_u = mk_buf("liquid_gpu_rb_u", kUBytes, wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst);
        readback_v = mk_buf("liquid_gpu_rb_v", kVBytes, wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst);
        readback_d = mk_buf("liquid_gpu_rb_d", kDBytes, wgpu::BufferUsage::eMapRead | wgpu::BufferUsage::eCopyDst);

        auto make_module = [&](std::string_view code) {
            wgpu::ShaderModuleDescriptor sm_desc;
            wgpu::ShaderSourceWGSL wgsl_desc;
            wgsl_desc.chain.sType = wgpu::SType::eShaderSourceWGSL;
            wgsl_desc.code        = code;
            sm_desc.setNextInChain(std::move(wgsl_desc));
            return device.createShaderModule(sm_desc);
        };

        const auto m_even             = make_module(kShaderEven);
        const auto m_odd              = make_module(kShaderOdd);
        const auto m_adv_u            = make_module(kShaderAdvectU);
        const auto m_adv_v            = make_module(kShaderAdvectV);
        const auto m_density          = make_module(kShaderDensity);
        const auto m_gravity          = make_module(kShaderGravity);
        const auto m_post_u           = make_module(kShaderPostU);
        const auto m_post_v           = make_module(kShaderPostV);
        const auto m_surface_u        = make_module(kShaderSurfaceU);
        const auto m_surface_v        = make_module(kShaderSurfaceV);
        const auto m_visc_u_even      = make_module(kShaderViscUEven);
        const auto m_visc_u_odd       = make_module(kShaderViscUOdd);
        const auto m_visc_v_even      = make_module(kShaderViscVEven);
        const auto m_visc_v_odd       = make_module(kShaderViscVOdd);
        const auto m_visc_wall_u      = make_module(kShaderViscWallU);
        const auto m_visc_wall_v      = make_module(kShaderViscWallV);
        const auto m_extrap_cell_even = make_module(kShaderExtrapCellEven);
        const auto m_extrap_cell_odd  = make_module(kShaderExtrapCellOdd);
        const auto m_clamp_u          = make_module(kShaderClampU);
        const auto m_clamp_v          = make_module(kShaderClampV);

        wgpu::ComputePipelineDescriptor cp_desc;
        cp_desc.setLabel("LiquidPressureProjectEven")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_even).setEntryPoint("main"));
        pipeline_even = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidPressureProjectOdd")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_odd).setEntryPoint("main"));
        pipeline_odd = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidAdvectU")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_adv_u).setEntryPoint("main"));
        advect_u_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidAdvectV")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_adv_v).setEntryPoint("main"));
        advect_v_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidDensityTransport")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_density).setEntryPoint("main"));
        density_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidGravity")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_gravity).setEntryPoint("main"));
        gravity_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidPostU")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_post_u).setEntryPoint("main"));
        post_u_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidPostV")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_post_v).setEntryPoint("main"));
        post_v_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidSurfaceU")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_surface_u).setEntryPoint("main"));
        surface_u_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidSurfaceV")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_surface_v).setEntryPoint("main"));
        surface_v_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidViscosityUEven")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_visc_u_even).setEntryPoint("main"));
        visc_u_even_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidViscosityUOdd")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_visc_u_odd).setEntryPoint("main"));
        visc_u_odd_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidViscosityVEven")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_visc_v_even).setEntryPoint("main"));
        visc_v_even_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidViscosityVOdd")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_visc_v_odd).setEntryPoint("main"));
        visc_v_odd_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidViscosityWallU")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_visc_wall_u).setEntryPoint("main"));
        visc_wall_u_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidViscosityWallV")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_visc_wall_v).setEntryPoint("main"));
        visc_wall_v_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidExtrapolateCellEven")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_extrap_cell_even).setEntryPoint("main"));
        extrap_cell_even_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidExtrapolateCellOdd")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_extrap_cell_odd).setEntryPoint("main"));
        extrap_cell_odd_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidClampU")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_clamp_u).setEntryPoint("main"));
        clamp_u_pipeline = device.createComputePipeline(cp_desc);

        cp_desc.setLabel("LiquidClampV")
            .setCompute(wgpu::ProgrammableStageDescriptor().setModule(m_clamp_v).setEntryPoint("main"));
        clamp_v_pipeline = device.createComputePipeline(cp_desc);

        const auto make_bg = [&](const wgpu::ComputePipeline& p, std::string_view label) {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel(label)
                    .setLayout(p.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                        wgpu::BindGroupEntry().setBinding(1).setBuffer(s_buf).setSize(kSBytes),
                        wgpu::BindGroupEntry().setBinding(2).setBuffer(u_buf).setSize(kUBytes),
                        wgpu::BindGroupEntry().setBinding(3).setBuffer(v_buf).setSize(kVBytes),
                        wgpu::BindGroupEntry().setBinding(4).setBuffer(p_buf).setSize(kDBytes),
                    }));
        };

        bind_group_even = make_bg(pipeline_even, "LiquidPressureProjectBGEven");
        bind_group_odd  = make_bg(pipeline_odd, "LiquidPressureProjectBGOdd");

        const auto make_adv_u_bg = [&]() {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel("LiquidAdvectUBG")
                    .setLayout(advect_u_pipeline.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                        wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                        wgpu::BindGroupEntry().setBinding(2).setBuffer(v_buf).setSize(kVBytes),
                        wgpu::BindGroupEntry().setBinding(3).setBuffer(advect_u_out_buf).setSize(kUBytes),
                        wgpu::BindGroupEntry().setBinding(4).setBuffer(advect_param_buf).setSize(kParamBytes),
                    }));
        };

        const auto make_adv_v_bg = [&]() {
            return device.createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel("LiquidAdvectVBG")
                    .setLayout(advect_v_pipeline.getBindGroupLayout(0))
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                        wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                        wgpu::BindGroupEntry().setBinding(2).setBuffer(v_buf).setSize(kVBytes),
                        wgpu::BindGroupEntry().setBinding(3).setBuffer(advect_v_out_buf).setSize(kVBytes),
                        wgpu::BindGroupEntry().setBinding(4).setBuffer(advect_param_buf).setSize(kParamBytes),
                    }));
        };

        advect_u_bind_group = make_adv_u_bg();
        advect_v_bind_group = make_adv_v_bg();

        density_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidDensityTransportBG")
                .setLayout(density_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(u_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(3).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(4).setBuffer(density_out_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(5).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        gravity_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidGravityBG")
                .setLayout(gravity_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        post_u_bind_group =
            device.createBindGroup(wgpu::BindGroupDescriptor()
                                       .setLabel("LiquidPostUBG")
                                       .setLayout(post_u_pipeline.getBindGroupLayout(0))
                                       .setEntries(std::array{
                                           wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                                           wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                                       }));

        post_v_bind_group =
            device.createBindGroup(wgpu::BindGroupDescriptor()
                                       .setLabel("LiquidPostVBG")
                                       .setLayout(post_v_pipeline.getBindGroupLayout(0))
                                       .setEntries(std::array{
                                           wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                                           wgpu::BindGroupEntry().setBinding(1).setBuffer(v_buf).setSize(kVBytes),
                                       }));

        surface_u_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidSurfaceUBG")
                .setLayout(surface_u_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_u_out_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(3).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        surface_v_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidSurfaceVBG")
                .setLayout(surface_v_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_v_out_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(3).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        visc_u_even_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidViscUEvenBG")
                .setLayout(visc_u_even_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        visc_u_odd_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidViscUOddBG")
                .setLayout(visc_u_odd_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        visc_v_even_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidViscVEvenBG")
                .setLayout(visc_v_even_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        visc_v_odd_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidViscVOddBG")
                .setLayout(visc_v_odd_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        visc_wall_u_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidViscWallUBG")
                .setLayout(visc_wall_u_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(u_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(3).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        visc_wall_v_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidViscWallVBG")
                .setLayout(visc_wall_v_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(s_buf).setSize(kSBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(d_buf).setSize(kDBytes),
                    wgpu::BindGroupEntry().setBinding(2).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(3).setBuffer(advect_param_buf).setSize(kParamBytes),
                }));

        extrap_cell_even_bind_group =
            device.createBindGroup(wgpu::BindGroupDescriptor()
                                       .setLabel("LiquidExtrapCellEvenBG")
                                       .setLayout(extrap_cell_even_pipeline.getBindGroupLayout(0))
                                       .setEntries(std::array{
                                           wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                                           wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                                           wgpu::BindGroupEntry().setBinding(2).setBuffer(v_buf).setSize(kVBytes),
                                       }));

        extrap_cell_odd_bind_group =
            device.createBindGroup(wgpu::BindGroupDescriptor()
                                       .setLabel("LiquidExtrapCellOddBG")
                                       .setLayout(extrap_cell_odd_pipeline.getBindGroupLayout(0))
                                       .setEntries(std::array{
                                           wgpu::BindGroupEntry().setBinding(0).setBuffer(d_buf).setSize(kDBytes),
                                           wgpu::BindGroupEntry().setBinding(1).setBuffer(u_buf).setSize(kUBytes),
                                           wgpu::BindGroupEntry().setBinding(2).setBuffer(v_buf).setSize(kVBytes),
                                       }));

        clamp_u_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidClampUBG")
                .setLayout(clamp_u_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(u_buf).setSize(kUBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(advect_u_out_buf).setSize(kUBytes),
                }));

        clamp_v_bind_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("LiquidClampVBG")
                .setLayout(clamp_v_pipeline.getBindGroupLayout(0))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(v_buf).setSize(kVBytes),
                    wgpu::BindGroupEntry().setBinding(1).setBuffer(advect_v_out_buf).setSize(kVBytes),
                }));

        ready = static_cast<bool>(pipeline_even) && static_cast<bool>(pipeline_odd) &&
                static_cast<bool>(bind_group_even) && static_cast<bool>(bind_group_odd) &&
                static_cast<bool>(advect_u_pipeline) && static_cast<bool>(advect_v_pipeline) &&
                static_cast<bool>(advect_u_bind_group) && static_cast<bool>(advect_v_bind_group) &&
                static_cast<bool>(density_pipeline) && static_cast<bool>(density_bind_group) &&
                static_cast<bool>(gravity_pipeline) && static_cast<bool>(gravity_bind_group) &&
                static_cast<bool>(post_u_pipeline) && static_cast<bool>(post_v_pipeline) &&
                static_cast<bool>(post_u_bind_group) && static_cast<bool>(post_v_bind_group) &&
                static_cast<bool>(surface_u_pipeline) && static_cast<bool>(surface_v_pipeline) &&
                static_cast<bool>(surface_u_bind_group) && static_cast<bool>(surface_v_bind_group) &&
                static_cast<bool>(visc_u_even_pipeline) && static_cast<bool>(visc_u_odd_pipeline) &&
                static_cast<bool>(visc_v_even_pipeline) && static_cast<bool>(visc_v_odd_pipeline) &&
                static_cast<bool>(visc_wall_u_pipeline) && static_cast<bool>(visc_wall_v_pipeline) &&
                static_cast<bool>(visc_u_even_bind_group) && static_cast<bool>(visc_u_odd_bind_group) &&
                static_cast<bool>(visc_v_even_bind_group) && static_cast<bool>(visc_v_odd_bind_group) &&
                static_cast<bool>(visc_wall_u_bind_group) && static_cast<bool>(visc_wall_v_bind_group) &&
                static_cast<bool>(extrap_cell_even_pipeline) && static_cast<bool>(extrap_cell_odd_pipeline) &&
                static_cast<bool>(extrap_cell_even_bind_group) && static_cast<bool>(extrap_cell_odd_bind_group) &&
                static_cast<bool>(clamp_u_pipeline) && static_cast<bool>(clamp_v_pipeline) &&
                static_cast<bool>(clamp_u_bind_group) && static_cast<bool>(clamp_v_bind_group);
    }

    void upload_from_sim(Fluid& sim, const wgpu::Queue& queue) {
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                const std::size_t i = static_cast<std::size_t>(x + y * kN);
                host_d[i]           = sim.D.get({Fluid::u32(x), Fluid::u32(y)}).transform(Fluid::deref).value_or(0);
                host_s[i]           = static_cast<std::uint32_t>(sim.getS(x, y));
                host_p[i]           = static_cast<fx32>(0);
            }
        }

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x <= kN; ++x) {
                host_u[static_cast<std::size_t>(x + y * (kN + 1))] =
                    sim.U.get({Fluid::u32(x), Fluid::u32(y)}).transform(Fluid::deref).value_or(0);
            }
        }

        for (int y = 0; y <= kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                host_v[static_cast<std::size_t>(x + y * kN)] =
                    sim.V.get({Fluid::u32(x), Fluid::u32(y)}).transform(Fluid::deref).value_or(0);
            }
        }

        queue.writeBuffer(d_buf, 0, host_d.data(), kDBytes);
        queue.writeBuffer(s_buf, 0, host_s.data(), kSBytes);
        queue.writeBuffer(u_buf, 0, host_u.data(), kUBytes);
        queue.writeBuffer(v_buf, 0, host_v.data(), kVBytes);
        queue.writeBuffer(p_buf, 0, host_p.data(), kDBytes);
    }

    void dispatch_project(const wgpu::Device& device, const wgpu::Queue& queue) {
        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidPressureCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;
            const uint32_t gx      = (static_cast<uint32_t>(kN) + kWG - 1) / kWG;
            const uint32_t gy      = (static_cast<uint32_t>(kN) + kWG - 1) / kWG;
            static bool even_first = true;
            for (int k = 0; k < kIter; ++k) {
                pass.setPipeline(even_first ? pipeline_even : pipeline_odd);
                pass.setBindGroup(0, even_first ? bind_group_even : bind_group_odd, std::span<const uint32_t>{});
                pass.dispatchWorkgroups(gx, gy, 1);
                pass.setPipeline(even_first ? pipeline_odd : pipeline_even);
                pass.setBindGroup(0, even_first ? bind_group_odd : bind_group_even, std::span<const uint32_t>{});
                pass.dispatchWorkgroups(gx, gy, 1);
            }
            even_first = !even_first;
            pass.end();
        }
        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_advect(const wgpu::Device& device, const wgpu::Queue& queue, fx32 dt_fx) {
        std::array<fx32, 8> params{dt_fx, 0, 0, 0, 0, 0, 0, 0};
        queue.writeBuffer(advect_param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidAdvectCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;

            pass.setPipeline(advect_u_pipeline);
            pass.setBindGroup(0, advect_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(advect_v_pipeline);
            pass.setBindGroup(0, advect_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(advect_u_out_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(advect_v_out_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_density(const wgpu::Device& device, const wgpu::Queue& queue, fx32 dt_fx) {
        std::array<fx32, 8> params{dt_fx, 0, 0, 0, 0, 0, 0, 0};
        queue.writeBuffer(advect_param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidDensityCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;
            pass.setPipeline(density_pipeline);
            pass.setBindGroup(0, density_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(density_out_buf, 0, readback_d, 0, kDBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_gravity(const wgpu::Device& device, const wgpu::Queue& queue, fx32 dt_fx) {
        std::array<fx32, 8> params{dt_fx, 0, 0, 0, 0, 0, 0, 0};
        queue.writeBuffer(advect_param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidGravityCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;
            pass.setPipeline(gravity_pipeline);
            pass.setBindGroup(0, gravity_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_postprocess(const wgpu::Device& device, const wgpu::Queue& queue) {
        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidPostVelCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;

            pass.setPipeline(post_u_pipeline);
            pass.setBindGroup(0, post_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(post_v_pipeline);
            pass.setBindGroup(0, post_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_surface_tension(const wgpu::Device& device, const wgpu::Queue& queue, fx32 dt_fx) {
        std::array<fx32, 8> params{dt_fx, 0, 0, 0, 0, 0, 0, 0};
        queue.writeBuffer(advect_param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidSurfaceCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;

            pass.setPipeline(surface_u_pipeline);
            pass.setBindGroup(0, surface_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(surface_v_pipeline);
            pass.setBindGroup(0, surface_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(advect_u_out_buf, 0, u_buf, 0, kUBytes);
        encoder.copyBufferToBuffer(advect_v_out_buf, 0, v_buf, 0, kVBytes);
        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_viscosity(const wgpu::Device& device, const wgpu::Queue& queue, fx32 sticky_fx, fx32 dt_fx) {
        const int iterations = std::max(0, static_cast<int>(((sticky_fx * 4) + (kFxOne / 2)) / kFxOne));
        const fx32 nu_fx     = std::min(kFxHalf, fx_mul(sticky_fx, kFx005));
        const fx32 wall_fx =
            std::clamp<fx32>(static_cast<fx32>(kFxOne - fx_mul(sticky_fx, kFx008)), static_cast<fx32>(0), kFxOne);

        std::array<fx32, 8> params{dt_fx, nu_fx, wall_fx, 0, 0, 0, 0, 0};
        queue.writeBuffer(advect_param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidViscosityCompute"));
        constexpr uint32_t kWG = 16;
        for (int k = 0; k < iterations && nu_fx > 0; ++k) {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(visc_u_even_pipeline);
            pass.setBindGroup(0, visc_u_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_u_odd_pipeline);
            pass.setBindGroup(0, visc_u_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_v_even_pipeline);
            pass.setBindGroup(0, visc_v_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_v_odd_pipeline);
            pass.setBindGroup(0, visc_v_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        if (wall_fx < kFxOne) {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(visc_wall_u_pipeline);
            pass.setBindGroup(0, visc_wall_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_wall_v_pipeline);
            pass.setBindGroup(0, visc_wall_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_extrapolate(const wgpu::Device& device, const wgpu::Queue& queue) {
        auto encoder =
            device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidExtrapolateCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;

            pass.setPipeline(extrap_cell_even_pipeline);
            pass.setBindGroup(0, extrap_cell_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(extrap_cell_odd_pipeline);
            pass.setBindGroup(0, extrap_cell_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    void dispatch_clamp(const wgpu::Device& device, const wgpu::Queue& queue) {
        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidClampCompute"));
        {
            auto pass              = encoder.beginComputePass();
            constexpr uint32_t kWG = 16;

            pass.setPipeline(clamp_u_pipeline);
            pass.setBindGroup(0, clamp_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(clamp_v_pipeline);
            pass.setBindGroup(0, clamp_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(advect_u_out_buf, 0, u_buf, 0, kUBytes);
        encoder.copyBufferToBuffer(advect_v_out_buf, 0, v_buf, 0, kVBytes);
        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        queue.submit(encoder.finish());
    }

    bool readback_to_sim(Fluid& sim, const wgpu::Device& device) {
        bool done_u = false;
        bool done_v = false;
        bool ok_u   = false;
        bool ok_v   = false;

        (void)readback_u.mapAsync(
            wgpu::MapMode::eRead, 0, kUBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok_u   = status == wgpu::MapAsyncStatus::eSuccess;
                    done_u = true;
                })));
        (void)readback_v.mapAsync(
            wgpu::MapMode::eRead, 0, kVBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok_v   = status == wgpu::MapAsyncStatus::eSuccess;
                    done_v = true;
                })));

        while (!(done_u && done_v)) {
            (void)device.poll(true);
        }

        bool copied_u = false;
        bool copied_v = false;

        if (ok_u) {
            if (const void* ptr = readback_u.getConstMappedRange(0, kUBytes); ptr != nullptr) {
                const auto* iptr = static_cast<const fx32*>(ptr);
                for (std::size_t i = 0; i < host_u.size(); ++i) {
                    host_u[i] = iptr[i];
                }
                copied_u = true;
            }
        }
        if (ok_v) {
            if (const void* ptr = readback_v.getConstMappedRange(0, kVBytes); ptr != nullptr) {
                const auto* iptr = static_cast<const fx32*>(ptr);
                for (std::size_t i = 0; i < host_v.size(); ++i) {
                    host_v[i] = iptr[i];
                }
                copied_v = true;
            }
        }

        readback_u.unmap();
        readback_v.unmap();

        if (!(copied_u && copied_v)) return false;

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x <= kN; ++x) {
                (void)sim.U.set({Fluid::u32(x), Fluid::u32(y)}, host_u[static_cast<std::size_t>(x + y * (kN + 1))]);
            }
        }
        for (int y = 0; y <= kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                (void)sim.V.set({Fluid::u32(x), Fluid::u32(y)}, host_v[static_cast<std::size_t>(x + y * kN)]);
            }
        }
        return true;
    }

    bool readback_density_to_sim(Fluid& sim, const wgpu::Device& device) {
        bool done = false;
        bool ok   = false;
        (void)readback_d.mapAsync(
            wgpu::MapMode::eRead, 0, kDBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok   = status == wgpu::MapAsyncStatus::eSuccess;
                    done = true;
                })));

        while (!done) {
            (void)device.poll(true);
        }

        bool copied = false;
        if (ok) {
            if (const void* ptr = readback_d.getConstMappedRange(0, kDBytes); ptr != nullptr) {
                const auto* iptr = static_cast<const fx32*>(ptr);
                for (std::size_t i = 0; i < host_d.size(); ++i) {
                    host_d[i] = iptr[i];
                }
                copied = true;
            }
        }
        readback_d.unmap();

        if (!copied) return false;

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                sim.setD(x, y, fx_to_float(host_d[static_cast<std::size_t>(x + y * kN)]));
            }
        }
        return true;
    }

    bool readback_all_to_sim(Fluid& sim, const wgpu::Device& device) {
        bool done_u = false;
        bool done_v = false;
        bool done_d = false;
        bool ok_u   = false;
        bool ok_v   = false;
        bool ok_d   = false;

        (void)readback_u.mapAsync(
            wgpu::MapMode::eRead, 0, kUBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok_u   = status == wgpu::MapAsyncStatus::eSuccess;
                    done_u = true;
                })));
        (void)readback_v.mapAsync(
            wgpu::MapMode::eRead, 0, kVBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok_v   = status == wgpu::MapAsyncStatus::eSuccess;
                    done_v = true;
                })));
        (void)readback_d.mapAsync(
            wgpu::MapMode::eRead, 0, kDBytes,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    ok_d   = status == wgpu::MapAsyncStatus::eSuccess;
                    done_d = true;
                })));

        while (!(done_u && done_v && done_d)) {
            (void)device.poll(true);
        }

        bool copied_u = false;
        bool copied_v = false;
        bool copied_d = false;

        if (ok_u) {
            if (const void* ptr = readback_u.getConstMappedRange(0, kUBytes); ptr != nullptr) {
                const auto* iptr = static_cast<const fx32*>(ptr);
                for (std::size_t i = 0; i < host_u.size(); ++i) {
                    host_u[i] = iptr[i];
                }
                copied_u = true;
            }
        }
        if (ok_v) {
            if (const void* ptr = readback_v.getConstMappedRange(0, kVBytes); ptr != nullptr) {
                const auto* iptr = static_cast<const fx32*>(ptr);
                for (std::size_t i = 0; i < host_v.size(); ++i) {
                    host_v[i] = iptr[i];
                }
                copied_v = true;
            }
        }
        if (ok_d) {
            if (const void* ptr = readback_d.getConstMappedRange(0, kDBytes); ptr != nullptr) {
                const auto* iptr = static_cast<const fx32*>(ptr);
                for (std::size_t i = 0; i < host_d.size(); ++i) {
                    host_d[i] = iptr[i];
                }
                copied_d = true;
            }
        }

        readback_u.unmap();
        readback_v.unmap();
        readback_d.unmap();

        if (!(copied_u && copied_v && copied_d)) return false;

        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x <= kN; ++x) {
                (void)sim.U.set({Fluid::u32(x), Fluid::u32(y)}, host_u[static_cast<std::size_t>(x + y * (kN + 1))]);
            }
        }
        for (int y = 0; y <= kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                (void)sim.V.set({Fluid::u32(x), Fluid::u32(y)}, host_v[static_cast<std::size_t>(x + y * kN)]);
            }
        }
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                sim.setD(x, y, fx_to_float(host_d[static_cast<std::size_t>(x + y * kN)]));
            }
        }

        return true;
    }

    void dispatch_full_step_chain(const wgpu::Device& device, const wgpu::Queue& queue, fx32 dt_fx, fx32 sticky_fx) {
        const int iterations = std::max(0, static_cast<int>(((sticky_fx * 4) + (kFxOne / 2)) / kFxOne));
        const fx32 nu_fx     = std::min(kFxHalf, fx_mul(sticky_fx, kFx005));
        const fx32 wall_fx =
            std::clamp<fx32>(static_cast<fx32>(kFxOne - fx_mul(sticky_fx, kFx008)), static_cast<fx32>(0), kFxOne);

        std::array<fx32, 8> params{dt_fx, nu_fx, wall_fx, 0, 0, 0, 0, 0};
        queue.writeBuffer(advect_param_buf, 0, params.data(), kParamBytes);

        auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("LiquidFullStepChain"));
        constexpr uint32_t kWG = 16;

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(gravity_pipeline);
            pass.setBindGroup(0, gravity_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(surface_u_pipeline);
            pass.setBindGroup(0, surface_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(surface_v_pipeline);
            pass.setBindGroup(0, surface_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(advect_u_out_buf, 0, u_buf, 0, kUBytes);
        encoder.copyBufferToBuffer(advect_v_out_buf, 0, v_buf, 0, kVBytes);

        for (int k = 0; k < iterations && nu_fx > 0; ++k) {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(visc_u_even_pipeline);
            pass.setBindGroup(0, visc_u_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_u_odd_pipeline);
            pass.setBindGroup(0, visc_u_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_v_even_pipeline);
            pass.setBindGroup(0, visc_v_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_v_odd_pipeline);
            pass.setBindGroup(0, visc_v_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        if (wall_fx < kFxOne) {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(visc_wall_u_pipeline);
            pass.setBindGroup(0, visc_wall_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(visc_wall_v_pipeline);
            pass.setBindGroup(0, visc_wall_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        {
            auto pass              = encoder.beginComputePass();
            const uint32_t gx      = (static_cast<uint32_t>(kN) + kWG - 1) / kWG;
            const uint32_t gy      = (static_cast<uint32_t>(kN) + kWG - 1) / kWG;
            static bool even_first = true;
            for (int k = 0; k < kIter; ++k) {
                pass.setPipeline(even_first ? pipeline_even : pipeline_odd);
                pass.setBindGroup(0, even_first ? bind_group_even : bind_group_odd, std::span<const uint32_t>{});
                pass.dispatchWorkgroups(gx, gy, 1);
                pass.setPipeline(even_first ? pipeline_odd : pipeline_even);
                pass.setBindGroup(0, even_first ? bind_group_odd : bind_group_even, std::span<const uint32_t>{});
                pass.dispatchWorkgroups(gx, gy, 1);
            }
            even_first = !even_first;
            pass.end();
        }

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(extrap_cell_even_pipeline);
            pass.setBindGroup(0, extrap_cell_even_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(extrap_cell_odd_pipeline);
            pass.setBindGroup(0, extrap_cell_odd_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);
            pass.end();
        }

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(clamp_u_pipeline);
            pass.setBindGroup(0, clamp_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(clamp_v_pipeline);
            pass.setBindGroup(0, clamp_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(advect_u_out_buf, 0, u_buf, 0, kUBytes);
        encoder.copyBufferToBuffer(advect_v_out_buf, 0, v_buf, 0, kVBytes);

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(advect_u_pipeline);
            pass.setBindGroup(0, advect_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(advect_v_pipeline);
            pass.setBindGroup(0, advect_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }
        encoder.copyBufferToBuffer(advect_u_out_buf, 0, u_buf, 0, kUBytes);
        encoder.copyBufferToBuffer(advect_v_out_buf, 0, v_buf, 0, kVBytes);

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(density_pipeline);
            pass.setBindGroup(0, density_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);
            pass.end();
        }

        {
            auto pass = encoder.beginComputePass();
            pass.setPipeline(post_u_pipeline);
            pass.setBindGroup(0, post_u_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN) + kWG - 1) / kWG, 1);

            pass.setPipeline(post_v_pipeline);
            pass.setBindGroup(0, post_v_bind_group, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((static_cast<uint32_t>(kN) + kWG - 1) / kWG,
                                    (static_cast<uint32_t>(kN + 1) + kWG - 1) / kWG, 1);
            pass.end();
        }

        encoder.copyBufferToBuffer(u_buf, 0, readback_u, 0, kUBytes);
        encoder.copyBufferToBuffer(v_buf, 0, readback_v, 0, kVBytes);
        encoder.copyBufferToBuffer(density_out_buf, 0, readback_d, 0, kDBytes);
        queue.submit(encoder.finish());
    }
};

bool gpu_pressure_project(Fluid& sim,
                          GpuPressureProjector* projector,
                          const wgpu::Device* device,
                          const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_project(*device, *queue);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_advect_velocity_rk2(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_advect(*device, *queue, dt_fx);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_density_transport(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_density(*device, *queue, dt_fx);
    return projector->readback_density_to_sim(sim, *device);
}

bool gpu_add_gravity(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_gravity(*device, *queue, dt_fx);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_postprocess_velocities(Fluid& sim,
                                GpuPressureProjector* projector,
                                const wgpu::Device* device,
                                const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_postprocess(*device, *queue);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_surface_tension(
    Fluid& sim, fx32 dt_fx, GpuPressureProjector* projector, const wgpu::Device* device, const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_surface_tension(*device, *queue, dt_fx);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_viscosity(Fluid& sim,
                   fx32 sticky_fx,
                   fx32 dt_fx,
                   GpuPressureProjector* projector,
                   const wgpu::Device* device,
                   const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_viscosity(*device, *queue, sticky_fx, dt_fx);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_extrapolate_velocity(Fluid& sim,
                              GpuPressureProjector* projector,
                              const wgpu::Device* device,
                              const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_extrapolate(*device, *queue);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_clamp_velocities(Fluid& sim,
                          GpuPressureProjector* projector,
                          const wgpu::Device* device,
                          const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
    if (!projector->ready) return false;
    projector->upload_from_sim(sim, *queue);
    projector->dispatch_clamp(*device, *queue);
    return projector->readback_to_sim(sim, *device);
}

bool gpu_run_full_step_chain(Fluid& sim,
                             fx32 dt_fx,
                             fx32 sticky_fx,
                             GpuPressureProjector* projector,
                             const wgpu::Device* device,
                             const wgpu::Queue* queue) {
    if (projector == nullptr || device == nullptr || queue == nullptr) return false;
    projector->init(*device);
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

struct Plugin {
    void finish(core::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        Fluid sim;
        sim.reset();

        auto thread_pool  = std::make_unique<BS::thread_pool<>>(std::max(1u, std::thread::hardware_concurrency()));
        auto gpu_pressure = std::make_unique<GpuPressureProjector>();
        auto mesh_handle  = mesh_assets.emplace(build_mesh(sim, thread_pool.get()));

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
                                     std::addressof(*queue));
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
        .add_plugins(Plugin{});

    app.run();
}
