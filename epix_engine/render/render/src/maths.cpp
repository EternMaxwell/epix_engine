#include <spdlog/spdlog.h>

#include <epix/assets.hpp>
#include <epix/render/maths.hpp>
#include <epix/shader.hpp>
#include <span>
#include <string>

namespace epix::render {

namespace {

/**
 * @brief Slang representation of pinned Bevy 0.18 `bevy_render/src/maths.wgsl`.
 *
 * Full square matrices have the same mathematical values as WGSL/GLM and
 * multiply column vectors. Slang constructors and indexing use rows, whereas
 * WGSL uses columns: explicitly transpose constructors made from WGSL columns.
 * The rectangular affine/normal inputs retain their packed vector records;
 * their reconstruction helpers perform that representation conversion once.
 */
constexpr std::string_view kMathsShader = R"slang(
module epix.maths;

namespace epix::maths {

public static const float PI = 3.141592653589793;
public static const float PI_2 = 6.283185307179586;
public static const float HALF_PI = 1.57079632679;
public static const float FRAC_PI_3 = 1.0471975512;
public static const float E = 2.718281828459045;

public float3x3 affine2_to_square(float3x2 affine) {
    return transpose(float3x3(float3(affine[0].xy, 0.0), float3(affine[1].xy, 0.0), float3(affine[2].xy, 1.0)));
}

public float4x4 affine3_to_square(float3x4 affine) {
    // The packed vectors are already rows of the original affine transform.
    return float4x4(affine[0], affine[1], affine[2], float4(0.0, 0.0, 0.0, 1.0));
}

// The `row_major` modifier matches the `Mesh` members this reads: its two rows
// are Bevy's two `mat2x4<f32>` columns, so `a[0]`/`a[1]` are the `vec4`s the
// packed 3x3 is split across.
public float3x3 mat2x4_f32_to_mat3x3_unpack(row_major float2x4 a, float b) {
    return transpose(float3x3(a[0].xyz, float3(a[0].w, a[1].xy), float3(a[1].zw, b)));
}

// Extracts the square portion of an affine matrix: the translation is dropped.
public float3x3 affine3_to_mat3x3(float4x3 affine) {
    return transpose(float3x3(affine[0].xyz, affine[1].xyz, affine[2].xyz));
}

// Returns the inverse of a 3x3 matrix.
public float3x3 inverse_mat3x3(float3x3 matrix) {
    float3 tmp0 = cross(matrix[1], matrix[2]);
    float3 tmp1 = cross(matrix[2], matrix[0]);
    float3 tmp2 = cross(matrix[0], matrix[1]);
    float inv_det = 1.0 / dot(matrix[2], tmp2);
    return transpose(float3x3(tmp0 * inv_det, tmp1 * inv_det, tmp2 * inv_det));
}

// Returns the inverse of an affine matrix.
public float4x3 inverse_affine3(float4x3 affine) {
    float3x3 matrix3 = affine3_to_mat3x3(affine);
    float3x3 inv_matrix3 = inverse_mat3x3(matrix3);
    float3x3 inverse_columns = transpose(inv_matrix3);
    return float4x3(inverse_columns[0], inverse_columns[1], inverse_columns[2], -mul(inv_matrix3, affine[3]));
}

// Extracts the upper 3x3 portion of a 4x4 matrix.
public float3x3 mat4x4_to_mat3x3(float4x4 m) {
    return float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
}

// Copies the sign bit from `b` onto `a`, including for a negative zero, which is
// what makes this match the Rust side of `orthonormalize`.
public float copysign(float a, float b) {
    return asfloat((asuint(a) & 0x7FFFFFFF) | (asuint(b) & 0x80000000));
}

// Constructs a right-handed orthonormal basis from a given unit Z vector.
// Requires unit-length input; the construction has to match the Rust
// implementation in `bevy_light`'s spot-light `world_from_view`.
public float3x3 orthonormalize(float3 z_basis) {
    float sign = copysign(1.0, z_basis.z);
    float a = -1.0 / (sign + z_basis.z);
    float b = z_basis.x * z_basis.y * a;
    float3 x_basis = float3(1.0 + sign * z_basis.x * z_basis.x * a, sign * b, -sign * z_basis.x);
    float3 y_basis = float3(b, sign + z_basis.y * z_basis.y * a, -z_basis.y);
    return transpose(float3x3(x_basis, y_basis, z_basis));
}

// Returns true if any part of a sphere is on the positive side of a plane;
// `sphere_center.w` should be 1.0.
public bool sphere_intersects_plane_half_space(float4 plane, float4 sphere_center, float sphere_radius) {
    return dot(plane, sphere_center) + sphere_radius > 0.0;
}

// Returns the distances along the ray to its intersections with a sphere
// centred at the origin, or `float2(-1.0)` when there is none.
public float2 ray_sphere_intersect(float r, float mu, float sphere_radius) {
    float discriminant = r * r * (mu * mu - 1.0) + sphere_radius * sphere_radius;

    if (discriminant < 0.0) {
        return float2(-1.0, -1.0);
    }

    float q = -r * mu;
    float sqrt_discriminant = sqrt(discriminant);

    return float2(q - sqrt_discriminant, q + sqrt_discriminant);
}

// `pow` but safe for NaNs and negatives.
public float3 powsafe(float3 color, float power) {
    return pow(abs(color), float3(power)) * sign(color);
}

public float3 project_onto(float3 lhs, float3 rhs) {
    float other_len_sq_rcp = 1.0 / dot(rhs, rhs);
    return rhs * dot(lhs, rhs) * other_len_sq_rcp;
}

// Fast approximations of common irrational and trig functions, for raymarching
// where numeric accuracy can be traded for sample count.
public float fast_acos(float in_x) {
    float x = abs(in_x);
    float res = -0.156583 * x + HALF_PI;
    res *= sqrt(1.0 - x);
    return select(in_x >= 0.0, res, PI - res);
}

// Fourth-order polynomial approximation: 7 * 10^-5 radians precision
// (Abramowitz and Stegun, Handbook of Mathematical Functions).
public float fast_acos_4(float x) {
    float x1 = abs(x);
    float x2 = x1 * x1;
    float x3 = x2 * x1;

    float s = -0.2121144 * x1 + 1.5707288;
    s = 0.0742610 * x2 + s;
    s = -0.0187293 * x3 + s;
    s = sqrt(1.0 - x1) * s;

    // acos function mirroring.
    return select(x >= 0.0, s, PI - s);
}

public float fast_atan2(float y, float x) {
    float t0 = max(abs(x), abs(y));
    float t1 = min(abs(x), abs(y));
    float t3 = t1 / t0;
    float t4 = t3 * t3;

    t0 = 0.0872929;
    t0 = t0 * t4 - 0.301895;
    t0 = t0 * t4 + 1.0;
    t3 = t0 * t3;

    t3 = select(abs(y) > abs(x), (0.5 * PI) - t3, t3);
    t3 = select(x < 0, PI - t3, t3);
    t3 = select(y > 0, t3, -t3);

    return t3;
}

}
)slang";

}  // namespace

void register_maths_shader(ecs::World& world) {
    auto registry = world.get_resource_mut<epix::assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<epix::assets::AssetServer>();
    if (!registry || !server) {
        spdlog::warn(
            "[render.maths] EmbeddedAssetRegistry or AssetServer not available. Maths shader library not "
            "registered.");
        return;
    }
    registry->get().insert_asset_static(
        MATHS_SHADER_PATH,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(kMathsShader.data()), kMathsShader.size()));
    // The handle is held for the process, as `ViewPlugin` holds its own: the
    // shader processor resolves `import epix.maths;` through the asset server, so
    // the module has to stay loaded.
    static auto permanent =
        server->get().load<epix::shader::Shader>(std::string("embedded://") + std::string(MATHS_SHADER_PATH));
}

}  // namespace epix::render
