// 4D Voxel Path Tracer — example demonstrating a 4-dimensional voxel scene.
// Uses dense_grid<4> + svo_upload() for scene storage, custom render-graph
// sub-graph (V4DGraph) with a 4D perspective camera defined by explicit
// pos/forward/right/up/over float4 vectors (no mat5x5 in the API).

#include <imgui.h>
#include <spdlog/spdlog.h>

import std;
import glm;
import webgpu;
import epix.core;
import epix.render;
import epix.core_graph;
import epix.transform;
import epix.extension.grid;
import epix.extension.grid_gpu;
import epix.assets;
import epix.shader;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.input;
import epix.time;
import epix.render.imgui;

using namespace epix;
using namespace epix::core;
using namespace epix::ext::grid;
using namespace epix::ext::grid_gpu;
using namespace epix::render;
using namespace epix::render::view;
using namespace epix::render::camera;
using namespace epix::render::graph;
namespace assets = epix::assets;
namespace shader = epix::shader;
namespace tf     = epix::transform;

// ===========================================================================
// Shader sources
// ===========================================================================

// ---------------------------------------------------------------------------
// 4D Trace compute shader.
// group 0: 0=Voxel4DCamera, 1=SVO, 2=colors
// group 1: 0=hdr_tex (rgba16f write), 1=depth_tex (r32f write)
// ---------------------------------------------------------------------------
constexpr std::string_view kV4DTraceSlangPath = "voxel4d/trace.slang";
constexpr std::string_view kV4DTraceSlang     = R"slang(
import epix.ext.grid.svo;

// 4D camera: pos + 4 orthonormal basis vectors + previous frame for TAA
struct Voxel4DCamera {
    float4 pos;
    float4 forward;
    float4 right;
    float4 up;
    float4 over;
    float4 prev_pos;
    float4 prev_forward;
    float4 prev_right;
    float4 prev_up;
    float  fov_y;
    float  aspect;   // width / height
    float  taa_blend;
    uint   frame_index;
};

