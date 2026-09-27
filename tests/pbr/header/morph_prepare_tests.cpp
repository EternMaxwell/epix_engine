#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/pbr/morph.hpp>
#include <epix/render/render_phase.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

using namespace epix::app;

namespace pbr    = epix::pbr;
namespace render = epix::render;

/** @brief Morph weights are padded to the uniform binding's alignment (Bevy
 *  `add_to_alignment`, `render/morph.rs:83-105`).
 *
 *  WebGPU aligns a uniform binding to 256 bytes, and the morph buffer is bound as
 *  one, so every mesh's weights have to end on that boundary whether or not more
 *  weights follow. `f32` is 4 bytes, so 64 elements fill exactly 256. */
TEST(MorphPrepare, AlignmentPadsToTheUniformBinding) {
    render::render_resource::RawBufferVec<float> buffer(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst);

    // Already aligned: nothing is added.
    for (int index = 0; index < 64; ++index) buffer.push(1.0f);
    const std::size_t aligned = buffer.len();
    pbr::add_to_alignment_f32(buffer);
    EXPECT_EQ(buffer.len(), aligned) << "a buffer already on the boundary is left alone";

    // One element past the boundary pads back up to it, with zeros.
    buffer.push(2.0f);
    pbr::add_to_alignment_f32(buffer);
    EXPECT_EQ(buffer.len() % 64, 0u) << "the result is a whole number of 256-byte blocks";
    EXPECT_EQ(buffer.len(), 128u) << "65 elements pad to 128, the next 256-byte boundary";
    // The 65th element is the value pushed above; everything after it is padding.
    for (std::size_t index = aligned + 1; index < buffer.len(); ++index) {
        const float* value = buffer.get(static_cast<std::uint32_t>(index));
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, 0.0f) << "padding is zero, which is what Bevy's T::default() is";
    }
}

/** @brief Morphed meshes are tagged un-batchable, and the tag is not repeated
 *  (Bevy `no_automatic_morph_batching`, `render/morph.rs:147-154`).
 *
 *  Morph targets are read through per-mesh bindings, so two morphed meshes can
 *  never share a batch - and unlike the skin case there is no platform on which
 *  that changes, which is why this system has no device parameter at all. */
TEST(MorphPrepare, NoAutomaticMorphBatchingTagsMorphedMeshes) {
    App app = App::create();

    const epix::ecs::Entity morphed = app.world_mut().spawn(epix::mesh::MorphWeights{}).id();
    const epix::ecs::Entity plain   = app.world_mut().spawn(epix::transform::GlobalTransform{}).id();

    app.add_systems(Update, epix::ecs::into(epix::pbr::no_automatic_morph_batching).set_name("no automatic morph batching"));
    app.run_schedule(Update);

    const auto tagged = [&](epix::ecs::Entity entity) {
        return app.world().entity(entity).get<render::phase::NoAutomaticBatching>().has_value();
    };
    EXPECT_TRUE(tagged(morphed)) << "a morphed mesh cannot be batched";
    EXPECT_FALSE(tagged(plain)) << "a mesh with no morph weights is not tagged";
}
