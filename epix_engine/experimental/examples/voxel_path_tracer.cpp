// Voxel Path Tracer — dedicated 3D sub-graph example
// Uses dense_grid + SVO for scene storage, a custom render-graph sub-graph
// (VoxelGraph) for a 3D perspective camera, compute path tracing + fullscreen blit.

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
// Trace compute shader.
// group 0: 0=VoxelCamera, 1=SVO, 2=colors
// group 1: 0=hdr_tex (rgba16f write), 1=depth_tex (r32f write)
// ---------------------------------------------------------------------------
constexpr std::string_view kVoxelTraceSlangPath = "voxel/trace.slang";
constexpr std::string_view kVoxelTraceSlang     = R"slang(
import epix.ext.grid.svo;

struct VoxelCamera {
    float4x4 inv_proj;
    float4x4 inv_view;
    float4x4 prev_view_proj;
    float4x4 prev_inv_view;
    uint  frame_index;
    float taa_blend;
    uint  _pad0;
    uint  _pad1;
};

[[vk::binding(0, 0)]] ConstantBuffer<VoxelCamera> camera;
[[vk::binding(1, 0)]] StructuredBuffer<uint>   svo_buf;
[[vk::binding(2, 0)]] StructuredBuffer<float4> colors;
[[vk::binding(0, 1)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> hdr_tex;
[[vk::binding(1, 1)]] [[vk::image_format("r32f")]]    RWTexture2D<float>  depth_tex;

// PCG hash - fast, good quality random numbers.
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
uint seed_for(uint2 px, uint frame, uint idx) {
    return pcg(px.x ^ pcg(px.y ^ pcg(frame * 127u + idx * 31u)));
}
float rand01(uint seed) { return float(pcg(seed)) * (1.0f / 4294967296.0f); }

// Procedural sky: gradient + sun disc.
float3 sky_color(float3 dir) {
    float t       = clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    float3 horiz  = float3(0.55f, 0.70f, 0.90f);
    float3 zenit  = float3(0.08f, 0.22f, 0.60f);
    float3 sky    = lerp(horiz, zenit, t);
    // Sun: a clearly bounded disc (~15 deg radius) with a soft penumbra band (~3 deg).
    // High intensity so surfaces in direct sun are much brighter than shadowed ones,
    // making shadow contrast visible without any explicit shadow rays.
    float3 sun_dir = normalize(float3(0.6f, 1.0f, 0.4f));
    float  cos15   = 0.9659f; // cos(15 deg) - inner edge
    float  cos18   = 0.9511f; // cos(18 deg) - outer edge
    float  sun_d   = dot(dir, sun_dir);
    float  disc    = smoothstep(cos18, cos15, sun_d);
    sky += float3(8.0f, 7.0f, 5.5f) * disc;
    return sky;
}

// Hit record used by path tracer.
struct RayHit {
    int    idx;     // voxel colour index; -1 = miss
    float  t;       // entry t (distance along ray to voxel face)
    float3 normal;  // face normal at entry
};

// Convert SvoRayHit axis/sign to float3 normal.
float3 hit_normal3(epix::ext::grid::SvoRayHit<3> h) {
    float3 n = float3(0.0f);
    if (h.hit_axis == 0) n.x = float(h.hit_sign);
    else if (h.hit_axis == 1) n.y = float(h.hit_sign);
    else n.z = float(h.hit_sign);
    return n;
}

// Thin wrapper: call SvoGrid3D member trace_ray and convert to RayHit.
RayHit trace_ray(epix::ext::grid::SvoGrid3D svo, float3 origin, float3 dir, int max_steps) {
    float[3] o = { origin.x, origin.y, origin.z };
    float[3] d = { dir.x, dir.y, dir.z };
    epix::ext::grid::SvoRayHit<3> h = svo.trace_ray(o, d, max_steps);
    RayHit r;
    r.idx    = h.data_index;
    r.t      = h.t;
    r.normal = (h.data_index >= 0) ? hit_normal3(h) : float3(0.0f, 1.0f, 0.0f);
    return r;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void traceMain(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    hdr_tex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    // Subpixel jitter accumulates into spatial AA via TAA.
    float jx = rand01(seed_for(id.xy, camera.frame_index, 0u)) - 0.5f;
    float jy = rand01(seed_for(id.xy, camera.frame_index, 1u)) - 0.5f;

    float2 ndc;
    ndc.x = ((float(id.x) + 0.5f + jx) / float(width))  * 2.0f - 1.0f;
    ndc.y = ((float(id.y) + 0.5f + jy) / float(height)) * 2.0f - 1.0f;

    float4 vr = mul(camera.inv_proj, float4(ndc, 0.0f, 1.0f));
    vr.xyz /= vr.w;
    float3 ray_orig = mul(camera.inv_view, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    float3 ray_dir  = normalize(mul((float3x3)camera.inv_view, normalize(vr.xyz)));
    epix::ext::grid::SvoGrid3D svo = epix::ext::grid::SvoGrid3D(svo_buf);

    // Primary ray.
    RayHit primary = trace_ray(svo, ray_orig, ray_dir, 256);

    float3 radiance;
    if (primary.idx < 0) {
        radiance = sky_color(ray_dir);
        depth_tex[id.xy] = 1e30f;
    } else {
        float3 hit_pos = ray_orig + ray_dir * primary.t;
        float3 albedo  = colors[primary.idx].rgb;
        float3 N       = primary.normal;

        // Build orthonormal basis (Gram-Schmidt) around N for hemisphere sampling.
        float3 up = abs(N.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 T  = normalize(cross(N, up));
        float3 B  = cross(N, T);

        // Cosine-weighted hemisphere sample (Malley's method).
        // PDF = cos(theta)/pi, Lambertian BRDF = albedo/pi
        // -> estimator weight simplifies to just `albedo` (cos and pi cancel).
        float u1  = rand01(seed_for(id.xy, camera.frame_index, 2u));
        float u2  = rand01(seed_for(id.xy, camera.frame_index, 3u));
        float rr  = sqrt(u1);
        float phi = 6.28318f * u2;
        float3 ld     = float3(rr * cos(phi), rr * sin(phi), sqrt(max(0.0f, 1.0f - u1)));
        float3 bounce = normalize(T * ld.x + B * ld.y + N * ld.z);

        // Secondary ray (offset origin to avoid self-intersection on entry face).
        RayHit secondary = trace_ray(svo, hit_pos + N * 0.02f, bounce, 128);

        float3 incoming;
        if (secondary.idx < 0) {
            // Bounce escaped to sky - sample sky radiance in that direction.
            incoming = sky_color(bounce);
        } else {
            // Bounce hit another voxel. Fire a third (cheap) ray: only test sky visibility
            // with a cosine-weighted sample to get a rough indirect contribution.
            float3 h2_albedo = colors[secondary.idx].rgb;
            float3 h2_pos    = (ray_orig + ray_dir * primary.t) + bounce * secondary.t;
            float3 N2        = secondary.normal;
            float3 up2 = abs(N2.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
            float3 T2  = normalize(cross(N2, up2));
            float3 B2  = cross(N2, T2);
            float u3   = rand01(seed_for(id.xy, camera.frame_index, 4u));
            float u4   = rand01(seed_for(id.xy, camera.frame_index, 5u));
            float rr2  = sqrt(u3);
            float phi2 = 6.28318f * u4;
            float3 ld2     = float3(rr2 * cos(phi2), rr2 * sin(phi2), sqrt(max(0.0f, 1.0f - u3)));
            float3 bounce2 = normalize(T2 * ld2.x + B2 * ld2.y + N2 * ld2.z);
            RayHit tertiary = trace_ray(svo, h2_pos + N2 * 0.02f, bounce2, 64);
            float3 in2 = (tertiary.idx < 0) ? sky_color(bounce2)
                                             : colors[tertiary.idx].rgb * float3(0.25f, 0.30f, 0.38f);
            incoming = h2_albedo * in2;
        }

        // Final sample: Lambertian reflectance * incoming (cos/pdf cancel with cosine sampling).
        radiance = albedo * incoming;
        depth_tex[id.xy] = primary.t;
    }

    hdr_tex[id.xy] = float4(radiance, 1.0f);
}
)slang";

// ---------------------------------------------------------------------------
// TAA accumulation compute shader.
// group 0: 0=camera uniform
// group 1: 0=hdr_tex (read), 1=depth_tex (read), 2=prev_accum (read), 3=prev_depth (read)
// group 2: 0=accum_out (rgba16f write)
// ---------------------------------------------------------------------------
constexpr std::string_view kVoxelTaaSlangPath = "voxel/taa.slang";
constexpr std::string_view kVoxelTaaSlang     = R"slang(
struct VoxelCamera {
    float4x4 inv_proj;
    float4x4 inv_view;
    float4x4 prev_view_proj;
    float4x4 prev_inv_view;
    uint  frame_index;
    float taa_blend;
    uint  _pad0;
    uint  _pad1;
};

[[vk::binding(0, 0)]] ConstantBuffer<VoxelCamera> camera;
[[vk::binding(0, 1)]] Texture2D<float4>  hdr_tex;
[[vk::binding(1, 1)]] Texture2D<float>   depth_tex;
[[vk::binding(2, 1)]] Texture2D<float4>  prev_accum;
[[vk::binding(3, 1)]] Texture2D<float>   prev_depth;
[[vk::binding(0, 2)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> accum_out;

// Reconstruct world-space position from depth and inverse matrices.
float3 reproject(float2 uv, float depth, float4x4 inv_proj, float4x4 inv_view) {
    float2 ndc = uv * 2.0f - 1.0f;
    float4 vp  = mul(inv_proj, float4(ndc, 0.0f, 1.0f));
    vp.xyz /= vp.w;
    float3 view_pos = normalize(vp.xyz) * depth;
    return mul(inv_view, float4(view_pos, 1.0f)).xyz;
}

float2 project_to_uv(float3 world_pos, float4x4 prev_view_proj) {
    float4 clip = mul(prev_view_proj, float4(world_pos, 1.0f));
    if (abs(clip.w) < 1e-4f) return float2(-2.0f);

    float2 ndc = clip.xy / clip.w;
    return float2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
}

float3 camera_origin(float4x4 inv_view) {
    return mul(inv_view, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
}

float4 sample_history_4tap(float2 prev_uv, float expected_prev_d, float far_history, uint width, uint height, int2 fallback_px) {
    float2 texel_pos = prev_uv * float2(float(width), float(height)) - 0.5f;
    int2 base_px = int2(floor(texel_pos));
    float2 frac_uv = frac(texel_pos);

    float4 accum = float4(0.0f);
    float weight_sum = 0.0f;
    float depth_window = lerp(0.02f, 0.05f, far_history);

    for (int oy = 0; oy <= 1; ++oy)
        for (int ox = 0; ox <= 1; ++ox) {
            int2 sample_px = clamp(base_px + int2(ox, oy), int2(0, 0), int2(int(width) - 1, int(height) - 1));
            float bilinear_wx = ox == 0 ? (1.0f - frac_uv.x) : frac_uv.x;
            float bilinear_wy = oy == 0 ? (1.0f - frac_uv.y) : frac_uv.y;
            float bilinear_weight = bilinear_wx * bilinear_wy;

            float sample_d = prev_depth.Load(int3(sample_px, 0));
            float depth_diff = abs(sample_d - expected_prev_d) / max(expected_prev_d, 0.1f);
            float depth_weight = saturate(1.0f - depth_diff / depth_window);
            float sample_weight = bilinear_weight * depth_weight;
            accum += prev_accum.Load(int3(sample_px, 0)) * sample_weight;
            weight_sum += sample_weight;
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

    // Reproject: get world-space hit point, then find it in previous frame's UV.
    bool   sky     = cur_d > 1e20f;
    float  blend   = camera.taa_blend; // controlled by VoxelConfig

    if (!sky) {
        float3 world_pos = reproject(uv, cur_d, camera.inv_proj, camera.inv_view);
        float2 prev_uv   = project_to_uv(world_pos, camera.prev_view_proj);
        float3 prev_cam_pos = camera_origin(camera.prev_inv_view);
        float expected_prev_d = length(world_pos - prev_cam_pos);
        float far_history = saturate((expected_prev_d - 24.0f) / 96.0f);

        bool in_bounds   = all(prev_uv >= 0.0f) && all(prev_uv <= 1.0f);
        if (in_bounds) {
            int2 prev_px = int2(prev_uv * float2(float(width), float(height)));
            prev_px = clamp(prev_px, int2(0, 0), int2(int(width) - 1, int(height) - 1));

            float best_score = 1e30f;
            float best_depth_diff = 1e30f;
            float best_px_dist = 1e30f;
            int2 best_px = prev_px;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int2 cand_px = clamp(prev_px + int2(dx, dy), int2(0, 0), int2(int(width) - 1, int(height) - 1));
                    float cand_d = prev_depth.Load(int3(cand_px, 0));
                    float cand_depth_diff = abs(cand_d - expected_prev_d) / max(expected_prev_d, 0.1f);
                    float cand_px_dist = length(float2(cand_px - prev_px));
                    float cand_score = cand_depth_diff + cand_px_dist * 0.02f;
                    if (cand_score < best_score) {
                        best_score = cand_score;
                        best_depth_diff = cand_depth_diff;
                        best_px_dist = cand_px_dist;
                        best_px = cand_px;
                    }
                }

            float max_depth_diff = best_px_dist > 0.0f ? 0.02f : 0.04f;
            if (best_depth_diff < max_depth_diff && best_score < 0.06f) {
                float4 history = sample_history_4tap(prev_uv, expected_prev_d, far_history, width, height, best_px);
                // Neighbourhood colour clamp to avoid ghosting.
                float3 cmin = current.rgb, cmax = current.rgb;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        int2 sample_px = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(int(width) - 1, int(height) - 1));
                        float3 s = hdr_tex.Load(int3(sample_px, 0)).rgb;
                        cmin = min(cmin, s);
                        cmax = max(cmax, s);
                    }
                float3 clamp_pad = max((cmax - cmin) * (0.08f + far_history * 0.17f), float3(0.01f + far_history * 0.03f));
                float4 clamped = float4(clamp(history.rgb, cmin - clamp_pad, cmax + clamp_pad), 1.0f);
                float fallback_blend = lerp(max(blend, 0.3f), max(blend, 0.12f), far_history);
                float confidence = saturate(best_score / 0.06f);
                float effective_blend = lerp(blend, fallback_blend, confidence);
                effective_blend = lerp(effective_blend, blend * 0.35f, far_history * (1.0f - confidence));
                accum_out[id.xy] = lerp(clamped, current, effective_blend);
                return;
            }
        }
    }
    // Disocclusion or sky: full reset to current frame.
    accum_out[id.xy] = current;
}
)slang";

// ---------------------------------------------------------------------------
// Blit vertex shader (unchanged)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Blit vertex shader (fullscreen triangle, explicit float2 UV VBO)
// ---------------------------------------------------------------------------
constexpr std::string_view kVoxelBlitVertPath  = "voxel/blit_vert.slang";
constexpr std::string_view kVoxelBlitVertSlang = R"slang(
struct VOut {
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv;
};
[shader("vertex")]
VOut blitVert([[vk::location(0)]] float2 uv : TEXCOORD0) {
    VOut o;
    o.uv  = uv;
    o.pos = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return o;
}
)slang";

// Blit fragment: composite TAA accum + bloom, Reinhard tone-map, gamma-correct.
constexpr std::string_view kVoxelBlitFragPath  = "voxel/blit_frag.slang";
constexpr std::string_view kVoxelBlitFragSlang = R"slang(
[[vk::binding(0, 0)]] SamplerState blit_sampler;
[[vk::binding(1, 0)]] Texture2D<float4> accum_tex;
[[vk::binding(2, 0)]] Texture2D<float4> bloom_tex;
struct VIn { [[vk::location(0)]] float2 uv; };
[shader("fragment")]
float4 blitFrag(VIn input) : SV_Target {
    uint w, h;
    accum_tex.GetDimensions(w, h);
    int2   px      = int2(input.uv * float2(float(w), float(h)));
    float3 hdr     = accum_tex.Load(int3(px, 0)).rgb;
    float3 bloom   = bloom_tex.Load(int3(px, 0)).rgb;
    // Composite: add bloom (already intensity-scaled) before tone mapping
    float3 combined = hdr + bloom;
    // Reinhard tone mapping
    float3 mapped = combined / (combined + 1.0f);
    // Gamma correction (sRGB approx)
    float3 gamma  = pow(max(mapped, 0.0f), float3(1.0f / 2.2f));
    return float4(gamma, 1.0f);
}
)slang";

// ---------------------------------------------------------------------------
// Bloom compute shader: threshold, horizontal Gaussian blur, vertical Gaussian blur.
// group 0: 0=BloomParams uniform, 1=src_tex (sampled)
// group 1: 0=dst_tex (rgba16f storage write)
// ---------------------------------------------------------------------------
constexpr std::string_view kVoxelBloomSlangPath = "voxel/bloom.slang";
constexpr std::string_view kVoxelBloomSlang     = R"slang(
struct BloomParams {
    float threshold;  // HDR luminance cutoff
    float knee;       // soft-knee half-width
    float intensity;  // final bloom brightness scale
    float enabled;    // 0.0=off (outputs black), 1.0=on
};

[[vk::binding(0, 0)]] ConstantBuffer<BloomParams> bloom;
[[vk::binding(1, 0)]] Texture2D<float4>   src_tex;
[[vk::binding(0, 1)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> dst_tex;

float luminance(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Smooth-step soft-knee: returns [0,1] weight based on luminance vs threshold.
float soft_thresh(float luma) {
    float lo = bloom.threshold - bloom.knee;
    float hi = bloom.threshold + bloom.knee;
    if (luma <= lo) return 0.0f;
    if (luma >= hi) return 1.0f;
    float t = (luma - lo) / max(hi - lo, 1e-5f);
    return t * t * (3.0f - 2.0f * t);
}

// Threshold pass: extract bright pixels into dst_tex.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomThreshMain(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    dst_tex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float4 c = src_tex.Load(int3(id.xy, 0));
    float  f = soft_thresh(luminance(c.rgb)) * bloom.enabled;
    dst_tex[id.xy] = float4(c.rgb * f, 1.0f);
}

// 13-tap Gaussian blur along `dir` (int2(1,0) or int2(0,1)).
// sigma=3, normalization done by wsum so absolute values don't matter.
float3 gaussian_blur(int2 center, int2 dir, uint w, uint h) {
    const float sigma = 3.0f;
    float3 col  = float3(0.0f);
    float  wsum = 0.0f;
    for (int k = -6; k <= 6; ++k) {
        int2  tc = center + dir * k;
        tc.x     = clamp(tc.x, 0, int(w) - 1);
        tc.y     = clamp(tc.y, 0, int(h) - 1);
        float wt = exp(-float(k * k) / (2.0f * sigma * sigma));
        col  += src_tex.Load(int3(tc, 0)).rgb * wt;
        wsum += wt;
    }
    return col / wsum;
}

// Horizontal blur pass.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomBlurHMain(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    dst_tex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    dst_tex[id.xy] = float4(gaussian_blur(int2(id.xy), int2(1, 0), w, h), 1.0f);
}

// Vertical blur pass - also applies bloom.intensity to the final result.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomBlurVMain(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    dst_tex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float3 blurred = gaussian_blur(int2(id.xy), int2(0, 1), w, h);
    dst_tex[id.xy] = float4(blurred * bloom.intensity, 1.0f);
}
)slang";

// ===========================================================================
// C++ types
// ===========================================================================

struct alignas(16) VoxelCameraUniform {
    glm::mat4 inv_proj;
    glm::mat4 inv_view;
    glm::mat4 prev_view_proj;
    glm::mat4 prev_inv_view;
    uint32_t frame_index;
    float taa_blend;
    uint32_t _pad0;
    uint32_t _pad1;
};

struct alignas(16) BloomParamsUniform {
    float threshold;  // HDR luminance cutoff
    float knee;       // soft-knee half-width
    float intensity;  // final bloom scale
    float enabled;    // 0.0=off, 1.0=on
};

// Runtime-configurable rendering / camera parameters (main world + extracted to render world).
struct VoxelConfig {
    float taa_blend    = 0.1f;   // TAA history weight (0=full reset, 1=full history)
    float camera_speed = 20.0f;  // WASD movement speed in world units per second
    // Bloom
    bool bloom_enabled    = true;
    float bloom_threshold = 1.0f;  // HDR luminance cutoff
    float bloom_knee      = 0.5f;  // soft-knee half-width
    float bloom_intensity = 0.2f;  // final bloom scale multiplier
};

struct VoxelScene {
    dense_grid<3, uint8_t> grid;
    std::vector<glm::vec4> palette;
    bool dirty = true;
};

struct ExtractedVoxelSvo {
    std::vector<uint32_t> svo_words;
    std::vector<glm::vec4> colors;
};

struct VoxelShaderHandles {
    assets::Handle<shader::Shader> svo_lib;
    assets::Handle<shader::Shader> trace;
    assets::Handle<shader::Shader> taa;
    assets::Handle<shader::Shader> blit_vert;
    assets::Handle<shader::Shader> blit_frag;
    assets::Handle<shader::Shader> bloom;
};

struct VoxelRenderState {
    wgpu::Buffer svo_buffer;
    wgpu::Buffer color_buffer;
    wgpu::Buffer camera_uniform;
    wgpu::Buffer vertex_buffer;  // 3 float2 UVs for fullscreen triangle
    // HDR radiance from trace pass (rgba16f)
    wgpu::Texture hdr_tex;
    wgpu::TextureView hdr_storage_view;
    wgpu::TextureView hdr_sampled_view;
    // Linear depth from trace pass (r32f)
    wgpu::Texture depth_tex;
    wgpu::TextureView depth_storage_view;
    wgpu::TextureView depth_sampled_view;
    // TAA double-buffered accumulation (rgba16f), ping=0/pong=1
    wgpu::Texture accum_tex[2];
    wgpu::TextureView accum_storage_view[2];
    wgpu::TextureView accum_sampled_view[2];
    // Previous depth (r32f, sampled only — copy from depth_tex at end of TAA)
    wgpu::Texture prev_depth_tex;
    wgpu::TextureView prev_depth_sampled_view;
    wgpu::Sampler linear_sampler;
    glm::uvec2 output_size{0, 0};
    int accum_idx = 0;  // which accum buffer was written last frame
    glm::mat4 prev_inv_view{1.0f};
    // Bloom intermediate textures (rgba16f)
    wgpu::Buffer bloom_params_uniform;
    wgpu::Texture bloom_a_tex;
    wgpu::TextureView bloom_a_storage_view;
    wgpu::TextureView bloom_a_sampled_view;
    wgpu::Texture bloom_b_tex;
    wgpu::TextureView bloom_b_storage_view;
    wgpu::TextureView bloom_b_sampled_view;
    // Bloom bind group layouts
    wgpu::BindGroupLayout bloom_in_layout;   // group 0: params uniform + src texture
    wgpu::BindGroupLayout bloom_out_layout;  // group 1: dst texture (storage write)
    // Bloom bind groups: input (params+src) per pass
    wgpu::BindGroup bloom_src_accum_bg[2];  // src = accum_sampled[0 or 1]
    wgpu::BindGroup bloom_src_a_bg;         // src = bloom_a
    wgpu::BindGroup bloom_src_b_bg;         // src = bloom_b
    // Bloom bind groups: output per pass
    wgpu::BindGroup bloom_dst_a_bg;  // dst = bloom_a (storage)
    wgpu::BindGroup bloom_dst_b_bg;  // dst = bloom_b (storage)
    // Bloom pipeline IDs
    CachedPipelineId bloom_thresh_pipeline_id;
    CachedPipelineId bloom_blur_h_pipeline_id;
    CachedPipelineId bloom_blur_v_pipeline_id;
    // Bind group layouts
    wgpu::BindGroupLayout trace_scene_layout;  // group 0: camera+svo+colors
    wgpu::BindGroupLayout trace_out_layout;    // group 1: hdr_tex+depth_tex (write)
    wgpu::BindGroupLayout taa_in_layout;       // group 1: hdr+depth+prev_accum+prev_depth (read)
    wgpu::BindGroupLayout taa_out_layout;      // group 2: accum_out (write)
    wgpu::BindGroupLayout blit_layout;
    // Bind groups (TAA has two, one per ping-pong direction)
    wgpu::BindGroup trace_scene_bg;
    wgpu::BindGroup trace_out_bg;
    wgpu::BindGroup taa_in_bg[2];   // [i] reads from accum[1-i]
    wgpu::BindGroup taa_out_bg[2];  // [i] writes to accum[i]
    wgpu::BindGroup blit_bg[2];     // [i] blits from accum[i]
    CachedPipelineId trace_pipeline_id;
    CachedPipelineId taa_pipeline_id;
    CachedPipelineId blit_pipeline_id;
    bool resources_created = false;
    bool pipelines_queued  = false;
    bool svo_valid         = false;
    uint32_t frame_count   = 0;
};

// ===========================================================================
// Sub-graph label and node labels
// ===========================================================================

struct VoxelGraphTag {
    void register_to(RenderGraph& g);
};
inline VoxelGraphTag VoxelGraph;
enum class VoxelNode { Trace, Blit };

// ===========================================================================
// Render graph nodes
// ===========================================================================

struct VoxelTraceNode : graph::Node {
    std::optional<QueryState<Item<const ExtractedView&, const ViewTarget&>, Filter<>>> view_qs;

    void update(const World& world) override {
        if (!view_qs)
            view_qs = world.try_query<Item<const ExtractedView&, const ViewTarget&>>();
        else
            view_qs->update_archetypes(world);
    }

    void run(GraphContext& ctx, RenderContext& rc, const World& world) override {
        auto& state = const_cast<VoxelRenderState&>(world.resource<VoxelRenderState>());
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

        VoxelCameraUniform cam;
        cam.inv_proj       = glm::inverse(exview.projection);
        cam.inv_view       = exview.transform.matrix;
        cam.prev_view_proj = exview.projection * glm::inverse(state.prev_inv_view);
        cam.prev_inv_view  = state.prev_inv_view;
        cam.frame_index    = state.frame_count++;
        cam.taa_blend      = world.resource<VoxelConfig>().taa_blend;
        cam._pad0 = cam._pad1 = 0;
        world.resource<wgpu::Queue>().writeBuffer(state.camera_uniform, 0, &cam, sizeof(cam));

        // Trace pass: write hdr_tex + depth_tex
        {
            auto pass = rc.command_encoder().beginComputePass({});
            pass.setPipeline(trace_pl->get().pipeline());
            pass.setBindGroup(0, state.trace_scene_bg, std::span<const uint32_t>{});
            pass.setBindGroup(1, state.trace_out_bg, std::span<const uint32_t>{});
            pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
            pass.end();
        }
        rc.flush_encoder();

        // TAA pass: write into accum_tex[next], read from accum_tex[cur] + hdr + depths
        int next = 1 - state.accum_idx;
        {
            auto pass = rc.command_encoder().beginComputePass({});
            pass.setPipeline(taa_pl->get().pipeline());
            pass.setBindGroup(0, state.trace_scene_bg, std::span<const uint32_t>{});
            pass.setBindGroup(1, state.taa_in_bg[next], std::span<const uint32_t>{});
            pass.setBindGroup(2, state.taa_out_bg[next], std::span<const uint32_t>{});
            pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
            pass.end();
        }
        rc.flush_encoder();

        // Copy current depth -> prev_depth_tex for next frame
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

        // Bloom passes: threshold -> H-blur -> V-blur on the TAA output
        const auto& cfg   = world.resource<VoxelConfig>();
        auto bloom_thr_pl = ps.get_compute_pipeline(state.bloom_thresh_pipeline_id);
        auto bloom_h_pl   = ps.get_compute_pipeline(state.bloom_blur_h_pipeline_id);
        auto bloom_v_pl   = ps.get_compute_pipeline(state.bloom_blur_v_pipeline_id);
        if (bloom_thr_pl && bloom_h_pl && bloom_v_pl) {
            BloomParamsUniform bp;
            bp.threshold = cfg.bloom_threshold;
            bp.knee      = cfg.bloom_knee;
            bp.intensity = cfg.bloom_intensity;
            bp.enabled   = cfg.bloom_enabled ? 1.0f : 0.0f;
            world.resource<wgpu::Queue>().writeBuffer(state.bloom_params_uniform, 0, &bp, sizeof(bp));

            // Threshold: accum[next] -> bloom_a
            {
                auto pass = rc.command_encoder().beginComputePass({});
                pass.setPipeline(bloom_thr_pl->get().pipeline());
                pass.setBindGroup(0, state.bloom_src_accum_bg[next], std::span<const uint32_t>{});
                pass.setBindGroup(1, state.bloom_dst_a_bg, std::span<const uint32_t>{});
                pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
                pass.end();
            }
            rc.flush_encoder();

            // H-blur: bloom_a -> bloom_b
            {
                auto pass = rc.command_encoder().beginComputePass({});
                pass.setPipeline(bloom_h_pl->get().pipeline());
                pass.setBindGroup(0, state.bloom_src_a_bg, std::span<const uint32_t>{});
                pass.setBindGroup(1, state.bloom_dst_b_bg, std::span<const uint32_t>{});
                pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
                pass.end();
            }
            rc.flush_encoder();

            // V-blur (+ intensity scale): bloom_b -> bloom_a
            {
                auto pass = rc.command_encoder().beginComputePass({});
                pass.setPipeline(bloom_v_pl->get().pipeline());
                pass.setBindGroup(0, state.bloom_src_b_bg, std::span<const uint32_t>{});
                pass.setBindGroup(1, state.bloom_dst_a_bg, std::span<const uint32_t>{});
                pass.dispatchWorkgroups((vp.x + 7) / 8, (vp.y + 7) / 8, 1);
                pass.end();
            }
            rc.flush_encoder();
        }

        // Advance ping-pong and save camera for next frame.
        state.accum_idx     = next;
        state.prev_inv_view = exview.transform.matrix;
    }
};

struct VoxelBlitNode : graph::Node {
    std::optional<QueryState<Item<const ViewTarget&>, Filter<>>> view_qs;

    void update(const World& world) override {
        if (!view_qs)
            view_qs = world.try_query<Item<const ViewTarget&>>();
        else
            view_qs->update_archetypes(world);
    }

    void run(GraphContext& ctx, RenderContext& rc, const World& world) override {
        const auto& state = world.resource<VoxelRenderState>();
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

void VoxelGraphTag::register_to(RenderGraph& g) {
    RenderGraph vg;
    vg.add_node(VoxelNode::Trace, VoxelTraceNode{});
    vg.add_node(VoxelNode::Blit, VoxelBlitNode{});
    vg.add_node_edges(VoxelNode::Trace, VoxelNode::Blit);
    if (auto res = g.add_sub_graph(VoxelGraph, std::move(vg)); !res) {
        spdlog::error("[voxel] Failed to register sub-graph");
    }
}

// ===========================================================================
// ECS systems
// ===========================================================================

static std::span<const std::byte> to_bytes(std::string_view sv) {
    return {reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
}

void camera_control(Res<input::ButtonInput<input::KeyCode>> keys,
                    Res<input::ButtonInput<input::MouseButton>> mouse_btns,
                    EventReader<input::MouseMove> mouse_moves,
                    Res<time::Time<>> game_time,
                    Res<VoxelConfig> config,
                    Query<Item<Mut<tf::Transform>>, With<Camera>> cameras) {
    const float dt    = game_time->delta_secs();
    const float speed = config->camera_speed;
    const float sens  = 0.002f;

    double dx = 0.0, dy = 0.0;
    const bool looking = mouse_btns->pressed(input::MouseButton::MouseButtonRight);
    for (auto&& [delta] : mouse_moves.read()) {
        if (looking) {
            dx += delta.first;
            dy += delta.second;
        }
    }

    for (auto&& [transform_mut] : cameras.iter()) {
        auto& tr = transform_mut.get_mut();

        glm::vec3 move{0.0f};
        if (keys->pressed(input::KeyCode::KeyW)) move += tr.local_z();
        if (keys->pressed(input::KeyCode::KeyS)) move -= tr.local_z();
        if (keys->pressed(input::KeyCode::KeyA)) move -= tr.local_x();
        if (keys->pressed(input::KeyCode::KeyD)) move += tr.local_x();
        if (keys->pressed(input::KeyCode::KeySpace)) move.y += 1.0f;
        if (keys->pressed(input::KeyCode::KeyLeftShift)) move.y -= 1.0f;
        if (glm::length(move) > 0.001f) tr.translation += glm::normalize(move) * speed * dt;

        if (dx != 0.0 || dy != 0.0) {
            // Decompose orientation into yaw (world-Y) + pitch (world-X) so
            // the horizon stays stable and roll never accumulates.
            glm::vec3 fwd   = tr.local_z();
            float cur_yaw   = std::atan2f(fwd.x, fwd.z);
            float cur_pitch = std::asinf(glm::clamp(fwd.y, -1.0f, 1.0f));

            // dx>0 (drag right) → yaw right; dy>0 (drag down) → look down
            float new_yaw   = cur_yaw + float(dx) * sens;
            float new_pitch = glm::clamp(cur_pitch - float(dy) * sens, -glm::radians(89.0f), glm::radians(89.0f));

            // Rebuild: positive pitch = look up → negative rotation around world X
            auto yaw_q   = glm::gtc::angleAxis(new_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            auto pitch_q = glm::gtc::angleAxis(-new_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            tr.rotation  = glm::normalize(yaw_q * pitch_q);
        }
    }
}

void voxel_imgui_ui(imgui::Ctx /*imgui_ctx*/, core::ResMut<VoxelConfig> config) {
    ImGui::Begin("Voxel Path Tracer");

    ImGui::SeparatorText("TAA");
    ImGui::SliderFloat("Blend Rate", &config->taa_blend, 0.001f, 1.0f, "%.3f");
    ImGui::SetItemTooltip("0.001 = slow accumulation / low noise, 1.0 = no accumulation");

    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat("Speed", &config->camera_speed, 1.0f, 200.0f, "%.1f");

    ImGui::SeparatorText("Bloom");
    ImGui::Checkbox("Bloom Enabled", &config->bloom_enabled);
    ImGui::SliderFloat("Threshold", &config->bloom_threshold, 0.1f, 5.0f, "%.2f");
    ImGui::SetItemTooltip("HDR luminance cutoff - pixels brighter than this bloom");
    ImGui::SliderFloat("Knee", &config->bloom_knee, 0.0f, 2.0f, "%.2f");
    ImGui::SetItemTooltip("Soft-knee transition width around threshold");
    ImGui::SliderFloat("Intensity", &config->bloom_intensity, 0.0f, 2.0f, "%.2f");
    ImGui::SetItemTooltip("Final bloom brightness multiplier");

    ImGui::End();
}

void extract_voxel_config(Commands cmd, Extract<Res<VoxelConfig>> config) {
    cmd.insert_resource(VoxelConfig{
        .taa_blend       = config->taa_blend,
        .camera_speed    = config->camera_speed,
        .bloom_enabled   = config->bloom_enabled,
        .bloom_threshold = config->bloom_threshold,
        .bloom_knee      = config->bloom_knee,
        .bloom_intensity = config->bloom_intensity,
    });
}

void setup_voxel_scene(Commands cmd) {
    VoxelScene scene{.grid = dense_grid<3, uint8_t>({80u, 48u, 80u})};
    scene.palette = {
        glm::vec4{0.0f},
        glm::vec4{0.55f, 0.55f, 0.55f, 1.0f},  // 1 grey stone
        glm::vec4{0.80f, 0.25f, 0.15f, 1.0f},  // 2 red
        glm::vec4{0.20f, 0.65f, 0.25f, 1.0f},  // 3 green
        glm::vec4{0.85f, 0.75f, 0.25f, 1.0f},  // 4 yellow
    };
    // Ground floor
    for (uint32_t x = 0; x < 80; ++x)
        for (uint32_t z = 0; z < 80; ++z) scene.grid.set({x, 0u, z}, uint8_t(1));
    // 4x4 grid of coloured pillars
    for (uint32_t i = 0; i < 4; ++i)
        for (uint32_t j = 0; j < 4; ++j) {
            uint32_t bx = 6 + i * 16, bz = 6 + j * 16;
            for (uint32_t h = 1; h <= 10; ++h) scene.grid.set({bx, h, bz}, uint8_t(2 + (i + j) % 3));
        }
    cmd.spawn(std::move(scene));

    // Perspective camera, positioned outside the scene looking inward.
    CameraBundle cam = CameraBundle::with_render_graph(VoxelGraph);
    cam.projection   = Projection::perspective(PerspectiveProjection{
        .fov        = glm::radians(65.0f),
        .near_plane = 0.1f,
        .far_plane  = 600.0f,
    });
    // perspectiveLH: view_z = world_z − cam_z must be > 0 for visibility.
    // Scene spans z=0..79; camera must be at z < 0 to see it with identity rotation (+Z forward).
    cam.transform = tf::Transform::from_xyz(40.0f, 15.0f, -30.0f);
    cmd.spawn(std::move(cam));
}

void extract_voxel_scene(Commands cmd, Extract<Query<Item<const VoxelScene&>>> scenes) {
    for (auto&& [scene] : scenes.iter()) {
        auto result = svo_upload(scene.grid);
        if (!result) {
            spdlog::warn("[voxel] svo_upload failed");
            continue;
        }

        std::vector<glm::vec4> colors;
        colors.reserve(scene.grid.count());
        for (auto&& [pos, mat] : scene.grid.iter()) {
            uint8_t m = mat;
            colors.push_back(m < scene.palette.size() ? scene.palette[m] : glm::vec4(1.0f));
        }
        cmd.insert_resource(ExtractedVoxelSvo{
            .svo_words = std::move(result->words),
            .colors    = std::move(colors),
        });
        break;
    }
}

void prepare_voxel_render(Res<wgpu::Device> device,
                          Res<wgpu::Queue> queue,
                          Res<VoxelShaderHandles> handles,
                          ResMut<PipelineServer> pipeline_server,
                          ResMut<VoxelRenderState> state,
                          Res<ExtractedVoxelSvo> svo_res,
                          Query<Item<const ExtractedView&, const ViewTarget&>, With<ExtractedCamera>> views) {
    // -----------------------------------------------------------------------
    // Phase 1: layouts, sampler, static buffers, pipeline kick-offs
    // -----------------------------------------------------------------------
    if (!state->resources_created) {
        // group 0 shared by trace and TAA: camera uniform + SVO + colors
        state->trace_scene_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelSceneBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout()
                                       .setType(wgpu::BufferBindingType::eUniform)
                                       .setMinBindingSize(sizeof(VoxelCameraUniform))),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(2)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eReadOnlyStorage)),
                }));

        // trace group 1: write hdr_tex (rgba16f) + depth_tex (r32f)
        state->trace_out_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelTraceOutBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                               .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                               .setFormat(wgpu::TextureFormat::eRGBA16Float)
                                               .setViewDimension(wgpu::TextureViewDimension::e2D)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                               .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                               .setFormat(wgpu::TextureFormat::eR32Float)
                                               .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        // TAA group 1: read hdr(rgba16f) + depth(r32f) + prev_accum(rgba16f) + prev_depth(r32f)
        state->taa_in_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelTaaInBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(2)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(3)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        // TAA group 2: write accum_out (rgba16f)
        state->taa_out_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelTaaOutBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                               .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                               .setFormat(wgpu::TextureFormat::eRGBA16Float)
                                               .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        // Blit group 0: sampler + accum_tex + bloom_tex (all read as unfiltered float)
        state->blit_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelBlitBGL")
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

        // Bloom group 0: BloomParams uniform + src texture
        state->bloom_in_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelBloomInBGL")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setBuffer(wgpu::BufferBindingLayout()
                                       .setType(wgpu::BufferBindingType::eUniform)
                                       .setMinBindingSize(sizeof(BloomParamsUniform))),
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(1)
                        .setVisibility(wgpu::ShaderStage::eCompute)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }));

        // Bloom group 1: dst storage texture (rgba16f)
        state->bloom_out_layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("VoxelBloomOutBGL")
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
                                     .setLabel("VoxelBloomParams")
                                     .setSize(sizeof(BloomParamsUniform))
                                     .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));

        state->linear_sampler = device->createSampler(wgpu::SamplerDescriptor()
                                                          .setLabel("VoxelLinearSampler")
                                                          .setMagFilter(wgpu::FilterMode::eNearest)
                                                          .setMinFilter(wgpu::FilterMode::eNearest)
                                                          .setMaxAnisotropy(1));

        state->camera_uniform =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("VoxelCameraUniform")
                                     .setSize(sizeof(VoxelCameraUniform))
                                     .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));

        constexpr float kBlitVerts[6] = {0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 2.0f};
        state->vertex_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("VoxelBlitVBO")
                                     .setSize(sizeof(kBlitVerts))
                                     .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        queue->writeBuffer(state->vertex_buffer, 0, kBlitVerts, sizeof(kBlitVerts));

        state->trace_pipeline_id = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "voxel-trace",
            .layouts     = {state->trace_scene_layout, state->trace_out_layout},
            .shader      = handles->trace,
            .entry_point = std::string("traceMain"),
        });

        state->taa_pipeline_id = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "voxel-taa",
            .layouts     = {state->trace_scene_layout, state->taa_in_layout, state->taa_out_layout},
            .shader      = handles->taa,
            .entry_point = std::string("taaMain"),
        });

        state->bloom_thresh_pipeline_id = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "voxel-bloom-thresh",
            .layouts     = {state->bloom_in_layout, state->bloom_out_layout},
            .shader      = handles->bloom,
            .entry_point = std::string("bloomThreshMain"),
        });
        state->bloom_blur_h_pipeline_id = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "voxel-bloom-blur-h",
            .layouts     = {state->bloom_in_layout, state->bloom_out_layout},
            .shader      = handles->bloom,
            .entry_point = std::string("bloomBlurHMain"),
        });
        state->bloom_blur_v_pipeline_id = pipeline_server->queue_compute_pipeline(ComputePipelineDescriptor{
            .label       = "voxel-bloom-blur-v",
            .layouts     = {state->bloom_in_layout, state->bloom_out_layout},
            .shader      = handles->bloom,
            .entry_point = std::string("bloomBlurVMain"),
        });

        state->resources_created = true;
    }

    // -----------------------------------------------------------------------
    // Phase 2: blit render pipeline (needs surface format)
    // -----------------------------------------------------------------------
    if (state->resources_created && !state->pipelines_queued) {
        wgpu::TextureFormat color_fmt = wgpu::TextureFormat::eUndefined;
        for (auto&& [exview, target] : views.iter()) {
            color_fmt = target.format;
            break;
        }
        if (color_fmt != wgpu::TextureFormat::eUndefined) {
            VertexState vs{.shader = handles->blit_vert, .entry_point = std::string("blitVert")};
            vs.buffers.push_back(wgpu::VertexBufferLayout()
                                     .setArrayStride(sizeof(float) * 2)
                                     .setStepMode(wgpu::VertexStepMode::eVertex)
                                     .setAttributes(std::array{
                                         wgpu::VertexAttribute().setShaderLocation(0).setOffset(0).setFormat(
                                             wgpu::VertexFormat::eFloat32x2),
                                     }));
            FragmentState fs{.shader = handles->blit_frag, .entry_point = std::string("blitFrag")};
            fs.add_target(wgpu::ColorTargetState().setFormat(color_fmt).setWriteMask(wgpu::ColorWriteMask::eAll));
            state->blit_pipeline_id = pipeline_server->queue_render_pipeline(RenderPipelineDescriptor{
                .label       = "voxel-blit",
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

    // -----------------------------------------------------------------------
    // Phase 3: upload SVO + color data once
    // -----------------------------------------------------------------------
    if (!state->svo_valid && !svo_res->svo_words.empty()) {
        size_t svo_bytes   = svo_res->svo_words.size() * sizeof(uint32_t);
        size_t color_bytes = std::max(svo_res->colors.size() * sizeof(glm::vec4), sizeof(glm::vec4));
        state->svo_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("VoxelSvoBuffer")
                                     .setSize(svo_bytes)
                                     .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        queue->writeBuffer(state->svo_buffer, 0, svo_res->svo_words.data(), svo_bytes);

        state->color_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("VoxelColorBuffer")
                                     .setSize(color_bytes)
                                     .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        if (!svo_res->colors.empty())
            queue->writeBuffer(state->color_buffer, 0, svo_res->colors.data(),
                               svo_res->colors.size() * sizeof(glm::vec4));
        state->svo_valid = true;
    }
    if (!state->svo_valid) return;

    // -----------------------------------------------------------------------
    // Phase 4: (re-)create per-resolution textures and bind groups
    // -----------------------------------------------------------------------
    glm::uvec2 vp_size{0, 0};
    for (auto&& [exview, target] : views.iter()) {
        vp_size = exview.viewport_size;
        break;
    }
    if (vp_size.x == 0 || vp_size.y == 0 || vp_size == state->output_size) return;

    state->output_size = vp_size;
    wgpu::Extent3D extent{vp_size.x, vp_size.y, 1};

    // Helpers for creating texture + two views (storage + sampled).
    auto make_tex = [&](const char* label, wgpu::TextureFormat fmt, wgpu::TextureView& sv,
                        wgpu::TextureView& tv) -> wgpu::Texture {
        auto tex = device->createTexture(wgpu::TextureDescriptor()
                                             .setLabel(label)
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
        make_tex("VoxelHdrTex", wgpu::TextureFormat::eRGBA16Float, state->hdr_storage_view, state->hdr_sampled_view);
    state->depth_tex =
        make_tex("VoxelDepthTex", wgpu::TextureFormat::eR32Float, state->depth_storage_view, state->depth_sampled_view);
    for (int i = 0; i < 2; ++i) {
        std::string lbl     = "VoxelAccumTex" + std::to_string(i);
        state->accum_tex[i] = make_tex(lbl.c_str(), wgpu::TextureFormat::eRGBA16Float, state->accum_storage_view[i],
                                       state->accum_sampled_view[i]);
    }

    // Bloom intermediate textures (rgba16f, same size as viewport)
    state->bloom_a_tex = make_tex("VoxelBloomA", wgpu::TextureFormat::eRGBA16Float, state->bloom_a_storage_view,
                                  state->bloom_a_sampled_view);
    state->bloom_b_tex = make_tex("VoxelBloomB", wgpu::TextureFormat::eRGBA16Float, state->bloom_b_storage_view,
                                  state->bloom_b_sampled_view);
    {
        // prev_depth: CopyDst + TextureBinding (only copied into, then read)
        auto tex =
            device->createTexture(wgpu::TextureDescriptor()
                                      .setLabel("VoxelPrevDepthTex")
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

    // Scene bind group (shared)
    state->trace_scene_bg = device->createBindGroup(wgpu::BindGroupDescriptor()
                                                        .setLabel("VoxelSceneBG")
                                                        .setLayout(state->trace_scene_layout)
                                                        .setEntries(std::array{
                                                            wgpu::BindGroupEntry()
                                                                .setBinding(0)
                                                                .setBuffer(state->camera_uniform)
                                                                .setOffset(0)
                                                                .setSize(sizeof(VoxelCameraUniform)),
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

    // Trace output bind group
    state->trace_out_bg =
        device->createBindGroup(wgpu::BindGroupDescriptor()
                                    .setLabel("VoxelTraceOutBG")
                                    .setLayout(state->trace_out_layout)
                                    .setEntries(std::array{
                                        wgpu::BindGroupEntry().setBinding(0).setTextureView(state->hdr_storage_view),
                                        wgpu::BindGroupEntry().setBinding(1).setTextureView(state->depth_storage_view),
                                    }));

    // TAA bind groups — taa_in_bg[i] reads from accum[1-i]; taa_out_bg[i] writes to accum[i]
    for (int i = 0; i < 2; ++i) {
        int prev            = 1 - i;
        state->taa_in_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("VoxelTaaInBG" + std::to_string(i)).c_str())
                .setLayout(state->taa_in_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setTextureView(state->hdr_sampled_view),
                    wgpu::BindGroupEntry().setBinding(1).setTextureView(state->depth_sampled_view),
                    wgpu::BindGroupEntry().setBinding(2).setTextureView(state->accum_sampled_view[prev]),
                    wgpu::BindGroupEntry().setBinding(3).setTextureView(state->prev_depth_sampled_view),
                }));
        state->taa_out_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("VoxelTaaOutBG" + std::to_string(i)).c_str())
                .setLayout(state->taa_out_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setTextureView(state->accum_storage_view[i]),
                }));
        state->blit_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("VoxelBlitBG" + std::to_string(i)).c_str())
                .setLayout(state->blit_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setSampler(state->linear_sampler),
                    wgpu::BindGroupEntry().setBinding(1).setTextureView(state->accum_sampled_view[i]),
                    wgpu::BindGroupEntry().setBinding(2).setTextureView(state->bloom_a_sampled_view),
                }));
    }

    // Bloom bind groups:
    // group 0 (src): bloom_params_uniform + respective sampled texture
    // group 1 (dst): respective storage-write texture
    for (int i = 0; i < 2; ++i) {
        state->bloom_src_accum_bg[i] = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel(("VoxelBloomSrcAccum" + std::to_string(i)).c_str())
                .setLayout(state->bloom_in_layout)
                .setEntries(std::array{
                    wgpu::BindGroupEntry()
                        .setBinding(0)
                        .setBuffer(state->bloom_params_uniform)
                        .setOffset(0)
                        .setSize(sizeof(BloomParamsUniform)),
                    wgpu::BindGroupEntry().setBinding(1).setTextureView(state->accum_sampled_view[i]),
                }));
    }
    state->bloom_src_a_bg = device->createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("VoxelBloomSrcA")
            .setLayout(state->bloom_in_layout)
            .setEntries(std::array{
                wgpu::BindGroupEntry()
                    .setBinding(0)
                    .setBuffer(state->bloom_params_uniform)
                    .setOffset(0)
                    .setSize(sizeof(BloomParamsUniform)),
                wgpu::BindGroupEntry().setBinding(1).setTextureView(state->bloom_a_sampled_view),
            }));
    state->bloom_src_b_bg = device->createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("VoxelBloomSrcB")
            .setLayout(state->bloom_in_layout)
            .setEntries(std::array{
                wgpu::BindGroupEntry()
                    .setBinding(0)
                    .setBuffer(state->bloom_params_uniform)
                    .setOffset(0)
                    .setSize(sizeof(BloomParamsUniform)),
                wgpu::BindGroupEntry().setBinding(1).setTextureView(state->bloom_b_sampled_view),
            }));
    state->bloom_dst_a_bg = device->createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("VoxelBloomDstA")
            .setLayout(state->bloom_out_layout)
            .setEntries(std::array{
                wgpu::BindGroupEntry().setBinding(0).setTextureView(state->bloom_a_storage_view),
            }));
    state->bloom_dst_b_bg = device->createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("VoxelBloomDstB")
            .setLayout(state->bloom_out_layout)
            .setEntries(std::array{
                wgpu::BindGroupEntry().setBinding(0).setTextureView(state->bloom_b_storage_view),
            }));
}

