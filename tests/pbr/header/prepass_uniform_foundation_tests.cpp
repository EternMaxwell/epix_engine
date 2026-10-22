#include "prepass_foundation_fixture.hpp"

namespace {
class PrepassUniformFoundation : public PrepassFoundation {
   protected:
    bool uniform_fallback() const override { return true; }
};

TEST_F(PrepassUniformFoundation, ActualConstrainedDeviceCompilesUniformPrepassBranchMatrix) {
    wgpu::Limits limits{};
    render_world->resource<wgpu::Device>().getLimits(&limits);
    ASSERT_EQ(limits.maxStorageBuffersPerShaderStage, 0u);
    ASSERT_TRUE(render::render_resource::GpuArrayBuffer<pbr::MeshUniform>::batch_size(limits));
    compile_branch_matrix();
}

TEST_F(PrepassUniformFoundation, ActualUniformDrawUsesNonzeroMeshBatchAndViewHistoryOffsets) {
    draw_and_check_outputs();
}

TEST_F(PrepassUniformFoundation, AllVisibilityLayoutsRequireTheWholeUniformArrayWithinDeviceLimits) {
    check_visibility_binding_sizes();
}

TEST_F(PrepassUniformFoundation, ActualConstrainedDeviceCompilesForwardVisibilityDitherAndControl) {
    compile_branch_matrix(true);
}

TEST_F(PrepassUniformFoundation, PreparedViewBindsTheEntireVisibilityRangeTable) { draw_and_check_outputs(true); }

TEST_F(PrepassUniformFoundation, PreparedViewWithoutMotionBindsTheEntireVisibilityRangeTable) {
    draw_and_check_outputs(true, false);
}

TEST_F(PrepassUniformFoundation, CpuModelBindingRetainsTheNativeBufferAndLayoutContract) { check_cpu_model_binding(); }
}  // namespace