[[vk::binding(0, 0)]] ConstantBuffer<Voxel4DCamera> camera;
[[vk::binding(1, 0)]] StructuredBuffer<uint>   svo_buf;
[[vk::binding(2, 0)]] StructuredBuffer<float4> colors;
[[vk::binding(0, 1)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> hdr_tex;
[[vk::binding(1, 1)]] [[vk::image_format("r32f")]]    RWTexture2D<float>  depth_tex;

// PCG hash
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
uint seed_for(uint2 px, uint frame, uint idx) {
    return pcg(px.x ^ pcg(px.y ^ pcg(frame * 127u + idx * 31u)));
}
float rand01(uint seed) { return float(pcg(seed)) * (1.0f / 4294967296.0f); }

// Sky colour: uses y-component for vertical gradient, dot with 4D sun dir.
float3 sky_color4(float4 dir) {
    float t      = clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    float3 horiz = float3(0.55f, 0.70f, 0.90f);
    float3 zenit = float3(0.08f, 0.22f, 0.60f);
    float3 sky   = lerp(horiz, zenit, t);
    float4 sun_dir = normalize(float4(0.6f, 1.0f, 0.4f, 0.3f));
    float  cos15   = 0.9659f;
    float  cos18   = 0.9511f;
    float  sun_d   = dot(dir, sun_dir);
    float  disc    = smoothstep(cos18, cos15, sun_d);
    sky += float3(8.0f, 7.0f, 5.5f) * disc;
    return sky;
}

struct RayHit4 {
    int    idx;
    float  t;
    float4 normal;
};

// Convert SvoRayHit axis/sign to float4 normal.
float4 hit_normal4(epix::ext::grid::SvoRayHit<4> h) {
    float4 n = float4(0.0f);
    if (h.hit_axis == 0) n.x = float(h.hit_sign);
    else if (h.hit_axis == 1) n.y = float(h.hit_sign);
    else if (h.hit_axis == 2) n.z = float(h.hit_sign);
    else n.w = float(h.hit_sign);
    return n;
}

// Thin wrapper: call SvoGrid<4,2> member trace_ray and convert to RayHit4.
RayHit4 trace_ray_4d(epix::ext::grid::SvoGrid<4, 2> svo, float4 origin, float4 dir, int max_steps) {
    float[4] o = { origin.x, origin.y, origin.z, origin.w };
    float[4] d = { dir.x, dir.y, dir.z, dir.w };
    epix::ext::grid::SvoRayHit<4> h = svo.trace_ray(o, d, max_steps);
    RayHit4 r;
    r.idx    = h.data_index;
    r.t      = h.t;
    r.normal = (h.data_index >= 0) ? hit_normal4(h) : float4(0.0f, 1.0f, 0.0f, 0.0f);
    return r;
}

// Build the 3 tangent vectors for an axis-aligned 4D surface normal.
void axis_tangent_frame4(float4 N, out float4 T, out float4 B, out float4 C) {
    if      (abs(N.x) > 0.5f) { T = float4(0,1,0,0); B = float4(0,0,1,0); C = float4(0,0,0,1); }
    else if (abs(N.y) > 0.5f) { T = float4(1,0,0,0); B = float4(0,0,1,0); C = float4(0,0,0,1); }
    else if (abs(N.z) > 0.5f) { T = float4(1,0,0,0); B = float4(0,1,0,0); C = float4(0,0,0,1); }
    else                       { T = float4(1,0,0,0); B = float4(0,1,0,0); C = float4(0,0,1,0); }
}

// Pair of Box-Muller normal samples.
float2 box_muller(float u1, float u2) {
    float r = sqrt(-2.0f * log(max(u1, 1e-7f)));
    float a = 6.28318f * u2;
    return float2(r * cos(a), r * sin(a));
}

// Cosine-weighted hemisphere sample in 4D via generalised Malley's method.
// Samples a uniform point in a 3D unit ball then lifts to the 4D hemisphere.
// PDF = cos(theta) / (4*pi/3),  estimate weight = albedo (cos/normalization cancel).
// Requires 5 uniform random values u1..u5.
float4 cosine_hemisphere4(float4 N, float4 T, float4 B, float4 C,
                           float u1, float u2, float u3, float u4, float u5) {
    float2 bm1 = box_muller(u1, u2);
    float2 bm2 = box_muller(u3, u4);
    float3 g   = float3(bm1.x, bm1.y, bm2.x);
    float  gl  = length(g);
    if (gl < 1e-6f) return N;
    float3 dir3 = g / gl;                        // uniform direction on S2
    float  r    = pow(u5, 1.0f / 3.0f);          // uniform radius in 3D ball
    float3 p    = dir3 * r;
    float  ww   = sqrt(max(0.0f, 1.0f - dot(p, p)));
    return normalize(p.x * T + p.y * B + p.z * C + ww * N);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    hdr_tex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    // Build jittered ray in 4D from the 4D perspective camera.
    float jx = rand01(seed_for(id.xy, camera.frame_index, 0u)) - 0.5f;
    float jy = rand01(seed_for(id.xy, camera.frame_index, 1u)) - 0.5f;
    float nx = ((float(id.x) + 0.5f + jx) / float(width))  * 2.0f - 1.0f;
    float ny = ((float(id.y) + 0.5f + jy) / float(height)) * 2.0f - 1.0f;

    float half_h = tan(camera.fov_y * 0.5f);
    float half_w = half_h * camera.aspect;
    float4 ray_orig = camera.pos;
    float4 ray_dir  = normalize(camera.forward + nx * half_w * camera.right
                                               + ny * half_h * camera.up);

    epix::ext::grid::SvoGrid<4, 2> svo = epix::ext::grid::SvoGrid<4, 2>(svo_buf);
    RayHit4 primary = trace_ray_4d(svo, ray_orig, ray_dir, 256);

    float3 radiance;
    if (primary.idx < 0) {
        radiance         = sky_color4(ray_dir);
        depth_tex[id.xy] = 1e30f;
    } else {
        float4 hit_pos = ray_orig + ray_dir * primary.t;
        float3 albedo  = colors[primary.idx].rgb;
        float4 N       = primary.normal;

        float4 T; float4 B; float4 C;
        axis_tangent_frame4(N, T, B, C);

        float u1 = rand01(seed_for(id.xy, camera.frame_index, 2u));
        float u2 = rand01(seed_for(id.xy, camera.frame_index, 3u));
        float u3 = rand01(seed_for(id.xy, camera.frame_index, 4u));
        float u4 = rand01(seed_for(id.xy, camera.frame_index, 5u));
        float u5 = rand01(seed_for(id.xy, camera.frame_index, 6u));
        float4 bounce = cosine_hemisphere4(N, T, B, C, u1, u2, u3, u4, u5);

        RayHit4 secondary = trace_ray_4d(svo, hit_pos + N * 0.02f, bounce, 128);

        float3 incoming;
        if (secondary.idx < 0) {
            incoming = sky_color4(bounce);
        } else {
            // One more cheap bounce: sample sky in a random direction from the second hit.
            float4 h2_pos    = hit_pos + bounce * secondary.t;
            float3 h2_albedo = colors[secondary.idx].rgb;
            float4 N2        = secondary.normal;
            float4 T2; float4 B2; float4 C2;
            axis_tangent_frame4(N2, T2, B2, C2);
            float v1 = rand01(seed_for(id.xy, camera.frame_index, 7u));
            float v2 = rand01(seed_for(id.xy, camera.frame_index, 8u));
            float v3 = rand01(seed_for(id.xy, camera.frame_index, 9u));
            float v4 = rand01(seed_for(id.xy, camera.frame_index, 10u));
            float v5 = rand01(seed_for(id.xy, camera.frame_index, 11u));
            float4 bounce2   = cosine_hemisphere4(N2, T2, B2, C2, v1, v2, v3, v4, v5);
            RayHit4 tertiary = trace_ray_4d(svo, h2_pos + N2 * 0.02f, bounce2, 64);
            float3 in2 = (tertiary.idx < 0) ? sky_color4(bounce2)
                                             : colors[tertiary.idx].rgb * float3(0.25f, 0.30f, 0.38f);
            incoming = h2_albedo * in2;
        }

        radiance         = albedo * incoming;
        depth_tex[id.xy] = primary.t;
    }
    hdr_tex[id.xy] = float4(radiance, 1.0f);
}
)slang";

// ---------------------------------------------------------------------------
// 4D TAA accumulation compute shader.
// Reprojection uses the explicit 4D camera vectors instead of matrices.
// ---------------------------------------------------------------------------
constexpr std::string_view kV4DTaaSlangPath = "voxel4d/taa.slang";
constexpr std::string_view kV4DTaaSlang     = R"slang(
struct Voxel4DCamera {
    float4 pos;
    float4 forward;
    float4 right;
    float4 up;
    float4 over;
    float4 prev_pos;
    float4 prev_forward;
    float4 prev_right;
    float4 prev_up;
    float  fov_y;
    float  aspect;
    float  taa_blend;
    uint   frame_index;
};

[[vk::binding(0, 0)]] ConstantBuffer<Voxel4DCamera> camera;
[[vk::binding(0, 1)]] Texture2D<float4>  hdr_tex;
[[vk::binding(1, 1)]] Texture2D<float>   depth_tex;
[[vk::binding(2, 1)]] Texture2D<float4>  prev_accum;
[[vk::binding(3, 1)]] Texture2D<float>   prev_depth;
[[vk::binding(0, 2)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> accum_out;

// Reconstruct 4D world-space hit point from (2D uv, depth, current camera).
float4 reproject4d(float2 uv, float depth) {
    float nx     = uv.x * 2.0f - 1.0f;
    float ny     = uv.y * 2.0f - 1.0f;
    float half_h = tan(camera.fov_y * 0.5f);
    float half_w = half_h * camera.aspect;
    float4 dir   = normalize(camera.forward + nx * half_w * camera.right
                                            + ny * half_h * camera.up);
    return camera.pos + dir * depth;
}

// Project a 4D world point through the previous frame's 4D camera to a 2D UV.
float2 project4d_to_prev_uv(float4 world_pos) {
    float4 rel   = world_pos - camera.prev_pos;
    float  d     = dot(rel, camera.prev_forward);
    if (d < 1e-4f) return float2(-2.0f);
    float half_h = tan(camera.fov_y * 0.5f);
    float half_w = half_h * camera.aspect;
    float  x     = dot(rel, camera.prev_right);
    float  y     = dot(rel, camera.prev_up);
    float ndc_x  = x / (d * half_w);
    float ndc_y  = y / (d * half_h);
    return float2(ndc_x * 0.5f + 0.5f, ndc_y * 0.5f + 0.5f);
}

// Depth-aware bilinear 4-tap history gather (same as 3D TAA).
float4 sample_history_4tap(float2 prev_uv, float expected_prev_d, float far_history,
                            uint width, uint height, int2 fallback_px) {
    float2 texel_pos  = prev_uv * float2(float(width), float(height)) - 0.5f;
    int2   base_px    = int2(floor(texel_pos));
    float2 frac_uv    = frac(texel_pos);
    float4 accum      = float4(0.0f);
    float  weight_sum = 0.0f;
    float  depth_win  = lerp(0.02f, 0.05f, far_history);

    for (int oy = 0; oy <= 1; ++oy)
        for (int ox = 0; ox <= 1; ++ox) {
            int2   sp   = clamp(base_px + int2(ox, oy), int2(0, 0),
                                int2(int(width) - 1, int(height) - 1));
            float  bwx  = ox == 0 ? (1.0f - frac_uv.x) : frac_uv.x;
            float  bwy  = oy == 0 ? (1.0f - frac_uv.y) : frac_uv.y;
            float  sd   = prev_depth.Load(int3(sp, 0));
            float  dd   = abs(sd - expected_prev_d) / max(expected_prev_d, 0.1f);
            float  dw   = saturate(1.0f - dd / depth_win);
            float  sw   = bwx * bwy * dw;
            accum       += prev_accum.Load(int3(sp, 0)) * sw;
            weight_sum  += sw;
        }
    if (weight_sum > 1e-4f) return accum / weight_sum;
    return prev_accum.Load(int3(fallback_px, 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void taaMain(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    accum_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float2 uv      = (float2(id.xy) + 0.5f) / float2(float(width), float(height));
    float4 current = hdr_tex.Load(int3(id.xy, 0));
    float  cur_d   = depth_tex.Load(int3(id.xy, 0));
    bool   sky     = cur_d > 1e20f;
    float  blend   = camera.taa_blend;

    if (!sky) {
        float4 world_pos      = reproject4d(uv, cur_d);
        float2 prev_uv        = project4d_to_prev_uv(world_pos);
        float  expected_d     = length(world_pos - camera.prev_pos);
        float  far_history    = saturate((expected_d - 24.0f) / 96.0f);

        bool in_bounds = all(prev_uv >= 0.0f) && all(prev_uv <= 1.0f);
        if (in_bounds) {
            int2 prev_px = clamp(int2(prev_uv * float2(float(width), float(height))),
                                 int2(0, 0), int2(int(width) - 1, int(height) - 1));

            float best_score = 1e30f, best_dd = 1e30f, best_px_dist = 1e30f;
            int2  best_px = prev_px;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int2  cp   = clamp(prev_px + int2(dx, dy), int2(0, 0),
                                       int2(int(width) - 1, int(height) - 1));
                    float cd   = prev_depth.Load(int3(cp, 0));
                    float cdd  = abs(cd - expected_d) / max(expected_d, 0.1f);
                    float cpd  = length(float2(cp - prev_px));
                    float cs   = cdd + cpd * 0.02f;
                    if (cs < best_score) {
                        best_score = cs; best_dd = cdd;
                        best_px_dist = cpd; best_px = cp;
                    }
                }

            float max_dd = best_px_dist > 0.0f ? 0.02f : 0.04f;
            if (best_dd < max_dd && best_score < 0.06f) {
                float4 history = sample_history_4tap(prev_uv, expected_d, far_history,
                                                      width, height, best_px);
                float3 cmin = current.rgb, cmax = current.rgb;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        int2   sp = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0),
                                          int2(int(width) - 1, int(height) - 1));
                        float3 s  = hdr_tex.Load(int3(sp, 0)).rgb;
                        cmin = min(cmin, s);
                        cmax = max(cmax, s);
                    }
                float3 pad  = max((cmax - cmin) * (0.08f + far_history * 0.17f),
                                   float3(0.01f + far_history * 0.03f));
                float4 clamped   = float4(clamp(history.rgb, cmin - pad, cmax + pad), 1.0f);
                float  fb_blend  = lerp(max(blend, 0.3f), max(blend, 0.12f), far_history);
                float  conf      = saturate(best_score / 0.06f);
                float  eff       = lerp(blend, fb_blend, conf);
                eff = lerp(eff, blend * 0.35f, far_history * (1.0f - conf));
                accum_out[id.xy] = lerp(clamped, current, eff);
                return;
            }
        }
    }
    accum_out[id.xy] = current;
}
)slang";

// ---------------------------------------------------------------------------
// Blit + Bloom shaders — identical to 3D version.
// ---------------------------------------------------------------------------
constexpr std::string_view kV4DBlitVertPath  = "voxel4d/blit_vert.slang";
constexpr std::string_view kV4DBlitVertSlang = R"slang(
struct VOut { float4 pos : SV_Position; [[vk::location(0)]] float2 uv; };
[shader("vertex")]
VOut blitVert([[vk::location(0)]] float2 uv : TEXCOORD0) {
    VOut o;
    o.uv  = uv;
    o.pos = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return o;
}
)slang";