// ===========================================================================
// Plugin
// ===========================================================================

struct VoxelPathTracerPlugin {
    void build(core::App& app) {
        app.world_mut().insert_resource(VoxelConfig{});
        app.add_systems(core::Startup, into(setup_voxel_scene).set_name("setup voxel scene"));
        app.add_systems(core::Update, into(camera_control).set_name("camera control"));
        app.add_systems(core::Update, into(voxel_imgui_ui).set_name("voxel imgui ui"));
    }

    void finish(core::App& app) {
        // Register embedded shader assets.
        auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>();
        if (!registry || !server) {
            spdlog::error("[voxel] EmbeddedAssetRegistry or AssetServer not available");
            return;
        }
        registry->get().insert_asset_static("epix/shaders/grid/svo.slang", to_bytes(kSvoGridSlangSource));
        registry->get().insert_asset_static(kVoxelTraceSlangPath, to_bytes(kVoxelTraceSlang));
        registry->get().insert_asset_static(kVoxelTaaSlangPath, to_bytes(kVoxelTaaSlang));
        registry->get().insert_asset_static(kVoxelBlitVertPath, to_bytes(kVoxelBlitVertSlang));
        registry->get().insert_asset_static(kVoxelBlitFragPath, to_bytes(kVoxelBlitFragSlang));
        registry->get().insert_asset_static(kVoxelBloomSlangPath, to_bytes(kVoxelBloomSlang));

        auto render_app = app.get_sub_app_mut(render::Render);
        if (!render_app) {
            spdlog::error("[voxel] Render sub-app not found");
            return;
        }
        auto& rapp  = render_app->get();
        auto& world = rapp.world_mut();

        // All resources that render-world systems depend on go into the render world.
        world.insert_resource(VoxelShaderHandles{
            .svo_lib   = server->get().load<shader::Shader>("embedded://epix/shaders/grid/svo.slang"),
            .trace     = server->get().load<shader::Shader>("embedded://voxel/trace.slang"),
            .taa       = server->get().load<shader::Shader>("embedded://voxel/taa.slang"),
            .blit_vert = server->get().load<shader::Shader>("embedded://voxel/blit_vert.slang"),
            .blit_frag = server->get().load<shader::Shader>("embedded://voxel/blit_frag.slang"),
            .bloom     = server->get().load<shader::Shader>("embedded://voxel/bloom.slang"),
        });

        // Register the custom voxel sub-graph.
        VoxelGraph.register_to(world.resource_mut<RenderGraph>());

        // Initialise resources (ExtractedVoxelSvo must exist for Res<> access).
        world.insert_resource(VoxelRenderState{});
        world.insert_resource(ExtractedVoxelSvo{});
        world.insert_resource(VoxelConfig{});

        rapp.add_systems(ExtractSchedule, into(extract_voxel_scene).set_name("extract voxel scene"))
            .add_systems(ExtractSchedule, into(extract_voxel_config).set_name("extract voxel config"))
            .add_systems(
                Render,
                into(prepare_voxel_render).in_set(RenderSet::PrepareResources).set_name("prepare voxel render"));
    }
};

// ===========================================================================
// main
// ===========================================================================

int main() {
    core::App app = core::App::create();

    epix::window::Window win;
    win.title = "Voxel Path Tracer";
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
        .add_plugins(VoxelPathTracerPlugin{});

    app.run();
}
