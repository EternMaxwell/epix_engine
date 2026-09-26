#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <epix/sprite_render/mesh2d_pipeline.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace epix::sprite_render;

// Bevy's `Mesh2d` WGSL struct (mesh2d_types.wgsl) is a mat3x4, a mat2x4 plus an
// f32, and two u32 words, padded to the 16-byte array stride that
// GpuArrayBuffer<Mesh2dUniform> uploads.
TEST(Mesh2dUniform, MatchesTheWgslMesh2dStrideAndOffsets) {
    static_assert(sizeof(Mesh2dUniform) == 96);
    static_assert(offsetof(Mesh2dUniform, world_from_local) == 0);
    static_assert(offsetof(Mesh2dUniform, local_from_world_transpose_a) == 48);
    static_assert(offsetof(Mesh2dUniform, local_from_world_transpose_b) == 80);
    static_assert(offsetof(Mesh2dUniform, flags) == 84);
    static_assert(offsetof(Mesh2dUniform, tag) == 88);
    SUCCEED();
}

// Bevy's `Affine3::to_transpose` puts axis i plus translation component i in
// row i, and the inverse-transpose 3x3 is packed so the shader's
// mat2x4_f32_to_mat3x3_unpack rebuilds its columns from (a0.xyz),
// (a0.w, a1.x, a1.y) and (a1.z, a1.w, b).
TEST(Mesh2dUniform, PacksATranslationOnlyTransformAsBevyDoes) {
    Mesh2dTransforms transforms;
    transforms.world_from_local = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -4.0f, 5.0f));
    transforms.flags            = 7;
    const auto uniform          = Mesh2dUniform::from_components(transforms, 42);

    EXPECT_EQ(uniform.world_from_local[0], (glm::vec4{1.0f, 0.0f, 0.0f, 3.0f}));
    EXPECT_EQ(uniform.world_from_local[1], (glm::vec4{0.0f, 1.0f, 0.0f, -4.0f}));
    EXPECT_EQ(uniform.world_from_local[2], (glm::vec4{0.0f, 0.0f, 1.0f, 5.0f}));

    // The inverse-transpose of an identity 3x3 is the identity.
    EXPECT_EQ(uniform.local_from_world_transpose_a[0], (glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(uniform.local_from_world_transpose_a[1], (glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(uniform.local_from_world_transpose_b, 1.0f);
    EXPECT_EQ(uniform.flags, 7u);
    EXPECT_EQ(uniform.tag, 42u);
}

TEST(Mesh2dUniform, PacksTheInverseTransposeOfANonUniformScale) {
    Mesh2dTransforms transforms;
    transforms.world_from_local = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
    const auto uniform          = Mesh2dUniform::from_components(transforms, 0);

    EXPECT_EQ(uniform.world_from_local[0], (glm::vec4{2.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(uniform.world_from_local[1], (glm::vec4{0.0f, 3.0f, 0.0f, 0.0f}));
    EXPECT_EQ(uniform.world_from_local[2], (glm::vec4{0.0f, 0.0f, 4.0f, 0.0f}));

    // A diagonal matrix is its own inverse-transpose.
    EXPECT_FLOAT_EQ(uniform.local_from_world_transpose_a[0].x, 0.5f);
    EXPECT_FLOAT_EQ(uniform.local_from_world_transpose_a[1].x, 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(uniform.local_from_world_transpose_b, 0.25f);
}

TEST(Mesh2dUniform, UnpackRebuildsTheInverseTransposeInShaderColumnOrder) {
    Mesh2dTransforms transforms;
    transforms.world_from_local =
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, -2.0f, 1.0f)) *
        glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 0.5f, 2.0f));
    const auto uniform = Mesh2dUniform::from_components(transforms, 3);

    const glm::mat3 expected = glm::transpose(glm::inverse(glm::mat3(transforms.world_from_local)));
    const glm::mat3 rebuilt  = uniform.unpack_local_from_world_transpose();
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            EXPECT_NEAR(rebuilt[column][row], expected[column][row], 1e-5f)
                << "column " << column << " row " << row;
        }
    }

    // The transposed affine rows rebuild the square of the original matrix.
    const glm::mat4 square{glm::vec4(uniform.world_from_local[0].x, uniform.world_from_local[1].x,
                                     uniform.world_from_local[2].x, 0.0f),
                           glm::vec4(uniform.world_from_local[0].y, uniform.world_from_local[1].y,
                                     uniform.world_from_local[2].y, 0.0f),
                           glm::vec4(uniform.world_from_local[0].z, uniform.world_from_local[1].z,
                                     uniform.world_from_local[2].z, 0.0f),
                           glm::vec4(uniform.world_from_local[0].w, uniform.world_from_local[1].w,
                                     uniform.world_from_local[2].w, 1.0f)};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(square[column][row], transforms.world_from_local[column][row], 1e-5f)
                << "column " << column << " row " << row;
        }
    }
}