constexpr std::string_view kV4DBlitFragPath  = "voxel4d/blit_frag.slang";
constexpr std::string_view kV4DBlitFragSlang = R"slang(
[[vk::binding(0, 0)]] SamplerState    blit_sampler;
[[vk::binding(1, 0)]] Texture2D<float4> accum_tex;
[[vk::binding(2, 0)]] Texture2D<float4> bloom_tex;
struct VIn { [[vk::location(0)]] float2 uv; };
[shader("fragment")]
float4 blitFrag(VIn input) : SV_Target {
    uint w, h;
    accum_tex.GetDimensions(w, h);
    int2   px  = int2(input.uv * float2(float(w), float(h)));
    float3 hdr = accum_tex.Load(int3(px, 0)).rgb + bloom_tex.Load(int3(px, 0)).rgb;
    float3 out = pow(max(hdr / (hdr + 1.0f), 0.0f), float3(1.0f / 2.2f));
    return float4(out, 1.0f);
}
)slang";

constexpr std::string_view kV4DBloomSlangPath = "voxel4d/bloom.slang";
constexpr std::string_view kV4DBloomSlang     = R"slang(
struct BloomParams { float threshold; float knee; float intensity; float enabled; };
[[vk::binding(0, 0)]] ConstantBuffer<BloomParams> bloom;
[[vk::binding(1, 0)]] Texture2D<float4>   src_tex;
[[vk::binding(0, 1)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> dst_tex;
float luminance(float3 c) { return dot(c, float3(0.2126f, 0.7152f, 0.0722f)); }
float soft_thresh(float luma) {
    float lo = bloom.threshold - bloom.knee, hi = bloom.threshold + bloom.knee;
    if (luma <= lo) return 0.0f;
    if (luma >= hi) return 1.0f;
    float t = (luma - lo) / max(hi - lo, 1e-5f);
    return t * t * (3.0f - 2.0f * t);
}
[shader("compute")] [numthreads(8, 8, 1)]
void bloomThreshMain(uint3 id : SV_DispatchThreadID) {
    uint w, h; dst_tex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float4 c = src_tex.Load(int3(id.xy, 0));
    dst_tex[id.xy] = float4(c.rgb * soft_thresh(luminance(c.rgb)) * bloom.enabled, 1.0f);
}
float3 gauss_blur(int2 center, int2 dir, uint w, uint h) {
    const float sigma = 3.0f; float3 col = 0.0f; float wsum = 0.0f;
    for (int k = -6; k <= 6; ++k) {
        int2 tc = clamp(center + dir * k, int2(0,0), int2(int(w)-1, int(h)-1));
        float wt = exp(-float(k*k)/(2.0f*sigma*sigma));
        col += src_tex.Load(int3(tc, 0)).rgb * wt; wsum += wt;
    }
    return col / wsum;
}
[shader("compute")] [numthreads(8, 8, 1)]
void bloomBlurHMain(uint3 id : SV_DispatchThreadID) {
    uint w, h; dst_tex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    dst_tex[id.xy] = float4(gauss_blur(int2(id.xy), int2(1,0), w, h), 1.0f);
}
[shader("compute")] [numthreads(8, 8, 1)]
void bloomBlurVMain(uint3 id : SV_DispatchThreadID) {
    uint w, h; dst_tex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    dst_tex[id.xy] = float4(gauss_blur(int2(id.xy), int2(0,1), w, h) * bloom.intensity, 1.0f);
}
)slang";

// ===========================================================================
// C++ types
// ===========================================================================

// GPU-side camera: 9 float4s + 4 floats = 176 bytes (all 16-byte aligned).
struct alignas(16) Voxel4DCameraUniform {
    glm::vec4 pos;
    glm::vec4 forward;
    glm::vec4 right;
    glm::vec4 up;
    glm::vec4 over;
    glm::vec4 prev_pos;
    glm::vec4 prev_forward;
    glm::vec4 prev_right;
    glm::vec4 prev_up;
    float fov_y;
    float aspect;
    float taa_blend;
    uint32_t frame_index;
};
static_assert(sizeof(Voxel4DCameraUniform) == 160);

struct alignas(16) V4DBloomParamsUniform {
    float threshold;
    float knee;
    float intensity;
    float enabled;
};

// ---------------------------------------------------------------------------
// 4D camera resource (lives in main world, extracted to render world)
// ---------------------------------------------------------------------------
struct Voxel4DCameraState {
    glm::vec4 pos{16.0f, 12.0f, -6.0f, 8.0f};   // start outside the scene in z
    glm::vec4 forward{0.0f, 0.0f, 1.0f, 0.0f};  // look in +z
    glm::vec4 right{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 up{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 over{0.0f, 0.0f, 0.0f, 1.0f};  // 4th dimension (+w)
    float fov_y      = glm::radians(65.0f);
    float move_speed = 10.0f;
};

struct Voxel4DConfig {
    float taa_blend       = 0.1f;
    float camera_speed    = 10.0f;
    bool bloom_enabled    = true;
    float bloom_threshold = 1.0f;
    float bloom_knee      = 0.5f;
    float bloom_intensity = 0.2f;
};

struct Voxel4DScene {
    dense_grid<4, uint8_t> grid;
    std::vector<glm::vec4> palette;
};

struct ExtractedVoxel4DSvo {
    std::vector<uint32_t> svo_words;
    std::vector<glm::vec4> colors;
};

struct ExtractedVoxel4DCamera {
    glm::vec4 pos;
    glm::vec4 forward;
    glm::vec4 right;
    glm::vec4 up;
    glm::vec4 over;
    float fov_y = glm::radians(65.0f);
};

struct Voxel4DShaderHandles {
    assets::Handle<shader::Shader> svo_lib;
    assets::Handle<shader::Shader> trace;
    assets::Handle<shader::Shader> taa;
    assets::Handle<shader::Shader> blit_vert;
    assets::Handle<shader::Shader> blit_frag;
    assets::Handle<shader::Shader> bloom;
};

struct Voxel4DRenderState {
    wgpu::Buffer svo_buffer;
    wgpu::Buffer color_buffer;
    wgpu::Buffer camera_uniform;
    wgpu::Buffer vertex_buffer;
    // HDR + depth from trace pass
    wgpu::Texture hdr_tex;
    wgpu::TextureView hdr_storage_view, hdr_sampled_view;
    wgpu::Texture depth_tex;
    wgpu::TextureView depth_storage_view, depth_sampled_view;
    // TAA ping-pong accum
    wgpu::Texture accum_tex[2];
    wgpu::TextureView accum_storage_view[2], accum_sampled_view[2];
    // Previous-frame depth
    wgpu::Texture prev_depth_tex;
    wgpu::TextureView prev_depth_sampled_view;
    wgpu::Sampler linear_sampler;
    // Bloom intermediates
    wgpu::Buffer bloom_params_uniform;
    wgpu::Texture bloom_a_tex, bloom_b_tex;
    wgpu::TextureView bloom_a_storage_view, bloom_a_sampled_view;
    wgpu::TextureView bloom_b_storage_view, bloom_b_sampled_view;
    // Bind group layouts
    wgpu::BindGroupLayout scene_layout;
    wgpu::BindGroupLayout trace_out_layout;
    wgpu::BindGroupLayout taa_in_layout;
    wgpu::BindGroupLayout taa_out_layout;
    wgpu::BindGroupLayout blit_layout;
    wgpu::BindGroupLayout bloom_in_layout;
    wgpu::BindGroupLayout bloom_out_layout;
    // Bind groups
    wgpu::BindGroup scene_bg;
    wgpu::BindGroup trace_out_bg;
    wgpu::BindGroup taa_in_bg[2];
    wgpu::BindGroup taa_out_bg[2];
    wgpu::BindGroup blit_bg[2];
    wgpu::BindGroup bloom_src_accum_bg[2];
    wgpu::BindGroup bloom_src_a_bg, bloom_src_b_bg;
    wgpu::BindGroup bloom_dst_a_bg, bloom_dst_b_bg;
    // Pipeline IDs
    CachedPipelineId trace_pipeline_id;
    CachedPipelineId taa_pipeline_id;
    CachedPipelineId blit_pipeline_id;
    CachedPipelineId bloom_thresh_id, bloom_h_id, bloom_v_id;
    // State
    glm::uvec2 output_size{0, 0};
    int accum_idx = 0;
    // Previous-frame camera (used to fill the "prev_*" fields next frame)
    glm::vec4 prev_pos{16.0f, 12.0f, -6.0f, 8.0f};
    glm::vec4 prev_forward{0.0f, 0.0f, 1.0f, 0.0f};
    glm::vec4 prev_right{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 prev_up{0.0f, 1.0f, 0.0f, 0.0f};
    float prev_fov_y       = glm::radians(65.0f);
    bool resources_created = false;
    bool pipelines_queued  = false;
    bool svo_valid         = false;
    uint32_t frame_count   = 0;
};

// ===========================================================================
// Sub-graph
// ===========================================================================

struct V4DGraphTag {
    void register_to(RenderGraph& g);
};
inline V4DGraphTag V4DGraph;
enum class V4DNode { Trace, Blit };

// ===========================================================================
// Render graph nodes
// ===========================================================================

struct V4DTraceNode : graph::Node {
    std::optional<QueryState<Item<const ExtractedView&, const ViewTarget&>, Filter<>>> view_qs;

    void update(const World& world) override {
        if (!view_qs)
            view_qs = world.try_query<Item<const ExtractedView&, const ViewTarget&>>();
        else
            view_qs->update_archetypes(world);
    }

    void run(GraphContext& ctx, RenderContext& rc, const World& world) override {
        auto& state = const_cast<Voxel4DRenderState&>(world.resource<Voxel4DRenderState>());
        if (!state.svo_valid || !state.resources_created) return;
        const auto& ps = world.resource<PipelineServer>();
        auto trace_pl  = ps.get_compute_pipeline(state.trace_pipeline_id);
        auto taa_pl    = ps.get_compute_pipeline(state.taa_pipeline_id);
        if (!trace_pl || !taa_pl) return;
        auto view_ent = ctx.get_view_entity();
        if (!view_ent || !view_qs) return;
        auto view_opt = view_qs->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(*view_ent);
        if (!view_opt) return;
        auto&& [exview, target] = *view_opt;
        glm::uvec2 vp           = exview.viewport_size;
        if (vp.x == 0 || vp.y == 0 || state.output_size != vp) return;

        // Build camera uniform from the extracted 4D camera state.
        const auto& ecam = world.resource<ExtractedVoxel4DCamera>();
        const auto& cfg  = world.resource<Voxel4DConfig>();
        Voxel4DCameraUniform cam;
        cam.pos          = ecam.pos;
        cam.forward      = ecam.forward;
        cam.right        = ecam.right;
        cam.up           = ecam.up;
        cam.over         = ecam.over;
        cam.prev_pos     = state.prev_pos;
        cam.prev_forward = state.prev_forward;
        cam.prev_right   = state.prev_right;
        cam.prev_up      = state.prev_up;
        cam.fov_y        = ecam.fov_y;
        cam.aspect       = float(vp.x) / float(vp.y);
        cam.taa_blend    = cfg.taa_blend;
        cam.frame_index  = state.frame_count++;
        world.resource<wgpu::Queue>().writeBuffer(state.camera_uniform, 0, &cam, sizeof(cam));

        // Trace pass
        {
            auto pass = rc.command_encoder().beginComputePass({});
            pass.setPipeline(trace_pl->get().pipeline());
            pass.setBindGroup(0, state.scene_bg, std::span<const uint32_t>{});
            pass.setBindGroup(1, state.trace_out_bg, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
            pass.end();
        }
        rc.flush_encoder();

        // TAA pass
        int next = 1 - state.accum_idx;
        {
            auto pass = rc.command_encoder().beginComputePass({});
            pass.setPipeline(taa_pl->get().pipeline());
            pass.setBindGroup(0, state.scene_bg, std::span<const uint32_t>{});
            pass.setBindGroup(1, state.taa_in_bg[next], std::span<const uint32_t>{});
            pass.setBindGroup(2, state.taa_out_bg[next], std::span<const uint32_t>{});
            pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
            pass.end();
        }
        rc.flush_encoder();

        // Copy current depth → prev_depth for next frame
        rc.command_encoder().copyTextureToTexture(wgpu::TexelCopyTextureInfo()
                                                      .setTexture(state.depth_tex)
                                                      .setMipLevel(0)
                                                      .setOrigin(wgpu::Origin3D{0, 0, 0})
                                                      .setAspect(wgpu::TextureAspect::eAll),
                                                  wgpu::TexelCopyTextureInfo()
                                                      .setTexture(state.prev_depth_tex)
                                                      .setMipLevel(0)
                                                      .setOrigin(wgpu::Origin3D{0, 0, 0})
                                                      .setAspect(wgpu::TextureAspect::eAll),
                                                  wgpu::Extent3D{vp.x, vp.y, 1});
        rc.flush_encoder();

        // Bloom passes
        auto bloom_thr_pl = ps.get_compute_pipeline(state.bloom_thresh_id);
        auto bloom_h_pl   = ps.get_compute_pipeline(state.bloom_h_id);
        auto bloom_v_pl   = ps.get_compute_pipeline(state.bloom_v_id);
        if (bloom_thr_pl && bloom_h_pl && bloom_v_pl) {
            V4DBloomParamsUniform bp;
            bp.threshold = cfg.bloom_threshold;
            bp.knee      = cfg.bloom_knee;
            bp.intensity = cfg.bloom_intensity;
            bp.enabled   = cfg.bloom_enabled ? 1.0f : 0.0f;
            world.resource<wgpu::Queue>().writeBuffer(state.bloom_params_uniform, 0, &bp, sizeof(bp));
            auto dispatch = [&](auto& pl, wgpu::BindGroup& in_bg, wgpu::BindGroup& out_bg) {
                auto pass = rc.command_encoder().beginComputePass({});
                pass.setPipeline(pl->get().pipeline());
                pass.setBindGroup(0, in_bg, std::span<const uint32_t>{});
                pass.setBindGroup(1, out_bg, std::span<const uint32_t>{});
                pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
                pass.end();
                rc.flush_encoder();
            };
            dispatch(bloom_thr_pl, state.bloom_src_accum_bg[next], state.bloom_dst_a_bg);
            dispatch(bloom_h_pl, state.bloom_src_a_bg, state.bloom_dst_b_bg);
            dispatch(bloom_v_pl, state.bloom_src_b_bg, state.bloom_dst_a_bg);
        }

        state.accum_idx    = next;
        state.prev_pos     = ecam.pos;
        state.prev_forward = ecam.forward;
        state.prev_right   = ecam.right;
        state.prev_up      = ecam.up;
        state.prev_fov_y   = ecam.fov_y;
    }
};

struct V4DBlitNode : graph::Node {
    std::optional<QueryState<Item<const ViewTarget&>, Filter<>>> view_qs;

    void update(const World& world) override {
        if (!view_qs)
            view_qs = world.try_query<Item<const ViewTarget&>>();
        else
            view_qs->update_archetypes(world);
    }

    void run(GraphContext& ctx, RenderContext& rc, const World& world) override {
        const auto& state = world.resource<Voxel4DRenderState>();
        if (!state.svo_valid || !state.pipelines_queued) return;
        const auto& ps = world.resource<PipelineServer>();
        auto pipeline  = ps.get_render_pipeline(state.blit_pipeline_id);
        if (!pipeline) return;
        auto view_ent = ctx.get_view_entity();
        if (!view_ent || !view_qs) return;
        auto view_opt = view_qs->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(*view_ent);
        if (!view_opt) return;
        auto&& [target] = *view_opt;
        if (!target.texture_view) return;

        auto pass = rc.command_encoder().beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(std::array{
            wgpu::RenderPassColorAttachment()
                .setView(target.texture_view)
                .setDepthSlice(~0u)
                .setLoadOp(wgpu::LoadOp::eLoad)
                .setStoreOp(wgpu::StoreOp::eStore),
        }));
        pass.setPipeline(pipeline->get().pipeline());
        pass.setVertexBuffer(0, state.vertex_buffer, 0, sizeof(float) * 6);
        pass.setBindGroup(0, state.blit_bg[state.accum_idx], std::span<const uint32_t>{});
        pass.draw(3, 1, 0, 0);
        pass.end();
        rc.flush_encoder();
    }
};

void V4DGraphTag::register_to(RenderGraph& g) {
    RenderGraph vg;
    vg.add_node(V4DNode::Trace, V4DTraceNode{});
    vg.add_node(V4DNode::Blit, V4DBlitNode{});
    vg.add_node_edges(V4DNode::Trace, V4DNode::Blit);
    if (auto res = g.add_sub_graph(V4DGraph, std::move(vg)); !res) spdlog::error("[v4d] Failed to register sub-graph");
}

// ===========================================================================
// ECS systems
// ===========================================================================

static std::span<const std::byte> to_bytes4d(std::string_view sv) {
    return {reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
}

// Gram-Schmidt re-orthonormalise 4 basis vectors: forward, right, up, over.
static void ortho4(glm::vec4& f, glm::vec4& r, glm::vec4& u, glm::vec4& o) {
    f = glm::normalize(f);
    r = glm::normalize(r - glm::dot(r, f) * f);
    u = glm::normalize(u - glm::dot(u, f) * f - glm::dot(u, r) * r);
    o = glm::normalize(o - glm::dot(o, f) * f - glm::dot(o, r) * r - glm::dot(o, u) * u);
}

void camera_control_4d(Res<input::ButtonInput<input::KeyCode>> keys,
                       Res<input::ButtonInput<input::MouseButton>> mouse_btns,
                       EventReader<input::MouseScroll> scroll_input,
                       Local<std::optional<glm::dvec2>> last_mouse_pos,
                       Query<Item<const epix::window::Window&>, With<epix::window::PrimaryWindow>> windows,
                       Res<time::Time<>> game_time,
                       core::ResMut<Voxel4DCameraState> cam) {
    const float dt    = game_time->delta_secs();
    const float spd   = cam->move_speed;
    const float sens  = 0.002f;
    const float rsens = 1.2f;  // 4D rotation speed (rad/s)

    float scroll_rot4 = 0.0f;
    for (const auto& scroll : scroll_input.read()) scroll_rot4 += static_cast<float>(scroll.yoffset);

    double dx = 0.0, dy = 0.0;
    const bool looking = mouse_btns->pressed(input::MouseButton::MouseButtonRight);
    if (looking) {
        if (auto window = windows.single(); window.has_value()) {
            auto&& [primary_window] = *window;
            auto [mx, my]           = primary_window.cursor_pos;
            if (last_mouse_pos->has_value()) {
                dx = mx - last_mouse_pos->value().x;
                dy = my - last_mouse_pos->value().y;
            }
            last_mouse_pos->emplace(mx, my);
        } else {
            last_mouse_pos->reset();
        }
    } else {
        last_mouse_pos->reset();
    }

    glm::vec4& pos = cam->pos;
    glm::vec4& f   = cam->forward;
    glm::vec4& r   = cam->right;
    glm::vec4& u   = cam->up;
    glm::vec4& o   = cam->over;

    // Translation
    glm::vec4 move{0.0f};
    if (keys->pressed(input::KeyCode::KeyW)) move += f;
    if (keys->pressed(input::KeyCode::KeyS)) move -= f;
    if (keys->pressed(input::KeyCode::KeyA)) move -= r;
    if (keys->pressed(input::KeyCode::KeyD)) move += r;
    if (keys->pressed(input::KeyCode::KeySpace)) move += u;
    if (keys->pressed(input::KeyCode::KeyLeftShift)) move -= u;
    if (keys->pressed(input::KeyCode::KeyR)) move += o;
    if (keys->pressed(input::KeyCode::KeyF)) move -= o;
    if (glm::length(move) > 0.001f) pos += glm::normalize(move) * spd * dt;

    // Mouse yaw (rotate forward & right in their plane)
    if (dx != 0.0) {
        float a = float(dx) * sens;
        float c = std::cos(a), s = std::sin(a);
        auto newf = glm::normalize(c * f + s * r);
        auto newr = glm::normalize(-s * f + c * r);
        f         = newf;
        r         = newr;
    }

    // Mouse pitch (rotate forward & up)
    if (dy != 0.0) {
        float a = float(dy) * sens;
        float c = std::cos(a), s = std::sin(a);
        auto newf = glm::normalize(c * f - s * u);
        auto newu = glm::normalize(s * f + c * u);
        f         = newf;
        u         = newu;
    }

    // 3D roll: Q/E rotate right/up around the current forward direction.
    float roll = 0.0f;
    if (keys->pressed(input::KeyCode::KeyQ)) roll = rsens * dt;
    if (keys->pressed(input::KeyCode::KeyE)) roll = -rsens * dt;
    if (std::abs(roll) > 1e-6f) {
        float c = std::cos(roll), s = std::sin(roll);
        auto newr = glm::normalize(c * r + s * u);
        auto newu = glm::normalize(-s * r + c * u);
        r         = newr;
        u         = newu;
    }

    // Mouse wheel rotates the camera through the fourth dimension.
    float rot4 = scroll_rot4 * sens;
    if (std::abs(rot4) > 1e-6f) {
        float c = std::cos(rot4), s = std::sin(rot4);
        auto newf = glm::normalize(c * f + s * o);
        auto newo = glm::normalize(-s * f + c * o);
        f         = newf;
        o         = newo;
    }

    ortho4(f, r, u, o);
}

void v4d_imgui_ui(imgui::Ctx /*ctx*/, core::ResMut<Voxel4DConfig> cfg, core::ResMut<Voxel4DCameraState> cam) {
    // ---- Settings window ----
    ImGui::Begin("4D Voxel Path Tracer");
    ImGui::SeparatorText("TAA");
    ImGui::SliderFloat("Blend Rate", &cfg->taa_blend, 0.001f, 1.0f, "%.3f");
    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat("Speed", &cam->move_speed, 1.0f, 100.0f, "%.1f");
    ImGui::Text("W/S=forward  A/D=strafe  Space/Shift=up/down");
    ImGui::Text("Q/E=roll camera   R/F=move +/-w   Wheel=rotate through w");
    ImGui::Text("Right-drag=look");
    ImGui::SeparatorText("Bloom");
    ImGui::Checkbox("Enabled", &cfg->bloom_enabled);
    ImGui::SliderFloat("Threshold", &cfg->bloom_threshold, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Knee", &cfg->bloom_knee, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Intensity", &cfg->bloom_intensity, 0.0f, 2.0f, "%.2f");
    ImGui::End();

    // ---- Camera control window ----
    ImGui::Begin("4D Camera");

    ImGui::SeparatorText("Position");
    ImGui::DragFloat("pos.x", &cam->pos.x, 0.1f, -1000.0f, 1000.0f, "%.2f");
    ImGui::DragFloat("pos.y", &cam->pos.y, 0.1f, -1000.0f, 1000.0f, "%.2f");
    ImGui::DragFloat("pos.z", &cam->pos.z, 0.1f, -1000.0f, 1000.0f, "%.2f");
    ImGui::DragFloat("pos.w", &cam->pos.w, 0.1f, -1000.0f, 1000.0f, "%.2f");

    // Helper: show a read-only float4 row with a label.
    auto show_vec4 = [](const char* label, const glm::vec4& v) {
        ImGui::Text("%-12s (%.3f, %.3f, %.3f, %.3f)", label, v.x, v.y, v.z, v.w);
    };

    ImGui::SeparatorText("Basis vectors (read-only)");
    show_vec4("forward:", cam->forward);
    show_vec4("right:", cam->right);
    show_vec4("up:", cam->up);
    show_vec4("over:", cam->over);

    ImGui::SeparatorText("Orientation");
    ImGui::Text("Yaw / Pitch (deg)");

    // Yaw: rotate forward & right around the up axis.
    if (ImGui::Button("Yaw +5")) {
        float a      = glm::radians(5.0f);
        auto nf      = glm::normalize(std::cos(a) * cam->forward + std::sin(a) * cam->right);
        auto nr      = glm::normalize(-std::sin(a) * cam->forward + std::cos(a) * cam->right);
        cam->forward = nf;
        cam->right   = nr;
        ortho4(cam->forward, cam->right, cam->up, cam->over);
    }
    ImGui::SameLine();
    if (ImGui::Button("Yaw -5")) {
        float a      = glm::radians(-5.0f);
        auto nf      = glm::normalize(std::cos(a) * cam->forward + std::sin(a) * cam->right);
        auto nr      = glm::normalize(-std::sin(a) * cam->forward + std::cos(a) * cam->right);
        cam->forward = nf;
        cam->right   = nr;
        ortho4(cam->forward, cam->right, cam->up, cam->over);
    }

    if (ImGui::Button("Pitch +5")) {
        float a      = glm::radians(5.0f);
        auto nf      = glm::normalize(std::cos(a) * cam->forward - std::sin(a) * cam->up);
        auto nu      = glm::normalize(std::sin(a) * cam->forward + std::cos(a) * cam->up);
        cam->forward = nf;
        cam->up      = nu;
        ortho4(cam->forward, cam->right, cam->up, cam->over);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pitch -5")) {
        float a      = glm::radians(-5.0f);
        auto nf      = glm::normalize(std::cos(a) * cam->forward - std::sin(a) * cam->up);
        auto nu      = glm::normalize(std::sin(a) * cam->forward + std::cos(a) * cam->up);
        cam->forward = nf;
        cam->up      = nu;
        ortho4(cam->forward, cam->right, cam->up, cam->over);
    }

    ImGui::Text("4D rotation (forward <-> over)");
    if (ImGui::Button("Rot4 +5")) {
        float a      = glm::radians(5.0f);
        auto nf      = glm::normalize(std::cos(a) * cam->forward + std::sin(a) * cam->over);
        auto no      = glm::normalize(-std::sin(a) * cam->forward + std::cos(a) * cam->over);
        cam->forward = nf;
        cam->over    = no;
        ortho4(cam->forward, cam->right, cam->up, cam->over);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rot4 -5")) {
        float a      = glm::radians(-5.0f);
        auto nf      = glm::normalize(std::cos(a) * cam->forward + std::sin(a) * cam->over);
        auto no      = glm::normalize(-std::sin(a) * cam->forward + std::cos(a) * cam->over);
        cam->forward = nf;
        cam->over    = no;
        ortho4(cam->forward, cam->right, cam->up, cam->over);
    }

    ImGui::SeparatorText("FOV");
    float fov_deg = glm::degrees(cam->fov_y);
    if (ImGui::SliderFloat("fov_y (deg)", &fov_deg, 10.0f, 120.0f, "%.1f")) cam->fov_y = glm::radians(fov_deg);

    ImGui::SeparatorText("Reset");
    if (ImGui::Button("Reset to default")) {
        cam->pos     = glm::vec4{16.0f, 12.0f, -6.0f, 8.0f};
        cam->forward = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
        cam->right   = glm::vec4{1.0f, 0.0f, 0.0f, 0.0f};
        cam->up      = glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
        cam->over    = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
        cam->fov_y   = glm::radians(65.0f);
    }

    ImGui::End();
}

void extract_v4d_config(Commands cmd, Extract<Res<Voxel4DConfig>> cfg) {
    cmd.insert_resource(Voxel4DConfig{
        .taa_blend       = cfg->taa_blend,
        .camera_speed    = cfg->camera_speed,
        .bloom_enabled   = cfg->bloom_enabled,
        .bloom_threshold = cfg->bloom_threshold,
        .bloom_knee      = cfg->bloom_knee,
        .bloom_intensity = cfg->bloom_intensity,
    });
}

void extract_v4d_camera(Commands cmd, Extract<Res<Voxel4DCameraState>> cam) {
    cmd.insert_resource(ExtractedVoxel4DCamera{
        .pos     = cam->pos,
        .forward = cam->forward,
        .right   = cam->right,
        .up      = cam->up,
        .over    = cam->over,
        .fov_y   = cam->fov_y,
    });
}

void setup_v4d_scene(Commands cmd) {
    // 32×32×32×16 4D grid.  Material 0 = empty (not stored in dense_grid).
    Voxel4DScene scene{.grid = dense_grid<4, uint8_t>({32u, 32u, 32u, 16u})};
    scene.palette = {
        glm::vec4{0.0f},                       // 0 empty
        glm::vec4{0.55f, 0.55f, 0.55f, 1.0f},  // 1 grey stone
        glm::vec4{0.80f, 0.25f, 0.15f, 1.0f},  // 2 red
        glm::vec4{0.20f, 0.65f, 0.25f, 1.0f},  // 3 green
        glm::vec4{0.85f, 0.75f, 0.25f, 1.0f},  // 4 yellow
        glm::vec4{0.20f, 0.35f, 0.85f, 1.0f},  // 5 blue
        glm::vec4{0.75f, 0.15f, 0.75f, 1.0f},  // 6 purple (w-only objects)
    };

    // Floor at y=0 for all (x, z, w) — grey stone
    for (uint32_t x = 0; x < 32; ++x)
        for (uint32_t z = 0; z < 32; ++z)
            for (uint32_t w = 0; w < 16; ++w) scene.grid.set({x, 0u, z, w}, uint8_t(1));

    // 3×3 grid of pillars (x,z) that exist at every w-slice (colour by (i+j)%3+2)
    for (uint32_t i = 0; i < 3; ++i)
        for (uint32_t j = 0; j < 3; ++j) {
            uint32_t bx = 4 + i * 10, bz = 4 + j * 10;
            uint8_t col = uint8_t(2 + (i + j) % 3);
            for (uint32_t h = 1; h <= 8; ++h)
                for (uint32_t w = 0; w < 16; ++w) scene.grid.set({bx, h, bz, w}, col);
        }

    // 4D-only object: purple slab that only exists at w=4..7 (visible when you approach w≈5-6)
    for (uint32_t x = 12; x < 20; ++x)
        for (uint32_t z = 12; z < 20; ++z)
            for (uint32_t w = 4; w <= 7; ++w) scene.grid.set({x, 5u, z, w}, uint8_t(6));

    // Another unique structure: a ring of blue pillars only at w=10..13
    for (uint32_t theta = 0; theta < 8; ++theta) {
        float angle = float(theta) * (2.0f * 3.14159f / 8.0f);
        uint32_t bx = uint32_t(16 + int(7.5f * std::cos(angle)));
        uint32_t bz = uint32_t(16 + int(7.5f * std::sin(angle)));
        bx          = std::min(bx, 31u);
        bz          = std::min(bz, 31u);
        for (uint32_t h = 1; h <= 12; ++h)
            for (uint32_t w = 10; w <= 13; ++w) scene.grid.set({bx, h, bz, w}, uint8_t(5));
    }

    cmd.spawn(std::move(scene));

    // Spawn a standard Camera using our 4D render-graph sub-graph.
    CameraBundle cam = CameraBundle::with_render_graph(V4DGraph);
    cam.projection   = Projection::perspective(PerspectiveProjection{
        .fov        = glm::radians(65.0f),
        .near_plane = 0.1f,
        .far_plane  = 600.0f,
    });
    cam.transform    = tf::Transform::from_xyz(16.0f, 12.0f, -6.0f);
    cmd.spawn(std::move(cam));
}

void extract_v4d_scene(Commands cmd, Extract<Query<Item<const Voxel4DScene&>>> scenes) {
    for (auto&& [scene] : scenes.iter()) {
        auto result = svo_upload(scene.grid);
        if (!result) {
            spdlog::warn("[v4d] svo_upload failed: {}", result.error().message());
            continue;
        }
        std::vector<glm::vec4> colors;
        colors.reserve(scene.grid.count());
        for (auto&& [pos, mat] : scene.grid.iter()) {
            uint8_t m = mat;
            colors.push_back(m < scene.palette.size() ? scene.palette[m] : glm::vec4(1.0f));
        }
        cmd.insert_resource(ExtractedVoxel4DSvo{
            .svo_words = std::move(result->words),
            .colors    = std::move(colors),
        });
        break;
    }
}

// ---------------------------------------------------------------------------
// Render-world prepare system — creates GPU resources on first run and
// re-creates per-resolution textures/bind-groups when the window resizes.
// ---------------------------------------------------------------------------
void prepare_v4d_render(Res<wgpu::Device> device,
                        Res<wgpu::Queue> queue,
                        Res<Voxel4DShaderHandles> handles,
                        ResMut<PipelineServer> pipeline_server,
                        ResMut<Voxel4DRenderState> state,
                        Res<ExtractedVoxel4DSvo> svo_res,
                        Query<Item<const ExtractedView&, const ViewTarget&>, With<ExtractedCamera>> views) {
    // ---- Phase 1: static resources and pipeline kick-offs ----
    if (!state->resources_created) {
        // group 0: camera uniform + SVO buffer + color buffer
        state->scene_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("V4DSceneBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout()
                                       .setType(wgpu::BufferBindingType::eUniform)
                                       .setMinBindingSize(sizeof(Voxel4DCameraUniform))),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(2)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                }));

        auto make_storage_tex_bgl = [&](const char* lbl, wgpu::TextureFormat fmt) {
            return device->createBindGroupLayout(wgpu::BindGroupLayoutDescriptor().setLabel(lbl).setEntries(std::array{
                wgpu::BindGroupLayoutEntry()
                    .setBinding(0)
                    .setVisibility(wgpu::ShaderStage::eCompute)
                    .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                           .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                           .setFormat(fmt)
                                           .setViewDimension(wgpu::TextureViewDimension::e2D)),
                wgpu::BindGroupLayoutEntry()
                    .setBinding(1)
                    .setVisibility(wgpu::ShaderStage::eCompute)
                    .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                           .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                           .setFormat(wgpu::TextureFormat::eR32Float)
                                           .setViewDimension(wgpu::TextureViewDimension::e2D)),
            }));
        };
        state->trace_out_layout = make_storage_tex_bgl("V4DTraceOutBGL", wgpu::TextureFormat::eRGBA16Float);

        auto make_sampled_bgl = [&](const char* lbl, std::size_t count) {
            std::vector<wgpu::BindGroupLayoutEntry> entries;
            for (std::size_t i = 0; i < count; ++i)
                entries.push_back(wgpu::BindGroupLayoutEntry()
                                      .setBinding(uint32_t(i))
                                      .setVisibility(wgpu::ShaderStage::eCompute)
                                      .setTexture(wgpu::TextureBindingLayout()
                                                      .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                                      .setViewDimension(wgpu::TextureViewDimension::e2D)));
            return device->createBindGroupLayout(wgpu::BindGroupLayoutDescriptor().setLabel(lbl).setEntries(entries));
        };
        state->taa_in_layout = make_sampled_bgl("V4DTaaInBGL", 4);

        state->taa_out_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("V4DTaaOutBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                               .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                               .setFormat(wgpu::TextureFormat::eRGBA16Float)
                                               .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        state->blit_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("V4DBlitBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eFragment)
                        .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eNonFiltering)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eFragment)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(2)
                        .setVisibility(wgpu::ShaderStage::eFragment)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        state->bloom_in_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("V4DBloomInBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout()
                                       .setType(wgpu::BufferBindingType::eUniform)
                                       .setMinBindingSize(sizeof(V4DBloomParamsUniform))),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        state->bloom_out_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("V4DBloomOutBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                               .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                               .setFormat(wgpu::TextureFormat::eRGBA16Float)
                                               .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        state->bloom_params_uniform =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("V4DBloomParams")
                                     .setSize(sizeof(V4DBloomParamsUniform))
                                     .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));

        state->linear_sampler = device->createSampler(wgpu::SamplerDescriptor()
                                                          .setLabel("V4DLinear")
                                                          .setMagFilter(wgpu::FilterMode::eNearest)
                                                          .setMinFilter(wgpu::FilterMode::eNearest)
                                                          .setMaxAnisotropy(1));

        state->camera_uniform =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("V4DCameraUniform")
                                     .setSize(sizeof(Voxel4DCameraUniform))
                                     .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));

        constexpr float kVerts[6] = {0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 2.0f};
        state->vertex_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("V4DBlitVBO")
                                     .setSize(sizeof(kVerts))
                                     .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        queue->writeBuffer(state->vertex_buffer, 0, kVerts, sizeof(kVerts));

        state->trace_pipeline_id = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "v4d-trace",
            .layouts     = {state->scene_layout, state->trace_out_layout},
            .shader      = handles->trace,
            .entry_point = std::string("traceMain"),
        });
        state->taa_pipeline_id   = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "v4d-taa",
            .layouts     = {state->scene_layout, state->taa_in_layout, state->taa_out_layout},
            .shader      = handles->taa,
            .entry_point = std::string("taaMain"),
        });
        state->bloom_thresh_id   = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "v4d-bloom-thresh",
            .layouts     = {state->bloom_in_layout, state->bloom_out_layout},
            .shader      = handles->bloom,
            .entry_point = std::string("bloomThreshMain"),
        });
        state->bloom_h_id        = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "v4d-bloom-h",
            .layouts     = {state->bloom_in_layout, state->bloom_out_layout},
            .shader      = handles->bloom,
            .entry_point = std::string("bloomBlurHMain"),
        });
        state->bloom_v_id        = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "v4d-bloom-v",
            .layouts     = {state->bloom_in_layout, state->bloom_out_layout},
            .shader      = handles->bloom,
            .entry_point = std::string("bloomBlurVMain"),
        });

        state->resources_created = true;
    }

    // ---- Phase 2: blit render pipeline (needs surface format) ----
    if (state->resources_created && !state->pipelines_queued) {
        wgpu::TextureFormat fmt = wgpu::TextureFormat::eUndefined;
        for (auto&& [exv, tgt] : views.iter()) {
            fmt = tgt.format;
            break;
        }
        if (fmt != wgpu::TextureFormat::eUndefined) {
            VertexState vs{.shader = handles->blit_vert, .entry_point = std::string("blitVert")};
            vs.buffers.push_back(wgpu::VertexBufferLayout()
                                     .setArrayStride(sizeof(float) * 2)
                                     .setStepMode(wgpu::VertexStepMode::eVertex)
                                     .setAttributes(std::array{
                                         wgpu::VertexAttribute().setShaderLocation(0).setOffset(0).setFormat(
                                             wgpu::VertexFormat::eFloat32x2),
                                     }));
            FragmentState fs{.shader = handles->blit_frag, .entry_point = std::string("blitFrag")};
            fs.add_target(wgpu::ColorTargetState().setFormat(fmt).setWriteMask(wgpu::ColorWriteMask::eAll));
            state->blit_pipeline_id = pipeline_server->queue_render_pipeline(RenderPipelineDescriptor{
                .label       = "v4d-blit",
                .layouts     = {state->blit_layout},
                .vertex      = std::move(vs),
                .primitive   = wgpu::PrimitiveState()
                                   .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                                   .setCullMode(wgpu::CullMode::eNone),
                .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
                .fragment    = std::move(fs),
            });
            state->pipelines_queued = true;
        }
    }

    // ---- Phase 3: upload SVO + colors once ----
    if (!state->svo_valid && !svo_res->svo_words.empty()) {
        size_t sb = svo_res->svo_words.size() * sizeof(uint32_t);
        size_t cb = std::max(svo_res->colors.size() * sizeof(glm::vec4), sizeof(glm::vec4));
        state->svo_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("V4DSvoBuffer")
                                     .setSize(sb)
                                     .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        queue->writeBuffer(state->svo_buffer, 0, svo_res->svo_words.data(), sb);
        state->color_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("V4DColorBuffer")
                                     .setSize(cb)
                                     .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        if (!svo_res->colors.empty())
            queue->writeBuffer(state->color_buffer, 0, svo_res->colors.data(),
                               svo_res->colors.size() * sizeof(glm::vec4));
        state->svo_valid = true;
    }
    if (!state->svo_valid) return;

    // ---- Phase 4: (re-)create per-resolution textures and bind groups ----
    glm::uvec2 vp{0, 0};
    for (auto&& [exv, tgt] : views.iter()) {
        vp = exv.viewport_size;
        break;
    }
    if (vp.x == 0 || vp.y == 0 || vp == state->output_size) return;
    state->output_size = vp;
    wgpu::Extent3D extent{vp.x, vp.y, 1};

    auto make_tex = [&](const char* lbl, wgpu::TextureFormat fmt, wgpu::TextureView& sv,
                        wgpu::TextureView& tv) -> wgpu::Texture {
        auto tex = device->createTexture(wgpu::TextureDescriptor()
                                             .setLabel(lbl)
                                             .setFormat(fmt)
                                             .setUsage(wgpu::TextureUsage::eStorageBinding |
                                                       wgpu::TextureUsage::eTextureBinding |
                                                       wgpu::TextureUsage::eCopySrc | wgpu::TextureUsage::eCopyDst)
                                             .setSize(extent)
                                             .setMipLevelCount(1)
                                             .setSampleCount(1)
                                             .setDimension(wgpu::TextureDimension::e2D));
        auto vd  = wgpu::TextureViewDescriptor()
                       .setFormat(fmt)
                       .setDimension(wgpu::TextureViewDimension::e2D)
                       .setBaseMipLevel(0)
                       .setMipLevelCount(1)
                       .setBaseArrayLayer(0)
                       .setArrayLayerCount(1);
        sv       = tex.createView(vd);
        tv       = tex.createView(vd);
        return tex;
    };

    state->hdr_tex =
        make_tex("V4DHdrTex", wgpu::TextureFormat::eRGBA16Float, state->hdr_storage_view, state->hdr_sampled_view);
    state->depth_tex =
        make_tex("V4DDepthTex", wgpu::TextureFormat::eR32Float, state->depth_storage_view, state->depth_sampled_view);
    for (int i = 0; i < 2; ++i) {
        auto lbl            = "V4DAccum" + std::to_string(i);
        state->accum_tex[i] = make_tex(lbl.c_str(), wgpu::TextureFormat::eRGBA16Float, state->accum_storage_view[i],
                                       state->accum_sampled_view[i]);
    }
    state->bloom_a_tex = make_tex("V4DBloomA", wgpu::TextureFormat::eRGBA16Float, state->bloom_a_storage_view,
                                  state->bloom_a_sampled_view);
    state->bloom_b_tex = make_tex("V4DBloomB", wgpu::TextureFormat::eRGBA16Float, state->bloom_b_storage_view,
                                  state->bloom_b_sampled_view);
    {
        auto tex =
            device->createTexture(wgpu::TextureDescriptor()
                                      .setLabel("V4DPrevDepth")
                                      .setFormat(wgpu::TextureFormat::eR32Float)
                                      .setUsage(wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst)
                                      .setSize(extent)
                                      .setMipLevelCount(1)
                                      .setSampleCount(1)
                                      .setDimension(wgpu::TextureDimension::e2D));
        auto vd                        = wgpu::TextureViewDescriptor()
                                             .setFormat(wgpu::TextureFormat::eR32Float)
                                             .setDimension(wgpu::TextureViewDimension::e2D)
                                             .setBaseMipLevel(0)
                                             .setMipLevelCount(1)
                                             .setBaseArrayLayer(0)
                                             .setArrayLayerCount(1);
        state->prev_depth_tex          = tex;
        state->prev_depth_sampled_view = tex.createView(vd);
    }

    state->scene_bg = device->createBindGroup(wgpu::BindGroupDescriptor()
                                                  .setLabel("V4DSceneBG")
                                                  .setLayout(state->scene_layout)
                                                  .setEntries(std::array{
                                                      wgpu::BindGroupEntry()
                                                          .setBinding(0)
                                                          .setBuffer(state->camera_uniform)
                                                          .setOffset(0)
                                                          .setSize(sizeof(Voxel4DCameraUniform)),
                                                      wgpu::BindGroupEntry()
                                                          .setBinding(1)
                                                          .setBuffer(state->svo_buffer)
                                                          .setOffset(0)
                                                          .setSize(state->svo_buffer.getSize()),
                                                      wgpu::BindGroupEntry()
                                                          .setBinding(2)
                                                          .setBuffer(state->color_buffer)
                                                          .setOffset(0)
                                                          .setSize(state->color_buffer.getSize()),
                                                  }));

    state->trace_out_bg =
        device->createBindGroup(wgpu::BindGroupDescriptor()
                                    .setLabel("V4DTraceOutBG")
                                    .setLayout(state->trace_out_layout)
                                    .setEntries(std::array{
                                        wgpu::BindGroupEntry().setBinding(0).setTextureView(state->hdr_storage_view),
                                        wgpu::BindGroupEntry().setBinding(1).setTextureView(state->depth_storage_view),
                                    }));

    for (int i = 0; i < 2; ++i) {
        int prev            = 1 - i;
        state->taa_in_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("V4DTaaIn" + std::to_string(i)).c_str())
                .setLayout(state->taa_in_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setTextureView(state->hdr_sampled_view),
                    wgpu::BindGroupEntry().setBinding(1).setTextureView(state->depth_sampled_view),
                    wgpu::BindGroupEntry().setBinding(2).setTextureView(state->accum_sampled_view[prev]),
                    wgpu::BindGroupEntry().setBinding(3).setTextureView(state->prev_depth_sampled_view),
                }));
        state->taa_out_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("V4DTaaOut" + std::to_string(i)).c_str())
                .setLayout(state->taa_out_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setTextureView(state->accum_storage_view[i]),
                }));
        state->blit_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("V4DBlitBG" + std::to_string(i)).c_str())
                .setLayout(state->blit_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setSampler(state->linear_sampler),
                    wgpu::BindGroupEntry().setBinding(1).setTextureView(state->accum_sampled_view[i]),
                    wgpu::BindGroupEntry().setBinding(2).setTextureView(state->bloom_a_sampled_view),
                }));
    }

    auto make_bloom_src = [&](const char* lbl, wgpu::TextureView& tex_view) -> wgpu::BindGroup {
        return device->createBindGroup(wgpu::BindGroupDescriptor()
                                           .setLabel(lbl)
                                           .setLayout(state->bloom_in_layout)
                                           .setEntries(std::array{
                                               wgpu::BindGroupEntry()
                                                   .setBinding(0)
                                                   .setBuffer(state->bloom_params_uniform)
                                                   .setOffset(0)
                                                   .setSize(sizeof(V4DBloomParamsUniform)),
                                               wgpu::BindGroupEntry().setBinding(1).setTextureView(tex_view),
                                           }));
    };
    for (int i = 0; i < 2; ++i)
        state->bloom_src_accum_bg[i] =
            make_bloom_src(("V4DBloomSrcAccum" + std::to_string(i)).c_str(), state->accum_sampled_view[i]);
    state->bloom_src_a_bg = make_bloom_src("V4DBloomSrcA", state->bloom_a_sampled_view);
    state->bloom_src_b_bg = make_bloom_src("V4DBloomSrcB", state->bloom_b_sampled_view);

    auto make_bloom_dst = [&](const char* lbl, wgpu::TextureView& sv) -> wgpu::BindGroup {
        return device->createBindGroup(wgpu::BindGroupDescriptor()
                                           .setLabel(lbl)
                                           .setLayout(state->bloom_out_layout)
                                           .setEntries(std::array{
                                               wgpu::BindGroupEntry().setBinding(0).setTextureView(sv),
                                           }));
    };
    state->bloom_dst_a_bg = make_bloom_dst("V4DBloomDstA", state->bloom_a_storage_view);
    state->bloom_dst_b_bg = make_bloom_dst("V4DBloomDstB", state->bloom_b_storage_view);
}

