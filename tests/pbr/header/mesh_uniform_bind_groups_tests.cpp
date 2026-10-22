#include "mesh_bind_groups_fixture.hpp"

TEST(MeshBindGroups, EveryVariantRetainsItsUniformModelBatchSlice) { verify_mesh_bind_groups(true); }