// ===========================================================================
// Plugin + main
// ===========================================================================

struct Voxel4DPathTracerPlugin {
    void build(core::App& app) {
        app.world_mut().insert_resource(Voxel4DConfig{});
        app.world_mut().insert_resource(Voxel4DCameraState{});
        app.add_systems(core::Startup, into(setup_v4d_scene).set_name("setup 4d scene"));
        app.add_systems(core::Update, into(camera_control_4d).set_name("camera control 4d"));
        app.add_systems(core::Update, into(v4d_imgui_ui).set_name("4d imgui ui"));
    }

    void finish(core::App& app) {
        auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>();
        if (!registry || !server) {
            spdlog::error("[v4d] EmbeddedAssetRegistry or AssetServer not available");
            return;
        }
        registry->get().insert_asset_static("epix/shaders/grid/svo.slang", to_bytes4d(kSvoGridSlangSource));
        registry->get().insert_asset_static(kV4DTraceSlangPath, to_bytes4d(kV4DTraceSlang));
        registry->get().insert_asset_static(kV4DTaaSlangPath, to_bytes4d(kV4DTaaSlang));
        registry->get().insert_asset_static(kV4DBlitVertPath, to_bytes4d(kV4DBlitVertSlang));
        registry->get().insert_asset_static(kV4DBlitFragPath, to_bytes4d(kV4DBlitFragSlang));
        registry->get().insert_asset_static(kV4DBloomSlangPath, to_bytes4d(kV4DBloomSlang));

        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) {
            spdlog::error("[v4d] Render sub-app not found");
            return;
        }
        auto& rapp  = render_app->get();
        auto& world = rapp.world_mut();

        world.insert_resource(Voxel4DShaderHandles{
            .svo_lib   = server->get().load<shader::Shader>("embedded://epix/shaders/grid/svo.slang"),
            .trace     = server->get().load<shader::Shader>("embedded://voxel4d/trace.slang"),
            .taa       = server->get().load<shader::Shader>("embedded://voxel4d/taa.slang"),
            .blit_vert = server->get().load<shader::Shader>("embedded://voxel4d/blit_vert.slang"),
            .blit_frag = server->get().load<shader::Shader>("embedded://voxel4d/blit_frag.slang"),
            .bloom     = server->get().load<shader::Shader>("embedded://voxel4d/bloom.slang"),
        });

        V4DGraph.register_to(world.resource_mut<RenderGraph>());

        world.insert_resource(Voxel4DRenderState{});
        world.insert_resource(ExtractedVoxel4DSvo{});
        world.insert_resource(Voxel4DConfig{});
        world.insert_resource(ExtractedVoxel4DCamera{});

        rapp.add_systems(ExtractSchedule, into(extract_v4d_scene).set_name("extract v4d scene"))
            .add_systems(ExtractSchedule, into(extract_v4d_config).set_name("extract v4d config"))
            .add_systems(ExtractSchedule, into(extract_v4d_camera).set_name("extract v4d camera"))
            .add_systems(Render,
                         into(prepare_v4d_render).in_set(RenderSet::PrepareResources).set_name("prepare v4d render"));
    }
};

int main() {
    core::App app = core::App::create();

    epix::window::Window win;
    win.title = "4D Voxel Path Tracer";
    win.size  = {1280, 720};

    app.add_plugins(epix::window::WindowPlugin{
                        .primary_window = win,
                        .exit_condition = epix::window::ExitCondition::OnPrimaryClosed,
                    })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(tf::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(imgui::ImGuiPlugin{})
        .add_plugins(Voxel4DPathTracerPlugin{});

    app.run();
}
